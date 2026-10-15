#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace ms::theme {

QString hex(const QColor &c)
{
	return c.name(QColor::HexRgb);
}

QFont monoFont(int pointSize, bool bold)
{
	QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef _WIN32
	f.setFamily(QStringLiteral("Consolas"));
#endif
	f.setPointSize(pointSize);
	f.setBold(bold);
	f.setStyleHint(QFont::Monospace);
	return f;
}

QFont uiFont(int pointSize, int weight)
{
	QFont f = QApplication::font();
	f.setPointSize(pointSize);
	f.setWeight(QFont::Weight(weight));
	return f;
}

void apply(QApplication &app)
{
	app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	QFont base = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef _WIN32
	base.setFamily(QStringLiteral("Segoe UI"));
#endif
	base.setPointSize(9);
	app.setFont(base);

	QPalette p;
	p.setColor(QPalette::Window, bg1);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, bg2);
	p.setColor(QPalette::AlternateBase, bg1);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::PlaceholderText, text3);
	p.setColor(QPalette::Button, bg3);
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::Highlight, focus);
	p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
	p.setColor(QPalette::ToolTipBase, bg3);
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::Light, bg4);
	p.setColor(QPalette::Midlight, bg3);
	p.setColor(QPalette::Mid, line);
	p.setColor(QPalette::Dark, bg0);
	p.setColor(QPalette::Shadow, QColor(0, 0, 0));
	p.setColor(QPalette::Link, focus);
	p.setColor(QPalette::Disabled, QPalette::Text, text3);
	p.setColor(QPalette::Disabled, QPalette::ButtonText, text3);
	p.setColor(QPalette::Disabled, QPalette::WindowText, text3);
	app.setPalette(p);

	const QString qss = QStringLiteral(R"(
QMainWindow, QDialog { background: %1; }
QWidget { color: %8; }
QToolTip { background: %4; color: %8; border: 1px solid %6; padding: 4px 6px; }

QSplitter::handle { background: %1; }
QSplitter::handle:hover { background: %6; }

QFrame#Panel { background: %2; border: 1px solid %5; }
QLabel#PanelTitle { color: %9; font-size: 8pt; font-weight: 600; letter-spacing: 1px; }
QWidget#PanelHeader { background: %3; border-bottom: 1px solid %5; }

QPushButton, QToolButton {
  background: %4; border: 1px solid %6; border-radius: 2px; padding: 4px 10px; color: %8;
}
QPushButton:hover, QToolButton:hover { background: %7; }
QPushButton:pressed, QToolButton:pressed { background: %3; }
QPushButton:disabled, QToolButton:disabled { color: %10; border-color: %5; }
QPushButton:focus, QToolButton:focus { border-color: %11; }
QToolButton[flat="true"] { background: transparent; border: none; padding: 2px; }
QToolButton[flat="true"]:hover { background: %7; }
QToolButton::menu-indicator { image: none; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QKeySequenceEdit {
  background: %3; border: 1px solid %6; border-radius: 2px; padding: 3px 6px; selection-background-color: %11;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %11; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: %3; border: 1px solid %6; selection-background-color: %7; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 14px; }

QTabWidget::pane { border: 1px solid %5; background: %2; top: -1px; }
QTabBar::tab { background: transparent; color: %9; padding: 5px 12px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %8; border-bottom: 2px solid %11; }
QTabBar::tab:hover { color: %8; }

QScrollBar:vertical { background: %2; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %6; min-height: 24px; border-radius: 2px; margin: 2px; }
QScrollBar:horizontal { background: %2; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: %6; min-width: 24px; border-radius: 2px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QListWidget, QTreeWidget, QTableWidget, QTableView, QListView {
  background: %2; border: 1px solid %5; alternate-background-color: %3; outline: none;
}
QListWidget::item:selected, QTreeWidget::item:selected, QTableWidget::item:selected { background: %7; color: %8; }
QHeaderView::section { background: %3; color: %9; border: none; border-right: 1px solid %5; padding: 4px 6px; }

QMenu { background: %3; border: 1px solid %6; padding: 4px 0; }
QMenu::item { padding: 5px 18px; }
QMenu::item:selected { background: %7; }
QMenu::item:disabled { color: %10; }
QMenu::separator { height: 1px; background: %5; margin: 4px 0; }
QMenuBar { background: %1; }

QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }
QGroupBox { border: 1px solid %5; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; color: %9; }
QSlider::groove:horizontal { height: 4px; background: %6; }
QSlider::handle:horizontal { width: 10px; margin: -6px 0; background: %8; border-radius: 1px; }
QProgressBar { background: %3; border: 1px solid %5; text-align: center; height: 6px; }
QProgressBar::chunk { background: %12; }
)")
				  .arg(hex(bg0), hex(bg1), hex(bg2), hex(bg3), hex(line), hex(lineStrong), hex(bg4), hex(text),
				       hex(text2), hex(text3), hex(focus), hex(transition));
	app.setStyleSheet(qss);
}

} // namespace ms::theme
