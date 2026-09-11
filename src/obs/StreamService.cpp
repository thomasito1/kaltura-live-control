#include "StreamService.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"

namespace obsbridge {

namespace {

/* Reads one string field off whatever service OBS currently has configured. */
QString currentServiceField(const char *field)
{
	/*
	 * BORROWED pointer - do NOT release it.
	 *
	 * OBSStudioAPI::obs_frontend_get_streaming_service() is `return main->GetService();`
	 * with no obs_service_get_ref(), unlike obs_frontend_get_streaming_output() which
	 * does take a ref. Releasing this one drops OBS's own reference to zero, frees the
	 * service out from under the frontend, and crashes inside obs_service_release.
	 */
	obs_service_t *svc = obs_frontend_get_streaming_service();
	if (!svc)
		return QString();

	obs_data_t *settings = obs_service_get_settings(svc);
	if (!settings)
		return QString();

	const QString value = QString::fromUtf8(obs_data_get_string(settings, field));
	obs_data_release(settings); /* obs_service_get_settings DOES return a new ref */
	return value;
}

} // namespace

bool isStreamingActive()
{
	return obs_frontend_streaming_active();
}

void startMainStream()
{
	if (!obs_frontend_streaming_active())
		obs_frontend_streaming_start();
}

void stopMainStream()
{
	if (obs_frontend_streaming_active())
		obs_frontend_streaming_stop();
}

QString currentServer()
{
	return currentServiceField("server");
}

QString currentKey()
{
	return currentServiceField("key");
}

bool applyCustomService(const QString &server, const QString &key, QString *error)
{
	if (server.trimmed().isEmpty()) {
		if (error)
			*error = QStringLiteral("No ingest URL resolved for this entry.");
		return false;
	}

	if (isStreamingActive()) {
		if (error)
			*error = QStringLiteral(
				"OBS is streaming right now. Stop the stream before "
				"changing the ingest endpoint.");
		return false;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "server", server.toUtf8().constData());
	obs_data_set_string(settings, "key", key.toUtf8().constData());

	obs_service_t *svc =
		obs_service_create("rtmp_custom", "kaltura_service", settings, nullptr);

	if (!svc) {
		obs_data_release(settings);
		if (error)
			*error = QStringLiteral("OBS refused to create the custom service.");
		return false;
	}

	obs_frontend_set_streaming_service(svc);
	obs_frontend_save_streaming_service();

	obs_service_release(svc);
	obs_data_release(settings);

	/* Deliberately not logging the URL: it carries the ?t= ingest token. */
	obs_log(LOG_INFO, "applied Kaltura ingest endpoint to OBS stream settings");
	return true;
}

} // namespace obsbridge
