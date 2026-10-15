#include "SourceDialogs.h"

#include "Engine.h"
#include "Icons.h"
#include "ObsPlatform.h"
#include "Planning.h"
#include "Studio.h"
#include "Theme.h"

#include <obs.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace ms {

QString colorButtonStyle(const QColor &c)
{
	return QStringLiteral("QPushButton{background:%1;border:1px solid %2;min-width:60px;}")
		.arg(c.name(QColor::HexRgb), theme::hex(theme::lineStrong));
}

namespace {

QLabel *hint(const QString &text)
{
	auto *l = new QLabel(text);
	l->setWordWrap(true);
	l->setStyleSheet(QStringLiteral("color:%1;font-size:8pt").arg(theme::hex(theme::text3)));
	return l;
}

QComboBox *endActionCombo(MediaEndAction current)
{
	auto *c = new QComboBox;
	c->addItem(QStringLiteral("Manter o último quadro"), int(MediaEndAction::Hold));
	c->addItem(QStringLiteral("Ocultar (preto)"), int(MediaEndAction::Black));
	c->addItem(QStringLiteral("Voltar automaticamente à fonte anterior"), int(MediaEndAction::ReturnPrevious));
	c->setCurrentIndex(c->findData(int(current)));
	return c;
}

QString mediaFilter()
{
	return QStringLiteral("Vídeo e áudio (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.ts *.flv *.mp3 *.wav *.aac *.m4a *.ogg "
			      "*.flac);;Todos os arquivos (*)");
}

QString imageFilter()
{
	return QStringLiteral("Imagens (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga);;Todos os arquivos (*)");
}

uint32_t qcolorToObs(const QColor &c)
{
	return (uint32_t(c.alpha()) << 24) | (uint32_t(c.blue()) << 16) | (uint32_t(c.green()) << 8) | uint32_t(c.red());
}

QColor obsToQColor(long long v)
{
	const uint32_t u = uint32_t(v);
	return QColor(u & 0xFF, (u >> 8) & 0xFF, (u >> 16) & 0xFF, (u >> 24) & 0xFF);
}

} // namespace

// ---------------------------------------------------------------------------
// DeviceCombo
// ---------------------------------------------------------------------------

DeviceCombo::DeviceCombo(SourceKind kind, QWidget *parent) : QWidget(parent), m_kind(kind)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	auto *h = new QHBoxLayout;
	m_combo = new QComboBox;
	m_combo->setMinimumWidth(260);
	h->addWidget(m_combo, 1);
	auto *refresh = new QPushButton(Icons::icon(Icon::Refresh, theme::text2), QString());
	refresh->setToolTip(QStringLiteral("Atualizar a lista de dispositivos"));
	h->addWidget(refresh);
	v->addLayout(h);
	m_note = hint({});
	v->addWidget(m_note);
	connect(refresh, &QPushButton::clicked, this, &DeviceCombo::reload);
	connect(m_combo, &QComboBox::currentIndexChanged, this, &DeviceCombo::changed);
	reload();
}

void DeviceCombo::reload()
{
	const DeviceRef keep = m_pending.isEmpty() ? current() : m_pending;
	bool known = false;
	QVector<DeviceInfo> list = Engine::cachedDevices(m_kind, &known);
	if (!known) // first enumeration still running in the background
		list = ObsPlatform::enumerateDevices(m_kind);
	m_combo->blockSignals(true);
	m_combo->clear();
	for (const auto &d : list)
		m_combo->addItem(d.name.isEmpty() ? d.id : d.name, QStringList{d.id, d.name});
	int idx = -1;
	for (int i = 0; i < m_combo->count(); ++i)
		if (m_combo->itemData(i).toStringList().value(0) == keep.id)
			idx = i;
	if (idx < 0 && !keep.isEmpty()) {
		m_combo->addItem(QStringLiteral("%1 (não conectado)").arg(keep.name.isEmpty() ? keep.id : keep.name),
				 QStringList{keep.id, keep.name});
		idx = m_combo->count() - 1;
	}
	if (idx >= 0)
		m_combo->setCurrentIndex(idx);
	m_combo->blockSignals(false);
	if (list.isEmpty())
		m_note->setText(QStringLiteral("Nenhum dispositivo encontrado. Conecte o equipamento e clique em atualizar."));
	else
		m_note->setText(QStringLiteral("%1 dispositivo(s) disponível(is).").arg(list.size()));
	m_pending = {};
}

void DeviceCombo::setCurrent(const DeviceRef &d)
{
	m_pending = d;
	reload();
}

DeviceRef DeviceCombo::current() const
{
	const QStringList v = m_combo->currentData().toStringList();
	return v.size() == 2 ? DeviceRef{v[0], v[1]} : DeviceRef{};
}

// ---------------------------------------------------------------------------
// AddSourceDialog
// ---------------------------------------------------------------------------

AddSourceDialog::AddSourceDialog(Studio *studio, SourceKind kind, QWidget *parent)
	: QDialog(parent), m_studio(studio), m_kind(kind)
{
	setWindowTitle(QStringLiteral("Adicionar — %1").arg(kindLabel(kind)));
	setMinimumWidth(480);
	auto *v = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	m_name = new QLineEdit;
	m_name->setPlaceholderText(kindLabel(kind));
	form->addRow(QStringLiteral("Nome"), m_name);

	const QString unsupported = ObsPlatform::unsupportedReason(kind);
	const KindBinding b = ObsPlatform::binding(kind);
	if (b.deviceProp && unsupported.isEmpty()) {
		m_device = new DeviceCombo(kind);
		form->addRow(kind == SourceKind::Screen   ? QStringLiteral("Monitor")
			     : kind == SourceKind::Window ? QStringLiteral("Janela")
							  : QStringLiteral("Dispositivo"),
			     m_device);
		connect(m_device, &DeviceCombo::changed, this, [this] {
			if (m_name->text().isEmpty() || m_name->property("auto").toBool()) {
				m_name->setText(m_device->current().name.left(40));
				m_name->setProperty("auto", true);
			}
		});
		emit m_device->changed();
	}
	if (kind == SourceKind::Screen || kind == SourceKind::Window) {
		m_cursor = new QCheckBox(QStringLiteral("Capturar o cursor do mouse"));
		m_cursor->setChecked(true);
		form->addRow(QString(), m_cursor);
		form->addRow(QString(), hint(QStringLiteral(
						"Para evitar recursão visual (o programa aparecendo dentro de si mesmo), "
						"o Marshalls Studio é ocultado das capturas no Windows 10 2004+ (opção em Interface). "
						"Em outros sistemas, capture outro monitor ou minimize a janela do estúdio.")));
	}
	if (kind == SourceKind::Media || kind == SourceKind::Image) {
		auto *row = new QHBoxLayout;
		m_path = new QLineEdit;
		auto *browse = new QPushButton(QStringLiteral("Procurar…"));
		row->addWidget(m_path, 1);
		row->addWidget(browse);
		form->addRow(QStringLiteral("Arquivo"), row);
		connect(browse, &QPushButton::clicked, this, [this, kind] {
			const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Selecionar arquivo"), QString(),
									 kind == SourceKind::Media ? mediaFilter() : imageFilter());
			if (!f.isEmpty()) {
				m_path->setText(f);
				if (m_name->text().isEmpty())
					m_name->setText(QFileInfo(f).completeBaseName().left(40));
			}
		});
	}
	if (kind == SourceKind::Media) {
		m_loop = new QCheckBox(QStringLiteral("Repetir em loop"));
		m_restart = new QCheckBox(QStringLiteral("Reiniciar do começo ao entrar no ar"));
		m_restart->setChecked(true);
		m_endAction = endActionCombo(MediaEndAction::Hold);
		form->addRow(QString(), m_loop);
		form->addRow(QString(), m_restart);
		form->addRow(QStringLiteral("Ao terminar"), m_endAction);
		form->addRow(QString(), hint(QStringLiteral("Formatos dependem do decodificador FFmpeg embarcado (H.264, HEVC, "
							    "VP8/VP9, ProRes, AAC, MP3, Opus, PCM…). Arquivos inválidos são "
							    "indicados no cartão da fonte.")));
	}
	if (kind == SourceKind::Slideshow) {
		m_files = new QListWidget;
		m_files->setMinimumHeight(120);
		auto *btns = new QHBoxLayout;
		auto *addFiles = new QPushButton(QStringLiteral("Adicionar imagens…"));
		auto *clear = new QPushButton(QStringLiteral("Limpar"));
		btns->addWidget(addFiles);
		btns->addWidget(clear);
		btns->addStretch(1);
		auto *col = new QVBoxLayout;
		col->addWidget(m_files);
		col->addLayout(btns);
		col->addWidget(hint(QStringLiteral("Exporte os slides (PowerPoint, Keynote, PDF) como imagens. A troca é manual "
						   "pelos controles do cartão. Para apresentar o próprio software de slides, "
						   "use Captura de janela.")));
		form->addRow(QStringLiteral("Slides"), col);
		connect(addFiles, &QPushButton::clicked, this, [this] {
			const QStringList f = QFileDialog::getOpenFileNames(this, QStringLiteral("Selecionar slides"), QString(), imageFilter());
			m_files->addItems(f);
		});
		connect(clear, &QPushButton::clicked, m_files, &QListWidget::clear);
	}
	if (kind == SourceKind::Color) {
		m_color = new QPushButton;
		m_color->setStyleSheet(colorButtonStyle(m_colorValue));
		form->addRow(QStringLiteral("Cor"), m_color);
		connect(m_color, &QPushButton::clicked, this, [this] {
			const QColor c = QColorDialog::getColor(m_colorValue, this);
			if (c.isValid()) {
				m_colorValue = c;
				m_color->setStyleSheet(colorButtonStyle(c));
			}
		});
	}
	v->addLayout(form);
	m_error = new QLabel(unsupported);
	m_error->setWordWrap(true);
	m_error->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::failure)));
	v->addWidget(m_error);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Adicionar"));
	buttons->button(QDialogButtonBox::Ok)->setEnabled(unsupported.isEmpty());
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(buttons);
}

void AddSourceDialog::accept()
{
	SourceConfig c;
	c.kind = m_kind;
	c.name = m_name->text().trimmed();
	if (m_device) {
		c.device = m_device->current();
		if (c.device.isEmpty()) {
			m_error->setText(QStringLiteral("Selecione um dispositivo."));
			return;
		}
	}
	if (m_cursor)
		c.engineSettings.insert(
#ifdef _WIN32
			"capture_cursor",
#else
			"show_cursor",
#endif
			m_cursor->isChecked());
	if (m_path) {
		const QString p = m_path->text().trimmed();
		if (p.isEmpty() || !QFileInfo::exists(p)) {
			m_error->setText(QStringLiteral("Selecione um arquivo existente."));
			return;
		}
		c.engineSettings.insert(m_kind == SourceKind::Media ? "local_file" : "file", p);
	}
	if (m_kind == SourceKind::Media) {
		c.media.loop = m_loop->isChecked();
		c.media.restartOnTake = m_restart->isChecked();
		c.media.endAction = MediaEndAction(m_endAction->currentData().toInt());
	}
	if (m_files) {
		if (m_files->count() == 0) {
			m_error->setText(QStringLiteral("Adicione ao menos uma imagem."));
			return;
		}
		QJsonArray arr;
		for (int i = 0; i < m_files->count(); ++i)
			arr.append(QJsonObject{{"value", m_files->item(i)->text()}, {"hidden", false}, {"selected", false}});
		c.engineSettings.insert("files", arr);
		c.engineSettings.insert("slide_mode", "mode_manual");
		c.engineSettings.insert("transition", "fade");
		c.engineSettings.insert("playback_behavior", "always_play");
	}
	if (m_color)
		c.engineSettings.insert("color", double(qcolorToObs(m_colorValue)));
	QString err;
	const QString uid = m_studio->addSource(c, &err);
	if (uid.isEmpty()) {
		m_error->setText(err);
		return;
	}
	m_result = *m_studio->profile().findSource(uid);
	QDialog::accept();
}

SourceConfig AddSourceDialog::result() const
{
	return m_result;
}

// ---------------------------------------------------------------------------
// ObsPropertiesDialog
// ---------------------------------------------------------------------------

ObsPropertiesDialog::ObsPropertiesDialog(obs_source_t *source, const QStringList &hidden, QWidget *parent)
	: QDialog(parent), m_source(obs_source_get_ref(source)), m_hidden(hidden)
{
	setWindowTitle(QStringLiteral("Configurações avançadas do dispositivo"));
	setMinimumSize(460, 360);
	m_settings = obs_source_get_settings(m_source);
	auto *v = new QVBoxLayout(this);
	v->addWidget(hint(QStringLiteral("Parâmetros do driver/plugin aplicados em tempo real (resolução, taxa de quadros, "
					 "formato, método de captura).")));
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	m_host = new QWidget;
	m_form = new QFormLayout(m_host);
	m_form->setLabelAlignment(Qt::AlignRight);
	scroll->setWidget(m_host);
	v->addWidget(scroll, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(buttons);
	rebuild();
}

ObsPropertiesDialog::~ObsPropertiesDialog()
{
	if (m_props)
		obs_properties_destroy(m_props);
	obs_data_release(m_settings);
	obs_source_release(m_source);
}

QJsonObject ObsPropertiesDialog::userSettings() const
{
	const char *json = obs_data_get_json(m_settings);
	QJsonObject o = QJsonDocument::fromJson(QByteArray(json ? json : "{}")).object();
	for (const QString &h : m_hidden)
		o.remove(h);
	return o;
}

void ObsPropertiesDialog::rebuild()
{
	while (m_form->rowCount() > 0)
		m_form->removeRow(0);
	if (m_props)
		obs_properties_destroy(m_props);
	m_props = obs_source_properties(m_source);
	if (!m_props) {
		m_form->addRow(hint(QStringLiteral("Esta fonte não expõe configurações adicionais.")));
		return;
	}
	auto changed = [this](obs_property_t *p) {
		obs_source_update(m_source, m_settings);
		if (p && obs_property_modified(p, m_settings))
			QTimer::singleShot(0, this, &ObsPropertiesDialog::rebuild);
	};
	std::function<void(obs_properties_t *)> addAll = [&](obs_properties_t *props) {
		for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p)) {
			const QString name = QString::fromUtf8(obs_property_name(p));
			if (!obs_property_visible(p) || m_hidden.contains(name))
				continue;
			const QString label = QString::fromUtf8(obs_property_description(p));
			const QByteArray key = name.toUtf8();
			QWidget *w = nullptr;
			switch (obs_property_get_type(p)) {
			case OBS_PROPERTY_BOOL: {
				auto *c = new QCheckBox(label);
				c->setChecked(obs_data_get_bool(m_settings, key));
				connect(c, &QCheckBox::toggled, this, [this, key, p, changed](bool on) {
					obs_data_set_bool(m_settings, key, on);
					changed(p);
				});
				m_form->addRow(QString(), c);
				continue;
			}
			case OBS_PROPERTY_INT: {
				auto *s = new QSpinBox;
				s->setRange(obs_property_int_min(p), obs_property_int_max(p));
				s->setSingleStep(std::max(1, obs_property_int_step(p)));
				s->setValue(int(obs_data_get_int(m_settings, key)));
				s->setSuffix(QString::fromUtf8(obs_property_int_suffix(p)));
				connect(s, &QSpinBox::valueChanged, this, [this, key, p, changed](int val) {
					obs_data_set_int(m_settings, key, val);
					changed(p);
				});
				w = s;
				break;
			}
			case OBS_PROPERTY_FLOAT: {
				auto *s = new QDoubleSpinBox;
				s->setRange(obs_property_float_min(p), obs_property_float_max(p));
				s->setSingleStep(obs_property_float_step(p));
				s->setValue(obs_data_get_double(m_settings, key));
				connect(s, &QDoubleSpinBox::valueChanged, this, [this, key, p, changed](double val) {
					obs_data_set_double(m_settings, key, val);
					changed(p);
				});
				w = s;
				break;
			}
			case OBS_PROPERTY_TEXT: {
				if (obs_property_text_type(p) == OBS_TEXT_INFO) {
					m_form->addRow(hint(label));
					continue;
				}
				auto *e = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, key)));
				if (obs_property_text_type(p) == OBS_TEXT_PASSWORD)
					e->setEchoMode(QLineEdit::Password);
				connect(e, &QLineEdit::editingFinished, this, [this, e, key, p, changed] {
					obs_data_set_string(m_settings, key, e->text().toUtf8().constData());
					changed(p);
				});
				w = e;
				break;
			}
			case OBS_PROPERTY_PATH: {
				auto *row = new QWidget;
				auto *h = new QHBoxLayout(row);
				h->setContentsMargins(0, 0, 0, 0);
				auto *e = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, key)));
				auto *b = new QPushButton(QStringLiteral("…"));
				h->addWidget(e, 1);
				h->addWidget(b);
				connect(b, &QPushButton::clicked, this, [this, e, p] {
					const QString f = obs_property_path_type(p) == OBS_PATH_DIRECTORY
								  ? QFileDialog::getExistingDirectory(this)
								  : QFileDialog::getOpenFileName(this, QString(), QString(),
												 QString::fromUtf8(obs_property_path_filter(p)));
					if (!f.isEmpty()) {
						e->setText(f);
						emit e->editingFinished();
					}
				});
				connect(e, &QLineEdit::editingFinished, this, [this, e, key, p, changed] {
					obs_data_set_string(m_settings, key, e->text().toUtf8().constData());
					changed(p);
				});
				w = row;
				break;
			}
			case OBS_PROPERTY_LIST: {
				auto *c = new QComboBox;
				const obs_combo_format fmt = obs_property_list_format(p);
				const size_t n = obs_property_list_item_count(p);
				int current = -1;
				for (size_t i = 0; i < n; ++i) {
					const QString itemName = QString::fromUtf8(obs_property_list_item_name(p, i));
					QVariant data;
					if (fmt == OBS_COMBO_FORMAT_INT) {
						data = qlonglong(obs_property_list_item_int(p, i));
						if (obs_data_get_int(m_settings, key) == data.toLongLong())
							current = int(i);
					} else if (fmt == OBS_COMBO_FORMAT_FLOAT) {
						data = obs_property_list_item_float(p, i);
						if (qFuzzyCompare(obs_data_get_double(m_settings, key), data.toDouble()))
							current = int(i);
					} else if (fmt == OBS_COMBO_FORMAT_STRING) {
						data = QString::fromUtf8(obs_property_list_item_string(p, i));
						if (data.toString() == QString::fromUtf8(obs_data_get_string(m_settings, key)))
							current = int(i);
					} else if (fmt == OBS_COMBO_FORMAT_BOOL) {
						data = obs_property_list_item_bool(p, i);
						if (obs_data_get_bool(m_settings, key) == data.toBool())
							current = int(i);
					}
					c->addItem(itemName, data);
				}
				c->setCurrentIndex(current);
				connect(c, &QComboBox::activated, this, [this, c, fmt, key, p, changed](int idx) {
					const QVariant d = c->itemData(idx);
					if (fmt == OBS_COMBO_FORMAT_INT)
						obs_data_set_int(m_settings, key, d.toLongLong());
					else if (fmt == OBS_COMBO_FORMAT_FLOAT)
						obs_data_set_double(m_settings, key, d.toDouble());
					else if (fmt == OBS_COMBO_FORMAT_STRING)
						obs_data_set_string(m_settings, key, d.toString().toUtf8().constData());
					else if (fmt == OBS_COMBO_FORMAT_BOOL)
						obs_data_set_bool(m_settings, key, d.toBool());
					changed(p);
				});
				w = c;
				break;
			}
			case OBS_PROPERTY_COLOR:
			case OBS_PROPERTY_COLOR_ALPHA: {
				auto *b = new QPushButton;
				const QColor col = obsToQColor(obs_data_get_int(m_settings, key));
				b->setStyleSheet(colorButtonStyle(col));
				const bool alpha = obs_property_get_type(p) == OBS_PROPERTY_COLOR_ALPHA;
				connect(b, &QPushButton::clicked, this, [this, b, key, p, alpha, changed] {
					const QColor c = QColorDialog::getColor(obsToQColor(obs_data_get_int(m_settings, key)), this,
										QString(), alpha ? QColorDialog::ShowAlphaChannel
												 : QColorDialog::ColorDialogOptions());
					if (!c.isValid())
						return;
					QColor cc = c;
					if (!alpha)
						cc.setAlpha(255);
					obs_data_set_int(m_settings, key, qcolorToObs(cc));
					b->setStyleSheet(colorButtonStyle(cc));
					changed(p);
				});
				w = b;
				break;
			}
			case OBS_PROPERTY_BUTTON: {
				auto *b = new QPushButton(label);
				connect(b, &QPushButton::clicked, this, [this, p] {
					if (obs_property_button_clicked(p, m_source))
						QTimer::singleShot(0, this, &ObsPropertiesDialog::rebuild);
				});
				m_form->addRow(QString(), b);
				continue;
			}
			case OBS_PROPERTY_GROUP: {
				m_form->addRow(hint(QStringLiteral("<b>%1</b>").arg(label)));
				addAll(obs_property_group_content(p));
				continue;
			}
			default: continue;
			}
			if (w) {
				w->setEnabled(obs_property_enabled(p));
				w->setToolTip(QString::fromUtf8(obs_property_long_description(p)));
				m_form->addRow(label, w);
			}
		}
	};
	addAll(m_props);
}

// ---------------------------------------------------------------------------
// SourcePropertiesDialog
// ---------------------------------------------------------------------------

SourcePropertiesDialog::SourcePropertiesDialog(Studio *studio, const QString &uid, QWidget *parent)
	: QDialog(parent), m_studio(studio)
{
	m_cfg = *studio->profile().findSource(uid);
	m_engineSettings = m_cfg.engineSettings;
	setWindowTitle(QStringLiteral("Propriedades — %1").arg(m_cfg.name));
	setMinimumWidth(500);
	auto *v = new QVBoxLayout(this);
	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	m_name = new QLineEdit(m_cfg.name);
	form->addRow(QStringLiteral("Nome"), m_name);
	form->addRow(QStringLiteral("Tipo"), new QLabel(kindLabel(m_cfg.kind)));

	const KindBinding b = ObsPlatform::binding(m_cfg.kind);
	if (b.deviceProp && ObsPlatform::isKindSupported(m_cfg.kind)) {
		m_device = new DeviceCombo(m_cfg.kind);
		m_device->setCurrent(m_cfg.device);
		form->addRow(QStringLiteral("Dispositivo"), m_device);
	}
	if (m_cfg.kind == SourceKind::Media || m_cfg.kind == SourceKind::Image) {
		auto *row = new QHBoxLayout;
		m_path = new QLineEdit(m_cfg.engineSettings.value(m_cfg.kind == SourceKind::Media ? "local_file" : "file").toString());
		auto *browse = new QPushButton(QStringLiteral("Procurar…"));
		row->addWidget(m_path, 1);
		row->addWidget(browse);
		form->addRow(QStringLiteral("Arquivo"), row);
		const SourceKind kind = m_cfg.kind;
		connect(browse, &QPushButton::clicked, this, [this, kind] {
			const QString f = QFileDialog::getOpenFileName(this, QString(), QString(),
								       kind == SourceKind::Media ? mediaFilter() : imageFilter());
			if (!f.isEmpty())
				m_path->setText(f);
		});
	}
	if (kindHasVideo(m_cfg.kind)) {
		m_videoDelay = new QSpinBox;
		m_videoDelay->setRange(0, 500);
		m_videoDelay->setSuffix(QStringLiteral(" ms"));
		m_videoDelay->setValue(m_cfg.videoDelayMs);
		m_videoDelay->setToolTip(QStringLiteral("Atrasa somente o vídeo desta fonte (compensa áudio que chega depois da imagem)."));
		form->addRow(QStringLiteral("Atraso de vídeo"), m_videoDelay);
	}
	if (m_cfg.kind == SourceKind::Media || m_cfg.kind == SourceKind::Camera) {
		m_hasAudio = new QCheckBox(QStringLiteral("Usar o áudio desta fonte (aparece na mesa de áudio)"));
		m_hasAudio->setChecked(m_cfg.hasAudio);
		form->addRow(QString(), m_hasAudio);
	}
	if (m_cfg.kind == SourceKind::Media) {
		m_loop = new QCheckBox(QStringLiteral("Repetir em loop"));
		m_loop->setChecked(m_cfg.media.loop);
		m_restart = new QCheckBox(QStringLiteral("Reiniciar do começo ao entrar no ar"));
		m_restart->setChecked(m_cfg.media.restartOnTake);
		m_endAction = endActionCombo(m_cfg.media.endAction);
		form->addRow(QString(), m_loop);
		form->addRow(QString(), m_restart);
		form->addRow(QStringLiteral("Ao terminar"), m_endAction);
	}
	v->addLayout(form);

	if (obs_source_t *src = studio->engine().obsSource(m_cfg.uid)) {
		auto *adv = new QPushButton(Icons::icon(Icon::Gear, theme::text2), QStringLiteral("Configurações avançadas…"));
		adv->setToolTip(QStringLiteral("Resolução, taxa de quadros, formato e demais opções do dispositivo"));
		connect(adv, &QPushButton::clicked, this, [this, src, b] {
			QStringList hidden;
			if (b.deviceProp)
				hidden << QString::fromLatin1(b.deviceProp);
			ObsPropertiesDialog dlg(src, hidden, this);
			if (dlg.exec() == QDialog::Accepted) {
				const QJsonObject s = dlg.userSettings();
				for (auto it = s.begin(); it != s.end(); ++it)
					m_engineSettings.insert(it.key(), it.value());
			} else {
				// restore what the studio had
				m_studio->engine().updateSource(*m_studio->profile().findSource(m_cfg.uid));
			}
		});
		v->addWidget(adv, 0, Qt::AlignLeft);
	}
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(buttons);
}

void SourcePropertiesDialog::accept()
{
	SourceConfig c = *m_studio->profile().findSource(m_cfg.uid);
	c.engineSettings = m_engineSettings;
	if (m_device)
		c.device = m_device->current();
	if (m_path)
		c.engineSettings.insert(c.kind == SourceKind::Media ? "local_file" : "file", m_path->text().trimmed());
	if (m_videoDelay)
		c.videoDelayMs = m_videoDelay->value();
	if (m_hasAudio)
		c.hasAudio = m_hasAudio->isChecked();
	if (m_loop) {
		c.media.loop = m_loop->isChecked();
		c.media.restartOnTake = m_restart->isChecked();
		c.media.endAction = MediaEndAction(m_endAction->currentData().toInt());
	}
	const bool audioChanged = c.hasAudio != m_cfg.hasAudio;
	if (!m_studio->updateSource(c))
		return;
	m_studio->renameSource(c.uid, m_name->text());
	if (audioChanged)
		emit m_studio->contentChanged();
	QDialog::accept();
}

// ---------------------------------------------------------------------------
// AudioChannelDialog
// ---------------------------------------------------------------------------

AudioChannelDialog::AudioChannelDialog(Studio *studio, const QString &uid, QWidget *parent)
	: QDialog(parent), m_studio(studio), m_uid(uid)
{
	m_original = *studio->profile().findSource(uid);
	const SourceConfig &c = m_original;
	setWindowTitle(QStringLiteral("Canal de áudio — %1").arg(c.name));
	setMinimumWidth(560);
	auto *v = new QVBoxLayout(this);

	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	m_name = new QLineEdit(c.name);
	form->addRow(QStringLiteral("Nome"), m_name);
	if (c.kind == SourceKind::AudioInput || c.kind == SourceKind::AudioOutput) {
		m_device = new DeviceCombo(c.kind);
		m_device->setCurrent(c.device);
		form->addRow(QStringLiteral("Dispositivo"), m_device);
	}
	m_sync = new QSpinBox;
	m_sync->setRange(-950, 20000);
	m_sync->setSingleStep(10);
	m_sync->setSuffix(QStringLiteral(" ms"));
	m_sync->setValue(c.audio.syncOffsetMs);
	auto *syncRow = new QVBoxLayout;
	syncRow->addWidget(m_sync);
	syncRow->addWidget(hint(QStringLiteral("Atraso de sincronização deste áudio. Positivo atrasa o áudio (use quando o som "
					       "chega antes da imagem). Para compensar som que chega depois da imagem, use o "
					       "atraso de vídeo nas propriedades da câmera.")));
	form->addRow(QStringLiteral("Sincronização"), syncRow);
	m_toProgram = new QCheckBox(QStringLiteral("Enviar ao programa (trilha 1: gravação e transmissão)"));
	m_toProgram->setChecked(c.audio.toProgram);
	m_isolated = new QCheckBox(QStringLiteral("Gravar também em trilha isolada (pós-produção)"));
	m_isolated->setChecked(c.audio.isolatedTrack);
	form->addRow(QStringLiteral("Roteamento"), m_toProgram);
	form->addRow(QString(), m_isolated);
	m_monitor = new QComboBox;
	m_monitor->addItem(QStringLiteral("Desligado"), int(MonitorMode::Off));
	m_monitor->addItem(QStringLiteral("Somente monitorar (não vai ao programa)"), int(MonitorMode::MonitorOnly));
	m_monitor->addItem(QStringLiteral("Monitorar e enviar ao programa"), int(MonitorMode::MonitorAndOutput));
	m_monitor->setCurrentIndex(m_monitor->findData(int(c.audio.monitor)));
	form->addRow(QStringLiteral("Monitoramento"), m_monitor);
	v->addLayout(form);

	auto *fx = new QGroupBox(QStringLiteral("Processamento (ordem: ganho → supressão → gate → EQ → compressor → limitador)"));
	auto *g = new QGridLayout(fx);
	const AudioFilters &f = c.audio.filters;
	auto dspin = [](double lo, double hi, double val, const QString &suffix, double step = 0.5) {
		auto *s = new QDoubleSpinBox;
		s->setRange(lo, hi);
		s->setSingleStep(step);
		s->setValue(val);
		s->setSuffix(suffix);
		return s;
	};
	auto ispin = [](int lo, int hi, int val, const QString &suffix) {
		auto *s = new QSpinBox;
		s->setRange(lo, hi);
		s->setValue(val);
		s->setSuffix(suffix);
		return s;
	};
	int r = 0;
	g->addWidget(new QLabel(QStringLiteral("Ganho de entrada")), r, 0);
	m_gain = dspin(-30, 30, f.gainDb, QStringLiteral(" dB"));
	g->addWidget(m_gain, r++, 1);

	m_suppressOn = new QCheckBox(QStringLiteral("Supressão de ruído"));
	m_suppressOn->setChecked(f.suppressEnabled);
	m_suppressLevel = ispin(-60, 0, f.suppressLevelDb, QStringLiteral(" dB"));
	g->addWidget(m_suppressOn, r, 0);
	g->addWidget(m_suppressLevel, r++, 1);

	m_gateOn = new QCheckBox(QStringLiteral("Gate de ruído"));
	m_gateOn->setChecked(f.gateEnabled);
	m_gateOpen = dspin(-96, 0, f.gateOpenDb, QStringLiteral(" dB abre"));
	m_gateClose = dspin(-96, 0, f.gateCloseDb, QStringLiteral(" dB fecha"));
	g->addWidget(m_gateOn, r, 0);
	g->addWidget(m_gateOpen, r, 1);
	g->addWidget(m_gateClose, r++, 2);

	m_eqOn = new QCheckBox(QStringLiteral("Equalizador (3 bandas)"));
	m_eqOn->setChecked(f.eqEnabled);
	m_eqLow = dspin(-20, 20, f.eqLowDb, QStringLiteral(" dB grave"));
	m_eqMid = dspin(-20, 20, f.eqMidDb, QStringLiteral(" dB médio"));
	m_eqHigh = dspin(-20, 20, f.eqHighDb, QStringLiteral(" dB agudo"));
	g->addWidget(m_eqOn, r, 0);
	g->addWidget(m_eqLow, r, 1);
	g->addWidget(m_eqMid, r, 2);
	g->addWidget(m_eqHigh, r++, 3);

	m_compOn = new QCheckBox(QStringLiteral("Compressor"));
	m_compOn->setChecked(f.compEnabled);
	m_compRatio = dspin(1, 32, f.compRatio, QStringLiteral(":1"));
	m_compThreshold = dspin(-60, 0, f.compThresholdDb, QStringLiteral(" dB limiar"));
	m_compGain = dspin(-32, 32, f.compOutputGainDb, QStringLiteral(" dB saída"));
	g->addWidget(m_compOn, r, 0);
	g->addWidget(m_compRatio, r, 1);
	g->addWidget(m_compThreshold, r, 2);
	g->addWidget(m_compGain, r++, 3);
	m_compAttack = ispin(1, 500, f.compAttackMs, QStringLiteral(" ms ataque"));
	m_compRelease = ispin(1, 1000, f.compReleaseMs, QStringLiteral(" ms release"));
	g->addWidget(m_compAttack, r, 1);
	g->addWidget(m_compRelease, r++, 2);

	m_limOn = new QCheckBox(QStringLiteral("Limitador"));
	m_limOn->setChecked(f.limiterEnabled);
	m_limThreshold = dspin(-60, 0, f.limiterThresholdDb, QStringLiteral(" dB teto"));
	g->addWidget(m_limOn, r, 0);
	g->addWidget(m_limThreshold, r++, 1);
	v->addWidget(fx);
	v->addWidget(hint(QStringLiteral("As alterações de processamento e sincronização são aplicadas em tempo real para "
					 "você ouvir/medir o resultado; Cancelar restaura os valores anteriores.")));

	for (auto *w : findChildren<QDoubleSpinBox *>())
		connect(w, &QDoubleSpinBox::valueChanged, this, &AudioChannelDialog::applyLive);
	for (auto *w : findChildren<QSpinBox *>())
		connect(w, &QSpinBox::valueChanged, this, &AudioChannelDialog::applyLive);
	for (auto *w : findChildren<QCheckBox *>())
		connect(w, &QCheckBox::toggled, this, &AudioChannelDialog::applyLive);
	connect(m_monitor, &QComboBox::currentIndexChanged, this, &AudioChannelDialog::applyLive);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto *remove = buttons->addButton(QStringLiteral("Remover canal…"), QDialogButtonBox::DestructiveRole);
	connect(remove, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, QStringLiteral("Remover canal"),
					  QStringLiteral("Remover “%1” da mesa de áudio?").arg(m_original.name)) ==
		    QMessageBox::Yes) {
			m_studio->removeSource(m_uid);
			QDialog::reject();
		}
	});
	remove->setVisible(m_original.kind == SourceKind::AudioInput || m_original.kind == SourceKind::AudioOutput);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(buttons);
}

AudioFilters AudioChannelDialog::filters() const
{
	AudioFilters f = m_original.audio.filters;
	f.gainDb = m_gain->value();
	f.suppressEnabled = m_suppressOn->isChecked();
	f.suppressLevelDb = m_suppressLevel->value();
	f.gateEnabled = m_gateOn->isChecked();
	f.gateOpenDb = m_gateOpen->value();
	f.gateCloseDb = m_gateClose->value();
	f.eqEnabled = m_eqOn->isChecked();
	f.eqLowDb = m_eqLow->value();
	f.eqMidDb = m_eqMid->value();
	f.eqHighDb = m_eqHigh->value();
	f.compEnabled = m_compOn->isChecked();
	f.compRatio = m_compRatio->value();
	f.compThresholdDb = m_compThreshold->value();
	f.compOutputGainDb = m_compGain->value();
	f.compAttackMs = m_compAttack->value();
	f.compReleaseMs = m_compRelease->value();
	f.limiterEnabled = m_limOn->isChecked();
	f.limiterThresholdDb = m_limThreshold->value();
	return f;
}

void AudioChannelDialog::applyLive()
{
	m_studio->setFilters(m_uid, filters());
	m_studio->setSyncOffset(m_uid, m_sync->value());
	m_studio->setMonitor(m_uid, MonitorMode(m_monitor->currentData().toInt()));
}

void AudioChannelDialog::accept()
{
	SourceConfig c = *m_studio->profile().findSource(m_uid);
	if (m_device)
		c.device = m_device->current();
	c.audio.toProgram = m_toProgram->isChecked();
	c.audio.isolatedTrack = m_isolated->isChecked();
	c.audio.filters = filters();
	c.audio.syncOffsetMs = m_sync->value();
	c.audio.monitor = MonitorMode(m_monitor->currentData().toInt());
	if (!m_studio->updateSource(c))
		return;
	m_studio->renameSource(m_uid, m_name->text());
	QDialog::accept();
}

void AudioChannelDialog::reject()
{
	if (m_studio->profile().findSource(m_uid)) {
		m_studio->setFilters(m_uid, m_original.audio.filters);
		m_studio->setSyncOffset(m_uid, m_original.audio.syncOffsetMs);
		m_studio->setMonitor(m_uid, m_original.audio.monitor);
	}
	QDialog::reject();
}

} // namespace ms
