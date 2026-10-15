// Broadcast graphics: lower thirds, bands and titles.
//
// LowerThirdRenderer rasterizes a template with Qt (typography, logo, colors)
// into a premultiplied RGBA image. The image is handed to a custom libobs
// source ("marshalls_graphic") that composites it on the program and runs the
// in/hold/out animation frame-accurately on the libobs video thread, so the
// animation is part of the real output and never depends on the UI thread.
#pragma once

#include "StudioConfig.h"

#include <QImage>
#include <QRect>
#include <QSize>

#include <atomic>
#include <mutex>

struct obs_source;
typedef struct obs_source obs_source_t;

namespace ms {

struct RenderedGraphic {
	QImage image;    // premultiplied RGBA8888, size == placement.size()
	QRect placement; // position on the canvas, in canvas pixels
};

class LowerThirdRenderer {
public:
	static RenderedGraphic render(const LowerThirdConfig &cfg, QSize canvas);
	static QString defaultFontFamily();
};

enum class GraphicPhase { Hidden = 0, In = 1, Hold = 2, Out = 3 };

// Shared state between the studio (UI thread) and the libobs source.
class GraphicController {
public:
	void setGraphic(const RenderedGraphic &g, GraphicAnimation in, GraphicAnimation out, int animMs,
			double holdSeconds);
	void show();
	void hide();
	GraphicPhase phase() const { return GraphicPhase(m_phase.load()); }
	bool isOnAir() const { return phase() != GraphicPhase::Hidden; }

	// ---- called by the libobs source (graphics/video thread) ----
	struct Frame;
	void tick(float seconds);
	void render();
	void destroyGraphics();

private:
	std::mutex m_mutex;
	RenderedGraphic m_pending;
	bool m_hasPending = false;
	GraphicAnimation m_in = GraphicAnimation::Slide, m_out = GraphicAnimation::Slide;
	float m_animSec = 0.45f;
	float m_holdSec = 0.0f;
	std::atomic<int> m_request{0}; // 1 show, 2 hide
	std::atomic<int> m_phase{0};

	// video thread only
	void *m_texture = nullptr; // gs_texture_t*
	void *m_effect = nullptr;  // gs_effect_t*
	QRect m_place;
	float m_progress = 0.0f; // 0..1 within In/Out
	float m_held = 0.0f;
};

// Registers the "marshalls_graphic" source type (call once after obs_startup).
void registerGraphicSource();
// Creates a private graphic source driven by `controller` (not owned).
obs_source_t *createGraphicSource(const QString &name, GraphicController *controller);

constexpr const char *kGraphicSourceId = "marshalls_graphic";

} // namespace ms
