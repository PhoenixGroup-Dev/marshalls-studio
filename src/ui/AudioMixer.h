// Audio desk: one strip per input (microphones first), each with its own
// meter, fader, mute, monitoring and processing, plus the program master
// meter (the mix actually recorded/streamed).
#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QSlider;
class QToolButton;
class QTimer;

namespace ms {

class Studio;

class LevelMeter : public QWidget {
	Q_OBJECT
public:
	explicit LevelMeter(QWidget *parent = nullptr);
	void setLevel(float magnitudeDb, float peakDb);
	void setMuted(bool m);
	QSize sizeHint() const override { return {14, 160}; }

protected:
	void paintEvent(QPaintEvent *) override;

private:
	float m_mag = -100, m_peak = -100, m_hold = -100;
	bool m_muted = false;
	QElapsedTimer m_holdClock;
};

class ChannelStrip : public QWidget {
	Q_OBJECT
public:
	ChannelStrip(Studio *studio, const QString &uid, int index, QWidget *parent = nullptr);
	const QString &uid() const { return m_uid; }
	void tick();
	void refresh();

signals:
	void settingsRequested(const QString &uid);

private:
	Studio *m_studio;
	QString m_uid;
	QLabel *m_name;
	QLabel *m_index;
	QLabel *m_status;
	LevelMeter *m_meter;
	QSlider *m_fader;
	QLabel *m_db;
	QToolButton *m_mute;
	QToolButton *m_monitor;
	QToolButton *m_fx;
	QToolButton *m_reconnect;
	QElapsedTimer m_silence;
	bool m_updating = false;
};

class AudioMixer : public QWidget {
	Q_OBJECT
public:
	explicit AudioMixer(Studio *studio, QWidget *parent = nullptr);
	void rebuild();

signals:
	void settingsRequested(const QString &uid);
	void addRequested(int kind);

private:
	void tick();
	Studio *m_studio;
	QHBoxLayout *m_strips;
	QWidget *m_host;
	QList<ChannelStrip *> m_list;
	LevelMeter *m_master;
	QLabel *m_masterDb;
	QLabel *m_warning;
	QTimer *m_timer;
};

} // namespace ms
