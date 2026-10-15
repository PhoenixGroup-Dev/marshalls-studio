// Vector icon set drawn with QPainter on a 24-unit grid (1.6 unit stroke).
// No bitmap or emoji assets: icons stay crisp at any DPI and share one style.
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace ms {

enum class Icon {
	Camera, Screen, Window, Film, Image, Slides, Color, Mic, MicOff, Speaker, Headphones,
	Layers, Plus, Gear, Record, Stop, Broadcast, Play, Pause, Restart, Cut, Auto, FadeBlack,
	Text, Eye, EyeOff, Trash, Edit, Refresh, Warning, Unplugged, Folder, Keyboard, Sliders,
	Clock, Logo, Grid, Duplicate, More,
};

class Icons {
public:
	static QIcon icon(Icon i, const QColor &color = QColor(0xE4, 0xE7, 0xEB), int size = 16);
	static QPixmap pixmap(Icon i, const QColor &color, int size, qreal dpr = 1.0);
	static void paint(class QPainter &p, Icon i, const QRectF &r, const QColor &color);
};

} // namespace ms
