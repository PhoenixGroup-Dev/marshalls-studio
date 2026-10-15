#include "Outputs.h"

#include "Engine.h"
#include "Log.h"
#include "Planning.h"
#include "RtmpService.h"

#include <obs.h>
#include <media-io/media-remux.h>
#include <util/platform.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

#include <thread>

namespace ms {

namespace {

QByteArray utf8(const QString &s)
{
	return s.toUtf8();
}

void connectSig(obs_output_t *o, const char *sig, signal_callback_t cb, void *data)
{
	signal_handler_connect(obs_output_get_signal_handler(o), sig, cb, data);
}

void disconnectSig(obs_output_t *o, const char *sig, signal_callback_t cb, void *data)
{
	signal_handler_disconnect(obs_output_get_signal_handler(o), sig, cb, data);
}

const char *audioEncoderId()
{
	return "ffmpeg_aac";
}

} // namespace

OutputManager::OutputManager(Engine *engine) : QObject(engine), m_engine(engine)
{
	qRegisterMetaType<OutputStats>("OutputStats");
	m_cpuInfo = os_cpu_usage_info_start();
	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, &OutputManager::updateStats);
	m_timer->start(1000);
}

OutputManager::~OutputManager()
{
	shutdown();
}

void OutputManager::shutdown()
{
	if (m_timer)
		m_timer->stop();
	if (m_stream) {
		obs_output_force_stop(m_stream);
		releaseStreaming();
	}
	if (m_rec) {
		obs_output_stop(m_rec);
		// Give the muxer a moment to finalize the file.
		for (int i = 0; i < 100 && obs_output_active(m_rec); ++i)
			QThread::msleep(50);
		releaseRecording();
	}
	if (m_cpuInfo) {
		os_cpu_usage_info_destroy(static_cast<os_cpu_usage_info_t *>(m_cpuInfo));
		m_cpuInfo = nullptr;
	}
}

QVector<EncoderInfo> OutputManager::availableVideoEncoders()
{
	struct Known {
		const char *id;
		const char *label;
		bool hw;
	};
	static const Known known[] = {
		{"obs_qsv11_v2", "Intel Quick Sync (H.264)", true},
		{"obs_qsv11", "Intel Quick Sync (H.264)", true},
		{"obs_nvenc_h264_tex", "NVIDIA NVENC (H.264)", true},
		{"jim_nvenc", "NVIDIA NVENC (H.264)", true},
		{"ffmpeg_nvenc", "NVIDIA NVENC (H.264, FFmpeg)", true},
		{"h264_texture_amf", "AMD AMF (H.264)", true},
		{"ffmpeg_vaapi_tex", "VA-API (H.264)", true},
		{"ffmpeg_vaapi", "VA-API (H.264)", true},
		{"com.apple.videotoolbox.videoencoder.ave.avc", "Apple VideoToolbox (H.264)", true},
		{"obs_x264", "x264 (software, CPU)", false},
	};
	QVector<EncoderInfo> out;
	const char *id = nullptr;
	QStringList registered;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i)
		registered.append(QString::fromUtf8(id));
	for (const auto &k : known) {
		const QString kid = QString::fromLatin1(k.id);
		if (!registered.contains(kid))
			continue;
		const uint32_t caps = obs_get_encoder_caps(k.id);
		if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;
		bool dupLabel = false;
		for (const auto &e : out)
			dupLabel |= e.label == QLatin1String(k.label);
		if (dupLabel)
			continue;
		out.append({kid, QString::fromUtf8(k.label), k.hw});
	}
	return out;
}

QString OutputManager::pickEncoder(const QString &preferred)
{
	const auto list = availableVideoEncoders();
	for (const auto &e : list)
		if (e.id == preferred)
			return e.id;
	// Auto: first hardware encoder, else x264.
	for (const auto &e : list)
		if (e.hardware)
			return e.id;
	return list.isEmpty() ? QString() : list.last().id;
}

obs_encoder_t *OutputManager::createVideoEncoder(const EncoderSettings &e, bool forStream, const QString &name,
						 QString *error)
{
	const QString id = pickEncoder(e.encoderId);
	if (id.isEmpty()) {
		if (error)
			*error = QStringLiteral("Nenhum codificador de vídeo H.264 disponível.");
		return nullptr;
	}
	obs_data_t *d = obs_data_create();
	RateControl rc = e.rateControl;
	if (forStream && rc == RateControl::CRF)
		rc = RateControl::CBR; // CRF is not suitable for live streaming
	const bool x264 = id == QLatin1String("obs_x264");
	if (rc == RateControl::CRF) {
		obs_data_set_string(d, "rate_control", x264 ? "CRF" : "CQP");
		obs_data_set_int(d, "crf", e.crf);
		obs_data_set_int(d, "cqp", e.crf);
		obs_data_set_int(d, "qpi", e.crf);
		obs_data_set_int(d, "qpp", e.crf);
	} else {
		obs_data_set_string(d, "rate_control", rc == RateControl::CBR ? "CBR" : "VBR");
		obs_data_set_int(d, "bitrate", e.videoBitrateKbps);
		obs_data_set_int(d, "max_bitrate", e.videoBitrateKbps * 3 / 2);
	}
	obs_data_set_int(d, "keyint_sec", e.keyframeSec);
	obs_data_set_string(d, "profile", "high");
	if (x264) {
		obs_data_set_string(d, "preset", utf8(e.preset.isEmpty() ? "veryfast" : e.preset).constData());
	} else {
		obs_data_set_string(d, "preset2", "p5");          // NVENC
		obs_data_set_string(d, "target_usage", "balanced"); // QSV
		obs_data_set_string(d, "preset", "quality");       // AMF
	}
	obs_encoder_t *enc = obs_video_encoder_create(utf8(id).constData(), utf8(name).constData(), d, nullptr);
	obs_data_release(d);
	if (!enc) {
		if (error)
			*error = QStringLiteral("Falha ao criar o codificador %1.").arg(id);
		return nullptr;
	}
	obs_encoder_set_video(enc, obs_get_video());
	logInfo("output", QStringLiteral("Codificador de vídeo “%1”: %2, %3 %4")
				  .arg(name, id, rateControlToString(rc))
				  .arg(rc == RateControl::CRF ? QString::number(e.crf)
							      : QString::number(e.videoBitrateKbps) + " kbps"));
	return enc;
}

// ---------------------------------------------------------------------------
// recording
// ---------------------------------------------------------------------------

QString OutputManager::defaultRecordingFolder()
{
	QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	if (base.isEmpty())
		base = QDir::homePath();
	return base + QStringLiteral("/Marshalls Studio");
}

QString OutputManager::recordingFilePath(const RecordingSettings &rec)
{
	const QString folder = rec.folder.isEmpty() ? defaultRecordingFolder() : rec.folder;
	QString base = rec.filenamePattern.arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss"));
	for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
		base.replace(c, QLatin1Char('-'));
	QString path = folder + QLatin1Char('/') + base + QLatin1Char('.') + rec.container;
	for (int i = 2; QFileInfo::exists(path); ++i)
		path = folder + QLatin1Char('/') + base + QStringLiteral(" (%1).").arg(i) + rec.container;
	return path;
}

bool OutputManager::isRecording() const
{
	return m_rec && obs_output_active(m_rec);
}

bool OutputManager::startRecording(const RecordingSettings &rec, QString *error)
{
	if (isRecording())
		return true;
	releaseRecording();
	m_recSettings = rec;
	const QString path = recordingFilePath(rec);
	const QString folder = QFileInfo(path).absolutePath();
	if (!QDir().mkpath(folder)) {
		if (error)
			*error = QStringLiteral("Não foi possível criar a pasta de gravação: %1").arg(folder);
		return false;
	}
	QStorageInfo storage(folder);
	const VideoSettings v = m_engine->videoSettings();
	const QStringList tracks = rec.isolatedTracks ? m_engine->isolatedTrackSources() : QStringList{};
	const DiskEstimate est = estimateDisk(rec, v.outputWidth, v.outputHeight, double(v.fpsNum) / v.fpsDen,
					      1 + int(tracks.size()), storage.bytesAvailable());
	if (est.belowMinimum) {
		if (error)
			*error = QStringLiteral("Espaço insuficiente em %1: %2 MB livres (mínimo configurado: %3 MB).")
					 .arg(storage.rootPath())
					 .arg(storage.bytesAvailable() / (1024 * 1024))
					 .arg(rec.minFreeMb);
		return false;
	}

	if (rec.shareStreamEncoder && m_streamVideo) {
		m_recVideo = obs_encoder_get_ref(m_streamVideo);
	} else {
		m_recVideo = createVideoEncoder(rec.encoder, false, QStringLiteral("rec-video"), error);
		if (!m_recVideo)
			return false;
	}

	obs_data_t *os = obs_data_create();
	obs_data_set_string(os, "path", utf8(path).constData());
	m_rec = obs_output_create("ffmpeg_muxer", "ms-recording", os, nullptr);
	obs_data_release(os);
	if (!m_rec) {
		if (error)
			*error = QStringLiteral("Saída de gravação indisponível (ffmpeg_muxer).");
		releaseRecording();
		return false;
	}
	obs_output_set_video_encoder(m_rec, m_recVideo);

	// Track 1 = program mix; tracks 2.. = isolated microphones.
	QVector<int> mixes{0};
	for (const QString &uid : tracks)
		mixes.append(m_engine->trackIndexOf(uid));
	size_t idx = 0;
	for (int mix : mixes) {
		obs_data_t *ad = obs_data_create();
		obs_data_set_int(ad, "bitrate", rec.encoder.audioBitrateKbps);
		obs_encoder_t *ae = obs_audio_encoder_create(audioEncoderId(), utf8(QStringLiteral("rec-audio-%1").arg(mix)).constData(),
							     ad, size_t(mix), nullptr);
		obs_data_release(ad);
		if (!ae) {
			if (error)
				*error = QStringLiteral("Falha ao criar o codificador de áudio AAC.");
			releaseRecording();
			return false;
		}
		obs_encoder_set_audio(ae, obs_get_audio());
		obs_output_set_audio_encoder(m_rec, ae, idx++);
		m_recAudio.append(ae);
	}

	connectSig(m_rec, "start", &OutputManager::onRecStart, this);
	connectSig(m_rec, "stop", &OutputManager::onRecStop, this);
	m_recPath = path;
	m_recStopRequested = false;
	m_recStopReason.clear();
	if (!obs_output_start(m_rec)) {
		const QString err = QString::fromUtf8(obs_output_get_last_error(m_rec));
		if (error)
			*error = QStringLiteral("A gravação não iniciou. %1").arg(err);
		logError("output", QStringLiteral("Falha ao iniciar gravação: %1").arg(err));
		releaseRecording();
		return false;
	}
	m_recClock.start();
	logInfo("output", QStringLiteral("Gravação iniciada: %1 (%2 trilhas de áudio)").arg(path).arg(mixes.size()));
	return true;
}

void OutputManager::stopRecording()
{
	if (!m_rec)
		return;
	m_recStopRequested = true;
	obs_output_stop(m_rec);
}

void OutputManager::releaseRecording()
{
	if (m_rec) {
		disconnectSig(m_rec, "start", &OutputManager::onRecStart, this);
		disconnectSig(m_rec, "stop", &OutputManager::onRecStop, this);
		obs_output_release(m_rec);
		m_rec = nullptr;
	}
	if (m_recVideo) {
		obs_encoder_release(m_recVideo);
		m_recVideo = nullptr;
	}
	for (auto *a : m_recAudio)
		obs_encoder_release(a);
	m_recAudio.clear();
}

void OutputManager::onRecStart(void *data, calldata_t *)
{
	auto *self = static_cast<OutputManager *>(data);
	QMetaObject::invokeMethod(self, [self] { emit self->recordingStarted(self->m_recPath); }, Qt::QueuedConnection);
}

void OutputManager::onRecStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<OutputManager *>(data);
	const int code = int(calldata_int(cd, "code"));
	const QString lastErr = QString::fromUtf8(calldata_string(cd, "last_error"));
	QMetaObject::invokeMethod(
		self,
		[self, code, lastErr] {
			const QString path = self->m_recPath;
			const bool ok = code == OBS_OUTPUT_SUCCESS;
			QString msg = ok ? self->m_recStopReason : stopMessage(code, lastErr);
			if (ok)
				logInfo("output", QStringLiteral("Gravação finalizada: %1 (%2 bytes)")
							  .arg(path)
							  .arg(QFileInfo(path).size()));
			else
				logError("output", QStringLiteral("Gravação interrompida: %1").arg(msg));
			const bool remux = self->m_recSettings.remuxToMp4 && self->m_recSettings.container == "mkv";
			self->releaseRecording();
			emit self->recordingStopped(path, ok, msg);
			if (remux && QFileInfo::exists(path))
				self->remuxToMp4(path);
		},
		Qt::QueuedConnection);
}

void OutputManager::remuxToMp4(const QString &mkvPath)
{
	QString out = mkvPath;
	out.replace(QRegularExpression("\\.mkv$"), ".mp4");
	if (out == mkvPath)
		out += ".mp4";
	std::thread([this, mkvPath, out] {
		media_remux_job_t job = nullptr;
		bool ok = media_remux_job_create(&job, utf8(mkvPath).constData(), utf8(out).constData());
		if (ok) {
			ok = media_remux_job_process(job, nullptr, nullptr);
			media_remux_job_destroy(job);
		}
		const QString err = ok ? QString() : QStringLiteral("Falha ao converter para MP4.");
		QMetaObject::invokeMethod(this, [this, out, ok, err] { emit remuxFinished(out, ok, err); },
					  Qt::QueuedConnection);
	}).detach();
}

// ---------------------------------------------------------------------------
// streaming
// ---------------------------------------------------------------------------

bool OutputManager::isStreaming() const
{
	return m_stream && (obs_output_active(m_stream) || obs_output_reconnecting(m_stream));
}

bool OutputManager::startStreaming(const StreamSettings &s, const QString &key, QString *error)
{
	if (isStreaming())
		return true;
	releaseStreaming();
	const VideoSettings v = m_engine->videoSettings();
	for (const auto &issue : validateStream(s, key, v)) {
		if (issue.severity == ValidationIssue::Severity::Error) {
			if (error)
				*error = issue.message;
			return false;
		}
		emit warning(issue.message);
	}
	m_secret = key;
	Redactor::instance().addSecret(key);

	obs_data_t *sd = obs_data_create();
	obs_data_set_string(sd, "server", utf8(s.server.trimmed()).constData());
	obs_data_set_string(sd, "key", utf8(key.trimmed()).constData());
	obs_data_set_bool(sd, "use_auth", false);
	m_service = obs_service_create(kRtmpServiceId, "ms-stream-service", sd, nullptr);
	obs_data_release(sd);
	if (!m_service) {
		if (error)
			*error = QStringLiteral("Serviço RTMP indisponível.");
		return false;
	}

	m_streamVideo = createVideoEncoder(s.encoder, true, QStringLiteral("stream-video"), error);
	if (!m_streamVideo) {
		releaseStreaming();
		return false;
	}
	obs_data_t *ad = obs_data_create();
	obs_data_set_int(ad, "bitrate", s.encoder.audioBitrateKbps);
	m_streamAudio = obs_audio_encoder_create(audioEncoderId(), "stream-audio", ad, 0, nullptr);
	obs_data_release(ad);
	if (!m_streamAudio) {
		if (error)
			*error = QStringLiteral("Falha ao criar o codificador de áudio.");
		releaseStreaming();
		return false;
	}
	obs_encoder_set_audio(m_streamAudio, obs_get_audio());

	m_stream = obs_output_create("rtmp_output", "ms-stream", nullptr, nullptr);
	if (!m_stream) {
		if (error)
			*error = QStringLiteral("Saída RTMP indisponível (plugin obs-outputs).");
		releaseStreaming();
		return false;
	}
	obs_output_set_video_encoder(m_stream, m_streamVideo);
	obs_output_set_audio_encoder(m_stream, m_streamAudio, 0);
	obs_output_set_service(m_stream, m_service);
	obs_output_set_reconnect_settings(m_stream, s.reconnectRetries, s.reconnectDelaySec);
	obs_output_set_delay(m_stream, uint32_t(s.delaySec), s.delayPreserve ? OBS_OUTPUT_DELAY_PRESERVE : 0);
	connectSig(m_stream, "start", &OutputManager::onStreamStart, this);
	connectSig(m_stream, "stop", &OutputManager::onStreamStop, this);
	connectSig(m_stream, "reconnect", &OutputManager::onStreamReconnect, this);
	connectSig(m_stream, "reconnect_success", &OutputManager::onStreamReconnected, this);

	m_reconnecting = false;
	m_lastBytes = 0;
	m_lastBytesMs = 0;
	if (!obs_output_start(m_stream)) {
		const QString err = Redactor::instance().redact(QString::fromUtf8(obs_output_get_last_error(m_stream)));
		if (error)
			*error = QStringLiteral("A transmissão não iniciou. %1").arg(err);
		logError("output", QStringLiteral("Falha ao iniciar transmissão: %1").arg(err));
		releaseStreaming();
		return false;
	}
	m_streamClock.start();
	logInfo("output", QStringLiteral("Conectando a %1 (atraso %2 s)…").arg(s.server).arg(s.delaySec));
	return true;
}

void OutputManager::stopStreaming(bool force)
{
	if (!m_stream)
		return;
	if (force || obs_output_reconnecting(m_stream))
		obs_output_force_stop(m_stream);
	else
		obs_output_stop(m_stream);
}

void OutputManager::releaseStreaming()
{
	if (m_stream) {
		disconnectSig(m_stream, "start", &OutputManager::onStreamStart, this);
		disconnectSig(m_stream, "stop", &OutputManager::onStreamStop, this);
		disconnectSig(m_stream, "reconnect", &OutputManager::onStreamReconnect, this);
		disconnectSig(m_stream, "reconnect_success", &OutputManager::onStreamReconnected, this);
		obs_output_release(m_stream);
		m_stream = nullptr;
	}
	if (m_streamVideo)
		obs_encoder_release(m_streamVideo);
	if (m_streamAudio)
		obs_encoder_release(m_streamAudio);
	if (m_service)
		obs_service_release(m_service);
	m_streamVideo = nullptr;
	m_streamAudio = nullptr;
	m_service = nullptr;
}

void OutputManager::onStreamStart(void *data, calldata_t *)
{
	auto *self = static_cast<OutputManager *>(data);
	QMetaObject::invokeMethod(self, [self] {
		logInfo("output", "Transmissão conectada");
		emit self->streamingStarted();
	}, Qt::QueuedConnection);
}

void OutputManager::onStreamStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<OutputManager *>(data);
	const int code = int(calldata_int(cd, "code"));
	const QString lastErr = QString::fromUtf8(calldata_string(cd, "last_error"));
	QMetaObject::invokeMethod(
		self,
		[self, code, lastErr] {
			const bool ok = code == OBS_OUTPUT_SUCCESS;
			const QString msg = ok ? QString() : stopMessage(code, Redactor::instance().redact(lastErr));
			if (ok)
				logInfo("output", "Transmissão encerrada");
			else
				logError("output", QStringLiteral("Transmissão interrompida: %1").arg(msg));
			self->m_reconnecting = false;
			// Releasing the encoders while recording shares them is safe: refs are counted.
			self->releaseStreaming();
			emit self->streamingStopped(ok, msg);
		},
		Qt::QueuedConnection);
}

void OutputManager::onStreamReconnect(void *data, calldata_t *)
{
	auto *self = static_cast<OutputManager *>(data);
	QMetaObject::invokeMethod(self, [self] {
		self->m_reconnecting = true;
		logWarn("output", "Conexão perdida; reconectando…");
		emit self->streamReconnecting();
	}, Qt::QueuedConnection);
}

void OutputManager::onStreamReconnected(void *data, calldata_t *)
{
	auto *self = static_cast<OutputManager *>(data);
	QMetaObject::invokeMethod(self, [self] {
		self->m_reconnecting = false;
		logInfo("output", "Reconectado");
		emit self->streamReconnected();
	}, Qt::QueuedConnection);
}

QString OutputManager::stopMessage(int code, const QString &lastError)
{
	QString base;
	switch (code) {
	case OBS_OUTPUT_SUCCESS: return {};
	case OBS_OUTPUT_BAD_PATH: base = QStringLiteral("Endereço inválido ou caminho inacessível."); break;
	case OBS_OUTPUT_CONNECT_FAILED: base = QStringLiteral("Não foi possível conectar ao servidor."); break;
	case OBS_OUTPUT_INVALID_STREAM:
		base = QStringLiteral("O servidor recusou a transmissão (chave inválida ou já em uso?).");
		break;
	case OBS_OUTPUT_ERROR: base = QStringLiteral("Erro na saída."); break;
	case OBS_OUTPUT_DISCONNECTED: base = QStringLiteral("Desconectado do servidor."); break;
	case OBS_OUTPUT_UNSUPPORTED: base = QStringLiteral("Formato ou codec não suportado pela saída."); break;
	case OBS_OUTPUT_NO_SPACE: base = QStringLiteral("Sem espaço em disco."); break;
	case OBS_OUTPUT_ENCODE_ERROR: base = QStringLiteral("Falha no codificador."); break;
	default: base = QStringLiteral("Falha (código %1).").arg(code);
	}
	return lastError.isEmpty() ? base : base + QLatin1Char(' ') + lastError;
}

// ---------------------------------------------------------------------------
// statistics
// ---------------------------------------------------------------------------

void OutputManager::updateStats()
{
	OutputStats s;
	if (m_cpuInfo)
		s.cpuPercent = os_cpu_usage_info_query(static_cast<os_cpu_usage_info_t *>(m_cpuInfo));
	s.memoryMb = double(os_get_proc_resident_size()) / (1024.0 * 1024.0);
	s.activeFps = obs_get_active_fps();

	const quint64 lagged = obs_get_lagged_frames();
	const quint64 total = obs_get_total_frames();
	if (total > m_lastTotal)
		s.renderLagPercent = 100.0 * double(lagged - std::min(lagged, m_lastLagged)) / double(total - m_lastTotal);
	m_lastLagged = lagged;
	m_lastTotal = total;
	s.renderLagged = lagged;

	video_t *video = obs_get_video();
	const quint64 skipped = video ? video_output_get_skipped_frames(video) : 0;
	const quint64 vtotal = video ? video_output_get_total_frames(video) : 0;
	if (vtotal > m_lastVideoTotal)
		s.encodeSkipPercent =
			100.0 * double(skipped - std::min(skipped, m_lastSkipped)) / double(vtotal - m_lastVideoTotal);
	m_lastSkipped = skipped;
	m_lastVideoTotal = vtotal;
	s.encodeSkipped = skipped;

	if (isRecording()) {
		s.recording = true;
		s.recordingMs = m_recClock.elapsed();
		s.recordingPath = m_recPath;
		// The muxer subprocess reports bytes late; the file size is authoritative.
		s.recordingBytes = std::max<qint64>(qint64(obs_output_get_total_bytes(m_rec)), QFileInfo(m_recPath).size());
		QStorageInfo st(QFileInfo(m_recPath).absolutePath());
		s.diskFreeBytes = st.bytesAvailable();
		const VideoSettings v = m_engine->videoSettings();
		const DiskEstimate est = estimateDisk(m_recSettings, v.outputWidth, v.outputHeight,
						      double(v.fpsNum) / v.fpsDen, int(m_recAudio.size()), s.diskFreeBytes);
		s.diskMinutesLeft = est.minutesRemaining;
		if (est.belowMinimum && !m_recStopRequested) {
			m_recStopReason = QStringLiteral("Gravação encerrada automaticamente: espaço em disco abaixo de %1 MB.")
						  .arg(m_recSettings.minFreeMb);
			logError("output", m_recStopReason);
			emit warning(m_recStopReason);
			stopRecording();
		}
	}
	if (m_stream && (obs_output_active(m_stream) || obs_output_reconnecting(m_stream))) {
		s.streaming = true;
		s.reconnecting = obs_output_reconnecting(m_stream);
		s.streamingMs = m_streamClock.elapsed();
		const quint64 bytes = obs_output_get_total_bytes(m_stream);
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (m_lastBytesMs > 0 && now > m_lastBytesMs && bytes >= m_lastBytes)
			s.streamKbps = double(bytes - m_lastBytes) * 8.0 / double(now - m_lastBytesMs);
		m_lastBytes = bytes;
		m_lastBytesMs = now;
		s.streamDropped = obs_output_get_frames_dropped(m_stream);
		s.streamTotalFrames = obs_output_get_total_frames(m_stream);
		s.streamDropPercent = s.streamTotalFrames > 0 ? 100.0 * s.streamDropped / s.streamTotalFrames : 0.0;
		s.congestion = obs_output_get_congestion(m_stream);
		s.delaySec = int(obs_output_get_active_delay(m_stream));
	}
	m_stats = s;
	emit statsUpdated(s);
}

} // namespace ms
