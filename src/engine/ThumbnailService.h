// Low-cost live thumbnails for the source grid.
//
// Runs inside the libobs render loop (graphics thread): each registered
// source is rendered into a small texture at a reduced rate, staged to CPU
// memory and published as a QImage. Readback is pipelined (the surface staged
// on one pass is mapped on the next one) so the GPU is never stalled. The
// program output itself is captured the same way after the main texture is
// rendered, which is how the exact on-air image can be inspected/verified.
#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>

#include <memory>

struct obs_source;
typedef struct obs_source obs_source_t;

namespace ms {

class ThumbnailService {
public:
	static constexpr const char *kProgramKey = "__program__";

	ThumbnailService();
	~ThumbnailService();

	void start();
	void stop();

	// width: thumbnail width in pixels (height follows 16:9); fps: update rate.
	void setQuality(int width, int fps);
	void add(const QString &key, obs_source_t *source);
	void remove(const QString &key);
	void setPaused(const QString &key, bool paused);
	void enableProgramCapture(bool on);

	// Latest image and its serial (increments on every new frame).
	QImage latest(const QString &key, quint64 *serial = nullptr) const;

private:
	struct Job;
	static void drawCallback(void *param, uint32_t cx, uint32_t cy);
	static void renderedCallback(void *param);
	void processJob(Job &job, bool program);
	static void disposeJob(const std::shared_ptr<Job> &job);

	mutable QMutex m_mutex;
	QHash<QString, std::shared_ptr<Job>> m_jobs;
	std::shared_ptr<Job> m_program;
	int m_width = 320;
	int m_height = 180;
	int m_interval = 3;
	bool m_started = false;
	unsigned m_frame = 0;
};

} // namespace ms
