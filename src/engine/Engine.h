// libobs facade.
//
// Output channel layout (rendered bottom → top on the program canvas):
//   0      program bus: a transition whose active source is a "solo" scene
//          (one grid source fitted to the canvas) or a composition scene
//   1      graphics layer: lower thirds, logos, overlays (unaffected by cuts)
//   2      fade-to-black layer: a fade transition between an empty scene and
//          a black card, covering everything below it
//   10..63 standalone audio inputs (microphones, computer audio)
//
// All public methods must be called from the thread that owns the Engine
// (the UI thread). libobs callbacks are marshalled back with queued calls.
#pragma once

#include "LowerThird.h"
#include "ObsPlatform.h"
#include "ProgramController.h"
#include "StudioConfig.h"
#include "ThumbnailService.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <array>
#include <atomic>
#include <memory>

struct obs_source;
typedef struct obs_source obs_source_t;
struct obs_scene;
typedef struct obs_scene obs_scene_t;
struct obs_volmeter;
typedef struct obs_volmeter obs_volmeter_t;
struct signal_handler;
typedef struct signal_handler signal_handler_t;
struct calldata;
typedef struct calldata calldata_t;

class QTimer;
struct audio_data;

namespace ms {

class OutputManager;

struct EngineInit {
	QString locale = QStringLiteral("pt-BR");
	QString moduleConfigDir;   // libobs plugin configuration directory
	void *x11Display = nullptr; // Display* on Linux/X11 (from the Qt platform)
	bool wayland = false;
	VideoSettings video;
};

enum class SourceHealth {
	Ok,
	Starting,      // created, no frame/packet yet
	NoSignal,      // device present but no frames / silent input
	Disconnected,  // configured device not present on the system
	Error,         // engine could not create/open the source
	Unsupported,   // source type not available on this platform/build
};
QString healthLabel(SourceHealth h);

struct AudioLevel {
	float magnitudeDb = -100.0f;
	float peakDb = -100.0f;
};

struct SourceRuntime {
	SourceHealth health = SourceHealth::Starting;
	QString detail;
	int width = 0, height = 0;
	bool active = false;           // rendered on the program output
	// media
	int mediaState = 0;            // obs_media_state
	qint64 mediaTimeMs = 0, mediaDurationMs = 0;
};

class Engine : public QObject {
	Q_OBJECT
public:
	static constexpr int kProgramChannel = 0;
	static constexpr int kGraphicsChannel = 1;
	static constexpr int kFtbChannel = 2;
	static constexpr int kFirstAudioChannel = 10;
	static constexpr int kMaxIsolatedTracks = 5; // tracks 2..6

	explicit Engine(QObject *parent = nullptr);
	~Engine() override;

	bool initialize(const EngineInit &init, QString *error);
	void shutdown();
	bool isInitialized() const { return m_initialized; }

	// Changing the canvas requires that no output is active.
	bool resetVideo(const VideoSettings &v, QString *error);
	VideoSettings videoSettings() const { return m_video; }

	// ---- sources ------------------------------------------------------------
	bool createSource(const SourceConfig &cfg, QString *error);
	void updateSource(const SourceConfig &cfg); // settings, delay, audio, media options
	void removeSource(const QString &uid);
	bool hasSource(const QString &uid) const;
	obs_source_t *obsSource(const QString &uid) const; // borrowed
	SourceRuntime runtime(const QString &uid) const;
	AudioLevel audioLevel(const QString &uid) const;
	AudioLevel programLevel() const; // peak/RMS of the program mix (what is recorded/streamed)
	void reconnectSource(const QString &uid);
	QStringList sourceIds() const;

	// Audio controls (real-time; also part of updateSource()).
	void setVolumeDb(const QString &uid, double db);
	void setMuted(const QString &uid, bool muted);
	void setSyncOffsetMs(const QString &uid, int ms);
	void setMonitoring(const QString &uid, MonitorMode mode);
	void setAudioFilters(const QString &uid, const AudioFilters &f);
	bool setMonitoringDevice(const QString &id, const QString &name);
	static QVector<DeviceInfo> monitoringDevices();

	// Media controls.
	void mediaPlayPause(const QString &uid);
	void mediaRestart(const QString &uid);
	void mediaStop(const QString &uid);

	// ---- compositions --------------------------------------------------------
	bool createScene(const SceneConfig &cfg);
	void updateScene(const SceneConfig &cfg); // atomic item rebuild (safe on air)
	void removeScene(const QString &uid);
	obs_source_t *sceneSource(const QString &uid) const; // borrowed

	// ---- program bus -----------------------------------------------------------
	// Executes an accepted decision from ProgramController.
	bool take(const TakeDecision &d);
	bool fadeToBlack(const TakeDecision &d, bool toBlack);
	void clearProgram(); // nothing on air (black)
	double transitionProgress() const;
	obs_source_t *contentSource(const ContentRef &ref) const; // borrowed

	// ---- graphics layer ------------------------------------------------------
	void setLowerThird(const LowerThirdConfig &cfg); // create or update
	void removeLowerThird(const QString &uid);
	void showLowerThird(const QString &uid, bool show);
	GraphicPhase lowerThirdPhase(const QString &uid) const;
	void setOverlay(const OverlayImageConfig &cfg);
	void removeOverlay(const QString &uid);
	void showOverlay(const QString &uid, bool show);
	bool overlayVisible(const QString &uid) const;
	void hideAllGraphics();

	// ---- services ------------------------------------------------------------
	ThumbnailService &thumbnails() { return *m_thumbs; }
	OutputManager &outputs() { return *m_outputs; }

	// Audio channels with an isolated recording track, in track order.
	QStringList isolatedTrackSources() const;
	int trackIndexOf(const QString &uid) const; // 1..5 (mix index) or -1

	// Last device enumeration from the background watcher (UI thread only).
	static QVector<DeviceInfo> cachedDevices(SourceKind kind, bool *known = nullptr);
	void setPreviewEnabled(const QString &uid, bool enabled); // thumbnail + keep device open
	bool isPreviewEnabled(const QString &uid) const;

	// Registered libobs type ids (diagnostics).
	static QStringList registeredInputs();
	static QStringList registeredEncoders();

signals:
	void transitionFinished();
	void fadeToBlackFinished();
	void mediaEnded(const QString &uid);
	void sourceHealthChanged(const QString &uid);
	void log(int level, const QString &message);

private:
	struct Src;
	struct Graphic;
	struct Overlay;

	void loadModules();
	void createProgramBus();
	void destroyProgramBus();
	obs_source_t *transitionFor(TransitionType t) const;
	void applyCanvasToSolo(Src &s);
	void applyAudio(Src &s, const SourceConfig &cfg);
	void applyVideoDelay(Src &s, int ms);
	void applyMixers(Src &s);
	void applySyncOffset(obs_source_t *source, int64_t offsetNs);
	void reassignTracks();
	void buildSceneItems(obs_scene_t *scene, const SceneConfig &cfg);
	void pollHealth();
	void updateLowerThirdRender(Graphic &g);

	static void onTransitionStop(void *data, calldata_t *cd);
	static void onFtbStop(void *data, calldata_t *cd);
	static void onMediaEnded(void *data, calldata_t *cd);
	static void onVolmeter(void *data, const float magnitude[], const float peak[], const float inputPeak[]);
	static void onAudioCapture(void *param, obs_source_t *source, const audio_data *audio, bool muted);
	static void onProgramAudio(void *param, size_t mixIdx, audio_data *data);
	std::atomic<float> m_programRms{-100.0f};
	std::atomic<float> m_programPeak{-100.0f};

	bool m_initialized = false;
	VideoSettings m_video;
	QHash<QString, std::shared_ptr<Src>> m_sources;
	QHash<QString, obs_scene_t *> m_scenes; // owned refs
	QHash<QString, SceneConfig> m_sceneConfigs;
	QHash<QString, std::shared_ptr<Graphic>> m_graphics;
	QHash<QString, std::shared_ptr<Overlay>> m_overlays;

	std::array<obs_source_t *, 5> m_transitions{};
	obs_source_t *m_currentTransition = nullptr;
	obs_source_t *m_ftbTransition = nullptr;
	obs_source_t *m_black = nullptr;
	obs_scene_t *m_clearScene = nullptr;
	obs_scene_t *m_graphicsScene = nullptr;
	obs_source_t *m_overlayFade = nullptr;
	std::vector<int> m_freeAudioChannels;

	std::unique_ptr<ThumbnailService> m_thumbs;
	std::unique_ptr<OutputManager> m_outputs;
	QTimer *m_healthTimer = nullptr;
};

} // namespace ms
