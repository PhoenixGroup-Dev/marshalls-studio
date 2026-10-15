#include "SceneEditor.h"

#include "Icons.h"
#include "Planning.h"
#include "Studio.h"
#include "Theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace ms {

// ---------------------------------------------------------------------------
// SceneCanvas
// ---------------------------------------------------------------------------

SceneCanvas::SceneCanvas(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
	setMinimumSize(480, 270);
	setMouseTracking(true);
	QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Expanding);
	sp.setHeightForWidth(true);
	setSizePolicy(sp);
	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, &SceneCanvas::tick);
	m_timer->start(66);
}

void SceneCanvas::setScene(const SceneConfig &cfg)
{
	m_cfg = cfg;
	if (m_selected >= m_cfg.items.size())
		m_selected = int(m_cfg.items.size()) - 1;
	update();
}

void SceneCanvas::setSelected(int index)
{
	m_selected = index;
	update();
}

void SceneCanvas::tick()
{
	quint64 serial = 0;
	const QImage img = m_studio->engine().thumbnails().latest(QStringLiteral("scene:") + m_cfg.uid, &serial);
	if (serial != m_serial) {
		m_serial = serial;
		m_image = img;
		update();
	}
}

QRectF SceneCanvas::canvasRect() const
{
	QSizeF s(16, 9);
	s.scale(QSizeF(width() - 2, height() - 2), Qt::KeepAspectRatio);
	return QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
}

QRectF SceneCanvas::toWidget(const QRectF &n) const
{
	const QRectF c = canvasRect();
	return QRectF(c.left() + n.x() * c.width(), c.top() + n.y() * c.height(), n.width() * c.width(),
		      n.height() * c.height());
}

QPointF SceneCanvas::toNorm(const QPointF &w) const
{
	const QRectF c = canvasRect();
	return QPointF((w.x() - c.left()) / c.width(), (w.y() - c.top()) / c.height());
}

void SceneCanvas::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), theme::bg0);
	const QRectF c = canvasRect();
	p.fillRect(c, Qt::black);
	if (!m_image.isNull())
		p.drawImage(c, m_image);
	// Safe areas (action 93%, title 90%).
	QPen safe(QColor(255, 255, 255, 40), 1, Qt::DashLine);
	p.setPen(safe);
	p.setBrush(Qt::NoBrush);
	p.drawRect(c.adjusted(c.width() * 0.035, c.height() * 0.035, -c.width() * 0.035, -c.height() * 0.035));
	p.drawRect(c.adjusted(c.width() * 0.05, c.height() * 0.05, -c.width() * 0.05, -c.height() * 0.05));
	p.setRenderHint(QPainter::Antialiasing);
	for (int i = 0; i < m_cfg.items.size(); ++i) {
		const auto &it = m_cfg.items[i];
		const QRectF r = toWidget(it.rect);
		const bool sel = i == m_selected;
		p.setPen(QPen(sel ? theme::focus : QColor(255, 255, 255, 90), sel ? 2 : 1, it.visible ? Qt::SolidLine : Qt::DotLine));
		p.setBrush(Qt::NoBrush);
		p.drawRect(r);
		const SourceConfig *s = m_studio->profile().findSource(it.sourceUid);
		const QString label = QStringLiteral("%1. %2").arg(i + 1).arg(s ? s->name : QStringLiteral("?"));
		p.setFont(theme::uiFont(8, 700));
		const QRectF lr(r.left() + 1, r.top() + 1, QFontMetrics(p.font()).horizontalAdvance(label) + 10, 16);
		p.fillRect(lr, sel ? theme::focus : QColor(0, 0, 0, 170));
		p.setPen(Qt::white);
		p.drawText(lr, Qt::AlignCenter, label);
		if (sel) {
			p.fillRect(QRectF(r.right() - 5, r.bottom() - 5, 10, 10), theme::focus);
		}
	}
}

void SceneCanvas::mousePressEvent(QMouseEvent *e)
{
	const QPointF pos = e->position();
	if (m_selected >= 0 && m_selected < m_cfg.items.size()) {
		const QRectF r = toWidget(m_cfg.items[m_selected].rect);
		if (QRectF(r.right() - 8, r.bottom() - 8, 16, 16).contains(pos)) {
			m_drag = Drag::Resize;
			m_dragStart = pos;
			m_startRect = m_cfg.items[m_selected].rect;
			return;
		}
	}
	for (int i = int(m_cfg.items.size()) - 1; i >= 0; --i) { // topmost first
		if (toWidget(m_cfg.items[i].rect).contains(pos)) {
			m_selected = i;
			emit itemSelected(i);
			m_drag = Drag::Move;
			m_dragStart = pos;
			m_startRect = m_cfg.items[i].rect;
			update();
			return;
		}
	}
}

void SceneCanvas::mouseMoveEvent(QMouseEvent *e)
{
	if (m_drag == Drag::None || m_selected < 0)
		return;
	const QPointF d = toNorm(e->position()) - toNorm(m_dragStart);
	QRectF r = m_startRect;
	auto snap = [](double v) {
		for (double s : {0.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0})
			if (std::abs(v - s) < 0.012)
				return s;
		return v;
	};
	if (m_drag == Drag::Move) {
		r.moveTo(snap(m_startRect.x() + d.x()), snap(m_startRect.y() + d.y()));
		if (std::abs(r.right() - snap(r.right())) > 1e-9)
			r.moveRight(snap(r.right()));
		if (std::abs(r.bottom() - snap(r.bottom())) > 1e-9)
			r.moveBottom(snap(r.bottom()));
	} else {
		double w = std::max(0.03, m_startRect.width() + d.x());
		double h = std::max(0.03, m_startRect.height() + d.y());
		if (e->modifiers() & Qt::ShiftModifier) // keep the original aspect
			h = w * m_startRect.height() / m_startRect.width();
		r.setWidth(snap(r.x() + w) - r.x());
		r.setHeight(snap(r.y() + h) - r.y());
	}
	m_cfg.items[m_selected].rect = r;
	emit rectChanged(m_selected, r);
	update();
}

void SceneCanvas::mouseReleaseEvent(QMouseEvent *)
{
	m_drag = Drag::None;
}

// ---------------------------------------------------------------------------
// SceneEditorDialog
// ---------------------------------------------------------------------------

SceneEditorDialog::SceneEditorDialog(Studio *studio, const QString &uid, QWidget *parent)
	: QDialog(parent), m_studio(studio)
{
	m_original = *studio->profile().findScene(uid);
	m_cfg = m_original;
	setWindowTitle(QStringLiteral("Composição — %1").arg(m_cfg.name));
	resize(1100, 640);

	m_pushTimer = new QTimer(this);
	m_pushTimer->setSingleShot(true);
	m_pushTimer->setInterval(60);
	connect(m_pushTimer, &QTimer::timeout, this, [this] { m_studio->updateScene(m_cfg); });

	auto *root = new QHBoxLayout(this);

	// Left: items.
	auto *left = new QVBoxLayout;
	auto *nameForm = new QFormLayout;
	m_name = new QLineEdit(m_cfg.name);
	nameForm->addRow(QStringLiteral("Nome"), m_name);
	left->addLayout(nameForm);
	auto *itemsLabel = new QLabel(QStringLiteral("CAMADAS (de baixo para cima)"));
	itemsLabel->setObjectName("PanelTitle");
	left->addWidget(itemsLabel);
	m_items = new QListWidget;
	m_items->setMinimumWidth(220);
	left->addWidget(m_items, 1);
	auto *itemBtns = new QHBoxLayout;
	auto *add = new QToolButton;
	add->setIcon(Icons::icon(Icon::Plus));
	add->setToolTip(QStringLiteral("Adicionar fonte"));
	add->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(add);
	for (const auto &s : studio->profile().sources) {
		if (!kindHasVideo(s.kind))
			continue;
		const QString suid = s.uid;
		connect(menu->addAction(s.name), &QAction::triggered, this, [this, suid] {
			SceneItemConfig it;
			it.sourceUid = suid;
			m_cfg.items.append(it);
			rebuildList();
			m_items->setCurrentRow(int(m_cfg.items.size()) - 1);
			push();
		});
	}
	add->setMenu(menu);
	auto *remove = new QToolButton;
	remove->setIcon(Icons::icon(Icon::Trash, theme::text2));
	remove->setToolTip(QStringLiteral("Remover da composição (a fonte continua na grade)"));
	auto *up = new QToolButton;
	up->setText(QStringLiteral("Subir"));
	auto *down = new QToolButton;
	down->setText(QStringLiteral("Descer"));
	itemBtns->addWidget(add);
	itemBtns->addWidget(remove);
	itemBtns->addStretch(1);
	itemBtns->addWidget(down);
	itemBtns->addWidget(up);
	left->addLayout(itemBtns);
	root->addLayout(left);

	// Center: canvas.
	auto *center = new QVBoxLayout;
	m_onAir = new QLabel;
	m_onAir->setStyleSheet(QStringLiteral("color:%1;font-weight:700").arg(theme::hex(theme::program)));
	center->addWidget(m_onAir);
	m_canvas = new SceneCanvas(studio);
	center->addWidget(m_canvas, 1);
	auto *tip = new QLabel(QStringLiteral("Arraste para mover; puxe o canto para redimensionar (Shift mantém a proporção). "
					      "Linhas tracejadas: áreas de segurança de ação e de título."));
	tip->setWordWrap(true);
	tip->setStyleSheet(QStringLiteral("color:%1;font-size:8pt").arg(theme::hex(theme::text3)));
	center->addWidget(tip);
	root->addLayout(center, 1);

	// Right: presets + fields.
	auto *right = new QVBoxLayout;
	auto *presets = new QGroupBox(QStringLiteral("Layouts prontos"));
	auto *pv = new QVBoxLayout(presets);
	for (const auto &lp : layoutPresets()) {
		auto *b = new QPushButton(lp.label);
		b->setToolTip(QStringLiteral("Usa as %1 primeiras camadas").arg(lp.minItems));
		const QString id = lp.id;
		connect(b, &QPushButton::clicked, this, [this, id] { applyPreset(id); });
		pv->addWidget(b);
	}
	right->addWidget(presets);
	auto *geo = new QGroupBox(QStringLiteral("Camada selecionada"));
	auto *gf = new QFormLayout(geo);
	auto mk = [](double lo, double hi) {
		auto *s = new QDoubleSpinBox;
		s->setRange(lo, hi);
		s->setDecimals(1);
		s->setSingleStep(0.5);
		s->setSuffix(QStringLiteral(" %"));
		return s;
	};
	m_x = mk(-100, 200);
	m_y = mk(-100, 200);
	m_w = mk(1, 300);
	m_h = mk(1, 300);
	gf->addRow(QStringLiteral("X"), m_x);
	gf->addRow(QStringLiteral("Y"), m_y);
	gf->addRow(QStringLiteral("Largura"), m_w);
	gf->addRow(QStringLiteral("Altura"), m_h);
	m_fill = new QCheckBox(QStringLiteral("Preencher (corta o excedente)"));
	m_visible = new QCheckBox(QStringLiteral("Visível"));
	gf->addRow(QString(), m_fill);
	gf->addRow(QString(), m_visible);
	right->addWidget(geo);
	right->addStretch(1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	right->addWidget(buttons);
	root->addLayout(right);

	connect(m_items, &QListWidget::currentRowChanged, this, [this](int row) {
		m_canvas->setSelected(row);
		loadItemFields();
	});
	connect(m_canvas, &SceneCanvas::itemSelected, this, [this](int i) { m_items->setCurrentRow(i); });
	connect(m_canvas, &SceneCanvas::rectChanged, this, [this](int i, const QRectF &r) {
		m_cfg.items[i].rect = r;
		loadItemFields();
		push();
	});
	auto fieldsChanged = [this] {
		if (m_loading)
			return;
		const int i = m_items->currentRow();
		if (i < 0 || i >= m_cfg.items.size())
			return;
		m_cfg.items[i].rect = QRectF(m_x->value() / 100, m_y->value() / 100, m_w->value() / 100, m_h->value() / 100);
		m_cfg.items[i].fill = m_fill->isChecked();
		m_cfg.items[i].visible = m_visible->isChecked();
		m_canvas->setScene(m_cfg);
		push();
	};
	for (auto *s : {m_x, m_y, m_w, m_h})
		connect(s, &QDoubleSpinBox::valueChanged, this, fieldsChanged);
	connect(m_fill, &QCheckBox::toggled, this, fieldsChanged);
	connect(m_visible, &QCheckBox::toggled, this, fieldsChanged);
	connect(remove, &QToolButton::clicked, this, [this] {
		const int i = m_items->currentRow();
		if (i < 0)
			return;
		m_cfg.items.removeAt(i);
		rebuildList();
		push();
	});
	auto move = [this](int delta) {
		const int i = m_items->currentRow();
		const int j = i + delta;
		if (i < 0 || j < 0 || j >= m_cfg.items.size())
			return;
		m_cfg.items.swapItemsAt(i, j);
		rebuildList();
		m_items->setCurrentRow(j);
		push();
	};
	connect(up, &QToolButton::clicked, this, [move] { move(1); });
	connect(down, &QToolButton::clicked, this, [move] { move(-1); });

	m_canvas->setScene(m_cfg);
	rebuildList();
	if (!m_cfg.items.isEmpty())
		m_items->setCurrentRow(0);
	m_onAir->setText(studio->program().program() == ContentRef::scene(uid)
				 ? QStringLiteral("NO AR — alterações aparecem imediatamente na saída")
				 : QString());
}

void SceneEditorDialog::rebuildList()
{
	const int keep = m_items->currentRow();
	m_items->blockSignals(true);
	m_items->clear();
	for (int i = 0; i < m_cfg.items.size(); ++i) {
		const SourceConfig *s = m_studio->profile().findSource(m_cfg.items[i].sourceUid);
		m_items->addItem(QStringLiteral("%1. %2").arg(i + 1).arg(s ? s->name : QStringLiteral("(removida)")));
	}
	m_items->blockSignals(false);
	m_items->setCurrentRow(std::min(keep, int(m_cfg.items.size()) - 1));
	m_canvas->setScene(m_cfg);
	loadItemFields();
}

void SceneEditorDialog::loadItemFields()
{
	const int i = m_items->currentRow();
	const bool has = i >= 0 && i < m_cfg.items.size();
	for (QWidget *w : std::initializer_list<QWidget *>{m_x, m_y, m_w, m_h, m_fill, m_visible})
		w->setEnabled(has);
	if (!has)
		return;
	m_loading = true;
	const auto &it = m_cfg.items[i];
	m_x->setValue(it.rect.x() * 100);
	m_y->setValue(it.rect.y() * 100);
	m_w->setValue(it.rect.width() * 100);
	m_h->setValue(it.rect.height() * 100);
	m_fill->setChecked(it.fill);
	m_visible->setChecked(it.visible);
	m_loading = false;
}

void SceneEditorDialog::push()
{
	m_canvas->setScene(m_cfg);
	m_pushTimer->start();
}

void SceneEditorDialog::applyPreset(const QString &id)
{
	const auto rects = layoutRects(id, int(m_cfg.items.size()));
	for (int i = 0; i < m_cfg.items.size() && i < rects.size(); ++i) {
		m_cfg.items[i].rect = rects[i];
		m_cfg.items[i].fill = layoutUsesFill(id) || (id.startsWith("pip") && i == 0);
	}
	// PiP layouts: camera above the screen.
	loadItemFields();
	push();
}

void SceneEditorDialog::accept()
{
	m_pushTimer->stop();
	m_studio->updateScene(m_cfg);
	m_studio->renameScene(m_cfg.uid, m_name->text());
	QDialog::accept();
}

void SceneEditorDialog::reject()
{
	m_pushTimer->stop();
	m_studio->updateScene(m_original);
	QDialog::reject();
}

} // namespace ms
