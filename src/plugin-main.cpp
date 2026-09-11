/*
Kaltura Live Control - an OBS dock for Kaltura live entries.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMainWindow>

#include "plugin-support.h"
#include "ui/KalturaDock.hpp"

/*
 * obs-module.h declares these inside extern "C", so defining them here in C++ gives
 * them C linkage automatically - no extra wrapping needed.
 */
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Search, create and wire Kaltura live entries straight into OBS stream settings.";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Kaltura Live Control";
}

bool obs_module_load(void)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_ERROR, "no OBS main window; dock not registered");
		return false;
	}

	/*
	 * OBS 30 deprecated obs_frontend_add_dock(QDockWidget*) in favour of
	 * obs_frontend_add_dock_by_id(), which takes a plain QWidget and wraps it
	 * itself. OBS owns the widget after this call.
	 */
	auto *dock = new KalturaDock(mainWindow);

	if (!obs_frontend_add_dock_by_id("kaltura_live_control", "Kaltura Live Control",
					 dock)) {
		obs_log(LOG_ERROR, "obs_frontend_add_dock_by_id failed");
		delete dock;
		return false;
	}

	obs_log(LOG_INFO, "loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "unloaded");
}
