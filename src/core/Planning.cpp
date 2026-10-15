#include "Planning.h"

#include <QUrl>
#include <cmath>

namespace ms {

double estimateVideoKbps(const EncoderSettings &enc, int width, int height, double fps)
{
	if (enc.rateControl == RateControl::CBR || enc.rateControl == RateControl::VBR)
		return enc.videoBitrateKbps;
	// CRF: ~0.1 bpp at crf 20 for talking-head content, doubling every 6 steps.
	const double bpp = 0.1 * std::pow(2.0, (20.0 - enc.crf) / 6.0);
	return width * height * fps * bpp / 1000.0;
}

DiskEstimate estimateDisk(const RecordingSettings &rec, int width, int height, double fps, int audioTracks,
			  qint64 freeBytes)
{
	DiskEstimate e;
	const double kbps = estimateVideoKbps(rec.encoder, width, height, fps) +
			    rec.encoder.audioBitrateKbps * std::max(1, audioTracks);
	e.mbPerMinute = kbps * 60.0 / 8.0 / 1024.0;
	const double usableMb = double(freeBytes) / (1024.0 * 1024.0) - rec.minFreeMb;
	e.belowMinimum = usableMb <= 0.0;
	e.minutesRemaining = e.mbPerMinute > 0 ? std::max(0.0, usableMb / e.mbPerMinute) : 0.0;
	e.warn = e.belowMinimum || e.minutesRemaining < 30.0;
	return e;
}

const QVector<PlatformPreset> &platformPresets()
{
	static const QVector<PlatformPreset> presets = {
		{"youtube", "YouTube", {"rtmps://a.rtmps.youtube.com:443/live2", "rtmp://a.rtmp.youtube.com/live2"}, 51000,
		 false, 2},
		{"twitch", "Twitch", {"rtmp://live.twitch.tv/app"}, 6000, false, 2},
		{"facebook", "Facebook Live", {"rtmps://live-api-s.facebook.com:443/rtmp/"}, 8000, true, 2},
		{"custom", QStringLiteral("Servidor personalizado (RTMP/RTMPS)"), {}, 0, false, 2},
	};
	return presets;
}

const PlatformPreset *findPlatform(const QString &id)
{
	for (const auto &p : platformPresets())
		if (p.id == id)
			return &p;
	return nullptr;
}

QVector<ValidationIssue> validateStream(const StreamSettings &s, const QString &key, const VideoSettings &video)
{
	using S = ValidationIssue::Severity;
	QVector<ValidationIssue> out;
	const QUrl url(s.server.trimmed());
	const QString scheme = url.scheme().toLower();
	if (s.server.trimmed().isEmpty())
		out.append({S::Error, QStringLiteral("Informe a URL do servidor.")});
	else if (scheme != "rtmp" && scheme != "rtmps")
		out.append({S::Error, QStringLiteral("Protocolo não suportado: use rtmp:// ou rtmps://.")});
	else if (url.host().isEmpty())
		out.append({S::Error, QStringLiteral("URL do servidor sem host.")});

	if (key.trimmed().isEmpty())
		out.append({S::Error, QStringLiteral("Informe a chave de transmissão.")});
	else if (!key.isEmpty() && s.server.contains(key))
		out.append({S::Warning, QStringLiteral("A chave parece estar também na URL do servidor.")});

	const PlatformPreset *p = findPlatform(s.platform);
	if (p) {
		if (p->requiresRtmps && scheme == "rtmp")
			out.append({S::Error, QStringLiteral("%1 exige RTMPS.").arg(p->label)});
		if (p->maxVideoKbps > 0 && s.encoder.videoBitrateKbps > p->maxVideoKbps)
			out.append({S::Warning, QStringLiteral("Bitrate de vídeo acima do máximo recomendado por %1 (%2 kbps).")
						      .arg(p->label)
						      .arg(p->maxVideoKbps)});
		if (s.encoder.keyframeSec == 0 || s.encoder.keyframeSec > p->keyframeSec)
			out.append({S::Warning, QStringLiteral("Intervalo de keyframe recomendado: %1 s.").arg(p->keyframeSec)});
	}
	if (s.encoder.rateControl == RateControl::CRF)
		out.append({S::Warning, QStringLiteral("CRF não é adequado para transmissão; será usado CBR.")});
	const double pixelsPerSec = double(video.outputWidth) * video.outputHeight * video.fpsNum / video.fpsDen;
	const double bpp = s.encoder.videoBitrateKbps * 1000.0 / std::max(1.0, pixelsPerSec);
	if (bpp < 0.03)
		out.append({S::Warning, QStringLiteral("Bitrate baixo para %1x%2: a imagem pode ficar pixelada.")
					      .arg(video.outputWidth)
					      .arg(video.outputHeight)});
	if (s.delaySec > 0 && s.delaySec * (s.encoder.videoBitrateKbps + s.encoder.audioBitrateKbps) / 8 > 512 * 1024)
		out.append({S::Warning, QStringLiteral("Atraso longo com bitrate alto consome muita memória.")});
	return out;
}

const QVector<LayoutPreset> &layoutPresets()
{
	static const QVector<LayoutPreset> presets = {
		{"full", QStringLiteral("Tela cheia"), 1},
		{"side_by_side", QStringLiteral("Lado a lado"), 2},
		{"three_up", QStringLiteral("Três lado a lado"), 3},
		{"grid_2x2", QStringLiteral("Grade 2×2"), 4},
		{"host_guest", QStringLiteral("Apresentador + convidado (destaque)"), 2},
		{"pip_br", QStringLiteral("Tela + câmera (canto inferior direito)"), 2},
		{"pip_bl", QStringLiteral("Tela + câmera (canto inferior esquerdo)"), 2},
		{"screen_side", QStringLiteral("Apresentação + câmera lateral"), 2},
		{"feature_two", QStringLiteral("Destaque + duas câmeras"), 3},
	};
	return presets;
}

bool layoutUsesFill(const QString &presetId)
{
	return presetId == "side_by_side" || presetId == "three_up" || presetId == "host_guest" ||
	       presetId == "feature_two" || presetId == "screen_side";
}

QVector<QRectF> layoutRects(const QString &id, int count)
{
	QVector<QRectF> r;
	if (count <= 0)
		return r;
	const double m = 0.0; // edge-to-edge; spacing is a design choice of the scene
	if (id == "side_by_side") {
		for (int i = 0; i < count; ++i)
			r.append(i < 2 ? QRectF(i * 0.5, 0, 0.5, 1) : QRectF(0, 0, 1, 1));
	} else if (id == "three_up") {
		for (int i = 0; i < count; ++i)
			r.append(i < 3 ? QRectF(i / 3.0, 0, 1 / 3.0, 1) : QRectF(0, 0, 1, 1));
	} else if (id == "grid_2x2") {
		for (int i = 0; i < count; ++i)
			r.append(i < 4 ? QRectF((i % 2) * 0.5, (i / 2) * 0.5, 0.5, 0.5) : QRectF(0, 0, 1, 1));
	} else if (id == "host_guest") {
		for (int i = 0; i < count; ++i)
			r.append(i == 0 ? QRectF(0, 0, 0.62, 1) : i == 1 ? QRectF(0.62, 0, 0.38, 1) : QRectF(0, 0, 1, 1));
	} else if (id == "pip_br" || id == "pip_bl") {
		const double w = 0.28, h = 0.28, pad = 0.025;
		for (int i = 0; i < count; ++i) {
			if (i == 0)
				r.append(QRectF(0, 0, 1, 1));
			else
				r.append(QRectF(id == "pip_br" ? 1 - w - pad : pad, 1 - h - pad, w, h));
		}
	} else if (id == "screen_side") {
		for (int i = 0; i < count; ++i)
			r.append(i == 0 ? QRectF(0, 0, 0.75, 1) : QRectF(0.75, (i - 1) * (1.0 / std::max(1, count - 1)), 0.25,
								      1.0 / std::max(1, count - 1)));
	} else if (id == "feature_two") {
		for (int i = 0; i < count; ++i)
			r.append(i == 0 ? QRectF(0, 0, 0.66, 1)
					: i <= 2 ? QRectF(0.66, (i - 1) * 0.5, 0.34, 0.5) : QRectF(0, 0, 1, 1));
	} else { // full
		for (int i = 0; i < count; ++i)
			r.append(QRectF(m, m, 1 - 2 * m, 1 - 2 * m));
	}
	return r;
}

QStringList audioSafetyWarnings(const StudioProfile &p)
{
	QStringList w;
	// 1) Same physical device captured twice.
	QMap<QString, QString> seen;
	for (const auto &s : p.sources) {
		if ((s.kind != SourceKind::AudioInput && s.kind != SourceKind::AudioOutput) || s.device.id.isEmpty())
			continue;
		const QString k = kindToString(s.kind) + QLatin1Char('|') + s.device.id;
		if (seen.contains(k))
			w.append(QStringLiteral("“%1” e “%2” capturam o mesmo dispositivo: o áudio será duplicado.")
					 .arg(seen.value(k), s.name));
		else
			seen.insert(k, s.name);
	}
	// 2) Monitoring while the computer playback is captured into the program:
	//    monitored audio comes back through the desktop capture → echo/feedback.
	bool desktopToProgram = false;
	QString desktopName;
	for (const auto &s : p.sources)
		if (s.kind == SourceKind::AudioOutput && s.audio.toProgram && !s.audio.muted) {
			desktopToProgram = true;
			desktopName = s.name;
		}
	if (desktopToProgram) {
		for (const auto &s : p.sources)
			if (s.audio.monitor != MonitorMode::Off && s.kind != SourceKind::AudioOutput)
				w.append(QStringLiteral("“%1” está em monitoramento e “%2” captura o áudio do computador: "
							"use fones ou um dispositivo de monitoramento diferente para evitar eco.")
						 .arg(s.name, desktopName));
	}
	// 3) Monitoring the desktop capture itself is a direct loop.
	for (const auto &s : p.sources)
		if (s.kind == SourceKind::AudioOutput && s.audio.monitor != MonitorMode::Off)
			w.append(QStringLiteral("Monitorar “%1” cria realimentação (o áudio do PC volta para si mesmo).").arg(s.name));
	return w;
}

double dbToMul(double db)
{
	return db <= -100.0 ? 0.0 : std::pow(10.0, db / 20.0);
}
double mulToDb(double mul)
{
	return mul <= 0.0 ? -100.0 : 20.0 * std::log10(mul);
}

// Piecewise taper: top 75% of travel covers -30..+10 dB linearly in dB,
// lower 25% covers -100..-30 dB (fine control where it matters).
double faderToDb(double pos)
{
	pos = std::clamp(pos, 0.0, 1.0);
	if (pos <= 0.0)
		return -100.0;
	if (pos >= 0.25)
		return -30.0 + (pos - 0.25) / 0.75 * 40.0;
	return -100.0 + pos / 0.25 * 70.0;
}
double dbToFader(double db)
{
	if (db <= -100.0)
		return 0.0;
	if (db >= -30.0)
		return std::clamp(0.25 + (db + 30.0) / 40.0 * 0.75, 0.0, 1.0);
	return std::clamp((db + 100.0) / 70.0 * 0.25, 0.0, 0.25);
}

} // namespace ms
