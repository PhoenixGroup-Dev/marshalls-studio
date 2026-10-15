// Settings: video, recording, streaming, audio, shortcuts, interface and
// diagnostics. Changes are validated before being applied.
#pragma once

#include "StudioConfig.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QPlainTextEdit;

namespace ms {

class Studio;

class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	enum Tab { Video, Recording, Streaming, Audio, Shortcuts, Interface, Diagnostics };
	SettingsDialog(Studio *studio, int tab = 0, QWidget *parent = nullptr);

protected:
	void accept() override;

private:
	QWidget *buildVideo();
	QWidget *buildRecording();
	QWidget *buildStreaming();
	QWidget *buildAudio();
	QWidget *buildShortcuts();
	QWidget *buildInterface();
	QWidget *buildDiagnostics();
	void updateDiskEstimate();
	void validateStream();
	void refreshShortcutConflicts();
	StreamSettings streamFromUi() const;
	RecordingSettings recordingFromUi() const;

	Studio *m_studio;
	QTabWidget *m_tabs;
	// video
	QComboBox *m_base, *m_output, *m_fps;
	QLabel *m_videoNote;
	// recording
	QLineEdit *m_recFolder, *m_recPattern;
	QComboBox *m_recContainer, *m_recEncoder, *m_recRc, *m_recPreset;
	QSpinBox *m_recBitrate, *m_recCrf, *m_recAudio, *m_recMinFree;
	QCheckBox *m_recRemux, *m_recIsolated, *m_recShare;
	QLabel *m_diskLabel;
	// streaming
	QComboBox *m_platform, *m_server, *m_streamEncoder;
	QLineEdit *m_key;
	QSpinBox *m_streamBitrate, *m_streamAudio, *m_keyint, *m_delay, *m_retries, *m_retryDelay;
	QCheckBox *m_delayPreserve;
	QLabel *m_streamIssues;
	// audio
	QComboBox *m_monitorDevice;
	// shortcuts
	QTableWidget *m_shortcutTable;
	QLabel *m_shortcutStatus;
	// interface
	QSpinBox *m_thumbWidth, *m_thumbFps, *m_columns;
	QCheckBox *m_hideCapture;
};

} // namespace ms
