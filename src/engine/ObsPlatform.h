// Platform specific libobs identifiers and device discovery.
//
// The rest of the engine speaks in SourceKind; this file maps each kind to
// the libobs source type and the property that selects the device on the
// current operating system.
#pragma once

#include "StudioConfig.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace ms {

struct DeviceInfo {
	QString id;
	QString name;
};

struct KindBinding {
	const char *sourceId = nullptr;   // libobs input id
	const char *deviceProp = nullptr; // property holding the device (nullptr: none)
	bool deviceIsInt = false;         // xshm "screen" is an integer index
	// Some plugins (linux-v4l2, win-dshow) dereference the instance inside
	// get_properties, so devices must be listed through an idle instance.
	bool enumNeedsInstance = false;
};

class ObsPlatform {
public:
	static KindBinding binding(SourceKind kind);
	static bool isKindSupported(SourceKind kind); // source type registered in this build
	static QString unsupportedReason(SourceKind kind);

	// Plugins required by the studio (others, e.g. OBS frontend plugins, are never loaded).
	static QStringList moduleAllowList();
	static QString graphicsModule();

	// Enumerates devices for a kind by querying the libobs source properties.
	// Must be called after modules are loaded. Safe from a worker thread.
	static QVector<DeviceInfo> enumerateDevices(SourceKind kind);
	// Kinds whose enumeration is safe and potentially slow (worker thread);
	// the others (screen/window capture, Xlib based on Linux) run on the UI thread.
	static bool enumerateOnWorker(SourceKind kind);
	// Releases the idle instances used for enumeration (call on the same thread).
	static void releaseEnumerationInstances();

	// Default device entry to pick when nothing is configured.
	static QString defaultAudioDeviceId() { return QStringLiteral("default"); }

	// Native display handle required by libobs on Linux/X11 (nullptr elsewhere).
	static void *qtX11Display();
	static bool qtIsWayland();

	// Executable name used to filter the application itself out of window lists.
	static QString selfWindowToken();
};

} // namespace ms
