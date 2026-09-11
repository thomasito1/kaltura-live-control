/*
 * Minimal async HTTPS POST built on libcurl.
 *
 * Why not QNetworkAccessManager: OBS ships Qt6Network.dll but no Qt TLS backend
 * plugin - there is no tls/qopensslbackend.dll or qschannelbackend.dll anywhere in
 * the OBS install - so every https request fails at handshake with
 * "TLS initialization failed". libcurl is what OBS itself uses, and libcurl.dll is
 * already in obs-studio/bin/64bit, so this adds nothing for the user to install.
 *
 * curl_easy_perform() blocks, so the work happens on a dedicated QThread and results
 * come back to the owning (UI) thread via a queued signal. Nothing here ever blocks
 * the OBS UI.
 */
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class QThread;

namespace kaltura {

struct HttpResponse {
	long status = 0;
	QByteArray body;
	QString error; /* transport-level failure; empty when the request completed */
};

/* Lives on the worker thread. */
class HttpWorker : public QObject {
	Q_OBJECT
public slots:
	void perform(quint64 id, const QString &url, const QByteArray &body);
signals:
	void done(quint64 id, const kaltura::HttpResponse &response);
};

class HttpClient : public QObject {
	Q_OBJECT

public:
	explicit HttpClient(QObject *parent = nullptr);
	~HttpClient() override;

	using Handler = std::function<void(const HttpResponse &)>;

	/* `formBody` is already url-encoded. Handler runs on the calling thread. */
	void post(const QString &url, const QByteArray &formBody, Handler handler);

private:
	QThread *m_thread = nullptr;
	HttpWorker *m_worker = nullptr;
	QHash<quint64, Handler> m_handlers;
	quint64 m_nextId = 1;
};

} // namespace kaltura

Q_DECLARE_METATYPE(kaltura::HttpResponse)
