#include "GraphicsDialogs.h"

#include "LowerThird.h"
#include "SourceDialogs.h"
#include "Studio.h"
#include "Theme.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ms {

namespace {

template<typename E> QComboBox *enumCombo(const QList<QPair<E, QString>> &items, E current)
{
	auto *c = new QComboBox;
	for (const auto &i : items)
		c->addItem(i.second, int(i.first));
	c->setCurrentIndex(std::max(0, c->findData(int(current))));
	return c;
}

const QList<QPair<GraphicAnimation, QString>> kAnims = {{GraphicAnimation::Slide, QStringLiteral("Deslizar + fade")},
							{GraphicAnimation::Wipe, QStringLiteral("Revelar (wipe)")},
							{GraphicAnimation::Fade, QStringLiteral("Fade")},
							{GraphicAnimation::Cut, QStringLiteral("Corte")}};

} // namespace

LowerThirdDialog::LowerThirdDialog(Studio *studio, const QString &uid, QWidget *parent)
	: QDialog(parent), m_studio(studio), m_isNew(uid.isEmpty())
{
	if (!m_isNew)
		m_cfg = *studio->profile().findLowerThird(uid);
	else
		m_cfg.templateName = QStringLiteral("Identificação");
	setWindowTitle(m_isNew ? QStringLiteral("Nova identificação") : QStringLiteral("Editar identificação"));
	resize(980, 560);
	m_accentC = m_cfg.accent;
	m_plateC = m_cfg.plate;
	m_textC = m_cfg.text;
	m_subC = m_cfg.subText;

	auto *root = new QHBoxLayout(this);
	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	m_template = new QLineEdit(m_cfg.templateName);
	m_template->setPlaceholderText(QStringLiteral("ex.: Convidado 1"));
	form->addRow(QStringLiteral("Nome na biblioteca"), m_template);
	m_style = enumCombo<GraphicStyle>({{GraphicStyle::LowerThird, QStringLiteral("Lower third (nome + cargo)")},
					   {GraphicStyle::Band, QStringLiteral("Tarja (faixa inferior)")},
					   {GraphicStyle::Title, QStringLiteral("Título / chamada")}},
					  m_cfg.style);
	form->addRow(QStringLiteral("Estilo"), m_style);
	m_name = new QLineEdit(m_cfg.name);
	form->addRow(QStringLiteral("Nome / título"), m_name);
	m_role = new QLineEdit(m_cfg.role);
	form->addRow(QStringLiteral("Cargo"), m_role);
	m_company = new QLineEdit(m_cfg.company);
	form->addRow(QStringLiteral("Empresa"), m_company);
	m_secondary = new QLineEdit(m_cfg.secondary);
	m_secondary->setPlaceholderText(QStringLiteral("ex.: @perfil, tema do episódio, AO VIVO"));
	form->addRow(QStringLiteral("Texto secundário"), m_secondary);
	auto *logoRow = new QHBoxLayout;
	m_logo = new QLineEdit(m_cfg.logoPath);
	auto *browse = new QPushButton(QStringLiteral("…"));
	logoRow->addWidget(m_logo, 1);
	logoRow->addWidget(browse);
	form->addRow(QStringLiteral("Logotipo"), logoRow);
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString f = QFileDialog::getOpenFileName(this, QString(), QString(),
							       QStringLiteral("Imagens (*.png *.jpg *.jpeg *.svg *.webp)"));
		if (!f.isEmpty())
			m_logo->setText(f);
	});
	m_font = new QFontComboBox;
	m_font->setCurrentFont(QFont(m_cfg.fontFamily.isEmpty() ? LowerThirdRenderer::defaultFontFamily() : m_cfg.fontFamily));
	form->addRow(QStringLiteral("Fonte"), m_font);

	auto colorBtn = [this](QColor &target) {
		auto *b = new QPushButton;
		b->setStyleSheet(colorButtonStyle(target));
		connect(b, &QPushButton::clicked, this, [this, b, &target] {
			const QColor c = QColorDialog::getColor(target, this, QString(), QColorDialog::ShowAlphaChannel);
			if (c.isValid()) {
				target = c;
				b->setStyleSheet(colorButtonStyle(c));
				updatePreview();
			}
		});
		return b;
	};
	auto *colors = new QHBoxLayout;
	m_accent = colorBtn(m_accentC);
	m_plate = colorBtn(m_plateC);
	m_text = colorBtn(m_textC);
	m_sub = colorBtn(m_subC);
	for (auto pair : {qMakePair(QStringLiteral("Destaque"), m_accent), qMakePair(QStringLiteral("Fundo"), m_plate),
			  qMakePair(QStringLiteral("Texto"), m_text), qMakePair(QStringLiteral("Secundário"), m_sub)}) {
		auto *col = new QVBoxLayout;
		auto *l = new QLabel(pair.first);
		l->setStyleSheet(QStringLiteral("color:%1;font-size:8pt").arg(theme::hex(theme::text3)));
		col->addWidget(l);
		col->addWidget(pair.second);
		colors->addLayout(col);
	}
	form->addRow(QStringLiteral("Cores"), colors);
	m_position = enumCombo<GraphicPosition>({{GraphicPosition::BottomLeft, QStringLiteral("Inferior esquerda")},
						 {GraphicPosition::BottomCenter, QStringLiteral("Inferior central")},
						 {GraphicPosition::BottomRight, QStringLiteral("Inferior direita")},
						 {GraphicPosition::TopLeft, QStringLiteral("Superior esquerda")},
						 {GraphicPosition::TopRight, QStringLiteral("Superior direita")},
						 {GraphicPosition::Center, QStringLiteral("Centro")}},
						m_cfg.position);
	form->addRow(QStringLiteral("Posição"), m_position);
	m_scale = new QDoubleSpinBox;
	m_scale->setRange(0.4, 2.5);
	m_scale->setSingleStep(0.05);
	m_scale->setValue(m_cfg.scale);
	m_scale->setSuffix(QStringLiteral("×"));
	form->addRow(QStringLiteral("Tamanho"), m_scale);
	auto *anim = new QHBoxLayout;
	m_animIn = enumCombo<GraphicAnimation>(kAnims, m_cfg.animIn);
	m_animOut = enumCombo<GraphicAnimation>(kAnims, m_cfg.animOut);
	m_animMs = new QSpinBox;
	m_animMs->setRange(0, 3000);
	m_animMs->setSingleStep(50);
	m_animMs->setSuffix(QStringLiteral(" ms"));
	m_animMs->setValue(m_cfg.animMs);
	anim->addWidget(new QLabel(QStringLiteral("Entrada")));
	anim->addWidget(m_animIn);
	anim->addWidget(new QLabel(QStringLiteral("Saída")));
	anim->addWidget(m_animOut);
	anim->addWidget(m_animMs);
	form->addRow(QStringLiteral("Animação"), anim);
	m_hold = new QDoubleSpinBox;
	m_hold->setRange(0, 3600);
	m_hold->setDecimals(1);
	m_hold->setSuffix(QStringLiteral(" s"));
	m_hold->setSpecialValueText(QStringLiteral("Até remover manualmente"));
	m_hold->setValue(m_cfg.holdSeconds);
	form->addRow(QStringLiteral("Duração no ar"), m_hold);
	root->addLayout(form);

	auto *right = new QVBoxLayout;
	auto *pl = new QLabel(QStringLiteral("PRÉ-VISUALIZAÇÃO (renderização idêntica à saída)"));
	pl->setObjectName("PanelTitle");
	right->addWidget(pl);
	m_preview = new ImagePreview;
	m_preview->setMinimumSize(480, 270);
	right->addWidget(m_preview, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	if (!m_isNew) {
		auto *del = buttons->addButton(QStringLiteral("Excluir…"), QDialogButtonBox::DestructiveRole);
		connect(del, &QPushButton::clicked, this, [this] {
			if (QMessageBox::question(this, QStringLiteral("Excluir identificação"),
						  QStringLiteral("Excluir “%1” da biblioteca?").arg(m_cfg.name)) == QMessageBox::Yes) {
				m_studio->removeLowerThird(m_cfg.uid);
				QDialog::reject();
			}
		});
	}
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	right->addWidget(buttons);
	root->addLayout(right, 1);

	for (auto *e : findChildren<QLineEdit *>())
		connect(e, &QLineEdit::textChanged, this, &LowerThirdDialog::updatePreview);
	for (auto *c : findChildren<QComboBox *>())
		connect(c, &QComboBox::currentIndexChanged, this, &LowerThirdDialog::updatePreview);
	connect(m_scale, &QDoubleSpinBox::valueChanged, this, &LowerThirdDialog::updatePreview);
	updatePreview();
}

LowerThirdConfig LowerThirdDialog::current() const
{
	LowerThirdConfig c = m_cfg;
	c.templateName = m_template->text().trimmed();
	c.style = GraphicStyle(m_style->currentData().toInt());
	c.name = m_name->text();
	c.role = m_role->text();
	c.company = m_company->text();
	c.secondary = m_secondary->text();
	c.logoPath = m_logo->text().trimmed();
	c.fontFamily = m_font->currentFont().family();
	c.accent = m_accentC;
	c.plate = m_plateC;
	c.text = m_textC;
	c.subText = m_subC;
	c.position = GraphicPosition(m_position->currentData().toInt());
	c.animIn = GraphicAnimation(m_animIn->currentData().toInt());
	c.animOut = GraphicAnimation(m_animOut->currentData().toInt());
	c.animMs = m_animMs->value();
	c.holdSeconds = m_hold->value();
	c.scale = m_scale->value();
	return c;
}

void LowerThirdDialog::updatePreview()
{
	const VideoSettings &v = m_studio->profile().video;
	const QSize canvas(v.baseWidth, v.baseHeight);
	const RenderedGraphic g = LowerThirdRenderer::render(current(), canvas);
	QImage frame(canvas, QImage::Format_ARGB32_Premultiplied);
	QPainter p(&frame);
	// Neutral backdrop resembling a studio shot so contrast can be judged.
	QLinearGradient bg(0, 0, 0, canvas.height());
	bg.setColorAt(0, QColor(0x55, 0x5b, 0x63));
	bg.setColorAt(1, QColor(0x22, 0x25, 0x29));
	p.fillRect(frame.rect(), bg);
	p.setPen(QPen(QColor(255, 255, 255, 50), 2, Qt::DashLine));
	p.drawRect(QRectF(canvas.width() * 0.05, canvas.height() * 0.05, canvas.width() * 0.9, canvas.height() * 0.9));
	if (!g.image.isNull())
		p.drawImage(g.placement.topLeft(), g.image);
	p.end();
	m_preview->setImage(frame);
}

void ImagePreview::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), theme::bg0);
	if (m_image.isNull())
		return;
	QSizeF s = m_image.size();
	s.scale(size(), Qt::KeepAspectRatio);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height()), m_image);
}

void LowerThirdDialog::accept()
{
	LowerThirdConfig c = current();
	if (c.name.trimmed().isEmpty()) {
		QMessageBox::warning(this, windowTitle(), QStringLiteral("Informe o nome ou título."));
		return;
	}
	if (c.templateName.isEmpty())
		c.templateName = c.name;
	if (m_isNew)
		m_studio->addLowerThird(c);
	else
		m_studio->updateLowerThird(c);
	QDialog::accept();
}

// ---------------------------------------------------------------------------

OverlayDialog::OverlayDialog(Studio *studio, const QString &uid, QWidget *parent)
	: QDialog(parent), m_studio(studio), m_isNew(uid.isEmpty())
{
	if (!m_isNew)
		m_cfg = *studio->profile().findOverlay(uid);
	setWindowTitle(m_isNew ? QStringLiteral("Novo logotipo / imagem") : QStringLiteral("Editar logotipo / imagem"));
	setMinimumWidth(460);
	auto *v = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	m_name = new QLineEdit(m_cfg.name);
	form->addRow(QStringLiteral("Nome"), m_name);
	auto *row = new QHBoxLayout;
	m_path = new QLineEdit(m_cfg.path);
	auto *browse = new QPushButton(QStringLiteral("Procurar…"));
	row->addWidget(m_path, 1);
	row->addWidget(browse);
	form->addRow(QStringLiteral("Arquivo"), row);
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString f = QFileDialog::getOpenFileName(this, QString(), QString(),
							       QStringLiteral("Imagens (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"));
		if (!f.isEmpty()) {
			m_path->setText(f);
			if (m_name->text().isEmpty())
				m_name->setText(QFileInfo(f).completeBaseName());
		}
	});
	auto mk = [](double val) {
		auto *s = new QDoubleSpinBox;
		s->setRange(0, 100);
		s->setDecimals(1);
		s->setSuffix(QStringLiteral(" %"));
		s->setValue(val * 100);
		return s;
	};
	m_x = mk(m_cfg.rect.x());
	m_y = mk(m_cfg.rect.y());
	m_w = mk(m_cfg.rect.width());
	m_h = mk(m_cfg.rect.height());
	auto *geo = new QHBoxLayout;
	for (auto pair : {qMakePair(QStringLiteral("X"), m_x), qMakePair(QStringLiteral("Y"), m_y),
			  qMakePair(QStringLiteral("L"), m_w), qMakePair(QStringLiteral("A"), m_h)}) {
		geo->addWidget(new QLabel(pair.first));
		geo->addWidget(pair.second);
	}
	form->addRow(QStringLiteral("Área"), geo);
	m_fade = new QSpinBox;
	m_fade->setRange(0, 5000);
	m_fade->setSingleStep(50);
	m_fade->setSuffix(QStringLiteral(" ms"));
	m_fade->setValue(m_cfg.fadeMs);
	form->addRow(QStringLiteral("Fade"), m_fade);
	v->addLayout(form);
	m_error = new QLabel;
	m_error->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::failure)));
	v->addWidget(m_error);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	if (!m_isNew) {
		auto *del = buttons->addButton(QStringLiteral("Excluir"), QDialogButtonBox::DestructiveRole);
		connect(del, &QPushButton::clicked, this, [this] {
			m_studio->removeOverlay(m_cfg.uid);
			QDialog::reject();
		});
	}
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(buttons);
}

void OverlayDialog::accept()
{
	OverlayImageConfig c = m_cfg;
	c.name = m_name->text().trimmed();
	c.path = m_path->text().trimmed();
	c.rect = QRectF(m_x->value() / 100, m_y->value() / 100, std::max(0.01, m_w->value() / 100),
			std::max(0.01, m_h->value() / 100));
	c.fadeMs = m_fade->value();
	if (c.path.isEmpty() || !QFileInfo::exists(c.path) || QImage(c.path).isNull()) {
		m_error->setText(QStringLiteral("Selecione um arquivo de imagem válido."));
		return;
	}
	if (c.name.isEmpty())
		c.name = QFileInfo(c.path).completeBaseName();
	if (m_isNew)
		m_studio->addOverlay(c);
	else
		m_studio->updateOverlay(c);
	QDialog::accept();
}

} // namespace ms
