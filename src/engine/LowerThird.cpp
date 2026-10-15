#include "LowerThird.h"

#include <algorithm>
#include <obs-module.h>
#include <obs.h>
#include <graphics/graphics.h>

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace ms {

// --------------------------------------------------------------------------
// Rasterization
// --------------------------------------------------------------------------

QString LowerThirdRenderer::defaultFontFamily()
{
#if defined(_WIN32)
	return QStringLiteral("Segoe UI");
#elif defined(__APPLE__)
	return QStringLiteral("Helvetica Neue");
#else
	const QStringList families = QFontDatabase::families();
	for (const char *f : {"Inter", "Noto Sans", "DejaVu Sans", "Liberation Sans"})
		if (families.contains(QLatin1String(f)))
			return QLatin1String(f);
	return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
#endif
}

namespace {

QFont makeFont(const QString &family, double px, int weight)
{
	QFont f(family.isEmpty() ? LowerThirdRenderer::defaultFontFamily() : family);
	f.setPixelSize(std::max(6, int(std::lround(px))));
	f.setWeight(QFont::Weight(weight));
	f.setHintingPreference(QFont::PreferNoHinting);
	f.setStyleStrategy(QFont::PreferAntialias);
	return f;
}

QString joinNonEmpty(const QStringList &parts, const QString &sep)
{
	QStringList kept;
	for (const auto &p : parts)
		if (!p.trimmed().isEmpty())
			kept.append(p.trimmed());
	return kept.join(sep);
}

QPoint placeOnCanvas(GraphicPosition pos, QSize box, QSize canvas)
{
	const int mx = int(canvas.width() * 0.055);  // title-safe margins
	const int mBottom = int(canvas.height() * 0.085);
	const int mTop = int(canvas.height() * 0.07);
	switch (pos) {
	case GraphicPosition::BottomLeft: return {mx, canvas.height() - mBottom - box.height()};
	case GraphicPosition::BottomCenter:
		return {(canvas.width() - box.width()) / 2, canvas.height() - mBottom - box.height()};
	case GraphicPosition::BottomRight:
		return {canvas.width() - mx - box.width(), canvas.height() - mBottom - box.height()};
	case GraphicPosition::TopLeft: return {mx, mTop};
	case GraphicPosition::TopRight: return {canvas.width() - mx - box.width(), mTop};
	case GraphicPosition::Center:
		return {(canvas.width() - box.width()) / 2, (canvas.height() - box.height()) / 2};
	}
	return {mx, canvas.height() - mBottom - box.height()};
}

} // namespace

RenderedGraphic LowerThirdRenderer::render(const LowerThirdConfig &c, QSize canvas)
{
	RenderedGraphic out;
	if (canvas.isEmpty())
		return out;
	const double H = canvas.height() * c.scale;
	const QString line2 = joinNonEmpty({c.role, c.company}, QStringLiteral("  ·  "));
	QImage logo;
	if (!c.logoPath.isEmpty())
		logo.load(c.logoPath);

	if (c.style == GraphicStyle::Title) {
		const QFont fTitle = makeFont(c.fontFamily, H * 0.075, 700);
		const QFont fSub = makeFont(c.fontFamily, H * 0.032, 500);
		const QFontMetricsF mt(fTitle), ms(fSub);
		const QString sub = joinNonEmpty({line2, c.secondary}, QStringLiteral("  ·  "));
		const double padX = H * 0.05, padY = H * 0.035;
		const double textW = std::max(mt.horizontalAdvance(c.name), sub.isEmpty() ? 0.0 : ms.horizontalAdvance(sub));
		const double w = std::min<double>(canvas.width() * 0.9, textW + padX * 2);
		const double rule = std::max(2.0, H * 0.006);
		const double h = padY * 2 + mt.height() + (sub.isEmpty() ? 0 : ms.height() + H * 0.012) + rule + H * 0.014;
		QImage img(int(std::ceil(w)), int(std::ceil(h)), QImage::Format_RGBA8888_Premultiplied);
		img.fill(Qt::transparent);
		QPainter p(&img);
		p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
		p.fillRect(QRectF(0, 0, w, h), c.plate);
		double y = padY;
		p.setFont(fTitle);
		p.setPen(c.text);
		p.drawText(QRectF(padX, y, w - 2 * padX, mt.height()), Qt::AlignHCenter | Qt::AlignVCenter,
			   mt.elidedText(c.name, Qt::ElideRight, w - 2 * padX));
		y += mt.height() + H * 0.014;
		const double ruleW = std::min(w * 0.18, H * 0.16);
		p.fillRect(QRectF((w - ruleW) / 2, y, ruleW, rule), c.accent);
		y += rule + H * 0.012;
		if (!sub.isEmpty()) {
			p.setFont(fSub);
			p.setPen(c.subText);
			p.drawText(QRectF(padX, y, w - 2 * padX, ms.height()), Qt::AlignHCenter | Qt::AlignVCenter,
				   ms.elidedText(sub, Qt::ElideRight, w - 2 * padX));
		}
		p.end();
		out.image = img;
	} else if (c.style == GraphicStyle::Band) {
		const double bandH = H * 0.085;
		const double w = canvas.width() * 0.89;
		const QFont fMain = makeFont(c.fontFamily, bandH * 0.42, 700);
		const QFont fTag = makeFont(c.fontFamily, bandH * 0.30, 800);
		const QFontMetricsF mm(fMain), mtag(fTag);
		const QString tag = c.secondary.trimmed().isEmpty() ? c.company.trimmed() : c.secondary.trimmed();
		const QString main = joinNonEmpty({c.name, c.role}, QStringLiteral(" — "));
		const double tagW = tag.isEmpty() ? bandH * 0.18 : mtag.horizontalAdvance(tag.toUpper()) + bandH * 0.7;
		QImage img(int(std::ceil(w)), int(std::ceil(bandH)), QImage::Format_RGBA8888_Premultiplied);
		img.fill(Qt::transparent);
		QPainter p(&img);
		p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
		p.fillRect(QRectF(0, 0, w, bandH), c.plate);
		p.fillRect(QRectF(0, 0, tagW, bandH), c.accent);
		if (!tag.isEmpty()) {
			p.setFont(fTag);
			p.setPen(QColor(255, 255, 255));
			p.drawText(QRectF(0, 0, tagW, bandH), Qt::AlignCenter, tag.toUpper());
		}
		double x = tagW + bandH * 0.4;
		if (!logo.isNull()) {
			const double lh = bandH * 0.62;
			const QImage l = logo.scaledToHeight(int(lh), Qt::SmoothTransformation);
			p.drawImage(QPointF(w - l.width() - bandH * 0.3, (bandH - l.height()) / 2), l);
		}
		p.setFont(fMain);
		p.setPen(c.text);
		p.drawText(QRectF(x, 0, w - x - bandH * 0.4, bandH), Qt::AlignVCenter | Qt::AlignLeft,
			   mm.elidedText(main, Qt::ElideRight, w - x - bandH * 0.4));
		p.end();
		out.image = img;
	} else { // LowerThird
		const QFont fName = makeFont(c.fontFamily, H * 0.044, 700);
		const QFont fRole = makeFont(c.fontFamily, H * 0.026, 500);
		const QFont fSec = makeFont(c.fontFamily, H * 0.021, 400);
		const QFontMetricsF mn(fName), mr(fRole), msec(fSec);
		const double padX = H * 0.028, padY = H * 0.016, gap = H * 0.004;
		const double accentW = std::max(3.0, H * 0.008);
		double textH = mn.height();
		if (!line2.isEmpty())
			textH += gap + mr.height();
		if (!c.secondary.trimmed().isEmpty())
			textH += gap + msec.height();
		const double h = textH + padY * 2;
		const double logoSize = logo.isNull() ? 0.0 : h - padY * 1.2;
		double textW = mn.horizontalAdvance(c.name);
		textW = std::max(textW, mr.horizontalAdvance(line2));
		textW = std::max(textW, msec.horizontalAdvance(c.secondary));
		double w = accentW + padX + (logoSize > 0 ? logoSize + padX * 0.8 : 0) + textW + padX * 1.4;
		w = std::clamp(w, canvas.width() * 0.2, canvas.width() * 0.89);
		QImage img(int(std::ceil(w)), int(std::ceil(h)), QImage::Format_RGBA8888_Premultiplied);
		img.fill(Qt::transparent);
		QPainter p(&img);
		p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
		p.fillRect(QRectF(0, 0, w, h), c.plate);
		p.fillRect(QRectF(0, 0, accentW, h), c.accent);
		double x = accentW + padX;
		if (logoSize > 0) {
			const QImage l = logo.scaled(int(logoSize), int(logoSize), Qt::KeepAspectRatio, Qt::SmoothTransformation);
			p.drawImage(QPointF(x + (logoSize - l.width()) / 2, (h - l.height()) / 2), l);
			x += logoSize + padX * 0.8;
		}
		const double avail = w - x - padX;
		double y = padY;
		p.setFont(fName);
		p.setPen(c.text);
		p.drawText(QRectF(x, y, avail, mn.height()), Qt::AlignLeft | Qt::AlignVCenter,
			   mn.elidedText(c.name, Qt::ElideRight, avail));
		y += mn.height() + gap;
		if (!line2.isEmpty()) {
			p.setFont(fRole);
			p.setPen(c.subText);
			p.drawText(QRectF(x, y, avail, mr.height()), Qt::AlignLeft | Qt::AlignVCenter,
				   mr.elidedText(line2, Qt::ElideRight, avail));
			y += mr.height() + gap;
		}
		if (!c.secondary.trimmed().isEmpty()) {
			p.setFont(fSec);
			p.setPen(c.subText);
			p.drawText(QRectF(x, y, avail, msec.height()), Qt::AlignLeft | Qt::AlignVCenter,
				   msec.elidedText(c.secondary, Qt::ElideRight, avail));
		}
		p.end();
		out.image = img;
	}
	out.placement = QRect(placeOnCanvas(c.style == GraphicStyle::Band && c.position != GraphicPosition::TopLeft &&
							    c.position != GraphicPosition::TopRight
						    ? GraphicPosition::BottomCenter
						    : c.position,
					    out.image.size(), canvas),
			      out.image.size());
	return out;
}

// --------------------------------------------------------------------------
// Controller (shared state)
// --------------------------------------------------------------------------

void GraphicController::setGraphic(const RenderedGraphic &g, GraphicAnimation in, GraphicAnimation out, int animMs,
				   double holdSeconds)
{
	std::lock_guard<std::mutex> l(m_mutex);
	m_pending = g;
	m_pending.image = g.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
	m_hasPending = true;
	m_in = in;
	m_out = out;
	m_animSec = std::max(0, animMs) / 1000.0f;
	m_holdSec = float(holdSeconds);
}

void GraphicController::show()
{
	m_request.store(1);
}

void GraphicController::hide()
{
	m_request.store(2);
}

static float easeOutCubic(float t)
{
	const float u = 1.0f - t;
	return 1.0f - u * u * u;
}

void GraphicController::tick(float seconds)
{
	const int req = m_request.exchange(0);
	GraphicPhase ph = GraphicPhase(m_phase.load());
	float animSec, holdSec;
	GraphicAnimation in, out;
	{
		std::lock_guard<std::mutex> l(m_mutex);
		animSec = m_animSec;
		holdSec = m_holdSec;
		in = m_in;
		out = m_out;
	}
	if (req == 1 && (ph == GraphicPhase::Hidden || ph == GraphicPhase::Out)) {
		// Reverse an ongoing exit from where it is.
		m_progress = (ph == GraphicPhase::Out) ? 1.0f - m_progress : 0.0f;
		ph = GraphicPhase::In;
		m_held = 0.0f;
		if (in == GraphicAnimation::Cut || animSec <= 0.0f)
			m_progress = 1.0f;
	} else if (req == 1 && ph == GraphicPhase::Hold) {
		m_held = 0.0f; // re-trigger restarts the hold timer
	} else if (req == 2 && (ph == GraphicPhase::In || ph == GraphicPhase::Hold)) {
		m_progress = (ph == GraphicPhase::In) ? 1.0f - m_progress : 0.0f;
		ph = GraphicPhase::Out;
		if (out == GraphicAnimation::Cut || animSec <= 0.0f)
			m_progress = 1.0f;
	}

	const float step = animSec > 0.0f ? seconds / animSec : 1.0f;
	switch (ph) {
	case GraphicPhase::In:
		m_progress = std::min(1.0f, m_progress + step);
		if (m_progress >= 1.0f) {
			ph = GraphicPhase::Hold;
			m_held = 0.0f;
		}
		break;
	case GraphicPhase::Hold:
		m_held += seconds;
		if (holdSec > 0.0f && m_held >= holdSec) {
			ph = GraphicPhase::Out;
			m_progress = (out == GraphicAnimation::Cut) ? 1.0f : 0.0f;
		}
		break;
	case GraphicPhase::Out:
		m_progress = std::min(1.0f, m_progress + step);
		if (m_progress >= 1.0f) {
			ph = GraphicPhase::Hidden;
			m_progress = 0.0f;
		}
		break;
	case GraphicPhase::Hidden: break;
	}
	m_phase.store(int(ph));
}

static const char *kEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float opacity;

sampler_state def_sampler {
	Filter   = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

struct VertInOut {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertInOut VSDefault(VertInOut vert_in)
{
	VertInOut vert_out;
	vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv  = vert_in.uv;
	return vert_out;
}

float4 PSDraw(VertInOut vert_in) : TARGET
{
	return image.Sample(def_sampler, vert_in.uv) * opacity;
}

technique Draw
{
	pass
	{
		vertex_shader = VSDefault(vert_in);
		pixel_shader  = PSDraw(vert_in);
	}
}
)";

void GraphicController::render()
{
	{
		std::lock_guard<std::mutex> l(m_mutex);
		if (m_hasPending) {
			m_hasPending = false;
			if (m_texture) {
				gs_texture_destroy(static_cast<gs_texture_t *>(m_texture));
				m_texture = nullptr;
			}
			if (!m_pending.image.isNull()) {
				const uint8_t *data = m_pending.image.constBits();
				m_texture = gs_texture_create(uint32_t(m_pending.image.width()),
							      uint32_t(m_pending.image.height()), GS_RGBA, 1, &data, 0);
			}
			m_place = m_pending.placement;
			m_pending = {};
		}
	}
	if (!m_effect) {
		char *errors = nullptr;
		m_effect = gs_effect_create(kEffect, "marshalls_graphic.effect", &errors);
		if (errors) {
			blog(LOG_ERROR, "[marshalls] graphic effect: %s", errors);
			bfree(errors);
		}
	}
	const GraphicPhase ph = GraphicPhase(m_phase.load());
	if (ph == GraphicPhase::Hidden || !m_texture || !m_effect)
		return;

	GraphicAnimation anim;
	{
		std::lock_guard<std::mutex> l(m_mutex);
		anim = (ph == GraphicPhase::Out) ? m_out : m_in;
	}
	float vis = 1.0f; // 0 = invisible, 1 = fully in
	if (ph == GraphicPhase::In)
		vis = easeOutCubic(m_progress);
	else if (ph == GraphicPhase::Out)
		vis = 1.0f - easeOutCubic(m_progress);

	float opacity = 1.0f, dx = 0.0f;
	uint32_t revealW = uint32_t(m_place.width());
	switch (anim) {
	case GraphicAnimation::Slide:
		opacity = vis;
		dx = -(1.0f - vis) * float(m_place.height()) * 0.9f;
		break;
	case GraphicAnimation::Wipe:
		revealW = uint32_t(std::max(1.0f, std::round(vis * float(m_place.width()))));
		break;
	case GraphicAnimation::Fade: opacity = vis; break;
	case GraphicAnimation::Cut: break;
	}
	if (opacity <= 0.001f)
		return;

	gs_effect_t *effect = static_cast<gs_effect_t *>(m_effect);
	gs_texture_t *tex = static_cast<gs_texture_t *>(m_texture);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "opacity"), opacity);

	const bool prevSrgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_matrix_push();
	gs_matrix_translate3f(float(m_place.x()) + dx, float(m_place.y()), 0.0f);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite_subregion(tex, 0, 0, 0, revealW, uint32_t(m_place.height()));
	gs_matrix_pop();
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(prevSrgb);
}

void GraphicController::destroyGraphics()
{
	obs_enter_graphics();
	if (m_texture)
		gs_texture_destroy(static_cast<gs_texture_t *>(m_texture));
	if (m_effect)
		gs_effect_destroy(static_cast<gs_effect_t *>(m_effect));
	obs_leave_graphics();
	m_texture = nullptr;
	m_effect = nullptr;
}

// --------------------------------------------------------------------------
// libobs source type
// --------------------------------------------------------------------------

namespace {

struct GraphicSourceData {
	GraphicController *controller = nullptr;
};

const char *gsGetName(void *)
{
	return "Marshalls Graphic";
}

void *gsCreate(obs_data_t *settings, obs_source_t *)
{
	auto *d = new GraphicSourceData;
	d->controller = static_cast<GraphicController *>(
		reinterpret_cast<void *>(uintptr_t(obs_data_get_int(settings, "controller"))));
	return d;
}

void gsDestroy(void *data)
{
	auto *d = static_cast<GraphicSourceData *>(data);
	if (d->controller)
		d->controller->destroyGraphics();
	delete d;
}

uint32_t gsWidth(void *)
{
	obs_video_info ovi;
	return obs_get_video_info(&ovi) ? ovi.base_width : 0;
}

uint32_t gsHeight(void *)
{
	obs_video_info ovi;
	return obs_get_video_info(&ovi) ? ovi.base_height : 0;
}

void gsTick(void *data, float seconds)
{
	auto *d = static_cast<GraphicSourceData *>(data);
	if (d->controller)
		d->controller->tick(seconds);
}

void gsRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<GraphicSourceData *>(data);
	if (d->controller)
		d->controller->render();
}

} // namespace

void registerGraphicSource()
{
	// Called after every obs_startup(): the type registry is reset on shutdown.
	obs_source_info info = {};
	info.id = kGraphicSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED;
	info.get_name = gsGetName;
	info.create = gsCreate;
	info.destroy = gsDestroy;
	info.get_width = gsWidth;
	info.get_height = gsHeight;
	info.video_tick = gsTick;
	info.video_render = gsRender;
	obs_register_source(&info);
}

obs_source_t *createGraphicSource(const QString &name, GraphicController *controller)
{
	obs_data_t *s = obs_data_create();
	obs_data_set_int(s, "controller", static_cast<long long>(reinterpret_cast<uintptr_t>(controller)));
	obs_source_t *src = obs_source_create_private(kGraphicSourceId, name.toUtf8().constData(), s);
	obs_data_release(s);
	return src;
}

} // namespace ms
