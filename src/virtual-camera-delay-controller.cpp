#include "virtual-camera-delay-controller.hpp"

#include "delayed-virtual-camera-output.hpp"
#include "frame-buffer.hpp"
#include "virtual-camera-delay.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#include <memory>

namespace {
constexpr const char *kConfigSection = "VirtualCameraDelay";
constexpr const char *kConfigEnabled = "Enabled";
constexpr const char *kConfigDelayMs = "DelayMs";

enum class DelayState {
    Idle,
    Passthrough,
    StoppingForRewire,
    StartingDelayed,
    ActiveDelayed,
};

class VirtualCameraDelayController {
public:
    bool Load()
    {
        config_t *config = obs_frontend_get_profile_config();
        if (config) {
            config_set_default_bool(config, kConfigSection, kConfigEnabled,
                                    true);
            config_set_default_uint(config, kConfigSection, kConfigDelayMs,
                                    virtual_camera_delay::kDefaultDelayMs);
        }

        obs_data_t *settings = obs_data_create();
        obs_data_set_bool(settings, "enabled",
                          config ? config_get_bool(config, kConfigSection,
                                                  kConfigEnabled)
                                 : true);
        obs_data_set_int(
            settings, "delay_ms",
            config ? static_cast<long long>(config_get_uint(
                         config, kConfigSection, kConfigDelayMs))
                   : virtual_camera_delay::kDefaultDelayMs);
        delaySource_ = obs_source_create_private(
            kVirtualCameraDelaySourceId,
            obs_module_text("VirtualCameraDelaySource"), settings);
        obs_data_release(settings);
        if (!delaySource_) {
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] failed to create private delayed "
                 "video source");
            return false;
        }

        obs_frontend_add_event_callback(FrontendEvent, this);
        obsSignals_ = obs_get_signal_handler();
        if (obsSignals_)
            signal_handler_connect(obsSignals_, "video_reset", VideoReset, this);
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
        if (obsSignals_) {
            signal_handler_disconnect(obsSignals_, "video_reset", VideoReset, this);
            obsSignals_ = nullptr;
        }

        DestroyFeeder();
        ClearDelayedVideoTarget();
        if (virtualCameraOutput_ && obs_output_active(virtualCameraOutput_))
            obs_output_force_stop(virtualCameraOutput_);
        RestoreOriginalMedia();
        DestroyDelayedView();
        DetachVirtualCameraOutput();

        if (delaySource_) {
            obs_source_release(delaySource_);
            delaySource_ = nullptr;
        }
        loaded_ = false;
    }

private:
    static void FrontendEvent(obs_frontend_event event, void *data)
    {
        static_cast<VirtualCameraDelayController *>(data)->OnFrontendEvent(event);
    }

    static void OpenSettings(void *data)
    {
        auto *controller = static_cast<VirtualCameraDelayController *>(data);
        if (controller->delaySource_)
            obs_frontend_open_source_properties(controller->delaySource_);
    }

    static void VideoReset(void *data, calldata_t *)
    {
        obs_queue_task(OBS_TASK_UI, FinishVideoReset, data, false);
    }

    static void FinishVideoReset(void *data)
    {
        static_cast<VirtualCameraDelayController *>(data)
            ->HandleVideoSettingsChanged();
    }

    static void OutputDeactivated(void *data, calldata_t *)
    {
        obs_queue_task(OBS_TASK_UI, FinishOutputDeactivated, data, false);
    }

    static void FinishOutputDeactivated(void *data)
    {
        static_cast<VirtualCameraDelayController *>(data)->OnOutputDeactivated();
    }

    void OnFrontendEvent(obs_frontend_event event)
    {
        switch (event) {
        case OBS_FRONTEND_EVENT_FINISHED_LOADING:
            AttachVirtualCameraOutput();
            if (obs_frontend_virtualcam_active())
                HandleVirtualCameraStarted();
            break;
        case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
            HandleVirtualCameraStarted();
            break;
        case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
            if (state_ != DelayState::StoppingForRewire)
                FinishNormalStop();
            break;
        case OBS_FRONTEND_EVENT_EXIT:
            shuttingDown_ = true;
            break;
        default:
            break;
        }
    }

    void AttachVirtualCameraOutput()
    {
        obs_output_t *output = obs_frontend_get_virtualcam_output();
        if (output == virtualCameraOutput_)
            return;
        DetachVirtualCameraOutput();
        if (!output)
            return;

        virtualCameraOutput_ = obs_output_get_ref(output);
        outputSignals_ = obs_output_get_signal_handler(virtualCameraOutput_);
        signal_handler_connect(outputSignals_, "deactivate", OutputDeactivated,
                               this);
    }

    void DetachVirtualCameraOutput()
    {
        if (outputSignals_) {
            signal_handler_disconnect(outputSignals_, "deactivate",
                                      OutputDeactivated, this);
            outputSignals_ = nullptr;
        }
        if (virtualCameraOutput_) {
            obs_output_release(virtualCameraOutput_);
            virtualCameraOutput_ = nullptr;
        }
    }

    bool DelayEnabled() const
    {
        config_t *config = obs_frontend_get_profile_config();
        return !config ||
               config_get_bool(config, kConfigSection, kConfigEnabled);
    }

    uint32_t ConfiguredDelayMs() const
    {
        config_t *config = obs_frontend_get_profile_config();
        const uint64_t value =
            config ? config_get_uint(config, kConfigSection, kConfigDelayMs)
                   : virtual_camera_delay::kDefaultDelayMs;
        return static_cast<uint32_t>(
            value > virtual_camera_delay::kMaximumDelayMs
                ? virtual_camera_delay::kMaximumDelayMs
                : value);
    }

    void HandleVirtualCameraStarted()
    {
        if (shuttingDown_)
            return;
        AttachVirtualCameraOutput();
        if (!virtualCameraOutput_)
            return;

        if (state_ == DelayState::StartingDelayed) {
            state_ = DelayState::ActiveDelayed;
            blog(LOG_INFO,
                 "[obs-virtual-camera-delay] delayed OBS Virtual Camera is "
                 "active (%u ms configured)",
                 ConfiguredDelayMs());
            return;
        }
        if (state_ == DelayState::ActiveDelayed ||
            state_ == DelayState::StoppingForRewire)
            return;
        if (!DelayEnabled()) {
            state_ = DelayState::Passthrough;
            blog(LOG_INFO,
                 "[obs-virtual-camera-delay] delay disabled; OBS Virtual "
                 "Camera remains real-time");
            return;
        }

        // Capture the media selected by the component that started the
        // built-in Virtual Camera. This may be the main OBS video output,
        // an Aitum Vertical canvas, or another custom canvas.
        originalVideo_ = obs_output_video(virtualCameraOutput_);
        originalAudio_ = obs_output_audio(virtualCameraOutput_);
        if (!originalVideo_) {
            blog(LOG_WARNING,
                 "[obs-virtual-camera-delay] Virtual Camera has no selected "
                 "video media; using the main OBS video output");
            originalVideo_ = obs_get_video();
        }
        state_ = DelayState::StoppingForRewire;
        blog(LOG_INFO,
             "[obs-virtual-camera-delay] attaching timestamp delay to the "
             "normal OBS Virtual Camera device");
        obs_frontend_stop_virtualcam();
    }

    void OnOutputDeactivated()
    {
        if (state_ == DelayState::StoppingForRewire && !shuttingDown_) {
            if (pendingVideoReconfigure_) {
                pendingVideoReconfigure_ = false;
                DestroyFeeder();
                ClearDelayedVideoTarget();
                DestroyDelayedView();

                // Keep the media captured when Virtual Camera was originally
                // started. In particular, do not replace an Aitum Vertical
                // canvas with obs_get_video() merely because the main OBS
                // video pipeline emitted video_reset.
                blog(LOG_INFO,
                     "[obs-virtual-camera-delay] rebuilding delayed pipeline "
                     "while preserving the selected Virtual Camera media");
            }
            StartDelayedPipeline();
            return;
        }
        if (state_ == DelayState::ActiveDelayed ||
            state_ == DelayState::StartingDelayed || shuttingDown_)
            FinishNormalStop();
    }

    void HandleVideoSettingsChanged()
    {
        if (shuttingDown_ || state_ != DelayState::ActiveDelayed ||
            !virtualCameraOutput_)
            return;

        // The delayed view and frame buffer were created for the previous
        // geometry. Stop the built-in Virtual Camera first and rebuild the
        // delay path, but preserve originalVideo_: it is the actual media
        // selected for Virtual Camera and may belong to Aitum Vertical rather
        // than the main OBS canvas.
        pendingVideoReconfigure_ = true;
        state_ = DelayState::StoppingForRewire;
        blog(LOG_INFO,
             "[obs-virtual-camera-delay] OBS video settings changed; "
             "restarting delayed Virtual Camera pipeline");
        obs_frontend_stop_virtualcam();
    }

    bool CreateDelayedView()
    {
        if (delayedVideo_)
            return true;
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

    bool CreateAndStartFeeder()
    {
        obs_data_t *settings = obs_data_create();
        obs_data_set_int(settings, "delay_ms", ConfiguredDelayMs());
        feederOutput_ = obs_output_create(kDelayedVirtualCameraOutputId,
                                          "virtual-camera-delay-feeder",
                                          settings, nullptr);
        obs_data_release(settings);
        if (!feederOutput_)
            return false;
        obs_output_set_media(feederOutput_, originalVideo_, nullptr);
        if (!obs_output_start(feederOutput_)) {
            obs_output_release(feederOutput_);
            feederOutput_ = nullptr;
            return false;
        }
        return true;
    }

    void StartDelayedPipeline()
    {
        if (!CreateDelayedView()) {
            FallBackToPassthrough("could not create delayed video view");
            return;
        }

        SetDelayedVideoTarget(delaySource_);
        obs_output_set_media(virtualCameraOutput_, delayedVideo_, originalAudio_);
        if (!CreateAndStartFeeder()) {
            FallBackToPassthrough("could not start NV12 delay feeder");
            return;
        }

        state_ = DelayState::StartingDelayed;
        if (!obs_output_start(virtualCameraOutput_))
            FallBackToPassthrough("could not restart OBS Virtual Camera");
    }

    void FallBackToPassthrough(const char *reason)
    {
        blog(LOG_ERROR,
             "[obs-virtual-camera-delay] %s; restoring real-time Virtual "
             "Camera",
             reason);
        DestroyFeeder();
        ClearDelayedVideoTarget();
        RestoreOriginalMedia();
        DestroyDelayedView();
        state_ = DelayState::Passthrough;
        if (virtualCameraOutput_ && !obs_output_active(virtualCameraOutput_))
            obs_output_start(virtualCameraOutput_);
    }

    void DestroyFeeder()
    {
        if (!feederOutput_)
            return;
        if (obs_output_active(feederOutput_))
            obs_output_stop(feederOutput_);
        obs_output_release(feederOutput_);
        feederOutput_ = nullptr;
    }

    void RestoreOriginalMedia()
    {
        if (virtualCameraOutput_ &&
            !obs_output_active(virtualCameraOutput_)) {
            obs_output_set_media(virtualCameraOutput_,
                                 originalVideo_ ? originalVideo_
                                                : obs_get_video(),
                                 originalAudio_ ? originalAudio_
                                                : obs_get_audio());
        }
        originalVideo_ = nullptr;
        originalAudio_ = nullptr;
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

    void FinishNormalStop()
    {
        DestroyFeeder();
        ClearDelayedVideoTarget();
        RestoreOriginalMedia();
        DestroyDelayedView();
        state_ = DelayState::Idle;
    }

    obs_output_t *virtualCameraOutput_ = nullptr;
    signal_handler_t *outputSignals_ = nullptr;
    signal_handler_t *obsSignals_ = nullptr;
    obs_output_t *feederOutput_ = nullptr;
    obs_source_t *delaySource_ = nullptr;
    obs_view_t *delayedView_ = nullptr;
    video_t *delayedVideo_ = nullptr;
    video_t *originalVideo_ = nullptr;
    audio_t *originalAudio_ = nullptr;
    DelayState state_ = DelayState::Idle;
    bool loaded_ = false;
    bool shuttingDown_ = false;
    bool pendingVideoReconfigure_ = false;
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
