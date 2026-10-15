// Switcher controls: what is next, what is on air, and the take actions.
// The label of each button states its exact effect for the current selection.
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace ms {

class Studio;

class ControlPanel : public QWidget {
	Q_OBJECT
public:
	explicit ControlPanel(Studio *studio, QWidget *parent = nullptr);
	void refresh();

private:
	void applyTransitionUi();
	Studio *m_studio;
	QLabel *m_next;
	QLabel *m_onAir;
	QLabel *m_effect;
	QPushButton *m_cut;
	QPushButton *m_auto;
	QPushButton *m_ftb;
	QComboBox *m_type;
	QSpinBox *m_duration;
	QComboBox *m_doubleClick;
	bool m_updating = false;
};

} // namespace ms
