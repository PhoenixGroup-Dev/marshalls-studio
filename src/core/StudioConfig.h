// Marshalls Studio — studio data model (pure, no media engine dependency).
//
// Everything the operator configures lives in a StudioProfile and is
// persisted as versioned JSON by ConfigStore. The media engine reads these
// structures to build the libobs graph; the UI edits them.
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ms {

// Bump when the on-disk format changes; add a step to ConfigStore::migrate().
constexpr int kSchemaVersion = 2;

enum class SourceKind {
	Camera,       // USB/webcam/capture device (dshow / v4l2)
	Screen,       // full monitor capture
	Window,       // single window capture
	Media,        // video file / vinheta (ffmpeg_source)
	Image,        // still image
	Slideshow,    // folder/list of images (exported slides)
	Color,        // solid color card
	AudioInput,   // microphone / line in
	AudioOutput,  // computer playback ("desktop audio")
};

QString kindToString(SourceKind k);
SourceKind kindFromString(const QString &s, SourceKind fallback = SourceKind::Camera);
bool kindHasVideo(SourceKind k);
bool kindHasAudioByDefault(SourceKind k);
QString kindLabel(SourceKind k); // localized, for UI

enum class MonitorMode { Off, MonitorOnly, MonitorAndOutput };
QString monitorToString(MonitorMode m);
MonitorMode monitorFromString(const QString &s);

enum class MediaEndAction {
	Hold,          // stay on last frame
	Black,         // show nothing (transparent → black program)
	ReturnPrevious // automatically take back the previous program content
};
QString endActionToString(MediaEndAction a);
MediaEndAction endActionFromString(const QString &s);

struct DeviceRef {
	QString id;   // stable platform identifier (dshow path, WASAPI endpoint, pulse name…)
	QString name; // human readable at time of selection (used as fallback match)
	bool isEmpty() const { return id.isEmpty() && name.isEmpty(); }
	bool operator==(const DeviceRef &o) const { return id == o.id && name == o.name; }
};

struct AudioFilters {
	bool gateEnabled = false;
	double gateOpenDb = -26.0, gateCloseDb = -32.0;
	int gateAttackMs = 25, gateHoldMs = 200, gateReleaseMs = 150;

	bool suppressEnabled = false;
	int suppressLevelDb = -30;

	bool compEnabled = false;
	double compRatio = 4.0, compThresholdDb = -18.0, compOutputGainDb = 0.0;
	int compAttackMs = 6, compReleaseMs = 60;

	bool limiterEnabled = false;
	double limiterThresholdDb = -3.0;
	int limiterReleaseMs = 60;

	bool eqEnabled = false;
	double eqLowDb = 0.0, eqMidDb = 0.0, eqHighDb = 0.0;

	double gainDb = 0.0; // input trim, applied first

	QJsonObject toJson() const;
	static AudioFilters fromJson(const QJsonObject &o);
};

struct AudioSettings {
	double volumeDb = 0.0;       // fader level sent to program
	bool muted = false;
	int syncOffsetMs = 0;        // audio delay, positive = later
	MonitorMode monitor = MonitorMode::Off;
	bool toProgram = true;       // routed to the program mix (track 1)
	bool isolatedTrack = true;   // also routed to its own recording track
	AudioFilters filters;

	QJsonObject toJson() const;
	static AudioSettings fromJson(const QJsonObject &o);
};

struct MediaOptions {
	bool loop = false;
	bool restartOnTake = true;
	MediaEndAction endAction = MediaEndAction::Hold;
	QJsonObject toJson() const;
	static MediaOptions fromJson(const QJsonObject &o);
};

struct SourceConfig {
	QString uid;
	QString name;
	SourceKind kind = SourceKind::Camera;
	DeviceRef device;           // for Camera / Audio* / Screen / Window
	QJsonObject engineSettings; // raw settings for the engine source (resolution, path…)
	int videoDelayMs = 0;       // video sync compensation (render delay)
	bool hasAudio = false;
	AudioSettings audio;
	MediaOptions media;
	int order = 0;              // position in the source grid

	QJsonObject toJson() const;
	static SourceConfig fromJson(const QJsonObject &o);
};

// A scene item placed in normalized canvas coordinates (0..1).
struct SceneItemConfig {
	QString sourceUid;
	QRectF rect{0, 0, 1, 1};
	bool visible = true;
	bool fill = false; // true: scale to cover the rect and crop overflow; false: fit inside
	// crop in source pixels
	int cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;
	QJsonObject toJson() const;
	static SceneItemConfig fromJson(const QJsonObject &o);
};

struct SceneConfig {
	QString uid;
	QString name;
	QVector<SceneItemConfig> items; // bottom → top
	int order = 0;
	QJsonObject toJson() const;
	static SceneConfig fromJson(const QJsonObject &o);
};

enum class GraphicStyle { LowerThird, Band, Title };
enum class GraphicPosition { BottomLeft, BottomCenter, BottomRight, TopLeft, TopRight, Center };
enum class GraphicAnimation { Slide, Wipe, Fade, Cut };

QString styleToString(GraphicStyle s);
GraphicStyle styleFromString(const QString &s);
QString positionToString(GraphicPosition p);
GraphicPosition positionFromString(const QString &s);
QString animationToString(GraphicAnimation a);
GraphicAnimation animationFromString(const QString &s);

struct LowerThirdConfig {
	QString uid;
	QString templateName;     // label in the graphics library
	GraphicStyle style = GraphicStyle::LowerThird;
	QString name;             // person name / title
	QString role;             // cargo
	QString company;          // empresa
	QString secondary;        // texto secundário
	QString logoPath;
	QColor accent{0xE5, 0x32, 0x2D};
	QColor plate{0x12, 0x14, 0x17, 235};
	QColor text{0xF4, 0xF5, 0xF7};
	QColor subText{0xB8, 0xBE, 0xC6};
	QString fontFamily;       // empty = default sans
	GraphicPosition position = GraphicPosition::BottomLeft;
	GraphicAnimation animIn = GraphicAnimation::Slide;
	GraphicAnimation animOut = GraphicAnimation::Slide;
	int animMs = 450;
	double holdSeconds = 0.0; // 0 = stay until removed manually
	double scale = 1.0;       // relative to canvas height
	int order = 0;

	QJsonObject toJson() const;
	static LowerThirdConfig fromJson(const QJsonObject &o);
};

// Logo / bug / still overlay that sits on top of every program content.
struct OverlayImageConfig {
	QString uid;
	QString name;
	QString path;
	QRectF rect{0.84, 0.04, 0.12, 0.12};
	int fadeMs = 300;
	int order = 0;
	QJsonObject toJson() const;
	static OverlayImageConfig fromJson(const QJsonObject &o);
};

struct VideoSettings {
	int baseWidth = 1920, baseHeight = 1080;
	int outputWidth = 1280, outputHeight = 720;
	int fpsNum = 30, fpsDen = 1;
	QJsonObject toJson() const;
	static VideoSettings fromJson(const QJsonObject &o);
	bool operator==(const VideoSettings &o) const;
	bool operator!=(const VideoSettings &o) const { return !(*this == o); }
};

enum class RateControl { CBR, VBR, CRF };
QString rateControlToString(RateControl r);
RateControl rateControlFromString(const QString &s);

struct EncoderSettings {
	QString encoderId;        // empty = auto (best available)
	RateControl rateControl = RateControl::CBR;
	int videoBitrateKbps = 4500;
	int crf = 22;
	QString preset = "veryfast"; // x264 preset; hardware encoders map their own
	int keyframeSec = 2;
	int audioBitrateKbps = 160;
	QJsonObject toJson() const;
	static EncoderSettings fromJson(const QJsonObject &o);
};

struct RecordingSettings {
	QString folder;           // empty = <Videos>/Marshalls Studio
	QString container = "mkv"; // mkv | mp4 | mov | flv
	QString filenamePattern = "Marshalls %1"; // %1 = yyyy-MM-dd HH-mm-ss
	bool remuxToMp4 = false;  // after stopping a MKV recording
	bool shareStreamEncoder = false;
	bool isolatedTracks = true; // program mix + one track per microphone
	int minFreeMb = 2048;     // stop recording below this
	EncoderSettings encoder;
	QJsonObject toJson() const;
	static RecordingSettings fromJson(const QJsonObject &o);
};

struct StreamSettings {
	QString platform = "custom"; // youtube | twitch | facebook | custom
	QString server;
	QString keyRef = "stream-key"; // name in SecretStore; never the key itself
	EncoderSettings encoder;
	int delaySec = 0;          // intentional output delay (stream only)
	bool delayPreserve = true; // keep delay on reconnect
	int reconnectRetries = 20;
	int reconnectDelaySec = 3;
	QJsonObject toJson() const;
	static StreamSettings fromJson(const QJsonObject &o);
};

enum class TransitionType { Cut, Fade, FadeToBlack /*program bus only*/, Slide, Swipe };
QString transitionToString(TransitionType t);
TransitionType transitionFromString(const QString &s);

enum class DoubleClickAction { None, Cut, Auto };

struct TransitionSettings {
	TransitionType type = TransitionType::Fade;
	int durationMs = 500;
	int ftbDurationMs = 800;
	DoubleClickAction doubleClick = DoubleClickAction::Auto;
	QJsonObject toJson() const;
	static TransitionSettings fromJson(const QJsonObject &o);
};

struct UiSettings {
	int gridColumns = 4;
	int thumbnailWidth = 320;   // render size of live thumbnails
	int thumbnailFps = 10;
	QString gridFilter = "all";
	bool hideFromCapture = true; // exclude own window from screen capture (Windows 10 2004+)
	QString monitoringDeviceId = "default";
	QString monitoringDeviceName;
	QMap<QString, QByteArray> layoutState; // splitter states etc.
	QJsonObject toJson() const;
	static UiSettings fromJson(const QJsonObject &o);
};

struct StudioProfile {
	int schemaVersion = kSchemaVersion;
	QString name = "Padrão";
	VideoSettings video;
	QVector<SourceConfig> sources;
	QVector<SceneConfig> scenes;
	QVector<LowerThirdConfig> lowerThirds;
	QVector<OverlayImageConfig> overlays;
	RecordingSettings recording;
	StreamSettings stream;
	TransitionSettings transition;
	UiSettings ui;
	QMap<QString, QString> shortcuts; // action id → portable key sequence

	QJsonObject toJson() const;
	static StudioProfile fromJson(const QJsonObject &o);

	SourceConfig *findSource(const QString &uid);
	const SourceConfig *findSource(const QString &uid) const;
	SceneConfig *findScene(const QString &uid);
	const SceneConfig *findScene(const QString &uid) const;
	LowerThirdConfig *findLowerThird(const QString &uid);
	OverlayImageConfig *findOverlay(const QString &uid);

	// Removes a source and every scene item that references it.
	bool removeSource(const QString &uid);
	// Returns the uid of a source already bound to this device, or empty.
	QString sourceUsingDevice(SourceKind kind, const DeviceRef &dev, const QString &exceptUid = {}) const;
	QString uniqueName(const QString &base) const;
};

QString newUid(const QString &prefix);

} // namespace ms
