// Graphics on-air panel: prepared identifications (lower thirds, bands,
// titles) and image overlays, each with a single clear on/off action.
#pragma once

#include <QWidget>

class QVBoxLayout;
class QTimer;

namespace ms {

class Studio;

class GraphicsPanel : public QWidget {
	Q_OBJECT
public:
	explicit GraphicsPanel(Studio *studio, QWidget *parent = nullptr);
	void rebuild();

signals:
	void editLowerThird(const QString &uid); // empty = new
	void editOverlay(const QString &uid);    // empty = new

private:
	void refreshStates();
	Studio *m_studio;
	QVBoxLayout *m_list;
	QWidget *m_listHost;
	QTimer *m_timer;
};

} // namespace ms
