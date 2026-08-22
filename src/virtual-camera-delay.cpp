#include "virtual-camera-delay.hpp"

#include <graphics/graphics.h>
#include <util/util_uint64.h>

#include <cstdint>
#include <deque>
#include <new>

namespace {
constexpr const char *kSettingDelayMs = "delay_ms";
constexpr int kDefaultDelayMs = 3000;
constexpr int kMaxDelayMs = 10000;

struct DelayedTexture {
    gs_texrender_t *render = nullptr;
    uint64_t sequence = 0;
};

class VirtualCameraDelay {
public:
    explicit VirtualCameraDelay(obs_source_t *context) : context_(context) {}

    ~VirtualCameraDelay()
    {
        ReleaseFrames();
    }

    void Update(obs_data_t *settings)
    {
        const int64_t delayMs = obs_data_get_int(settings, kSettingDelayMs);
        delayNs_ = static_cast<uint64_t>(delayMs) * 1000000ULL;

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
        obs_source_t *target = obs_filter_get_target(context_);
        obs_source_t *parent = obs_filter_get_parent(context_);

        if (!targetValid_ || !target || !parent || frames_.empty()) {
            obs_source_skip_video_filter(context_);
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

            const uint32_t flags = obs_source_get_output_flags(target);
            const bool customDraw = (flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
            const bool asyncVideo = (flags & OBS_SOURCE_ASYNC) != 0;

            if (target == parent && !customDraw && !asyncVideo)
                obs_source_default_render(target);
            else
                obs_source_video_render(target);

            gs_texrender_end(writeSlot.render);
            writeSlot.sequence = ++sequence_;
        }

        frames_.push_back(writeSlot);
        DrawTexture(frames_.front());
        renderedThisTick_ = true;
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

        if (!targetValid_ || !width_ || !height_ || !frameIntervalNs_)
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
        obs_source_t *target = obs_filter_get_target(context_);
        targetValid_ = target != nullptr;

        if (!target) {
            ReleaseFrames();
            return;
        }

        const uint32_t width = obs_source_get_base_width(target);
        const uint32_t height = obs_source_get_base_height(target);

        obs_video_info ovi{};
        if (!obs_get_video_info(&ovi) || !ovi.fps_num) {
            targetValid_ = false;
            return;
        }

        const uint64_t interval = util_mul_div64(
            static_cast<uint64_t>(ovi.fps_den), 1000000000ULL,
            static_cast<uint64_t>(ovi.fps_num));

        if (!width || !height || !interval) {
            targetValid_ = false;
            return;
        }

        if (width_ != width || height_ != height ||
            frameIntervalNs_ != interval || frames_.empty()) {
            width_ = width;
            height_ = height;
            frameIntervalNs_ = interval;
            RebuildBuffer();
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

    obs_source_t *context_ = nullptr;
    std::deque<DelayedTexture> frames_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t frameIntervalNs_ = 0;
    uint64_t delayNs_ = 0;
    uint64_t sequence_ = 0;
    bool targetValid_ = false;
    bool renderedThisTick_ = false;
};

const char *DelayGetName(void *)
{
    return obs_module_text("VirtualCameraDelayFilter");
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
} // namespace

const obs_source_info *GetVirtualCameraDelayFilterInfo()
{
    static obs_source_info info{};
    static bool initialized = false;

    if (!initialized) {
        info.id = "virtual_camera_delay_filter";
        info.type = OBS_SOURCE_TYPE_FILTER;
        info.output_flags = OBS_SOURCE_VIDEO;
        info.get_name = DelayGetName;
        info.create = DelayCreate;
        info.destroy = DelayDestroy;
        info.get_defaults = DelayDefaults;
        info.update = DelayUpdate;
        info.get_properties = DelayProperties;
        info.video_tick = DelayTick;
        info.video_render = DelayRender;
        initialized = true;
    }

    return &info;
}
