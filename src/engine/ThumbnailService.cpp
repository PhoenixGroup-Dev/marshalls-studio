#include "ThumbnailService.h"

#include <algorithm>
#include <obs.h>
#include <graphics/graphics.h>

#include <QMutexLocker>
#include <vector>

namespace ms {

struct ThumbnailService::Job {
	QString key;
	obs_weak_source_t *weak = nullptr;
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stage = nullptr;
	uint32_t stageW = 0, stageH = 0;
	bool staged = false;
	bool paused = false;
	unsigned phase = 0;

	QMutex imageMutex;
	QImage image;
	quint64 serial = 0;

	void releaseGpu()
	{
		if (texrender)
			gs_texrender_destroy(texrender);
		if (stage)
			gs_stagesurface_destroy(stage);
		texrender = nullptr;
		stage = nullptr;
		staged = false;
	}
};

ThumbnailService::ThumbnailService() = default;

ThumbnailService::~ThumbnailService()
{
	stop();
}

void ThumbnailService::start()
{
	if (m_started)
		return;
	obs_add_main_render_callback(drawCallback, this);
	obs_add_main_rendered_callback(renderedCallback, this);
	m_started = true;
}

void ThumbnailService::stop()
{
	if (!m_started)
		return;
	obs_remove_main_render_callback(drawCallback, this);
	obs_remove_main_rendered_callback(renderedCallback, this);
	m_started = false;
	QHash<QString, std::shared_ptr<Job>> jobs;
	std::shared_ptr<Job> program;
	{
		QMutexLocker l(&m_mutex);
		jobs.swap(m_jobs);
		program.swap(m_program);
	}
	for (auto &j : jobs)
		disposeJob(j);
	if (program)
		disposeJob(program);
}

void ThumbnailService::setQuality(int width, int fps)
{
	obs_video_info ovi;
	const double baseFps = obs_get_video_info(&ovi) ? double(ovi.fps_num) / ovi.fps_den : 30.0;
	QMutexLocker l(&m_mutex);
	m_width = std::clamp(width, 64, 1920) & ~1;
	m_height = (m_width * 9 / 16) & ~1;
	m_interval = std::max(1, int(baseFps / std::clamp(fps, 1, 60) + 0.5));
}

void ThumbnailService::add(const QString &key, obs_source_t *source)
{
	auto job = std::make_shared<Job>();
	job->key = key;
	job->weak = obs_source_get_weak_source(source);
	std::shared_ptr<Job> old;
	{
		QMutexLocker l(&m_mutex);
		old = m_jobs.take(key);
		job->phase = unsigned(m_jobs.size());
		m_jobs.insert(key, job);
	}
	if (old)
		disposeJob(old);
}

// Never hold m_mutex while entering the graphics context: the render thread
// owns the context while it waits for m_mutex in the callbacks.
void ThumbnailService::disposeJob(const std::shared_ptr<Job> &job)
{
	obs_enter_graphics();
	job->releaseGpu();
	obs_leave_graphics();
	obs_weak_source_release(job->weak);
	job->weak = nullptr;
}

void ThumbnailService::remove(const QString &key)
{
	std::shared_ptr<Job> job;
	{
		QMutexLocker l(&m_mutex);
		job = m_jobs.take(key);
	}
	if (job)
		disposeJob(job);
}

void ThumbnailService::setPaused(const QString &key, bool paused)
{
	QMutexLocker l(&m_mutex);
	if (auto j = m_jobs.value(key))
		j->paused = paused;
}

void ThumbnailService::enableProgramCapture(bool on)
{
	std::shared_ptr<Job> old;
	{
		QMutexLocker l(&m_mutex);
		if (on && !m_program) {
			m_program = std::make_shared<Job>();
			m_program->key = QString::fromLatin1(kProgramKey);
		} else if (!on && m_program) {
			old.swap(m_program);
		}
	}
	if (old)
		disposeJob(old);
}

QImage ThumbnailService::latest(const QString &key, quint64 *serial) const
{
	std::shared_ptr<Job> job;
	{
		QMutexLocker l(&m_mutex);
		job = key == QLatin1String(kProgramKey) ? m_program : m_jobs.value(key);
	}
	if (!job) {
		if (serial)
			*serial = 0;
		return {};
	}
	QMutexLocker il(&job->imageMutex);
	if (serial)
		*serial = job->serial;
	return job->image;
}

void ThumbnailService::drawCallback(void *param, uint32_t, uint32_t)
{
	auto *self = static_cast<ThumbnailService *>(param);
	std::vector<std::shared_ptr<Job>> due;
	{
		QMutexLocker l(&self->m_mutex);
		++self->m_frame;
		for (auto &j : self->m_jobs) {
			// Spread jobs across frames so the cost per frame stays flat.
			if (!j->paused && (self->m_frame + j->phase) % unsigned(self->m_interval) == 0)
				due.push_back(j);
		}
	}
	for (auto &j : due)
		self->processJob(*j, false);
}

void ThumbnailService::renderedCallback(void *param)
{
	auto *self = static_cast<ThumbnailService *>(param);
	std::shared_ptr<Job> job;
	{
		QMutexLocker l(&self->m_mutex);
		if (self->m_program && self->m_frame % unsigned(self->m_interval) == 0)
			job = self->m_program;
	}
	if (job)
		self->processJob(*job, true);
}

void ThumbnailService::processJob(Job &job, bool program)
{
	int w, h;
	{
		QMutexLocker l(&m_mutex);
		w = m_width;
		h = m_height;
	}

	// 1) Collect the frame staged on the previous pass.
	if (job.staged && job.stage) {
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(job.stage, &data, &linesize)) {
			QImage img(int(job.stageW), int(job.stageH), QImage::Format_RGBX8888);
			for (uint32_t y = 0; y < job.stageH; ++y)
				memcpy(img.scanLine(int(y)), data + size_t(y) * linesize, size_t(job.stageW) * 4);
			gs_stagesurface_unmap(job.stage);
			QMutexLocker il(&job.imageMutex);
			job.image = img;
			++job.serial;
		}
		job.staged = false;
	}

	// 2) Render the current frame.
	uint32_t srcW = 0, srcH = 0;
	obs_source_t *src = nullptr;
	gs_texture_t *mainTex = nullptr;
	if (program) {
		obs_video_info ovi;
		if (!obs_get_video_info(&ovi))
			return;
		mainTex = obs_get_main_texture();
		if (!mainTex)
			return;
		srcW = ovi.base_width;
		srcH = ovi.base_height;
	} else {
		src = obs_weak_source_get_source(job.weak);
		if (!src)
			return;
		srcW = obs_source_get_width(src);
		srcH = obs_source_get_height(src);
	}

	if (!job.texrender)
		job.texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (!job.stage || job.stageW != uint32_t(w) || job.stageH != uint32_t(h)) {
		if (job.stage)
			gs_stagesurface_destroy(job.stage);
		job.stage = gs_stagesurface_create(uint32_t(w), uint32_t(h), GS_RGBA);
		job.stageW = uint32_t(w);
		job.stageH = uint32_t(h);
	}

	gs_texrender_reset(job.texrender);
	if (gs_texrender_begin(job.texrender, uint32_t(w), uint32_t(h))) {
		vec4 clear;
		vec4_zero(&clear);
		clear.w = 1.0f;
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		if (srcW > 0 && srcH > 0) {
			// Letterbox the source into the thumbnail.
			const double scale = std::min(double(w) / srcW, double(h) / srcH);
			const int vw = std::max(1, int(srcW * scale + 0.5));
			const int vh = std::max(1, int(srcH * scale + 0.5));
			gs_set_viewport((w - vw) / 2, (h - vh) / 2, vw, vh);
			gs_ortho(0.0f, float(srcW), 0.0f, float(srcH), -100.0f, 100.0f);
			gs_blend_state_push();
			gs_reset_blend_state();
			if (program) {
				gs_effect_t *eff = obs_get_base_effect(OBS_EFFECT_DEFAULT);
				gs_effect_set_texture(gs_effect_get_param_by_name(eff, "image"), mainTex);
				gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
				while (gs_effect_loop(eff, "Draw"))
					gs_draw_sprite(mainTex, 0, srcW, srcH);
			} else {
				obs_source_video_render(src);
			}
			gs_blend_state_pop();
		}
		gs_texrender_end(job.texrender);
		gs_stage_texture(job.stage, gs_texrender_get_texture(job.texrender));
		job.staged = true;
	}
	if (src)
		obs_source_release(src);
}

} // namespace ms
