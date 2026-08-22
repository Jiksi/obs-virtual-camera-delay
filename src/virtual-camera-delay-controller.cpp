#include "virtual-camera-delay-controller.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#include <memory>

namespace {
constexpr const char *kDelaySourceId = "virtual_camera_delay_program_source";
constexpr const char *kConfigSection = "VirtualCameraDelay";
constexpr const char *kConfigDelayMs = "DelayMs";
constexpr uint64_t kDefaultDelayMs = 3000;

enum class DelayState {
    Idle,
    Passthrough,
    StoppingForRewire,
    StartingDelayed,
    ActiveDelayed,
    StoppingNormally,
};

class VirtualCameraDelayController {
public:
    bool Load()
    {
        config_t *config = obs_frontend_get_profile_config();
        if (config)
            config_set_default_uint(config, kConfigSection, kConfigDelayMs,
                                    kDefaultDelayMs);

        obs_data_t *settings = obs_data_create();
        obs_data_set_int(settings, "delay_ms",
                         config ? static_cast<long long>(config_get_uint(
                                      config, kConfigSection, kConfigDelayMs))
                                : static_cast<long long>(kDefaultDelayMs));

        delaySource_ = obs_source_create_private(
            kDelaySourceId, obs_module_text("VirtualCameraDelaySource"),
            settings);
        obs_data_release(settings);

        if (!delaySource_) {
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] failed to create private delay source");
            return false;
        }

        obs_frontend_add_event_callback(FrontendEvent, this);
        obs_frontend_add_tools_menu_item(
            obs_module_text("VirtualCameraDelaySettings"), OpenSettings, this);

        loaded_ = true;
        return true;
    }

    void Unload()
    {
        if (!loaded_)
            return;

        shuttingDown_ = true;
        obs_frontend_remove_event_callback(FrontendEvent, this);

        if (output_ && delayedVideo_ && obs_output_active(output_))
            obs_output_force_stop(output_);

        if (!output_ || !obs_output_active(output_)) {
            RestoreProgramMedia();
            DestroyDelayedView();
        } else {
            blog(LOG_WARNING,
                 "[obs-virtual-camera-delay] output is still stopping during "
                 "module unload; OBS will release the remaining view");
        }

        DetachOutput();

        if (delaySource_) {
            obs_source_release(delaySource_);
            delaySource_ = nullptr;
        }

        loaded_ = false;
    }

private:
    static void FrontendEvent(enum obs_frontend_event event, void *data)
    {
        static_cast<VirtualCameraDelayController *>(data)->OnFrontendEvent(event);
    }

    static void OpenSettings(void *data)
    {
        auto *controller = static_cast<VirtualCameraDelayController *>(data);
        if (controller->delaySource_)
            obs_frontend_open_source_properties(controller->delaySource_);
    }

    static void OutputDeactivated(void *data, calldata_t *)
    {
        auto *controller = static_cast<VirtualCameraDelayController *>(data);
        obs_queue_task(OBS_TASK_UI, FinishOutputDeactivated, controller, false);
    }

    static void FinishOutputDeactivated(void *data)
    {
        static_cast<VirtualCameraDelayController *>(data)->OnOutputDeactivated();
    }

    void OnFrontendEvent(enum obs_frontend_event event)
    {
        switch (event) {
        case OBS_FRONTEND_EVENT_FINISHED_LOADING:
            // The built-in Virtual Camera output is created by the frontend's
            // output handler, which is not available while modules are loading.
            AttachOutput();
            if (obs_frontend_virtualcam_active())
                HandleVirtualCameraStarted();
            break;
        case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
            HandleVirtualCameraStarted();
            break;
        case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
            HandleVirtualCameraStopped();
            break;
        case OBS_FRONTEND_EVENT_EXIT:
            shuttingDown_ = true;
            break;
        default:
            break;
        }
    }

    void AttachOutput()
    {
        obs_output_t *output = obs_frontend_get_virtualcam_output();
        if (output == output_)
            return;

        DetachOutput();
        if (!output)
            return;

        output_ = obs_output_get_ref(output);
        outputSignals_ = obs_output_get_signal_handler(output_);
        signal_handler_connect(outputSignals_, "deactivate", OutputDeactivated,
                               this);
    }

    void DetachOutput()
    {
        if (outputSignals_) {
            signal_handler_disconnect(outputSignals_, "deactivate",
                                      OutputDeactivated, this);
            outputSignals_ = nullptr;
        }

        if (output_) {
            obs_output_release(output_);
            output_ = nullptr;
        }
    }

    void HandleVirtualCameraStarted()
    {
        if (shuttingDown_)
            return;

        AttachOutput();
        if (!output_)
            return;

        if (state_ == DelayState::StartingDelayed) {
            state_ = DelayState::ActiveDelayed;
            blog(LOG_INFO,
                 "[obs-virtual-camera-delay] delayed Virtual Camera pipeline active");
            return;
        }

        if (state_ == DelayState::ActiveDelayed ||
            state_ == DelayState::StoppingForRewire)
            return;

        if (obs_output_video(output_) != obs_get_video()) {
            state_ = DelayState::Passthrough;
            blog(LOG_WARNING,
                 "[obs-virtual-camera-delay] Virtual Camera delay requires "
                 "Program output mode; leaving the current Virtual Camera "
                 "output unchanged");
            return;
        }

        state_ = DelayState::StoppingForRewire;
        blog(LOG_INFO,
             "[obs-virtual-camera-delay] restarting Virtual Camera once to "
             "attach the delayed Program view");
        obs_frontend_stop_virtualcam();
    }

    void HandleVirtualCameraStopped()
    {
        if (state_ == DelayState::StoppingForRewire)
            return;

        if (state_ == DelayState::ActiveDelayed ||
            state_ == DelayState::StartingDelayed) {
            state_ = DelayState::StoppingNormally;
        } else {
            state_ = DelayState::Idle;
        }
    }

    void OnOutputDeactivated()
    {
        if (state_ == DelayState::StoppingForRewire && !shuttingDown_) {
            StartDelayedOutput();
            return;
        }

        if (state_ == DelayState::StoppingNormally ||
            state_ == DelayState::Passthrough || shuttingDown_) {
            RestoreProgramMedia();
            DestroyDelayedView();
            state_ = DelayState::Idle;
        }
    }

    void StartDelayedOutput()
    {
        if (!CreateDelayedView()) {
            state_ = DelayState::Idle;
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] failed to create delayed video view");
            return;
        }

        obs_output_set_media(output_, delayedVideo_, obs_get_audio());
        state_ = DelayState::StartingDelayed;

        if (!obs_output_start(output_)) {
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] failed to restart Virtual Camera "
                 "with delayed media");
            RestoreProgramMedia();
            DestroyDelayedView();
            state_ = DelayState::Idle;
        }
    }

    bool CreateDelayedView()
    {
        if (delayedVideo_)
            return true;
        if (!delaySource_)
            return false;

        delayedView_ = obs_view_create();
        if (!delayedView_)
            return false;

        obs_view_set_source(delayedView_, 0, delaySource_);
        delayedVideo_ = obs_view_add(delayedView_);
        if (!delayedVideo_) {
            obs_view_set_source(delayedView_, 0, nullptr);
            obs_view_destroy(delayedView_);
            delayedView_ = nullptr;
            return false;
        }

        return true;
    }

    void RestoreProgramMedia()
    {
        if (output_ && !obs_output_active(output_))
            obs_output_set_media(output_, obs_get_video(), obs_get_audio());
    }

    void DestroyDelayedView()
    {
        if (!delayedView_)
            return;

        if (delayedVideo_)
            obs_view_remove(delayedView_);
        obs_view_set_source(delayedView_, 0, nullptr);
        obs_view_destroy(delayedView_);
        delayedView_ = nullptr;
        delayedVideo_ = nullptr;
    }

    obs_output_t *output_ = nullptr;
    signal_handler_t *outputSignals_ = nullptr;
    obs_source_t *delaySource_ = nullptr;
    obs_view_t *delayedView_ = nullptr;
    video_t *delayedVideo_ = nullptr;
    DelayState state_ = DelayState::Idle;
    bool loaded_ = false;
    bool shuttingDown_ = false;
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
