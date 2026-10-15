// Composition editor: arrange several sources on the canvas.
//
// Edits are applied live to the engine scene (atomically), so the operator
// sees the real render in the canvas; Cancel restores the original layout.
#pragma once

#include "StudioConfig.h"

#include <QDialog>
#include <QImage>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace ms {

class Studio;

class SceneCanvas : public QWidget {
	Q_OBJECT
public:
	SceneCanvas(Studio *studio, QWidget *parent = nullptr);
	void setScene(const SceneConfig &cfg);
	void setSelected(int index);
	int selected() const { return m_selected; }

signals:
	void itemSelected(int index);
	void rectChanged(int index, const QRectF &rect);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	QSize sizeHint() const override { return {640, 360}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override { return w * 9 / 16; }

private:
	QRectF canvasRect() const;
	QRectF toWidget(const QRectF &n) const;
	QPointF toNorm(const QPointF &w) const;
	void tick();

	Studio *m_studio;
	SceneConfig m_cfg;
	int m_selected = -1;
	QImage m_image;
	quint64 m_serial = 0;
	enum class Drag { None, Move, Resize } m_drag = Drag::None;
	QPointF m_dragStart;
	QRectF m_startRect;
	QTimer *m_timer;
};

class SceneEditorDialog : public QDialog {
	Q_OBJECT
public:
	SceneEditorDialog(Studio *studio, const QString &uid, QWidget *parent = nullptr);

protected:
	void accept() override;
	void reject() override;

private:
	void rebuildList();
	void loadItemFields();
	void push(); // apply m_cfg to the engine (debounced)
	void applyPreset(const QString &id);

	Studio *m_studio;
	SceneConfig m_original;
	SceneConfig m_cfg;
	QLineEdit *m_name;
	QListWidget *m_items;
	SceneCanvas *m_canvas;
	QDoubleSpinBox *m_x, *m_y, *m_w, *m_h;
	QCheckBox *m_fill, *m_visible;
	QLabel *m_onAir;
	QTimer *m_pushTimer;
	bool m_loading = false;
};

} // namespace ms
