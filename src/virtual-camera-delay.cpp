#include "virtual-camera-delay.hpp"

#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <util/config-file.h>
#include <util/util_uint64.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <new>

namespace {
constexpr const char *kSettingDelayMs = "delay_ms";
constexpr const char *kConfigSection = "VirtualCameraDelay";
constexpr const char *kConfigDelayMs = "DelayMs";
constexpr int kDefaultDelayMs = 3000;
constexpr int kMaxDelayMs = 10000;

struct DelayedTexture {
    gs_texrender_t *render = nullptr;
    uint64_t sequence = 0;
};

class VirtualCameraDelay {
public:
    explicit VirtualCameraDelay(obs_source_t *)
    {
        RefreshVideoGeometry();
    }

    ~VirtualCameraDelay()
    {
        ReleaseFrames();
    }

    void Update(obs_data_t *settings)
    {
        const int64_t delayMs = std::clamp<int64_t>(
            obs_data_get_int(settings, kSettingDelayMs), 0, kMaxDelayMs);
        obs_data_set_int(settings, kSettingDelayMs, delayMs);
        delayNs_ = static_cast<uint64_t>(delayMs) * 1000000ULL;

        config_t *config = obs_frontend_get_profile_config();
        if (config) {
            config_set_uint(config, kConfigSection, kConfigDelayMs,
                            static_cast<uint64_t>(delayMs));
            config_save_safe(config, "tmp", nullptr);
        }

        if (frameIntervalNs_)
            RebuildBuffer();
    }

    void Tick()
    {
        renderedThisTick_ = false;
        RefreshVideoGeometry();
    }

    void Render()
    {
        if (!videoValid_ || frames_.empty()) {
            return;
        }

        if (renderedThisTick_) {
            DrawTexture(frames_.front());
            return;
        }

        DelayedTexture writeSlot = frames_.front();
        frames_.pop_front();

        gs_texrender_reset(writeSlot.render);

        if (gs_texrender_begin(writeSlot.render, width_, height_)) {
            vec4 clear{};
            gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
            gs_ortho(0.0f, static_cast<float>(width_), 0.0f,
                     static_cast<float>(height_), -100.0f, 100.0f);

            RenderProgramView();

            gs_texrender_end(writeSlot.render);
            writeSlot.sequence = ++sequence_;
        }

        frames_.push_back(writeSlot);
        DrawTexture(frames_.front());
        renderedThisTick_ = true;
    }

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }

    void Deactivate()
    {
        renderedThisTick_ = false;
        sequence_ = 0;
        ReleaseFrames();
    }

private:
    size_t DesiredFrameCount() const
    {
        if (!frameIntervalNs_)
            return 0;

        const uint64_t count =
            (delayNs_ + frameIntervalNs_ - 1) / frameIntervalNs_;
        return static_cast<size_t>(count > 0 ? count : 1);
    }

    void ReleaseFrames()
    {
        if (frames_.empty())
            return;

        obs_enter_graphics();
        for (auto &frame : frames_) {
            if (frame.render)
                gs_texrender_destroy(frame.render);
        }
        obs_leave_graphics();

        frames_.clear();
    }

    void RebuildBuffer()
    {
        ReleaseFrames();

        if (!videoValid_ || !width_ || !height_ || !frameIntervalNs_)
            return;

        const size_t count = DesiredFrameCount();

        obs_enter_graphics();
        for (size_t i = 0; i < count; ++i) {
            DelayedTexture frame;
            frame.render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
            if (!frame.render)
                break;
            frames_.push_back(frame);
        }
        obs_leave_graphics();
    }

    void RefreshVideoGeometry()
    {
        obs_video_info ovi{};
        if (!obs_get_video_info(&ovi) || !ovi.fps_num) {
            videoValid_ = false;
            return;
        }

        const uint32_t width = ovi.base_width;
        const uint32_t height = ovi.base_height;

        const uint64_t interval = util_mul_div64(
            static_cast<uint64_t>(ovi.fps_den), 1000000000ULL,
            static_cast<uint64_t>(ovi.fps_num));

        if (!width || !height || !interval) {
            videoValid_ = false;
            return;
        }

        videoValid_ = true;

        if (width_ != width || height_ != height ||
            frameIntervalNs_ != interval || frames_.empty()) {
            width_ = width;
            height_ = height;
            frameIntervalNs_ = interval;
            RebuildBuffer();
        }
    }

    void RenderProgramView()
    {
        for (uint32_t channel = 0; channel < MAX_CHANNELS; ++channel) {
            obs_source_t *source = obs_get_output_source(channel);
            if (!source)
                continue;

            obs_source_video_render(source);
            obs_source_release(source);
        }
    }

    void DrawTexture(const DelayedTexture &frame)
    {
        if (!frame.render)
            return;

        gs_texture_t *texture = gs_texrender_get_texture(frame.render);
        if (!texture)
            return;

        gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
        gs_effect_set_texture(image, texture);

        while (gs_effect_loop(effect, "Draw"))
            gs_draw_sprite(texture, 0, width_, height_);
    }

    std::deque<DelayedTexture> frames_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t frameIntervalNs_ = 0;
    uint64_t delayNs_ = 0;
    uint64_t sequence_ = 0;
    bool videoValid_ = false;
    bool renderedThisTick_ = false;
};

const char *DelayGetName(void *)
{
    return obs_module_text("VirtualCameraDelaySource");
}

void *DelayCreate(obs_data_t *settings, obs_source_t *context)
{
    auto *filter = new (std::nothrow) VirtualCameraDelay(context);
    if (!filter)
        return nullptr;

    filter->Update(settings);
    return filter;
}

void DelayDestroy(void *data)
{
    delete static_cast<VirtualCameraDelay *>(data);
}

uint32_t DelayWidth(void *data)
{
    return static_cast<VirtualCameraDelay *>(data)->Width();
}

uint32_t DelayHeight(void *data)
{
    return static_cast<VirtualCameraDelay *>(data)->Height();
}

void DelayDefaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, kSettingDelayMs, kDefaultDelayMs);
}

void DelayUpdate(void *data, obs_data_t *settings)
{
    static_cast<VirtualCameraDelay *>(data)->Update(settings);
}

obs_properties_t *DelayProperties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_property_t *delay = obs_properties_add_int(
        props, kSettingDelayMs, obs_module_text("DelayMs"), 0,
        kMaxDelayMs, 1);
    obs_property_int_set_suffix(delay, " ms");

    obs_properties_add_text(props, "memory_notice",
                            obs_module_text("MemoryNotice"), OBS_TEXT_INFO);
    return props;
}

void DelayTick(void *data, float)
{
    static_cast<VirtualCameraDelay *>(data)->Tick();
}

void DelayRender(void *data, gs_effect_t *)
{
    static_cast<VirtualCameraDelay *>(data)->Render();
}

void DelayDeactivate(void *data)
{
    static_cast<VirtualCameraDelay *>(data)->Deactivate();
}
} // namespace

const obs_source_info *GetVirtualCameraDelaySourceInfo()
{
    static obs_source_info info{};
    static bool initialized = false;

    if (!initialized) {
        info.id = "virtual_camera_delay_program_source";
        info.type = OBS_SOURCE_TYPE_INPUT;
        info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
        info.get_name = DelayGetName;
        info.create = DelayCreate;
        info.destroy = DelayDestroy;
        info.get_width = DelayWidth;
        info.get_height = DelayHeight;
        info.get_defaults = DelayDefaults;
        info.update = DelayUpdate;
        info.get_properties = DelayProperties;
        info.video_tick = DelayTick;
        info.video_render = DelayRender;
        info.deactivate = DelayDeactivate;
        initialized = true;
    }

    return &info;
}
