#include "AudioMixer.h"

#include "Icons.h"
#include "Planning.h"
#include "Studio.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace ms {

namespace {

constexpr float kMinDb = -60.0f;

double dbToY(float db, double top, double height)
{
	const double f = std::clamp((double(db) - kMinDb) / (0.0 - kMinDb), 0.0, 1.0);
	return top + height * (1.0 - f);
}

QToolButton *smallToggle(const QString &text, const QString &tip)
{
	auto *b = new QToolButton;
	b->setText(text);
	b->setToolTip(tip);
	b->setFocusPolicy(Qt::NoFocus);
	b->setFixedSize(26, 22);
	b->setIconSize(QSize(14, 14));
	b->setStyleSheet(QStringLiteral("QToolButton{padding:0px;}"));
	return b;
}

} // namespace

// ---------------------------------------------------------------------------

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(10);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	m_holdClock.start();
}

void LevelMeter::setLevel(float mag, float peak)
{
	if (peak >= m_hold || m_holdClock.elapsed() > 1500) {
		m_hold = peak;
		m_holdClock.restart();
	}
	// Meter ballistics: fast attack, ~20 dB/s release.
	m_mag = mag > m_mag ? mag : std::max(mag, m_mag - 1.2f);
	m_peak = peak > m_peak ? peak : std::max(peak, m_peak - 1.2f);
	update();
}

void LevelMeter::setMuted(bool m)
{
	m_muted = m;
	update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	const QRectF r = rect().adjusted(0, 1, 0, -1);
	p.fillRect(rect(), theme::bg0);
	const double top = r.top(), h = r.height();
	auto zone = [&](float from, float to, QColor c, float level) {
		if (level <= from)
			return;
		const float hi = std::min(level, to);
		const double y1 = dbToY(hi, top, h), y2 = dbToY(from, top, h);
		if (m_muted)
			c = theme::text3;
		p.fillRect(QRectF(r.left(), y1, r.width(), y2 - y1), c);
	};
	// Dim background zones.
	auto dim = [&](float from, float to, QColor c) {
		c.setAlpha(28);
		p.fillRect(QRectF(r.left(), dbToY(to, top, h), r.width(), dbToY(from, top, h) - dbToY(to, top, h)), c);
	};
	dim(kMinDb, -20, theme::meterLow);
	dim(-20, -9, theme::meterMid);
	dim(-9, 0, theme::meterHigh);
	// Peak (light) and magnitude (solid).
	const float pk = m_peak;
	QColor lo = theme::meterLow, mid = theme::meterMid, hi = theme::meterHigh;
	lo.setAlpha(110);
	mid.setAlpha(110);
	hi.setAlpha(110);
	zone(kMinDb, -20, lo, pk);
	zone(-20, -9, mid, pk);
	zone(-9, 0, hi, pk);
	zone(kMinDb, -20, theme::meterLow, m_mag);
	zone(-20, -9, theme::meterMid, m_mag);
	zone(-9, 0, theme::meterHigh, m_mag);
	if (m_hold > kMinDb) {
		const double y = dbToY(m_hold, top, h);
		p.fillRect(QRectF(r.left(), y, r.width(), 2), m_hold > -9 ? theme::meterHigh : theme::text);
	}
	// Ticks.
	p.setPen(QColor(0, 0, 0, 140));
	for (float db : {-6.0f, -12.0f, -20.0f, -30.0f, -40.0f, -50.0f})
		p.drawLine(QPointF(r.left(), dbToY(db, top, h)), QPointF(r.right(), dbToY(db, top, h)));
}

// ---------------------------------------------------------------------------

ChannelStrip::ChannelStrip(Studio *studio, const QString &uid, int index, QWidget *parent)
	: QWidget(parent), m_studio(studio), m_uid(uid)
{
	setFixedWidth(92);
	setAutoFillBackground(true);
	QPalette pal = palette();
	pal.setColor(QPalette::Window, theme::bg1);
	setPalette(pal);

	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(5, 5, 5, 5);
	v->setSpacing(4);

	auto *top = new QHBoxLayout;
	top->setSpacing(4);
	m_index = new QLabel(index <= 8 ? QString::number(index) : QString());
	m_index->setFont(theme::monoFont(8, true));
	m_index->setFixedWidth(12);
	m_index->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
	m_index->setToolTip(index <= 8 ? QStringLiteral("Mute: F%1").arg(index) : QString());
	top->addWidget(m_index);
	m_name = new QLabel;
	m_name->setFont(theme::uiFont(8, 700));
	top->addWidget(m_name, 1);
	v->addLayout(top);

	m_status = new QLabel;
	m_status->setFont(theme::uiFont(7, 700));
	m_status->setAlignment(Qt::AlignCenter);
	m_status->setFixedHeight(14);
	v->addWidget(m_status);

	auto *mid = new QHBoxLayout;
	mid->setSpacing(4);
	m_meter = new LevelMeter;
	m_meter->setFixedWidth(12);
	mid->addWidget(m_meter);
	m_fader = new QSlider(Qt::Vertical);
	m_fader->setRange(0, 1000);
	m_fader->setFocusPolicy(Qt::NoFocus);
	m_fader->setToolTip(QStringLiteral("Nível enviado ao programa (duplo clique no valor: 0 dB)"));
	m_fader->setStyleSheet(QStringLiteral("QSlider::groove:vertical{width:4px;background:%1;}"
					      "QSlider::handle:vertical{height:22px;width:26px;margin:0 -11px;background:%2;"
					      "border:1px solid %3;border-radius:1px;}"
					      "QSlider::add-page:vertical{background:%4;width:4px;}")
				       .arg(theme::hex(theme::bg0), theme::hex(theme::bg4), theme::hex(theme::lineStrong),
					    theme::hex(theme::focus)));
	mid->addWidget(m_fader, 1, Qt::AlignHCenter);
	v->addLayout(mid, 1);

	m_db = new QLabel;
	m_db->setFont(theme::monoFont(8));
	m_db->setAlignment(Qt::AlignCenter);
	v->addWidget(m_db);

	auto *btns = new QHBoxLayout;
	btns->setSpacing(2);
	m_mute = smallToggle({}, QStringLiteral("Silenciar este canal"));
	m_mute->setCheckable(true);
	m_monitor = smallToggle({}, QStringLiteral("Monitoramento (fones): desligado / só monitorar / monitorar e enviar"));
	m_fx = smallToggle({}, QStringLiteral("Dispositivo, sincronização, filtros e roteamento"));
	m_fx->setIcon(Icons::icon(Icon::Sliders, theme::text2, 14));
	btns->addWidget(m_mute);
	btns->addWidget(m_monitor);
	btns->addWidget(m_fx);
	v->addLayout(btns);
	m_reconnect = new QToolButton;
	m_reconnect->setText(QStringLiteral("Reconectar"));
	m_reconnect->setFocusPolicy(Qt::NoFocus);
	m_reconnect->setVisible(false);
	v->addWidget(m_reconnect);

	connect(m_fader, &QSlider::valueChanged, this, [this](int v) {
		if (m_updating)
			return;
		m_studio->setVolumeDb(m_uid, faderToDb(v / 1000.0));
		refresh();
	});
	connect(m_mute, &QToolButton::clicked, this, [this] { m_studio->toggleMute(m_uid); });
	connect(m_monitor, &QToolButton::clicked, this, [this] {
		const SourceConfig *s = m_studio->profile().findSource(m_uid);
		if (!s)
			return;
		const MonitorMode next = s->audio.monitor == MonitorMode::Off ? MonitorMode::MonitorOnly
					 : s->audio.monitor == MonitorMode::MonitorOnly ? MonitorMode::MonitorAndOutput
											: MonitorMode::Off;
		m_studio->setMonitor(m_uid, next);
	});
	connect(m_fx, &QToolButton::clicked, this, [this] { emit settingsRequested(m_uid); });
	connect(m_reconnect, &QToolButton::clicked, this, [this] { m_studio->engine().reconnectSource(m_uid); });
	refresh();
}

void ChannelStrip::refresh()
{
	const SourceConfig *s = m_studio->profile().findSource(m_uid);
	if (!s)
		return;
	m_updating = true;
	m_name->setText(QFontMetrics(m_name->font()).elidedText(s->name, Qt::ElideRight, 64));
	m_name->setToolTip(QStringLiteral("%1\n%2").arg(s->name, s->device.name.isEmpty() ? kindLabel(s->kind) : s->device.name));
	m_fader->setValue(int(dbToFader(s->audio.volumeDb) * 1000));
	m_db->setText(s->audio.volumeDb <= -100 ? QStringLiteral("-∞ dB")
						: QStringLiteral("%1 dB").arg(s->audio.volumeDb, 0, 'f', 1));
	m_mute->setChecked(s->audio.muted);
	m_mute->setIcon(Icons::icon(s->audio.muted ? Icon::MicOff : Icon::Mic, s->audio.muted ? QColor(255, 255, 255) : theme::text2, 14));
	m_mute->setStyleSheet(s->audio.muted ? QStringLiteral("QToolButton{padding:0px;background:%1;border-color:%1;}").arg(theme::hex(theme::program))
					     : QStringLiteral("QToolButton{padding:0px;}"));
	const bool mon = s->audio.monitor != MonitorMode::Off;
	m_monitor->setIcon(Icons::icon(Icon::Headphones, mon ? QColor(0, 0, 0) : theme::text2, 14));
	m_monitor->setStyleSheet(mon ? QStringLiteral("QToolButton{padding:0px;background:%1;border-color:%1;}").arg(theme::hex(theme::focus))
				     : QStringLiteral("QToolButton{padding:0px;}"));
	m_monitor->setToolTip(s->audio.monitor == MonitorMode::Off ? QStringLiteral("Monitoramento: desligado")
			      : s->audio.monitor == MonitorMode::MonitorOnly
				      ? QStringLiteral("Monitoramento: só nos fones (não vai para o programa)")
				      : QStringLiteral("Monitoramento: fones + programa"));
	m_meter->setMuted(s->audio.muted);
	m_updating = false;
	tick();
}

void ChannelStrip::tick()
{
	const AudioLevel l = m_studio->engine().audioLevel(m_uid);
	m_meter->setLevel(l.magnitudeDb, l.peakDb);
	// "No signal" only after 1.5 s continuously below -60 dB (meter windows can be empty).
	if (l.peakDb >= -60)
		m_silence.invalidate();
	else if (!m_silence.isValid())
		m_silence.start();
	const SourceRuntime rt = m_studio->engine().runtime(m_uid);
	const SourceConfig *s = m_studio->profile().findSource(m_uid);
	QString text;
	QColor c = theme::text3;
	switch (rt.health) {
	case SourceHealth::Ok:
		if (s && s->audio.muted) {
			text = QStringLiteral("MUDO");
			c = theme::program;
		} else if (s && s->kind == SourceKind::Media && !rt.active) {
			text = QStringLiteral("FORA DO AR");
		} else if (m_silence.isValid() && m_silence.elapsed() > 1500) {
			text = QStringLiteral("SEM SINAL");
			c = theme::text3;
		} else if (l.peakDb > -1.0f) {
			text = QStringLiteral("CLIP");
			c = theme::meterHigh;
		} else {
			text = QStringLiteral("ATIVO");
			c = theme::ok;
		}
		break;
	case SourceHealth::Starting: text = QStringLiteral("INICIANDO"); break;
	case SourceHealth::NoSignal:
		text = QStringLiteral("SEM DADOS");
		c = theme::warning;
		break;
	case SourceHealth::Disconnected:
		text = QStringLiteral("DESCONECTADO");
		c = theme::failure;
		break;
	default:
		text = QStringLiteral("ERRO");
		c = theme::failure;
	}
	if (m_status->text() != text) {
		m_status->setText(text);
		m_status->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(c)));
		m_status->setToolTip(rt.detail);
	}
	m_reconnect->setVisible(rt.health == SourceHealth::NoSignal || rt.health == SourceHealth::Disconnected);
}

// ---------------------------------------------------------------------------

AudioMixer::AudioMixer(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	auto *header = new QWidget;
	header->setObjectName("PanelHeader");
	header->setFixedHeight(30);
	auto *h = new QHBoxLayout(header);
	h->setContentsMargins(10, 0, 6, 0);
	auto *title = new QLabel(QStringLiteral("ÁUDIO"));
	title->setObjectName("PanelTitle");
	h->addWidget(title);
	m_warning = new QLabel;
	m_warning->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::warning)));
	h->addWidget(m_warning, 1);
	auto *add = new QToolButton;
	add->setIcon(Icons::icon(Icon::Plus));
	add->setText(QStringLiteral("Entrada"));
	add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	add->setPopupMode(QToolButton::InstantPopup);
	add->setFocusPolicy(Qt::NoFocus);
	auto *menu = new QMenu(add);
	connect(menu->addAction(Icons::icon(Icon::Mic), QStringLiteral("Microfone / entrada de áudio…")), &QAction::triggered,
		this, [this] { emit addRequested(int(SourceKind::AudioInput)); });
	connect(menu->addAction(Icons::icon(Icon::Speaker), QStringLiteral("Áudio do computador…")), &QAction::triggered,
		this, [this] { emit addRequested(int(SourceKind::AudioOutput)); });
	add->setMenu(menu);
	h->addWidget(add);
	v->addWidget(header);

	auto *body = new QHBoxLayout;
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(0);
	auto *scroll = new QScrollArea;
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_host = new QWidget;
	m_strips = new QHBoxLayout(m_host);
	m_strips->setContentsMargins(4, 4, 4, 4);
	m_strips->setSpacing(4);
	scroll->setWidget(m_host);
	body->addWidget(scroll, 1);

	// Program master.
	auto *master = new QWidget;
	master->setFixedWidth(64);
	master->setAutoFillBackground(true);
	QPalette pal = master->palette();
	pal.setColor(QPalette::Window, theme::bg2);
	master->setPalette(pal);
	auto *mv = new QVBoxLayout(master);
	mv->setContentsMargins(6, 6, 6, 6);
	auto *mt = new QLabel(QStringLiteral("PGM"));
	mt->setFont(theme::uiFont(8, 800));
	mt->setAlignment(Qt::AlignCenter);
	mt->setToolTip(QStringLiteral("Mix do programa: exatamente o áudio gravado e transmitido"));
	mv->addWidget(mt);
	m_master = new LevelMeter;
	m_master->setFixedWidth(18);
	mv->addWidget(m_master, 1, Qt::AlignHCenter);
	m_masterDb = new QLabel;
	m_masterDb->setFont(theme::monoFont(8));
	m_masterDb->setAlignment(Qt::AlignCenter);
	mv->addWidget(m_masterDb);
	body->addWidget(master);
	v->addLayout(body, 1);

	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, &AudioMixer::tick);
	m_timer->start(33);
	connect(studio, &Studio::contentChanged, this, &AudioMixer::rebuild);
	connect(studio, &Studio::profileLoaded, this, &AudioMixer::rebuild);
	connect(studio, &Studio::sourceChanged, this, [this](const QString &uid) {
		for (auto *s : m_list)
			if (s->uid() == uid)
				s->refresh();
		const QStringList w = m_studio->audioWarnings();
		m_warning->setText(w.isEmpty() ? QString() : QStringLiteral("  Atenção: %1").arg(w.first()));
		m_warning->setToolTip(w.join("\n"));
	});
	rebuild();
}

void AudioMixer::rebuild()
{
	qDeleteAll(m_list);
	m_list.clear();
	while (QLayoutItem *it = m_strips->takeAt(0))
		delete it;
	int i = 0;
	for (const QString &uid : m_studio->audioChannels()) {
		auto *s = new ChannelStrip(m_studio, uid, ++i);
		connect(s, &ChannelStrip::settingsRequested, this, &AudioMixer::settingsRequested);
		m_strips->addWidget(s);
		m_list.append(s);
	}
	if (m_list.isEmpty()) {
		auto *l = new QLabel(QStringLiteral("Nenhuma entrada de áudio.\nUse “Entrada” para adicionar microfones."));
		l->setStyleSheet(QStringLiteral("color:%1;padding:10px").arg(theme::hex(theme::text3)));
		m_strips->addWidget(l);
	}
	m_strips->addStretch(1);
	const QStringList w = m_studio->audioWarnings();
	m_warning->setText(w.isEmpty() ? QString() : QStringLiteral("  Atenção: %1").arg(w.first()));
	m_warning->setToolTip(w.join("\n"));
}

void AudioMixer::tick()
{
	for (auto *s : m_list)
		s->tick();
	const AudioLevel pl = m_studio->engine().programLevel();
	m_master->setLevel(pl.magnitudeDb, pl.peakDb);
	m_masterDb->setText(pl.peakDb <= -99 ? QStringLiteral("-∞") : QString::number(pl.peakDb, 'f', 1));
}

} // namespace ms
