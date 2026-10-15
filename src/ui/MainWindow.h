// Main switcher desk: program monitor, take controls, graphics, the permanent
// source grid and the audio desk on one screen.
#pragma once

#include "ProgramController.h"

#include <QMainWindow>

class QSplitter;
class QTimer;

namespace ms {

class Studio;
class TopBar;
class AlertBar;
class SourceGrid;
class AudioMixer;
class ProgramMonitor;
class ControlPanel;
class GraphicsPanel;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(Studio *studio, QWidget *parent = nullptr);
	void showStartupNotes(const QStringList &notes);

protected:
	void closeEvent(QCloseEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;
	void showEvent(QShowEvent *e) override;

private:
	QWidget *panel(QWidget *content);
	void restoreLayout();
	void saveLayout();
	bool handleShortcut(QKeyEvent *e);
	void runAction(const QString &id);
	void applyCaptureExclusion();

	void addContent(int kind);
	void editContent(const ContentRef &ref);
	void removeContent(const ContentRef &ref);
	void renameContent(const ContentRef &ref);
	void editAudio(const QString &uid);
	void toggleRecording();
	void toggleStreaming();
	void switchProfile(const QString &name);
	void newProfile();
	void openSettings(int tab);
	void openRecordings();

	Studio *m_studio;
	TopBar *m_top;
	AlertBar *m_alerts;
	ProgramMonitor *m_monitor;
	ControlPanel *m_controls;
	GraphicsPanel *m_graphics;
	SourceGrid *m_grid;
	AudioMixer *m_mixer;
	QSplitter *m_vertical, *m_topRow, *m_rightCol, *m_bottomRow;
	QTimer *m_layoutTimer;
	bool m_closing = false;
};

} // namespace ms
