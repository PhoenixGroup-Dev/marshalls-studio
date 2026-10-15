#include "Icons.h"

#include <algorithm>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

namespace ms {

namespace {

QPainterPath roundRect(double x, double y, double w, double h, double r)
{
	QPainterPath p;
	p.addRoundedRect(QRectF(x, y, w, h), r, r);
	return p;
}

} // namespace

void Icons::paint(QPainter &p, Icon icon, const QRectF &r, const QColor &color)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.translate(r.topLeft());
	p.scale(r.width() / 24.0, r.height() / 24.0);
	QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	auto fill = [&](const QPainterPath &path) {
		p.save();
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawPath(path);
		p.restore();
	};

	switch (icon) {
	case Icon::Camera:
		p.drawPath(roundRect(2.5, 6.5, 13, 11, 1.5));
		{
			QPainterPath lens;
			lens.moveTo(15.5, 10.5);
			lens.lineTo(21.5, 7);
			lens.lineTo(21.5, 17);
			lens.lineTo(15.5, 13.5);
			p.drawPath(lens);
		}
		break;
	case Icon::Screen:
		p.drawPath(roundRect(2.5, 4, 19, 13, 1.2));
		p.drawLine(QPointF(9, 20.5), QPointF(15, 20.5));
		p.drawLine(QPointF(12, 17), QPointF(12, 20.5));
		break;
	case Icon::Window:
		p.drawPath(roundRect(2.5, 4, 19, 16, 1.2));
		p.drawLine(QPointF(2.5, 8.5), QPointF(21.5, 8.5));
		fill([] { QPainterPath d; d.addEllipse(QPointF(5.5, 6.3), 0.9, 0.9); d.addEllipse(QPointF(8.3, 6.3), 0.9, 0.9); return d; }());
		break;
	case Icon::Film:
		p.drawPath(roundRect(3, 3.5, 18, 17, 1.2));
		p.drawLine(QPointF(7.5, 3.5), QPointF(7.5, 20.5));
		p.drawLine(QPointF(16.5, 3.5), QPointF(16.5, 20.5));
		p.drawLine(QPointF(3, 12), QPointF(21, 12));
		break;
	case Icon::Image:
		p.drawPath(roundRect(3, 4, 18, 16, 1.2));
		{
			QPainterPath m;
			m.moveTo(3.5, 18);
			m.lineTo(9, 12);
			m.lineTo(13, 16);
			m.lineTo(16, 13);
			m.lineTo(20.5, 17.5);
			p.drawPath(m);
		}
		p.drawEllipse(QPointF(15.5, 8.5), 1.5, 1.5);
		break;
	case Icon::Slides:
		p.drawPath(roundRect(3, 3.5, 18, 12.5, 1));
		p.drawLine(QPointF(12, 16), QPointF(12, 19));
		p.drawLine(QPointF(8, 21), QPointF(12, 19));
		p.drawLine(QPointF(16, 21), QPointF(12, 19));
		p.drawLine(QPointF(7, 12.5), QPointF(10, 9));
		p.drawLine(QPointF(10, 9), QPointF(12.5, 11));
		p.drawLine(QPointF(12.5, 11), QPointF(17, 7));
		break;
	case Icon::Color:
		fill(roundRect(4, 4, 16, 16, 2));
		break;
	case Icon::Mic:
	case Icon::MicOff:
		p.drawPath(roundRect(9, 2.5, 6, 11.5, 3));
		{
			QPainterPath arc;
			arc.moveTo(5.5, 10.5);
			arc.cubicTo(5.5, 19, 18.5, 19, 18.5, 10.5);
			p.drawPath(arc);
		}
		p.drawLine(QPointF(12, 17.5), QPointF(12, 21));
		p.drawLine(QPointF(8.5, 21), QPointF(15.5, 21));
		if (icon == Icon::MicOff) {
			QPen thick(color, 2.0, Qt::SolidLine, Qt::RoundCap);
			p.setPen(thick);
			p.drawLine(QPointF(3.5, 3.5), QPointF(20.5, 20.5));
		}
		break;
	case Icon::Speaker:
		{
			QPainterPath s;
			s.moveTo(3.5, 9);
			s.lineTo(7.5, 9);
			s.lineTo(12.5, 4.5);
			s.lineTo(12.5, 19.5);
			s.lineTo(7.5, 15);
			s.lineTo(3.5, 15);
			s.closeSubpath();
			p.drawPath(s);
			QPainterPath w;
			w.moveTo(16, 8.5);
			w.cubicTo(18.5, 10.5, 18.5, 13.5, 16, 15.5);
			w.moveTo(18.5, 5.5);
			w.cubicTo(23, 9.5, 23, 14.5, 18.5, 18.5);
			p.drawPath(w);
		}
		break;
	case Icon::Headphones:
		{
			QPainterPath h;
			h.moveTo(4, 15);
			h.lineTo(4, 12);
			h.cubicTo(4, 1.5, 20, 1.5, 20, 12);
			h.lineTo(20, 15);
			p.drawPath(h);
			p.drawPath(roundRect(3.5, 13.5, 4.5, 7, 1.2));
			p.drawPath(roundRect(16, 13.5, 4.5, 7, 1.2));
		}
		break;
	case Icon::Layers:
		{
			QPainterPath a;
			a.moveTo(12, 3);
			a.lineTo(21, 8);
			a.lineTo(12, 13);
			a.lineTo(3, 8);
			a.closeSubpath();
			p.drawPath(a);
			QPainterPath b;
			b.moveTo(3, 12);
			b.lineTo(12, 17);
			b.lineTo(21, 12);
			b.moveTo(3, 16);
			b.lineTo(12, 21);
			b.lineTo(21, 16);
			p.drawPath(b);
		}
		break;
	case Icon::Plus:
		p.drawLine(QPointF(12, 5), QPointF(12, 19));
		p.drawLine(QPointF(5, 12), QPointF(19, 12));
		break;
	case Icon::Gear:
		{
			p.drawEllipse(QPointF(12, 12), 3, 3);
			for (int i = 0; i < 8; ++i) {
				p.save();
				p.translate(12, 12);
				p.rotate(i * 45);
				p.drawLine(QPointF(0, -6.5), QPointF(0, -9.5));
				p.restore();
			}
			p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
		}
		break;
	case Icon::Record:
		fill([] { QPainterPath d; d.addEllipse(QPointF(12, 12), 6.5, 6.5); return d; }());
		break;
	case Icon::Stop:
		fill(roundRect(6, 6, 12, 12, 1.5));
		break;
	case Icon::Broadcast:
		fill([] { QPainterPath d; d.addEllipse(QPointF(12, 12), 2.2, 2.2); return d; }());
		{
			QPainterPath w;
			w.moveTo(8, 8);
			w.cubicTo(5.8, 10.2, 5.8, 13.8, 8, 16);
			w.moveTo(16, 8);
			w.cubicTo(18.2, 10.2, 18.2, 13.8, 16, 16);
			w.moveTo(5, 5);
			w.cubicTo(1, 9, 1, 15, 5, 19);
			w.moveTo(19, 5);
			w.cubicTo(23, 9, 23, 15, 19, 19);
			p.drawPath(w);
		}
		break;
	case Icon::Play:
		{
			QPainterPath t;
			t.moveTo(7.5, 5);
			t.lineTo(19, 12);
			t.lineTo(7.5, 19);
			t.closeSubpath();
			fill(t);
		}
		break;
	case Icon::Pause:
		fill(roundRect(6.5, 5, 4, 14, 0.8));
		fill(roundRect(13.5, 5, 4, 14, 0.8));
		break;
	case Icon::Restart:
		{
			QPainterPath a;
			a.arcMoveTo(QRectF(5, 5, 14, 14), 120);
			a.arcTo(QRectF(5, 5, 14, 14), 120, -300);
			p.drawPath(a);
			QPainterPath h;
			h.moveTo(4.2, 6);
			h.lineTo(8.5, 6.9);
			h.lineTo(7.2, 10.8);
			p.drawPath(h);
		}
		break;
	case Icon::Cut:
		p.drawLine(QPointF(4, 6), QPointF(11, 6));
		p.drawLine(QPointF(13, 18), QPointF(20, 18));
		p.drawLine(QPointF(12, 3.5), QPointF(12, 20.5));
		break;
	case Icon::Auto:
		{
			QLinearGradient g(4, 0, 20, 0);
			QColor c0 = color;
			c0.setAlpha(40);
			g.setColorAt(0, c0);
			g.setColorAt(1, color);
			p.save();
			p.setPen(Qt::NoPen);
			p.setBrush(g);
			p.drawPath(roundRect(4, 6, 16, 12, 1));
			p.restore();
		}
		break;
	case Icon::FadeBlack:
		p.drawPath(roundRect(3.5, 5.5, 17, 13, 1));
		fill([] { QPainterPath d; d.moveTo(12, 5.5); d.lineTo(20.5, 5.5); d.lineTo(20.5, 18.5); d.lineTo(12, 18.5); d.closeSubpath(); return d; }());
		break;
	case Icon::Text:
		fill(roundRect(3, 14, 3, 6, 0.4));
		p.drawLine(QPointF(8.5, 15), QPointF(20.5, 15));
		p.drawLine(QPointF(8.5, 19), QPointF(16, 19));
		p.drawLine(QPointF(3, 4.5), QPointF(13, 4.5));
		p.drawLine(QPointF(8, 4.5), QPointF(8, 11));
		break;
	case Icon::Eye:
	case Icon::EyeOff:
		{
			QPainterPath e;
			e.moveTo(2.5, 12);
			e.cubicTo(6, 5.5, 18, 5.5, 21.5, 12);
			e.cubicTo(18, 18.5, 6, 18.5, 2.5, 12);
			p.drawPath(e);
			p.drawEllipse(QPointF(12, 12), 2.8, 2.8);
			if (icon == Icon::EyeOff)
				p.drawLine(QPointF(4, 4), QPointF(20, 20));
		}
		break;
	case Icon::Trash:
		p.drawLine(QPointF(4, 6.5), QPointF(20, 6.5));
		p.drawLine(QPointF(9.5, 6.5), QPointF(10, 3.5));
		p.drawLine(QPointF(10, 3.5), QPointF(14, 3.5));
		p.drawLine(QPointF(14, 3.5), QPointF(14.5, 6.5));
		{
			QPainterPath b;
			b.moveTo(6, 6.5);
			b.lineTo(7, 20.5);
			b.lineTo(17, 20.5);
			b.lineTo(18, 6.5);
			p.drawPath(b);
		}
		break;
	case Icon::Edit:
		{
			QPainterPath pen2;
			pen2.moveTo(4, 20);
			pen2.lineTo(4.8, 16);
			pen2.lineTo(16, 4.8);
			pen2.lineTo(19.2, 8);
			pen2.lineTo(8, 19.2);
			pen2.closeSubpath();
			p.drawPath(pen2);
		}
		break;
	case Icon::Refresh:
		{
			QPainterPath a;
			a.arcMoveTo(QRectF(4.5, 4.5, 15, 15), 30);
			a.arcTo(QRectF(4.5, 4.5, 15, 15), 30, 280);
			p.drawPath(a);
			QPainterPath h;
			h.moveTo(19.8, 4.5);
			h.lineTo(19, 9);
			h.lineTo(14.6, 8);
			p.drawPath(h);
		}
		break;
	case Icon::Warning:
		{
			QPainterPath t;
			t.moveTo(12, 3.5);
			t.lineTo(21.5, 20);
			t.lineTo(2.5, 20);
			t.closeSubpath();
			p.drawPath(t);
			p.drawLine(QPointF(12, 9.5), QPointF(12, 14));
			fill([] { QPainterPath d; d.addEllipse(QPointF(12, 17), 1.1, 1.1); return d; }());
		}
		break;
	case Icon::Unplugged:
		p.drawPath(roundRect(4, 8, 7, 8, 1));
		p.drawLine(QPointF(1.5, 12), QPointF(4, 12));
		p.drawLine(QPointF(13, 8), QPointF(13, 16));
		p.drawLine(QPointF(13, 12), QPointF(22.5, 12));
		p.drawLine(QPointF(15.5, 9.5), QPointF(15.5, 14.5));
		break;
	case Icon::Folder:
		{
			QPainterPath f;
			f.moveTo(3, 19);
			f.lineTo(3, 5.5);
			f.lineTo(9, 5.5);
			f.lineTo(11, 8);
			f.lineTo(21, 8);
			f.lineTo(21, 19);
			f.closeSubpath();
			p.drawPath(f);
		}
		break;
	case Icon::Keyboard:
		p.drawPath(roundRect(2.5, 6, 19, 12, 1.2));
		for (int i = 0; i < 5; ++i)
			p.drawPoint(QPointF(6 + i * 3, 10));
		for (int i = 0; i < 5; ++i)
			p.drawPoint(QPointF(6 + i * 3, 12.8));
		p.drawLine(QPointF(8, 15.3), QPointF(16, 15.3));
		break;
	case Icon::Sliders:
		for (int i = 0; i < 3; ++i) {
			const double x = 6 + i * 6;
			p.drawLine(QPointF(x, 4), QPointF(x, 20));
			const double y = i == 0 ? 15 : i == 1 ? 8 : 13;
			fill([&] { QPainterPath d; d.addRoundedRect(QRectF(x - 2.5, y - 1.5, 5, 3), 0.6, 0.6); return d; }());
		}
		break;
	case Icon::Clock:
		p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
		p.drawLine(QPointF(12, 7), QPointF(12, 12));
		p.drawLine(QPointF(12, 12), QPointF(15.5, 14));
		break;
	case Icon::Logo:
		p.drawPath(roundRect(3, 3, 18, 18, 1.5));
		fill(roundRect(13, 6, 5, 5, 0.6));
		break;
	case Icon::Grid:
		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 2; ++j)
				p.drawPath(roundRect(3.5 + i * 9.5, 3.5 + j * 9.5, 7.5, 7.5, 1));
		break;
	case Icon::Duplicate:
		p.drawPath(roundRect(8, 8, 12.5, 12.5, 1.2));
		{
			QPainterPath b;
			b.moveTo(16, 8);
			b.lineTo(16, 4.5);
			b.lineTo(3.5, 4.5);
			b.lineTo(3.5, 16);
			b.lineTo(8, 16);
			p.drawPath(b);
		}
		break;
	case Icon::More:
		for (int i = 0; i < 3; ++i)
			fill([&] { QPainterPath d; d.addEllipse(QPointF(6 + i * 6, 12), 1.5, 1.5); return d; }());
		break;
	}
	p.restore();
}

QPixmap Icons::pixmap(Icon i, const QColor &color, int size, qreal dpr)
{
	QPixmap pm(int(size * dpr), int(size * dpr));
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	paint(p, i, QRectF(0, 0, size, size), color);
	return pm;
}

QIcon Icons::icon(Icon i, const QColor &color, int size)
{
	const qreal dpr = qGuiApp && qGuiApp->primaryScreen() ? qGuiApp->primaryScreen()->devicePixelRatio() : 1.0;
	QIcon ic;
	ic.addPixmap(pixmap(i, color, size, std::max<qreal>(1.0, dpr)), QIcon::Normal);
	QColor dis = color;
	dis.setAlpha(90);
	ic.addPixmap(pixmap(i, dis, size, std::max<qreal>(1.0, dpr)), QIcon::Disabled);
	return ic;
}

} // namespace ms
