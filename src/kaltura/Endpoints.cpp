#include "Endpoints.hpp"

#include <QRegularExpression>
#include <QUrl>

namespace kaltura {

QString entryTypeName(EntryType t)
{
	switch (t) {
	case EntryType::ManualLive:
		return QStringLiteral("manuallive");
	case EntryType::Simulive:
		return QStringLiteral("simulive");
	case EntryType::Live:
	default:
		return QStringLiteral("live");
	}
}

EntryType detectEntryType(const LiveEntry &e)
{
	const QString tags = e.adminTags.toLower();

	/* 1. adminTags is the most explicit signal. */
	if (tags.contains(QStringLiteral("kms-webcast-event-simulive")))
		return EntryType::Simulive;
	if (tags.contains(QStringLiteral("kms-webcast-event-manual")) ||
	    tags.contains(QStringLiteral("live_hls")))
		return EntryType::ManualLive;

	/* 2. sourceType. */
	if (e.sourceType == 30)
		return EntryType::ManualLive;
	if (e.sourceType == 29 || e.sourceType == 32 || e.sourceType == 33)
		return EntryType::Live;

	/* 3. No RTMP ingest configured at all. */
	if (e.primarySecuredBroadcastingUrl.isEmpty() && e.primaryBroadcastingUrl.isEmpty())
		return EntryType::ManualLive;

	return EntryType::Live;
}

QString resolveStreamKey(const QString &streamName)
{
	if (streamName.isEmpty() || streamName == QLatin1String("undefined"))
		return QStringLiteral("1");

	QString out = streamName;
	out.replace(QRegularExpression(QStringLiteral("%i"),
				       QRegularExpression::CaseInsensitiveOption),
		    QStringLiteral("1"));
	return out;
}

QString tokenFromUrl(const QString &url)
{
	static const QRegularExpression re(QStringLiteral("[?&]t=([^&\\s]+)"));
	const QRegularExpressionMatch m = re.match(url);
	return m.hasMatch() ? m.captured(1) : QString();
}

QString stripToToken(const QString &url)
{
	if (url.isEmpty())
		return QString();

	const QString token = tokenFromUrl(url);
	const QString base = url.section(QLatin1Char('?'), 0, 0);
	return token.isEmpty() ? base : base + QStringLiteral("?t=") + token;
}

QString deriveSrtHost(const QString &rtmpUrl)
{
	if (rtmpUrl.isEmpty())
		return QString();

	/* Accepts rtmps:// as well as rtmp:// - see the note in the header. */
	static const QRegularExpression re(
		QStringLiteral("^rtmps?://([\\w-]+\\.[pb])\\.kpublish\\.kaltura\\.com"),
		QRegularExpression::CaseInsensitiveOption);

	const QRegularExpressionMatch m = re.match(rtmpUrl);
	if (m.hasMatch())
		return QStringLiteral("srt://") + m.captured(1) +
		       QStringLiteral(".srt.publish.live.kaltura.com:7045");

	/* Fallback: keep the host, swap the protocol, force the SRT port. */
	QString swapped = rtmpUrl;
	swapped.replace(QRegularExpression(QStringLiteral("^rtmps?:"),
					   QRegularExpression::CaseInsensitiveOption),
			QStringLiteral("https:"));

	const QUrl u(swapped);
	if (!u.isValid() || u.host().isEmpty())
		return QString();

	return QStringLiteral("srt://") + u.host() + QStringLiteral(":7045");
}

QString srtStreamId(const QString &entryId, int streamType, const QString &passphrase)
{
	return QStringLiteral("#:::e=%1,st=%2,p=%3")
		.arg(entryId)
		.arg(streamType)
		.arg(passphrase);
}

QString srtServerUrl(const QString &srtHost, const QString &streamId, SrtEncoding enc)
{
	if (srtHost.isEmpty() || streamId.isEmpty())
		return QString();

	QString id;
	switch (enc) {
	case SrtEncoding::Raw:
		id = streamId;
		break;
	case SrtEncoding::HashOnly:
		/*
		 * Left unencoded, everything after '#' is parsed as a URL fragment and
		 * never reaches libsrt. This is the suspected fix; see spec section 8.
		 */
		id = streamId;
		id.replace(QLatin1Char('#'), QStringLiteral("%23"));
		break;
	case SrtEncoding::Full:
		id = QString::fromUtf8(QUrl::toPercentEncoding(streamId));
		break;
	}

	return srtHost + QStringLiteral("?streamid=") + id;
}

Endpoints deriveEndpoints(const LiveEntry &e)
{
	Endpoints d;

	/* Prefer the secured (RTMPS) variants, fall back to plain RTMP. */
	d.rtmpPrimary = !e.primarySecuredBroadcastingUrl.isEmpty()
				? e.primarySecuredBroadcastingUrl
				: e.primaryBroadcastingUrl;
	d.rtmpSecondary = !e.secondarySecuredBroadcastingUrl.isEmpty()
				  ? e.secondarySecuredBroadcastingUrl
				  : e.secondaryBroadcastingUrl;

	d.rtmpPrimaryStripped = stripToToken(d.rtmpPrimary);
	d.rtmpSecondaryStripped = stripToToken(d.rtmpSecondary);

	d.streamKey = resolveStreamKey(e.streamName);
	d.streamNameHadTemplate = e.streamName.contains(QStringLiteral("%i"), Qt::CaseInsensitive);
	d.streamNameWasUndefined = (e.streamName == QLatin1String("undefined"));

	d.passphrase = e.srtPass;
	if (d.passphrase.isEmpty())
		d.passphrase = tokenFromUrl(d.rtmpPrimary);
	if (d.passphrase.isEmpty())
		d.passphrase = tokenFromUrl(d.rtmpSecondary);

	if (!e.primarySrtBroadcastingUrl.isEmpty()) {
		d.srtHost = e.primarySrtBroadcastingUrl;
		d.srtDerived = false;
	} else {
		/*
		 * Derive from the *unsecured* URL when present - that is the form the
		 * published pattern was written against - but deriveSrtHost() accepts
		 * either, so a secured-only account still resolves.
		 */
		d.srtHost = deriveSrtHost(!e.primaryBroadcastingUrl.isEmpty()
						  ? e.primaryBroadcastingUrl
						  : d.rtmpPrimary);
		d.srtDerived = true;
	}

	d.srtStreamIdPrimary = srtStreamId(e.id, 0, d.passphrase);
	d.srtStreamIdSecondary = srtStreamId(e.id, 1, d.passphrase);

	static const QRegularExpression extras(QStringLiteral("[?&](p|e|i)="));
	d.hasExtraQueryParams = extras.match(d.rtmpPrimary).hasMatch();

	return d;
}

QString playbackManifestUrl(const QString &apiHost, int partnerId, const QString &entryId)
{
	return QStringLiteral("https://%1/p/%2/sp/0/playManifest/entryId/%3"
			      "/format/applehttp/protocol/https/a.m3u8")
		.arg(apiHost)
		.arg(partnerId)
		.arg(entryId);
}

QString ingestManifestUrl(const QString &apiHost, int partnerId, const QString &entryId,
			  int streamType)
{
	/*
	 * Spec section 11c: the streamType variants use the full service-partner form
	 * sp/{pid}00, not the sp/0 of the simple playback URL above.
	 *
	 * Built as a separate string rather than inline: Qt's arg() accepts two-digit
	 * place markers, so "%200" would be read as marker %20 followed by '0'.
	 */
	const QString sp = QString::number(partnerId) + QStringLiteral("00");

	return QStringLiteral("https://%1/p/%2/sp/%3/playManifest/entryId/%4"
			      "/protocol/https/format/applehttp/streamType/%5/a.m3u8")
		.arg(apiHost)
		.arg(partnerId)
		.arg(sp)
		.arg(entryId)
		.arg(streamType);
}

} // namespace kaltura
