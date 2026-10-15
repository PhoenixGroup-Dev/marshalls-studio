#include "StudioConfig.h"

#include <algorithm>
#include <QJsonArray>
#include <QUuid>

namespace ms {

namespace {

template<typename E> struct EnumName {
	E value;
	const char *name;
};

template<typename E, size_t N> QString toName(const EnumName<E> (&tbl)[N], E v)
{
	for (const auto &e : tbl)
		if (e.value == v)
			return QString::fromLatin1(e.name);
	return QString::fromLatin1(tbl[0].name);
}

template<typename E, size_t N> E fromName(const EnumName<E> (&tbl)[N], const QString &s, E fallback)
{
	for (const auto &e : tbl)
		if (s == QLatin1String(e.name))
			return e.value;
	return fallback;
}

const EnumName<SourceKind> kKinds[] = {
	{SourceKind::Camera, "camera"},       {SourceKind::Screen, "screen"},
	{SourceKind::Window, "window"},       {SourceKind::Media, "media"},
	{SourceKind::Image, "image"},         {SourceKind::Slideshow, "slideshow"},
	{SourceKind::Color, "color"},         {SourceKind::AudioInput, "audio_input"},
	{SourceKind::AudioOutput, "audio_output"},
};
const EnumName<MonitorMode> kMonitor[] = {
	{MonitorMode::Off, "off"}, {MonitorMode::MonitorOnly, "monitor_only"}, {MonitorMode::MonitorAndOutput, "monitor_and_output"}};
const EnumName<MediaEndAction> kEnd[] = {
	{MediaEndAction::Hold, "hold"}, {MediaEndAction::Black, "black"}, {MediaEndAction::ReturnPrevious, "return_previous"}};
const EnumName<GraphicStyle> kStyle[] = {
	{GraphicStyle::LowerThird, "lower_third"}, {GraphicStyle::Band, "band"}, {GraphicStyle::Title, "title"}};
const EnumName<GraphicPosition> kPos[] = {
	{GraphicPosition::BottomLeft, "bottom_left"}, {GraphicPosition::BottomCenter, "bottom_center"},
	{GraphicPosition::BottomRight, "bottom_right"}, {GraphicPosition::TopLeft, "top_left"},
	{GraphicPosition::TopRight, "top_right"},     {GraphicPosition::Center, "center"}};
const EnumName<GraphicAnimation> kAnim[] = {{GraphicAnimation::Slide, "slide"},
					    {GraphicAnimation::Wipe, "wipe"},
					    {GraphicAnimation::Fade, "fade"},
					    {GraphicAnimation::Cut, "cut"}};
const EnumName<RateControl> kRc[] = {{RateControl::CBR, "CBR"}, {RateControl::VBR, "VBR"}, {RateControl::CRF, "CRF"}};
const EnumName<TransitionType> kTr[] = {{TransitionType::Cut, "cut"},
					{TransitionType::Fade, "fade"},
					{TransitionType::FadeToBlack, "fade_to_black"},
					{TransitionType::Slide, "slide"},
					{TransitionType::Swipe, "swipe"}};
const EnumName<DoubleClickAction> kDbl[] = {
	{DoubleClickAction::None, "none"}, {DoubleClickAction::Cut, "cut"}, {DoubleClickAction::Auto, "auto"}};

QJsonObject rectToJson(const QRectF &r)
{
	return QJsonObject{{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
}
QRectF rectFromJson(const QJsonValue &v, const QRectF &fallback)
{
	if (!v.isObject())
		return fallback;
	const QJsonObject o = v.toObject();
	QRectF r(o.value("x").toDouble(), o.value("y").toDouble(), o.value("w").toDouble(),
		 o.value("h").toDouble());
	if (!(r.width() > 0.0) || !(r.height() > 0.0))
		return fallback;
	return r;
}
QString colorToString(const QColor &c)
{
	return c.name(QColor::HexArgb);
}
QColor colorFromJson(const QJsonValue &v, const QColor &fallback)
{
	QColor c(v.toString());
	return c.isValid() ? c : fallback;
}

template<typename T> QJsonArray vecToJson(const QVector<T> &v)
{
	QJsonArray a;
	for (const auto &e : v)
		a.append(e.toJson());
	return a;
}
template<typename T> QVector<T> vecFromJson(const QJsonValue &v)
{
	QVector<T> out;
	for (const auto &e : v.toArray())
		if (e.isObject())
			out.append(T::fromJson(e.toObject()));
	return out;
}

int clampInt(const QJsonObject &o, const char *key, int def, int lo, int hi)
{
	const QJsonValue v = o.value(QLatin1String(key));
	if (!v.isDouble())
		return def;
	return qBound(lo, v.toInt(def), hi);
}
double clampD(const QJsonObject &o, const char *key, double def, double lo, double hi)
{
	const QJsonValue v = o.value(QLatin1String(key));
	if (!v.isDouble())
		return def;
	return qBound(lo, v.toDouble(def), hi);
}
bool getBool(const QJsonObject &o, const char *key, bool def)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isBool() ? v.toBool() : def;
}
QString getStr(const QJsonObject &o, const char *key, const QString &def = {})
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isString() ? v.toString() : def;
}

} // namespace

QString kindToString(SourceKind k)
{
	return toName(kKinds, k);
}
SourceKind kindFromString(const QString &s, SourceKind fallback)
{
	return fromName(kKinds, s, fallback);
}
bool kindHasVideo(SourceKind k)
{
	return k != SourceKind::AudioInput && k != SourceKind::AudioOutput;
}
bool kindHasAudioByDefault(SourceKind k)
{
	return k == SourceKind::AudioInput || k == SourceKind::AudioOutput || k == SourceKind::Media;
}
QString kindLabel(SourceKind k)
{
	switch (k) {
	case SourceKind::Camera: return QStringLiteral("Câmera");
	case SourceKind::Screen: return QStringLiteral("Tela");
	case SourceKind::Window: return QStringLiteral("Janela");
	case SourceKind::Media: return QStringLiteral("Vídeo");
	case SourceKind::Image: return QStringLiteral("Imagem");
	case SourceKind::Slideshow: return QStringLiteral("Apresentação");
	case SourceKind::Color: return QStringLiteral("Cor");
	case SourceKind::AudioInput: return QStringLiteral("Microfone");
	case SourceKind::AudioOutput: return QStringLiteral("Áudio do PC");
	}
	return {};
}

QString monitorToString(MonitorMode m)
{
	return toName(kMonitor, m);
}
MonitorMode monitorFromString(const QString &s)
{
	return fromName(kMonitor, s, MonitorMode::Off);
}
QString endActionToString(MediaEndAction a)
{
	return toName(kEnd, a);
}
MediaEndAction endActionFromString(const QString &s)
{
	return fromName(kEnd, s, MediaEndAction::Hold);
}
QString styleToString(GraphicStyle s)
{
	return toName(kStyle, s);
}
GraphicStyle styleFromString(const QString &s)
{
	return fromName(kStyle, s, GraphicStyle::LowerThird);
}
QString positionToString(GraphicPosition p)
{
	return toName(kPos, p);
}
GraphicPosition positionFromString(const QString &s)
{
	return fromName(kPos, s, GraphicPosition::BottomLeft);
}
QString animationToString(GraphicAnimation a)
{
	return toName(kAnim, a);
}
GraphicAnimation animationFromString(const QString &s)
{
	return fromName(kAnim, s, GraphicAnimation::Slide);
}
QString rateControlToString(RateControl r)
{
	return toName(kRc, r);
}
RateControl rateControlFromString(const QString &s)
{
	return fromName(kRc, s, RateControl::CBR);
}
QString transitionToString(TransitionType t)
{
	return toName(kTr, t);
}
TransitionType transitionFromString(const QString &s)
{
	return fromName(kTr, s, TransitionType::Fade);
}

// ---------------------------------------------------------------------------

QJsonObject AudioFilters::toJson() const
{
	return QJsonObject{
		{"gate", QJsonObject{{"enabled", gateEnabled}, {"open_db", gateOpenDb}, {"close_db", gateCloseDb},
				     {"attack_ms", gateAttackMs}, {"hold_ms", gateHoldMs}, {"release_ms", gateReleaseMs}}},
		{"suppress", QJsonObject{{"enabled", suppressEnabled}, {"level_db", suppressLevelDb}}},
		{"compressor", QJsonObject{{"enabled", compEnabled}, {"ratio", compRatio}, {"threshold_db", compThresholdDb},
					   {"attack_ms", compAttackMs}, {"release_ms", compReleaseMs},
					   {"output_gain_db", compOutputGainDb}}},
		{"limiter", QJsonObject{{"enabled", limiterEnabled}, {"threshold_db", limiterThresholdDb},
					{"release_ms", limiterReleaseMs}}},
		{"eq", QJsonObject{{"enabled", eqEnabled}, {"low_db", eqLowDb}, {"mid_db", eqMidDb}, {"high_db", eqHighDb}}},
		{"gain_db", gainDb},
	};
}

AudioFilters AudioFilters::fromJson(const QJsonObject &o)
{
	AudioFilters f;
	const QJsonObject g = o.value("gate").toObject();
	f.gateEnabled = getBool(g, "enabled", false);
	f.gateOpenDb = clampD(g, "open_db", f.gateOpenDb, -96, 0);
	f.gateCloseDb = clampD(g, "close_db", f.gateCloseDb, -96, 0);
	f.gateAttackMs = clampInt(g, "attack_ms", f.gateAttackMs, 1, 10000);
	f.gateHoldMs = clampInt(g, "hold_ms", f.gateHoldMs, 1, 10000);
	f.gateReleaseMs = clampInt(g, "release_ms", f.gateReleaseMs, 1, 10000);
	const QJsonObject s = o.value("suppress").toObject();
	f.suppressEnabled = getBool(s, "enabled", false);
	f.suppressLevelDb = clampInt(s, "level_db", f.suppressLevelDb, -60, 0);
	const QJsonObject c = o.value("compressor").toObject();
	f.compEnabled = getBool(c, "enabled", false);
	f.compRatio = clampD(c, "ratio", f.compRatio, 1, 32);
	f.compThresholdDb = clampD(c, "threshold_db", f.compThresholdDb, -60, 0);
	f.compAttackMs = clampInt(c, "attack_ms", f.compAttackMs, 1, 500);
	f.compReleaseMs = clampInt(c, "release_ms", f.compReleaseMs, 1, 1000);
	f.compOutputGainDb = clampD(c, "output_gain_db", f.compOutputGainDb, -32, 32);
	const QJsonObject l = o.value("limiter").toObject();
	f.limiterEnabled = getBool(l, "enabled", false);
	f.limiterThresholdDb = clampD(l, "threshold_db", f.limiterThresholdDb, -60, 0);
	f.limiterReleaseMs = clampInt(l, "release_ms", f.limiterReleaseMs, 1, 1000);
	const QJsonObject e = o.value("eq").toObject();
	f.eqEnabled = getBool(e, "enabled", false);
	f.eqLowDb = clampD(e, "low_db", 0, -20, 20);
	f.eqMidDb = clampD(e, "mid_db", 0, -20, 20);
	f.eqHighDb = clampD(e, "high_db", 0, -20, 20);
	f.gainDb = clampD(o, "gain_db", 0, -30, 30);
	return f;
}

QJsonObject AudioSettings::toJson() const
{
	return QJsonObject{{"volume_db", volumeDb},        {"muted", muted},
			   {"sync_offset_ms", syncOffsetMs},  {"monitor", monitorToString(monitor)},
			   {"to_program", toProgram},         {"isolated_track", isolatedTrack},
			   {"filters", filters.toJson()}};
}

AudioSettings AudioSettings::fromJson(const QJsonObject &o)
{
	AudioSettings a;
	a.volumeDb = clampD(o, "volume_db", 0.0, -100.0, 26.0);
	a.muted = getBool(o, "muted", false);
	a.syncOffsetMs = clampInt(o, "sync_offset_ms", 0, -950, 20000);
	a.monitor = monitorFromString(getStr(o, "monitor"));
	a.toProgram = getBool(o, "to_program", true);
	a.isolatedTrack = getBool(o, "isolated_track", true);
	a.filters = AudioFilters::fromJson(o.value("filters").toObject());
	return a;
}

QJsonObject MediaOptions::toJson() const
{
	return QJsonObject{{"loop", loop}, {"restart_on_take", restartOnTake}, {"end_action", endActionToString(endAction)}};
}

MediaOptions MediaOptions::fromJson(const QJsonObject &o)
{
	MediaOptions m;
	m.loop = getBool(o, "loop", false);
	m.restartOnTake = getBool(o, "restart_on_take", true);
	m.endAction = endActionFromString(getStr(o, "end_action"));
	return m;
}

QJsonObject SourceConfig::toJson() const
{
	return QJsonObject{{"uid", uid},
			   {"name", name},
			   {"kind", kindToString(kind)},
			   {"device", QJsonObject{{"id", device.id}, {"name", device.name}}},
			   {"settings", engineSettings},
			   {"video_delay_ms", videoDelayMs},
			   {"has_audio", hasAudio},
			   {"audio", audio.toJson()},
			   {"media", media.toJson()},
			   {"order", order}};
}

SourceConfig SourceConfig::fromJson(const QJsonObject &o)
{
	SourceConfig s;
	s.uid = getStr(o, "uid");
	if (s.uid.isEmpty())
		s.uid = newUid("src");
	s.name = getStr(o, "name");
	s.kind = kindFromString(getStr(o, "kind"));
	const QJsonObject d = o.value("device").toObject();
	s.device.id = getStr(d, "id");
	s.device.name = getStr(d, "name");
	s.engineSettings = o.value("settings").toObject();
	s.videoDelayMs = clampInt(o, "video_delay_ms", 0, 0, 500);
	s.hasAudio = getBool(o, "has_audio", kindHasAudioByDefault(s.kind));
	s.audio = AudioSettings::fromJson(o.value("audio").toObject());
	s.media = MediaOptions::fromJson(o.value("media").toObject());
	s.order = clampInt(o, "order", 0, 0, 100000);
	return s;
}

QJsonObject SceneItemConfig::toJson() const
{
	return QJsonObject{{"source", sourceUid},
			   {"rect", rectToJson(rect)},
			   {"visible", visible},
			   {"fill", fill},
			   {"crop", QJsonObject{{"l", cropLeft}, {"t", cropTop}, {"r", cropRight}, {"b", cropBottom}}}};
}

SceneItemConfig SceneItemConfig::fromJson(const QJsonObject &o)
{
	SceneItemConfig i;
	i.sourceUid = getStr(o, "source");
	i.rect = rectFromJson(o.value("rect"), QRectF(0, 0, 1, 1));
	i.visible = getBool(o, "visible", true);
	i.fill = getBool(o, "fill", false);
	const QJsonObject c = o.value("crop").toObject();
	i.cropLeft = clampInt(c, "l", 0, 0, 16384);
	i.cropTop = clampInt(c, "t", 0, 0, 16384);
	i.cropRight = clampInt(c, "r", 0, 0, 16384);
	i.cropBottom = clampInt(c, "b", 0, 0, 16384);
	return i;
}

QJsonObject SceneConfig::toJson() const
{
	return QJsonObject{{"uid", uid}, {"name", name}, {"items", vecToJson(items)}, {"order", order}};
}

SceneConfig SceneConfig::fromJson(const QJsonObject &o)
{
	SceneConfig s;
	s.uid = getStr(o, "uid");
	if (s.uid.isEmpty())
		s.uid = newUid("scn");
	s.name = getStr(o, "name");
	s.items = vecFromJson<SceneItemConfig>(o.value("items"));
	s.order = clampInt(o, "order", 0, 0, 100000);
	return s;
}

QJsonObject LowerThirdConfig::toJson() const
{
	return QJsonObject{{"uid", uid},
			   {"template", templateName},
			   {"style", styleToString(style)},
			   {"name", name},
			   {"role", role},
			   {"company", company},
			   {"secondary", secondary},
			   {"logo", logoPath},
			   {"accent", colorToString(accent)},
			   {"plate", colorToString(plate)},
			   {"text", colorToString(text)},
			   {"sub_text", colorToString(subText)},
			   {"font", fontFamily},
			   {"position", positionToString(position)},
			   {"anim_in", animationToString(animIn)},
			   {"anim_out", animationToString(animOut)},
			   {"anim_ms", animMs},
			   {"hold_s", holdSeconds},
			   {"scale", scale},
			   {"order", order}};
}

LowerThirdConfig LowerThirdConfig::fromJson(const QJsonObject &o)
{
	LowerThirdConfig l;
	l.uid = getStr(o, "uid");
	if (l.uid.isEmpty())
		l.uid = newUid("lt");
	l.templateName = getStr(o, "template");
	l.style = styleFromString(getStr(o, "style"));
	l.name = getStr(o, "name");
	l.role = getStr(o, "role");
	l.company = getStr(o, "company");
	l.secondary = getStr(o, "secondary");
	l.logoPath = getStr(o, "logo");
	l.accent = colorFromJson(o.value("accent"), l.accent);
	l.plate = colorFromJson(o.value("plate"), l.plate);
	l.text = colorFromJson(o.value("text"), l.text);
	l.subText = colorFromJson(o.value("sub_text"), l.subText);
	l.fontFamily = getStr(o, "font");
	l.position = positionFromString(getStr(o, "position"));
	l.animIn = animationFromString(getStr(o, "anim_in"));
	l.animOut = animationFromString(getStr(o, "anim_out"));
	l.animMs = clampInt(o, "anim_ms", 450, 0, 5000);
	l.holdSeconds = clampD(o, "hold_s", 0.0, 0.0, 3600.0);
	l.scale = clampD(o, "scale", 1.0, 0.4, 2.5);
	l.order = clampInt(o, "order", 0, 0, 100000);
	return l;
}

QJsonObject OverlayImageConfig::toJson() const
{
	return QJsonObject{
		{"uid", uid}, {"name", name}, {"path", path}, {"rect", rectToJson(rect)}, {"fade_ms", fadeMs}, {"order", order}};
}

OverlayImageConfig OverlayImageConfig::fromJson(const QJsonObject &o)
{
	OverlayImageConfig g;
	g.uid = getStr(o, "uid");
	if (g.uid.isEmpty())
		g.uid = newUid("ovl");
	g.name = getStr(o, "name");
	g.path = getStr(o, "path");
	g.rect = rectFromJson(o.value("rect"), g.rect);
	g.fadeMs = clampInt(o, "fade_ms", 300, 0, 5000);
	g.order = clampInt(o, "order", 0, 0, 100000);
	return g;
}

QJsonObject VideoSettings::toJson() const
{
	return QJsonObject{{"base_w", baseWidth},     {"base_h", baseHeight}, {"out_w", outputWidth},
			   {"out_h", outputHeight},   {"fps_num", fpsNum},    {"fps_den", fpsDen}};
}

VideoSettings VideoSettings::fromJson(const QJsonObject &o)
{
	VideoSettings v;
	v.baseWidth = clampInt(o, "base_w", v.baseWidth, 320, 7680) & ~1;
	v.baseHeight = clampInt(o, "base_h", v.baseHeight, 180, 4320) & ~1;
	v.outputWidth = clampInt(o, "out_w", v.outputWidth, 320, 7680) & ~1;
	v.outputHeight = clampInt(o, "out_h", v.outputHeight, 180, 4320) & ~1;
	v.fpsNum = clampInt(o, "fps_num", 30, 1, 240000);
	v.fpsDen = clampInt(o, "fps_den", 1, 1, 1001);
	return v;
}

bool VideoSettings::operator==(const VideoSettings &o) const
{
	return baseWidth == o.baseWidth && baseHeight == o.baseHeight && outputWidth == o.outputWidth &&
	       outputHeight == o.outputHeight && fpsNum == o.fpsNum && fpsDen == o.fpsDen;
}

QJsonObject EncoderSettings::toJson() const
{
	return QJsonObject{{"encoder", encoderId},      {"rate_control", rateControlToString(rateControl)},
			   {"video_kbps", videoBitrateKbps}, {"crf", crf},
			   {"preset", preset},               {"keyframe_s", keyframeSec},
			   {"audio_kbps", audioBitrateKbps}};
}

EncoderSettings EncoderSettings::fromJson(const QJsonObject &o)
{
	EncoderSettings e;
	e.encoderId = getStr(o, "encoder");
	e.rateControl = rateControlFromString(getStr(o, "rate_control"));
	e.videoBitrateKbps = clampInt(o, "video_kbps", e.videoBitrateKbps, 200, 100000);
	e.crf = clampInt(o, "crf", e.crf, 0, 51);
	e.preset = getStr(o, "preset", e.preset);
	e.keyframeSec = clampInt(o, "keyframe_s", 2, 0, 20);
	e.audioBitrateKbps = clampInt(o, "audio_kbps", 160, 32, 512);
	return e;
}

QJsonObject RecordingSettings::toJson() const
{
	return QJsonObject{{"folder", folder},
			   {"container", container},
			   {"filename_pattern", filenamePattern},
			   {"remux_to_mp4", remuxToMp4},
			   {"share_stream_encoder", shareStreamEncoder},
			   {"isolated_tracks", isolatedTracks},
			   {"min_free_mb", minFreeMb},
			   {"encoder", encoder.toJson()}};
}

RecordingSettings RecordingSettings::fromJson(const QJsonObject &o)
{
	RecordingSettings r;
	r.folder = getStr(o, "folder");
	r.container = getStr(o, "container", "mkv");
	if (!QStringList{"mkv", "mp4", "mov", "flv"}.contains(r.container))
		r.container = "mkv";
	r.filenamePattern = getStr(o, "filename_pattern", r.filenamePattern);
	if (!r.filenamePattern.contains("%1"))
		r.filenamePattern += " %1";
	r.remuxToMp4 = getBool(o, "remux_to_mp4", false);
	r.shareStreamEncoder = getBool(o, "share_stream_encoder", false);
	r.isolatedTracks = getBool(o, "isolated_tracks", true);
	r.minFreeMb = clampInt(o, "min_free_mb", 2048, 100, 1000000);
	r.encoder = o.contains("encoder") ? EncoderSettings::fromJson(o.value("encoder").toObject()) : EncoderSettings{};
	if (!o.contains("encoder")) {
		r.encoder.rateControl = RateControl::CRF;
		r.encoder.crf = 20;
	}
	return r;
}

QJsonObject StreamSettings::toJson() const
{
	return QJsonObject{{"platform", platform},
			   {"server", server},
			   {"key_ref", keyRef},
			   {"encoder", encoder.toJson()},
			   {"delay_s", delaySec},
			   {"delay_preserve", delayPreserve},
			   {"reconnect_retries", reconnectRetries},
			   {"reconnect_delay_s", reconnectDelaySec}};
}

StreamSettings StreamSettings::fromJson(const QJsonObject &o)
{
	StreamSettings s;
	s.platform = getStr(o, "platform", "custom");
	s.server = getStr(o, "server");
	s.keyRef = getStr(o, "key_ref", "stream-key");
	s.encoder = EncoderSettings::fromJson(o.value("encoder").toObject());
	s.delaySec = clampInt(o, "delay_s", 0, 0, 120);
	s.delayPreserve = getBool(o, "delay_preserve", true);
	s.reconnectRetries = clampInt(o, "reconnect_retries", 20, 0, 1000);
	s.reconnectDelaySec = clampInt(o, "reconnect_delay_s", 3, 1, 60);
	return s;
}

QJsonObject TransitionSettings::toJson() const
{
	return QJsonObject{{"type", transitionToString(type)},
			   {"duration_ms", durationMs},
			   {"ftb_duration_ms", ftbDurationMs},
			   {"double_click", toName(kDbl, doubleClick)}};
}

TransitionSettings TransitionSettings::fromJson(const QJsonObject &o)
{
	TransitionSettings t;
	t.type = transitionFromString(getStr(o, "type"));
	if (t.type == TransitionType::FadeToBlack)
		t.type = TransitionType::Fade;
	t.durationMs = clampInt(o, "duration_ms", 500, 50, 10000);
	t.ftbDurationMs = clampInt(o, "ftb_duration_ms", 800, 50, 10000);
	t.doubleClick = fromName(kDbl, getStr(o, "double_click"), DoubleClickAction::Auto);
	return t;
}

QJsonObject UiSettings::toJson() const
{
	QJsonObject layout;
	for (auto it = layoutState.cbegin(); it != layoutState.cend(); ++it)
		layout.insert(it.key(), QString::fromLatin1(it.value().toBase64()));
	return QJsonObject{{"grid_columns", gridColumns},
			   {"thumb_width", thumbnailWidth},
			   {"thumb_fps", thumbnailFps},
			   {"grid_filter", gridFilter},
			   {"hide_from_capture", hideFromCapture},
			   {"monitoring_device_id", monitoringDeviceId},
			   {"monitoring_device_name", monitoringDeviceName},
			   {"layout", layout}};
}

UiSettings UiSettings::fromJson(const QJsonObject &o)
{
	UiSettings u;
	u.gridColumns = clampInt(o, "grid_columns", 4, 1, 12);
	u.thumbnailWidth = clampInt(o, "thumb_width", 320, 128, 960);
	u.thumbnailFps = clampInt(o, "thumb_fps", 10, 1, 30);
	u.gridFilter = getStr(o, "grid_filter", "all");
	u.hideFromCapture = getBool(o, "hide_from_capture", true);
	u.monitoringDeviceId = getStr(o, "monitoring_device_id", "default");
	u.monitoringDeviceName = getStr(o, "monitoring_device_name");
	const QJsonObject layout = o.value("layout").toObject();
	for (auto it = layout.begin(); it != layout.end(); ++it)
		u.layoutState.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));
	return u;
}

QJsonObject StudioProfile::toJson() const
{
	QJsonObject sc;
	for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it)
		sc.insert(it.key(), it.value());
	return QJsonObject{{"schema_version", schemaVersion},
			   {"name", name},
			   {"video", video.toJson()},
			   {"sources", vecToJson(sources)},
			   {"scenes", vecToJson(scenes)},
			   {"lower_thirds", vecToJson(lowerThirds)},
			   {"overlays", vecToJson(overlays)},
			   {"recording", recording.toJson()},
			   {"stream", stream.toJson()},
			   {"transition", transition.toJson()},
			   {"ui", ui.toJson()},
			   {"shortcuts", sc}};
}

StudioProfile StudioProfile::fromJson(const QJsonObject &o)
{
	StudioProfile p;
	p.schemaVersion = clampInt(o, "schema_version", kSchemaVersion, 0, 1000);
	p.name = getStr(o, "name", p.name);
	p.video = VideoSettings::fromJson(o.value("video").toObject());
	p.sources = vecFromJson<SourceConfig>(o.value("sources"));
	p.scenes = vecFromJson<SceneConfig>(o.value("scenes"));
	p.lowerThirds = vecFromJson<LowerThirdConfig>(o.value("lower_thirds"));
	p.overlays = vecFromJson<OverlayImageConfig>(o.value("overlays"));
	p.recording = RecordingSettings::fromJson(o.value("recording").toObject());
	p.stream = StreamSettings::fromJson(o.value("stream").toObject());
	p.transition = TransitionSettings::fromJson(o.value("transition").toObject());
	p.ui = UiSettings::fromJson(o.value("ui").toObject());
	const QJsonObject sc = o.value("shortcuts").toObject();
	for (auto it = sc.begin(); it != sc.end(); ++it)
		p.shortcuts.insert(it.key(), it.value().toString());

	// Drop dangling scene items (source removed while app was not running).
	for (auto &scene : p.scenes) {
		QVector<SceneItemConfig> kept;
		for (const auto &item : scene.items)
			if (p.findSource(item.sourceUid))
				kept.append(item);
		scene.items = kept;
	}
	return p;
}

SourceConfig *StudioProfile::findSource(const QString &uid)
{
	for (auto &s : sources)
		if (s.uid == uid)
			return &s;
	return nullptr;
}
const SourceConfig *StudioProfile::findSource(const QString &uid) const
{
	for (const auto &s : sources)
		if (s.uid == uid)
			return &s;
	return nullptr;
}
SceneConfig *StudioProfile::findScene(const QString &uid)
{
	for (auto &s : scenes)
		if (s.uid == uid)
			return &s;
	return nullptr;
}
const SceneConfig *StudioProfile::findScene(const QString &uid) const
{
	for (const auto &s : scenes)
		if (s.uid == uid)
			return &s;
	return nullptr;
}
LowerThirdConfig *StudioProfile::findLowerThird(const QString &uid)
{
	for (auto &l : lowerThirds)
		if (l.uid == uid)
			return &l;
	return nullptr;
}
OverlayImageConfig *StudioProfile::findOverlay(const QString &uid)
{
	for (auto &l : overlays)
		if (l.uid == uid)
			return &l;
	return nullptr;
}

bool StudioProfile::removeSource(const QString &uid)
{
	const auto before = sources.size();
	sources.erase(std::remove_if(sources.begin(), sources.end(), [&](const SourceConfig &s) { return s.uid == uid; }),
		      sources.end());
	for (auto &scene : scenes)
		scene.items.erase(std::remove_if(scene.items.begin(), scene.items.end(),
						 [&](const SceneItemConfig &i) { return i.sourceUid == uid; }),
				  scene.items.end());
	return sources.size() != before;
}

QString StudioProfile::sourceUsingDevice(SourceKind kind, const DeviceRef &dev, const QString &exceptUid) const
{
	if (dev.id.isEmpty())
		return {};
	// Default/system aliases are not physical devices; two "default" captures
	// of the same kind would still duplicate, so they are checked too.
	for (const auto &s : sources)
		if (s.uid != exceptUid && s.kind == kind && s.device.id == dev.id)
			return s.uid;
	return {};
}

QString StudioProfile::uniqueName(const QString &base) const
{
	auto taken = [&](const QString &n) {
		for (const auto &s : sources)
			if (s.name.compare(n, Qt::CaseInsensitive) == 0)
				return true;
		for (const auto &s : scenes)
			if (s.name.compare(n, Qt::CaseInsensitive) == 0)
				return true;
		return false;
	};
	if (!taken(base))
		return base;
	for (int i = 2;; ++i) {
		const QString n = QStringLiteral("%1 %2").arg(base).arg(i);
		if (!taken(n))
			return n;
	}
}

QString newUid(const QString &prefix)
{
	return prefix + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

} // namespace ms
