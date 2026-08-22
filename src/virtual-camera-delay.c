#include "virtual-camera-delay.h"

#include <graphics/graphics.h>
#include <util/deque.h>
#include <util/util_uint64.h>

#define SETTING_DELAY_MS "delay_ms"
#define DEFAULT_DELAY_MS 3000
#define MAX_DELAY_MS 10000

struct delayed_texture {
    gs_texrender_t *render;
    uint64_t sequence;
};

struct virtual_camera_delay {
    obs_source_t *context;
    struct deque frames;
    uint32_t width;
    uint32_t height;
    uint64_t frame_interval_ns;
    uint64_t delay_ns;
    uint64_t sequence;
    bool target_valid;
    bool rendered_this_tick;
};

static const char *delay_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("VirtualCameraDelayFilter");
}

static size_t buffered_count(const struct virtual_camera_delay *filter)
{
    return filter->frames.size / sizeof(struct delayed_texture);
}

static void release_frames(struct virtual_camera_delay *filter)
{
    obs_enter_graphics();

    while (filter->frames.size) {
        struct delayed_texture item = {0};
        deque_pop_front(&filter->frames, &item, sizeof(item));
        if (item.render)
            gs_texrender_destroy(item.render);
    }

    deque_free(&filter->frames);
    obs_leave_graphics();
}

static size_t desired_frame_count(const struct virtual_camera_delay *filter)
{
    if (!filter->frame_interval_ns)
        return 0;

    uint64_t count = (filter->delay_ns + filter->frame_interval_ns - 1) /
                     filter->frame_interval_ns;
    return (size_t)(count > 0 ? count : 1);
}

static void rebuild_buffer(struct virtual_camera_delay *filter)
{
    release_frames(filter);

    if (!filter->target_valid || !filter->width || !filter->height ||
        !filter->frame_interval_ns)
        return;

    const size_t count = desired_frame_count(filter);

    obs_enter_graphics();
    for (size_t i = 0; i < count; i++) {
        struct delayed_texture item = {0};
        item.render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        deque_push_back(&filter->frames, &item, sizeof(item));
    }
    obs_leave_graphics();
}

static void refresh_video_geometry(struct virtual_camera_delay *filter)
{
    obs_source_t *target = obs_filter_get_target(filter->context);
    filter->target_valid = target != NULL;

    if (!target) {
        if (filter->frames.size)
            release_frames(filter);
        return;
    }

    const uint32_t width = obs_source_get_base_width(target);
    const uint32_t height = obs_source_get_base_height(target);

    struct obs_video_info ovi = {0};
    if (!obs_get_video_info(&ovi) || !ovi.fps_num) {
        filter->target_valid = false;
        return;
    }

    const uint64_t interval = util_mul_div64((uint64_t)ovi.fps_den,
                                              1000000000ULL,
                                              (uint64_t)ovi.fps_num);

    if (!width || !height || !interval) {
        filter->target_valid = false;
        return;
    }

    if (filter->width != width || filter->height != height ||
        filter->frame_interval_ns != interval || !filter->frames.size) {
        filter->width = width;
        filter->height = height;
        filter->frame_interval_ns = interval;
        rebuild_buffer(filter);
    }
}

static void delay_update(void *data, obs_data_t *settings)
{
    struct virtual_camera_delay *filter = data;
    const int64_t delay_ms = obs_data_get_int(settings, SETTING_DELAY_MS);
    filter->delay_ns = (uint64_t)delay_ms * 1000000ULL;

    if (filter->frame_interval_ns)
        rebuild_buffer(filter);
}

static void *delay_create(obs_data_t *settings, obs_source_t *context)
{
    struct virtual_camera_delay *filter = bzalloc(sizeof(*filter));
    filter->context = context;
    delay_update(filter, settings);
    return filter;
}

static void delay_destroy(void *data)
{
    struct virtual_camera_delay *filter = data;
    release_frames(filter);
    bfree(filter);
}

static void delay_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, SETTING_DELAY_MS, DEFAULT_DELAY_MS);
}

static obs_properties_t *delay_properties(void *data)
{
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();
    obs_property_t *delay = obs_properties_add_int(
        props, SETTING_DELAY_MS, obs_module_text("DelayMs"), 0, MAX_DELAY_MS, 1);
    obs_property_int_set_suffix(delay, " ms");

    obs_properties_add_text(
        props, "memory_notice",
        obs_module_text("MemoryNotice"), OBS_TEXT_INFO);

    return props;
}

static void delay_tick(void *data, float seconds)
{
    UNUSED_PARAMETER(seconds);
    struct virtual_camera_delay *filter = data;
    filter->rendered_this_tick = false;
    refresh_video_geometry(filter);
}

static void draw_texture(struct virtual_camera_delay *filter,
                         const struct delayed_texture *item)
{
    if (!item || !item->render)
        return;

    gs_texture_t *texture = gs_texrender_get_texture(item->render);
    if (!texture)
        return;

    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, texture);

    while (gs_effect_loop(effect, "Draw"))
        gs_draw_sprite(texture, 0, filter->width, filter->height);
}

static void delay_render(void *data, gs_effect_t *effect)
{
    UNUSED_PARAMETER(effect);

    struct virtual_camera_delay *filter = data;
    obs_source_t *target = obs_filter_get_target(filter->context);
    obs_source_t *parent = obs_filter_get_parent(filter->context);

    if (!filter->target_valid || !target || !parent || !filter->frames.size) {
        obs_source_skip_video_filter(filter->context);
        return;
    }

    struct delayed_texture front = {0};
    deque_peek_front(&filter->frames, &front, sizeof(front));

    if (filter->rendered_this_tick) {
        draw_texture(filter, &front);
        return;
    }

    struct delayed_texture write_slot = {0};
    deque_pop_front(&filter->frames, &write_slot, sizeof(write_slot));

    gs_texrender_reset(write_slot.render);

    if (gs_texrender_begin(write_slot.render, filter->width, filter->height)) {
        struct vec4 clear = {0};
        gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
        gs_ortho(0.0f, (float)filter->width, 0.0f, (float)filter->height,
                 -100.0f, 100.0f);

        const uint32_t flags = obs_source_get_output_flags(target);
        const bool custom_draw = (flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
        const bool async_video = (flags & OBS_SOURCE_ASYNC) != 0;

        if (target == parent && !custom_draw && !async_video)
            obs_source_default_render(target);
        else
            obs_source_video_render(target);

        gs_texrender_end(write_slot.render);
        write_slot.sequence = ++filter->sequence;
    }

    deque_push_back(&filter->frames, &write_slot, sizeof(write_slot));
    deque_peek_front(&filter->frames, &front, sizeof(front));
    draw_texture(filter, &front);
    filter->rendered_this_tick = true;
}

struct obs_source_info virtual_camera_delay_filter_info = {
    .id = "virtual_camera_delay_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = delay_get_name,
    .create = delay_create,
    .destroy = delay_destroy,
    .get_defaults = delay_defaults,
    .update = delay_update,
    .get_properties = delay_properties,
    .video_tick = delay_tick,
    .video_render = delay_render,
};
