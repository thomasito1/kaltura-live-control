#include "KalturaClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrlQuery>

namespace kaltura {

namespace {

/*
 * Kaltura failures arrive as HTTP 200 with a KalturaAPIException body (spec gotcha
 * 1 - the single most common first-attempt mistake). Returns an error string, or an
 * empty QString when the payload is clean.
 */
QString apiErrorIn(const QJsonObject &obj)
{
	const QString code = obj.value(QStringLiteral("code")).toString();
	const QString message = obj.value(QStringLiteral("message")).toString();
	if (code.isEmpty() || message.isEmpty())
		return QString();
	return QStringLiteral("Kaltura: %1 (%2)").arg(message, code);
}

int intField(const QJsonObject &o, const char *key)
{
	const QJsonValue v = o.value(QLatin1String(key));
	if (v.isDouble())
		return static_cast<int>(v.toDouble());
	if (v.isString())
		return v.toString().toInt();
	return 0;
}

QString strField(const QJsonObject &o, const char *key)
{
	const QJsonValue v = o.value(QLatin1String(key));
	return v.isString() ? v.toString() : QString();
}

/*
 * Parses a response body. Returns an error string, or empty on success.
 *
 * session.start is the awkward case: it answers with a bare JSON string ("djJ8...")
 * rather than an object, which QJsonDocument rejects at top level. `rawString` gets
 * that value when it happens; islive likewise answers a bare true/false.
 */
QString parseBody(const QByteArray &payload, QJsonObject *obj, QString *rawString)
{
	QJsonParseError perr{};
	const QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);

	if (doc.isObject()) {
		*obj = doc.object();
		return apiErrorIn(*obj);
	}

	const QByteArray trimmed = payload.trimmed();

	if (trimmed.size() >= 2 && trimmed.startsWith('"') && trimmed.endsWith('"')) {
		*rawString = QString::fromUtf8(trimmed.mid(1, trimmed.size() - 2));
		return QString();
	}
	if (trimmed == "true" || trimmed == "1" || trimmed == "false" || trimmed == "0") {
		*rawString = QString::fromUtf8(trimmed);
		return QString();
	}

	return QStringLiteral("Unparseable response: %1")
		.arg(QString::fromUtf8(trimmed.left(200)));
}

} // namespace

QString redactSecrets(const QString &text)
{
	QString out = text;
	out.replace(QRegularExpression(QStringLiteral("(secret|ks)=[^&\\s]+"),
				       QRegularExpression::CaseInsensitiveOption),
		    QStringLiteral("\\1=***"));
	return out;
}

KalturaClient::KalturaClient(QObject *parent)
	: QObject(parent), m_http(new HttpClient(this))
{
}

void KalturaClient::setHost(const QString &host)
{
	m_host = host.trimmed().isEmpty() ? QStringLiteral("cdnapisec.kaltura.com")
					  : host.trimmed();
}

void KalturaClient::clearSession()
{
	m_ks.clear();
	m_partnerId = 0;
	m_expiresAt = 0;
}

bool KalturaClient::looksLikeEntryId(const QString &s)
{
	static const QRegularExpression re(QStringLiteral("^\\d+_[a-z0-9]+$"),
					   QRegularExpression::CaseInsensitiveOption);
	return re.match(s.trimmed()).hasMatch();
}

void KalturaClient::setBusy(bool busy)
{
	m_inFlight += busy ? 1 : -1;
	if (m_inFlight < 0)
		m_inFlight = 0;
	emit busyChanged(m_inFlight > 0);
}

void KalturaClient::post(const QString &service, const QString &action, const Params &params,
			 std::function<void(const QByteArray &, const QString &)> handler)
{
	const QString url = QStringLiteral("https://%1/api_v3/service/%2/action/%3")
				    .arg(m_host, service, action);

	QUrlQuery body;
	/* Without format=1 the API answers in XML (spec section 3). */
	body.addQueryItem(QStringLiteral("format"), QStringLiteral("1"));
	for (const auto &kv : params)
		body.addQueryItem(kv.first, kv.second);

	setBusy(true);

	m_http->post(url, body.toString(QUrl::FullyEncoded).toUtf8(),
		     [this, handler](const HttpResponse &resp) {
			     setBusy(false);
			     m_lastRaw = resp.body;
			     handler(resp.body, resp.error);
		     });
}

void KalturaClient::startSession(const QString &partnerId, const QString &secret)
{
	post(QStringLiteral("session"), QStringLiteral("start"),
	     {{QStringLiteral("partnerId"), partnerId},
	      {QStringLiteral("secret"), secret},
	      {QStringLiteral("userId"), QString()},
	      {QStringLiteral("type"), QStringLiteral("2")},
	      {QStringLiteral("expiry"), QStringLiteral("900")},
	      {QStringLiteral("privileges"), QStringLiteral("disableentitlement,appid:kmc")}},
	     [this](const QByteArray &body, const QString &transportError) {
		     if (!transportError.isEmpty()) {
			     emit failed(QStringLiteral("Network: %1").arg(transportError));
			     return;
		     }

		     QJsonObject obj;
		     QString ks;
		     const QString err = parseBody(body, &obj, &ks);
		     if (!err.isEmpty()) {
			     emit failed(redactSecrets(err));
			     return;
		     }
		     if (ks.isEmpty()) {
			     emit failed(QStringLiteral("session.start returned no KS."));
			     return;
		     }

		     /* Round-trip through session.get so partnerId and expiry come from
		      * the server rather than from what the user typed. */
		     validateKs(ks);
	     });
}

void KalturaClient::validateKs(const QString &ks)
{
	const QString candidate = ks.trimmed();
	if (candidate.isEmpty()) {
		emit failed(QStringLiteral("Paste a KS first."));
		return;
	}

	post(QStringLiteral("session"), QStringLiteral("get"),
	     {{QStringLiteral("ks"), candidate}},
	     [this, candidate](const QByteArray &body, const QString &transportError) {
		     if (!transportError.isEmpty()) {
			     clearSession();
			     emit failed(QStringLiteral("Network: %1").arg(transportError));
			     return;
		     }

		     QJsonObject obj;
		     QString raw;
		     const QString err = parseBody(body, &obj, &raw);
		     if (!err.isEmpty()) {
			     clearSession();
			     emit failed(redactSecrets(err));
			     return;
		     }

		     m_ks = candidate;
		     m_partnerId = intField(obj, "partnerId");
		     m_expiresAt = static_cast<qint64>(
			     obj.value(QStringLiteral("expiry")).toDouble());

		     if (m_partnerId <= 0) {
			     clearSession();
			     emit failed(QStringLiteral("session.get returned no partnerId."));
			     return;
		     }

		     emit sessionReady(m_partnerId, strField(obj, "userId"), m_expiresAt);
	     });
}

namespace {

/* KalturaEntryType::LIVE_STREAM */
const char *kLiveEntryType = "7";

} // namespace

void KalturaClient::searchEntries(const QString &query)
{
	const QString q = query.trimmed();
	if (q.isEmpty())
		return;
	runSearch(q, 0);
}

void KalturaClient::runSearch(const QString &q, int stage)
{
	Params params = {
		{QStringLiteral("ks"), m_ks},
		/* Without filter[objectType] the filter is silently ignored and you
		 * get unfiltered results (spec gotcha 12). */
		{QStringLiteral("filter[objectType]"), QStringLiteral("KalturaBaseEntryFilter")},
		{QStringLiteral("filter[orderBy]"), QStringLiteral("-createdAt")},
		{QStringLiteral("pager[pageSize]"), QStringLiteral("20")},
	};

	if (looksLikeEntryId(q)) {
		params.append({QStringLiteral("filter[idEqual]"), q});
		stage = 99; /* an exact id needs no fallback chain */
	} else {
		switch (stage) {
		case 0:
			params.append({QStringLiteral("filter[typeIn]"),
				       QString::fromLatin1(kLiveEntryType)});
			params.append({QStringLiteral("filter[freeText]"), q});
			break;
		case 1:
			params.append({QStringLiteral("filter[typeIn]"),
				       QString::fromLatin1(kLiveEntryType)});
			params.append({QStringLiteral("filter[nameLike]"), q});
			break;
		default:
			/* Last resort: any entry type, so the user sees what does exist. */
			params.append({QStringLiteral("filter[freeText]"), q});
			break;
		}
	}

	post(QStringLiteral("baseentry"), QStringLiteral("list"), params,
	     [this, q, stage](const QByteArray &body, const QString &transportError) {
		     if (!transportError.isEmpty()) {
			     emit failed(QStringLiteral("Network: %1").arg(transportError));
			     return;
		     }

		     QJsonObject obj;
		     QString raw;
		     const QString err = parseBody(body, &obj, &raw);
		     if (!err.isEmpty()) {
			     emit failed(redactSecrets(err));
			     return;
		     }

		     QVector<EntrySummary> out;
		     const QJsonArray objects = obj.value(QStringLiteral("objects")).toArray();
		     for (const QJsonValue &v : objects) {
			     const QJsonObject o = v.toObject();
			     EntrySummary s;
			     s.id = strField(o, "id");
			     s.name = strField(o, "name");
			     s.type = intField(o, "type");
			     s.mediaType = intField(o, "mediaType");
			     if (!s.id.isEmpty())
				     out.append(s);
		     }

		     if (out.isEmpty() && stage < 2) {
			     runSearch(q, stage + 1);
			     return;
		     }

		     emit searchResults(out);
	     });
}

void KalturaClient::fetchLiveEntry(const QString &entryId)
{
	post(QStringLiteral("livestream"), QStringLiteral("get"),
	     {{QStringLiteral("ks"), m_ks}, {QStringLiteral("entryId"), entryId}},
	     [this](const QByteArray &body, const QString &transportError) {
		     if (!transportError.isEmpty()) {
			     emit failed(QStringLiteral("Network: %1").arg(transportError));
			     return;
		     }

		     QJsonObject o;
		     QString raw;
		     const QString err = parseBody(body, &o, &raw);
		     if (!err.isEmpty()) {
			     emit failed(redactSecrets(err));
			     return;
		     }

		     LiveEntry e;
		     e.id = strField(o, "id");
		     e.name = strField(o, "name");
		     e.description = strField(o, "description");
		     e.adminTags = strField(o, "adminTags");
		     e.streamName = strField(o, "streamName");
		     e.primaryBroadcastingUrl = strField(o, "primaryBroadcastingUrl");
		     e.secondaryBroadcastingUrl = strField(o, "secondaryBroadcastingUrl");
		     e.primarySecuredBroadcastingUrl =
			     strField(o, "primarySecuredBroadcastingUrl");
		     e.secondarySecuredBroadcastingUrl =
			     strField(o, "secondarySecuredBroadcastingUrl");
		     e.primarySrtBroadcastingUrl = strField(o, "primarySrtBroadcastingUrl");
		     e.secondarySrtBroadcastingUrl = strField(o, "secondarySrtBroadcastingUrl");
		     e.srtPass = strField(o, "srtPass");
		     e.sourceType = intField(o, "sourceType");
		     e.dvrStatus = intField(o, "dvrStatus");
		     e.dvrWindow = intField(o, "dvrWindow");
		     e.recordStatus = intField(o, "recordStatus");
		     e.viewMode = intField(o, "viewMode");
		     e.explicitLive = intField(o, "explicitLive");
		     e.valid = !e.id.isEmpty();

		     if (!e.valid) {
			     emit failed(QStringLiteral("livestream.get returned no entry id."));
			     return;
		     }

		     emit liveEntryReady(e);
	     });
}

void KalturaClient::updateRecordingAndDvr(const QString &entryId, bool recording, bool dvr,
					  int dvrWindowMinutes)
{
	Params params = {
		{QStringLiteral("ks"), m_ks},
		{QStringLiteral("entryId"), entryId},
		/* The Admin variant is required or the call is rejected outright. */
		{QStringLiteral("liveStreamEntry[objectType]"),
		 QStringLiteral("KalturaLiveStreamAdminEntry")},
		/*
		 * Spec gotcha 3 ties the encodingIP1/encodingIP2-as-empty-strings
		 * requirement to KalturaLiveStreamAdminEntry itself, not specifically to
		 * liveStream.add - so send them here too. They are meaningless for this
		 * use case and harmless if the field is not actually required.
		 */
		{QStringLiteral("liveStreamEntry[encodingIP1]"), QString()},
		{QStringLiteral("liveStreamEntry[encodingIP2]"), QString()},
		{QStringLiteral("liveStreamEntry[recordStatus]"),
		 recording ? QStringLiteral("1") : QStringLiteral("0")},
		{QStringLiteral("liveStreamEntry[dvrStatus]"),
		 dvr ? QStringLiteral("1") : QStringLiteral("0")},
	};

	/* Only send a window when DVR is on; Kaltura rejects a zero window with it. */
	if (dvr)
		params.append({QStringLiteral("liveStreamEntry[dvrWindow]"),
			       QString::number(qMax(1, dvrWindowMinutes))});

	post(QStringLiteral("liveStream"), QStringLiteral("update"), params,
	     [this, entryId](const QByteArray &body, const QString &transportError) {
		     if (!transportError.isEmpty()) {
			     emit failed(QStringLiteral("Network: %1").arg(transportError));
			     return;
		     }

		     QJsonObject obj;
		     QString raw;
		     const QString err = parseBody(body, &obj, &raw);
		     if (!err.isEmpty()) {
			     emit failed(redactSecrets(err));
			     return;
		     }

		     emit entryUpdated(QStringLiteral("recording / DVR"));
		     /* Read back rather than trusting the write: the UI should reflect
		      * what Kaltura actually stored. */
		     fetchLiveEntry(entryId);
	     });
}

void KalturaClient::updateViewMode(const QString &entryId, ViewMode mode)
{
	const Params params = {
		{QStringLiteral("ks"), m_ks},
		{QStringLiteral("entryId"), entryId},
		/* baseentry.update, not liveStream.update - see the header. */
		{QStringLiteral("baseEntry[objectType]"),
		 QStringLiteral("KalturaLiveStreamAdminEntry")},
		{QStringLiteral("baseEntry[viewMode]"),
		 QString::number(static_cast<int>(mode))},
	};

	post(QStringLiteral("baseentry"), QStringLiteral("update"), params,
	     [this, entryId](const QByteArray &body, const QString &transportError) {
		     if (!transportError.isEmpty()) {
			     emit failed(QStringLiteral("Network: %1").arg(transportError));
			     return;
		     }

		     QJsonObject obj;
		     QString raw;
		     const QString err = parseBody(body, &obj, &raw);
		     if (!err.isEmpty()) {
			     emit failed(redactSecrets(err));
			     return;
		     }

		     emit entryUpdated(QStringLiteral("view mode"));
		     fetchLiveEntry(entryId);
	     });
}

void KalturaClient::checkIsLive(const QString &entryId)
{
	post(QStringLiteral("livestream"), QStringLiteral("islive"),
	     {{QStringLiteral("ks"), m_ks},
	      {QStringLiteral("id"), entryId},
	      {QStringLiteral("protocol"), QStringLiteral("hls")}},
	     [this, entryId](const QByteArray &body, const QString &transportError) {
		     if (!transportError.isEmpty()) {
			     emit isLiveResult(entryId, false);
			     return;
		     }

		     QJsonObject obj;
		     QString raw;
		     /*
		      * islive throws when the entry has never been broadcast. That is a
		      * legitimate "no", not a failure worth showing the engineer.
		      */
		     const QString err = parseBody(body, &obj, &raw);
		     const bool live = err.isEmpty() &&
				       (raw == QLatin1String("true") ||
					raw == QLatin1String("1") ||
					obj.value(QStringLiteral("result")).toBool());

		     emit isLiveResult(entryId, live);
	     });
}

} // namespace kaltura
