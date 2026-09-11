/*
 * Kaltura API transport (spec section 3) and the calls this plugin needs.
 *
 * Async throughout: requests go out on HttpClient's worker thread and every handler
 * runs back on the UI thread, so nothing here ever blocks OBS.
 */
#pragma once

#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

#include "Endpoints.hpp"
#include "HttpClient.hpp"

namespace kaltura {

struct EntrySummary {
	QString id;
	QString name;
	int type = 0;      /* KalturaEntryType: 1 = media clip, 7 = live stream */
	int mediaType = 0; /* 201/202/203 are the live variants */
	bool isLive() const { return type == 7; }
};

class KalturaClient : public QObject {
	Q_OBJECT

public:
	explicit KalturaClient(QObject *parent = nullptr);

	void setHost(const QString &host);
	QString host() const { return m_host; }

	QString ks() const { return m_ks; }
	int partnerId() const { return m_partnerId; }
	bool isConnected() const { return !m_ks.isEmpty() && m_partnerId > 0; }
	qint64 expiresAtUnix() const { return m_expiresAt; }

	void clearSession();

	/*
	 * Mint an admin KS. The disableentitlement privilege is what lets an admin see
	 * all entries regardless of category entitlements - without it, searches come
	 * back empty for no visible reason (spec gotcha 13).
	 */
	void startSession(const QString &partnerId, const QString &secret);

	/* Validate a pasted KS and derive the partnerId from it. */
	void validateKs(const QString &ks);

	/*
	 * Spec section 5: an id-shaped query does idEqual, anything else a name search.
	 *
	 * Name search runs a fallback chain, because one filter is not reliably enough:
	 *   1. live entries only, freeText  (what KMC's own search box uses)
	 *   2. live entries only, nameLike  (substring match)
	 *   3. any entry type, freeText     (so a VOD-only match still shows up,
	 *                                    flagged, instead of "no matches")
	 * Restricting to live first matters because livestream.get fails on a VOD entry,
	 * which made picking a VOD result look like the search itself was broken.
	 */
	void searchEntries(const QString &query);

	void fetchLiveEntry(const QString &entryId);
	void checkIsLive(const QString &entryId);

	/*
	 * Recording and DVR go through liveStream.update with the Admin object type.
	 * Pass dvrWindow in minutes; it is ignored by Kaltura when dvrStatus is 0.
	 */
	void updateRecordingAndDvr(const QString &entryId, bool recording, bool dvr,
				   int dvrWindowMinutes);

	/*
	 * viewMode must go through baseentry.update, NOT liveStream.update: the latter
	 * rejects viewMode/explicitLive with CANNOT_UPDATE_FIELDS_WHILE_ENTRY_BROADCASTING
	 * once the stream is on air, which is exactly when an operator needs to flip a
	 * preview to live (spec section 6).
	 */
	void updateViewMode(const QString &entryId, ViewMode mode);

	static bool looksLikeEntryId(const QString &s);

	/* Last raw response body, for the UI's diagnostics expander. */
	QByteArray lastRawResponse() const { return m_lastRaw; }

signals:
	void busyChanged(bool busy);
	void sessionReady(int partnerId, const QString &userId, qint64 expiresAtUnix);
	void searchResults(const QVector<kaltura::EntrySummary> &results);
	void liveEntryReady(const kaltura::LiveEntry &entry);
	void isLiveResult(const QString &entryId, bool live);
	/* Emitted after a successful update, before the entry is re-read. */
	void entryUpdated(const QString &what);
	void failed(const QString &message);

private:
	using Params = QVector<QPair<QString, QString>>;

	/*
	 * Form-encoded POST with format=1. `params` uses the bracket notation Kaltura
	 * expects for nested objects, e.g. "filter[objectType]" (spec section 3).
	 */
	void post(const QString &service, const QString &action, const Params &params,
		  std::function<void(const QByteArray &body, const QString &error)> handler);

	void runSearch(const QString &query, int stage);
	void setBusy(bool busy);

	HttpClient *m_http = nullptr;
	QString m_host = QStringLiteral("cdnapisec.kaltura.com");
	QString m_ks;
	int m_partnerId = 0;
	qint64 m_expiresAt = 0;
	int m_inFlight = 0;
	QByteArray m_lastRaw;
};

/*
 * Strips a KS or admin secret out of any string before it reaches a log, a status
 * label or a crash report (spec section 4).
 */
QString redactSecrets(const QString &text);

} // namespace kaltura
