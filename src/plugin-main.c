#include <obs-module.h>
#include "virtual-camera-delay.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-virtual-camera-delay", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Long GPU render delay filter intended for a dedicated OBS Virtual Camera scene.";
}

bool obs_module_load(void)
{
    obs_register_source(&virtual_camera_delay_filter_info);
    blog(LOG_INFO, "[obs-virtual-camera-delay] loaded");
    return true;
}
