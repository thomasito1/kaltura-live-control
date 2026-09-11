/*
 * Kaltura Live+ multi-audio stream-name mapping.
 *
 * Source: "Using multiple audio tracks with Kaltura Live+" (Kaltura Knowledge Center).
 *
 * All tracks - video and every audio language - share one ingest endpoint:
 *
 *   rtmp://[entryId].[p|b].kpublish.kaltura.com:1935/kLive?t=[token]
 *
 * and are told apart purely by the RTMP stream name (what OBS calls the stream key):
 *
 *   video   -> "1"
 *   English -> "1000",  Spanish -> "1001",  French -> "1002",  ...
 *
 * Two constraints from that document drive the design here:
 *
 *  1. "Broadcast audio and video tracks separately. Audio that is interleaved
 *     (embedded) with video will be discarded."  So a language stream must be
 *     AUDIO-ONLY - sending video alongside it wastes uplink and the audio is dropped.
 *
 *  2. "Multi-audio is not supported for Kaltura Passthrough streaming" and the
 *     transcoding profile must include the audio flavors (100 and onward).
 *
 * Also documented: multi-audio does not work with *seamless* failover, but ordinary
 * primary/backup failover is supported.
 */
#pragma once

#include <QString>
#include <QVector>

namespace kaltura {

struct AudioLanguage {
	const char *name;    /* display name as Kaltura lists it */
	const char *iso639_1; /* 2-letter code, for stream containers / labels */
	int streamName;      /* the RTMP stream name, e.g. 1000 */
};

/* The full published table, in Kaltura's own order. */
const QVector<AudioLanguage> &audioLanguages();

/* Stream name for the video track. */
inline QString videoStreamName()
{
	return QStringLiteral("1");
}

/* Looks up by display name; returns -1 when unknown. */
int streamNameForLanguage(const QString &displayName);

} // namespace kaltura
