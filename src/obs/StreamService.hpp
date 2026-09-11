/*
 * Writing resolved Kaltura endpoints into OBS's own stream settings (spec section 8).
 * This is the payoff of the whole plugin: it replaces copying an RTMP URL and a
 * stream key out of KMC by hand.
 *
 * Every function here must be called on the OBS UI thread.
 */
#pragma once

#include <QString>

namespace obsbridge {

/*
 * Spec section 8 guardrail. Silently repointing a broadcast that is already on air is
 * the worst failure this plugin could have, so applyCustomService() refuses outright
 * rather than warning.
 */
bool isStreamingActive();

/*
 * Configures OBS's "Custom..." streaming service.
 *
 * For RTMP/RTMPS: `server` is the ingest URL, `key` the stream name.
 * For SRT: the whole srt:// URL goes in `server` and `key` must be empty - OBS routes
 * srt:// through FFmpeg on this same rtmp_custom service.
 *
 * Returns false and fills `error` if OBS is streaming or the service could not be
 * created. Persists via obs_frontend_save_streaming_service() so the setting survives
 * a restart.
 */
bool applyCustomService(const QString &server, const QString &key, QString *error);

/*
 * Start/stop OBS's own streaming output. The Video row in the streams list drives
 * these so every row in the UI has the same start/stop affordance, rather than the
 * video track alone requiring the user to go and press OBS's button.
 */
void startMainStream();
void stopMainStream();

/* What OBS currently has configured, for the "before" side of the UI. */
QString currentServer();
QString currentKey();

} // namespace obsbridge
