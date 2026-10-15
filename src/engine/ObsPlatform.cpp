#include "ObsPlatform.h"

#include <obs.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QDir>
#include <QFile>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <QGuiApplication>

namespace ms {

KindBinding ObsPlatform::binding(SourceKind kind)
{
	switch (kind) {
#if defined(_WIN32)
	case SourceKind::Camera: return {"dshow_input", "video_device_id", false, true};
	case SourceKind::Screen: return {"monitor_capture", "monitor_id", false, true};
	case SourceKind::Window: return {"window_capture", "window", false, true};
	case SourceKind::AudioInput: return {"wasapi_input_capture", "device_id"};
	case SourceKind::AudioOutput: return {"wasapi_output_capture", "device_id"};
#elif defined(__APPLE__)
	case SourceKind::Camera: return {"av_capture_input_v2", "device"};
	case SourceKind::Screen: return {"screen_capture", "display_uuid", false, true};
	case SourceKind::Window: return {"screen_capture", "window", false, true};
	case SourceKind::AudioInput: return {"coreaudio_input_capture", "device_id"};
	case SourceKind::AudioOutput: return {"coreaudio_output_capture", "device_id"};
#else
	case SourceKind::Camera: return {"v4l2_input", "device_id"};
	case SourceKind::Screen: return {"xshm_input", "screen", true, true};
	case SourceKind::Window: return {"xcomposite_input", "capture_window", false, true};
	case SourceKind::AudioInput: return {"pulse_input_capture", "device_id"};
	case SourceKind::AudioOutput: return {"pulse_output_capture", "device_id"};
#endif
	case SourceKind::Media: return {"ffmpeg_source", nullptr};
	case SourceKind::Image: return {"image_source", nullptr};
	case SourceKind::Slideshow: return {"slideshow", nullptr};
	case SourceKind::Color: return {"color_source_v3", nullptr};
	}
	return {};
}

static bool inputTypeRegistered(const char *id)
{
	const char *t = nullptr;
	for (size_t i = 0; obs_enum_input_types(i, &t); ++i)
		if (t && strcmp(t, id) == 0)
			return true;
	return false;
}

bool ObsPlatform::isKindSupported(SourceKind kind)
{
	const KindBinding b = binding(kind);
	return b.sourceId && inputTypeRegistered(b.sourceId);
}

QString ObsPlatform::unsupportedReason(SourceKind kind)
{
	if (isKindSupported(kind))
		return {};
#if !defined(_WIN32) && !defined(__APPLE__)
	if (kind == SourceKind::Window)
		return QStringLiteral("Captura de janela indisponível neste sistema: o plugin xcomposite exige "
				      "suporte EGL a pixmaps X11 (no Windows usa-se a captura de janela nativa).");
#endif
	return QStringLiteral("O tipo de fonte “%1” não está disponível nesta instalação (%2).")
		.arg(kindLabel(kind), QString::fromLatin1(binding(kind).sourceId));
}

QStringList ObsPlatform::moduleAllowList()
{
	QStringList l = {"image-source", "obs-ffmpeg", "obs-filters", "obs-outputs",
			 "obs-transitions", "obs-x264"};
#if defined(_WIN32)
	l << "win-dshow" << "win-wasapi" << "win-capture" << "obs-qsv11" << "obs-nvenc" << "obs-text"
	  << "win-mf"; // win-mf: Media Foundation encoders on older builds (ignored if absent)
	l << "enc-amf";
#elif defined(__APPLE__)
	l << "mac-avcapture" << "mac-capture" << "mac-videotoolbox" << "coreaudio-encoder";
#else
	l << "linux-capture" << "linux-pulseaudio" << "linux-v4l2" << "linux-pipewire" << "text-freetype2";
#endif
	return l;
}

QString ObsPlatform::graphicsModule()
{
#if defined(_WIN32)
	return QStringLiteral("libobs-d3d11");
#else
	return QStringLiteral("libobs-opengl");
#endif
}

void *ObsPlatform::qtX11Display()
{
#if !defined(_WIN32) && !defined(__APPLE__) && QT_CONFIG(xcb)
	if (auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr)
		return x11->display();
#endif
	return nullptr;
}

bool ObsPlatform::qtIsWayland()
{
	return QGuiApplication::platformName().contains(QLatin1String("wayland"));
}

QString ObsPlatform::selfWindowToken()
{
	return QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}

// Per thread: X11/Xlib based plugins must be used from the thread that created them.
static thread_local QHash<int, obs_source_t *> g_enumInstances;

static obs_source_t *enumerationInstance(SourceKind kind)
{
	if (obs_source_t *s = g_enumInstances.value(int(kind)))
		return s;
	obs_data_t *d = obs_data_create();
	if (!ObsPlatform::binding(kind).deviceIsInt)
		obs_data_set_string(d, ObsPlatform::binding(kind).deviceProp, "");
	obs_data_set_bool(d, "active", false); // dshow: do not open any device
	obs_source_t *s = obs_source_create_private(ObsPlatform::binding(kind).sourceId, "ms-device-enum", d);
	obs_data_release(d);
	if (s)
		g_enumInstances.insert(int(kind), s);
	return s;
}

bool ObsPlatform::enumerateOnWorker(SourceKind kind)
{
	return kind == SourceKind::Camera || kind == SourceKind::AudioInput || kind == SourceKind::AudioOutput;
}

void ObsPlatform::releaseEnumerationInstances()
{
	for (obs_source_t *s : g_enumInstances)
		obs_source_release(s);
	g_enumInstances.clear();
}

#if !defined(_WIN32) && !defined(__APPLE__)
// V4L2 capture devices, identified by their /dev/v4l/by-id link when present
// (stable across reboots and USB port changes), else by /dev/videoN.
static QVector<DeviceInfo> enumerateV4l2()
{
	QVector<DeviceInfo> out;
	QHash<QString, QString> byIdFor; // canonical /dev/videoN -> by-id link
	const auto links = QDir("/dev/v4l/by-id").entryInfoList(QDir::System | QDir::Files | QDir::NoDotAndDotDot);
	for (const QFileInfo &l : links)
		byIdFor.insert(l.canonicalFilePath(), l.absoluteFilePath());
	const auto nodes = QDir("/dev").entryInfoList({"video*"}, QDir::System, QDir::Name);
	for (const QFileInfo &n : nodes) {
		const int fd = ::open(QFile::encodeName(n.absoluteFilePath()).constData(), O_RDWR | O_NONBLOCK);
		if (fd < 0)
			continue;
		v4l2_capability cap = {};
		const bool ok = ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
		::close(fd);
		if (!ok)
			continue;
		const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
		if (!(caps & V4L2_CAP_VIDEO_CAPTURE))
			continue; // metadata nodes of the same camera
		DeviceInfo d;
		d.name = QString::fromUtf8(reinterpret_cast<const char *>(cap.card));
		d.id = byIdFor.value(n.canonicalFilePath(), n.absoluteFilePath());
		out.append(d);
	}
	return out;
}
#endif

QVector<DeviceInfo> ObsPlatform::enumerateDevices(SourceKind kind)
{
	QVector<DeviceInfo> out;
#if !defined(_WIN32) && !defined(__APPLE__)
	if (kind == SourceKind::Camera)
		return inputTypeRegistered("v4l2_input") ? enumerateV4l2() : out;
#endif
	const KindBinding b = binding(kind);
	if (!b.sourceId || !b.deviceProp || !inputTypeRegistered(b.sourceId))
		return out;

	obs_properties_t *props = nullptr;
	if (b.enumNeedsInstance) {
		obs_source_t *inst = enumerationInstance(kind);
		props = inst ? obs_source_properties(inst) : nullptr;
	} else {
		props = obs_get_source_properties(b.sourceId);
	}
	if (!props)
		return out;
	obs_property_t *p = obs_properties_get(props, b.deviceProp);
	if (p && obs_property_get_type(p) == OBS_PROPERTY_LIST) {
		const obs_combo_format fmt = obs_property_list_format(p);
		const size_t n = obs_property_list_item_count(p);
		const QString self = selfWindowToken();
		for (size_t i = 0; i < n; ++i) {
			if (obs_property_list_item_disabled(p, i))
				continue;
			DeviceInfo d;
			d.name = QString::fromUtf8(obs_property_list_item_name(p, i));
			if (fmt == OBS_COMBO_FORMAT_INT)
				d.id = QString::number(obs_property_list_item_int(p, i));
			else if (fmt == OBS_COMBO_FORMAT_STRING)
				d.id = QString::fromUtf8(obs_property_list_item_string(p, i));
			else
				continue;
			if (d.id.isEmpty() && kind != SourceKind::Window)
				continue;
			// Never offer our own window for window capture (visual recursion).
			if (kind == SourceKind::Window && !self.isEmpty() && d.id.contains(self, Qt::CaseInsensitive))
				continue;
			if (kind == SourceKind::Window && d.id.isEmpty())
				continue;
			out.append(d);
		}
	}
	obs_properties_destroy(props);
	return out;
}

} // namespace ms
