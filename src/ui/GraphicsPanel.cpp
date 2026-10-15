#include "GraphicsPanel.h"

#include "Icons.h"
#include "Studio.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace ms {

namespace {

const char *kUid = "uid";
const char *kType = "gtype"; // "lt" | "ovl"

void styleToggle(QPushButton *b, int state) // 0 off, 1 animating, 2 on air
{
	const QColor bg = state == 2 ? theme::program : state == 1 ? theme::transition : theme::bg3;
	const QColor fg = state == 1 ? QColor(0, 0, 0) : state == 2 ? QColor(255, 255, 255) : theme::text2;
	b->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;font-weight:700;font-size:8pt;border:1px solid %3;"
					"padding:2px 6px;min-width:62px;}")
				 .arg(theme::hex(bg), theme::hex(fg), theme::hex(state ? bg : theme::lineStrong)));
}

} // namespace

GraphicsPanel::GraphicsPanel(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	auto *header = new QWidget;
	header->setObjectName("PanelHeader");
	header->setFixedHeight(30);
	auto *h = new QHBoxLayout(header);
	h->setContentsMargins(10, 0, 6, 0);
	auto *title = new QLabel(QStringLiteral("GRÁFICOS"));
	title->setObjectName("PanelTitle");
	h->addWidget(title);
	h->addStretch(1);
	auto *clear = new QToolButton;
	clear->setText(QStringLiteral("Limpar tudo"));
	clear->setToolTip(QStringLiteral("Remove todos os gráficos do ar (Alt+0)"));
	clear->setFocusPolicy(Qt::NoFocus);
	connect(clear, &QToolButton::clicked, studio, &Studio::clearGraphics);
	h->addWidget(clear);
	auto *add = new QToolButton;
	add->setIcon(Icons::icon(Icon::Plus));
	add->setPopupMode(QToolButton::InstantPopup);
	add->setFocusPolicy(Qt::NoFocus);
	auto *menu = new QMenu(add);
	connect(menu->addAction(Icons::icon(Icon::Text), QStringLiteral("Identificação / lower third…")), &QAction::triggered,
		this, [this] { emit editLowerThird({}); });
	connect(menu->addAction(Icons::icon(Icon::Logo), QStringLiteral("Logotipo / imagem sobreposta…")), &QAction::triggered,
		this, [this] { emit editOverlay({}); });
	add->setMenu(menu);
	h->addWidget(add);
	v->addWidget(header);

	auto *scroll = new QScrollArea;
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	m_listHost = new QWidget;
	m_list = new QVBoxLayout(m_listHost);
	m_list->setContentsMargins(6, 6, 6, 6);
	m_list->setSpacing(4);
	scroll->setWidget(m_listHost);
	v->addWidget(scroll, 1);

	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, &GraphicsPanel::refreshStates);
	m_timer->start(100);
	connect(studio, &Studio::graphicsChanged, this, &GraphicsPanel::rebuild);
	connect(studio, &Studio::profileLoaded, this, &GraphicsPanel::rebuild);
	rebuild();
}

void GraphicsPanel::rebuild()
{
	while (QLayoutItem *it = m_list->takeAt(0)) {
		delete it->widget();
		delete it;
	}
	QVector<const LowerThirdConfig *> lts;
	for (const auto &l : m_studio->profile().lowerThirds)
		lts.append(&l);
	std::stable_sort(lts.begin(), lts.end(), [](auto *a, auto *b) { return a->order < b->order; });

	auto makeRow = [this](const QString &uid, const QString &type, int slot, const QString &line1, const QString &line2,
			      Icon icon) {
		auto *row = new QFrame;
		row->setObjectName("Panel");
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(6, 4, 4, 4);
		h->setSpacing(6);
		auto *num = new QLabel(slot > 0 && slot <= 9 ? QString::number(slot) : QString());
		num->setFixedWidth(14);
		num->setFont(theme::monoFont(8, true));
		num->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
		num->setToolTip(slot > 0 && slot <= 9 ? QStringLiteral("Atalho: Alt+%1").arg(slot) : QString());
		h->addWidget(num);
		auto *ic = new QLabel;
		ic->setPixmap(Icons::pixmap(icon, theme::text2, 14, devicePixelRatioF()));
		h->addWidget(ic);
		auto *text = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:%2;font-size:8pt'>%3</span>")
						.arg(line1.toHtmlEscaped(), theme::hex(theme::text2), line2.toHtmlEscaped()));
		text->setTextFormat(Qt::RichText);
		text->setMinimumWidth(40);
		h->addWidget(text, 1);
		auto *edit = new QToolButton;
		edit->setIcon(Icons::icon(Icon::Edit, theme::text2));
		edit->setProperty("flat", true);
		edit->setFocusPolicy(Qt::NoFocus);
		edit->setToolTip(QStringLiteral("Editar"));
		connect(edit, &QToolButton::clicked, this, [this, uid, type] {
			if (type == QLatin1String("lt"))
				emit editLowerThird(uid);
			else
				emit editOverlay(uid);
		});
		h->addWidget(edit);
		auto *toggle = new QPushButton(QStringLiteral("EXIBIR"));
		toggle->setFocusPolicy(Qt::NoFocus);
		toggle->setProperty(kUid, uid);
		toggle->setProperty(kType, type);
		styleToggle(toggle, 0);
		connect(toggle, &QPushButton::clicked, this, [this, uid, type] {
			if (type == QLatin1String("lt"))
				m_studio->toggleLowerThird(uid);
			else
				m_studio->toggleOverlay(uid);
			refreshStates();
		});
		h->addWidget(toggle);
		m_list->addWidget(row);
	};

	int slot = 0;
	for (auto *l : lts) {
		const QString title = l->name.isEmpty() ? l->templateName : l->name;
		QStringList sub;
		for (const QString &s : {l->role, l->company})
			if (!s.isEmpty())
				sub << s;
		if (sub.isEmpty() && !l->templateName.isEmpty())
			sub << l->templateName;
		QString when = l->holdSeconds > 0 ? QStringLiteral(" · %1 s").arg(l->holdSeconds) : QString();
		makeRow(l->uid, "lt", ++slot, title, sub.join(QStringLiteral(" · ")) + when, Icon::Text);
	}
	for (const auto &o : m_studio->profile().overlays)
		makeRow(o.uid, "ovl", 0, o.name, QStringLiteral("Imagem sobreposta"), Icon::Logo);

	if (lts.isEmpty() && m_studio->profile().overlays.isEmpty()) {
		auto *empty = new QLabel(QStringLiteral("Prepare as identificações dos participantes antes do programa: "
							"use + para criar lower thirds e logotipos."));
		empty->setWordWrap(true);
		empty->setStyleSheet(QStringLiteral("color:%1;padding:8px").arg(theme::hex(theme::text3)));
		m_list->addWidget(empty);
	}
	m_list->addStretch(1);
	refreshStates();
}

void GraphicsPanel::refreshStates()
{
	for (auto *b : m_listHost->findChildren<QPushButton *>()) {
		const QString uid = b->property(kUid).toString();
		if (uid.isEmpty())
			continue;
		int state = 0;
		if (b->property(kType).toString() == QLatin1String("lt")) {
			const GraphicPhase ph = m_studio->engine().lowerThirdPhase(uid);
			state = ph == GraphicPhase::Hold ? 2 : ph == GraphicPhase::Hidden ? 0 : 1;
			b->setText(ph == GraphicPhase::Hold ? QStringLiteral("NO AR")
				   : ph == GraphicPhase::In ? QStringLiteral("ENTRANDO")
				   : ph == GraphicPhase::Out ? QStringLiteral("SAINDO")
							     : QStringLiteral("EXIBIR"));
		} else {
			state = m_studio->engine().overlayVisible(uid) ? 2 : 0;
			b->setText(state ? QStringLiteral("NO AR") : QStringLiteral("EXIBIR"));
		}
		if (b->property("state").toInt() != state + 1) {
			b->setProperty("state", state + 1);
			styleToggle(b, state);
		}
	}
}

} // namespace ms
