/*
 * Kaltura Live Control - ingest endpoint derivation (spec section 7).
 *
 * Everything here is pure: no network, no OBS, no Qt widgets. That is deliberate -
 * this is the logic that was expensive to get right in production, so it is kept
 * free of dependencies and is the part worth unit-testing first.
 */
#pragma once

#include <QString>
#include <QVector>

namespace kaltura {

/* The subset of a livestream.get response this plugin acts on. */
struct LiveEntry {
	QString id;
	QString name;
	QString description;
	QString adminTags;
	QString streamName;
	QString primaryBroadcastingUrl;
	QString secondaryBroadcastingUrl;
	QString primarySecuredBroadcastingUrl;
	QString secondarySecuredBroadcastingUrl;
	QString primarySrtBroadcastingUrl;
	QString secondarySrtBroadcastingUrl;
	QString srtPass;
	int sourceType = 0;
	int dvrStatus = 0;
	int dvrWindow = 0;
	int recordStatus = 0;
	/*
	 * viewMode gates whether viewers see the stream or only moderators do.
	 * KalturaViewMode: 0 = PREVIEW, 1 = LIVE. explicitLive says whether the entry
	 * uses that gate at all.
	 */
	int viewMode = 0;
	int explicitLive = 0;
	bool valid = false;
};

enum class ViewMode { Preview = 0, Live = 1 };

enum class EntryType { Live, ManualLive, Simulive };

QString entryTypeName(EntryType t);

/*
 * Spec section 5, in priority order: adminTags, then sourceType, then an empty
 * broadcasting URL, then default to live.
 */
EntryType detectEntryType(const LiveEntry &e);

/* Which encoding to use for the '#' that opens an SRT streamid. */
enum class SrtEncoding {
	Raw,       /* #:::e=...   - as the spec prints it        */
	HashOnly,  /* %23:::e=... - '#' percent-encoded          */
	Full       /* every reserved character percent-encoded   */
};

struct Endpoints {
	QString rtmpPrimary;
	QString rtmpSecondary;
	QString rtmpPrimaryStripped;
	QString rtmpSecondaryStripped;
	QString streamKey;
	QString passphrase;
	QString srtHost;
	QString srtStreamIdPrimary;   /* st=0 */
	QString srtStreamIdSecondary; /* st=1 */

	/* Diagnostics the UI surfaces so an engineer can see *why* a value looks odd. */
	bool srtDerived = false;            /* SRT host was inferred, not returned by the API */
	bool streamNameHadTemplate = false; /* streamName contained a %i placeholder           */
	bool streamNameWasUndefined = false;/* streamName was the literal string "undefined"   */
	bool hasExtraQueryParams = false;   /* broadcast URL carried ?p=&e=&i=                 */
};

Endpoints deriveEndpoints(const LiveEntry &e);

/* ---- individual steps, exposed for testing and reuse ---- */

/*
 * Spec gotcha 2. Kaltura returns stream names containing a %i placeholder; passing
 * it through literally makes ingest fail with "Incorrect stream name/key
 * <entry>_undefined". Some accounts also return the literal string "undefined".
 */
QString resolveStreamKey(const QString &streamName);

QString tokenFromUrl(const QString &url);

/*
 * Spec gotcha 11. Newer Kaltura live infrastructure appends ?p=&e=&i=0&t=; the extra
 * params break playpath parsing in downstream pushers. Reduce to the ?t= token only.
 */
QString stripToToken(const QString &url);

/*
 * Derive the SRT ingest host from an RTMP broadcasting URL.
 *
 * NOTE: the spec publishes this regex anchored to "rtmp://" only, yet also says to
 * prefer the secured "rtmps://" variant - so on any account returning a secured URL
 * the published form silently falls through to the weaker protocol-swap fallback.
 * This implementation accepts both. See tools/endpoint-probe/README.md.
 */
QString deriveSrtHost(const QString &rtmpUrl);

QString srtStreamId(const QString &entryId, int streamType, const QString &passphrase);

/* Assemble srt://host:7045?streamid=<encoded streamid>. */
QString srtServerUrl(const QString &srtHost, const QString &streamId, SrtEncoding enc);

/* Spec section 9 / 11c playback + ingest manifest URLs. */
QString playbackManifestUrl(const QString &apiHost, int partnerId, const QString &entryId);
QString ingestManifestUrl(const QString &apiHost, int partnerId, const QString &entryId,
			  int streamType);

} // namespace kaltura
