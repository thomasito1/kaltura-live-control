/*
 * Encoder-side stream health (spec section 11a).
 *
 * The distinction this exists to surface, which OBS's own stats dialog buries:
 *
 *   dropped frames -> the NETWORK cannot keep up
 *   skipped frames -> the CPU/GPU cannot ENCODE fast enough
 *   lagged frames  -> the GPU cannot RENDER the scene
 *
 * Engineers routinely misdiagnose an encoder problem as a network problem. The UI
 * must label these three separately and never merge them into one "dropped" number.
 *
 * No Kaltura dependency, so this works on any machine, VPN or not.
 */
#pragma once

#include <QObject>

class QTimer;

namespace obsbridge {

struct HealthSample {
	bool streaming = false;

	/* Network */
	int droppedFrames = 0;
	int totalFrames = 0;
	double droppedPercent = 0.0;
	float congestion = 0.0f;
	int connectTimeMs = 0;
	double sendKbps = 0.0;

	/* Encoder overload */
	unsigned int skippedFrames = 0;
	unsigned int totalEncoded = 0;
	double skippedPercent = 0.0;

	/* Render overload */
	unsigned int laggedFrames = 0;
	unsigned int totalRendered = 0;
	double laggedPercent = 0.0;
};

enum class Severity { Ok, Warn, Critical };

/* Spec section 11a thresholds. Tune against real events. */
Severity gradeDropped(double percent);
Severity gradeCongestion(float congestion);
Severity gradeSkippedOrLagged(double percent);

class HealthMonitor : public QObject {
	Q_OBJECT

public:
	explicit HealthMonitor(QObject *parent = nullptr);

	void start(int intervalMs = 1000);
	void stop();
	bool isRunning() const;

signals:
	void sampled(const obsbridge::HealthSample &s);

	/* Each reconnect is a visible glitch for viewers - always surfaced. */
	void reconnected(bool succeeded);

private:
	void poll();

	QTimer *m_timer = nullptr;
	quint64 m_lastBytes = 0;
	qint64 m_lastSampleMs = 0;
};

} // namespace obsbridge
