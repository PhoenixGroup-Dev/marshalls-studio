// Editors for lower thirds (with an exact preview — the same rasterizer feeds
// the program output) and for image overlays.
#pragma once

#include "StudioConfig.h"

#include <QDialog>
#include <QImage>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace ms {

class Studio;

// Paints an image scaled to fit (keeps aspect) at whatever size the layout gives.
class ImagePreview : public QWidget {
	Q_OBJECT
public:
	using QWidget::QWidget;
	void setImage(const QImage &img)
	{
		m_image = img;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QImage m_image;
};

class LowerThirdDialog : public QDialog {
	Q_OBJECT
public:
	LowerThirdDialog(Studio *studio, const QString &uid, QWidget *parent = nullptr);

protected:
	void accept() override;

private:
	LowerThirdConfig current() const;
	void updatePreview();
	Studio *m_studio;
	LowerThirdConfig m_cfg;
	bool m_isNew;
	QLineEdit *m_template, *m_name, *m_role, *m_company, *m_secondary, *m_logo;
	QComboBox *m_style, *m_position, *m_animIn, *m_animOut;
	QSpinBox *m_animMs;
	QDoubleSpinBox *m_hold, *m_scale;
	QFontComboBox *m_font;
	QPushButton *m_accent, *m_plate, *m_text, *m_sub;
	QColor m_accentC, m_plateC, m_textC, m_subC;
	ImagePreview *m_preview;
};

class OverlayDialog : public QDialog {
	Q_OBJECT
public:
	OverlayDialog(Studio *studio, const QString &uid, QWidget *parent = nullptr);

protected:
	void accept() override;

private:
	Studio *m_studio;
	OverlayImageConfig m_cfg;
	bool m_isNew;
	QLineEdit *m_name, *m_path;
	QDoubleSpinBox *m_x, *m_y, *m_w, *m_h;
	QSpinBox *m_fade;
	QLabel *m_error;
};

} // namespace ms
