// Top bar: identity, production profile, system health and output controls.
#pragma once

#include "Outputs.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace ms {

class Studio;

class TopBar : public QWidget {
	Q_OBJECT
public:
	explicit TopBar(Studio *studio, QWidget *parent = nullptr);
	void refreshProfiles();

signals:
	void recordToggleRequested();
	void streamToggleRequested();
	void settingsRequested(int tab);
	void profileSwitchRequested(const QString &name);
	void newProfileRequested();
	void openRecordingsRequested();

protected:
	void paintEvent(QPaintEvent *) override;

private:
	void updateStats(const OutputStats &s);
	void refreshOutputs();
	Studio *m_studio;
	QComboBox *m_profile;
	QLabel *m_cpu, *m_mem, *m_fps, *m_render, *m_encode;
	QLabel *m_recTime, *m_recInfo;
	QLabel *m_liveTime, *m_liveInfo;
	QPushButton *m_rec, *m_live;
	QToolButton *m_settings;
	OutputStats m_last;
	bool m_updatingProfiles = false;
};

class AlertBar : public QWidget {
	Q_OBJECT
public:
	explicit AlertBar(Studio *studio, QWidget *parent = nullptr);
	void push(int level, const QString &message);

private:
	QLabel *m_icon;
	QLabel *m_text;
	QToolButton *m_history;
	QStringList m_entries;
};

} // namespace ms
