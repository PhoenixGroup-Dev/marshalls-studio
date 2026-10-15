// Program monitor: the exact composited output (libobs main texture) drawn
// by the GPU into a native child window, plus a header that states what is on
// air, the pending transition and the output state.
#pragma once

#include <QWidget>

struct obs_display;
typedef struct obs_display obs_display_t;

class QLabel;
class QProgressBar;

namespace ms {

class Studio;

class ObsDisplay : public QWidget {
	Q_OBJECT
public:
	explicit ObsDisplay(QWidget *parent = nullptr);
	~ObsDisplay() override;
	QPaintEngine *paintEngine() const override { return nullptr; }
	bool hasDisplay() const { return m_display != nullptr; }

protected:
	void showEvent(QShowEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *) override;
	bool event(QEvent *e) override;

private:
	void createDisplay();
	void destroyDisplay();
	static void draw(void *data, uint32_t cx, uint32_t cy);
	obs_display_t *m_display = nullptr;
};

class ProgramMonitor : public QWidget {
	Q_OBJECT
public:
	explicit ProgramMonitor(Studio *studio, QWidget *parent = nullptr);
	void refresh();

private:
	void tick();
	Studio *m_studio;
	ObsDisplay *m_display;
	QWidget *m_frame;
	QLabel *m_onAir;
	QLabel *m_state;
	QLabel *m_format;
	QProgressBar *m_progress;
};

} // namespace ms
