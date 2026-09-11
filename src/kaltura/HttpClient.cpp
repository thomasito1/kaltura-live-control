#include "HttpClient.hpp"

#include <QThread>

#include <curl/curl.h>

#include <mutex>

namespace kaltura {

namespace {

size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	const size_t bytes = size * nmemb;
	static_cast<QByteArray *>(userdata)->append(ptr, static_cast<int>(bytes));
	return bytes;
}

void ensureCurlGlobalInit()
{
	static std::once_flag once;
	std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

void HttpWorker::perform(quint64 id, const QString &url, const QByteArray &body)
{
	ensureCurlGlobalInit();

	HttpResponse resp;

	CURL *curl = curl_easy_init();
	if (!curl) {
		resp.error = QStringLiteral("curl_easy_init failed");
		emit done(id, resp);
		return;
	}

	const QByteArray urlUtf8 = url.toUtf8();
	struct curl_slist *headers = curl_slist_append(
		nullptr, "Content-Type: application/x-www-form-urlencoded");

	curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.constData());
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.constData());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "kaltura-live-control/0.1 (OBS plugin)");

	const CURLcode rc = curl_easy_perform(curl);
	if (rc != CURLE_OK)
		resp.error = QString::fromUtf8(curl_easy_strerror(rc));
	else
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	emit done(id, resp);
}

HttpClient::HttpClient(QObject *parent) : QObject(parent)
{
	qRegisterMetaType<kaltura::HttpResponse>("kaltura::HttpResponse");

	m_thread = new QThread(this);
	m_worker = new HttpWorker; /* no parent: owned by the worker thread */
	m_worker->moveToThread(m_thread);

	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

	/* Queued by construction (different threads), so handlers run on our thread. */
	connect(m_worker, &HttpWorker::done, this,
		[this](quint64 id, const kaltura::HttpResponse &resp) {
			const Handler handler = m_handlers.take(id);
			if (handler)
				handler(resp);
		});

	m_thread->start();
}

HttpClient::~HttpClient()
{
	m_thread->quit();
	/* Bounded wait: a request in flight has a 30s curl timeout, but OBS must not
	 * hang on shutdown if the network is wedged. */
	if (!m_thread->wait(5000))
		m_thread->terminate();
}

void HttpClient::post(const QString &url, const QByteArray &formBody, Handler handler)
{
	const quint64 id = m_nextId++;
	m_handlers.insert(id, std::move(handler));

	QMetaObject::invokeMethod(m_worker, "perform", Qt::QueuedConnection,
				  Q_ARG(quint64, id), Q_ARG(QString, url),
				  Q_ARG(QByteArray, formBody));
}

} // namespace kaltura
