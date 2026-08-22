#include "virtual-camera-delay-controller.hpp"

#include "delayed-virtual-camera-output.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#include <memory>

namespace {
constexpr const char *kConfigSection = "VirtualCameraDelay";
constexpr const char *kConfigDelayMs = "DelayMs";
constexpr uint64_t kDefaultDelayMs = 3000;

class VirtualCameraDelayController {
public:
    bool Load()
    {
        config_t *config = obs_frontend_get_profile_config();
        if (config)
            config_set_default_uint(config, kConfigSection, kConfigDelayMs,
                                    kDefaultDelayMs);

        obs_frontend_add_event_callback(FrontendEvent, this);
        loaded_ = true;
        return true;
    }

    void Unload()
    {
        if (!loaded_)
            return;

        obs_frontend_remove_event_callback(FrontendEvent, this);
        DestroyOutput();
        loaded_ = false;
    }

private:
    static void FrontendEvent(obs_frontend_event event, void *data)
    {
        static_cast<VirtualCameraDelayController *>(data)->OnFrontendEvent(event);
    }

    void OnFrontendEvent(obs_frontend_event event)
    {
        switch (event) {
        case OBS_FRONTEND_EVENT_FINISHED_LOADING:
            CreateAndStartOutput();
            break;
        case OBS_FRONTEND_EVENT_EXIT:
            DestroyOutput();
            break;
        default:
            break;
        }
    }

    void CreateAndStartOutput()
    {
        if (output_)
            return;

        obs_data_t *settings = obs_data_create();
        config_t *config = obs_frontend_get_profile_config();
        const uint64_t delayMs =
            config ? config_get_uint(config, kConfigSection, kConfigDelayMs)
                   : kDefaultDelayMs;
        obs_data_set_int(settings, "delay_ms",
                         static_cast<long long>(delayMs));

        output_ = obs_output_create(kDelayedVirtualCameraOutputId,
                                    "delayed-virtual-camera-poc", settings,
                                    nullptr);
        obs_data_release(settings);

        if (!output_) {
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] failed to create custom output");
            return;
        }

        obs_output_set_media(output_, obs_get_video(), nullptr);
        if (!obs_output_start(output_)) {
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] failed to start custom output");
            obs_output_release(output_);
            output_ = nullptr;
            return;
        }

        blog(LOG_INFO,
             "[obs-virtual-camera-delay] custom delayed virtual camera output "
             "created and started independently of streaming and recording");
    }

    void DestroyOutput()
    {
        if (!output_)
            return;

        if (obs_output_active(output_))
            obs_output_stop(output_);
        obs_output_release(output_);
        output_ = nullptr;
    }

    obs_output_t *output_ = nullptr;
    bool loaded_ = false;
};

std::unique_ptr<VirtualCameraDelayController> controller;
} // namespace

bool LoadVirtualCameraDelayController()
{
    controller = std::make_unique<VirtualCameraDelayController>();
    if (controller->Load())
        return true;

    controller.reset();
    return false;
}

void UnloadVirtualCameraDelayController()
{
    if (!controller)
        return;

    controller->Unload();
    controller.reset();
}
