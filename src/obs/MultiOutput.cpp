#include "MultiOutput.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QDateTime>

#include "plugin-support.h"

namespace obsbridge {

Destination::Destination(const DestinationSpec &spec, QObject *parent)
	: QObject(parent), m_spec(spec)
{
}

Destination::~Destination()
{
	teardown();
}

void Destination::setSpec(const DestinationSpec &spec)
{
	if (isActive())
		return;
	m_spec = spec;
}

bool Destination::isActive() const
{
	return m_output && obs_output_active(m_output);
}

void Destination::teardown()
{
	if (m_output) {
		if (obs_output_active(m_output))
			obs_output_stop(m_output);
		obs_output_release(m_output);
		m_output = nullptr;
	}
	if (m_audioEnc) {
		obs_encoder_release(m_audioEnc);
		m_audioEnc = nullptr;
	}
	if (m_service) {
		obs_service_release(m_service);
		m_service = nullptr;
	}
	m_lastBytes = 0;
	m_lastSampleMs = 0;
}

bool Destination::start(QString *error)
{
	const auto fail = [&](const QString &msg) {
		if (error)
			*error = msg;
		teardown();
		return false;
	};

	if (isActive())
		return true;

	if (m_spec.server.trimmed().isEmpty())
		return fail(QObject::tr("No ingest URL for %1.").arg(m_spec.label));

	teardown();

	const QByteArray name = QStringLiteral("kaltura_dest_%1").arg(m_spec.label).toUtf8();

	/* --- service: same rtmp_custom OBS uses for its own custom destination --- */
	obs_data_t *svcSettings = obs_data_create();
	obs_data_set_string(svcSettings, "server", m_spec.server.toUtf8().constData());
	obs_data_set_string(svcSettings, "key", m_spec.streamKey.toUtf8().constData());
	m_service = obs_service_create("rtmp_custom", name.constData(), svcSettings, nullptr);
	obs_data_release(svcSettings);
	if (!m_service)
		return fail(QObject::tr("Could not create the RTMP service."));

	/* --- output --- */
	obs_data_t *outSettings = obs_data_create();
	m_output = obs_output_create("rtmp_output", name.constData(), outSettings, nullptr);
	obs_data_release(outSettings);
	if (!m_output)
		return fail(QObject::tr("Could not create the RTMP output."));

	obs_output_set_service(m_output, m_service);

	if (m_spec.audioOnly) {
		/*
		 * Audio-only, deliberately. Kaltura: "Broadcast audio and video tracks
		 * separately. Audio that is interleaved (embedded) with video will be
		 * discarded." Sending video on a language stream wastes uplink AND loses
		 * the audio.
		 *
		 * mixer_idx binds the encoder to one OBS audio track, which is how a
		 * language gets picked out of the scene's mixer.
		 */
		obs_data_t *aset = obs_data_create();
		obs_data_set_int(aset, "bitrate", m_spec.audioBitrateKbps);
		m_audioEnc = obs_audio_encoder_create("ffmpeg_aac", name.constData(), aset,
						      static_cast<size_t>(m_spec.mixerTrack),
						      nullptr);
		obs_data_release(aset);

		if (!m_audioEnc)
			return fail(QObject::tr("Could not create the AAC encoder."));

		obs_encoder_set_audio(m_audioEnc, obs_get_audio());
		obs_output_set_audio_encoder(m_output, m_audioEnc, 0);
	} else {
		/*
		 * A video stream needs an encoder, and re-encoding would double the CPU
		 * cost, so borrow the one the main OBS stream is already running. That
		 * only exists while OBS is streaming.
		 *
		 * Both getters below return BORROWED pointers - obs_output_get_video_encoder
		 * returns output->video_encoders[i] directly. Do not release them. (The
		 * frontend's streaming *output*, by contrast, IS incremented.)
		 */
		obs_output_t *main = obs_frontend_get_streaming_output();
		if (!main)
			return fail(QObject::tr(
				"Start the main OBS stream first - a video destination "
				"reuses its encoder."));

		obs_encoder_t *venc = obs_output_get_video_encoder(main);
		obs_encoder_t *aenc = obs_output_get_audio_encoder(main, 0);
		obs_output_release(main);

		if (!venc)
			return fail(QObject::tr("The main OBS output has no video encoder yet."));

		obs_output_set_video_encoder(m_output, venc);
		if (aenc)
			obs_output_set_audio_encoder(m_output, aenc, 0);
	}

	if (!obs_output_start(m_output)) {
		const char *last = obs_output_get_last_error(m_output);
		const QString msg = (last && *last) ? QString::fromUtf8(last)
						    : QObject::tr("OBS refused to start the output.");
		return fail(msg);
	}

	/* Never log the URL: it carries the ?t= ingest token. */
	obs_log(LOG_INFO, "started destination '%s' (key %s, %s)",
		m_spec.label.toUtf8().constData(), m_spec.streamKey.toUtf8().constData(),
		m_spec.audioOnly ? "audio-only" : "video");
	return true;
}

void Destination::stop()
{
	if (m_output && obs_output_active(m_output))
		obs_output_stop(m_output);

	obs_log(LOG_INFO, "stopped destination '%s'", m_spec.label.toUtf8().constData());
}

DestinationStatus Destination::status()
{
	DestinationStatus s;
	if (!m_output)
		return s;

	s.active = obs_output_active(m_output);
	s.reconnecting = obs_output_reconnecting(m_output);
	s.droppedFrames = obs_output_get_frames_dropped(m_output);
	s.totalFrames = obs_output_get_total_frames(m_output);

	const quint64 bytes = obs_output_get_total_bytes(m_output);
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (m_lastSampleMs > 0 && bytes >= m_lastBytes) {
		const double secs = (now - m_lastSampleMs) / 1000.0;
		if (secs > 0.0)
			s.sentKbps = ((bytes - m_lastBytes) * 8.0) / 1000.0 / secs;
	}
	m_lastBytes = bytes;
	m_lastSampleMs = now;

	const char *last = obs_output_get_last_error(m_output);
	if (last && *last)
		s.lastError = QString::fromUtf8(last);

	return s;
}

} // namespace obsbridge
