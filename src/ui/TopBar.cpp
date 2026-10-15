#include "TopBar.h"

#include "Icons.h"
#include "Log.h"
#include "Studio.h"
#include "Theme.h"

#include <algorithm>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ms {

namespace {

QString clock(qint64 ms)
{
	const qint64 s = ms / 1000;
	return QStringLiteral("%1:%2:%3")
		.arg(s / 3600, 2, 10, QLatin1Char('0'))
		.arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(s % 60, 2, 10, QLatin1Char('0'));
}

QLabel *statLabel(const QString &tip)
{
	auto *l = new QLabel;
	l->setFont(theme::monoFont(8));
	l->setToolTip(tip);
	l->setTextFormat(Qt::RichText);
	return l;
}

QString stat(const QString &name, const QString &value, const QColor &c)
{
	return QStringLiteral("<span style='color:%1'>%2</span> <span style='color:%3'>%4</span>")
		.arg(theme::hex(theme::text3), name, theme::hex(c), value);
}

} // namespace

TopBar::TopBar(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
	setFixedHeight(48);
	auto *h = new QHBoxLayout(this);
	h->setContentsMargins(12, 0, 8, 0);
	h->setSpacing(14);

	auto *brand = new QLabel;
	brand->setTextFormat(Qt::RichText);
	brand->setText(QStringLiteral("<span style='font-weight:800;letter-spacing:2px;font-size:11pt;color:%1'>MARSHALLS</span>"
				      "<span style='font-weight:300;letter-spacing:2px;font-size:11pt;color:%2'>&nbsp;STUDIO</span>")
			       .arg(theme::hex(theme::text), theme::hex(theme::text2)));
	h->addWidget(brand);

	auto *sep = new QFrame;
	sep->setFrameShape(QFrame::VLine);
	sep->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::line)));
	h->addWidget(sep);

	auto *plabel = new QLabel(QStringLiteral("Perfil"));
	plabel->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
	h->addWidget(plabel);
	m_profile = new QComboBox;
	m_profile->setMinimumWidth(150);
	m_profile->setFocusPolicy(Qt::NoFocus);
	m_profile->setToolTip(QStringLiteral("Perfil de produção: fontes, cenas, gráficos, áudio e saídas"));
	h->addWidget(m_profile);
	connect(m_profile, &QComboBox::activated, this, [this](int idx) {
		if (m_updatingProfiles)
			return;
		const QString data = m_profile->itemData(idx).toString();
		if (data == QLatin1String("__new__")) {
			refreshProfiles();
			emit newProfileRequested();
		} else if (data != m_studio->profile().name) {
			emit profileSwitchRequested(data);
		}
	});
	h->addStretch(1);

	m_cpu = statLabel(QStringLiteral("Uso de CPU do processo"));
	m_mem = statLabel(QStringLiteral("Memória em uso pelo processo"));
	m_fps = statLabel(QStringLiteral("Quadros por segundo efetivamente renderizados"));
	m_render = statLabel(QStringLiteral("Quadros perdidos na composição (GPU sobrecarregada)"));
	m_encode = statLabel(QStringLiteral("Quadros descartados pelo codificador (CPU/codificador sobrecarregado)"));
	for (QLabel *l : {m_cpu, m_mem, m_fps, m_render, m_encode})
		h->addWidget(l);

	auto *sep2 = new QFrame;
	sep2->setFrameShape(QFrame::VLine);
	sep2->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::line)));
	h->addWidget(sep2);

	auto outputBlock = [&](QLabel *&time, QLabel *&info, QPushButton *&btn, const QString &text) {
		auto *col = new QVBoxLayout;
		col->setSpacing(0);
		time = new QLabel(QStringLiteral("--:--:--"));
		time->setFont(theme::monoFont(10, true));
		time->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
		info = new QLabel;
		info->setFont(theme::monoFont(7));
		info->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
		col->addWidget(time);
		col->addWidget(info);
		h->addLayout(col);
		btn = new QPushButton(text);
		btn->setFocusPolicy(Qt::NoFocus);
		btn->setMinimumWidth(118);
		btn->setMinimumHeight(30);
		h->addWidget(btn);
	};
	outputBlock(m_recTime, m_recInfo, m_rec, QStringLiteral("GRAVAR"));
	outputBlock(m_liveTime, m_liveInfo, m_live, QStringLiteral("TRANSMITIR"));
	connect(m_rec, &QPushButton::clicked, this, &TopBar::recordToggleRequested);
	connect(m_live, &QPushButton::clicked, this, &TopBar::streamToggleRequested);

	m_settings = new QToolButton;
	m_settings->setIcon(Icons::icon(Icon::Gear, theme::text2, 18));
	m_settings->setProperty("flat", true);
	m_settings->setPopupMode(QToolButton::InstantPopup);
	m_settings->setFocusPolicy(Qt::NoFocus);
	auto *menu = new QMenu(m_settings);
	const QStringList tabs = {QStringLiteral("Vídeo"), QStringLiteral("Gravação"), QStringLiteral("Transmissão"),
				  QStringLiteral("Áudio"), QStringLiteral("Atalhos"), QStringLiteral("Interface"),
				  QStringLiteral("Diagnóstico")};
	for (int i = 0; i < tabs.size(); ++i)
		connect(menu->addAction(tabs[i] + QStringLiteral("…")), &QAction::triggered, this,
			[this, i] { emit settingsRequested(i); });
	menu->addSeparator();
	connect(menu->addAction(Icons::icon(Icon::Folder), QStringLiteral("Abrir pasta de gravações")), &QAction::triggered,
		this, &TopBar::openRecordingsRequested);
	connect(menu->addAction(Icons::icon(Icon::Folder), QStringLiteral("Abrir pasta de logs")), &QAction::triggered, this,
		[] { QDesktopServices::openUrl(QUrl::fromLocalFile(Log::instance().directory())); });
	m_settings->setMenu(menu);
	h->addWidget(m_settings);

	connect(studio, &Studio::statsUpdated, this, &TopBar::updateStats);
	connect(studio, &Studio::recordingChanged, this, &TopBar::refreshOutputs);
	connect(studio, &Studio::streamingChanged, this, &TopBar::refreshOutputs);
	connect(studio, &Studio::streamReconnecting, this, &TopBar::refreshOutputs);
	connect(studio, &Studio::profileLoaded, this, &TopBar::refreshProfiles);
	refreshProfiles();
	refreshOutputs();
	updateStats({});
}

void TopBar::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), theme::bg0);
	p.fillRect(QRect(0, height() - 1, width(), 1), theme::line);
}

void TopBar::refreshProfiles()
{
	m_updatingProfiles = true;
	m_profile->clear();
	for (const QString &n : m_studio->profiles())
		m_profile->addItem(n, n);
	m_profile->insertSeparator(m_profile->count());
	m_profile->addItem(QStringLiteral("Novo perfil…"), QStringLiteral("__new__"));
	m_profile->setCurrentIndex(m_profile->findData(m_studio->profile().name));
	m_updatingProfiles = false;
}

void TopBar::refreshOutputs()
{
	const bool rec = m_studio->isRecording();
	const bool live = m_studio->isStreaming();
	m_rec->setText(rec ? QStringLiteral("PARAR GRAVAÇÃO") : QStringLiteral("GRAVAR"));
	m_rec->setIcon(Icons::icon(rec ? Icon::Stop : Icon::Record, rec ? QColor(255, 255, 255) : theme::recording, 14));
	m_rec->setStyleSheet(rec ? QStringLiteral("QPushButton{background:%1;border-color:%1;color:white;font-weight:700}")
					   .arg(theme::hex(theme::recording))
				 : QStringLiteral("QPushButton{font-weight:700}"));
	m_live->setText(live ? QStringLiteral("ENCERRAR") : QStringLiteral("TRANSMITIR"));
	m_live->setIcon(Icons::icon(live ? Icon::Stop : Icon::Broadcast, live ? QColor(255, 255, 255) : theme::program, 14));
	m_live->setStyleSheet(live ? QStringLiteral("QPushButton{background:%1;border-color:%1;color:white;font-weight:700}")
					     .arg(theme::hex(theme::program))
				   : QStringLiteral("QPushButton{font-weight:700}"));
	if (!rec) {
		m_recTime->setText(QStringLiteral("--:--:--"));
		m_recTime->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
		m_recInfo->setText(QStringLiteral("REC"));
	}
	if (!live) {
		m_liveTime->setText(QStringLiteral("--:--:--"));
		m_liveTime->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
		m_liveInfo->setText(QStringLiteral("AO VIVO"));
	}
}

void TopBar::updateStats(const OutputStats &s)
{
	m_last = s;
	auto level = [](double v, double warn, double bad) {
		return v >= bad ? theme::failure : v >= warn ? theme::warning : theme::text2;
	};
	m_cpu->setText(stat("CPU", QStringLiteral("%1%").arg(s.cpuPercent, 0, 'f', 0), level(s.cpuPercent, 70, 90)));
	m_mem->setText(stat("RAM", QStringLiteral("%1 MB").arg(s.memoryMb, 0, 'f', 0), level(s.memoryMb, 3000, 6000)));
	const double target = double(m_studio->profile().video.fpsNum) / m_studio->profile().video.fpsDen;
	m_fps->setText(stat("FPS", QString::number(s.activeFps, 'f', 1),
			    s.activeFps > 0 && s.activeFps < target * 0.95 ? theme::warning : theme::text2));
	m_render->setText(stat("COMP", QStringLiteral("%1%").arg(s.renderLagPercent, 0, 'f', 1), level(s.renderLagPercent, 1, 5)));
	m_encode->setText(stat("COD", QStringLiteral("%1%").arg(s.encodeSkipPercent, 0, 'f', 1), level(s.encodeSkipPercent, 1, 5)));

	if (s.recording) {
		m_recTime->setText(clock(s.recordingMs));
		m_recTime->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::recording)));
		QString info = QStringLiteral("%1 MB").arg(s.recordingBytes / (1024 * 1024));
		if (s.diskMinutesLeft > 0)
			info += QStringLiteral(" · disco ~%1 min").arg(int(s.diskMinutesLeft));
		m_recInfo->setText(info);
		m_recInfo->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(s.diskMinutesLeft < 30 ? theme::warning : theme::text3)));
	}
	if (s.streaming) {
		m_liveTime->setText(s.reconnecting ? QStringLiteral("RECONECT.") : clock(s.streamingMs));
		m_liveTime->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(s.reconnecting ? theme::warning : theme::program)));
		QString info = QStringLiteral("%1 kb/s · perdidos %2 (%3%)")
				       .arg(int(s.streamKbps))
				       .arg(s.streamDropped)
				       .arg(s.streamDropPercent, 0, 'f', 1);
		if (s.delaySec > 0)
			info += QStringLiteral(" · atraso %1 s").arg(s.delaySec);
		m_liveInfo->setText(info);
		m_liveInfo->setStyleSheet(QStringLiteral("color:%1").arg(
			theme::hex(s.streamDropPercent > 2 || s.congestion > 0.5 ? theme::warning : theme::text3)));
	}
}

// ---------------------------------------------------------------------------

AlertBar::AlertBar(Studio *studio, QWidget *parent) : QWidget(parent)
{
	setFixedHeight(24);
	setAutoFillBackground(true);
	QPalette p = palette();
	p.setColor(QPalette::Window, theme::bg0);
	setPalette(p);
	auto *h = new QHBoxLayout(this);
	h->setContentsMargins(10, 0, 6, 0);
	h->setSpacing(6);
	m_icon = new QLabel;
	h->addWidget(m_icon);
	m_text = new QLabel(QStringLiteral("Pronto."));
	m_text->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text2)));
	h->addWidget(m_text, 1);
	m_history = new QToolButton;
	m_history->setText(QStringLiteral("Histórico"));
	m_history->setProperty("flat", true);
	m_history->setFocusPolicy(Qt::NoFocus);
	connect(m_history, &QToolButton::clicked, this, [this] {
		QMenu menu;
		const int n = int(m_entries.size());
		for (int i = n - 1; i >= std::max(0, n - 25); --i)
			menu.addAction(m_entries[i])->setEnabled(false);
		if (m_entries.isEmpty())
			menu.addAction(QStringLiteral("Sem eventos"))->setEnabled(false);
		menu.exec(m_history->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
	});
	h->addWidget(m_history);
	connect(studio, &Studio::alert, this, &AlertBar::push);
}

void AlertBar::push(int level, const QString &message)
{
	const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
	m_entries.append(QStringLiteral("%1  %2").arg(ts, message));
	if (m_entries.size() > 500)
		m_entries.removeFirst();
	const QColor c = level >= 2 ? theme::failure : level == 1 ? theme::warning : theme::text2;
	m_icon->setPixmap(level >= 1 ? Icons::pixmap(Icon::Warning, c, 14, devicePixelRatioF()) : QPixmap());
	m_text->setText(QStringLiteral("%1  %2").arg(ts, message));
	m_text->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(c)));
}

} // namespace ms
