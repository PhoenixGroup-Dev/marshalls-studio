// Integration tests against the real libobs engine.
//
// Requires the environment from scripts/linux/test-env.sh (Xvfb display,
// PulseAudio with two virtual microphones, nginx-rtmp on 127.0.0.1:1935).
// Every assertion inspects real output: program pixels read back from the
// GPU, the program audio mix, recorded files (ffprobe/ffmpeg) and the stream
// received by the RTMP server.
#include "Log.h"
#include "LowerThird.h"
#include "Planning.h"
#include "Studio.h"

#include <obs.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QApplication>
#include <QWidget>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <functional>
#include <thread>

#include <atomic>
#include <cmath>
#include <deque>

using namespace ms;

namespace {

QString g_work; // working dir for generated media and recordings

QString run(const QString &program, const QStringList &args, int *exitCode = nullptr, int timeoutMs = 120000)
{
	QProcess p;
	p.setProcessChannelMode(QProcess::MergedChannels);
	p.start(program, args);
	p.waitForFinished(timeoutMs);
	if (exitCode)
		*exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
	return QString::fromUtf8(p.readAll());
}

QJsonObject ffprobe(const QString &path)
{
	const QString out = run("ffprobe", {"-v", "error", "-print_format", "json", "-show_streams", "-show_format", path});
	return QJsonDocument::fromJson(out.toUtf8()).object();
}

int countStreams(const QJsonObject &probe, const QString &type)
{
	int n = 0;
	for (const auto &s : probe.value("streams").toArray())
		if (s.toObject().value("codec_type").toString() == type)
			++n;
	return n;
}

double meanVolume(const QString &path, int audioIndex)
{
	const QString out = run("ffmpeg", {"-hide_banner", "-nostats", "-i", path, "-map", QString("0:a:%1").arg(audioIndex),
					   "-af", "volumedetect", "-f", "null", "-"});
	const auto m = QRegularExpression("mean_volume: (-?[0-9.]+) dB").match(out);
	return m.hasMatch() ? m.captured(1).toDouble() : -200.0;
}

bool decodesCleanly(const QString &path, QString *errors)
{
	int code = 0;
	const QString out = run("ffmpeg", {"-v", "error", "-i", path, "-f", "null", "-"}, &code);
	*errors = out.trimmed();
	return code == 0;
}

// Times (s) of the first frame of each white flash in a video.
QVector<double> flashTimes(const QString &path)
{
	const QString out = run("ffmpeg", {"-hide_banner", "-nostats", "-i", path, "-map", "0:v:0", "-vf",
					   "signalstats,metadata=print:key=lavfi.signalstats.YAVG", "-f", "null", "-"});
	QVector<double> times;
	double t = -1;
	bool wasBright = false;
	const auto lines = out.split('\n');
	for (const auto &l : lines) {
		auto mt = QRegularExpression("pts_time:([0-9.]+)").match(l);
		if (mt.hasMatch())
			t = mt.captured(1).toDouble();
		auto my = QRegularExpression("YAVG=([0-9.]+)").match(l);
		if (my.hasMatch()) {
			const bool bright = my.captured(1).toDouble() > 100.0;
			if (bright && !wasBright)
				times.append(t);
			wasBright = bright;
		}
	}
	return times;
}

// Times (s) where a beep starts (end of silence) in an audio track.
QVector<double> beepTimes(const QString &path, int audioIndex)
{
	const QString out =
		run("ffmpeg", {"-hide_banner", "-nostats", "-i", path, "-map", QString("0:a:%1").arg(audioIndex), "-af",
			       "silencedetect=n=-35dB:d=0.2", "-f", "null", "-"});
	QVector<double> times;
	auto it = QRegularExpression("silence_end: ([0-9.]+)").globalMatch(out);
	while (it.hasNext())
		times.append(it.next().captured(1).toDouble());
	return times;
}

// Median (beep - flash) in milliseconds.
double avOffsetMs(const QString &path)
{
	const auto flashes = flashTimes(path);
	const auto beeps = beepTimes(path, 0);
	QVector<double> diffs;
	for (double b : beeps) {
		double best = 1e9;
		for (double f : flashes)
			if (std::abs(b - f) < std::abs(best))
				best = b - f;
		if (std::abs(best) < 0.45)
			diffs.append(best * 1000.0);
	}
	if (diffs.isEmpty())
		return 1e9;
	std::sort(diffs.begin(), diffs.end());
	qInfo() << "A/V: flashes" << flashes.size() << "beeps" << beeps.size() << "offsets(ms)" << diffs;
	return diffs[diffs.size() / 2];
}

// Program mix analyzer (mix 0 = program).
struct MixProbe {
	QMutex mutex;
	std::deque<float> samples; // left channel, last ~0.5 s
	static void callback(void *param, size_t, struct audio_data *data)
	{
		auto *self = static_cast<MixProbe *>(param);
		const float *left = reinterpret_cast<const float *>(data->data[0]);
		QMutexLocker l(&self->mutex);
		for (uint32_t i = 0; i < data->frames; ++i)
			self->samples.push_back(left[i]);
		while (self->samples.size() > 24000)
			self->samples.pop_front();
	}
	double goertzel(double freq)
	{
		QMutexLocker l(&mutex);
		const size_t n = samples.size();
		if (n < 4800)
			return 0.0;
		const double w = 2.0 * M_PI * freq / 48000.0;
		const double coeff = 2.0 * std::cos(w);
		double s1 = 0, s2 = 0;
		for (float x : samples) {
			const double s0 = x + coeff * s1 - s2;
			s2 = s1;
			s1 = s0;
		}
		return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) / double(n);
	}
	double rms()
	{
		QMutexLocker l(&mutex);
		if (samples.empty())
			return 0.0;
		double acc = 0;
		for (float x : samples)
			acc += double(x) * x;
		return std::sqrt(acc / double(samples.size()));
	}
	void clear()
	{
		QMutexLocker l(&mutex);
		samples.clear();
	}
};

bool near(const QColor &a, const QColor &b, int tol)
{
	return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
	       std::abs(a.blue() - b.blue()) <= tol;
}

QString colorStr(const QColor &c)
{
	return QString("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
}

} // namespace

class TstEngine : public QObject {
	Q_OBJECT
public:
	std::unique_ptr<Studio> studio;
	QTemporaryDir configDir;
	MixProbe mix;
	QString red, green, screen, media, image, micA, micB, scene, lt, ovl;

	Engine &engine() { return studio->engine(); }

	QImage programImage(int waitNewFrames = 2)
	{
		quint64 serial = 0;
		engine().thumbnails().latest(ThumbnailService::kProgramKey, &serial);
		const quint64 target = serial + quint64(waitNewFrames);
		(void)QTest::qWaitFor([&] {
			quint64 s = 0;
			engine().thumbnails().latest(ThumbnailService::kProgramKey, &s);
			return s >= target;
		}, 3000);
		return engine().thumbnails().latest(ThumbnailService::kProgramKey);
	}

	QColor programPixel(double fx, double fy)
	{
		const QImage img = programImage();
		if (img.isNull())
			return {};
		return img.pixelColor(std::min(img.width() - 1, int(fx * img.width())),
				      std::min(img.height() - 1, int(fy * img.height())));
	}

	bool waitProgramColor(double fx, double fy, const QColor &expected, int tol = 30, int timeoutMs = 4000)
	{
		QColor last;
		const bool ok = QTest::qWaitFor([&] {
			last = programPixel(fx, fy);
			return near(last, expected, tol);
		}, timeoutMs);
		if (!ok)
			qWarning() << "program pixel" << fx << fy << colorStr(last) << "expected" << colorStr(expected);
		return ok;
	}

	QString recordFor(int ms, std::function<void()> during = {})
	{
		QSignalSpy started(studio.get(), &Studio::recordingChanged);
		QString err;
		if (!studio->startRecording(&err)) {
			qWarning() << "recording failed:" << err;
			return {};
		}
		const QString path = engine().outputs().recordingPath();
		(void)QTest::qWaitFor([&] { return engine().outputs().isRecording(); }, 5000);
		if (during)
			during();
		QTest::qWait(ms);
		QSignalSpy stopped(studio.get(), &Studio::recordingChanged);
		studio->stopRecording();
		(void)QTest::qWaitFor([&] { return !engine().outputs().isRecording() && stopped.count() > 0; }, 15000);
		QTest::qWait(300);
		return path;
	}

private slots:
	void initTestCase();
	void t01_devicesDetected();
	void t02_addSources();
	void t03_thumbnailsShowRealContent();
	void t04_cutChangesProgram();
	void t05_transitionIsReal();
	void t06_fadeToBlack();
	void t07_compositionKeepsSources();
	void t08_lowerThirdOnProgram();
	void t09_overlayOnProgram();
	void t10_twoMicrophonesIndependent();
	void t11_recordingSurvivesSwitches();
	void t12_avSyncAndOffset();
	void t13_streamingToRtmpServer();
	void t14_streamDelay();
	void t15_missingCameraDoesNotFreeze();
	void t16_microphoneDisconnectAndRecovery();
	void t17_invalidMediaReported();
	void t18_lowDiskStopsRecording();
	void t19_overloadStaysResponsive();
	void t20_crashLeavesPlayableMkv();
	void t21_restartRestoresConfiguration();
	void cleanupTestCase();
};

void TstEngine::initTestCase()
{
	g_work = configDir.path() + "/work";
	QDir().mkpath(g_work);
	Log::instance().setEchoToStderr(qEnvironmentVariableIsSet("MS_LOG_STDERR"));
	Log::instance().open(configDir.path() + "/logs");

	// Sync test clip: white flash (2 frames) and 1 kHz beep (60 ms) every second from t=1.
	int code = 0;
	run("ffmpeg", {"-y", "-v", "error", "-f", "lavfi", "-i", "color=c=black:s=640x360:r=30:d=7", "-f", "lavfi", "-i",
		       "sine=f=1000:sample_rate=48000:d=7", "-vf",
		       "drawbox=c=white:t=fill:enable='gte(t,1)*lt(mod(t,1),0.066)'", "-af",
		       "volume=0:enable='lt(t,1)+gte(mod(t,1),0.06)'", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a",
		       "aac", "-shortest", g_work + "/sync.mp4"},
	    &code);
	QCOMPARE(code, 0);
	QFile bad(g_work + "/bad.mp4");
	QVERIFY(bad.open(QIODevice::WriteOnly));
	bad.write("this is not a video file");
	bad.close();
	QImage yellow(64, 64, QImage::Format_RGBA8888);
	yellow.fill(QColor(255, 220, 0));
	QVERIFY(yellow.save(g_work + "/logo.png"));
	QImage stripes(640, 360, QImage::Format_RGB32);
	stripes.fill(QColor(0, 160, 160));
	QVERIFY(stripes.save(g_work + "/slide.png"));

	Studio::Paths paths;
	paths.configRoot = configDir.path() + "/config";
	paths.logDir = configDir.path() + "/logs";
	paths.moduleConfig = configDir.path() + "/plugin_config";
	paths.secretsDir = configDir.path() + "/secrets";
	studio = std::make_unique<Studio>(paths);

	// Start from a profile with a modest canvas (software rendering in CI).
	StudioProfile p;
	p.name = "Teste";
	p.video.baseWidth = 1280;
	p.video.baseHeight = 720;
	p.video.outputWidth = 640;
	p.video.outputHeight = 360;
	p.recording.folder = g_work + "/rec";
	p.recording.encoder.encoderId = "obs_x264";
	p.recording.encoder.rateControl = RateControl::CBR;
	p.recording.encoder.videoBitrateKbps = 1500;
	p.recording.minFreeMb = 200;
	p.ui.thumbnailWidth = 256;
	p.ui.thumbnailFps = 15;
	ConfigStore(paths.configRoot).save(p);

	EngineInit init;
	init.moduleConfigDir = paths.moduleConfig;
	init.x11Display = ObsPlatform::qtX11Display();
	QVERIFY2(init.x11Display, "QT_QPA_PLATFORM must be xcb with DISPLAY set");
	QString err;
	QVERIFY2(studio->start("Teste", init, &err), qPrintable(err));
	engine().thumbnails().enableProgramCapture(true);

	audio_convert_info conv = {};
	conv.samples_per_sec = 48000;
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = SPEAKERS_STEREO;
	obs_add_raw_audio_callback(0, &conv, &MixProbe::callback, &mix);
}

void TstEngine::t01_devicesDetected()
{
	bool known = false;
	QVERIFY(QTest::qWaitFor([&] {
		Engine::cachedDevices(SourceKind::AudioInput, &known);
		return known;
	}, 10000));
	const auto mics = Engine::cachedDevices(SourceKind::AudioInput);
	QStringList ids;
	for (const auto &d : mics)
		ids << d.id;
	qInfo() << "microphones:" << ids;
	QVERIFY(ids.contains("ms_mic_a"));
	QVERIFY(ids.contains("ms_mic_b"));
	QVERIFY(QTest::qWaitFor([&] { return !Engine::cachedDevices(SourceKind::Screen).isEmpty(); }, 5000));
	const auto screens = Engine::cachedDevices(SourceKind::Screen);
	qInfo() << "screens:" << screens.size();
	QVERIFY(!screens.isEmpty());
	const auto cams = Engine::cachedDevices(SourceKind::Camera, &known);
	qInfo() << "cameras:" << cams.size() << "(container has no /dev/video*)";
	QVERIFY(known);
}

void TstEngine::t02_addSources()
{
	QString err;
	SourceConfig c;
	c.kind = SourceKind::Color;
	c.name = "Vermelho";
	c.engineSettings = QJsonObject{{"color", double(0xFF2020E0u)}}; // ABGR: rgb(224,32,32)
	red = studio->addSource(c, &err);
	QVERIFY2(!red.isEmpty(), qPrintable(err));
	c.name = "Verde";
	c.engineSettings = QJsonObject{{"color", double(0xFF20C020u)}};
	green = studio->addSource(c, &err);
	QVERIFY(!green.isEmpty());

	SourceConfig s;
	s.kind = SourceKind::Screen;
	s.name = "Tela";
	s.device = {"0", "Tela 0"};
	screen = studio->addSource(s, &err);
	QVERIFY2(!screen.isEmpty(), qPrintable(err));

	SourceConfig m;
	m.kind = SourceKind::Media;
	m.name = "Vinheta sync";
	m.engineSettings = QJsonObject{{"local_file", g_work + "/sync.mp4"}};
	m.media.loop = true;
	media = studio->addSource(m, &err);
	QVERIFY(!media.isEmpty());

	SourceConfig im;
	im.kind = SourceKind::Image;
	im.name = "Slide";
	im.engineSettings = QJsonObject{{"file", g_work + "/slide.png"}};
	image = studio->addSource(im, &err);
	QVERIFY(!image.isEmpty());

	SourceConfig a;
	a.kind = SourceKind::AudioInput;
	a.name = "Mic Host";
	a.device = {"ms_mic_a", "Sine 440"};
	micA = studio->addSource(a, &err);
	QVERIFY(!micA.isEmpty());
	a.name = "Mic Convidado";
	a.device = {"ms_mic_b", "Sine 880"};
	micB = studio->addSource(a, &err);
	QVERIFY(!micB.isEmpty());

	// The same physical device cannot be captured twice.
	a.name = "Duplicado";
	a.device = {"ms_mic_a", "Sine 440"};
	QVERIFY(studio->addSource(a, &err).isEmpty());
	QVERIFY(err.contains("já está em uso"));

	for (const QString &uid : {red, green, screen, media, image, micA, micB}) {
		QVERIFY2(QTest::qWaitFor([&] { return engine().runtime(uid).health == SourceHealth::Ok; }, 8000),
			 qPrintable(studio->profile().findSource(uid)->name + ": " + healthLabel(engine().runtime(uid).health) +
				    " " + engine().runtime(uid).detail));
	}
	QCOMPARE(engine().runtime(screen).width, 1920);
	QCOMPARE(studio->gridOrder().size(), 5); // video sources only; mics live in the mixer
	QCOMPARE(studio->audioChannels().size(), 3); // two mics + media audio
}

void TstEngine::t03_thumbnailsShowRealContent()
{
	auto center = [&](const QString &key) {
		const QImage img = engine().thumbnails().latest(key);
		return img.isNull() ? QColor() : img.pixelColor(img.width() / 2, img.height() / 2);
	};
	QVERIFY(QTest::qWaitFor([&] { return near(center(red), QColor(224, 32, 32), 30); }, 5000));
	QVERIFY(QTest::qWaitFor([&] { return near(center(green), QColor(32, 192, 32), 30); }, 5000));
	// Screen capture: a real window painted on the X display is captured.
	QWidget probe;
	probe.setWindowFlags(Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
	probe.setAutoFillBackground(true);
	QPalette pal = probe.palette();
	pal.setColor(QPalette::Window, QColor(0x2a, 0x55, 0x99));
	probe.setPalette(pal);
	probe.setGeometry(0, 0, 1920, 1080);
	probe.show();
	QTest::qWait(1000);
	{
		// Independent reference: what the X server actually shows at the same spot.
		const QString png = g_work + "/x11grab.png";
		run("ffmpeg", {"-v", "error", "-y", "-f", "x11grab", "-video_size", "1920x1080", "-i",
			       qEnvironmentVariable("DISPLAY"), "-frames:v", "1", png});
		const QImage ref(png);
		const QImage th = engine().thumbnails().latest(screen);
		qInfo() << "x11grab (480,270)" << colorStr(ref.pixelColor(480, 270)) << "engine thumbnail"
			<< colorStr(th.pixelColor(th.width() / 4, th.height() / 4));
	}
	// Sampled away from the screen centre, where the captured mouse cursor sits.
	auto quarter = [&](const QString &key) {
		const QImage img = engine().thumbnails().latest(key);
		return img.isNull() ? QColor() : img.pixelColor(img.width() / 4, img.height() / 4);
	};
	QVERIFY2(QTest::qWaitFor([&] { return near(quarter(screen), QColor(0x2a, 0x55, 0x99), 30); }, 5000),
		 qPrintable(colorStr(quarter(screen))));
	probe.hide();
	QVERIFY(QTest::qWaitFor([&] { return near(center(image), QColor(0, 160, 160), 30); }, 5000));
	quint64 s1 = 0, s2 = 0;
	engine().thumbnails().latest(media, &s1);
	QTest::qWait(500);
	engine().thumbnails().latest(media, &s2);
	QVERIFY2(s2 > s1, "media thumbnail must keep updating");
	const QImage t = engine().thumbnails().latest(red);
	QCOMPARE(t.width(), 256);
	QCOMPARE(t.height(), 144);
}

void TstEngine::t04_cutChangesProgram()
{
	studio->select(ContentRef::source(red));
	auto d = studio->take(TakeMode::Cut);
	QVERIFY(d.accepted);
	QVERIFY(waitProgramColor(0.5, 0.5, QColor(224, 32, 32)));
	QCOMPARE(studio->program().program(), ContentRef::source(red));
	studio->select(ContentRef::source(green));
	QVERIFY(studio->take(TakeMode::Cut).accepted);
	QVERIFY(waitProgramColor(0.5, 0.5, QColor(32, 192, 32)));
	// Taking what is already on air is rejected without side effects.
	QVERIFY(!studio->take(TakeMode::Cut).accepted);
}

void TstEngine::t05_transitionIsReal()
{
	TransitionSettings tr = studio->profile().transition;
	tr.type = TransitionType::Fade;
	tr.durationMs = 1500;
	studio->setTransition(tr);
	QSignalSpy done(studio.get(), &Studio::transitionStateChanged);
	studio->select(ContentRef::source(red));
	QVERIFY(studio->take(TakeMode::Auto).accepted);
	QVERIFY(studio->program().isTransitioning());
	// A conflicting command during the transition is refused.
	studio->select(ContentRef::source(image));
	QVERIFY(!studio->take(TakeMode::Cut).accepted);
	QTest::qWait(500);
	const QColor mid = programPixel(0.5, 0.5);
	qInfo() << "mid-transition pixel" << colorStr(mid) << "progress" << engine().transitionProgress();
	QVERIFY2(mid.red() > 60 && mid.green() > 60, qPrintable(colorStr(mid)));
	QVERIFY(QTest::qWaitFor([&] { return !studio->program().isTransitioning(); }, 4000));
	QCOMPARE(studio->program().program(), ContentRef::source(red));
	QVERIFY(waitProgramColor(0.5, 0.5, QColor(224, 32, 32)));
	tr.durationMs = 300;
	studio->setTransition(tr);
}

void TstEngine::t06_fadeToBlack()
{
	QVERIFY(studio->toggleFadeToBlack().accepted);
	QCOMPARE(studio->program().ftbState(), FtbState::FadingOut);
	QVERIFY(QTest::qWaitFor([&] { return studio->program().ftbState() == FtbState::Black; }, 4000));
	QVERIFY(waitProgramColor(0.5, 0.5, QColor(0, 0, 0), 12));
	QVERIFY(studio->toggleFadeToBlack().accepted);
	QVERIFY(QTest::qWaitFor([&] { return studio->program().ftbState() == FtbState::Off; }, 4000));
	QVERIFY(waitProgramColor(0.5, 0.5, QColor(224, 32, 32)));
}

void TstEngine::t07_compositionKeepsSources()
{
	SceneConfig sc;
	sc.name = "Lado a lado";
	const auto rects = layoutRects("side_by_side", 2);
	sc.items = {SceneItemConfig{red, rects[0], true, true}, SceneItemConfig{green, rects[1], true, true}};
	scene = studio->addScene(sc);
	studio->select(ContentRef::scene(scene));
	QVERIFY(studio->take(TakeMode::Cut).accepted);
	QVERIFY(waitProgramColor(0.25, 0.5, QColor(224, 32, 32)));
	QVERIFY(waitProgramColor(0.75, 0.5, QColor(32, 192, 32)));
	// The individual source stays intact in its own thumbnail.
	const QImage t = engine().thumbnails().latest(red);
	QVERIFY(near(t.pixelColor(t.width() - 4, t.height() / 2), QColor(224, 32, 32), 30));
	// Editing the composition on air applies atomically.
	SceneConfig edit = *studio->profile().findScene(scene);
	std::swap(edit.items[0].rect, edit.items[1].rect);
	studio->updateScene(edit);
	QVERIFY(waitProgramColor(0.25, 0.5, QColor(32, 192, 32)));
	QCOMPARE(studio->gridOrder().size(), 6);
}

void TstEngine::t08_lowerThirdOnProgram()
{
	LowerThirdConfig c;
	c.templateName = "Convidada";
	c.name = "Ana Souza";
	c.role = "Diretora de Produção";
	c.company = "Marshalls";
	c.accent = QColor(255, 200, 0);
	c.animMs = 300;
	lt = studio->addLowerThird(c);
	const RenderedGraphic g = LowerThirdRenderer::render(c, QSize(1280, 720));
	QVERIFY(!g.image.isNull());
	const double fx = (g.placement.x() + 2) / 1280.0;
	const double fy = (g.placement.y() + g.placement.height() / 2.0) / 720.0;
	const QColor before = programPixel(fx, fy);
	QVERIFY(!near(before, c.accent, 40));
	studio->toggleLowerThird(lt);
	QVERIFY(waitProgramColor(fx, fy, c.accent, 40));
	QVERIFY(QTest::qWaitFor([&] { return engine().lowerThirdPhase(lt) == GraphicPhase::Hold; }, 2000));
	// Switching program content does not remove the graphic.
	studio->select(ContentRef::source(green));
	QVERIFY(studio->take(TakeMode::Cut).accepted);
	QVERIFY(waitProgramColor(fx, fy, c.accent, 40));
	studio->toggleLowerThird(lt);
	QVERIFY(QTest::qWaitFor([&] { return engine().lowerThirdPhase(lt) == GraphicPhase::Hidden; }, 2000));
	QVERIFY(waitProgramColor(fx, fy, QColor(32, 192, 32)));
	// Auto-out after the configured duration.
	c = *studio->profile().findLowerThird(lt);
	c.holdSeconds = 1.0;
	studio->updateLowerThird(c);
	studio->toggleLowerThird(lt);
	QVERIFY(waitProgramColor(fx, fy, c.accent, 40));
	QVERIFY(QTest::qWaitFor([&] { return engine().lowerThirdPhase(lt) == GraphicPhase::Hidden; }, 3500));
}

void TstEngine::t09_overlayOnProgram()
{
	OverlayImageConfig o;
	o.name = "Logo";
	o.path = g_work + "/logo.png";
	o.rect = QRectF(0.85, 0.05, 0.1, 0.1);
	o.fadeMs = 200;
	ovl = studio->addOverlay(o);
	QVERIFY(waitProgramColor(0.9, 0.1, QColor(32, 192, 32)));
	studio->toggleOverlay(ovl);
	QVERIFY(waitProgramColor(0.9, 0.1, QColor(255, 220, 0), 40));
	studio->clearGraphics();
	QVERIFY(waitProgramColor(0.9, 0.1, QColor(32, 192, 32)));
}

void TstEngine::t10_twoMicrophonesIndependent()
{
	// Media is not on program, so the program mix contains only the microphones.
	QVERIFY(QTest::qWaitFor([&] { return engine().audioLevel(micA).peakDb > -40 && engine().audioLevel(micB).peakDb > -40; },
				5000));
	QTest::qWait(700);
	{
		QMutexLocker l(&mix.mutex);
		qInfo() << "mix samples buffered" << mix.samples.size() << "engine program peak" << engine().programLevel().peakDb;
	}
	const double a440 = mix.goertzel(440), a880 = mix.goertzel(880);
	qInfo() << "program mix: 440Hz" << a440 << "880Hz" << a880 << "rms" << mix.rms();
	QVERIFY(a440 > 0.01);
	QVERIFY(a880 > 0.01);

	// libobs channel meters are post-fader / pre-mute (the UI greys them out when
	// muted), so muting is verified on the program mix itself.
	studio->setMuted(micA, true);
	QVERIFY(QTest::qWaitFor([&] { return engine().audioLevel(micB).peakDb > -40; }, 2000));
	mix.clear();
	QTest::qWait(800);
	const double m440 = mix.goertzel(440), m880 = mix.goertzel(880);
	qInfo() << "mic A muted: 440Hz" << m440 << "880Hz" << m880;
	QVERIFY(m440 < a440 * 0.05);
	QVERIFY(m880 > a880 * 0.5);

	// Fader: -20 dB on mic B must scale its component in the program mix by 0.1.
	const double b880 = mix.goertzel(880);
	studio->setVolumeDb(micB, -20.0);
	mix.clear();
	QTest::qWait(800);
	const double c880 = mix.goertzel(880);
	qInfo() << "mic B 880Hz in program" << b880 << "-> at -20 dB" << c880 << "ratio" << c880 / b880;
	QVERIFY(std::abs(c880 / b880 - 0.1) < 0.025);

	studio->setMuted(micA, false);
	studio->setVolumeDb(micB, 0.0);
	studio->setSyncOffset(micB, 120);
	QVERIFY(QTest::qWaitFor([&] { return obs_source_get_sync_offset(engine().obsSource(micB)) == int64_t(120) * 1000000; }, 1000));
	studio->setSyncOffset(micB, 0);

	AudioFilters f;
	f.gateEnabled = true;
	f.compEnabled = true;
	f.limiterEnabled = true;
	f.eqEnabled = true;
	f.suppressEnabled = true;
	f.gainDb = 3;
	studio->setFilters(micA, f);
	QStringList names;
	obs_source_enum_filters(
		engine().obsSource(micA),
		[](obs_source_t *, obs_source_t *flt, void *p) {
			static_cast<QStringList *>(p)->append(QString::fromUtf8(obs_source_get_name(flt)));
		},
		&names);
	qInfo() << "mic A filter chain" << names;
	QCOMPARE(names, QStringList({"ms.gain", "ms.suppress", "ms.gate", "ms.eq", "ms.compressor", "ms.limiter"}));
	QVERIFY(QTest::qWaitFor([&] { return engine().audioLevel(micA).peakDb > -40; }, 3000));
	studio->setFilters(micA, AudioFilters{});
}

void TstEngine::t11_recordingSurvivesSwitches()
{
	const QString path = recordFor(6000, [&] {
		QTest::qWait(800);
		studio->select(ContentRef::source(red));
		QVERIFY(studio->take(TakeMode::Cut).accepted);
		QTest::qWait(800);
		studio->select(ContentRef::source(screen));
		QVERIFY(studio->take(TakeMode::Auto).accepted);
		QTest::qWait(800);
		QVERIFY(engine().outputs().isRecording());
		studio->select(ContentRef::scene(scene));
		QVERIFY(studio->take(TakeMode::Cut).accepted);
		studio->toggleLowerThird(lt);
		QTest::qWait(800);
		QVERIFY(engine().outputs().isRecording());
	});
	QVERIFY(!path.isEmpty());
	qInfo() << "recording:" << path << QFileInfo(path).size() << "bytes";
	const QJsonObject probe = ffprobe(path);
	QCOMPARE(countStreams(probe, "video"), 1);
	QCOMPARE(countStreams(probe, "audio"), 3); // program + 2 isolated microphones (media has no track)
	const double duration = probe.value("format").toObject().value("duration").toString().toDouble();
	qInfo() << "duration" << duration;
	QVERIFY(duration > 5.0);
	QString errors;
	QVERIFY2(decodesCleanly(path, &errors), qPrintable(errors));
	const double prog = meanVolume(path, 0), isoA = meanVolume(path, 1), isoB = meanVolume(path, 2);
	qInfo() << "mean volume program/micA/micB" << prog << isoA << isoB;
	QVERIFY(prog > -40);
	QVERIFY(isoA > -40);
	QVERIFY(isoB > -40);
	if (studio->profile().lowerThirds.size())
		studio->showLowerThird(lt, false);
}

void TstEngine::t12_avSyncAndOffset()
{
	// (1) Pipeline A/V sync: a clip with flashes and beeps at the same instants.
	studio->setMuted(micA, true);
	studio->setMuted(micB, true);
	studio->select(ContentRef::source(media));
	QVERIFY(studio->take(TakeMode::Cut).accepted);
	QTest::qWait(500);
	const QString p1 = recordFor(6500);
	const double off1 = avOffsetMs(p1);
	qInfo() << "A/V offset of the pipeline:" << off1 << "ms";
	// Software rendering (Mesa llvmpipe, no GPU) misses compositor frames, which
	// delays video presentation; the CI tolerance is 3 frames at 30 fps. The
	// hardware validation plan requires <= 1 frame on a real GPU.
	QVERIFY2(std::abs(off1) <= 100.0, "audio and video must be within 3 frames under software rendering");

	// (2) Video sync compensation: delaying the clip's video by 200 ms makes the
	//     flash appear 200 ms after its beep.
	SourceConfig mc = *studio->profile().findSource(media);
	mc.videoDelayMs = 200;
	QVERIFY(studio->updateSource(mc));
	QTest::qWait(1000);
	const QString p2 = recordFor(6500);
	const double off2 = avOffsetMs(p2);
	qInfo() << "A/V offset with 200 ms video delay:" << off2 << "ms (beep before flash)";
	// Render delay is frame based: with compositor frames missed under software
	// rendering (no GPU) the measured shift drifts (observed 115-200 ms for 200 ms).
	// CI checks that the delay is applied; +-1 frame precision is validated on a
	// real GPU (docs/HARDWARE_TEST_PLAN.md, H-08).
	const double shift = off1 - off2;
	QVERIFY2(shift >= 100.0 && shift <= 300.0, qPrintable(QString::number(shift)));
	mc.videoDelayMs = 0;
	QVERIFY(studio->updateSource(mc));

	// (3) Per-input audio delay between independent inputs: two inputs receive
	//     the same beeps; +200 ms on one shows up between the isolated tracks.
	QString err;
	SourceConfig c;
	c.kind = SourceKind::AudioInput;
	c.name = "Beeps C";
	c.device = {"ms_mic_c", "Beeps C"};
	const QString micC = studio->addSource(c, &err);
	c.name = "Beeps D";
	c.device = {"ms_mic_d", "Beeps D"};
	const QString micD = studio->addSource(c, &err);
	QVERIFY(!micC.isEmpty() && !micD.isEmpty());
	QVERIFY(QTest::qWaitFor([&] { return engine().runtime(micD).health == SourceHealth::Ok; }, 6000));
	const QStringList tracks = engine().isolatedTrackSources();
	const int idxC = int(tracks.indexOf(micC)) + 1, idxD = int(tracks.indexOf(micD)) + 1;
	QVERIFY(idxC > 0 && idxD > 0);
	auto trackOffsetMs = [&](const QString &path) {
		const auto bc = beepTimes(path, idxC), bd = beepTimes(path, idxD);
		QVector<double> d;
		for (double t : bd) {
			double best = 1e9;
			for (double u : bc)
				if (std::abs(t - u) < std::abs(best))
					best = t - u;
			if (std::abs(best) < 0.45)
				d.append(best * 1000.0);
		}
		std::sort(d.begin(), d.end());
		qInfo() << "track D - track C beeps (ms):" << d;
		return d.isEmpty() ? 1e9 : d[d.size() / 2];
	};
	QTest::qWait(1000);
	const double base = trackOffsetMs(recordFor(4500));
	studio->setSyncOffset(micD, 200);
	QTest::qWait(1000);
	const double delayed = trackOffsetMs(recordFor(4500));
	qInfo() << "input D relative to C: base" << base << "ms, with +200 ms sync offset" << delayed << "ms";
	QVERIFY(std::abs(base) <= 15.0);
	// Each capture stream carries its own latency estimate: tolerance < 1 frame.
	QVERIFY(std::abs((delayed - base) - 200.0) <= 25.0);
	studio->removeSource(micC);
	studio->removeSource(micD);

	studio->setMuted(micA, false);
	studio->setMuted(micB, false);
	studio->select(ContentRef::source(green));
	QVERIFY(studio->take(TakeMode::Cut).accepted);
}

void TstEngine::t13_streamingToRtmpServer()
{
	const QString rtmpDir = qEnvironmentVariable("MS_TEST_ENV", "/tmp/ms-test-env") + "/rtmp";
	for (const auto &f : QDir(rtmpDir).entryInfoList({"*.flv"}, QDir::Files))
		QFile::remove(f.absoluteFilePath());
	StreamSettings s = studio->profile().stream;
	s.platform = "custom";
	s.server = "rtmp://127.0.0.1:1935/live";
	s.encoder.encoderId = "obs_x264";
	s.encoder.videoBitrateKbps = 1200;
	s.delaySec = 0;
	studio->setStreamSettings(s);
	const QString key = "ms-test-KEY-7f3a91c2";
	studio->setStreamKey(key);

	QSignalSpy changed(studio.get(), &Studio::streamingChanged);
	QString err;
	QVERIFY2(studio->startStreaming(&err), qPrintable(err));
	QVERIFY(QTest::qWaitFor([&] { return engine().outputs().stats().streaming && engine().outputs().stats().streamKbps > 100; },
				10000));
	QTest::qWait(4000);
	const OutputStats st = engine().outputs().stats();
	qInfo() << "stream kbps" << st.streamKbps << "dropped" << st.streamDropped << "/" << st.streamTotalFrames
		<< "congestion" << st.congestion;
	QVERIFY(st.streamKbps > 300);
	QVERIFY(st.streamDropPercent < 5.0);
	studio->stopStreaming();
	QVERIFY(QTest::qWaitFor([&] { return !engine().outputs().isStreaming(); }, 10000));
	QTest::qWait(1000);

	const auto files = QDir(rtmpDir).entryInfoList({"*.flv"}, QDir::Files);
	QVERIFY2(!files.isEmpty(), "the RTMP server must have recorded the stream");
	const QString flv = files.first().absoluteFilePath();
	QVERIFY2(flv.contains(key), "nginx names the file after the stream key it received");
	const QJsonObject probe = ffprobe(flv);
	QCOMPARE(countStreams(probe, "video"), 1);
	QCOMPARE(countStreams(probe, "audio"), 1);
	const double duration = probe.value("format").toObject().value("duration").toString().toDouble();
	qInfo() << "received stream:" << files.first().fileName().replace(key, "[KEY]") << duration << "s";
	QVERIFY(duration > 3.0);

	// The key must never reach the logs.
	Log::instance().close();
	for (const auto &fi : QDir(studio->paths().logDir).entryInfoList({"*.log"})) {
		QFile f(fi.absoluteFilePath());
		QVERIFY(f.open(QIODevice::ReadOnly));
		QVERIFY2(!f.readAll().contains(key.toUtf8()), qPrintable(fi.fileName()));
	}
	Log::instance().open(studio->paths().logDir);
	// Nor the profile JSON.
	QFile prof(studio->store().profilePath(studio->profile().name));
	studio->saveNow();
	QVERIFY(prof.open(QIODevice::ReadOnly));
	QVERIFY(!prof.readAll().contains(key.toUtf8()));
}

void TstEngine::t14_streamDelay()
{
	StreamSettings s = studio->profile().stream;
	s.delaySec = 3;
	studio->setStreamSettings(s);
	QString err;
	QElapsedTimer t;
	t.start();
	QVERIFY2(studio->startStreaming(&err), qPrintable(err));
	QVERIFY(QTest::qWaitFor([&] { return engine().outputs().stats().streaming; }, 8000));
	QVERIFY(QTest::qWaitFor([&] { return engine().outputs().stats().delaySec == 3; }, 5000));
	// No bytes leave before the delay elapses.
	QVERIFY(QTest::qWaitFor([&] { return engine().outputs().stats().streamKbps > 100; }, 10000));
	qInfo() << "first data after" << t.elapsed() << "ms with 3 s delay";
	QVERIFY(t.elapsed() >= 2500);
	studio->stopStreaming();
	QVERIFY(QTest::qWaitFor([&] { return !engine().outputs().isStreaming(); }, 15000));
	s.delaySec = 0;
	studio->setStreamSettings(s);
}

void TstEngine::t15_missingCameraDoesNotFreeze()
{
	int ticks = 0;
	qint64 maxGap = 0;
	QElapsedTimer gap;
	gap.start();
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] {
		maxGap = std::max(maxGap, gap.restart());
		++ticks;
	});
	timer.start(20);
	SourceConfig cam;
	cam.kind = SourceKind::Camera;
	cam.name = "Câmera inexistente";
	cam.device = {"/dev/video-nao-existe", "Webcam USB"};
	QString err;
	const QString uid = studio->addSource(cam, &err);
	QVERIFY2(!uid.isEmpty(), qPrintable(err));
	QVERIFY(QTest::qWaitFor([&] {
		const auto h = engine().runtime(uid).health;
		return h == SourceHealth::Disconnected || h == SourceHealth::NoSignal;
	}, 12000));
	qInfo() << "missing camera:" << healthLabel(engine().runtime(uid).health) << engine().runtime(uid).detail
		<< "max UI gap" << maxGap << "ms";
	QVERIFY(maxGap < 400);
	// Taking it to air shows black instead of crashing.
	QVERIFY(studio->takeContent(ContentRef::source(uid), TakeMode::Cut).accepted);
	QVERIFY(waitProgramColor(0.5, 0.5, QColor(0, 0, 0), 12));
	studio->removeSource(uid);
	QVERIFY(!studio->program().program().isValid());
}

void TstEngine::t16_microphoneDisconnectAndRecovery()
{
	const QString list = run("pactl", {"list", "short", "modules"});
	QString moduleId;
	for (const auto &l : list.split('\n'))
		if (l.contains("source_name=ms_mic_b") || (l.contains("module-remap-source") && l.contains("ms_mic_b")))
			moduleId = l.split('\t').value(0);
	QVERIFY(!moduleId.isEmpty());
	QElapsedTimer gap;
	qint64 maxGap = 0;
	gap.start();
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] { maxGap = std::max(maxGap, gap.restart()); });
	timer.start(20);

	run("pactl", {"unload-module", moduleId});
	QVERIFY(QTest::qWaitFor([&] {
		const auto h = engine().runtime(micB).health;
		return h == SourceHealth::NoSignal || h == SourceHealth::Disconnected;
	}, 8000));
	qInfo() << "mic B after unplug:" << healthLabel(engine().runtime(micB).health) << engine().runtime(micB).detail;
	QVERIFY(engine().runtime(micA).health == SourceHealth::Ok); // the other mic is unaffected

	run("pactl", {"load-module", "module-remap-source", "master=ms_play_b.monitor", "source_name=ms_mic_b"});
	QTest::qWait(3500); // device watcher refresh
	studio->engine().reconnectSource(micB);
	QVERIFY2(QTest::qWaitFor([&] { return engine().runtime(micB).health == SourceHealth::Ok; }, 8000),
		 qPrintable(engine().runtime(micB).detail));
	bool levelBack = QTest::qWaitFor([&] { return engine().audioLevel(micB).peakDb > -40; }, 5000);
	if (!levelBack) { // a freshly re-created PulseAudio source may start silent: operator retries
		qInfo() << "signal still silent after reconnect; retrying once";
		studio->engine().reconnectSource(micB);
		levelBack = QTest::qWaitFor([&] { return engine().audioLevel(micB).peakDb > -40; }, 6000);
	}
	qInfo() << "mic B peak after reconnect" << engine().audioLevel(micB).peakDb;
	QVERIFY(levelBack);
	qInfo() << "mic B recovered; max UI gap" << maxGap << "ms";
	QVERIFY(maxGap < 400);
}

void TstEngine::t17_invalidMediaReported()
{
	SourceConfig m;
	m.kind = SourceKind::Media;
	m.name = "Arquivo inválido";
	m.engineSettings = QJsonObject{{"local_file", g_work + "/bad.mp4"}};
	QString err;
	const QString uid = studio->addSource(m, &err);
	QVERIFY(!uid.isEmpty());
	QVERIFY(QTest::qWaitFor([&] { return engine().runtime(uid).health == SourceHealth::Error; }, 8000));
	qInfo() << "invalid media:" << engine().runtime(uid).detail;
	SourceConfig missing = m;
	missing.name = "Arquivo ausente";
	missing.engineSettings = QJsonObject{{"local_file", g_work + "/nao-existe.mp4"}};
	const QString uid2 = studio->addSource(missing, &err);
	QVERIFY(QTest::qWaitFor([&] { return engine().runtime(uid2).health == SourceHealth::Error; }, 4000));
	QCOMPARE(engine().runtime(uid2).detail, QString("Arquivo não encontrado"));
	studio->removeSource(uid);
	studio->removeSource(uid2);
}

void TstEngine::t18_lowDiskStopsRecording()
{
	RecordingSettings r = studio->profile().recording;
	QDir().mkpath(r.folder);
	const qint64 freeMb = QStorageInfo(r.folder).bytesAvailable() / (1024 * 1024);
	// 1) Refuses to start below the threshold.
	r.minFreeMb = int(freeMb + 500);
	studio->setRecordingSettings(r);
	QString err;
	QVERIFY(!studio->startRecording(&err));
	qInfo() << "refused:" << err;
	QVERIFY(err.contains("Espaço insuficiente"));
	// 2) Stops by itself when the free space crosses the threshold while recording.
	r.minFreeMb = int(freeMb - 2);
	studio->setRecordingSettings(r);
	QSignalSpy alerts(studio.get(), &Studio::alert);
	QVERIFY2(studio->startRecording(&err), qPrintable(err));
	const QString path = engine().outputs().recordingPath();
	QVERIFY(QTest::qWaitFor([&] { return engine().outputs().isRecording(); }, 5000));
	// Fill 4 MB of disk to cross the threshold quickly.
	QFile filler(r.folder + "/filler.bin");
	QVERIFY(filler.open(QIODevice::WriteOnly));
	filler.write(QByteArray(4 * 1024 * 1024, 'x'));
	filler.close();
	QVERIFY(QTest::qWaitFor([&] { return !engine().outputs().isRecording(); }, 15000));
	QFile::remove(r.folder + "/filler.bin");
	bool sawWarning = false;
	for (const auto &a : alerts)
		sawWarning |= a.at(1).toString().contains("espaço em disco");
	QVERIFY(sawWarning);
	QString errors;
	QTest::qWait(500);
	QVERIFY2(decodesCleanly(path, &errors), qPrintable(errors)); // the partial file is still valid
	r.minFreeMb = 200;
	studio->setRecordingSettings(r);
}

void TstEngine::t19_overloadStaysResponsive()
{
	std::atomic<bool> stop{false};
	std::vector<std::thread> burners;
	for (unsigned i = 0; i < std::max(2u, std::thread::hardware_concurrency()); ++i)
		burners.emplace_back([&] {
			volatile double x = 0;
			while (!stop)
				for (int k = 0; k < 100000; ++k)
					x += std::sqrt(double(k));
		});
	QElapsedTimer gap;
	qint64 maxGap = 0;
	gap.start();
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] { maxGap = std::max(maxGap, gap.restart()); });
	timer.start(20);
	QString err;
	QVERIFY(studio->startRecording(&err));
	for (int i = 0; i < 6; ++i) {
		QTest::qWait(500);
		studio->takeSlot(1 + (i % 3), TakeMode::Cut);
	}
	const OutputStats st = engine().outputs().stats();
	stop = true;
	for (auto &t : burners)
		t.join();
	studio->stopRecording();
	QVERIFY(QTest::qWaitFor([&] { return !engine().outputs().isRecording(); }, 10000));
	qInfo() << "under CPU stress: cpu" << st.cpuPercent << "% render lag" << st.renderLagPercent << "% encode skip"
		<< st.encodeSkipPercent << "% max UI gap" << maxGap << "ms";
	QVERIFY(maxGap < 1000);
}

void TstEngine::t20_crashLeavesPlayableMkv()
{
	QProcess child;
	child.setProcessChannelMode(QProcess::MergedChannels);
	const QString dir = g_work + "/crash";
	QDir().mkpath(dir);
	child.start(QCoreApplication::applicationFilePath(), {"--crash-recording", dir});
	QVERIFY(child.waitForFinished(60000));
	QVERIFY2(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0, "child must die abruptly");
	const auto files = QDir(dir).entryInfoList({"*.mkv"}, QDir::Files);
	QVERIFY2(!files.isEmpty(), qPrintable(QString::fromUtf8(child.readAll().right(2000))));
	const QString path = files.first().absoluteFilePath();
	const QJsonObject probe = ffprobe(path);
	const double duration = probe.value("format").toObject().value("duration").toString().toDouble();
	qInfo() << "MKV after abort():" << QFileInfo(path).size() << "bytes," << duration << "s";
	QVERIFY(countStreams(probe, "video") == 1);
	QVERIFY(duration > 2.0);
	QString errors;
	int code = 0;
	run("ffmpeg", {"-v", "error", "-i", path, "-f", "null", "-"}, &code);
	QCOMPARE(code, 0);
}

void TstEngine::t21_restartRestoresConfiguration()
{
	studio->setVolumeDb(micB, -12.0);
	studio->setMuted(micA, true);
	const int sources = studio->profile().sources.size();
	const int scenes = studio->profile().scenes.size();
	const QString paths = studio->paths().configRoot;
	obs_remove_raw_audio_callback(0, &MixProbe::callback, &mix);
	studio->stop();
	const Studio::Paths p = studio->paths();
	studio.reset();

	studio = std::make_unique<Studio>(p);
	EngineInit init;
	init.moduleConfigDir = p.moduleConfig;
	init.x11Display = ObsPlatform::qtX11Display();
	QString err;
	QVERIFY2(studio->start({}, init, &err), qPrintable(err));
	QCOMPARE(studio->profile().name, QString("Teste"));
	QCOMPARE(studio->profile().sources.size(), sources);
	QCOMPARE(studio->profile().scenes.size(), scenes);
	QCOMPARE(studio->profile().lowerThirds.size(), 1);
	QCOMPARE(studio->profile().overlays.size(), 1);
	QCOMPARE(studio->profile().transition.durationMs, 300);
	QCOMPARE(studio->streamKey(), QString("ms-test-KEY-7f3a91c2"));
	QVERIFY(std::abs(obs_source_get_volume(engine().obsSource(micB)) - float(dbToMul(-12.0))) < 1e-3);
	QVERIFY(obs_source_muted(engine().obsSource(micA)));
	QVERIFY(QTest::qWaitFor([&] { return engine().runtime(red).health == SourceHealth::Ok; }, 5000));
	engine().thumbnails().enableProgramCapture(true);
	studio->select(ContentRef::scene(scene));
	QVERIFY(studio->take(TakeMode::Cut).accepted);
	QVERIFY(waitProgramColor(0.75, 0.5, QColor(224, 32, 32)));
}

void TstEngine::cleanupTestCase()
{
	if (studio)
		studio->stop();
	studio.reset();
}

// Child mode for t20: record a few seconds and abort() without finalizing.
static int crashRecording(const QString &dir)
{
	QTemporaryDir cfg;
	Studio::Paths paths;
	paths.configRoot = cfg.path() + "/config";
	paths.logDir = cfg.path() + "/logs";
	paths.moduleConfig = cfg.path() + "/plugin_config";
	paths.secretsDir = cfg.path() + "/secrets";
	StudioProfile p;
	p.name = "Crash";
	p.video.baseWidth = 640;
	p.video.baseHeight = 360;
	p.video.outputWidth = 640;
	p.video.outputHeight = 360;
	p.recording.folder = dir;
	p.recording.encoder.encoderId = "obs_x264";
	p.recording.minFreeMb = 100;
	ConfigStore(paths.configRoot).save(p);
	Studio studio(paths);
	EngineInit init;
	init.moduleConfigDir = paths.moduleConfig;
	init.x11Display = ObsPlatform::qtX11Display();
	QString err;
	if (!studio.start("Crash", init, &err))
		return 2;
	SourceConfig c;
	c.kind = SourceKind::Color;
	c.engineSettings = QJsonObject{{"color", double(0xFF2020E0u)}};
	const QString uid = studio.addSource(c, &err);
	studio.takeContent(ContentRef::source(uid), TakeMode::Cut);
	if (!studio.startRecording(&err))
		return 3;
	QTest::qWait(4500);
	fprintf(stderr, "aborting while recording %s\n", qPrintable(studio.engine().outputs().recordingPath()));
	std::abort();
}

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "xcb");
	QApplication app(argc, argv);
	const QStringList args = app.arguments();
	const int idx = args.indexOf("--crash-recording");
	if (idx >= 0 && idx + 1 < args.size())
		return crashRecording(args.at(idx + 1));
	TstEngine t;
	return QTest::qExec(&t, argc, argv);
}

#include "tst_engine.moc"
