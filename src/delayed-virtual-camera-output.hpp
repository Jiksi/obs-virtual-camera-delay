#pragma once

#include <obs-module.h>

constexpr const char *kDelayedVirtualCameraOutputId =
    "delayed_virtual_camera_output";

const obs_output_info *GetDelayedVirtualCameraOutputInfo();
