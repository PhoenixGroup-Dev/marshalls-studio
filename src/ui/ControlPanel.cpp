#include "ControlPanel.h"

#include "Icons.h"
#include "Studio.h"
#include "Theme.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ms {

namespace {

QString chip(const QColor &bg, const QString &label, const QColor &fg = QColor(255, 255, 255))
{
	return QStringLiteral("<span style='background:%1;color:%2;font-weight:700;font-size:7pt'>&nbsp;%3&nbsp;</span>")
		.arg(theme::hex(bg), theme::hex(fg), label);
}

QPushButton *bigButton(const QString &text, const QString &shortcut, Icon icon, const QColor &accent)
{
	auto *b = new QPushButton(Icons::icon(icon, accent, 18), text);
	b->setMinimumHeight(44);
	b->setToolTip(shortcut);
	b->setFocusPolicy(Qt::NoFocus);
	b->setStyleSheet(QStringLiteral("QPushButton{font-weight:700;font-size:10pt;text-align:center;background:%1;"
					"border:1px solid %2;border-bottom:2px solid %3;}"
					"QPushButton:hover{background:%4;}QPushButton:disabled{color:%5;border-bottom-color:%2;}")
				 .arg(theme::hex(theme::bg3), theme::hex(theme::lineStrong), theme::hex(accent),
				      theme::hex(theme::bg4), theme::hex(theme::text3)));
	return b;
}

} // namespace

ControlPanel::ControlPanel(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	auto *header = new QWidget;
	header->setObjectName("PanelHeader");
	header->setFixedHeight(30);
	auto *h = new QHBoxLayout(header);
	h->setContentsMargins(10, 0, 8, 0);
	auto *title = new QLabel(QStringLiteral("CORTE E TRANSIÇÃO"));
	title->setObjectName("PanelTitle");
	h->addWidget(title);
	h->addStretch(1);
	v->addWidget(header);

	auto *body = new QWidget;
	auto *g = new QGridLayout(body);
	g->setContentsMargins(10, 10, 10, 10);
	g->setHorizontalSpacing(8);
	g->setVerticalSpacing(8);

	m_next = new QLabel;
	m_next->setTextFormat(Qt::RichText);
	m_onAir = new QLabel;
	m_onAir->setTextFormat(Qt::RichText);
	g->addWidget(m_next, 0, 0, 1, 2);
	g->addWidget(m_onAir, 1, 0, 1, 2);

	m_cut = bigButton(QStringLiteral("CORTE"), QStringLiteral("Corte seco para a fonte selecionada (Enter)"), Icon::Cut,
			  theme::program);
	m_auto = bigButton(QStringLiteral("AUTO"), QStringLiteral("Transição para a fonte selecionada (Espaço)"), Icon::Auto,
			   theme::transition);
	g->addWidget(m_cut, 2, 0);
	g->addWidget(m_auto, 2, 1);
	m_effect = new QLabel;
	m_effect->setWordWrap(true);
	m_effect->setStyleSheet(QStringLiteral("color:%1;font-size:8pt").arg(theme::hex(theme::text2)));
	g->addWidget(m_effect, 3, 0, 1, 2);

	auto *trRow = new QHBoxLayout;
	m_type = new QComboBox;
	m_type->addItem(QStringLiteral("Dissolve (fade)"), int(TransitionType::Fade));
	m_type->addItem(QStringLiteral("Deslizar"), int(TransitionType::Slide));
	m_type->addItem(QStringLiteral("Varrer (swipe)"), int(TransitionType::Swipe));
	m_type->addItem(QStringLiteral("Corte"), int(TransitionType::Cut));
	m_type->setFocusPolicy(Qt::NoFocus);
	m_duration = new QSpinBox;
	m_duration->setRange(50, 10000);
	m_duration->setSingleStep(100);
	m_duration->setSuffix(QStringLiteral(" ms"));
	m_duration->setFixedWidth(90);
	trRow->addWidget(m_type, 1);
	trRow->addWidget(m_duration);
	g->addLayout(trRow, 4, 0, 1, 2);

	m_ftb = bigButton(QStringLiteral("FADE PARA PRETO"), QStringLiteral("Escurece toda a saída e volta (B)"),
			  Icon::FadeBlack, theme::text2);
	m_ftb->setMinimumHeight(32);
	m_ftb->setCheckable(true);
	g->addWidget(m_ftb, 5, 0, 1, 2);

	auto *dblRow = new QHBoxLayout;
	auto *dblLabel = new QLabel(QStringLiteral("Duplo clique na grade"));
	dblLabel->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text2)));
	m_doubleClick = new QComboBox;
	m_doubleClick->addItem(QStringLiteral("Apenas seleciona"), int(DoubleClickAction::None));
	m_doubleClick->addItem(QStringLiteral("Corte"), int(DoubleClickAction::Cut));
	m_doubleClick->addItem(QStringLiteral("Transição"), int(DoubleClickAction::Auto));
	m_doubleClick->setFocusPolicy(Qt::NoFocus);
	dblRow->addWidget(dblLabel);
	dblRow->addWidget(m_doubleClick, 1);
	g->addLayout(dblRow, 6, 0, 1, 2);
	g->setRowStretch(7, 1);
	v->addWidget(body, 1);

	connect(m_cut, &QPushButton::clicked, this, [this] { m_studio->take(TakeMode::Cut); });
	connect(m_auto, &QPushButton::clicked, this, [this] { m_studio->take(TakeMode::Auto); });
	connect(m_ftb, &QPushButton::clicked, this, [this] {
		m_studio->toggleFadeToBlack();
		refresh();
	});
	auto save = [this] {
		if (m_updating)
			return;
		TransitionSettings t = m_studio->profile().transition;
		t.type = TransitionType(m_type->currentData().toInt());
		t.durationMs = m_duration->value();
		t.doubleClick = DoubleClickAction(m_doubleClick->currentData().toInt());
		m_studio->setTransition(t);
	};
	connect(m_type, &QComboBox::currentIndexChanged, this, save);
	connect(m_duration, &QSpinBox::valueChanged, this, save);
	connect(m_doubleClick, &QComboBox::currentIndexChanged, this, save);

	connect(studio, &Studio::programChanged, this, &ControlPanel::refresh);
	connect(studio, &Studio::selectionChanged, this, &ControlPanel::refresh);
	connect(studio, &Studio::transitionStateChanged, this, &ControlPanel::refresh);
	connect(studio, &Studio::contentChanged, this, &ControlPanel::refresh);
	connect(studio, &Studio::profileLoaded, this, &ControlPanel::refresh);
	connect(studio, &Studio::ftbChanged, this, &ControlPanel::refresh);
	connect(studio, &Studio::settingsChanged, this, &ControlPanel::applyTransitionUi);
	applyTransitionUi();
	refresh();
}

void ControlPanel::applyTransitionUi()
{
	m_updating = true;
	const TransitionSettings &t = m_studio->profile().transition;
	m_type->setCurrentIndex(std::max(0, m_type->findData(int(t.type))));
	m_duration->setValue(t.durationMs);
	m_duration->setEnabled(t.type != TransitionType::Cut);
	m_doubleClick->setCurrentIndex(std::max(0, m_doubleClick->findData(int(t.doubleClick))));
	m_updating = false;
	refresh();
}

void ControlPanel::refresh()
{
	const auto &pc = m_studio->program();
	const QString selName = m_studio->contentName(pc.selected());
	const QString progName = m_studio->contentName(pc.program());
	m_next->setText(chip(theme::selected, QStringLiteral("PRÓXIMO")) + QStringLiteral("&nbsp;&nbsp;") +
			(selName.isEmpty() ? QStringLiteral("<span style='color:%1'>clique em uma fonte</span>").arg(theme::hex(theme::text3))
					   : QStringLiteral("<b>%1</b>").arg(selName.toHtmlEscaped())));
	m_onAir->setText(chip(theme::program, QStringLiteral("NO AR")) + QStringLiteral("&nbsp;&nbsp;") +
			 (progName.isEmpty() ? QStringLiteral("<span style='color:%1'>preto</span>").arg(theme::hex(theme::text3))
					     : QStringLiteral("<b>%1</b>").arg(progName.toHtmlEscaped())));

	const bool busy = pc.isBusy();
	const bool canTake = pc.selected().isValid() && pc.selected() != pc.program() && !busy;
	m_cut->setEnabled(canTake);
	m_auto->setEnabled(canTake);
	const TransitionSettings &t = m_studio->profile().transition;
	QString effect;
	if (pc.isTransitioning())
		effect = QStringLiteral("Transição em andamento — comandos de troca bloqueados até o fim.");
	else if (pc.ftbState() == FtbState::FadingOut || pc.ftbState() == FtbState::FadingIn)
		effect = QStringLiteral("Fade para preto em andamento.");
	else if (!pc.selected().isValid())
		effect = QStringLiteral("Selecione uma fonte na grade.");
	else if (pc.selected() == pc.program())
		effect = QStringLiteral("A fonte selecionada já está no ar.");
	else if (t.type == TransitionType::Cut)
		effect = QStringLiteral("CORTE e AUTO trocam imediatamente para “%1”.").arg(selName);
	else
		effect = QStringLiteral("CORTE: troca imediata. AUTO: %1 de %2 ms para “%3”.")
				 .arg(m_type->currentText().toLower())
				 .arg(t.durationMs)
				 .arg(selName);
	if (pc.ftbState() == FtbState::Black)
		effect += QStringLiteral(" A saída está em preto (FTB).");
	m_effect->setText(effect);

	m_ftb->setChecked(pc.ftbState() == FtbState::Black || pc.ftbState() == FtbState::FadingOut);
	m_ftb->setEnabled(!pc.isTransitioning() && pc.ftbState() != FtbState::FadingIn && pc.ftbState() != FtbState::FadingOut);
	m_ftb->setText(pc.ftbState() == FtbState::Off ? QStringLiteral("FADE PARA PRETO") : QStringLiteral("VOLTAR DO PRETO"));
	m_ftb->setStyleSheet(m_ftb->styleSheet().split("/*ftb*/").first() +
			     (pc.ftbState() != FtbState::Off
				      ? QStringLiteral("/*ftb*/QPushButton{background:%1;color:black;}").arg(theme::hex(theme::transition))
				      : QString()));
}

} // namespace ms
