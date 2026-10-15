#include "SettingsDialog.h"

#include "Engine.h"
#include "Log.h"
#include "Outputs.h"
#include "Planning.h"
#include "SecretStore.h"
#include "ShortcutMap.h"
#include "Studio.h"
#include "Theme.h"

#include <algorithm>
#include <obs.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStorageInfo>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace ms {

namespace {

QLabel *hint(const QString &t)
{
	auto *l = new QLabel(t);
	l->setWordWrap(true);
	l->setStyleSheet(QStringLiteral("color:%1;font-size:8pt").arg(theme::hex(theme::text3)));
	return l;
}

QSpinBox *spin(int lo, int hi, int val, const QString &suffix = {}, int step = 1)
{
	auto *s = new QSpinBox;
	s->setRange(lo, hi);
	s->setValue(val);
	s->setSuffix(suffix);
	s->setSingleStep(step);
	return s;
}

QComboBox *resolutionCombo(int w, int h)
{
	auto *c = new QComboBox;
	c->setEditable(true);
	for (const QSize &s : {QSize(3840, 2160), QSize(2560, 1440), QSize(1920, 1080), QSize(1600, 900), QSize(1280, 720),
			       QSize(1024, 576), QSize(854, 480), QSize(640, 360)})
		c->addItem(QStringLiteral("%1x%2").arg(s.width()).arg(s.height()));
	c->setCurrentText(QStringLiteral("%1x%2").arg(w).arg(h));
	return c;
}

bool parseRes(const QString &t, int *w, int *h)
{
	const QStringList p = t.toLower().split('x');
	if (p.size() != 2)
		return false;
	bool a = false, b = false;
	*w = p[0].trimmed().toInt(&a);
	*h = p[1].trimmed().toInt(&b);
	return a && b && *w >= 320 && *h >= 180 && *w <= 7680 && *h <= 4320;
}

QComboBox *encoderCombo(const QString &current)
{
	auto *c = new QComboBox;
	c->addItem(QStringLiteral("Automático (hardware quando disponível)"), QString());
	for (const auto &e : OutputManager::availableVideoEncoders())
		c->addItem(e.label, e.id);
	c->setCurrentIndex(std::max(0, c->findData(current)));
	return c;
}

} // namespace

SettingsDialog::SettingsDialog(Studio *studio, int tab, QWidget *parent) : QDialog(parent), m_studio(studio)
{
	setWindowTitle(QStringLiteral("Configurações — perfil “%1”").arg(studio->profile().name));
	resize(760, 620);
	auto *v = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	m_tabs->addTab(buildVideo(), QStringLiteral("Vídeo"));
	m_tabs->addTab(buildRecording(), QStringLiteral("Gravação"));
	m_tabs->addTab(buildStreaming(), QStringLiteral("Transmissão"));
	m_tabs->addTab(buildAudio(), QStringLiteral("Áudio"));
	m_tabs->addTab(buildShortcuts(), QStringLiteral("Atalhos"));
	m_tabs->addTab(buildInterface(), QStringLiteral("Interface"));
	m_tabs->addTab(buildDiagnostics(), QStringLiteral("Diagnóstico"));
	m_tabs->setCurrentIndex(tab);
	v->addWidget(m_tabs, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(buttons);
}

QWidget *SettingsDialog::buildVideo()
{
	const VideoSettings &vs = m_studio->profile().video;
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);
	m_base = resolutionCombo(vs.baseWidth, vs.baseHeight);
	m_output = resolutionCombo(vs.outputWidth, vs.outputHeight);
	m_fps = new QComboBox;
	const QList<QPair<QString, QPair<int, int>>> rates = {{"24", {24, 1}},        {"25", {25, 1}},
							      {"29.97", {30000, 1001}}, {"30", {30, 1}},
							      {"50", {50, 1}},        {"59.94", {60000, 1001}},
							      {"60", {60, 1}}};
	for (const auto &r : rates)
		m_fps->addItem(r.first, QList<QVariant>{r.second.first, r.second.second});
	for (int i = 0; i < m_fps->count(); ++i) {
		const auto d = m_fps->itemData(i).toList();
		if (d[0].toInt() == vs.fpsNum && d[1].toInt() == vs.fpsDen)
			m_fps->setCurrentIndex(i);
	}
	f->addRow(QStringLiteral("Resolução da composição (canvas)"), m_base);
	f->addRow(QStringLiteral("Resolução de saída (gravação/transmissão)"), m_output);
	f->addRow(QStringLiteral("Quadros por segundo"), m_fps);
	m_videoNote = hint(QStringLiteral(
		"Equipamento de referência (Core i5-10210U, Intel UHD): recomendado canvas 1920x1080 com saída 1280x720 a 30 fps, "
		"codificação por Quick Sync. Saída 1080p60 em CPU (x264) tende a sobrecarregar esse hardware. "
		"A alteração só é permitida com gravação e transmissão encerradas."));
	f->addRow(m_videoNote);
	return w;
}

QWidget *SettingsDialog::buildRecording()
{
	const RecordingSettings &r = m_studio->profile().recording;
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);
	auto *folderRow = new QHBoxLayout;
	m_recFolder = new QLineEdit(r.folder.isEmpty() ? OutputManager::defaultRecordingFolder() : r.folder);
	auto *browse = new QPushButton(QStringLiteral("Procurar…"));
	auto *open = new QPushButton(QStringLiteral("Abrir"));
	folderRow->addWidget(m_recFolder, 1);
	folderRow->addWidget(browse);
	folderRow->addWidget(open);
	f->addRow(QStringLiteral("Pasta"), folderRow);
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString d = QFileDialog::getExistingDirectory(this, QString(), m_recFolder->text());
		if (!d.isEmpty())
			m_recFolder->setText(d);
	});
	connect(open, &QPushButton::clicked, this, [this] {
		QDir().mkpath(m_recFolder->text());
		QDesktopServices::openUrl(QUrl::fromLocalFile(m_recFolder->text()));
	});
	m_recPattern = new QLineEdit(r.filenamePattern);
	f->addRow(QStringLiteral("Nome do arquivo (%1 = data/hora)"), m_recPattern);
	m_recContainer = new QComboBox;
	m_recContainer->addItem(QStringLiteral("MKV — resistente a falhas (recomendado)"), "mkv");
	m_recContainer->addItem(QStringLiteral("MP4 — compatível, mas arquivo se perde em queda de energia"), "mp4");
	m_recContainer->addItem(QStringLiteral("MOV"), "mov");
	m_recContainer->setCurrentIndex(std::max(0, m_recContainer->findData(r.container)));
	f->addRow(QStringLiteral("Formato"), m_recContainer);
	m_recRemux = new QCheckBox(QStringLiteral("Ao encerrar, criar cópia MP4 do arquivo MKV"));
	m_recRemux->setChecked(r.remuxToMp4);
	f->addRow(QString(), m_recRemux);
	m_recEncoder = encoderCombo(r.encoder.encoderId);
	f->addRow(QStringLiteral("Codificador"), m_recEncoder);
	m_recRc = new QComboBox;
	m_recRc->addItem(QStringLiteral("Qualidade constante (CRF/CQP)"), "CRF");
	m_recRc->addItem(QStringLiteral("Bitrate constante (CBR)"), "CBR");
	m_recRc->addItem(QStringLiteral("Bitrate variável (VBR)"), "VBR");
	m_recRc->setCurrentIndex(std::max(0, m_recRc->findData(rateControlToString(r.encoder.rateControl))));
	f->addRow(QStringLiteral("Controle de taxa"), m_recRc);
	m_recCrf = spin(0, 51, r.encoder.crf);
	m_recCrf->setToolTip(QStringLiteral("Menor = melhor qualidade e arquivos maiores (18–23 é típico)"));
	f->addRow(QStringLiteral("Qualidade (CRF/CQP)"), m_recCrf);
	m_recBitrate = spin(500, 100000, r.encoder.videoBitrateKbps, QStringLiteral(" kbps"), 500);
	f->addRow(QStringLiteral("Bitrate de vídeo"), m_recBitrate);
	m_recPreset = new QComboBox;
	for (const char *p : {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium"})
		m_recPreset->addItem(QString::fromLatin1(p), QString::fromLatin1(p));
	m_recPreset->setCurrentIndex(std::max(0, m_recPreset->findData(r.encoder.preset)));
	f->addRow(QStringLiteral("Preset x264 (CPU)"), m_recPreset);
	m_recAudio = spin(64, 320, r.encoder.audioBitrateKbps, QStringLiteral(" kbps"), 32);
	f->addRow(QStringLiteral("Bitrate de áudio (por trilha)"), m_recAudio);
	m_recIsolated = new QCheckBox(QStringLiteral("Trilhas isoladas: trilha 1 = programa, 2–6 = cada microfone"));
	m_recIsolated->setChecked(r.isolatedTracks);
	f->addRow(QString(), m_recIsolated);
	m_recShare = new QCheckBox(QStringLiteral("Reutilizar o codificador da transmissão quando ela estiver ativa (economiza CPU)"));
	m_recShare->setChecked(r.shareStreamEncoder);
	f->addRow(QString(), m_recShare);
	m_recMinFree = spin(100, 1000000, r.minFreeMb, QStringLiteral(" MB"), 256);
	f->addRow(QStringLiteral("Encerrar com espaço livre abaixo de"), m_recMinFree);
	m_diskLabel = new QLabel;
	m_diskLabel->setWordWrap(true);
	f->addRow(QStringLiteral("Estimativa"), m_diskLabel);
	for (auto *s : {m_recBitrate, m_recCrf, m_recAudio, m_recMinFree})
		connect(s, &QSpinBox::valueChanged, this, &SettingsDialog::updateDiskEstimate);
	connect(m_recRc, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateDiskEstimate);
	connect(m_recFolder, &QLineEdit::textChanged, this, &SettingsDialog::updateDiskEstimate);
	updateDiskEstimate();
	return w;
}

RecordingSettings SettingsDialog::recordingFromUi() const
{
	RecordingSettings r = m_studio->profile().recording;
	r.folder = m_recFolder->text().trimmed();
	r.filenamePattern = m_recPattern->text().contains("%1") ? m_recPattern->text() : m_recPattern->text() + " %1";
	r.container = m_recContainer->currentData().toString();
	r.remuxToMp4 = m_recRemux->isChecked();
	r.encoder.encoderId = m_recEncoder->currentData().toString();
	r.encoder.rateControl = rateControlFromString(m_recRc->currentData().toString());
	r.encoder.crf = m_recCrf->value();
	r.encoder.videoBitrateKbps = m_recBitrate->value();
	r.encoder.preset = m_recPreset->currentData().toString();
	r.encoder.audioBitrateKbps = m_recAudio->value();
	r.isolatedTracks = m_recIsolated->isChecked();
	r.shareStreamEncoder = m_recShare->isChecked();
	r.minFreeMb = m_recMinFree->value();
	return r;
}

void SettingsDialog::updateDiskEstimate()
{
	const RecordingSettings r = recordingFromUi();
	QString folder = r.folder;
	while (!folder.isEmpty() && !QFileInfo::exists(folder))
		folder = QFileInfo(folder).absolutePath() == folder ? QString() : QFileInfo(folder).absolutePath();
	const QStorageInfo st(folder.isEmpty() ? QDir::homePath() : folder);
	const VideoSettings &v = m_studio->profile().video;
	const int tracks = 1 + (r.isolatedTracks ? int(m_studio->engine().isolatedTrackSources().size()) : 0);
	const DiskEstimate e = estimateDisk(r, v.outputWidth, v.outputHeight, double(v.fpsNum) / v.fpsDen, tracks, st.bytesAvailable());
	m_recCrf->setEnabled(r.encoder.rateControl == RateControl::CRF);
	m_recBitrate->setEnabled(r.encoder.rateControl != RateControl::CRF);
	m_diskLabel->setText(QStringLiteral("~%1 MB/min (%2 trilha(s) de áudio) · %3 GB livres em %4 · ~%5 de gravação")
				     .arg(e.mbPerMinute, 0, 'f', 0)
				     .arg(tracks)
				     .arg(double(st.bytesAvailable()) / (1024.0 * 1024 * 1024), 0, 'f', 1)
				     .arg(st.rootPath())
				     .arg(e.minutesRemaining >= 120 ? QStringLiteral("%1 h").arg(e.minutesRemaining / 60, 0, 'f', 1)
								    : QStringLiteral("%1 min").arg(int(e.minutesRemaining))));
	m_diskLabel->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(e.warn ? theme::warning : theme::text2)));
}

QWidget *SettingsDialog::buildStreaming()
{
	const StreamSettings &s = m_studio->profile().stream;
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);
	m_platform = new QComboBox;
	for (const auto &p : platformPresets())
		m_platform->addItem(p.label, p.id);
	m_platform->setCurrentIndex(std::max(0, m_platform->findData(s.platform)));
	f->addRow(QStringLiteral("Destino"), m_platform);
	m_server = new QComboBox;
	m_server->setEditable(true);
	f->addRow(QStringLiteral("Servidor (URL)"), m_server);
	auto fillServers = [this](const QString &keep) {
		const PlatformPreset *p = findPlatform(m_platform->currentData().toString());
		m_server->clear();
		if (p)
			m_server->addItems(p->servers);
		m_server->setCurrentText(keep.isEmpty() && p && !p->servers.isEmpty() ? p->servers.first() : keep);
	};
	fillServers(s.server);
	connect(m_platform, &QComboBox::currentIndexChanged, this, [this, fillServers] {
		fillServers({});
		validateStream();
	});
	auto *keyRow = new QHBoxLayout;
	m_key = new QLineEdit(m_studio->streamKey());
	m_key->setEchoMode(QLineEdit::Password);
	auto *show = new QPushButton(QStringLiteral("Mostrar"));
	show->setCheckable(true);
	connect(show, &QPushButton::toggled, this,
		[this](bool on) { m_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
	keyRow->addWidget(m_key, 1);
	keyRow->addWidget(show);
	f->addRow(QStringLiteral("Chave de transmissão"), keyRow);
	f->addRow(hint(SecretStore::isEncrypted()
			       ? QStringLiteral("A chave é guardada criptografada (DPAPI do Windows, por usuário), fora do "
						"perfil, e é omitida dos logs.")
			       : QStringLiteral("Neste sistema a chave é guardada em arquivo acessível só ao usuário (sem "
						"criptografia), fora do perfil, e é omitida dos logs.")));
	m_streamEncoder = encoderCombo(s.encoder.encoderId);
	f->addRow(QStringLiteral("Codificador"), m_streamEncoder);
	m_streamBitrate = spin(300, 51000, s.encoder.videoBitrateKbps, QStringLiteral(" kbps"), 250);
	f->addRow(QStringLiteral("Bitrate de vídeo (CBR)"), m_streamBitrate);
	m_streamAudio = spin(64, 320, s.encoder.audioBitrateKbps, QStringLiteral(" kbps"), 32);
	f->addRow(QStringLiteral("Bitrate de áudio"), m_streamAudio);
	m_keyint = spin(1, 10, std::max(1, s.encoder.keyframeSec), QStringLiteral(" s"));
	f->addRow(QStringLiteral("Intervalo de keyframe"), m_keyint);
	m_delay = spin(0, 120, s.delaySec, QStringLiteral(" s"));
	m_delay->setSpecialValueText(QStringLiteral("Sem atraso"));
	f->addRow(QStringLiteral("Atraso intencional da transmissão"), m_delay);
	m_delayPreserve = new QCheckBox(QStringLiteral("Manter o ponto de atraso ao reconectar"));
	m_delayPreserve->setChecked(s.delayPreserve);
	f->addRow(QString(), m_delayPreserve);
	f->addRow(hint(QStringLiteral(
		"Tipos de atraso: (1) sincronização áudio/vídeo — por fonte, na mesa de áudio e nas propriedades da câmera; "
		"(2) atraso intencional — somente a transmissão, aplicado aos dados já codificados (memória ≈ bitrate × "
		"atraso; a gravação local não é atrasada); (3) buffer do codificador e (4) latência da rede/plataforma — "
		"não controláveis aqui; a plataforma ainda adiciona sua própria latência.")));
	m_retries = spin(0, 1000, s.reconnectRetries);
	m_retryDelay = spin(1, 60, s.reconnectDelaySec, QStringLiteral(" s"));
	auto *rec = new QHBoxLayout;
	rec->addWidget(m_retries);
	rec->addWidget(new QLabel(QStringLiteral("tentativas, intervalo")));
	rec->addWidget(m_retryDelay);
	f->addRow(QStringLiteral("Reconexão automática"), rec);
	m_streamIssues = new QLabel;
	m_streamIssues->setWordWrap(true);
	f->addRow(m_streamIssues);
	connect(m_server, &QComboBox::currentTextChanged, this, &SettingsDialog::validateStream);
	connect(m_key, &QLineEdit::textChanged, this, &SettingsDialog::validateStream);
	connect(m_streamBitrate, &QSpinBox::valueChanged, this, &SettingsDialog::validateStream);
	connect(m_keyint, &QSpinBox::valueChanged, this, &SettingsDialog::validateStream);
	connect(m_delay, &QSpinBox::valueChanged, this, &SettingsDialog::validateStream);
	validateStream();
	return w;
}

StreamSettings SettingsDialog::streamFromUi() const
{
	StreamSettings s = m_studio->profile().stream;
	s.platform = m_platform->currentData().toString();
	s.server = m_server->currentText().trimmed();
	s.encoder.encoderId = m_streamEncoder->currentData().toString();
	s.encoder.rateControl = RateControl::CBR;
	s.encoder.videoBitrateKbps = m_streamBitrate->value();
	s.encoder.audioBitrateKbps = m_streamAudio->value();
	s.encoder.keyframeSec = m_keyint->value();
	s.delaySec = m_delay->value();
	s.delayPreserve = m_delayPreserve->isChecked();
	s.reconnectRetries = m_retries->value();
	s.reconnectDelaySec = m_retryDelay->value();
	return s;
}

void SettingsDialog::validateStream()
{
	QStringList lines;
	for (const auto &i : ms::validateStream(streamFromUi(), m_key->text(), m_studio->profile().video))
		lines << QStringLiteral("<span style='color:%1'>%2 %3</span>")
				 .arg(theme::hex(i.severity == ValidationIssue::Severity::Error ? theme::failure : theme::warning),
				      i.severity == ValidationIssue::Severity::Error ? QStringLiteral("Erro:") : QStringLiteral("Aviso:"),
				      i.message.toHtmlEscaped());
	m_streamIssues->setText(lines.isEmpty() ? QStringLiteral("<span style='color:%1'>Configuração válida.</span>")
							  .arg(theme::hex(theme::ok))
						: lines.join("<br>"));
}

QWidget *SettingsDialog::buildAudio()
{
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);
	m_monitorDevice = new QComboBox;
	for (const auto &d : Engine::monitoringDevices())
		m_monitorDevice->addItem(d.name, QStringList{d.id, d.name});
	for (int i = 0; i < m_monitorDevice->count(); ++i)
		if (m_monitorDevice->itemData(i).toStringList().value(0) == m_studio->profile().ui.monitoringDeviceId)
			m_monitorDevice->setCurrentIndex(i);
	f->addRow(QStringLiteral("Saída de monitoramento (fones)"), m_monitorDevice);
	f->addRow(hint(QStringLiteral("Cada canal escolhe se é monitorado (botão de fone na mesa). O nível de cada canal "
				      "no monitoramento acompanha o fader do canal; o motor não oferece um volume mestre de "
				      "monitoramento separado — ajuste o volume no dispositivo de fones.")));
	f->addRow(QStringLiteral("Taxa de amostragem"), new QLabel(QStringLiteral("48 kHz, estéreo")));
	const QStringList warnings = m_studio->audioWarnings();
	auto *wl = new QLabel(warnings.isEmpty() ? QStringLiteral("Nenhum risco de eco ou captura duplicada detectado.")
						 : warnings.join("\n"));
	wl->setWordWrap(true);
	wl->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(warnings.isEmpty() ? theme::ok : theme::warning)));
	f->addRow(QStringLiteral("Verificação"), wl);
	return w;
}

QWidget *SettingsDialog::buildShortcuts()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->addWidget(hint(QStringLiteral("Clique na coluna Tecla e pressione a combinação. Atalhos em conflito ficam "
					 "desativados até serem resolvidos. Encerrar gravação/transmissão por atalho sempre pede "
					 "confirmação. Atalhos não funcionam enquanto um campo de texto está em edição.")));
	m_shortcutTable = new QTableWidget;
	m_shortcutTable->setColumnCount(3);
	m_shortcutTable->setHorizontalHeaderLabels({QStringLiteral("Ação"), QStringLiteral("Grupo"), QStringLiteral("Tecla")});
	m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_shortcutTable->verticalHeader()->setVisible(false);
	m_shortcutTable->setSelectionMode(QAbstractItemView::NoSelection);
	const auto &cat = ShortcutMap::catalogue();
	m_shortcutTable->setRowCount(int(cat.size()));
	for (int i = 0; i < cat.size(); ++i) {
		auto *a = new QTableWidgetItem(cat[i].label);
		a->setData(Qt::UserRole, cat[i].id);
		a->setFlags(Qt::ItemIsEnabled);
		m_shortcutTable->setItem(i, 0, a);
		auto *g = new QTableWidgetItem(cat[i].category);
		g->setFlags(Qt::ItemIsEnabled);
		m_shortcutTable->setItem(i, 1, g);
		auto *edit = new QKeySequenceEdit(QKeySequence::fromString(m_studio->shortcuts().key(cat[i].id), QKeySequence::PortableText));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
		edit->setMaximumSequenceLength(1);
#endif
		edit->setClearButtonEnabled(true);
		connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, edit] {
			QString why;
			const QString seq = edit->keySequence().toString(QKeySequence::PortableText);
			if (ShortcutMap::isForbidden(seq, &why)) {
				edit->clear();
				m_shortcutStatus->setText(why);
			}
			refreshShortcutConflicts();
		});
		m_shortcutTable->setCellWidget(i, 2, edit);
	}
	m_shortcutTable->setColumnWidth(2, 170);
	v->addWidget(m_shortcutTable, 1);
	auto *row = new QHBoxLayout;
	m_shortcutStatus = new QLabel;
	m_shortcutStatus->setWordWrap(true);
	row->addWidget(m_shortcutStatus, 1);
	auto *reset = new QPushButton(QStringLiteral("Restaurar padrões"));
	connect(reset, &QPushButton::clicked, this, [this] {
		ShortcutMap defaults;
		for (int i = 0; i < m_shortcutTable->rowCount(); ++i) {
			auto *e = qobject_cast<QKeySequenceEdit *>(m_shortcutTable->cellWidget(i, 2));
			e->setKeySequence(QKeySequence::fromString(defaults.key(m_shortcutTable->item(i, 0)->data(Qt::UserRole).toString()),
								   QKeySequence::PortableText));
		}
		refreshShortcutConflicts();
	});
	row->addWidget(reset);
	v->addLayout(row);
	refreshShortcutConflicts();
	return w;
}

void SettingsDialog::refreshShortcutConflicts()
{
	ShortcutMap m;
	for (int i = 0; i < m_shortcutTable->rowCount(); ++i) {
		auto *e = qobject_cast<QKeySequenceEdit *>(m_shortcutTable->cellWidget(i, 2));
		m.setKey(m_shortcutTable->item(i, 0)->data(Qt::UserRole).toString(), e->keySequence().toString(QKeySequence::PortableText));
	}
	const auto conflicts = m.conflicts();
	QStringList conflictIds;
	for (const auto &ids : conflicts)
		conflictIds << ids;
	for (int i = 0; i < m_shortcutTable->rowCount(); ++i) {
		const bool bad = conflictIds.contains(m_shortcutTable->item(i, 0)->data(Qt::UserRole).toString());
		m_shortcutTable->item(i, 0)->setForeground(bad ? theme::failure : theme::text);
	}
	if (conflicts.isEmpty())
		m_shortcutStatus->setText(QStringLiteral("<span style='color:%1'>Sem conflitos.</span>").arg(theme::hex(theme::ok)));
	else
		m_shortcutStatus->setText(QStringLiteral("<span style='color:%1'>Conflito em: %2</span>")
						  .arg(theme::hex(theme::failure), QStringList(conflicts.keys()).join(", ")));
}

QWidget *SettingsDialog::buildInterface()
{
	const UiSettings &u = m_studio->profile().ui;
	auto *w = new QWidget;
	auto *f = new QFormLayout(w);
	m_columns = spin(1, 12, u.gridColumns);
	f->addRow(QStringLiteral("Colunas da grade"), m_columns);
	m_thumbWidth = spin(128, 960, u.thumbnailWidth, QStringLiteral(" px"), 32);
	f->addRow(QStringLiteral("Resolução das miniaturas"), m_thumbWidth);
	m_thumbFps = spin(1, 30, u.thumbnailFps, QStringLiteral(" qps"));
	f->addRow(QStringLiteral("Atualização das miniaturas"), m_thumbFps);
	f->addRow(hint(QStringLiteral("Miniaturas são renderizadas em baixa resolução e taxa reduzida, sem afetar a "
				      "qualidade da saída. Reduza para economizar GPU/CPU em máquinas modestas.")));
	m_hideCapture = new QCheckBox(QStringLiteral("Ocultar o Marshalls Studio das capturas de tela (evita recursão visual)"));
	m_hideCapture->setChecked(u.hideFromCapture);
#ifndef _WIN32
	m_hideCapture->setEnabled(false);
	m_hideCapture->setToolTip(QStringLiteral("Disponível somente no Windows 10 versão 2004 ou superior."));
#endif
	f->addRow(QString(), m_hideCapture);
	return w;
}

QWidget *SettingsDialog::buildDiagnostics()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	auto *text = new QPlainTextEdit;
	text->setReadOnly(true);
	text->setFont(theme::monoFont(8));
	QStringList lines;
	lines << QStringLiteral("Marshalls Studio %1").arg(QStringLiteral(MS_VERSION));
	lines << QStringLiteral("libobs %1 · Qt %2 · %3").arg(QString::fromLatin1(obs_get_version_string()),
							       QString::fromLatin1(qVersion()), QSysInfo::prettyProductName());
	lines << QStringLiteral("Plataforma gráfica Qt: %1").arg(QGuiApplication::platformName());
	lines << QStringLiteral("Pasta de configuração: %1").arg(m_studio->paths().configRoot);
	lines << QStringLiteral("Log atual: %1").arg(Log::instance().currentPath());
	lines << QString();
	lines << QStringLiteral("Codificadores de vídeo disponíveis:");
	for (const auto &e : OutputManager::availableVideoEncoders())
		lines << QStringLiteral("  %1  (%2)%3").arg(e.label, e.id, e.hardware ? QStringLiteral(" [hardware]") : QString());
	lines << QStringLiteral("Tipos de entrada registrados: %1").arg(Engine::registeredInputs().join(", "));
	lines << QString();
	lines << QStringLiteral("Fontes:");
	for (const auto &s : m_studio->profile().sources) {
		const SourceRuntime rt = m_studio->engine().runtime(s.uid);
		lines << QStringLiteral("  %1 [%2] %3x%4 — %5 %6")
				 .arg(s.name, kindToString(s.kind))
				 .arg(rt.width)
				 .arg(rt.height)
				 .arg(healthLabel(rt.health), rt.detail);
	}
	const OutputStats st = m_studio->engine().outputs().stats();
	lines << QString();
	lines << QStringLiteral("CPU %1% · RAM %2 MB · %3 fps · composição perdida %4 quadros · codificação descartada %5 quadros")
			 .arg(st.cpuPercent, 0, 'f', 1)
			 .arg(st.memoryMb, 0, 'f', 0)
			 .arg(st.activeFps, 0, 'f', 2)
			 .arg(st.renderLagged)
			 .arg(st.encodeSkipped);
	text->setPlainText(lines.join('\n'));
	v->addWidget(text, 1);
	auto *row = new QHBoxLayout;
	auto *copy = new QPushButton(QStringLiteral("Copiar relatório"));
	connect(copy, &QPushButton::clicked, this, [text] { QApplication::clipboard()->setText(text->toPlainText()); });
	auto *logs = new QPushButton(QStringLiteral("Abrir pasta de logs"));
	connect(logs, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl::fromLocalFile(Log::instance().directory())); });
	row->addWidget(copy);
	row->addWidget(logs);
	row->addStretch(1);
	v->addLayout(row);
	return w;
}

void SettingsDialog::accept()
{
	// Video
	VideoSettings v = m_studio->profile().video;
	if (!parseRes(m_base->currentText(), &v.baseWidth, &v.baseHeight) ||
	    !parseRes(m_output->currentText(), &v.outputWidth, &v.outputHeight)) {
		m_tabs->setCurrentIndex(Video);
		QMessageBox::warning(this, windowTitle(), QStringLiteral("Resolução inválida. Use o formato 1920x1080."));
		return;
	}
	v.baseWidth &= ~1;
	v.baseHeight &= ~1;
	v.outputWidth &= ~1;
	v.outputHeight &= ~1;
	const auto fps = m_fps->currentData().toList();
	v.fpsNum = fps[0].toInt();
	v.fpsDen = fps[1].toInt();
	QString err;
	if (!m_studio->applyVideo(v, &err)) {
		m_tabs->setCurrentIndex(Video);
		QMessageBox::warning(this, windowTitle(), err);
		return;
	}
	m_studio->setRecordingSettings(recordingFromUi());
	m_studio->setStreamSettings(streamFromUi());
	if (m_key->text().trimmed() != m_studio->streamKey())
		m_studio->setStreamKey(m_key->text());

	const QStringList md = m_monitorDevice->currentData().toStringList();
	if (md.size() == 2 && md[0] != m_studio->profile().ui.monitoringDeviceId)
		m_studio->setMonitoringDevice(md[0], md[1]);

	QMap<QString, QString> keys;
	for (int i = 0; i < m_shortcutTable->rowCount(); ++i) {
		auto *e = qobject_cast<QKeySequenceEdit *>(m_shortcutTable->cellWidget(i, 2));
		keys.insert(m_shortcutTable->item(i, 0)->data(Qt::UserRole).toString(),
			    e->keySequence().toString(QKeySequence::PortableText));
	}
	ShortcutMap probe;
	probe.load(keys);
	m_studio->setShortcuts(probe.overrides());

	UiSettings u = m_studio->profile().ui;
	u.gridColumns = m_columns->value();
	u.thumbnailWidth = m_thumbWidth->value();
	u.thumbnailFps = m_thumbFps->value();
	u.hideFromCapture = m_hideCapture->isChecked();
	m_studio->setUiSettings(u);
	m_studio->saveNow();
	QDialog::accept();
}

} // namespace ms
