/*
 * Independent extra RTMP outputs, for Kaltura Live+ multi-audio and backup ingest.
 *
 * Why we own these rather than driving obs-multi-rtmp: that plugin is a Qt dock with
 * no public C API, and it reads its targets from
 * basic/profiles/<profile>/obs-multi-rtmp.json at load time, so writing its config
 * would need an OBS restart. Creating obs_output_t instances ourselves is what it
 * does internally anyway, and it lets us fill the endpoints in from the resolved
 * Kaltura entry instead of having them pasted by hand.
 *
 * Division of labour:
 *
 *   video track (stream name "1")  -> OBS's OWN streaming output, via
 *                                     StreamService::applyCustomService(). The user
 *                                     presses Start Streaming as normal.
 *   audio tracks ("1000"+)         -> one audio-only output each, created here.
 *   backup video                   -> an extra output here, which needs a video
 *                                     encoder and therefore only works while the
 *                                     main OBS stream is running.
 *
 * The audio-only outputs need no video encoder at all, so they do not double-encode
 * and do not depend on OBS's streaming state.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct obs_output;
struct obs_encoder;
struct obs_service;
typedef struct obs_output obs_output_t;
typedef struct obs_encoder obs_encoder_t;
typedef struct obs_service obs_service_t;

namespace obsbridge {

struct DestinationSpec {
	QString label;       /* "English", "Backup video" */
	QString server;      /* rtmp://<entry>.<p|b>.kpublish.kaltura.com:1935/kLive?t=... */
	QString streamKey;   /* "1" for video, "1000"+ for an audio language */
	int mixerTrack = 0;  /* OBS audio track 0-5; ignored when audioOnly is false */
	bool audioOnly = true;
	int audioBitrateKbps = 128;
};

struct DestinationStatus {
	bool active = false;
	bool reconnecting = false;
	int droppedFrames = 0;
	int totalFrames = 0;
	double sentKbps = 0.0;
	QString lastError;
};

class Destination : public QObject {
	Q_OBJECT

public:
	explicit Destination(const DestinationSpec &spec, QObject *parent = nullptr);
	~Destination() override;

	const DestinationSpec &spec() const { return m_spec; }
	void setSpec(const DestinationSpec &spec); /* only while stopped */

	/* Returns false and fills `error` with OBS's own last-error text when it can. */
	bool start(QString *error);
	void stop();
	bool isActive() const;

	DestinationStatus status();

private:
	void teardown();

	DestinationSpec m_spec;
	obs_output_t *m_output = nullptr;
	obs_encoder_t *m_audioEnc = nullptr;
	obs_service_t *m_service = nullptr;

	quint64 m_lastBytes = 0;
	qint64 m_lastSampleMs = 0;
};

} // namespace obsbridge
