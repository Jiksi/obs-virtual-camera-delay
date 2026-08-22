#include <obs-module.h>

#include "delayed-virtual-camera-output.hpp"
#include "virtual-camera-delay.hpp"
#include "virtual-camera-delay-controller.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-virtual-camera-delay", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Timestamp-based delay for the built-in OBS Virtual Camera.";
}

bool obs_module_load(void)
{
    obs_register_output(GetDelayedVirtualCameraOutputInfo());
    obs_register_source(GetVirtualCameraDelaySourceInfo());

    const bool loaded = LoadVirtualCameraDelayController();
    blog(loaded ? LOG_INFO : LOG_ERROR,
         "[obs-virtual-camera-delay] %s",
         loaded ? "loaded" : "failed to initialize");
    return loaded;
}

void obs_module_unload(void)
{
    UnloadVirtualCameraDelayController();
    blog(LOG_INFO, "[obs-virtual-camera-delay] unloaded");
}
