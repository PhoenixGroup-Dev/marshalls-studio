// Dialogs to add and configure sources and audio channels.
#pragma once

#include "StudioConfig.h"

#include <QDialog>

struct obs_source;
typedef struct obs_source obs_source_t;
struct obs_data;
typedef struct obs_data obs_data_t;
struct obs_properties;
typedef struct obs_properties obs_properties_t;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace ms {

class Studio;

// Device picker bound to the background device enumeration.
class DeviceCombo : public QWidget {
	Q_OBJECT
public:
	DeviceCombo(SourceKind kind, QWidget *parent = nullptr);
	void setCurrent(const DeviceRef &d);
	DeviceRef current() const;
	void reload();

signals:
	void changed();

private:
	SourceKind m_kind;
	QComboBox *m_combo;
	QLabel *m_note;
	DeviceRef m_pending;
};

class AddSourceDialog : public QDialog {
	Q_OBJECT
public:
	AddSourceDialog(Studio *studio, SourceKind kind, QWidget *parent = nullptr);
	SourceConfig result() const;

protected:
	void accept() override;

private:
	Studio *m_studio;
	SourceKind m_kind;
	QLineEdit *m_name;
	DeviceCombo *m_device = nullptr;
	QLineEdit *m_path = nullptr;
	QListWidget *m_files = nullptr;
	QPushButton *m_color = nullptr;
	QColor m_colorValue{0x20, 0x24, 0x2A};
	QCheckBox *m_loop = nullptr;
	QCheckBox *m_restart = nullptr;
	QComboBox *m_endAction = nullptr;
	QCheckBox *m_cursor = nullptr;
	QLabel *m_error;
	SourceConfig m_result;
};

// Generic editor for every libobs property of a live source (resolution,
// frame rate, pixel format, capture method…), excluding the device itself.
class ObsPropertiesDialog : public QDialog {
	Q_OBJECT
public:
	ObsPropertiesDialog(obs_source_t *source, const QStringList &hidden, QWidget *parent = nullptr);
	~ObsPropertiesDialog() override;
	QJsonObject userSettings() const;

private:
	void rebuild();
	obs_source_t *m_source;
	obs_data_t *m_settings;
	obs_properties_t *m_props = nullptr;
	QStringList m_hidden;
	QFormLayout *m_form;
	QWidget *m_host;
};

class SourcePropertiesDialog : public QDialog {
	Q_OBJECT
public:
	SourcePropertiesDialog(Studio *studio, const QString &uid, QWidget *parent = nullptr);

protected:
	void accept() override;

private:
	Studio *m_studio;
	SourceConfig m_cfg;
	QLineEdit *m_name;
	DeviceCombo *m_device = nullptr;
	QLineEdit *m_path = nullptr;
	QSpinBox *m_videoDelay = nullptr;
	QCheckBox *m_hasAudio = nullptr;
	QCheckBox *m_loop = nullptr;
	QCheckBox *m_restart = nullptr;
	QComboBox *m_endAction = nullptr;
	QJsonObject m_engineSettings;
};

class AudioChannelDialog : public QDialog {
	Q_OBJECT
public:
	AudioChannelDialog(Studio *studio, const QString &uid, QWidget *parent = nullptr);

protected:
	void accept() override;
	void reject() override;

private:
	AudioFilters filters() const;
	void applyLive();
	Studio *m_studio;
	SourceConfig m_original;
	QString m_uid;
	QLineEdit *m_name;
	DeviceCombo *m_device = nullptr;
	QSpinBox *m_sync;
	QCheckBox *m_toProgram, *m_isolated;
	QComboBox *m_monitor;
	QDoubleSpinBox *m_gain;
	QCheckBox *m_gateOn, *m_suppressOn, *m_compOn, *m_limOn, *m_eqOn;
	QDoubleSpinBox *m_gateOpen, *m_gateClose, *m_compRatio, *m_compThreshold, *m_compGain, *m_limThreshold;
	QSpinBox *m_suppressLevel, *m_compAttack, *m_compRelease;
	QDoubleSpinBox *m_eqLow, *m_eqMid, *m_eqHigh;
};

QString colorButtonStyle(const QColor &c);

} // namespace ms
