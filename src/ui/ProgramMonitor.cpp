#include "ProgramMonitor.h"

#include "Icons.h"
#include "Log.h"
#include "ObsPlatform.h"
#include "Studio.h"
#include "Theme.h"

#include <obs.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace ms {

ObsDisplay::ObsDisplay(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumSize(160, 90);
}

ObsDisplay::~ObsDisplay()
{
	destroyDisplay();
}

void ObsDisplay::paintEvent(QPaintEvent *)
{
	createDisplay();
}

void ObsDisplay::createDisplay()
{
	if (m_display || !isVisible() || width() <= 0 || height() <= 0)
		return;
	(void)winId(); // ensure the native window exists
	const qreal dpr = devicePixelRatioF();
	gs_init_data info = {};
	info.cx = uint32_t(width() * dpr);
	info.cy = uint32_t(height() * dpr);
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
#ifdef _WIN32
	info.window.hwnd = reinterpret_cast<HWND>(winId());
#elif defined(__APPLE__)
	info.window.view = reinterpret_cast<id>(winId());
#else
	info.window.id = uint32_t(winId());
	info.window.display = ObsPlatform::qtX11Display();
#endif
#if !defined(_WIN32) && !defined(__APPLE__)
	// libobs' X11/EGL backend sizes its swapchain child window from the parent X
	// window geometry at creation time (which may still be Qt's default) and only
	// reconfigures it when the requested size *changes*. Create it with a
	// provisional size so the real size below always triggers a resize.
	const uint32_t realCx = info.cx, realCy = info.cy;
	info.cx = info.cy = 16;
#endif
	m_display = obs_display_create(&info, 0xFF0B0C0E);
	if (m_display) {
#if !defined(_WIN32) && !defined(__APPLE__)
		info.cx = realCx;
		info.cy = realCy;
#endif
		obs_display_resize(m_display, info.cx, info.cy);
		obs_display_add_draw_callback(m_display, &ObsDisplay::draw, this);
		logInfo("ui", QStringLiteral("Monitor de programa criado (%1x%2)").arg(info.cx).arg(info.cy));
	} else {
		logError("ui", QStringLiteral("Falha ao criar o monitor de programa (obs_display_create)"));
	}
}

void ObsDisplay::destroyDisplay()
{
	if (!m_display)
		return;
	obs_display_remove_draw_callback(m_display, &ObsDisplay::draw, this);
	obs_display_destroy(m_display);
	m_display = nullptr;
}

void ObsDisplay::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	QTimer::singleShot(0, this, &ObsDisplay::createDisplay);
}

bool ObsDisplay::event(QEvent *e)
{
	if (e->type() == QEvent::Expose || e->type() == QEvent::UpdateRequest || e->type() == QEvent::Show)
		createDisplay();
	if (e->type() == QEvent::Hide)
		; // keep the display: re-creating swapchains is expensive
	return QWidget::event(e);
}

void ObsDisplay::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	createDisplay();
	if (m_display) {
		const qreal dpr = devicePixelRatioF();
		obs_display_resize(m_display, uint32_t(width() * dpr), uint32_t(height() * dpr));
	}
}

void ObsDisplay::draw(void *, uint32_t cx, uint32_t cy)
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || cx == 0 || cy == 0)
		return;
	const double scale = std::min(double(cx) / ovi.base_width, double(cy) / ovi.base_height);
	const int w = int(ovi.base_width * scale), h = int(ovi.base_height * scale);
	const int x = (int(cx) - w) / 2, y = (int(cy) - h) / 2;
	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
	gs_set_viewport(x, y, w, h);
	obs_render_main_texture_src_color_only();
	gs_projection_pop();
	gs_viewport_pop();
}

// ---------------------------------------------------------------------------

ProgramMonitor::ProgramMonitor(Studio *studio, QWidget *parent) : QWidget(parent), m_studio(studio)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	auto *header = new QWidget;
	header->setObjectName("PanelHeader");
	header->setFixedHeight(30);
	auto *h = new QHBoxLayout(header);
	h->setContentsMargins(10, 0, 8, 0);
	h->setSpacing(10);
	auto *title = new QLabel(QStringLiteral("PROGRAMA"));
	title->setObjectName("PanelTitle");
	h->addWidget(title);
	m_onAir = new QLabel;
	m_onAir->setTextFormat(Qt::RichText);
	h->addWidget(m_onAir, 1);
	m_progress = new QProgressBar;
	m_progress->setRange(0, 1000);
	m_progress->setTextVisible(false);
	m_progress->setFixedSize(120, 6);
	m_progress->setVisible(false);
	h->addWidget(m_progress);
	m_state = new QLabel;
	m_state->setTextFormat(Qt::RichText);
	h->addWidget(m_state);
	m_format = new QLabel;
	m_format->setFont(theme::monoFont(8));
	m_format->setStyleSheet(QStringLiteral("color:%1").arg(theme::hex(theme::text3)));
	h->addWidget(m_format);
	v->addWidget(header);

	m_frame = new QWidget;
	m_frame->setAutoFillBackground(true);
	auto *fl = new QVBoxLayout(m_frame);
	fl->setContentsMargins(2, 2, 2, 2);
	m_display = new ObsDisplay;
	fl->addWidget(m_display);
	v->addWidget(m_frame, 1);

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &ProgramMonitor::tick);
	timer->start(50);
	connect(studio, &Studio::programChanged, this, &ProgramMonitor::refresh);
	connect(studio, &Studio::ftbChanged, this, &ProgramMonitor::refresh);
	connect(studio, &Studio::transitionStateChanged, this, &ProgramMonitor::refresh);
	connect(studio, &Studio::recordingChanged, this, &ProgramMonitor::refresh);
	connect(studio, &Studio::streamingChanged, this, &ProgramMonitor::refresh);
	connect(studio, &Studio::settingsChanged, this, &ProgramMonitor::refresh);
	connect(studio, &Studio::contentChanged, this, &ProgramMonitor::refresh);
	refresh();
}

void ProgramMonitor::refresh()
{
	const auto &pc = m_studio->program();
	const QString name = m_studio->contentName(pc.program());
	QString html;
	if (pc.program().isValid())
		html = QStringLiteral("<span style='background:%1;color:white;font-weight:700;padding:1px 6px'>&nbsp;NO AR&nbsp;</span>"
				      "&nbsp;&nbsp;<b>%2</b>")
			       .arg(theme::hex(theme::program), name.toHtmlEscaped());
	else
		html = QStringLiteral("<span style='color:%1'>Nada no ar — selecione uma fonte e use CORTE ou AUTO</span>")
			       .arg(theme::hex(theme::text3));
	if (pc.isTransitioning())
		html += QStringLiteral("&nbsp;&nbsp;<span style='color:%1'>→ %2</span>")
				.arg(theme::hex(theme::transition), m_studio->contentName(pc.pendingTarget()).toHtmlEscaped());
	m_onAir->setText(html);

	QStringList states;
	if (pc.ftbState() != FtbState::Off)
		states << QStringLiteral("<span style='color:%1;font-weight:700'>%2</span>")
				  .arg(theme::hex(theme::transition),
				       pc.ftbState() == FtbState::Black ? QStringLiteral("PRETO (FTB)") : QStringLiteral("FTB…"));
	if (m_studio->isStreaming())
		states << QStringLiteral("<span style='color:%1;font-weight:700'>TRANSMITINDO</span>").arg(theme::hex(theme::program));
	if (m_studio->isRecording())
		states << QStringLiteral("<span style='color:%1;font-weight:700'>GRAVANDO</span>").arg(theme::hex(theme::recording));
	m_state->setText(states.join(QStringLiteral("&nbsp;&nbsp;")));

	const VideoSettings &v = m_studio->profile().video;
	m_format->setText(QStringLiteral("%1×%2 → %3×%4 · %5 fps")
				  .arg(v.baseWidth)
				  .arg(v.baseHeight)
				  .arg(v.outputWidth)
				  .arg(v.outputHeight)
				  .arg(double(v.fpsNum) / v.fpsDen, 0, 'g', 4));

	// Frame color: red while the output is live (anything leaving the machine).
	const bool live = m_studio->isStreaming() || m_studio->isRecording();
	QPalette p = m_frame->palette();
	p.setColor(QPalette::Window, live ? theme::program : theme::line);
	m_frame->setPalette(p);
}

void ProgramMonitor::tick()
{
	const bool tr = m_studio->program().isTransitioning();
	m_progress->setVisible(tr);
	if (tr)
		m_progress->setValue(int(m_studio->engine().transitionProgress() * 1000));
}

} // namespace ms
