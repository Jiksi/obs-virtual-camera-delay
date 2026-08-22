#pragma once

#include "frame-buffer.hpp"

#include <media-io/video-io.h>
#include <obs-module.h>

constexpr const char *kVirtualCameraDelaySourceId =
    "virtual_camera_delay_private_source";

const obs_source_info *GetVirtualCameraDelaySourceInfo();

void SetDelayedVideoTarget(obs_source_t *source);
void ClearDelayedVideoTarget();
bool OutputDelayedVideoFrame(
    const virtual_camera_delay::BufferedNv12Frame &frame,
    uint64_t deliveryTimestamp, enum video_colorspace colorSpace,
    enum video_range_type range);
