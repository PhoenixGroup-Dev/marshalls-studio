#include "Engine.h"

#include "Log.h"
#include "Outputs.h"
#include "Planning.h"
#include "RtmpService.h"

#include <obs.h>
#include <obs-module.h>
#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif
#include <util/base.h>
#include <util/platform.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <cstdarg>
#include <functional>
#include <cstdio>

#ifdef _WIN32
#include <objbase.h>
#endif

namespace ms {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

QString healthLabel(SourceHealth h)
{
	switch (h) {
	case SourceHealth::Ok: return QStringLiteral("OK");
	case SourceHealth::Starting: return QStringLiteral("Iniciando");
	case SourceHealth::NoSignal: return QStringLiteral("Sem sinal");
	case SourceHealth::Disconnected: return QStringLiteral("Desconectado");
	case SourceHealth::Error: return QStringLiteral("Erro");
	case SourceHealth::Unsupported: return QStringLiteral("Indisponível");
	}
	return {};
}

namespace {

qint64 nowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

QByteArray utf8(const QString &s)
{
	return s.toUtf8();
}

void obsLogHandler(int lvl, const char *fmt, va_list args, void *)
{
	char buf[4096];
	va_list copy;
	va_copy(copy, args);
	vsnprintf(buf, sizeof(buf), fmt, copy);
	va_end(copy);
	LogLevel level = LogLevel::Info;
	if (lvl <= LOG_ERROR)
		level = LogLevel::Error;
	else if (lvl <= LOG_WARNING)
		level = LogLevel::Warning;
	else if (lvl >= LOG_DEBUG)
		level = LogLevel::Debug;
	Log::instance().write(level, QStringLiteral("obs"), QString::fromUtf8(buf));
}

void obsCrashHandler(const char *fmt, va_list args, void *)
{
	char buf[8192];
	vsnprintf(buf, sizeof(buf), fmt, args);
	Log::instance().write(LogLevel::Error, QStringLiteral("crash"), QString::fromUtf8(buf));
	Log::instance().close();
	abort();
}

// Disconnects a libobs signal on destruction.
struct SignalConn {
	signal_handler_t *handler = nullptr;
	const char *name = nullptr;
	signal_callback_t cb = nullptr;
	void *data = nullptr;
	void connect(obs_source_t *src, const char *sig, signal_callback_t c, void *d)
	{
		handler = obs_source_get_signal_handler(src);
		name = sig;
		cb = c;
		data = d;
		signal_handler_connect(handler, name, cb, data);
	}
	void disconnect()
	{
		if (handler)
			signal_handler_disconnect(handler, name, cb, data);
		handler = nullptr;
	}
};

obs_data_t *dataFromJson(const QJsonObject &o)
{
	if (o.isEmpty())
		return obs_data_create();
	const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
	obs_data_t *d = obs_data_create_from_json(json.constData());
	return d ? d : obs_data_create();
}

// ---- frame probe: passive async video filter counting delivered frames -----

struct ProbeData {
	std::atomic<quint64> *counter = nullptr;
};

const char *probeName(void *)
{
	return "Marshalls frame probe";
}
void *probeCreate(obs_data_t *settings, obs_source_t *)
{
	auto *p = new ProbeData;
	p->counter = reinterpret_cast<std::atomic<quint64> *>(uintptr_t(obs_data_get_int(settings, "counter")));
	return p;
}
void probeDestroy(void *data)
{
	delete static_cast<ProbeData *>(data);
}
obs_source_frame *probeFilterVideo(void *data, obs_source_frame *frame)
{
	auto *p = static_cast<ProbeData *>(data);
	if (p->counter)
		p->counter->fetch_add(1, std::memory_order_relaxed);
	return frame;
}

constexpr const char *kProbeId = "marshalls_frame_probe";

void registerProbeFilter()
{
	obs_source_info info = {};
	info.id = kProbeId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_CAP_DISABLED;
	info.get_name = probeName;
	info.create = probeCreate;
	info.destroy = probeDestroy;
	info.filter_video = probeFilterVideo;
	obs_register_source(&info);
}

} // namespace

// ---------------------------------------------------------------------------
// Device watcher (worker thread: device enumeration can block for a while)
// ---------------------------------------------------------------------------

class DeviceWatcher : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
public slots:
	void init()
	{
#ifdef _WIN32
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
		m_timer = new QTimer(this);
		connect(m_timer, &QTimer::timeout, this, &DeviceWatcher::poll);
		m_timer->start(3000);
		poll();
	}
	void poll()
	{
		QHash<int, QVector<DeviceInfo>> result;
		for (SourceKind k : {SourceKind::Camera, SourceKind::AudioInput, SourceKind::AudioOutput})
			if (ObsPlatform::binding(k).deviceProp && ObsPlatform::isKindSupported(k))
				result.insert(int(k), ObsPlatform::enumerateDevices(k));
		emit updated(result);
	}
	void finish()
	{
		if (m_timer)
			m_timer->stop();
		ObsPlatform::releaseEnumerationInstances();
#ifdef _WIN32
		CoUninitialize();
#endif
	}
signals:
	void updated(const QHash<int, QVector<DeviceInfo>> &devices);

private:
	QTimer *m_timer = nullptr;
};

// ---------------------------------------------------------------------------
// private structures
// ---------------------------------------------------------------------------

struct Engine::Src {
	Engine *engine = nullptr;
	QString uid;
	SourceConfig cfg;
	obs_source_t *source = nullptr;
	obs_scene_t *solo = nullptr;
	obs_source_t *probe = nullptr;
	obs_volmeter_t *volmeter = nullptr;
	SignalConn mediaEndedConn;
	int audioChannel = -1;
	int track = -1; // mix index 1..5 for isolated tracks
	bool unsupported = false;
	bool showing = false;
	QString createError;

	std::atomic<float> magnitude{-100.0f};
	std::atomic<float> peak{-100.0f};
	std::atomic<quint64> audioPackets{0};
	std::atomic<quint64> frames{0};

	quint64 lastPackets = 0, lastFrames = 0;
	qint64 lastPacketMs = 0, lastFrameMs = 0, createdMs = 0;
	SourceRuntime rt;

	bool hasVideo() const { return kindHasVideo(cfg.kind); }
	bool isAudioOnly() const { return cfg.kind == SourceKind::AudioInput || cfg.kind == SourceKind::AudioOutput; }
	bool hasAudioOut() const
	{
		return source && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
	}
};

struct Engine::Graphic {
	LowerThirdConfig cfg;
	GraphicController controller;
	obs_source_t *source = nullptr;
	obs_sceneitem_t *item = nullptr;
};

struct Engine::Overlay {
	OverlayImageConfig cfg;
	obs_source_t *source = nullptr;
	obs_sceneitem_t *item = nullptr;
	obs_source_t *showTr = nullptr;
	obs_source_t *hideTr = nullptr;
};

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

static QHash<int, QVector<DeviceInfo>> g_devices; // UI thread only
static bool g_devicesKnown = false;
static QThread *g_watcherThread = nullptr;
static DeviceWatcher *g_watcher = nullptr;

Engine::Engine(QObject *parent) : QObject(parent), m_thumbs(std::make_unique<ThumbnailService>()) {}

Engine::~Engine()
{
	shutdown();
}

bool Engine::initialize(const EngineInit &init, QString *error)
{
	if (m_initialized)
		return true;

	base_set_log_handler(obsLogHandler, nullptr);
	base_set_crash_handler(obsCrashHandler, nullptr);

#if !defined(_WIN32) && !defined(__APPLE__)
	if (init.wayland) {
		obs_set_nix_platform(OBS_NIX_PLATFORM_WAYLAND);
	} else {
		obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	}
	obs_set_nix_platform_display(init.x11Display);
#endif

	QDir().mkpath(init.moduleConfigDir);
	if (!obs_startup(utf8(init.locale).constData(), utf8(init.moduleConfigDir).constData(), nullptr)) {
		if (error)
			*error = QStringLiteral("Falha ao iniciar o motor de mídia (obs_startup).");
		return false;
	}
	logInfo("engine", QStringLiteral("libobs %1 iniciado").arg(QString::fromLatin1(obs_get_version_string())));

#ifdef _WIN32
	// Packaged layout mirrors the OBS install tree: bin/64bit/<exe>,
	// data/libobs, data/obs-plugins/<module>, obs-plugins/64bit/<module>.dll.
	const QString appDir = QCoreApplication::applicationDirPath();
	for (const QString &d : {appDir + "/../../data/libobs/", appDir + "/data/libobs/"})
		if (QFileInfo::exists(d))
			obs_add_data_path(utf8(QDir::cleanPath(d) + "/").constData());
#endif

	registerGraphicSource();
	registerProbeFilter();
	registerRtmpService();

	m_initialized = true; // resetVideo requires it
	if (!resetVideo(init.video, error)) {
		obs_shutdown();
		m_initialized = false;
		return false;
	}

	obs_audio_info oai = {};
	oai.samples_per_sec = 48000;
	oai.speakers = SPEAKERS_STEREO;
	if (!obs_reset_audio(&oai)) {
		if (error)
			*error = QStringLiteral("Falha ao iniciar o subsistema de áudio.");
		obs_shutdown();
		m_initialized = false;
		return false;
	}

	loadModules();
	obs_post_load_modules();

	createProgramBus();
	m_thumbs->setQuality(320, 10);
	m_thumbs->start();
	m_outputs = std::make_unique<OutputManager>(this);

	for (int ch = 63; ch >= kFirstAudioChannel; --ch)
		m_freeAudioChannels.push_back(ch);

	{
		audio_convert_info conv = {};
		conv.samples_per_sec = 48000;
		conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
		conv.speakers = SPEAKERS_STEREO;
		obs_add_raw_audio_callback(0, &conv, &Engine::onProgramAudio, this);
	}

	m_healthTimer = new QTimer(this);
	connect(m_healthTimer, &QTimer::timeout, this, &Engine::pollHealth);
	m_healthTimer->start(500);

	qRegisterMetaType<QHash<int, QVector<DeviceInfo>>>("QHash<int,QVector<DeviceInfo>>");
	g_watcherThread = new QThread;
	g_watcherThread->setObjectName("ms-device-watcher");
	g_watcher = new DeviceWatcher;
	g_watcher->moveToThread(g_watcherThread);
	connect(g_watcherThread, &QThread::started, g_watcher, &DeviceWatcher::init);
	connect(g_watcher, &DeviceWatcher::updated, this,
		[](const QHash<int, QVector<DeviceInfo>> &d) {
			g_devices = d;
			g_devicesKnown = true;
		},
		Qt::QueuedConnection);
	g_watcherThread->start(QThread::LowPriority);

	logInfo("engine", QStringLiteral("Entradas registradas: %1").arg(registeredInputs().join(", ")));
	logInfo("engine", QStringLiteral("Codificadores: %1").arg(registeredEncoders().join(", ")));
	return true;
}

void Engine::shutdown()
{
	if (!m_initialized)
		return;
	logInfo("engine", "Encerrando motor de mídia");
	if (m_healthTimer)
		m_healthTimer->stop();

	if (g_watcherThread) {
		QMetaObject::invokeMethod(g_watcher, &DeviceWatcher::finish, Qt::BlockingQueuedConnection);
		g_watcherThread->quit();
		g_watcherThread->wait();
		delete g_watcher;
		delete g_watcherThread;
		g_watcher = nullptr;
		g_watcherThread = nullptr;
	}
	ObsPlatform::releaseEnumerationInstances(); // UI-thread instances (screen/window)
	g_devices.clear();
	g_devicesKnown = false;

	obs_remove_raw_audio_callback(0, &Engine::onProgramAudio, this);
	if (m_outputs)
		m_outputs->shutdown();
	m_outputs.reset();
	m_thumbs->stop();

	for (int ch = 0; ch < 64; ++ch)
		obs_set_output_source(uint32_t(ch), nullptr);

	const auto graphicIds = m_graphics.keys();
	for (const auto &id : graphicIds)
		removeLowerThird(id);
	const auto overlayIds = m_overlays.keys();
	for (const auto &id : overlayIds)
		removeOverlay(id);
	const auto sceneIds = m_scenes.keys();
	for (const auto &id : sceneIds)
		removeScene(id);
	const auto srcIds = m_sources.keys();
	for (const auto &id : srcIds)
		removeSource(id);
	destroyProgramBus();

	obs_wait_for_destroy_queue();
	obs_shutdown();
	base_set_log_handler(nullptr, nullptr);
	m_initialized = false;
	logInfo("engine", QStringLiteral("Motor encerrado; memória ativa de libobs: %1 alocações").arg(bnum_allocs()));
}

void Engine::loadModules()
{
	struct Ctx {
		QStringList allow;
		QStringList loaded;
	} ctx{ObsPlatform::moduleAllowList(), {}};

#ifdef _WIN32
	const QString appDir = QCoreApplication::applicationDirPath();
	const QString roots[] = {QDir::cleanPath(appDir + "/../.."), appDir};
	for (const QString &root : roots) {
		if (QFileInfo::exists(root + "/obs-plugins/64bit")) {
			obs_add_module_path(utf8(root + "/obs-plugins/64bit/").constData(),
					    utf8(root + "/data/obs-plugins/%module%/").constData());
			break;
		}
	}
#else
	const QByteArray envBin = qgetenv("MS_OBS_PLUGIN_DIR");
	const QByteArray envData = qgetenv("MS_OBS_PLUGIN_DATA_DIR");
	if (!envBin.isEmpty()) {
		obs_add_module_path(envBin.constData(), envData.isEmpty() ? "/usr/share/obs/obs-plugins/%module%"
									  : envData.constData());
	} else {
		for (const char *dir : {"/usr/lib/x86_64-linux-gnu/obs-plugins", "/usr/lib/aarch64-linux-gnu/obs-plugins",
					"/usr/lib64/obs-plugins", "/usr/lib/obs-plugins", "/usr/local/lib/obs-plugins"}) {
			if (QFileInfo::exists(QString::fromLatin1(dir))) {
				const bool local = QString::fromLatin1(dir).startsWith("/usr/local");
				obs_add_module_path(dir, local ? "/usr/local/share/obs/obs-plugins/%module%"
							       : "/usr/share/obs/obs-plugins/%module%");
				break;
			}
		}
	}
#endif

	obs_find_modules2(
		[](void *param, const obs_module_info2 *info) {
			auto *c = static_cast<Ctx *>(param);
			const QString name = QString::fromUtf8(info->name);
			// libobs also scans its built-in paths: never load a module twice
			// (double initialisation breaks plugins that own global state).
			if (!c->allow.contains(name) || c->loaded.contains(name))
				return;
			obs_module_t *mod = nullptr;
			const int r = obs_open_module(&mod, info->bin_path, info->data_path);
			if (r != MODULE_SUCCESS) {
				logWarn("engine", QStringLiteral("Plugin %1 não pôde ser aberto (código %2)").arg(name).arg(r));
				return;
			}
			if (!obs_init_module(mod)) {
				logWarn("engine", QStringLiteral("Plugin %1 falhou ao inicializar").arg(name));
				return;
			}
			c->loaded.append(name);
		},
		&ctx);
	logInfo("engine", QStringLiteral("Plugins carregados: %1").arg(ctx.loaded.join(", ")));
	for (const auto &required : {"obs-ffmpeg", "obs-outputs", "obs-transitions", "obs-filters", "image-source"})
		if (!ctx.loaded.contains(QLatin1String(required)))
			logError("engine", QStringLiteral("Plugin essencial ausente: %1").arg(QLatin1String(required)));
}

bool Engine::resetVideo(const VideoSettings &v, QString *error)
{
	if (!m_initialized)
		return false;
	obs_video_info ovi = {};
	const QByteArray gfx = utf8(ObsPlatform::graphicsModule());
	ovi.graphics_module = gfx.constData();
	ovi.fps_num = uint32_t(v.fpsNum);
	ovi.fps_den = uint32_t(v.fpsDen);
	ovi.base_width = uint32_t(v.baseWidth);
	ovi.base_height = uint32_t(v.baseHeight);
	ovi.output_width = uint32_t(v.outputWidth);
	ovi.output_height = uint32_t(v.outputHeight);
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	ovi.adapter = 0;
	ovi.gpu_conversion = true;
	ovi.scale_type = OBS_SCALE_BICUBIC;

	const int r = obs_reset_video(&ovi);
	if (r != OBS_VIDEO_SUCCESS) {
		QString msg;
		switch (r) {
		case OBS_VIDEO_CURRENTLY_ACTIVE:
			msg = QStringLiteral("Não é possível alterar o vídeo com gravação ou transmissão ativa.");
			break;
		case OBS_VIDEO_NOT_SUPPORTED:
			msg = QStringLiteral("A placa de vídeo não suporta os requisitos gráficos (Direct3D 11 / OpenGL 3.3).");
			break;
		case OBS_VIDEO_INVALID_PARAM: msg = QStringLiteral("Parâmetros de vídeo inválidos."); break;
		case OBS_VIDEO_MODULE_NOT_FOUND:
			msg = QStringLiteral("Módulo gráfico %1 não encontrado.").arg(ObsPlatform::graphicsModule());
			break;
		default: msg = QStringLiteral("Falha ao iniciar o vídeo (código %1).").arg(r);
		}
		if (error)
			*error = msg;
		logError("engine", msg);
		return false;
	}
	m_video = v;
	logInfo("engine", QStringLiteral("Vídeo: base %1x%2, saída %3x%4, %5/%6 fps")
				  .arg(v.baseWidth)
				  .arg(v.baseHeight)
				  .arg(v.outputWidth)
				  .arg(v.outputHeight)
				  .arg(v.fpsNum)
				  .arg(v.fpsDen));

	// Re-apply canvas dependent geometry.
	for (auto &s : m_sources)
		applyCanvasToSolo(*s);
	for (auto it = m_sceneConfigs.cbegin(); it != m_sceneConfigs.cend(); ++it)
		updateScene(it.value());
	if (m_black) {
		obs_data_t *d = obs_data_create();
		obs_data_set_int(d, "width", v.baseWidth);
		obs_data_set_int(d, "height", v.baseHeight);
		obs_source_update(m_black, d);
		obs_data_release(d);
	}
	for (auto &g : m_graphics)
		updateLowerThirdRender(*g);
	for (auto &o : m_overlays)
		setOverlay(o->cfg);
	m_thumbs->setQuality(320, 10);
	return true;
}

// ---------------------------------------------------------------------------
// program bus
// ---------------------------------------------------------------------------

void Engine::createProgramBus()
{
	const char *ids[] = {"cut_transition", "fade_transition", "fade_transition", "slide_transition",
			     "swipe_transition"};
	for (size_t i = 0; i < m_transitions.size(); ++i) {
		if (i == size_t(TransitionType::FadeToBlack))
			continue; // FTB has its own layer
		m_transitions[i] = obs_source_create_private(ids[i], ids[i], nullptr);
		if (!m_transitions[i]) {
			logError("engine", QStringLiteral("Transição indisponível: %1").arg(QLatin1String(ids[i])));
			continue;
		}
		signal_handler_connect(obs_source_get_signal_handler(m_transitions[i]), "transition_stop",
				       &Engine::onTransitionStop, this);
	}
	m_currentTransition = m_transitions[size_t(TransitionType::Fade)];
	obs_set_output_source(kProgramChannel, m_currentTransition);

	m_graphicsScene = obs_scene_create_private("ms-graphics");
	obs_set_output_source(kGraphicsChannel, obs_scene_get_source(m_graphicsScene));

	obs_data_t *bd = obs_data_create();
	obs_data_set_int(bd, "color", 0xFF000000);
	obs_data_set_int(bd, "width", m_video.baseWidth);
	obs_data_set_int(bd, "height", m_video.baseHeight);
	m_black = obs_source_create_private("color_source_v3", "ms-black", bd);
	obs_data_release(bd);
	m_clearScene = obs_scene_create_private("ms-clear");
	m_ftbTransition = obs_source_create_private("fade_transition", "ms-ftb", nullptr);
	signal_handler_connect(obs_source_get_signal_handler(m_ftbTransition), "transition_stop", &Engine::onFtbStop,
			       this);
	obs_transition_set(m_ftbTransition, obs_scene_get_source(m_clearScene));
	obs_set_output_source(kFtbChannel, m_ftbTransition);
}

void Engine::destroyProgramBus()
{
	for (auto &t : m_transitions) {
		if (!t)
			continue;
		signal_handler_disconnect(obs_source_get_signal_handler(t), "transition_stop", &Engine::onTransitionStop,
					  this);
		obs_transition_clear(t);
		obs_source_release(t);
		t = nullptr;
	}
	m_currentTransition = nullptr;
	if (m_ftbTransition) {
		signal_handler_disconnect(obs_source_get_signal_handler(m_ftbTransition), "transition_stop",
					  &Engine::onFtbStop, this);
		obs_transition_clear(m_ftbTransition);
		obs_source_release(m_ftbTransition);
		m_ftbTransition = nullptr;
	}
	if (m_black)
		obs_source_release(m_black);
	m_black = nullptr;
	if (m_clearScene)
		obs_scene_release(m_clearScene);
	m_clearScene = nullptr;
	if (m_graphicsScene)
		obs_scene_release(m_graphicsScene);
	m_graphicsScene = nullptr;
}

obs_source_t *Engine::transitionFor(TransitionType t) const
{
	if (t == TransitionType::FadeToBlack)
		t = TransitionType::Fade;
	obs_source_t *tr = m_transitions[size_t(t)];
	return tr ? tr : m_transitions[size_t(TransitionType::Fade)];
}

obs_source_t *Engine::contentSource(const ContentRef &ref) const
{
	if (ref.type == ContentRef::Type::Source) {
		auto s = m_sources.value(ref.uid);
		if (!s || !s->solo)
			return nullptr;
		return obs_scene_get_source(s->solo);
	}
	if (ref.type == ContentRef::Type::Scene) {
		obs_scene_t *sc = m_scenes.value(ref.uid);
		return sc ? obs_scene_get_source(sc) : nullptr;
	}
	return nullptr;
}

bool Engine::take(const TakeDecision &d)
{
	if (!m_initialized || !d.accepted)
		return false;
	obs_source_t *target = contentSource(d.target);
	if (!target) {
		logWarn("program", QStringLiteral("Conteúdo inexistente: %1").arg(d.target.key()));
		return false;
	}
	if (d.target.type == ContentRef::Type::Source) {
		auto s = m_sources.value(d.target.uid);
		if (s && s->cfg.kind == SourceKind::Media && s->cfg.media.restartOnTake)
			obs_source_media_restart(s->source);
	}

	if (d.isCut()) {
		obs_transition_set(m_currentTransition, target);
		logInfo("program", QStringLiteral("CORTE → %1").arg(d.target.key()));
		return true;
	}

	obs_source_t *tr = transitionFor(d.type);
	if (tr != m_currentTransition) {
		obs_source_t *cur = obs_transition_get_active_source(m_currentTransition);
		obs_transition_set(tr, cur);
		obs_source_release(cur);
		obs_set_output_source(kProgramChannel, tr);
		obs_transition_clear(m_currentTransition);
		m_currentTransition = tr;
	}
	const bool ok = obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, uint32_t(d.durationMs), target);
	logInfo("program", QStringLiteral("%1 %2 ms → %3 %4")
				   .arg(transitionToString(d.type))
				   .arg(d.durationMs)
				   .arg(d.target.key(), ok ? QString() : QStringLiteral("(falhou)")));
	return ok;
}

bool Engine::fadeToBlack(const TakeDecision &d, bool toBlack)
{
	if (!m_ftbTransition || !d.accepted)
		return false;
	obs_source_t *dest = toBlack ? m_black : obs_scene_get_source(m_clearScene);
	const bool ok = obs_transition_start(m_ftbTransition, OBS_TRANSITION_MODE_AUTO, uint32_t(d.durationMs), dest);
	logInfo("program", QStringLiteral("FTB %1 (%2 ms)").arg(toBlack ? "para preto" : "retorno").arg(d.durationMs));
	return ok;
}

void Engine::clearProgram()
{
	if (m_currentTransition)
		obs_transition_set(m_currentTransition, nullptr);
}

double Engine::transitionProgress() const
{
	return m_currentTransition ? double(obs_transition_get_time(m_currentTransition)) : 0.0;
}

void Engine::onTransitionStop(void *data, calldata_t *)
{
	auto *self = static_cast<Engine *>(data);
	QMetaObject::invokeMethod(self, [self] { emit self->transitionFinished(); }, Qt::QueuedConnection);
}

void Engine::onFtbStop(void *data, calldata_t *)
{
	auto *self = static_cast<Engine *>(data);
	QMetaObject::invokeMethod(self, [self] { emit self->fadeToBlackFinished(); }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// sources
// ---------------------------------------------------------------------------

static void fillKindSettings(obs_data_t *d, const SourceConfig &cfg, const VideoSettings &video)
{
	const KindBinding b = ObsPlatform::binding(cfg.kind);
	if (b.deviceProp && !cfg.device.id.isEmpty()) {
		if (b.deviceIsInt)
			obs_data_set_int(d, b.deviceProp, cfg.device.id.toLongLong());
		else
			obs_data_set_string(d, b.deviceProp, cfg.device.id.toUtf8().constData());
	}
	switch (cfg.kind) {
	case SourceKind::Media:
		obs_data_set_bool(d, "is_local_file", true);
		obs_data_set_bool(d, "looping", cfg.media.loop);
		obs_data_set_bool(d, "restart_on_activate", false); // handled on take
		obs_data_set_bool(d, "close_when_inactive", false);
		obs_data_set_bool(d, "clear_on_media_end", cfg.media.endAction == MediaEndAction::Black);
		obs_data_set_bool(d, "hw_decode", true);
		break;
	case SourceKind::Color:
		if (!obs_data_has_user_value(d, "color"))
			obs_data_set_int(d, "color", 0xFF202020);
		obs_data_set_int(d, "width", video.baseWidth);
		obs_data_set_int(d, "height", video.baseHeight);
		break;
	case SourceKind::Screen:
#ifdef _WIN32
		obs_data_set_bool(d, "capture_cursor", obs_data_has_user_value(d, "capture_cursor")
							       ? obs_data_get_bool(d, "capture_cursor")
							       : true);
#else
		if (!obs_data_has_user_value(d, "show_cursor"))
			obs_data_set_bool(d, "show_cursor", true);
#endif
		break;
	case SourceKind::Window:
#ifdef _WIN32
		if (!obs_data_has_user_value(d, "priority"))
			obs_data_set_int(d, "priority", 2); // match executable, then title
#endif
		break;
	case SourceKind::Camera:
#ifdef _WIN32
		obs_data_set_bool(d, "active", true);
		if (!obs_data_has_user_value(d, "deactivate_when_not_showing"))
			obs_data_set_bool(d, "deactivate_when_not_showing", false);
#endif
		break;
	case SourceKind::AudioInput:
	case SourceKind::AudioOutput:
		if (cfg.device.id.isEmpty())
			obs_data_set_string(d, "device_id", "default");
		break;
	default: break;
	}
}

bool Engine::createSource(const SourceConfig &cfg, QString *error)
{
	if (!m_initialized)
		return false;
	if (m_sources.contains(cfg.uid)) {
		updateSource(cfg);
		return true;
	}
	auto s = std::make_shared<Src>();
	s->engine = this;
	s->uid = cfg.uid;
	s->cfg = cfg;
	s->createdMs = nowMs();
	m_sources.insert(cfg.uid, s);

	if (!ObsPlatform::isKindSupported(cfg.kind)) {
		s->unsupported = true;
		s->rt.health = SourceHealth::Unsupported;
		s->rt.detail = ObsPlatform::unsupportedReason(cfg.kind);
		logWarn("source", QStringLiteral("%1: %2").arg(cfg.name, s->rt.detail));
		if (error)
			*error = s->rt.detail;
		return true; // the configuration is kept; the card shows the limitation
	}

	obs_data_t *settings = dataFromJson(cfg.engineSettings);
	fillKindSettings(settings, cfg, m_video);
	const KindBinding b = ObsPlatform::binding(cfg.kind);
	s->source = obs_source_create(b.sourceId, utf8(cfg.uid).constData(), settings, nullptr);
	obs_data_release(settings);
	if (!s->source) {
		s->rt.health = SourceHealth::Error;
		s->rt.detail = QStringLiteral("O motor não conseguiu criar a fonte.");
		if (error)
			*error = s->rt.detail;
		logError("source", QStringLiteral("%1: falha em obs_source_create(%2)").arg(cfg.name, b.sourceId));
		return false;
	}

	if (s->hasVideo()) {
		s->solo = obs_scene_create_private(utf8("solo:" + cfg.uid).constData());
		obs_scene_add(s->solo, s->source);
		applyCanvasToSolo(*s);
		applyVideoDelay(*s, cfg.videoDelayMs);
		if (obs_source_get_output_flags(s->source) & OBS_SOURCE_ASYNC) {
			obs_data_t *pd = obs_data_create();
			obs_data_set_int(pd, "counter", static_cast<long long>(reinterpret_cast<uintptr_t>(&s->frames)));
			s->probe = obs_source_create_private(kProbeId, "ms-probe", pd);
			obs_data_release(pd);
			if (s->probe)
				obs_source_filter_add(s->source, s->probe);
		}
		obs_source_inc_showing(s->source);
		s->showing = true;
		m_thumbs->add(cfg.uid, s->source);
	}

	if (cfg.kind == SourceKind::Media)
		s->mediaEndedConn.connect(s->source, "media_ended", &Engine::onMediaEnded, s.get());

	if (s->hasAudioOut()) {
		s->volmeter = obs_volmeter_create(OBS_FADER_LOG);
		obs_volmeter_attach_source(s->volmeter, s->source);
		obs_volmeter_add_callback(s->volmeter, &Engine::onVolmeter, s.get());
		obs_source_add_audio_capture_callback(s->source, &Engine::onAudioCapture, s.get());
		applyAudio(*s, cfg);
	}

	if (s->isAudioOnly() && !m_freeAudioChannels.empty()) {
		s->audioChannel = m_freeAudioChannels.back();
		m_freeAudioChannels.pop_back();
		obs_set_output_source(uint32_t(s->audioChannel), s->source);
	}
	reassignTracks();
	logInfo("source", QStringLiteral("Fonte criada: “%1” (%2, %3)")
				  .arg(cfg.name, kindToString(cfg.kind), QLatin1String(b.sourceId)));
	return true;
}

void Engine::applyCanvasToSolo(Src &s)
{
	if (!s.solo)
		return;
	obs_sceneitem_t *item = obs_scene_find_source(s.solo, utf8(s.uid).constData());
	if (!item)
		return;
	vec2 pos{0.0f, 0.0f}, bounds{float(m_video.baseWidth), float(m_video.baseHeight)};
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
}

void Engine::applyVideoDelay(Src &s, int ms)
{
	if (!s.source)
		return;
	obs_source_t *existing = obs_source_get_filter_by_name(s.source, "ms.video_delay");
	if (ms <= 0) {
		if (existing)
			obs_source_filter_remove(s.source, existing);
	} else {
		obs_data_t *d = obs_data_create();
		obs_data_set_int(d, "delay_ms", std::min(ms, 500));
		if (existing) {
			obs_source_update(existing, d);
		} else {
			obs_source_t *f = obs_source_create_private("gpu_delay", "ms.video_delay", d);
			if (f) {
				obs_source_filter_add(s.source, f);
				obs_source_release(f);
			}
		}
		obs_data_release(d);
	}
	if (existing)
		obs_source_release(existing);
}

void Engine::applyAudio(Src &s, const SourceConfig &cfg)
{
	if (!s.source || !s.hasAudioOut())
		return;
	obs_source_set_volume(s.source, float(dbToMul(cfg.audio.volumeDb)));
	obs_source_set_muted(s.source, cfg.audio.muted);
	applySyncOffset(s.source, int64_t(cfg.audio.syncOffsetMs) * 1000000);
	setMonitoring(s.uid, cfg.audio.monitor);
	setAudioFilters(s.uid, cfg.audio.filters);
	applyMixers(s);
}

void Engine::applyMixers(Src &s)
{
	if (!s.source || !s.hasAudioOut())
		return;
	uint32_t mixers = 0;
	if (s.cfg.hasAudio && s.cfg.audio.toProgram)
		mixers |= 1u;
	if (s.cfg.hasAudio && s.track > 0)
		mixers |= (1u << s.track);
	obs_source_set_audio_mixers(s.source, mixers);
}

void Engine::reassignTracks()
{
	// Isolated tracks go to microphones first (creation order), then computer audio.
	QVector<std::shared_ptr<Src>> ordered;
	for (auto &s : m_sources)
		if (s->isAudioOnly())
			ordered.append(s);
	std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
		if (a->cfg.kind != b->cfg.kind)
			return a->cfg.kind == SourceKind::AudioInput;
		if (a->cfg.order != b->cfg.order)
			return a->cfg.order < b->cfg.order;
		return a->createdMs < b->createdMs;
	});
	int next = 1;
	for (auto &s : m_sources)
		s->track = -1;
	for (auto &s : ordered) {
		if (s->cfg.audio.isolatedTrack && next <= kMaxIsolatedTracks)
			s->track = next++;
	}
	for (auto &s : m_sources)
		applyMixers(*s);
}

QStringList Engine::isolatedTrackSources() const
{
	QVector<QPair<int, QString>> v;
	for (const auto &s : m_sources)
		if (s->track > 0)
			v.append({s->track, s->uid});
	std::sort(v.begin(), v.end());
	QStringList out;
	for (const auto &p : v)
		out.append(p.second);
	return out;
}

int Engine::trackIndexOf(const QString &uid) const
{
	auto s = m_sources.value(uid);
	return s ? s->track : -1;
}

void Engine::updateSource(const SourceConfig &cfg)
{
	auto s = m_sources.value(cfg.uid);
	if (!s)
		return;
	const SourceConfig old = s->cfg;
	s->cfg = cfg;
	if (!s->source)
		return;
	if (old.engineSettings != cfg.engineSettings || !(old.device == cfg.device) ||
	    old.media.loop != cfg.media.loop || old.media.endAction != cfg.media.endAction) {
		obs_data_t *settings = dataFromJson(cfg.engineSettings);
		fillKindSettings(settings, cfg, m_video);
		obs_source_update(s->source, settings);
		obs_data_release(settings);
	}
	if (old.videoDelayMs != cfg.videoDelayMs)
		applyVideoDelay(*s, cfg.videoDelayMs);
	if (s->hasAudioOut())
		applyAudio(*s, cfg);
	if (old.audio.isolatedTrack != cfg.audio.isolatedTrack || old.hasAudio != cfg.hasAudio ||
	    old.order != cfg.order)
		reassignTracks();
}

void Engine::removeSource(const QString &uid)
{
	auto s = m_sources.take(uid);
	if (!s)
		return;
	m_thumbs->remove(uid);
	s->mediaEndedConn.disconnect();
	if (s->volmeter) {
		obs_volmeter_remove_callback(s->volmeter, &Engine::onVolmeter, s.get());
		obs_volmeter_detach_source(s->volmeter);
		obs_volmeter_destroy(s->volmeter);
		s->volmeter = nullptr;
	}
	if (s->source && s->hasAudioOut())
		obs_source_remove_audio_capture_callback(s->source, &Engine::onAudioCapture, s.get());
	if (s->audioChannel >= 0) {
		obs_set_output_source(uint32_t(s->audioChannel), nullptr);
		m_freeAudioChannels.push_back(s->audioChannel);
	}
	// Remove from every composition.
	for (auto it = m_scenes.begin(); it != m_scenes.end(); ++it) {
		obs_sceneitem_t *item;
		while ((item = obs_scene_find_source(it.value(), utf8(uid).constData())))
			obs_sceneitem_remove(item);
	}
	if (s->showing && s->source)
		obs_source_dec_showing(s->source);
	if (s->probe) {
		obs_source_filter_remove(s->source, s->probe);
		obs_source_release(s->probe);
	}
	if (s->solo)
		obs_scene_release(s->solo);
	if (s->source) {
		obs_source_remove(s->source);
		obs_source_release(s->source);
	}
	s->source = nullptr;
	reassignTracks();
	logInfo("source", QStringLiteral("Fonte removida: %1").arg(uid));
}

bool Engine::hasSource(const QString &uid) const
{
	return m_sources.contains(uid);
}

obs_source_t *Engine::obsSource(const QString &uid) const
{
	auto s = m_sources.value(uid);
	return s ? s->source : nullptr;
}

QStringList Engine::sourceIds() const
{
	return m_sources.keys();
}

SourceRuntime Engine::runtime(const QString &uid) const
{
	auto s = m_sources.value(uid);
	return s ? s->rt : SourceRuntime{};
}

AudioLevel Engine::audioLevel(const QString &uid) const
{
	auto s = m_sources.value(uid);
	if (!s)
		return {};
	return {s->magnitude.load(), s->peak.load()};
}

void Engine::reconnectSource(const QString &uid)
{
	auto s = m_sources.value(uid);
	if (!s || !s->source)
		return;
	logInfo("source", QStringLiteral("Reconectando “%1”").arg(s->cfg.name));
	obs_data_t *settings = dataFromJson(s->cfg.engineSettings);
	fillKindSettings(settings, s->cfg, m_video);
#ifdef _WIN32
	if (s->cfg.kind == SourceKind::Camera) {
		obs_data_set_bool(settings, "active", false);
		obs_source_update(s->source, settings);
		obs_data_set_bool(settings, "active", true);
	}
#endif
	obs_source_update(s->source, settings);
	obs_data_release(settings);
	s->lastFrameMs = s->lastPacketMs = nowMs();
	s->rt.health = SourceHealth::Starting;
	s->rt.detail = QStringLiteral("Reconectando…");
	emit sourceHealthChanged(uid);
}

void Engine::setVolumeDb(const QString &uid, double db)
{
	if (auto s = m_sources.value(uid); s && s->source) {
		s->cfg.audio.volumeDb = db;
		obs_source_set_volume(s->source, float(dbToMul(db)));
	}
}

void Engine::setMuted(const QString &uid, bool muted)
{
	if (auto s = m_sources.value(uid); s && s->source) {
		s->cfg.audio.muted = muted;
		obs_source_set_muted(s->source, muted);
	}
}

void Engine::setSyncOffsetMs(const QString &uid, int ms)
{
	auto s = m_sources.value(uid);
	if (!s || !s->source)
		return;
	s->cfg.audio.syncOffsetMs = ms;
	applySyncOffset(s->source, int64_t(ms) * 1000000);
}

// libobs (30.x, source_output_audio_data) keeps appending continuous audio
// back-to-back when the sync offset changes *from zero*, so a live change from
// 0 would only take effect after a timing reset. Going through a 1 ns offset
// first makes the following packet re-place the audio at the new timestamp.
void Engine::applySyncOffset(obs_source_t *source, int64_t offsetNs)
{
	const int64_t current = obs_source_get_sync_offset(source);
	if (current == offsetNs)
		return;
	if (current != 0 || offsetNs == 0) {
		obs_source_set_sync_offset(source, offsetNs);
		return;
	}
	obs_source_set_sync_offset(source, 1);
	obs_weak_source_t *weak = obs_source_get_weak_source(source);
	QTimer::singleShot(80, this, [weak, offsetNs] {
		if (obs_source_t *src = obs_weak_source_get_source(weak)) {
			if (obs_source_get_sync_offset(src) == 1)
				obs_source_set_sync_offset(src, offsetNs);
			obs_source_release(src);
		}
		obs_weak_source_release(weak);
	});
}

void Engine::setMonitoring(const QString &uid, MonitorMode mode)
{
	auto s = m_sources.value(uid);
	if (!s || !s->source)
		return;
	s->cfg.audio.monitor = mode;
	obs_monitoring_type t = OBS_MONITORING_TYPE_NONE;
	if (mode == MonitorMode::MonitorOnly)
		t = OBS_MONITORING_TYPE_MONITOR_ONLY;
	else if (mode == MonitorMode::MonitorAndOutput)
		t = OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT;
	obs_source_set_monitoring_type(s->source, t);
}

void Engine::setAudioFilters(const QString &uid, const AudioFilters &f)
{
	auto s = m_sources.value(uid);
	if (!s || !s->source || !s->hasAudioOut())
		return;
	s->cfg.audio.filters = f;

	struct Spec {
		const char *name;
		const char *id;
		bool enabled;
		std::function<void(obs_data_t *)> fill;
	};
	const std::vector<Spec> chain = {
		{"ms.gain", "gain_filter", std::abs(f.gainDb) > 0.01, [&](obs_data_t *d) { obs_data_set_double(d, "db", f.gainDb); }},
		{"ms.suppress", "noise_suppress_filter_v2", f.suppressEnabled,
		 [&](obs_data_t *d) {
			 obs_data_set_string(d, "method", "speex");
			 obs_data_set_int(d, "suppress_level", f.suppressLevelDb);
		 }},
		{"ms.gate", "noise_gate_filter", f.gateEnabled,
		 [&](obs_data_t *d) {
			 obs_data_set_double(d, "open_threshold", f.gateOpenDb);
			 obs_data_set_double(d, "close_threshold", f.gateCloseDb);
			 obs_data_set_int(d, "attack_time", f.gateAttackMs);
			 obs_data_set_int(d, "hold_time", f.gateHoldMs);
			 obs_data_set_int(d, "release_time", f.gateReleaseMs);
		 }},
		{"ms.eq", "basic_eq_filter", f.eqEnabled,
		 [&](obs_data_t *d) {
			 obs_data_set_double(d, "low", f.eqLowDb);
			 obs_data_set_double(d, "mid", f.eqMidDb);
			 obs_data_set_double(d, "high", f.eqHighDb);
		 }},
		{"ms.compressor", "compressor_filter", f.compEnabled,
		 [&](obs_data_t *d) {
			 obs_data_set_double(d, "ratio", f.compRatio);
			 obs_data_set_double(d, "threshold", f.compThresholdDb);
			 obs_data_set_int(d, "attack_time", f.compAttackMs);
			 obs_data_set_int(d, "release_time", f.compReleaseMs);
			 obs_data_set_double(d, "output_gain", f.compOutputGainDb);
			 obs_data_set_string(d, "sidechain_source", "none");
		 }},
		{"ms.limiter", "limiter_filter", f.limiterEnabled,
		 [&](obs_data_t *d) {
			 obs_data_set_double(d, "threshold", f.limiterThresholdDb);
			 obs_data_set_int(d, "release_time", f.limiterReleaseMs);
		 }},
	};

	for (const auto &spec : chain) {
		obs_source_t *existing = obs_source_get_filter_by_name(s->source, spec.name);
		if (!spec.enabled) {
			if (existing)
				obs_source_filter_remove(s->source, existing);
		} else {
			obs_data_t *d = obs_data_create();
			spec.fill(d);
			if (existing) {
				obs_source_update(existing, d);
			} else {
				obs_source_t *flt = obs_source_create_private(spec.id, spec.name, d);
				if (flt) {
					obs_source_filter_add(s->source, flt);
					obs_source_release(flt);
				} else {
					logWarn("audio", QStringLiteral("Filtro indisponível: %1").arg(QLatin1String(spec.id)));
				}
			}
			obs_data_release(d);
		}
		if (existing)
			obs_source_release(existing);
	}
	// Enforce the processing order (gain → suppression → gate → EQ → comp → limiter).
	for (const auto &spec : chain) {
		if (obs_source_t *flt = obs_source_get_filter_by_name(s->source, spec.name)) {
			obs_source_filter_set_order(s->source, flt, OBS_ORDER_MOVE_BOTTOM);
			obs_source_release(flt);
		}
	}
}

bool Engine::setMonitoringDevice(const QString &idIn, const QString &nameIn)
{
	const QString id = idIn.isEmpty() ? QStringLiteral("default") : idIn;
	const QString name = nameIn.isEmpty() ? QStringLiteral("Default") : nameIn;
	const bool ok = obs_set_audio_monitoring_device(utf8(name).constData(), utf8(id).constData());
	logInfo("audio", QStringLiteral("Dispositivo de monitoramento: %1 (%2)").arg(name, ok ? "ok" : "falhou"));
	return ok;
}

QVector<DeviceInfo> Engine::monitoringDevices()
{
	QVector<DeviceInfo> out;
	out.append({QStringLiteral("default"), QStringLiteral("Padrão do sistema")});
	obs_enum_audio_monitoring_devices(
		[](void *data, const char *name, const char *id) {
			auto *v = static_cast<QVector<DeviceInfo> *>(data);
			const QString sid = QString::fromUtf8(id);
			if (sid != QLatin1String("default"))
				v->append({sid, QString::fromUtf8(name)});
			return true;
		},
		&out);
	return out;
}

void Engine::mediaPlayPause(const QString &uid)
{
	auto s = m_sources.value(uid);
	if (!s || !s->source || s->cfg.kind != SourceKind::Media)
		return;
	const obs_media_state st = obs_source_media_get_state(s->source);
	if (st == OBS_MEDIA_STATE_ENDED || st == OBS_MEDIA_STATE_STOPPED)
		obs_source_media_restart(s->source);
	else
		obs_source_media_play_pause(s->source, st == OBS_MEDIA_STATE_PLAYING);
}

void Engine::mediaRestart(const QString &uid)
{
	if (auto s = m_sources.value(uid); s && s->source)
		obs_source_media_restart(s->source);
}

void Engine::mediaStop(const QString &uid)
{
	if (auto s = m_sources.value(uid); s && s->source)
		obs_source_media_stop(s->source);
}

void Engine::onMediaEnded(void *data, calldata_t *)
{
	auto *s = static_cast<Src *>(data);
	Engine *self = s->engine;
	const QString uid = s->uid;
	QMetaObject::invokeMethod(self, [self, uid] { emit self->mediaEnded(uid); }, Qt::QueuedConnection);
}

void Engine::onVolmeter(void *data, const float magnitude[], const float peak[], const float[])
{
	auto *s = static_cast<Src *>(data);
	float m = -100.0f, p = -100.0f;
	for (int i = 0; i < MAX_AUDIO_CHANNELS; ++i) {
		if (std::isfinite(magnitude[i]))
			m = std::max(m, magnitude[i]);
		if (std::isfinite(peak[i]))
			p = std::max(p, peak[i]);
	}
	s->magnitude.store(m);
	s->peak.store(p);
}

void Engine::onAudioCapture(void *param, obs_source_t *, const audio_data *, bool)
{
	static_cast<Src *>(param)->audioPackets.fetch_add(1, std::memory_order_relaxed);
}

void Engine::onProgramAudio(void *param, size_t, audio_data *data)
{
	auto *self = static_cast<Engine *>(param);
	float peak = 0.0f;
	double acc = 0.0;
	uint32_t n = 0;
	for (int ch = 0; ch < 2; ++ch) {
		const float *s = reinterpret_cast<const float *>(data->data[ch]);
		if (!s)
			continue;
		for (uint32_t i = 0; i < data->frames; ++i) {
			peak = std::max(peak, std::abs(s[i]));
			acc += double(s[i]) * s[i];
		}
		n += data->frames;
	}
	const float rms = n ? float(std::sqrt(acc / n)) : 0.0f;
	self->m_programPeak.store(peak > 0.0f ? 20.0f * std::log10(peak) : -100.0f);
	self->m_programRms.store(rms > 0.0f ? 20.0f * std::log10(rms) : -100.0f);
}

AudioLevel Engine::programLevel() const
{
	return {m_programRms.load(), m_programPeak.load()};
}

static bool deviceListed(SourceKind kind, const DeviceRef &dev)
{
	if (!g_devicesKnown || !g_devices.contains(int(kind)))
		return true; // unknown yet: do not claim a disconnection
	if (dev.id.isEmpty() || dev.id == QLatin1String("default"))
		return true;
	for (const auto &d : g_devices.value(int(kind)))
		if (d.id == dev.id)
			return true;
	return false;
}

void Engine::pollHealth()
{
	const qint64 now = nowMs();
	static qint64 lastScreenEnum = 0;
	if (now - lastScreenEnum > 3000) {
		lastScreenEnum = now;
		for (SourceKind k : {SourceKind::Screen, SourceKind::Window})
			if (ObsPlatform::isKindSupported(k))
				g_devices.insert(int(k), ObsPlatform::enumerateDevices(k));
	}
	for (auto &sp : m_sources) {
		Src &s = *sp;
		if (s.unsupported || !s.source)
			continue;
		const SourceHealth oldHealth = s.rt.health;
		const QString oldDetail = s.rt.detail;
		SourceHealth h = SourceHealth::Ok;
		QString detail;

		s.rt.width = int(obs_source_get_width(s.source));
		s.rt.height = int(obs_source_get_height(s.source));
		s.rt.active = obs_source_active(s.source);

		const bool hasDevice = ObsPlatform::binding(s.cfg.kind).deviceProp != nullptr;
		const bool present = !hasDevice || deviceListed(s.cfg.kind, s.cfg.device);

		const quint64 frames = s.frames.load();
		if (frames != s.lastFrames) {
			s.lastFrames = frames;
			s.lastFrameMs = now;
		}
		const quint64 packets = s.audioPackets.load();
		if (packets != s.lastPackets) {
			s.lastPackets = packets;
			s.lastPacketMs = now;
		}

		switch (s.cfg.kind) {
		case SourceKind::Camera: {
			const qint64 since = now - std::max(s.lastFrameMs, s.createdMs);
			if (!present) {
				h = SourceHealth::Disconnected;
				detail = QStringLiteral("Dispositivo “%1” não encontrado").arg(s.cfg.device.name);
			} else if (s.lastFrames == 0 && since < 8000) {
				h = SourceHealth::Starting;
				detail = QStringLiteral("Abrindo dispositivo…");
			} else if (since > 3000) {
				h = SourceHealth::NoSignal;
				detail = QStringLiteral("Nenhum quadro há %1 s").arg(since / 1000);
			}
			break;
		}
		case SourceKind::Media: {
			s.rt.mediaState = int(obs_source_media_get_state(s.source));
			s.rt.mediaTimeMs = obs_source_media_get_time(s.source);
			s.rt.mediaDurationMs = obs_source_media_get_duration(s.source);
			const QString path = s.cfg.engineSettings.value("local_file").toString();
			if (path.isEmpty() || !QFileInfo::exists(path)) {
				h = SourceHealth::Error;
				detail = QStringLiteral("Arquivo não encontrado");
			} else if (s.rt.mediaState == OBS_MEDIA_STATE_ERROR ||
				   (now - s.createdMs > 3000 && s.rt.mediaDurationMs <= 0 && s.rt.width == 0 &&
				    s.rt.mediaState != OBS_MEDIA_STATE_PLAYING && s.rt.mediaState != OBS_MEDIA_STATE_OPENING)) {
				// The ffmpeg source does not always flag an error state for corrupt
				// files; a file that never yields duration, size or playback is invalid.
				h = SourceHealth::Error;
				detail = QStringLiteral("Não foi possível decodificar o arquivo (formato ou codec não suportado)");
			}
			break;
		}
		case SourceKind::Image:
		case SourceKind::Slideshow:
			if (s.rt.width == 0) {
				h = SourceHealth::Error;
				detail = QStringLiteral("Imagem não carregada (arquivo ausente ou formato inválido)");
			}
			break;
		case SourceKind::Screen:
		case SourceKind::Window:
			if (!present) {
				h = SourceHealth::Disconnected;
				detail = QStringLiteral("Monitor não encontrado");
			} else if (s.rt.width == 0) {
				h = (now - s.createdMs < 4000) ? SourceHealth::Starting : SourceHealth::NoSignal;
				detail = s.cfg.kind == SourceKind::Window ? QStringLiteral("Janela fechada ou minimizada")
									   : QStringLiteral("Sem imagem");
			}
			break;
		case SourceKind::AudioInput:
		case SourceKind::AudioOutput: {
			const qint64 since = now - std::max(s.lastPacketMs, s.createdMs);
			if (!present) {
				h = SourceHealth::Disconnected;
				detail = QStringLiteral("Dispositivo “%1” não encontrado").arg(s.cfg.device.name);
			} else if (s.lastPackets == 0 && since < 5000) {
				h = SourceHealth::Starting;
				detail = QStringLiteral("Abrindo dispositivo…");
			} else if (since > 2500 && s.cfg.kind == SourceKind::AudioInput) {
				h = SourceHealth::NoSignal;
				detail = QStringLiteral("Sem dados do dispositivo há %1 s").arg(since / 1000);
			}
			break;
		}
		case SourceKind::Color: break;
		}
		s.rt.health = h;
		s.rt.detail = detail;
		if (h != oldHealth || detail != oldDetail) {
			if (h != oldHealth)
				logInfo("health", QStringLiteral("“%1”: %2 %3").arg(s.cfg.name, healthLabel(h), detail));
			emit sourceHealthChanged(s.uid);
		}
	}
}

// ---------------------------------------------------------------------------
// compositions
// ---------------------------------------------------------------------------

void Engine::buildSceneItems(obs_scene_t *scene, const SceneConfig &cfg)
{
	struct Ctx {
		Engine *self;
		const SceneConfig *cfg;
	} ctx{this, &cfg};
	obs_scene_atomic_update(
		scene,
		[](void *data, obs_scene_t *sc) {
			auto *c = static_cast<Ctx *>(data);
			// Remove previous items.
			obs_scene_enum_items(
				sc,
				[](obs_scene_t *, obs_sceneitem_t *item, void *) {
					obs_sceneitem_remove(item);
					return true;
				},
				nullptr);
			const double W = c->self->m_video.baseWidth, H = c->self->m_video.baseHeight;
			for (const auto &it : c->cfg->items) {
				obs_source_t *src = c->self->obsSource(it.sourceUid);
				if (!src)
					continue;
				obs_sceneitem_t *item = obs_scene_add(sc, src);
				if (!item)
					continue;
				vec2 pos{float(it.rect.x() * W), float(it.rect.y() * H)};
				vec2 bounds{float(it.rect.width() * W), float(it.rect.height() * H)};
				obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
				obs_sceneitem_set_pos(item, &pos);
				obs_sceneitem_set_bounds_type(item, it.fill ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
				obs_sceneitem_set_bounds(item, &bounds);
				obs_sceneitem_set_bounds_crop(item, it.fill);
				obs_sceneitem_crop crop{it.cropLeft, it.cropTop, it.cropRight, it.cropBottom};
				obs_sceneitem_set_crop(item, &crop);
				obs_sceneitem_set_visible(item, it.visible);
			}
		},
		&ctx);
}

bool Engine::createScene(const SceneConfig &cfg)
{
	if (!m_initialized)
		return false;
	if (m_scenes.contains(cfg.uid)) {
		updateScene(cfg);
		return true;
	}
	obs_scene_t *scene = obs_scene_create_private(utf8("scene:" + cfg.uid).constData());
	if (!scene)
		return false;
	m_scenes.insert(cfg.uid, scene);
	m_sceneConfigs.insert(cfg.uid, cfg);
	buildSceneItems(scene, cfg);
	m_thumbs->add(QStringLiteral("scene:") + cfg.uid, obs_scene_get_source(scene));
	logInfo("scene", QStringLiteral("Composição criada: “%1” (%2 itens)").arg(cfg.name).arg(cfg.items.size()));
	return true;
}

void Engine::updateScene(const SceneConfig &cfg)
{
	obs_scene_t *scene = m_scenes.value(cfg.uid);
	if (!scene)
		return;
	m_sceneConfigs.insert(cfg.uid, cfg);
	buildSceneItems(scene, cfg);
}

void Engine::removeScene(const QString &uid)
{
	obs_scene_t *scene = m_scenes.take(uid);
	m_sceneConfigs.remove(uid);
	if (!scene)
		return;
	m_thumbs->remove(QStringLiteral("scene:") + uid);
	obs_scene_release(scene);
}

obs_source_t *Engine::sceneSource(const QString &uid) const
{
	obs_scene_t *sc = m_scenes.value(uid);
	return sc ? obs_scene_get_source(sc) : nullptr;
}

// ---------------------------------------------------------------------------
// graphics layer
// ---------------------------------------------------------------------------

void Engine::updateLowerThirdRender(Graphic &g)
{
	const RenderedGraphic r = LowerThirdRenderer::render(g.cfg, QSize(m_video.baseWidth, m_video.baseHeight));
	g.controller.setGraphic(r, g.cfg.animIn, g.cfg.animOut, g.cfg.animMs, g.cfg.holdSeconds);
}

void Engine::setLowerThird(const LowerThirdConfig &cfg)
{
	auto g = m_graphics.value(cfg.uid);
	if (!g) {
		g = std::make_shared<Graphic>();
		g->cfg = cfg;
		updateLowerThirdRender(*g);
		g->source = createGraphicSource(QStringLiteral("lt:") + cfg.uid, &g->controller);
		if (g->source)
			g->item = obs_scene_add(m_graphicsScene, g->source);
		m_graphics.insert(cfg.uid, g);
		return;
	}
	g->cfg = cfg;
	updateLowerThirdRender(*g);
}

void Engine::removeLowerThird(const QString &uid)
{
	auto g = m_graphics.take(uid);
	if (!g)
		return;
	if (g->item)
		obs_sceneitem_remove(g->item);
	if (g->source) {
		obs_source_remove(g->source);
		obs_source_release(g->source);
	}
	obs_wait_for_destroy_queue(); // the controller must outlive the source
}

void Engine::showLowerThird(const QString &uid, bool show)
{
	auto g = m_graphics.value(uid);
	if (!g)
		return;
	if (show)
		g->controller.show();
	else
		g->controller.hide();
	logInfo("graphics", QStringLiteral("%1 “%2”").arg(show ? "Exibir" : "Remover", g->cfg.name));
}

GraphicPhase Engine::lowerThirdPhase(const QString &uid) const
{
	auto g = m_graphics.value(uid);
	return g ? g->controller.phase() : GraphicPhase::Hidden;
}

void Engine::setOverlay(const OverlayImageConfig &cfg)
{
	auto o = m_overlays.value(cfg.uid);
	if (!o) {
		o = std::make_shared<Overlay>();
		obs_data_t *d = obs_data_create();
		obs_data_set_string(d, "file", utf8(cfg.path).constData());
		o->source = obs_source_create_private("image_source", utf8("ovl:" + cfg.uid).constData(), d);
		obs_data_release(d);
		if (!o->source)
			return;
		o->item = obs_scene_add(m_graphicsScene, o->source);
		obs_sceneitem_set_visible(o->item, false);
		// Overlays below lower thirds.
		obs_sceneitem_set_order(o->item, OBS_ORDER_MOVE_BOTTOM);
		o->showTr = obs_source_create_private("fade_transition", "ovl-show", nullptr);
		o->hideTr = obs_source_create_private("fade_transition", "ovl-hide", nullptr);
		obs_sceneitem_set_transition(o->item, true, o->showTr);
		obs_sceneitem_set_transition(o->item, false, o->hideTr);
		m_overlays.insert(cfg.uid, o);
	} else if (o->cfg.path != cfg.path) {
		obs_data_t *d = obs_data_create();
		obs_data_set_string(d, "file", utf8(cfg.path).constData());
		obs_source_update(o->source, d);
		obs_data_release(d);
	}
	o->cfg = cfg;
	obs_sceneitem_set_transition_duration(o->item, true, uint32_t(cfg.fadeMs));
	obs_sceneitem_set_transition_duration(o->item, false, uint32_t(cfg.fadeMs));
	const double W = m_video.baseWidth, H = m_video.baseHeight;
	vec2 pos{float(cfg.rect.x() * W), float(cfg.rect.y() * H)};
	vec2 bounds{float(cfg.rect.width() * W), float(cfg.rect.height() * H)};
	obs_sceneitem_set_alignment(o->item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(o->item, &pos);
	obs_sceneitem_set_bounds_type(o->item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(o->item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(o->item, &bounds);
}

void Engine::removeOverlay(const QString &uid)
{
	auto o = m_overlays.take(uid);
	if (!o)
		return;
	if (o->item)
		obs_sceneitem_remove(o->item);
	for (obs_source_t *s : {o->source, o->showTr, o->hideTr})
		if (s)
			obs_source_release(s);
}

void Engine::showOverlay(const QString &uid, bool show)
{
	if (auto o = m_overlays.value(uid); o && o->item)
		obs_sceneitem_set_visible(o->item, show);
}

bool Engine::overlayVisible(const QString &uid) const
{
	auto o = m_overlays.value(uid);
	return o && o->item && obs_sceneitem_visible(o->item);
}

void Engine::hideAllGraphics()
{
	for (auto &g : m_graphics)
		g->controller.hide();
	for (auto &o : m_overlays)
		if (o->item)
			obs_sceneitem_set_visible(o->item, false);
}

QVector<DeviceInfo> Engine::cachedDevices(SourceKind kind, bool *known)
{
	if (known)
		*known = g_devicesKnown && g_devices.contains(int(kind));
	return g_devices.value(int(kind));
}

void Engine::setPreviewEnabled(const QString &uid, bool enabled)
{
	auto s = m_sources.value(uid);
	if (!s || !s->source || !s->hasVideo() || s->showing == enabled)
		return;
	s->showing = enabled;
	if (enabled)
		obs_source_inc_showing(s->source);
	else
		obs_source_dec_showing(s->source);
	m_thumbs->setPaused(uid, !enabled);
}

bool Engine::isPreviewEnabled(const QString &uid) const
{
	auto s = m_sources.value(uid);
	return s && s->showing;
}

// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------

QStringList Engine::registeredInputs()
{
	QStringList l;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_input_types(i, &id); ++i)
		l.append(QString::fromUtf8(id));
	return l;
}

QStringList Engine::registeredEncoders()
{
	QStringList l;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i)
		l.append(QString::fromUtf8(id));
	return l;
}

} // namespace ms

#include "Engine.moc"
