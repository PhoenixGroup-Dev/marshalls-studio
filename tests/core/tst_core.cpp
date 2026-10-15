// Unit tests for the hardware-independent core.
#include "ConfigStore.h"
#include "Log.h"
#include "Planning.h"
#include "ProgramController.h"
#include "SecretStore.h"
#include "ShortcutMap.h"
#include "StudioConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace ms;

class TstCore : public QObject {
	Q_OBJECT
private slots:
	void configRoundTrip();
	void configClampsInvalidValues();
	void configDropsDanglingSceneItems();
	void storeSkipsUnchangedWrite();
	void storeRecoversCorruptFromBackup();
	void storeMigratesV1();
	void storeKeepsNewerSchemaCopy();
	void storeRenameAndRemove();
	void deviceDuplicateDetection();
	void uniqueNames();

	void programSelectAndCut();
	void programRejectsWhileTransitioning();
	void programRejectsSameContentAndEmpty();
	void programFadeToBlackCycle();
	void programContentRemoved();
	void programReturnToPrevious();

	void shortcutsDefaultsHaveNoConflicts();
	void shortcutsConflictDetection();
	void shortcutsForbidden();
	void shortcutsOverridesRoundTrip();

	void redactorRemovesSecrets();
	void logRotationKeepsNewest();
	void secretStoreRoundTrip();

	void diskEstimate();
	void streamValidation();
	void layoutPresetsInsideCanvas();
	void audioSafety();
	void faderTaperMonotonic();
};

static StudioProfile sampleProfile()
{
	StudioProfile p;
	p.name = "Teste";
	SourceConfig cam;
	cam.uid = "src-cam1";
	cam.name = "Câmera 1";
	cam.kind = SourceKind::Camera;
	cam.device = {"\\\\?\\usb#vid_046d&pid_085c", "Logitech C922"};
	cam.videoDelayMs = 120;
	SourceConfig mic;
	mic.uid = "src-mic1";
	mic.name = "Mic Host";
	mic.kind = SourceKind::AudioInput;
	mic.hasAudio = true;
	mic.device = {"{0.0.1.00000000}.{abc}", "Microfone (Yeti)"};
	mic.audio.volumeDb = -6.5;
	mic.audio.syncOffsetMs = 80;
	mic.audio.monitor = MonitorMode::MonitorOnly;
	mic.audio.filters.gateEnabled = true;
	mic.audio.filters.compRatio = 3.0;
	p.sources = {cam, mic};
	SceneConfig sc;
	sc.uid = "scn-a";
	sc.name = "Entrevista";
	SceneItemConfig it;
	it.sourceUid = "src-cam1";
	it.rect = QRectF(0.5, 0, 0.5, 1);
	it.fill = true;
	sc.items = {it};
	p.scenes = {sc};
	LowerThirdConfig lt;
	lt.uid = "lt-1";
	lt.name = "Ana Souza";
	lt.role = "Diretora";
	lt.company = "Marshalls";
	lt.holdSeconds = 8;
	lt.accent = QColor(10, 20, 30);
	p.lowerThirds = {lt};
	p.shortcuts.insert("take.cut", "Ctrl+Return");
	p.ui.layoutState.insert("main", QByteArray("\x01\x02\x03", 3));
	return p;
}

void TstCore::configRoundTrip()
{
	const StudioProfile p = sampleProfile();
	const StudioProfile q = StudioProfile::fromJson(p.toJson());
	QCOMPARE(q.toJson(), p.toJson());
	QCOMPARE(q.sources.size(), 2);
	QCOMPARE(q.sources[0].device.id, p.sources[0].device.id);
	QCOMPARE(q.sources[1].audio.volumeDb, -6.5);
	QCOMPARE(q.sources[1].audio.monitor, MonitorMode::MonitorOnly);
	QVERIFY(q.scenes[0].items[0].fill);
	QCOMPARE(q.lowerThirds[0].accent, QColor(10, 20, 30));
	QCOMPARE(q.ui.layoutState.value("main"), QByteArray("\x01\x02\x03", 3));
}

void TstCore::configClampsInvalidValues()
{
	QJsonObject o = sampleProfile().toJson();
	QJsonObject v = o["video"].toObject();
	v["base_w"] = 99999;
	v["fps_num"] = -5;
	o["video"] = v;
	QJsonObject rec = o["recording"].toObject();
	rec["container"] = "exe";
	o["recording"] = rec;
	const StudioProfile p = StudioProfile::fromJson(o);
	QCOMPARE(p.video.baseWidth, 7680);
	QCOMPARE(p.video.fpsNum, 1);
	QCOMPARE(p.recording.container, QString("mkv"));
}

void TstCore::configDropsDanglingSceneItems()
{
	StudioProfile p = sampleProfile();
	p.scenes[0].items.append(SceneItemConfig{"src-missing"});
	const StudioProfile q = StudioProfile::fromJson(p.toJson());
	QCOMPARE(q.scenes[0].items.size(), 1);
	StudioProfile r = sampleProfile();
	QVERIFY(r.removeSource("src-cam1"));
	QCOMPARE(r.scenes[0].items.size(), 0);
}

void TstCore::storeSkipsUnchangedWrite()
{
	QTemporaryDir dir;
	ConfigStore store(dir.path());
	const StudioProfile p = sampleProfile();
	QVERIFY(store.save(p));
	const QString path = store.profilePath(p.name);
	const QDateTime m1 = QFileInfo(path).lastModified();
	QThread::msleep(1100);
	QVERIFY(store.save(p));
	QCOMPARE(QFileInfo(path).lastModified(), m1);
	QVERIFY(!QFile::exists(path + ".bak"));
	StudioProfile changed = p;
	changed.transition.durationMs = 900;
	QVERIFY(store.save(changed));
	QVERIFY(QFile::exists(path + ".bak"));
	QCOMPARE(store.load(p.name).profile.transition.durationMs, 900);
}

void TstCore::storeRecoversCorruptFromBackup()
{
	QTemporaryDir dir;
	ConfigStore store(dir.path());
	StudioProfile p = sampleProfile();
	QVERIFY(store.save(p));
	p.transition.durationMs = 777;
	QVERIFY(store.save(p)); // previous valid version now in .bak
	QFile f(store.profilePath(p.name));
	QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
	f.write("{ this is not json");
	f.close();
	const auto r = store.load(p.name);
	QVERIFY(r.fromDisk);
	QVERIFY(r.recoveredFromBackup);
	QCOMPARE(r.profile.sources.size(), 2);
	QVERIFY(!r.message.isEmpty());
	const auto quarantined = QDir(store.profilesDir()).entryList({"*.corrupt-*"});
	QCOMPARE(quarantined.size(), 1);
}

void TstCore::storeMigratesV1()
{
	QTemporaryDir dir;
	ConfigStore store(dir.path());
	QJsonObject v1{{"schema_version", 1},
		       {"name", "Antigo"},
		       {"transition_ms", 1200},
		       {"sources", QJsonArray{QJsonObject{{"uid", "m"},
							  {"name", "Mic"},
							  {"kind", "audio_input"},
							  {"audio", QJsonObject{{"volume", 0.5}}}}}}};
	QFile f(store.profilePath("Antigo"));
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(QJsonDocument(v1).toJson());
	f.close();
	const auto r = store.load("Antigo");
	QVERIFY(r.migrated);
	QCOMPARE(r.profile.transition.durationMs, 1200);
	QVERIFY(qAbs(r.profile.sources[0].audio.volumeDb - (-6.0206)) < 0.01);
	QVERIFY(QFile::exists(store.profilePath("Antigo") + ".v1.bak"));
}

void TstCore::storeKeepsNewerSchemaCopy()
{
	QTemporaryDir dir;
	ConfigStore store(dir.path());
	QJsonObject future = sampleProfile().toJson();
	future["schema_version"] = kSchemaVersion + 5;
	future["name"] = "Futuro";
	QFile f(store.profilePath("Futuro"));
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(QJsonDocument(future).toJson());
	f.close();
	const auto r = store.load("Futuro");
	QVERIFY(r.newerSchema);
	QVERIFY(QFile::exists(store.profilePath("Futuro") + QString(".v%1.bak").arg(kSchemaVersion + 5)));
	QCOMPARE(r.profile.sources.size(), 2);
}

void TstCore::storeRenameAndRemove()
{
	QTemporaryDir dir;
	ConfigStore store(dir.path());
	StudioProfile p = sampleProfile();
	QVERIFY(store.save(p));
	QVERIFY(store.renameProfile("Teste", "Podcast"));
	QVERIFY(store.listProfiles().contains("Podcast"));
	QVERIFY(!store.listProfiles().contains("Teste"));
	QVERIFY(store.removeProfile("Podcast"));
	QVERIFY(store.listProfiles().isEmpty());
	QCOMPARE(QDir(store.profilesDir()).entryList({"*.deleted-*"}).size(), 1);
	QCOMPARE(ConfigStore::sanitize("a/b:c*"), QString("a_b_c_"));
}

void TstCore::deviceDuplicateDetection()
{
	const StudioProfile p = sampleProfile();
	QCOMPARE(p.sourceUsingDevice(SourceKind::AudioInput, {"{0.0.1.00000000}.{abc}", "x"}), QString("src-mic1"));
	QCOMPARE(p.sourceUsingDevice(SourceKind::AudioInput, {"{0.0.1.00000000}.{abc}", "x"}, "src-mic1"), QString());
	QCOMPARE(p.sourceUsingDevice(SourceKind::Camera, {"{0.0.1.00000000}.{abc}", "x"}), QString());
}

void TstCore::uniqueNames()
{
	const StudioProfile p = sampleProfile();
	QCOMPARE(p.uniqueName("Câmera 1"), QString("Câmera 1 2"));
	QCOMPARE(p.uniqueName("Nova"), QString("Nova"));
	QCOMPARE(p.uniqueName("entrevista"), QString("entrevista 2"));
}

void TstCore::programSelectAndCut()
{
	ProgramController c;
	TransitionSettings tr;
	tr.type = TransitionType::Fade;
	tr.durationMs = 700;
	c.select(ContentRef::source("a"));
	auto d = c.requestTake(TakeMode::Cut, tr);
	QVERIFY(d.accepted);
	QVERIFY(d.isCut());
	c.cutApplied(d.target);
	QCOMPARE(c.program(), ContentRef::source("a"));
	QCOMPARE(c.selected(), ContentRef::source("a")); // selection is kept, not destroyed

	c.select(ContentRef::scene("s"));
	d = c.requestTake(TakeMode::Auto, tr);
	QVERIFY(d.accepted);
	QCOMPARE(d.durationMs, 700);
	QCOMPARE(d.type, TransitionType::Fade);
	c.transitionStarted(d.target);
	QVERIFY(c.isTransitioning());
	QCOMPARE(c.program(), ContentRef::source("a")); // still on air until finished
	c.transitionFinished();
	QCOMPARE(c.program(), ContentRef::scene("s"));
	QCOMPARE(c.previous(), ContentRef::source("a"));

	tr.type = TransitionType::Cut;
	c.select(ContentRef::source("b"));
	d = c.requestTake(TakeMode::Auto, tr);
	QVERIFY(d.isCut());
}

void TstCore::programRejectsWhileTransitioning()
{
	ProgramController c;
	TransitionSettings tr;
	c.select(ContentRef::source("a"));
	auto d = c.requestTake(TakeMode::Auto, tr);
	c.transitionStarted(d.target);
	c.select(ContentRef::source("b"));
	auto d2 = c.requestTake(TakeMode::Cut, tr);
	QVERIFY(!d2.accepted);
	QVERIFY(d2.reason.contains("andamento"));
	auto f = c.requestFadeToBlack(500);
	QVERIFY(!f.accepted);
	c.transitionFinished();
	QVERIFY(c.requestTake(TakeMode::Cut, tr).accepted);
}

void TstCore::programRejectsSameContentAndEmpty()
{
	ProgramController c;
	TransitionSettings tr;
	QVERIFY(!c.requestTake(TakeMode::Cut, tr).accepted);
	c.select(ContentRef::source("a"));
	c.cutApplied(ContentRef::source("a"));
	auto d = c.requestTake(TakeMode::Cut, tr);
	QVERIFY(!d.accepted);
	QCOMPARE(d.reason, QString("Já está no ar"));
}

void TstCore::programFadeToBlackCycle()
{
	ProgramController c;
	TransitionSettings tr;
	auto f = c.requestFadeToBlack(800);
	QVERIFY(f.accepted);
	QCOMPARE(c.ftbState(), FtbState::FadingOut);
	QVERIFY(!c.requestFadeToBlack(800).accepted);
	c.select(ContentRef::source("a"));
	QVERIFY(!c.requestTake(TakeMode::Cut, tr).accepted);
	c.ftbFinished();
	QCOMPARE(c.ftbState(), FtbState::Black);
	auto d = c.requestTake(TakeMode::Cut, tr);
	QVERIFY(d.accepted);
	QVERIFY(d.reason.contains("FTB"));
	c.cutApplied(d.target);
	QVERIFY(c.requestFadeToBlack(800).accepted);
	QCOMPARE(c.ftbState(), FtbState::FadingIn);
	c.ftbFinished();
	QCOMPARE(c.ftbState(), FtbState::Off);
}

void TstCore::programContentRemoved()
{
	ProgramController c;
	c.select(ContentRef::source("a"));
	c.cutApplied(ContentRef::source("a"));
	c.select(ContentRef::source("b"));
	QVERIFY(!c.contentRemoved(ContentRef::source("b")));
	QVERIFY(!c.selected().isValid());
	QVERIFY(c.contentRemoved(ContentRef::source("a")));
	QVERIFY(!c.program().isValid());
}

void TstCore::programReturnToPrevious()
{
	ProgramController c;
	TransitionSettings tr;
	QVERIFY(!c.requestReturnToPrevious(tr).accepted);
	c.cutApplied(ContentRef::source("cam"));
	c.cutApplied(ContentRef::source("vinheta"));
	auto d = c.requestReturnToPrevious(tr);
	QVERIFY(d.accepted);
	QCOMPARE(d.target, ContentRef::source("cam"));
	QCOMPARE(ContentRef::fromKey(ContentRef::scene("x").key()), ContentRef::scene("x"));
}

void TstCore::shortcutsDefaultsHaveNoConflicts()
{
	ShortcutMap m;
	QVERIFY2(m.conflicts().isEmpty(), qPrintable(QStringList(m.conflicts().keys()).join(",")));
	QCOMPARE(m.actionForKey("Return"), QString("take.cut"));
	QCOMPARE(m.actionForKey("Ctrl+3"), QString("slot.take.3"));
	QCOMPARE(m.actionForKey("F2"), QString("mic.mute.2"));
	for (const auto &a : ShortcutMap::catalogue())
		QVERIFY2(!ShortcutMap::isForbidden(a.defaultKey), qPrintable(a.id));
}

void TstCore::shortcutsConflictDetection()
{
	ShortcutMap m;
	QVERIFY(m.setKey("program.ftb", "Return"));
	const auto c = m.conflicts();
	QCOMPARE(c.size(), 1);
	QVERIFY(c.value("Return").contains("take.cut"));
	QVERIFY(c.value("Return").contains("program.ftb"));
	QCOMPARE(m.actionForKey("Return"), QString()); // ambiguous → nothing fires
}

void TstCore::shortcutsForbidden()
{
	ShortcutMap m;
	QString why;
	QVERIFY(!m.setKey("take.cut", "Alt+F4", &why));
	QVERIFY(!why.isEmpty());
	QVERIFY(ShortcutMap::isForbidden("ctrl+q"));
	QVERIFY(!m.setKey("nao.existe", "X"));
	QVERIFY(m.setKey("take.cut", "")); // unbinding is allowed
	QCOMPARE(m.key("take.cut"), QString());
}

void TstCore::shortcutsOverridesRoundTrip()
{
	ShortcutMap m;
	QVERIFY(m.setKey("rec.toggle", "Ctrl+Shift+R"));
	const auto ov = m.overrides();
	QCOMPARE(ov.size(), 1);
	ShortcutMap n;
	QMap<QString, QString> bad = ov;
	bad.insert("take.auto", "Alt+F4"); // forbidden values are ignored on load
	n.load(bad);
	QCOMPARE(n.key("rec.toggle"), QString("Ctrl+Shift+R"));
	QCOMPARE(n.key("take.auto"), QString("Space"));
}

void TstCore::redactorRemovesSecrets()
{
	Redactor &r = Redactor::instance();
	r.addSecret("live_abcdef123456");
	r.addSecret("ab"); // too short, ignored
	QCOMPARE(r.redact("connecting rtmp://srv/app/live_abcdef123456 now"),
		 QString("connecting rtmp://srv/app/[REDACTED] now"));
	QCOMPARE(r.redact("url?key=zzz999&x=1"), QString("url?key=[REDACTED]&x=1"));
	QCOMPARE(r.redact("about tab"), QString("about tab"));
	r.removeSecret("live_abcdef123456");
	QVERIFY(r.redact("live_abcdef123456").contains("live_"));
}

void TstCore::logRotationKeepsNewest()
{
	QTemporaryDir dir;
	for (int i = 0; i < 15; ++i) {
		QFile f(dir.path() + QString("/marshalls-20260101-0000%1.log").arg(i, 2, 10, QChar('0')));
		QVERIFY(f.open(QIODevice::WriteOnly));
	}
	Log::prune(dir.path(), 9);
	const auto left = QDir(dir.path()).entryList({"marshalls-*.log"}, QDir::Files, QDir::Name);
	QCOMPARE(left.size(), 9);
	QVERIFY(left.last().contains("000014"));
	Log &log = Log::instance();
	log.setEchoToStderr(false);
	QVERIFY(log.open(dir.path()));
	Redactor::instance().addSecret("SEGREDO-123456");
	logInfo("test", "chave SEGREDO-123456");
	log.close();
	QFile f(log.currentPath());
	QVERIFY(f.open(QIODevice::ReadOnly));
	const QByteArray content = f.readAll();
	QVERIFY(content.contains("[REDACTED]"));
	QVERIFY(!content.contains("SEGREDO-123456"));
	QCOMPARE(QDir(dir.path()).entryList({"marshalls-*.log"}).size(), 10);
}

void TstCore::secretStoreRoundTrip()
{
	QTemporaryDir dir;
	SecretStore s(dir.path());
	QVERIFY(s.set("stream-key", "live_xyz_987"));
	QCOMPARE(s.get("stream-key"), QString("live_xyz_987"));
	// The secret never appears in plain text in the profile directory names.
	for (const auto &n : QDir(dir.path()).entryList(QDir::Files))
		QVERIFY(!n.contains("stream-key"));
	QVERIFY(s.remove("stream-key"));
	QCOMPARE(s.get("stream-key"), QString());
}

void TstCore::diskEstimate()
{
	RecordingSettings r;
	r.encoder.rateControl = RateControl::CBR;
	r.encoder.videoBitrateKbps = 6000;
	r.encoder.audioBitrateKbps = 160;
	r.minFreeMb = 1024;
	const qint64 tenGb = 10LL * 1024 * 1024 * 1024;
	auto e = estimateDisk(r, 1920, 1080, 30, 1, tenGb);
	QVERIFY(qAbs(e.mbPerMinute - 6160 * 60.0 / 8 / 1024) < 0.01);
	QVERIFY(e.minutesRemaining > 190 && e.minutesRemaining < 205);
	QVERIFY(!e.warn);
	e = estimateDisk(r, 1920, 1080, 30, 1, 512LL * 1024 * 1024);
	QVERIFY(e.belowMinimum);
	QVERIFY(e.warn);
	r.encoder.rateControl = RateControl::CRF;
	r.encoder.crf = 20;
	QVERIFY(estimateVideoKbps(r.encoder, 1920, 1080, 30) > 4000);
	r.encoder.crf = 26;
	QVERIFY(estimateVideoKbps(r.encoder, 1920, 1080, 30) < 4000);
}

void TstCore::streamValidation()
{
	StreamSettings s;
	VideoSettings v;
	auto hasError = [](const QVector<ValidationIssue> &l) {
		for (const auto &i : l)
			if (i.severity == ValidationIssue::Severity::Error)
				return true;
		return false;
	};
	QVERIFY(hasError(validateStream(s, "", v)));
	s.server = "http://example.com/live";
	QVERIFY(hasError(validateStream(s, "k123", v)));
	s.server = "rtmp://127.0.0.1/live";
	QVERIFY(!hasError(validateStream(s, "k123", v)));
	s.platform = "facebook";
	QVERIFY(hasError(validateStream(s, "k123", v)));
	s.server = "rtmps://live-api-s.facebook.com:443/rtmp/";
	QVERIFY(!hasError(validateStream(s, "k123", v)));
	s.platform = "twitch";
	s.server = "rtmp://live.twitch.tv/app";
	s.encoder.videoBitrateKbps = 9000;
	const auto issues = validateStream(s, "k123", v);
	QVERIFY(!hasError(issues));
	QVERIFY(!issues.isEmpty());
}

void TstCore::layoutPresetsInsideCanvas()
{
	const QRectF canvas(0, 0, 1, 1);
	for (const auto &p : layoutPresets()) {
		for (int n = 1; n <= 5; ++n) {
			const auto rects = layoutRects(p.id, n);
			QCOMPARE(rects.size(), n);
			for (const auto &r : rects) {
				QVERIFY2(canvas.adjusted(-1e-9, -1e-9, 1e-9, 1e-9).contains(r), qPrintable(p.id));
				QVERIFY(r.width() > 0 && r.height() > 0);
			}
		}
	}
	const auto sbs = layoutRects("side_by_side", 2);
	QVERIFY(!sbs[0].intersects(sbs[1].adjusted(1e-6, 0, 0, 0)));
}

void TstCore::audioSafety()
{
	StudioProfile p = sampleProfile();
	QVERIFY(audioSafetyWarnings(p).isEmpty());
	SourceConfig dup = p.sources[1];
	dup.uid = "src-mic2";
	dup.name = "Mic Duplicado";
	p.sources.append(dup);
	QCOMPARE(audioSafetyWarnings(p).size(), 1);
	p.sources.removeLast();
	SourceConfig desk;
	desk.uid = "src-desk";
	desk.name = "Áudio do PC";
	desk.kind = SourceKind::AudioOutput;
	desk.device = {"default", "Padrão"};
	p.sources.append(desk);
	QCOMPARE(audioSafetyWarnings(p).size(), 1); // mic monitored + desktop captured
	p.sources.last().audio.monitor = MonitorMode::MonitorOnly;
	QCOMPARE(audioSafetyWarnings(p).size(), 2);
}

void TstCore::faderTaperMonotonic()
{
	double prev = -1000;
	for (int i = 0; i <= 1000; ++i) {
		const double db = faderToDb(i / 1000.0);
		QVERIFY(db >= prev);
		prev = db;
		QVERIFY(qAbs(dbToFader(db) - i / 1000.0) < 1e-9);
	}
	QCOMPARE(faderToDb(0), -100.0);
	QVERIFY(qAbs(faderToDb(dbToFader(0.0))) < 1e-9);
	QVERIFY(qAbs(dbToMul(-6.0206) - 0.5) < 1e-4);
	QCOMPARE(mulToDb(0), -100.0);
}

QTEST_GUILESS_MAIN(TstCore)
#include "tst_core.moc"
