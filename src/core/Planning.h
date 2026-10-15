// Pure helpers for planning outputs, layouts and audio safety. No engine
// dependency, fully unit tested.
#pragma once

#include "StudioConfig.h"

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ms {

// ---- Recording / disk ------------------------------------------------------
struct DiskEstimate {
	double mbPerMinute = 0.0;
	double minutesRemaining = 0.0; // until minFreeMb is reached
	bool belowMinimum = false;
	bool warn = false;             // less than 30 minutes left
};

// Approximate output rate. For CRF/VBR the rate is unknown in advance, so a
// conservative bits-per-pixel heuristic is used.
double estimateVideoKbps(const EncoderSettings &enc, int width, int height, double fps);
DiskEstimate estimateDisk(const RecordingSettings &rec, int width, int height, double fps, int audioTracks,
			  qint64 freeBytes);

// ---- Streaming -------------------------------------------------------------
struct PlatformPreset {
	QString id;
	QString label;
	QStringList servers;
	int maxVideoKbps = 0; // 0 = unknown/unlimited
	bool requiresRtmps = false;
	int keyframeSec = 2;
};
const QVector<PlatformPreset> &platformPresets();
const PlatformPreset *findPlatform(const QString &id);

struct ValidationIssue {
	enum class Severity { Warning, Error } severity;
	QString message;
};
QVector<ValidationIssue> validateStream(const StreamSettings &s, const QString &key, const VideoSettings &video);

// ---- Layout presets for compositions ----------------------------------------
struct LayoutPreset {
	QString id;
	QString label;
	int minItems;
};
const QVector<LayoutPreset> &layoutPresets();
// Returns normalized rects for `count` items (bottom → top order).
QVector<QRectF> layoutRects(const QString &presetId, int count);
bool layoutUsesFill(const QString &presetId);

// ---- Audio safety ----------------------------------------------------------
// Detects configurations likely to cause echo/feedback or duplicated capture.
QStringList audioSafetyWarnings(const StudioProfile &p);

// ---- dB helpers --------------------------------------------------------------
double dbToMul(double db);
double mulToDb(double mul);
// Fader position (0..1) ↔ dB with a broadcast-style taper (-inf..+10 dB).
double faderToDb(double pos);
double dbToFader(double db);

} // namespace ms
