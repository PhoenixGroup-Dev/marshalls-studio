#include "Studio.h"

#include "Log.h"
#include "Planning.h"

#include <algorithm>
#include <QDir>
#include <QStandardPaths>

namespace ms {

Studio::Paths Studio::defaultPaths()
{
	Paths p;
	QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.marshalls-studio");
	p.configRoot = base;
	p.logDir = base + QStringLiteral("/logs");
	p.moduleConfig = base + QStringLiteral("/plugin_config");
	p.secretsDir = base + QStringLiteral("/secrets");
	return p;
}

Studio::Studio(const Paths &paths, QObject *parent)
	: QObject(parent), m_paths(paths), m_store(paths.configRoot), m_secrets(paths.secretsDir), m_engine(this)
{
	m_saveTimer.setSingleShot(true);
	m_saveTimer.setInterval(800);
	connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });

	connect(&m_engine, &Engine::transitionFinished, this, [this] {
		if (!m_program.isTransitioning())
			return;
		m_program.transitionFinished();
		emit transitionStateChanged(false);
		emit programChanged();
	});
	connect(&m_engine, &Engine::fadeToBlackFinished, this, [this] {
		m_program.ftbFinished();
		emit ftbChanged();
	});
	connect(&m_engine, &Engine::mediaEnded, this, &Studio::onMediaEnded);
	connect(&m_engine, &Engine::sourceHealthChanged, this, &Studio::sourceChanged);

	m_rebindTimer.setInterval(3000);
	connect(&m_rebindTimer, &QTimer::timeout, this, &Studio::rebindDevices);
}

Studio::~Studio()
{
	stop();
}

void Studio::emitAlert(AlertLevel l, const QString &m)
{
	if (l == AlertLevel::Error)
		logError("studio", m);
	else if (l == AlertLevel::Warning)
		logWarn("studio", m);
	else
		logInfo("studio", m);
	emit alert(int(l), m);
}

bool Studio::start(const QString &profileName, const EngineInit &initIn, QString *error)
{
	if (m_started)
		return true;
	QString name = profileName;
	if (name.isEmpty())
		name = m_store.lastProfile();
	if (name.isEmpty())
		name = QStringLiteral("Padrão");

	const ConfigStore::LoadResult r = m_store.load(name);
	m_profile = r.profile;
	m_startupNotes = r.message;
	if (!r.message.isEmpty())
		logWarn("config", r.message);
	m_shortcuts.load(m_profile.shortcuts);

	EngineInit init = initIn;
	init.video = m_profile.video;
	if (!m_engine.initialize(init, error))
		return false;

	auto &out = m_engine.outputs();
	connect(&out, &OutputManager::recordingStarted, this, [this](const QString &p) {
		emitAlert(AlertLevel::Info, QStringLiteral("Gravando em %1").arg(p));
		emit recordingChanged(true);
	});
	connect(&out, &OutputManager::recordingStopped, this, [this](const QString &p, bool ok, const QString &err) {
		if (ok)
			emitAlert(err.isEmpty() ? AlertLevel::Info : AlertLevel::Warning,
				  err.isEmpty() ? QStringLiteral("Gravação salva: %1").arg(p) : err);
		else
			emitAlert(AlertLevel::Error, QStringLiteral("Gravação interrompida: %1").arg(err));
		emit recordingChanged(false);
	});
	connect(&out, &OutputManager::remuxFinished, this, [this](const QString &p, bool ok, const QString &err) {
		emitAlert(ok ? AlertLevel::Info : AlertLevel::Error,
			  ok ? QStringLiteral("Cópia MP4 criada: %1").arg(p) : err);
	});
	connect(&out, &OutputManager::streamingStarted, this, [this] {
		emitAlert(AlertLevel::Info, QStringLiteral("Transmissão no ar"));
		emit streamingChanged(true);
	});
	connect(&out, &OutputManager::streamingStopped, this, [this](bool ok, const QString &err) {
		if (!ok)
			emitAlert(AlertLevel::Error, QStringLiteral("Transmissão interrompida: %1").arg(err));
		else
			emitAlert(AlertLevel::Info, QStringLiteral("Transmissão encerrada"));
		emit streamingChanged(false);
	});
	connect(&out, &OutputManager::streamReconnecting, this, [this] {
		emitAlert(AlertLevel::Warning, QStringLiteral("Conexão perdida — reconectando…"));
		emit streamReconnecting(true);
	});
	connect(&out, &OutputManager::streamReconnected, this, [this] {
		emitAlert(AlertLevel::Info, QStringLiteral("Transmissão reconectada"));
		emit streamReconnecting(false);
	});
	connect(&out, &OutputManager::warning, this, [this](const QString &m) { emitAlert(AlertLevel::Warning, m); });
	connect(&out, &OutputManager::statsUpdated, this, &Studio::statsUpdated);

	m_started = true;
	applyProfileToEngine();
	m_store.setLastProfile(m_profile.name);
	m_rebindTimer.start();
	if (r.migrated || r.recoveredFromBackup || !r.fromDisk)
		saveNow();
	emit profileLoaded();
	return true;
}

void Studio::stop()
{
	if (!m_started)
		return;
	m_rebindTimer.stop();
	saveNow();
	m_engine.shutdown();
	m_started = false;
}

void Studio::applyProfileToEngine()
{
	QString err;
	m_engine.thumbnails().setQuality(m_profile.ui.thumbnailWidth, m_profile.ui.thumbnailFps);
	if (!m_profile.ui.monitoringDeviceId.isEmpty())
		m_engine.setMonitoringDevice(m_profile.ui.monitoringDeviceId, m_profile.ui.monitoringDeviceName);
	for (const auto &s : m_profile.sources)
		m_engine.createSource(s, &err);
	for (const auto &sc : m_profile.scenes)
		m_engine.createScene(sc);
	for (const auto &lt : m_profile.lowerThirds)
		m_engine.setLowerThird(lt);
	for (const auto &o : m_profile.overlays)
		m_engine.setOverlay(o);
	m_streamKeyCache = m_secrets.get(m_profile.stream.keyRef);
	if (!m_streamKeyCache.isEmpty())
		Redactor::instance().addSecret(m_streamKeyCache);
	for (const auto &w : audioWarnings())
		emitAlert(AlertLevel::Warning, w);
	emit contentChanged();
	emit programChanged();
	emit graphicsChanged();
}

void Studio::clearEngineContent()
{
	m_engine.clearProgram();
	m_engine.hideAllGraphics();
	for (const auto &lt : m_profile.lowerThirds)
		m_engine.removeLowerThird(lt.uid);
	for (const auto &o : m_profile.overlays)
		m_engine.removeOverlay(o.uid);
	for (const auto &sc : m_profile.scenes)
		m_engine.removeScene(sc.uid);
	for (const auto &s : m_profile.sources)
		m_engine.removeSource(s.uid);
	m_program.reset();
}

int Studio::nextOrder() const
{
	int o = 0;
	for (const auto &s : m_profile.sources)
		o = std::max(o, s.order + 1);
	for (const auto &s : m_profile.scenes)
		o = std::max(o, s.order + 1);
	return o;
}

// ---------------------------------------------------------------------------
// content
// ---------------------------------------------------------------------------

QString Studio::addSource(SourceConfig cfg, QString *error)
{
	if (cfg.uid.isEmpty())
		cfg.uid = newUid("src");
	cfg.name = m_profile.uniqueName(cfg.name.trimmed().isEmpty() ? kindLabel(cfg.kind) : cfg.name.trimmed());
	if (!ObsPlatform::isKindSupported(cfg.kind)) {
		if (error)
			*error = ObsPlatform::unsupportedReason(cfg.kind);
		return {};
	}
	const QString dup = m_profile.sourceUsingDevice(cfg.kind, cfg.device);
	if (!dup.isEmpty() && cfg.kind != SourceKind::Screen) {
		if (error)
			*error = QStringLiteral("Este dispositivo já está em uso pela fonte “%1”. "
						"Capturá-lo duas vezes duplica o áudio/vídeo e pode travar o driver.")
					 .arg(m_profile.findSource(dup)->name);
		return {};
	}
	cfg.order = nextOrder();
	if (cfg.kind == SourceKind::AudioInput || cfg.kind == SourceKind::AudioOutput || cfg.kind == SourceKind::Media)
		cfg.hasAudio = true;
	QString err;
	if (!m_engine.createSource(cfg, &err)) {
		if (error)
			*error = err;
		return {};
	}
	m_profile.sources.append(cfg);
	if (!err.isEmpty())
		emitAlert(AlertLevel::Warning, err);
	for (const auto &w : audioWarnings())
		emitAlert(AlertLevel::Warning, w);
	markDirty();
	emit contentChanged();
	return cfg.uid;
}

bool Studio::updateSource(const SourceConfig &cfg)
{
	SourceConfig *cur = m_profile.findSource(cfg.uid);
	if (!cur)
		return false;
	if (!(cur->device == cfg.device)) {
		const QString dup = m_profile.sourceUsingDevice(cfg.kind, cfg.device, cfg.uid);
		if (!dup.isEmpty() && cfg.kind != SourceKind::Screen) {
			emitAlert(AlertLevel::Error, QStringLiteral("Dispositivo já em uso por “%1”.")
							     .arg(m_profile.findSource(dup)->name));
			return false;
		}
	}
	*cur = cfg;
	m_engine.updateSource(cfg);
	markDirty();
	emit sourceChanged(cfg.uid);
	return true;
}

void Studio::removeSource(const QString &uid)
{
	const SourceConfig *s = m_profile.findSource(uid);
	if (!s)
		return;
	const QString name = s->name;
	const bool onAir = m_program.contentRemoved(ContentRef::source(uid));
	if (onAir)
		m_engine.clearProgram();
	m_engine.removeSource(uid);
	m_profile.removeSource(uid);
	for (const auto &sc : m_profile.scenes)
		m_engine.updateScene(sc);
	markDirty();
	emitAlert(onAir ? AlertLevel::Warning : AlertLevel::Info,
		  onAir ? QStringLiteral("“%1” estava no ar e foi removida: programa em preto.").arg(name)
			: QStringLiteral("Fonte “%1” removida.").arg(name));
	emit contentChanged();
	emit programChanged();
	emit selectionChanged();
}

void Studio::renameSource(const QString &uid, const QString &name)
{
	SourceConfig *s = m_profile.findSource(uid);
	if (!s || name.trimmed().isEmpty() || s->name == name.trimmed())
		return;
	s->name = m_profile.uniqueName(name.trimmed());
	markDirty();
	emit sourceChanged(uid);
	emit contentChanged();
}

QString Studio::addScene(SceneConfig cfg)
{
	if (cfg.uid.isEmpty())
		cfg.uid = newUid("scn");
	cfg.name = m_profile.uniqueName(cfg.name.trimmed().isEmpty() ? QStringLiteral("Composição") : cfg.name.trimmed());
	cfg.order = nextOrder();
	m_profile.scenes.append(cfg);
	m_engine.createScene(cfg);
	markDirty();
	emit contentChanged();
	return cfg.uid;
}

void Studio::updateScene(const SceneConfig &cfg)
{
	SceneConfig *cur = m_profile.findScene(cfg.uid);
	if (!cur)
		return;
	*cur = cfg;
	m_engine.updateScene(cfg);
	markDirty();
	emit contentChanged();
}

void Studio::removeScene(const QString &uid)
{
	const SceneConfig *sc = m_profile.findScene(uid);
	if (!sc)
		return;
	const QString name = sc->name;
	const bool onAir = m_program.contentRemoved(ContentRef::scene(uid));
	if (onAir)
		m_engine.clearProgram();
	m_engine.removeScene(uid);
	m_profile.scenes.erase(std::remove_if(m_profile.scenes.begin(), m_profile.scenes.end(),
					      [&](const SceneConfig &s) { return s.uid == uid; }),
			       m_profile.scenes.end());
	markDirty();
	emitAlert(onAir ? AlertLevel::Warning : AlertLevel::Info,
		  onAir ? QStringLiteral("“%1” estava no ar e foi excluída: programa em preto.").arg(name)
			: QStringLiteral("Composição “%1” excluída.").arg(name));
	emit contentChanged();
	emit programChanged();
	emit selectionChanged();
}

QString Studio::duplicateScene(const QString &uid)
{
	const SceneConfig *sc = m_profile.findScene(uid);
	if (!sc)
		return {};
	SceneConfig copy = *sc;
	copy.uid.clear();
	copy.name = sc->name + QStringLiteral(" (cópia)");
	return addScene(copy);
}

void Studio::renameScene(const QString &uid, const QString &name)
{
	SceneConfig *sc = m_profile.findScene(uid);
	if (!sc || name.trimmed().isEmpty() || sc->name == name.trimmed())
		return;
	sc->name = m_profile.uniqueName(name.trimmed());
	markDirty();
	emit contentChanged();
}

QVector<ContentRef> Studio::gridOrder() const
{
	struct E {
		int order;
		int type;
		ContentRef ref;
	};
	QVector<E> v;
	for (const auto &s : m_profile.sources)
		if (kindHasVideo(s.kind))
			v.append({s.order, 0, ContentRef::source(s.uid)});
	for (const auto &s : m_profile.scenes)
		v.append({s.order, 1, ContentRef::scene(s.uid)});
	std::stable_sort(v.begin(), v.end(), [](const E &a, const E &b) {
		return a.order != b.order ? a.order < b.order : a.type < b.type;
	});
	QVector<ContentRef> out;
	for (const auto &e : v)
		out.append(e.ref);
	return out;
}

void Studio::moveInGrid(const ContentRef &ref, int newIndex)
{
	QVector<ContentRef> order = gridOrder();
	const int from = order.indexOf(ref);
	if (from < 0)
		return;
	newIndex = std::clamp(newIndex, 0, int(order.size()) - 1);
	if (from == newIndex)
		return;
	order.move(from, newIndex);
	for (int i = 0; i < order.size(); ++i) {
		if (order[i].type == ContentRef::Type::Source) {
			if (auto *s = m_profile.findSource(order[i].uid))
				s->order = i;
		} else if (auto *s = m_profile.findScene(order[i].uid)) {
			s->order = i;
		}
	}
	markDirty();
	emit contentChanged();
}

QString Studio::contentName(const ContentRef &ref) const
{
	if (ref.type == ContentRef::Type::Source) {
		const SourceConfig *s = m_profile.findSource(ref.uid);
		return s ? s->name : QString();
	}
	if (ref.type == ContentRef::Type::Scene) {
		const SceneConfig *s = m_profile.findScene(ref.uid);
		return s ? s->name : QString();
	}
	return {};
}

bool Studio::isOnProgram(const ContentRef &ref) const
{
	return m_program.program() == ref;
}

QVector<QString> Studio::audioChannels() const
{
	QVector<const SourceConfig *> v;
	for (const auto &s : m_profile.sources)
		if (s.hasAudio)
			v.append(&s);
	std::stable_sort(v.begin(), v.end(), [](const SourceConfig *a, const SourceConfig *b) {
		auto rank = [](SourceKind k) { return k == SourceKind::AudioInput ? 0 : k == SourceKind::AudioOutput ? 1 : 2; };
		if (rank(a->kind) != rank(b->kind))
			return rank(a->kind) < rank(b->kind);
		return a->order < b->order;
	});
	QVector<QString> out;
	for (auto *s : v)
		out.append(s->uid);
	return out;
}

// ---------------------------------------------------------------------------
// program
// ---------------------------------------------------------------------------

void Studio::select(const ContentRef &ref)
{
	if (m_program.selected() == ref)
		return;
	m_program.select(ref);
	emit selectionChanged();
}

TakeDecision Studio::executeTake(const TakeDecision &d)
{
	if (!d.accepted) {
		if (!d.reason.isEmpty() && d.reason != QLatin1String("Já está no ar"))
			emitAlert(AlertLevel::Info, d.reason);
		return d;
	}
	if (!m_engine.take(d)) {
		TakeDecision failed = d;
		failed.accepted = false;
		failed.reason = QStringLiteral("O motor não executou a troca.");
		emitAlert(AlertLevel::Error, failed.reason);
		return failed;
	}
	if (!d.reason.isEmpty())
		emitAlert(AlertLevel::Warning, d.reason);
	if (d.isCut()) {
		m_program.cutApplied(d.target);
		emit programChanged();
	} else {
		m_program.transitionStarted(d.target);
		emit transitionStateChanged(true);
		emit programChanged();
	}
	return d;
}

TakeDecision Studio::take(TakeMode mode)
{
	return executeTake(m_program.requestTake(mode, m_profile.transition));
}

TakeDecision Studio::takeContent(const ContentRef &ref, TakeMode mode)
{
	return executeTake(m_program.requestTake(mode, m_profile.transition, ref));
}

TakeDecision Studio::takeSlot(int slot, TakeMode mode)
{
	const auto order = gridOrder();
	if (slot < 1 || slot > order.size())
		return {};
	select(order[slot - 1]);
	return takeContent(order[slot - 1], mode);
}

void Studio::selectSlot(int slot)
{
	const auto order = gridOrder();
	if (slot >= 1 && slot <= order.size())
		select(order[slot - 1]);
}

TakeDecision Studio::toggleFadeToBlack()
{
	const bool toBlack = m_program.ftbState() == FtbState::Off;
	TakeDecision d = m_program.requestFadeToBlack(m_profile.transition.ftbDurationMs);
	if (!d.accepted) {
		emitAlert(AlertLevel::Info, d.reason);
		return d;
	}
	m_engine.fadeToBlack(d, toBlack);
	emit ftbChanged();
	return d;
}

void Studio::setTransition(const TransitionSettings &t)
{
	m_profile.transition = t;
	markDirty();
	emit settingsChanged();
}

void Studio::onMediaEnded(const QString &uid)
{
	const SourceConfig *s = m_profile.findSource(uid);
	if (!s)
		return;
	emit sourceChanged(uid);
	if (s->media.endAction == MediaEndAction::ReturnPrevious && !s->media.loop &&
	    m_program.program() == ContentRef::source(uid)) {
		const TakeDecision d = m_program.requestReturnToPrevious(m_profile.transition);
		if (d.accepted) {
			logInfo("program", QStringLiteral("Fim de “%1”: voltando ao conteúdo anterior").arg(s->name));
			executeTake(d);
		}
	}
}

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------

void Studio::setVolumeDb(const QString &uid, double db)
{
	if (SourceConfig *s = m_profile.findSource(uid)) {
		s->audio.volumeDb = std::clamp(db, -100.0, 26.0);
		m_engine.setVolumeDb(uid, s->audio.volumeDb);
		markDirty();
	}
}

void Studio::setMuted(const QString &uid, bool muted)
{
	if (SourceConfig *s = m_profile.findSource(uid)) {
		s->audio.muted = muted;
		m_engine.setMuted(uid, muted);
		markDirty();
		emit sourceChanged(uid);
	}
}

void Studio::toggleMute(const QString &uid)
{
	if (const SourceConfig *s = m_profile.findSource(uid))
		setMuted(uid, !s->audio.muted);
}

void Studio::setSyncOffset(const QString &uid, int ms)
{
	if (SourceConfig *s = m_profile.findSource(uid)) {
		s->audio.syncOffsetMs = std::clamp(ms, -950, 20000);
		m_engine.setSyncOffsetMs(uid, s->audio.syncOffsetMs);
		markDirty();
		emit sourceChanged(uid);
	}
}

void Studio::setMonitor(const QString &uid, MonitorMode m)
{
	if (SourceConfig *s = m_profile.findSource(uid)) {
		s->audio.monitor = m;
		m_engine.setMonitoring(uid, m);
		markDirty();
		for (const auto &w : audioWarnings())
			emitAlert(AlertLevel::Warning, w);
		emit sourceChanged(uid);
	}
}

void Studio::setFilters(const QString &uid, const AudioFilters &f)
{
	if (SourceConfig *s = m_profile.findSource(uid)) {
		s->audio.filters = f;
		m_engine.setAudioFilters(uid, f);
		markDirty();
		emit sourceChanged(uid);
	}
}

void Studio::setMonitoringDevice(const QString &id, const QString &name)
{
	m_profile.ui.monitoringDeviceId = id;
	m_profile.ui.monitoringDeviceName = name;
	m_engine.setMonitoringDevice(id, name);
	markDirty();
}

QStringList Studio::audioWarnings() const
{
	return audioSafetyWarnings(m_profile);
}

// ---------------------------------------------------------------------------
// graphics
// ---------------------------------------------------------------------------

QString Studio::addLowerThird(LowerThirdConfig cfg)
{
	if (cfg.uid.isEmpty())
		cfg.uid = newUid("lt");
	int order = 0;
	for (const auto &l : m_profile.lowerThirds)
		order = std::max(order, l.order + 1);
	cfg.order = order;
	m_profile.lowerThirds.append(cfg);
	m_engine.setLowerThird(cfg);
	markDirty();
	emit graphicsChanged();
	return cfg.uid;
}

void Studio::updateLowerThird(const LowerThirdConfig &cfg)
{
	if (LowerThirdConfig *cur = m_profile.findLowerThird(cfg.uid)) {
		*cur = cfg;
		m_engine.setLowerThird(cfg);
		markDirty();
		emit graphicsChanged();
	}
}

void Studio::removeLowerThird(const QString &uid)
{
	m_engine.removeLowerThird(uid);
	m_profile.lowerThirds.erase(std::remove_if(m_profile.lowerThirds.begin(), m_profile.lowerThirds.end(),
						   [&](const LowerThirdConfig &l) { return l.uid == uid; }),
				    m_profile.lowerThirds.end());
	markDirty();
	emit graphicsChanged();
}

void Studio::showLowerThird(const QString &uid, bool show)
{
	m_engine.showLowerThird(uid, show);
	emit graphicsChanged();
}

void Studio::toggleLowerThird(const QString &uid)
{
	const GraphicPhase ph = m_engine.lowerThirdPhase(uid);
	showLowerThird(uid, ph == GraphicPhase::Hidden || ph == GraphicPhase::Out);
}

void Studio::toggleLowerThirdSlot(int slot)
{
	QVector<const LowerThirdConfig *> v;
	for (const auto &l : m_profile.lowerThirds)
		v.append(&l);
	std::stable_sort(v.begin(), v.end(), [](auto *a, auto *b) { return a->order < b->order; });
	if (slot >= 1 && slot <= v.size())
		toggleLowerThird(v[slot - 1]->uid);
}

QString Studio::addOverlay(OverlayImageConfig cfg)
{
	if (cfg.uid.isEmpty())
		cfg.uid = newUid("ovl");
	m_profile.overlays.append(cfg);
	m_engine.setOverlay(cfg);
	markDirty();
	emit graphicsChanged();
	return cfg.uid;
}

void Studio::updateOverlay(const OverlayImageConfig &cfg)
{
	if (OverlayImageConfig *cur = m_profile.findOverlay(cfg.uid)) {
		*cur = cfg;
		m_engine.setOverlay(cfg);
		markDirty();
		emit graphicsChanged();
	}
}

void Studio::removeOverlay(const QString &uid)
{
	m_engine.removeOverlay(uid);
	m_profile.overlays.erase(std::remove_if(m_profile.overlays.begin(), m_profile.overlays.end(),
						[&](const OverlayImageConfig &o) { return o.uid == uid; }),
				 m_profile.overlays.end());
	markDirty();
	emit graphicsChanged();
}

void Studio::toggleOverlay(const QString &uid)
{
	m_engine.showOverlay(uid, !m_engine.overlayVisible(uid));
	emit graphicsChanged();
}

void Studio::clearGraphics()
{
	m_engine.hideAllGraphics();
	emit graphicsChanged();
}

// ---------------------------------------------------------------------------
// outputs
// ---------------------------------------------------------------------------

bool Studio::startRecording(QString *error)
{
	QString err;
	const bool ok = m_engine.outputs().startRecording(m_profile.recording, &err);
	if (!ok) {
		emitAlert(AlertLevel::Error, err);
		if (error)
			*error = err;
	}
	return ok;
}

void Studio::stopRecording()
{
	m_engine.outputs().stopRecording();
}

bool Studio::isRecording() const
{
	return const_cast<Studio *>(this)->m_engine.outputs().isRecording();
}

bool Studio::startStreaming(QString *error)
{
	QString err;
	const bool ok = m_engine.outputs().startStreaming(m_profile.stream, m_streamKeyCache, &err);
	if (!ok) {
		emitAlert(AlertLevel::Error, err);
		if (error)
			*error = err;
	} else {
		emit streamingChanged(true);
	}
	return ok;
}

void Studio::stopStreaming()
{
	m_engine.outputs().stopStreaming();
}

bool Studio::isStreaming() const
{
	return const_cast<Studio *>(this)->m_engine.outputs().isStreaming();
}

void Studio::setStreamKey(const QString &key)
{
	if (!m_streamKeyCache.isEmpty())
		Redactor::instance().removeSecret(m_streamKeyCache);
	m_streamKeyCache = key.trimmed();
	Redactor::instance().addSecret(m_streamKeyCache);
	QString err;
	if (!m_secrets.set(m_profile.stream.keyRef, m_streamKeyCache, &err))
		emitAlert(AlertLevel::Error, QStringLiteral("Não foi possível salvar a chave: %1").arg(err));
}

QString Studio::streamKey() const
{
	return m_streamKeyCache;
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

bool Studio::applyVideo(const VideoSettings &v, QString *error)
{
	if (v == m_profile.video)
		return true;
	if (m_engine.outputs().anyActive()) {
		if (error)
			*error = QStringLiteral("Encerre a gravação e a transmissão antes de alterar o vídeo.");
		return false;
	}
	if (!m_engine.resetVideo(v, error)) {
		QString ignore;
		m_engine.resetVideo(m_profile.video, &ignore); // keep the previous working setup
		return false;
	}
	m_profile.video = v;
	m_engine.thumbnails().setQuality(m_profile.ui.thumbnailWidth, m_profile.ui.thumbnailFps);
	markDirty();
	emit settingsChanged();
	return true;
}

void Studio::setRecordingSettings(const RecordingSettings &r)
{
	m_profile.recording = r;
	markDirty();
	emit settingsChanged();
}

void Studio::setStreamSettings(const StreamSettings &s)
{
	m_profile.stream = s;
	markDirty();
	emit settingsChanged();
}

void Studio::setUiSettings(const UiSettings &u)
{
	const bool thumbs = u.thumbnailWidth != m_profile.ui.thumbnailWidth || u.thumbnailFps != m_profile.ui.thumbnailFps;
	m_profile.ui = u;
	if (thumbs)
		m_engine.thumbnails().setQuality(u.thumbnailWidth, u.thumbnailFps);
	markDirty();
	emit settingsChanged();
}

void Studio::setShortcuts(const QMap<QString, QString> &overrides)
{
	m_shortcuts.load(overrides);
	m_profile.shortcuts = m_shortcuts.overrides();
	markDirty();
	emit settingsChanged();
}

void Studio::markDirty()
{
	m_saveTimer.start();
}

bool Studio::saveNow(QString *error)
{
	m_saveTimer.stop();
	QString err;
	const bool ok = m_store.save(m_profile, &err);
	if (!ok) {
		emitAlert(AlertLevel::Error, QStringLiteral("Falha ao salvar a configuração: %1").arg(err));
		if (error)
			*error = err;
	}
	return ok;
}

QStringList Studio::profiles() const
{
	QStringList l = m_store.listProfiles();
	if (!l.contains(m_profile.name))
		l.prepend(m_profile.name);
	return l;
}

bool Studio::switchProfile(const QString &name, QString *error)
{
	if (name == m_profile.name)
		return true;
	if (m_engine.outputs().anyActive()) {
		if (error)
			*error = QStringLiteral("Encerre a gravação e a transmissão antes de trocar de perfil.");
		return false;
	}
	saveNow();
	clearEngineContent();
	const ConfigStore::LoadResult r = m_store.load(name);
	m_profile = r.profile;
	m_profile.name = name;
	m_shortcuts.load(m_profile.shortcuts);
	QString verr;
	if (!m_engine.resetVideo(m_profile.video, &verr))
		emitAlert(AlertLevel::Error, verr);
	applyProfileToEngine();
	m_store.setLastProfile(name);
	if (!r.message.isEmpty())
		emitAlert(AlertLevel::Warning, r.message);
	saveNow();
	emit profileLoaded();
	emit selectionChanged();
	return true;
}

bool Studio::createProfile(const QString &name, bool copyCurrent, QString *error)
{
	if (name.trimmed().isEmpty() || m_store.listProfiles().contains(name.trimmed())) {
		if (error)
			*error = QStringLiteral("Nome inválido ou já existente.");
		return false;
	}
	StudioProfile p = copyCurrent ? m_profile : StudioProfile{};
	p.name = name.trimmed();
	if (!m_store.save(p, error))
		return false;
	return switchProfile(p.name, error);
}

// Re-bind a device whose identifier changed (e.g. moved to another USB port)
// when a device with the same name appears and the stored id is gone.
void Studio::rebindDevices()
{
	for (auto &s : m_profile.sources) {
		if (s.device.id.isEmpty() || s.device.name.isEmpty() || s.device.id == QLatin1String("default"))
			continue;
		bool known = false;
		const auto devices = Engine::cachedDevices(s.kind, &known);
		if (!known)
			continue;
		bool present = false;
		for (const auto &d : devices)
			present |= d.id == s.device.id;
		if (present)
			continue;
		for (const auto &d : devices) {
			if (d.name == s.device.name && m_profile.sourceUsingDevice(s.kind, {d.id, d.name}, s.uid).isEmpty()) {
				logInfo("devices", QStringLiteral("“%1”: identificador do dispositivo mudou; reassociado a %2")
							   .arg(s.name, d.id));
				s.device.id = d.id;
				m_engine.updateSource(s);
				markDirty();
				emit sourceChanged(s.uid);
				break;
			}
		}
	}
}

} // namespace ms
