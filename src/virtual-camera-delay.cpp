#include "virtual-camera-delay.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <algorithm>
#include <mutex>
#include <new>

namespace {
constexpr const char *kSettingEnabled = "enabled";
constexpr const char *kSettingDelayMs = "delay_ms";
constexpr const char *kConfigSection = "VirtualCameraDelay";
constexpr const char *kConfigEnabled = "Enabled";
constexpr const char *kConfigDelayMs = "DelayMs";

struct DelaySourceContext {};

std::mutex targetMutex;
obs_source_t *targetSource = nullptr;

const char *DelayGetName(void *)
{
    return obs_module_text("VirtualCameraDelaySource");
}

void *DelayCreate(obs_data_t *settings, obs_source_t *)
{
    auto *context = new (std::nothrow) DelaySourceContext;
    if (!context)
        return nullptr;

    const bool enabled = obs_data_get_bool(settings, kSettingEnabled);
    const int64_t delay = std::clamp<int64_t>(
        obs_data_get_int(settings, kSettingDelayMs), 0,
        virtual_camera_delay::kMaximumDelayMs);
    obs_data_set_bool(settings, kSettingEnabled, enabled);
    obs_data_set_int(settings, kSettingDelayMs, delay);
    return context;
}

void DelayDestroy(void *data)
{
    delete static_cast<DelaySourceContext *>(data);
}

void DelayDefaults(obs_data_t *settings)
{
    obs_data_set_default_bool(settings, kSettingEnabled, true);
    obs_data_set_default_int(settings, kSettingDelayMs,
                             virtual_camera_delay::kDefaultDelayMs);
}

void DelayUpdate(void *, obs_data_t *settings)
{
    const bool enabled = obs_data_get_bool(settings, kSettingEnabled);
    const int64_t delay = std::clamp<int64_t>(
        obs_data_get_int(settings, kSettingDelayMs), 0,
        virtual_camera_delay::kMaximumDelayMs);
    obs_data_set_int(settings, kSettingDelayMs, delay);

    config_t *config = obs_frontend_get_profile_config();
    if (!config)
        return;
    config_set_bool(config, kConfigSection, kConfigEnabled, enabled);
    config_set_uint(config, kConfigSection, kConfigDelayMs,
                    static_cast<uint64_t>(delay));
    config_save_safe(config, "tmp", nullptr);
}

obs_properties_t *DelayProperties(void *)
{
    obs_properties_t *properties = obs_properties_create();
    obs_properties_add_bool(properties, kSettingEnabled,
                            obs_module_text("EnableDelay"));
    obs_property_t *delay = obs_properties_add_int(
        properties, kSettingDelayMs, obs_module_text("DelayMs"), 0,
        virtual_camera_delay::kMaximumDelayMs, 1);
    obs_property_int_set_suffix(delay, " ms");
    obs_properties_add_text(
        properties, "apply_notice",
        obs_module_text(obs_frontend_virtualcam_active()
                            ? "SettingsApplyNextStartActive"
                            : "SettingsApplyNextStartInactive"),
        OBS_TEXT_INFO);
    obs_properties_add_text(properties, "memory_notice",
                            obs_module_text("MemoryNotice"), OBS_TEXT_INFO);
    return properties;
}
} // namespace

const obs_source_info *GetVirtualCameraDelaySourceInfo()
{
    static obs_source_info info{};
    static bool initialized = false;
    if (!initialized) {
        info.id = kVirtualCameraDelaySourceId;
        info.type = OBS_SOURCE_TYPE_INPUT;
        info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
        info.get_name = DelayGetName;
        info.create = DelayCreate;
        info.destroy = DelayDestroy;
        info.get_defaults = DelayDefaults;
        info.update = DelayUpdate;
        info.get_properties = DelayProperties;
        initialized = true;
    }
    return &info;
}

void SetDelayedVideoTarget(obs_source_t *source)
{
    obs_source_t *replacement = source ? obs_source_get_ref(source) : nullptr;
    obs_source_t *previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(targetMutex);
        previous = targetSource;
        targetSource = replacement;
    }
    if (previous)
        obs_source_release(previous);
}

void ClearDelayedVideoTarget()
{
    obs_source_t *previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(targetMutex);
        previous = targetSource;
        targetSource = nullptr;
    }
    if (!previous)
        return;
    obs_source_output_video(previous, nullptr);
    obs_source_release(previous);
}

bool OutputDelayedVideoFrame(
    const virtual_camera_delay::BufferedNv12Frame &frame,
    uint64_t deliveryTimestamp, enum video_colorspace colorSpace,
    enum video_range_type range)
{
    obs_source_t *source = nullptr;
    {
        std::lock_guard<std::mutex> lock(targetMutex);
        if (targetSource)
            source = obs_source_get_ref(targetSource);
    }
    if (!source)
        return false;

    obs_source_frame2 output{};
    output.data[0] = const_cast<uint8_t *>(frame.Y());
    output.data[1] = const_cast<uint8_t *>(frame.UV());
    output.linesize[0] = frame.width;
    output.linesize[1] = frame.width;
    output.width = frame.width;
    output.height = frame.height;
    output.timestamp = deliveryTimestamp;
    output.format = VIDEO_FORMAT_NV12;
    output.range = range;
    video_format_get_parameters(colorSpace, range, output.color_matrix,
                                output.color_range_min,
                                output.color_range_max);
    obs_source_output_video2(source, &output);
    obs_source_release(source);
    return true;
}
