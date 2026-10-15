#include "SourceGrid.h"

#include "Icons.h"
#include "Studio.h"
#include "Theme.h"

#include <obs.h>

#include <QApplication>
#include <QButtonGroup>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace ms {

namespace {

constexpr int kInfoBar = 24;
constexpr const char *kMime = "application/x-marshalls-content";

Icon iconForKind(SourceKind k)
{
	switch (k) {
	case SourceKind::Camera: return Icon::Camera;
	case SourceKind::Screen: return Icon::Screen;
	case SourceKind::Window: return Icon::Window;
	case SourceKind::Media: return Icon::Film;
	case SourceKind::Image: return Icon::Image;
	case SourceKind::Slideshow: return Icon::Slides;
	case SourceKind::Color: return Icon::Color;
	case SourceKind::AudioInput: return Icon::Mic;
	case SourceKind::AudioOutput: return Icon::Speaker;
	}
	return Icon::Camera;
}

QString fmtTime(qint64 ms)
{
	if (ms < 0)
		ms = 0;
	const qint64 s = ms / 1000;
	return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

// ---------------------------------------------------------------------------
// SourceCard
// ---------------------------------------------------------------------------

SourceCard::SourceCard(Studio *studio, const ContentRef &ref, QWidget *parent)
	: QWidget(parent), m_studio(studio), m_ref(ref)
{
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	setAcceptDrops(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::NoFocus);
}

QSize SourceCard::sizeHint() const
{
	return QSize(240, 240 * 9 / 16 + kInfoBar);
}

QRectF SourceCard::thumbRect() const
{
	const double w = width() - 6;
	return QRectF(3, 3, w, std::max(10.0, double(height()) - kInfoBar - 6));
}

void SourceCard::refreshThumbnail()
{
	const QString key = m_ref.type == ContentRef::Type::Scene ? QStringLiteral("scene:") + m_ref.uid : m_ref.uid;
	quint64 serial = 0;
	const QImage img = m_studio->engine().thumbnails().latest(key, &serial);
	if (serial != m_serial) {
		m_serial = serial;
		m_image = img;
		update();
	} else if (m_ref.type == ContentRef::Type::Source) {
		const SourceConfig *s = m_studio->profile().findSource(m_ref.uid);
		if (s && s->kind == SourceKind::Media)
			update(); // media time label
	}
}

void SourceCard::refreshState()
{
	update();
}

QVector<SourceCard::QuickButton> SourceCard::quickButtons() const
{
	QVector<QuickButton> v;
	if (!m_hover)
		return v;
	const QRectF t = thumbRect();
	const double s = 22, pad = 4;
	double x = t.right() - pad - s;
	const double y = t.top() + pad;
	v.append({Quick::Menu, QRectF(x, y, s, s)});
	if (m_ref.type == ContentRef::Type::Source) {
		const SourceConfig *cfg = m_studio->profile().findSource(m_ref.uid);
		if (cfg && cfg->kind == SourceKind::Media) {
			x -= s + 3;
			v.append({Quick::Restart, QRectF(x, y, s, s)});
			x -= s + 3;
			v.append({Quick::PlayPause, QRectF(x, y, s, s)});
		}
		const SourceHealth h = m_studio->engine().runtime(m_ref.uid).health;
		if (cfg && (cfg->kind == SourceKind::Camera || cfg->kind == SourceKind::Screen ||
			    cfg->kind == SourceKind::Window) &&
		    (h == SourceHealth::NoSignal || h == SourceHealth::Disconnected || h == SourceHealth::Error)) {
			x -= s + 3;
			v.append({Quick::Reconnect, QRectF(x, y, s, s)});
		}
	}
	return v;
}

void SourceCard::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, false);
	const auto &pc = m_studio->program();
	const bool onAir = pc.program() == m_ref;
	const bool pending = pc.isTransitioning() && pc.pendingTarget() == m_ref;
	const bool selected = pc.selected() == m_ref;

	p.fillRect(rect(), theme::bg0);

	// Tally border: program red > pending yellow > selected green.
	QColor border = m_hover ? theme::lineStrong : theme::line;
	int bw = 1;
	if (onAir) {
		border = theme::program;
		bw = 3;
	} else if (pending) {
		border = theme::transition;
		bw = 3;
	} else if (selected) {
		border = theme::selected;
		bw = 3;
	}
	if (m_dropHover) {
		border = theme::focus;
		bw = 3;
	}
	p.fillRect(rect(), border);
	p.fillRect(rect().adjusted(bw, bw, -bw, -bw), theme::bg1);
	if (onAir && selected) // both: inner green hairline
		p.fillRect(rect().adjusted(bw, bw, -bw, -bw), theme::selected),
			p.fillRect(rect().adjusted(bw + 1, bw + 1, -bw - 1, -bw - 1), theme::bg1);

	const QRectF t = thumbRect();
	p.fillRect(t, Qt::black);
	if (!m_image.isNull()) {
		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		QSizeF sz = m_image.size();
		sz.scale(t.size(), Qt::KeepAspectRatio);
		const QRectF dst(t.center().x() - sz.width() / 2, t.center().y() - sz.height() / 2, sz.width(), sz.height());
		p.drawImage(dst, m_image);
	}

	// Info about the content.
	QString name = m_studio->contentName(m_ref);
	Icon icon = Icon::Layers;
	QString kindText = QStringLiteral("COMPOSIÇÃO");
	SourceRuntime rt;
	const SourceConfig *cfg = nullptr;
	if (m_ref.type == ContentRef::Type::Source) {
		cfg = m_studio->profile().findSource(m_ref.uid);
		if (cfg) {
			icon = iconForKind(cfg->kind);
			kindText = kindLabel(cfg->kind).toUpper();
		}
		rt = m_studio->engine().runtime(m_ref.uid);
	}

	// Health overlay.
	if (cfg && rt.health != SourceHealth::Ok) {
		const bool bad = rt.health == SourceHealth::Error || rt.health == SourceHealth::Disconnected ||
				 rt.health == SourceHealth::Unsupported;
		const QColor c = bad ? theme::failure : rt.health == SourceHealth::Starting ? theme::text2 : theme::warning;
		p.fillRect(t, QColor(0, 0, 0, 170));
		if (bad) {
			p.save();
			p.setClipRect(t);
			QPen hatch(QColor(c.red(), c.green(), c.blue(), 40), 6);
			p.setPen(hatch);
			for (double x = t.left() - t.height(); x < t.right(); x += 18)
				p.drawLine(QPointF(x, t.bottom()), QPointF(x + t.height(), t.top()));
			p.restore();
		}
		const QRectF iconR(t.center().x() - 11, t.center().y() - 26, 22, 22);
		Icons::paint(p, rt.health == SourceHealth::Disconnected ? Icon::Unplugged : Icon::Warning, iconR, c);
		p.setPen(c);
		p.setFont(theme::uiFont(8, 700));
		p.drawText(QRectF(t.left() + 6, t.center().y(), t.width() - 12, 16), Qt::AlignCenter,
			   healthLabel(rt.health).toUpper());
		p.setPen(theme::text2);
		p.setFont(theme::uiFont(7));
		p.drawText(QRectF(t.left() + 8, t.center().y() + 16, t.width() - 16, 30),
			   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, rt.detail);
	}

	// Media progress.
	if (cfg && cfg->kind == SourceKind::Media && rt.mediaDurationMs > 0) {
		const double f = std::clamp(double(rt.mediaTimeMs) / rt.mediaDurationMs, 0.0, 1.0);
		p.fillRect(QRectF(t.left(), t.bottom() - 3, t.width(), 3), QColor(255, 255, 255, 40));
		p.fillRect(QRectF(t.left(), t.bottom() - 3, t.width() * f, 3), onAir ? theme::program : theme::text);
		const QString remain = QStringLiteral("-%1").arg(fmtTime(rt.mediaDurationMs - rt.mediaTimeMs));
		p.setFont(theme::monoFont(8, true));
		const QRectF tr(t.left() + 4, t.bottom() - 20, 60, 15);
		p.fillRect(tr.adjusted(-1, 0, 1, 0), QColor(0, 0, 0, 160));
		p.setPen(theme::text);
		p.drawText(tr, Qt::AlignCenter, remain);
	}

	// Tally badges.
	p.setFont(theme::uiFont(7, 800));
	if (onAir || pending || selected) {
		const QString label = onAir ? QStringLiteral("NO AR") : pending ? QStringLiteral("ENTRANDO") : QStringLiteral("PRÓXIMO");
		const QColor bg = onAir ? theme::program : pending ? theme::transition : theme::selected;
		const QFontMetrics fm(p.font());
		const QRectF badge(t.left(), t.top(), fm.horizontalAdvance(label) + 12, 16);
		p.fillRect(badge, bg);
		p.setPen(pending ? QColor(0, 0, 0) : QColor(255, 255, 255));
		p.drawText(badge, Qt::AlignCenter, label);
	}

	// Quick controls.
	p.setRenderHint(QPainter::Antialiasing, true);
	for (const auto &b : quickButtons()) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(12, 13, 15, 210));
		p.drawRoundedRect(b.rect, 2, 2);
		Icon ic = Icon::More;
		if (b.kind == Quick::PlayPause)
			ic = rt.mediaState == OBS_MEDIA_STATE_PLAYING ? Icon::Pause : Icon::Play;
		else if (b.kind == Quick::Restart)
			ic = Icon::Restart;
		else if (b.kind == Quick::Reconnect)
			ic = Icon::Refresh;
		Icons::paint(p, ic, b.rect.adjusted(4, 4, -4, -4), theme::text);
	}
	p.setRenderHint(QPainter::Antialiasing, false);

	// Info bar.
	const QRectF bar(bw, height() - kInfoBar - bw + 1, width() - 2 * bw, kInfoBar - 1);
	p.fillRect(bar, onAir ? QColor(0x3A, 0x12, 0x11) : theme::bg2);
	double x = bar.left() + 4;
	if (m_slot > 0 && m_slot <= 9) {
		const QRectF slotR(x, bar.top() + 4, 16, bar.height() - 8);
		p.fillRect(slotR, onAir ? theme::program : theme::bg4);
		p.setPen(theme::text);
		p.setFont(theme::monoFont(8, true));
		p.drawText(slotR, Qt::AlignCenter, QString::number(m_slot));
		x += 21;
	}
	p.setRenderHint(QPainter::Antialiasing, true);
	Icons::paint(p, icon, QRectF(x, bar.center().y() - 7, 14, 14), theme::text2);
	x += 19;
	// Status dot.
	QColor dot = theme::ok;
	if (cfg) {
		switch (rt.health) {
		case SourceHealth::Ok: dot = theme::ok; break;
		case SourceHealth::Starting: dot = theme::text3; break;
		case SourceHealth::NoSignal: dot = theme::warning; break;
		default: dot = theme::failure;
		}
	}
	p.setPen(Qt::NoPen);
	p.setBrush(dot);
	p.drawEllipse(QPointF(bar.right() - 9, bar.center().y()), 3.5, 3.5);
	p.setRenderHint(QPainter::Antialiasing, false);

	p.setFont(theme::uiFont(9, onAir ? 700 : 600));
	p.setPen(theme::text);
	const QFontMetrics fm(p.font());
	const double nameW = bar.right() - 20 - x;
	p.drawText(QRectF(x, bar.top(), nameW, bar.height()), Qt::AlignVCenter | Qt::AlignLeft,
		   fm.elidedText(name, Qt::ElideRight, int(nameW)));
	if (width() > 200) {
		p.setFont(theme::uiFont(7, 600));
		p.setPen(theme::text3);
		const QFontMetrics fk(p.font());
		const int kw = fk.horizontalAdvance(kindText);
		const int nw = fm.horizontalAdvance(name);
		if (x + nw + kw + 16 < bar.right() - 20)
			p.drawText(QRectF(bar.right() - 20 - kw, bar.top(), kw, bar.height()), Qt::AlignVCenter, kindText);
	}
}

void SourceCard::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::RightButton) {
		emit clicked(m_ref);
		emit contextMenuRequested(m_ref, e->globalPosition().toPoint());
		return;
	}
	if (e->button() != Qt::LeftButton)
		return;
	m_pressedQuick = Quick::None;
	for (const auto &b : quickButtons())
		if (b.rect.contains(e->position()))
			m_pressedQuick = b.kind;
	m_pressPos = e->position().toPoint();
	m_pressed = true;
	if (m_pressedQuick == Quick::None)
		emit clicked(m_ref);
}

void SourceCard::mouseMoveEvent(QMouseEvent *e)
{
	if (!m_pressed || m_pressedQuick != Quick::None) {
		update();
		return;
	}
	if ((e->position().toPoint() - m_pressPos).manhattanLength() < QApplication::startDragDistance() * 2)
		return;
	m_pressed = false;
	auto *drag = new QDrag(this);
	auto *mime = new QMimeData;
	mime->setData(kMime, m_ref.key().toUtf8());
	drag->setMimeData(mime);
	QPixmap pm = grab().scaledToWidth(std::min(width(), 180), Qt::SmoothTransformation);
	drag->setPixmap(pm);
	drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
	drag->exec(Qt::MoveAction);
}

void SourceCard::mouseReleaseEvent(QMouseEvent *e)
{
	if (!m_pressed)
		return;
	m_pressed = false;
	for (const auto &b : quickButtons()) {
		if (b.kind != m_pressedQuick || !b.rect.contains(e->position()))
			continue;
		switch (b.kind) {
		case Quick::PlayPause: m_studio->engine().mediaPlayPause(m_ref.uid); break;
		case Quick::Restart: m_studio->engine().mediaRestart(m_ref.uid); break;
		case Quick::Reconnect: m_studio->engine().reconnectSource(m_ref.uid); break;
		case Quick::Menu: emit contextMenuRequested(m_ref, e->globalPosition().toPoint()); break;
		case Quick::None: break;
		}
	}
	m_pressedQuick = Quick::None;
	update();
}

void SourceCard::mouseDoubleClickEvent(QMouseEvent *e)
{
	if (e->button() != Qt::LeftButton)
		return;
	for (const auto &b : quickButtons())
		if (b.rect.contains(e->position()))
			return;
	emit doubleClicked(m_ref);
}

void SourceCard::enterEvent(QEnterEvent *)
{
	m_hover = true;
	update();
}

void SourceCard::leaveEvent(QEvent *)
{
	m_hover = false;
	update();
}

void SourceCard::dragEnterEvent(QDragEnterEvent *e)
{
	if (e->mimeData()->hasFormat(kMime) && QString::fromUtf8(e->mimeData()->data(kMime)) != m_ref.key()) {
		m_dropHover = true;
		update();
		e->acceptProposedAction();
	}
}

void SourceCard::dragLeaveEvent(QDragLeaveEvent *)
{
	m_dropHover = false;
	update();
}

void SourceCard::dropEvent(QDropEvent *e)
{
	m_dropHover = false;
	update();
	const ContentRef moved = ContentRef::fromKey(QString::fromUtf8(e->mimeData()->data(kMime)));
	if (moved.isValid() && moved != m_ref) {
		e->acceptProposedAction();
		emit dropped(moved, m_ref);
	}
}

// ---------------------------------------------------------------------------
// SourceGrid
// ---------------------------------------------------------------------------

SourceGrid::SourceGrid(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	auto *header = new QWidget;
	header->setObjectName("PanelHeader");
	header->setFixedHeight(30);
	auto *h = new QHBoxLayout(header);
	h->setContentsMargins(10, 0, 6, 0);
	h->setSpacing(2);
	auto *title = new QLabel(QStringLiteral("FONTES"));
	title->setObjectName("PanelTitle");
	h->addWidget(title);
	h->addSpacing(12);

	m_filters = new QButtonGroup(this);
	m_filters->setExclusive(true);
	const QList<QPair<QString, QString>> filters = {{"all", QStringLiteral("Todas")},
							{"camera", QStringLiteral("Câmeras")},
							{"screen", QStringLiteral("Telas")},
							{"media", QStringLiteral("Mídia")},
							{"scene", QStringLiteral("Composições")}};
	for (const auto &f : filters) {
		auto *b = new QToolButton;
		b->setText(f.second);
		b->setCheckable(true);
		b->setProperty("filter", f.first);
		b->setStyleSheet(QStringLiteral("QToolButton{background:transparent;border:none;color:%1;padding:4px 8px;}"
						"QToolButton:checked{color:%2;border-bottom:2px solid %3;}"
						"QToolButton:hover{color:%2;}")
					 .arg(theme::hex(theme::text2), theme::hex(theme::text), theme::hex(theme::focus)));
		m_filters->addButton(b);
		h->addWidget(b);
		if (f.first == m_studio->profile().ui.gridFilter)
			b->setChecked(true);
	}
	if (!m_filters->checkedButton())
		m_filters->buttons().first()->setChecked(true);
	m_filter = m_filters->checkedButton()->property("filter").toString();
	connect(m_filters, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *b) {
		m_filter = b->property("filter").toString();
		UiSettings u = m_studio->profile().ui;
		u.gridFilter = m_filter;
		m_studio->setUiSettings(u);
		layoutCards();
	});
	h->addStretch(1);

	auto *colLabel = new QLabel(QStringLiteral("Colunas"));
	colLabel->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
	h->addWidget(colLabel);
	m_columns = new QSpinBox;
	m_columns->setRange(1, 12);
	m_columns->setValue(m_studio->profile().ui.gridColumns);
	m_columns->setFixedWidth(52);
	m_columns->setToolTip(QStringLiteral("Quantidade de colunas da grade (o tamanho das miniaturas acompanha)"));
	connect(m_columns, &QSpinBox::valueChanged, this, &SourceGrid::setColumns);
	h->addWidget(m_columns);
	h->addSpacing(6);

	auto *add = new QToolButton;
	add->setIcon(Icons::icon(Icon::Plus));
	add->setText(QStringLiteral("Adicionar"));
	add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	add->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(add);
	const QList<QPair<SourceKind, QString>> kinds = {
		{SourceKind::Camera, QStringLiteral("Câmera / dispositivo de captura")},
		{SourceKind::Screen, QStringLiteral("Tela inteira (monitor)")},
		{SourceKind::Window, QStringLiteral("Janela específica")},
		{SourceKind::Media, QStringLiteral("Vídeo / vinheta")},
		{SourceKind::Image, QStringLiteral("Imagem")},
		{SourceKind::Slideshow, QStringLiteral("Apresentação (sequência de imagens)")},
		{SourceKind::Color, QStringLiteral("Cor sólida")}};
	for (const auto &k : kinds) {
		static const QHash<int, Icon> icons = {{int(SourceKind::Camera), Icon::Camera}, {int(SourceKind::Screen), Icon::Screen},
						       {int(SourceKind::Window), Icon::Window}, {int(SourceKind::Media), Icon::Film},
						       {int(SourceKind::Image), Icon::Image}, {int(SourceKind::Slideshow), Icon::Slides},
						       {int(SourceKind::Color), Icon::Color}};
		QAction *a = menu->addAction(Icons::icon(icons.value(int(k.first))), k.second);
		connect(a, &QAction::triggered, this, [this, k] { emit addRequested(int(k.first)); });
	}
	menu->addSeparator();
	connect(menu->addAction(Icons::icon(Icon::Layers), QStringLiteral("Composição (várias fontes)…")),
		&QAction::triggered, this, [this] { emit addRequested(-1); });
	add->setMenu(menu);
	h->addWidget(add);
	v->addWidget(header);

	m_scroll = new QScrollArea;
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setWidgetResizable(false);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_container = new QWidget;
	m_container->setAutoFillBackground(true);
	QPalette pal = m_container->palette();
	pal.setColor(QPalette::Window, theme::bg0);
	m_container->setPalette(pal);
	m_scroll->setWidget(m_container);
	m_scroll->viewport()->installEventFilter(this);
	v->addWidget(m_scroll, 1);

	m_empty = new QLabel(m_container);
	m_empty->setAlignment(Qt::AlignCenter);
	m_empty->setWordWrap(true);
	m_empty->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));

	m_thumbTimer = new QTimer(this);
	connect(m_thumbTimer, &QTimer::timeout, this, &SourceGrid::tickThumbnails);
	m_thumbTimer->start(1000 / std::max(1, m_studio->profile().ui.thumbnailFps));

	connect(studio, &Studio::contentChanged, this, &SourceGrid::rebuild);
	connect(studio, &Studio::profileLoaded, this, [this] {
		m_columns->setValue(m_studio->profile().ui.gridColumns);
		rebuild();
	});
	connect(studio, &Studio::programChanged, this, &SourceGrid::updateStates);
	connect(studio, &Studio::selectionChanged, this, &SourceGrid::updateStates);
	connect(studio, &Studio::transitionStateChanged, this, &SourceGrid::updateStates);
	connect(studio, &Studio::sourceChanged, this, &SourceGrid::updateStates);
	connect(studio, &Studio::settingsChanged, this, [this] {
		m_thumbTimer->setInterval(1000 / std::max(1, m_studio->profile().ui.thumbnailFps));
	});
	rebuild();
}

void SourceGrid::setColumns(int c)
{
	UiSettings u = m_studio->profile().ui;
	if (u.gridColumns != c) {
		u.gridColumns = c;
		m_studio->setUiSettings(u);
	}
	layoutCards();
}

bool SourceGrid::matchesFilter(const ContentRef &ref) const
{
	if (m_filter == QLatin1String("all"))
		return true;
	if (ref.type == ContentRef::Type::Scene)
		return m_filter == QLatin1String("scene");
	const SourceConfig *s = m_studio->profile().findSource(ref.uid);
	if (!s)
		return false;
	if (m_filter == QLatin1String("camera"))
		return s->kind == SourceKind::Camera;
	if (m_filter == QLatin1String("screen"))
		return s->kind == SourceKind::Screen || s->kind == SourceKind::Window;
	if (m_filter == QLatin1String("media"))
		return s->kind == SourceKind::Media || s->kind == SourceKind::Image || s->kind == SourceKind::Slideshow ||
		       s->kind == SourceKind::Color;
	return false;
}

void SourceGrid::rebuild()
{
	qDeleteAll(m_cards);
	m_cards.clear();
	const auto order = m_studio->gridOrder();
	int slot = 0;
	for (const auto &ref : order) {
		auto *card = new SourceCard(m_studio, ref, m_container);
		card->setSlot(++slot);
		connect(card, &SourceCard::clicked, this, [this](const ContentRef &r) { m_studio->select(r); });
		connect(card, &SourceCard::doubleClicked, this, [this](const ContentRef &r) {
			const DoubleClickAction a = m_studio->profile().transition.doubleClick;
			if (a == DoubleClickAction::None)
				return;
			m_studio->select(r);
			m_studio->take(a == DoubleClickAction::Cut ? TakeMode::Cut : TakeMode::Auto);
		});
		connect(card, &SourceCard::contextMenuRequested, this, &SourceGrid::showContextMenu);
		connect(card, &SourceCard::dropped, this, [this](const ContentRef &moved, const ContentRef &target) {
			const int idx = m_studio->gridOrder().indexOf(target);
			m_studio->moveInGrid(moved, idx);
		});
		m_cards.append(card);
	}
	layoutCards();
}

void SourceGrid::layoutCards()
{
	const int cols = std::max(1, m_columns->value());
	const int gap = 6;
	const int avail = m_scroll->viewport()->width() - gap;
	const int cw = std::max(120, (avail - gap * cols) / cols);
	const int ch = cw * 9 / 16 + kInfoBar + 6;
	int i = 0;
	for (auto *c : m_cards) {
		const bool vis = matchesFilter(c->ref());
		c->setVisible(vis);
		if (!vis)
			continue;
		const int r = i / cols, col = i % cols;
		c->setGeometry(gap + col * (cw + gap), gap + r * (ch + gap), cw, ch);
		++i;
	}
	const int rows = (i + cols - 1) / cols;
	m_container->resize(m_scroll->viewport()->width(), std::max(m_scroll->viewport()->height(), gap + rows * (ch + gap)));
	m_empty->setVisible(i == 0);
	if (i == 0) {
		m_empty->setText(m_cards.isEmpty()
					 ? QStringLiteral("Nenhuma fonte configurada.\nUse “Adicionar” para incluir câmeras, a tela do "
							  "computador, vídeos e imagens.")
					 : QStringLiteral("Nenhuma fonte nesta categoria."));
		m_empty->setGeometry(0, 0, m_container->width(), std::min(m_container->height(), 160));
	}
	tickThumbnails();
}

void SourceGrid::updateStates()
{
	for (auto *c : m_cards)
		c->refreshState();
}

void SourceGrid::tickThumbnails()
{
	const QRect visible(QPoint(0, m_scroll->verticalScrollBar() ? m_scroll->verticalScrollBar()->value() : 0),
			    m_scroll->viewport()->size());
	for (auto *c : m_cards)
		if (c->isVisible() && c->geometry().intersects(visible))
			c->refreshThumbnail();
}

bool SourceGrid::eventFilter(QObject *o, QEvent *e)
{
	if (o == m_scroll->viewport() && e->type() == QEvent::Resize)
		QTimer::singleShot(0, this, &SourceGrid::layoutCards);
	return QWidget::eventFilter(o, e);
}

void SourceGrid::showContextMenu(const ContentRef &ref, const QPoint &pos)
{
	QMenu menu;
	const bool onAir = m_studio->program().program() == ref;
	QAction *cut = menu.addAction(Icons::icon(Icon::Cut), QStringLiteral("Colocar no ar — corte"));
	QAction *autoT = menu.addAction(Icons::icon(Icon::Auto), QStringLiteral("Colocar no ar — transição"));
	cut->setEnabled(!onAir);
	autoT->setEnabled(!onAir);
	menu.addSeparator();
	QAction *media = nullptr, *restart = nullptr, *reconnect = nullptr, *preview = nullptr;
	if (ref.type == ContentRef::Type::Source) {
		const SourceConfig *s = m_studio->profile().findSource(ref.uid);
		if (s && s->kind == SourceKind::Media) {
			media = menu.addAction(Icons::icon(Icon::Play), QStringLiteral("Reproduzir / pausar"));
			restart = menu.addAction(Icons::icon(Icon::Restart), QStringLiteral("Reiniciar"));
			menu.addSeparator();
		}
		if (s && (s->kind == SourceKind::Camera || s->kind == SourceKind::Screen || s->kind == SourceKind::Window))
			reconnect = menu.addAction(Icons::icon(Icon::Refresh), QStringLiteral("Reconectar dispositivo"));
		preview = menu.addAction(Icons::icon(Icon::EyeOff), QStringLiteral("Pausar miniatura (libera o dispositivo se fora do ar)"));
		preview->setCheckable(true);
		preview->setChecked(!m_studio->engine().isPreviewEnabled(ref.uid));
	}
	QAction *props = menu.addAction(Icons::icon(Icon::Gear),
					ref.type == ContentRef::Type::Scene ? QStringLiteral("Editar composição…")
									    : QStringLiteral("Propriedades…"));
	QAction *rename = menu.addAction(Icons::icon(Icon::Edit), QStringLiteral("Renomear…"));
	QAction *dup = nullptr;
	if (ref.type == ContentRef::Type::Scene)
		dup = menu.addAction(Icons::icon(Icon::Duplicate), QStringLiteral("Duplicar"));
	menu.addSeparator();
	QAction *remove = menu.addAction(Icons::icon(Icon::Trash, theme::failure),
					 ref.type == ContentRef::Type::Scene ? QStringLiteral("Excluir composição…")
									     : QStringLiteral("Remover fonte…"));
	QAction *chosen = menu.exec(pos);
	if (!chosen)
		return;
	if (chosen == cut)
		m_studio->takeContent(ref, TakeMode::Cut);
	else if (chosen == autoT)
		m_studio->takeContent(ref, TakeMode::Auto);
	else if (chosen == media)
		m_studio->engine().mediaPlayPause(ref.uid);
	else if (chosen == restart)
		m_studio->engine().mediaRestart(ref.uid);
	else if (chosen == reconnect)
		m_studio->engine().reconnectSource(ref.uid);
	else if (chosen == preview)
		m_studio->engine().setPreviewEnabled(ref.uid, !preview->isChecked());
	else if (chosen == props)
		emit propertiesRequested(ref);
	else if (chosen == rename)
		emit renameRequested(ref);
	else if (chosen == dup)
		emit duplicateRequested(ref);
	else if (chosen == remove)
		emit removeRequested(ref);
}

} // namespace ms
