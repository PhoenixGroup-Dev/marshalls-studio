// Permanent source grid: live thumbnails of every source and composition.
//
// One click selects (green), the explicit take actions put the selection on
// air (red). Double click can be configured to take directly. Cards show the
// slot number used by the keyboard shortcuts, connection health and quick
// controls appropriate to the source type.
#pragma once

#include "ProgramController.h"

#include <QImage>
#include <QPointer>
#include <QScrollArea>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QSpinBox;
class QToolButton;
class QTimer;

namespace ms {

class Studio;

class SourceCard : public QWidget {
	Q_OBJECT
public:
	SourceCard(Studio *studio, const ContentRef &ref, QWidget *parent = nullptr);
	const ContentRef &ref() const { return m_ref; }
	void setSlot(int slot) { m_slot = slot; }
	void refreshThumbnail();
	void refreshState();
	QSize sizeHint() const override;

signals:
	void clicked(const ContentRef &ref);
	void doubleClicked(const ContentRef &ref);
	void contextMenuRequested(const ContentRef &ref, const QPoint &globalPos);
	void dropped(const ContentRef &moved, const ContentRef &target);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEvent(QEnterEvent *) override;
	void leaveEvent(QEvent *) override;
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *) override;
	void dropEvent(QDropEvent *e) override;

private:
	enum class Quick { None, PlayPause, Restart, Reconnect, Menu };
	struct QuickButton {
		Quick kind;
		QRectF rect;
	};
	QVector<QuickButton> quickButtons() const;
	QRectF thumbRect() const;

	Studio *m_studio;
	ContentRef m_ref;
	int m_slot = 0;
	QImage m_image;
	quint64 m_serial = 0;
	bool m_hover = false;
	bool m_dropHover = false;
	QPoint m_pressPos;
	bool m_pressed = false;
	Quick m_pressedQuick = Quick::None;
};

class SourceGrid : public QWidget {
	Q_OBJECT
public:
	explicit SourceGrid(Studio *studio, QWidget *parent = nullptr);
	void rebuild();
	void setColumns(int c);

signals:
	void addRequested(int kind); // SourceKind or -1 for composition
	void propertiesRequested(const ContentRef &ref);
	void removeRequested(const ContentRef &ref);
	void renameRequested(const ContentRef &ref);
	void duplicateRequested(const ContentRef &ref);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void doRebuild();
	void layoutCards();
	void updateStates();
	void tickThumbnails();
	bool matchesFilter(const ContentRef &ref) const;
	void showContextMenu(const ContentRef &ref, const QPoint &pos);

	Studio *m_studio;
	QScrollArea *m_scroll;
	QWidget *m_container;
	QVector<SourceCard *> m_cards;
	QButtonGroup *m_filters;
	QSpinBox *m_columns;
	QLabel *m_empty;
	QTimer *m_thumbTimer;
	QString m_filter = QStringLiteral("all");
	bool m_rebuildPending = false;
};

} // namespace ms
