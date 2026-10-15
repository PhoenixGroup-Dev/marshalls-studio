// Application layer: the operator's actions.
//
// Owns the profile, persistence, the program state machine, shortcuts and the
// media engine. The UI and the integration tests drive the studio exclusively
// through this class, so every button exercises the same code path as tests.
#pragma once

#include "ConfigStore.h"
#include "Engine.h"
#include "Outputs.h"
#include "ProgramController.h"
#include "SecretStore.h"
#include "ShortcutMap.h"
#include "StudioConfig.h"

#include <QObject>
#include <QTimer>

namespace ms {

enum class AlertLevel { Info = 0, Warning = 1, Error = 2 };

class Studio : public QObject {
	Q_OBJECT
public:
	struct Paths {
		QString configRoot;   // profiles, app.json
		QString logDir;
		QString moduleConfig; // libobs plugin config
		QString secretsDir;
	};
	static Paths defaultPaths();

	explicit Studio(const Paths &paths, QObject *parent = nullptr);
	~Studio() override;

	bool start(const QString &profileName, const EngineInit &init, QString *error);
	void stop();

	Engine &engine() { return m_engine; }
	StudioProfile &profile() { return m_profile; }
	const StudioProfile &profile() const { return m_profile; }
	ConfigStore &store() { return m_store; }
	ProgramController &program() { return m_program; }
	ShortcutMap &shortcuts() { return m_shortcuts; }
	const Paths &paths() const { return m_paths; }
	QString startupNotes() const { return m_startupNotes; }

	// ---- content -------------------------------------------------------------
	QString addSource(SourceConfig cfg, QString *error);
	bool updateSource(const SourceConfig &cfg);
	void removeSource(const QString &uid);
	void renameSource(const QString &uid, const QString &name);
	QString addScene(SceneConfig cfg);
	void updateScene(const SceneConfig &cfg);
	void removeScene(const QString &uid);
	QString duplicateScene(const QString &uid);
	void renameScene(const QString &uid, const QString &name);

	// Grid = video sources + compositions, in operator-defined order.
	QVector<ContentRef> gridOrder() const;
	void moveInGrid(const ContentRef &ref, int newIndex);
	QString contentName(const ContentRef &ref) const;
	bool isOnProgram(const ContentRef &ref) const;
	QVector<QString> audioChannels() const; // sources shown in the mixer, in order

	// ---- program -------------------------------------------------------------
	void select(const ContentRef &ref);
	TakeDecision take(TakeMode mode);
	TakeDecision takeContent(const ContentRef &ref, TakeMode mode);
	TakeDecision takeSlot(int slot1Based, TakeMode mode);
	void selectSlot(int slot1Based);
	TakeDecision toggleFadeToBlack();
	void setTransition(const TransitionSettings &t);

	// ---- audio ---------------------------------------------------------------
	void setVolumeDb(const QString &uid, double db);
	void setMuted(const QString &uid, bool muted);
	void toggleMute(const QString &uid);
	void setSyncOffset(const QString &uid, int ms);
	void setMonitor(const QString &uid, MonitorMode m);
	void setFilters(const QString &uid, const AudioFilters &f);
	void setMonitoringDevice(const QString &id, const QString &name);
	QStringList audioWarnings() const;

	// ---- graphics ------------------------------------------------------------
	QString addLowerThird(LowerThirdConfig cfg);
	void updateLowerThird(const LowerThirdConfig &cfg);
	void removeLowerThird(const QString &uid);
	void toggleLowerThird(const QString &uid);
	void showLowerThird(const QString &uid, bool show);
	void toggleLowerThirdSlot(int slot1Based);
	QString addOverlay(OverlayImageConfig cfg);
	void updateOverlay(const OverlayImageConfig &cfg);
	void removeOverlay(const QString &uid);
	void toggleOverlay(const QString &uid);
	void clearGraphics();

	// ---- outputs -------------------------------------------------------------
	bool startRecording(QString *error);
	void stopRecording();
	bool isRecording() const;
	bool startStreaming(QString *error);
	void stopStreaming();
	bool isStreaming() const;
	void setStreamKey(const QString &key);
	QString streamKey() const;

	// ---- settings / persistence ----------------------------------------------
	bool applyVideo(const VideoSettings &v, QString *error);
	void setRecordingSettings(const RecordingSettings &r);
	void setStreamSettings(const StreamSettings &s);
	void setUiSettings(const UiSettings &u);
	void setShortcuts(const QMap<QString, QString> &overrides);
	void markDirty();
	bool saveNow(QString *error = nullptr);

	QStringList profiles() const;
	bool switchProfile(const QString &name, QString *error);
	bool createProfile(const QString &name, bool copyCurrent, QString *error);

signals:
	void contentChanged();
	void sourceChanged(const QString &uid);
	void programChanged();
	void selectionChanged();
	void transitionStateChanged(bool running);
	void ftbChanged();
	void graphicsChanged();
	void recordingChanged(bool active);
	void streamingChanged(bool active);
	void streamReconnecting(bool reconnecting);
	void alert(int level, const QString &message);
	void statsUpdated(const ms::OutputStats &stats);
	void profileLoaded();
	void settingsChanged();

private:
	void applyProfileToEngine();
	void clearEngineContent();
	void onMediaEnded(const QString &uid);
	void rebindDevices();
	void emitAlert(AlertLevel l, const QString &m);
	TakeDecision executeTake(const TakeDecision &d);
	int nextOrder() const;

	Paths m_paths;
	ConfigStore m_store;
	SecretStore m_secrets;
	Engine m_engine;
	StudioProfile m_profile;
	ProgramController m_program;
	ShortcutMap m_shortcuts;
	QTimer m_saveTimer;
	QTimer m_rebindTimer;
	bool m_started = false;
	QString m_startupNotes;
	QString m_streamKeyCache;
};

} // namespace ms
