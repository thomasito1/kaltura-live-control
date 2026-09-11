#include "HealthMonitor.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QDateTime>
#include <QTimer>

namespace obsbridge {

Severity gradeDropped(double percent)
{
	if (percent > 1.0)
		return Severity::Critical;
	if (percent > 0.1)
		return Severity::Warn;
	return Severity::Ok;
}

Severity gradeCongestion(float congestion)
{
	if (congestion > 0.7f)
		return Severity::Critical;
	if (congestion > 0.3f)
		return Severity::Warn;
	return Severity::Ok;
}

Severity gradeSkippedOrLagged(double percent)
{
	return percent > 1.0 ? Severity::Warn : Severity::Ok;
}

HealthMonitor::HealthMonitor(QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
	connect(m_timer, &QTimer::timeout, this, &HealthMonitor::poll);
}

bool HealthMonitor::isRunning() const
{
	return m_timer->isActive();
}

void HealthMonitor::start(int intervalMs)
{
	m_lastBytes = 0;
	m_lastSampleMs = 0;
	m_timer->start(intervalMs);
}

void HealthMonitor::stop()
{
	m_timer->stop();
}

void HealthMonitor::poll()
{
	HealthSample s;

	/* obs_frontend_get_streaming_output() returns an incremented reference. */
	obs_output_t *out = obs_frontend_get_streaming_output();
	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

	if (out) {
		s.streaming = obs_output_active(out);

		s.droppedFrames = obs_output_get_frames_dropped(out);
		s.totalFrames = obs_output_get_total_frames(out);
		if (s.totalFrames > 0)
			s.droppedPercent =
				(100.0 * s.droppedFrames) / static_cast<double>(s.totalFrames);

		s.congestion = obs_output_get_congestion(out);
		s.connectTimeMs = obs_output_get_connect_time_ms(out);

		/* Actual send bitrate is a delta, so the first sample has none. */
		const quint64 bytes = obs_output_get_total_bytes(out);
		if (m_lastSampleMs > 0 && bytes >= m_lastBytes) {
			const double seconds = (nowMs - m_lastSampleMs) / 1000.0;
			if (seconds > 0.0)
				s.sendKbps = ((bytes - m_lastBytes) * 8.0) / 1000.0 / seconds;
		}
		m_lastBytes = bytes;

		obs_output_release(out);
	} else {
		m_lastBytes = 0;
	}

	m_lastSampleMs = nowMs;

	/* Encoder overload: frames the encoder could not keep up with. */
	if (video_t *video = obs_get_video()) {
		s.skippedFrames = video_output_get_skipped_frames(video);
		s.totalEncoded = video_output_get_total_frames(video);
		if (s.totalEncoded > 0)
			s.skippedPercent =
				(100.0 * s.skippedFrames) / static_cast<double>(s.totalEncoded);
	}

	/* Render overload: frames the GPU could not draw - usually too many sources. */
	s.laggedFrames = obs_get_lagged_frames();
	s.totalRendered = obs_get_total_frames();
	if (s.totalRendered > 0)
		s.laggedPercent = (100.0 * s.laggedFrames) / static_cast<double>(s.totalRendered);

	emit sampled(s);
}

} // namespace obsbridge
