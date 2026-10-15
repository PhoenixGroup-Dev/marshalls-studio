// Recording and streaming outputs plus performance statistics.
#pragma once

#include "StudioConfig.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct obs_output;
typedef struct obs_output obs_output_t;
struct obs_encoder;
typedef struct obs_encoder obs_encoder_t;
struct obs_service;
typedef struct obs_service obs_service_t;
struct calldata;
typedef struct calldata calldata_t;

class QTimer;

namespace ms {

class Engine;

struct EncoderInfo {
	QString id;
	QString label;
	bool hardware = false;
};

struct OutputStats {
	// global
	double cpuPercent = 0.0;
	double memoryMb = 0.0;
	double activeFps = 0.0;
	double renderLagPercent = 0.0;  // frames missed by the renderer (GPU/compositing)
	double encodeSkipPercent = 0.0; // frames skipped by the encoder (CPU/encoder overload)
	quint64 renderLagged = 0, encodeSkipped = 0;
	// recording
	bool recording = false;
	qint64 recordingMs = 0;
	QString recordingPath;
	qint64 recordingBytes = 0;
	qint64 diskFreeBytes = 0;
	double diskMinutesLeft = 0.0;
	// streaming
	bool streaming = false;
	bool reconnecting = false;
	qint64 streamingMs = 0;
	double streamKbps = 0.0;
	int streamDropped = 0;
	int streamTotalFrames = 0;
	double streamDropPercent = 0.0;
	double congestion = 0.0;
	int delaySec = 0;
};

class OutputManager : public QObject {
	Q_OBJECT
public:
	explicit OutputManager(Engine *engine);
	~OutputManager() override;
	void shutdown();

	static QVector<EncoderInfo> availableVideoEncoders();
	static QString pickEncoder(const QString &preferred);

	// ---- recording ----
	bool startRecording(const RecordingSettings &rec, QString *error);
	void stopRecording();
	bool isRecording() const;
	QString recordingPath() const { return m_recPath; }
	static QString defaultRecordingFolder();
	static QString recordingFilePath(const RecordingSettings &rec);

	// ---- streaming ----
	bool startStreaming(const StreamSettings &s, const QString &key, QString *error);
	void stopStreaming(bool force = false);
	bool isStreaming() const;

	OutputStats stats() const { return m_stats; }
	bool anyActive() const { return isRecording() || isStreaming(); }

	// Remux a finished MKV into MP4 (worker thread). Emits remuxFinished.
	void remuxToMp4(const QString &mkvPath);

signals:
	void recordingStarted(const QString &path);
	void recordingStopped(const QString &path, bool ok, const QString &error);
	void streamingStarted();
	void streamingStopped(bool ok, const QString &error);
	void streamReconnecting();
	void streamReconnected();
	void remuxFinished(const QString &output, bool ok, const QString &error);
	void statsUpdated(const OutputStats &stats);
	void warning(const QString &message);

private:
	obs_encoder_t *createVideoEncoder(const EncoderSettings &e, bool forStream, const QString &name,
					  QString *error);
	void releaseRecording();
	void releaseStreaming();
	void updateStats();
	static QString stopMessage(int code, const QString &lastError);

	static void onRecStart(void *data, calldata_t *cd);
	static void onRecStop(void *data, calldata_t *cd);
	static void onStreamStart(void *data, calldata_t *cd);
	static void onStreamStop(void *data, calldata_t *cd);
	static void onStreamReconnect(void *data, calldata_t *cd);
	static void onStreamReconnected(void *data, calldata_t *cd);

	Engine *m_engine;
	obs_output_t *m_rec = nullptr;
	obs_encoder_t *m_recVideo = nullptr;
	QVector<obs_encoder_t *> m_recAudio;
	RecordingSettings m_recSettings;
	QString m_recPath;
	QElapsedTimer m_recClock;
	bool m_recStopRequested = false;
	QString m_recStopReason;

	obs_output_t *m_stream = nullptr;
	obs_encoder_t *m_streamVideo = nullptr;
	obs_encoder_t *m_streamAudio = nullptr;
	obs_service_t *m_service = nullptr;
	QElapsedTimer m_streamClock;
	bool m_reconnecting = false;
	quint64 m_lastBytes = 0;
	qint64 m_lastBytesMs = 0;

	QTimer *m_timer = nullptr;
	void *m_cpuInfo = nullptr;
	OutputStats m_stats;
	quint64 m_lastLagged = 0, m_lastTotal = 0, m_lastSkipped = 0, m_lastVideoTotal = 0;
	QString m_secret;
};

} // namespace ms

Q_DECLARE_METATYPE(ms::OutputStats)
