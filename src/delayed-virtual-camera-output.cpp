#include "delayed-virtual-camera-output.hpp"

#include "frame-buffer.hpp"
#include "virtual-camera-delay.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <new>

namespace {
constexpr const char *kSettingDelayMs = "delay_ms";

class DelayedVirtualCameraOutput {
public:
    explicit DelayedVirtualCameraOutput(obs_output_t *output) : output_(output) {}

    bool Start()
    {
        if (!obs_output_can_begin_data_capture(output_, 0))
            return false;

        obs_video_info videoInfo{};
        if (!obs_get_video_info(&videoInfo) || !videoInfo.fps_num ||
            !videoInfo.fps_den)
            return false;
        if (videoInfo.colorspace == VIDEO_CS_2100_PQ ||
            videoInfo.colorspace == VIDEO_CS_2100_HLG) {
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] HDR PQ/HLG is unsupported in "
                 "v0.1.0; use an SDR color space");
            return false;
        }

        width_ = obs_output_get_width(output_);
        height_ = obs_output_get_height(output_);
        if (!width_ || !height_)
            return false;

        video_scale_info conversion{};
        conversion.format = VIDEO_FORMAT_NV12;
        conversion.width = width_;
        conversion.height = height_;
        conversion.colorspace = videoInfo.colorspace;
        conversion.range = videoInfo.range;
        obs_output_set_video_conversion(output_, &conversion);

        colorSpace_ = videoInfo.colorspace;
        range_ = videoInfo.range;
        const auto config = buffer_.Configure(
            width_, height_, videoInfo.fps_num, videoInfo.fps_den,
            requestedDelayMs_);
        if (!config.frameLimit) {
            blog(LOG_ERROR,
                 "[obs-virtual-camera-delay] %ux%u NV12 frames exceed the "
                 "buffer memory limit",
                 width_, height_);
            return false;
        }
        if (config.clamped) {
            blog(LOG_WARNING,
                 "[obs-virtual-camera-delay] requested delay %u ms was "
                 "clamped to %u ms for %ux%u under the 768 MiB limit",
                 config.requestedDelayMs, config.delayMs, width_, height_);
        }

        active_.store(true, std::memory_order_release);
        if (!obs_output_begin_data_capture(output_, 0)) {
            active_.store(false, std::memory_order_release);
            buffer_.Reset();
            return false;
        }

        blog(LOG_INFO,
             "[obs-virtual-camera-delay] feeder started at %ux%u, delay=%u "
             "ms, expected buffer=%.1f MiB",
             width_, height_, config.delayMs,
             static_cast<double>(config.expectedBytes) / (1024.0 * 1024.0));
        return true;
    }

    void Stop()
    {
        if (!active_.exchange(false, std::memory_order_acq_rel)) {
            buffer_.Reset();
            return;
        }
        obs_output_end_data_capture(output_);
        const auto stats = buffer_.Stats();
        buffer_.Reset();
        blog(LOG_INFO,
             "[obs-virtual-camera-delay] feeder stopped: received=%llu, "
             "emitted=%llu, dropped=%llu",
             static_cast<unsigned long long>(stats.enqueued),
             static_cast<unsigned long long>(stats.emitted),
             static_cast<unsigned long long>(stats.dropped));
    }

    void Update(obs_data_t *settings)
    {
        const int64_t delayMs = std::clamp<int64_t>(
            obs_data_get_int(settings, kSettingDelayMs), 0,
            virtual_camera_delay::kMaximumDelayMs);
        obs_data_set_int(settings, kSettingDelayMs, delayMs);
        requestedDelayMs_ = static_cast<uint32_t>(delayMs);
    }

    void ReceiveVideo(const video_data *frame)
    {
        if (!active_.load(std::memory_order_acquire) || !frame ||
            !frame->data[0] || !frame->data[1])
            return;

        virtual_camera_delay::Nv12FrameView view;
        view.y = frame->data[0];
        view.uv = frame->data[1];
        view.yStride = frame->linesize[0];
        view.uvStride = frame->linesize[1];
        view.width = width_;
        view.height = height_;
        view.timestamp = frame->timestamp;
        auto ready = buffer_.Push(view);
        if (ready)
            OutputDelayedVideoFrame(*ready, frame->timestamp, colorSpace_, range_);
    }

private:
    obs_output_t *output_ = nullptr;
    virtual_camera_delay::TimestampFrameBuffer buffer_;
    std::atomic<bool> active_{false};
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t requestedDelayMs_ = virtual_camera_delay::kDefaultDelayMs;
    enum video_colorspace colorSpace_ = VIDEO_CS_DEFAULT;
    enum video_range_type range_ = VIDEO_RANGE_DEFAULT;
};

const char *OutputGetName(void *)
{
    return obs_module_text("DelayedVirtualCameraOutput");
}

void *OutputCreate(obs_data_t *settings, obs_output_t *output)
{
    auto *context = new (std::nothrow) DelayedVirtualCameraOutput(output);
    if (!context)
        return nullptr;
    context->Update(settings);
    return context;
}

void OutputDestroy(void *data)
{
    auto *context = static_cast<DelayedVirtualCameraOutput *>(data);
    context->Stop();
    delete context;
}

bool OutputStart(void *data)
{
    return static_cast<DelayedVirtualCameraOutput *>(data)->Start();
}

void OutputStop(void *data, uint64_t)
{
    static_cast<DelayedVirtualCameraOutput *>(data)->Stop();
}

void OutputRawVideo(void *data, video_data *frame)
{
    static_cast<DelayedVirtualCameraOutput *>(data)->ReceiveVideo(frame);
}

void OutputUpdate(void *data, obs_data_t *settings)
{
    static_cast<DelayedVirtualCameraOutput *>(data)->Update(settings);
}

void OutputDefaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, kSettingDelayMs,
                             virtual_camera_delay::kDefaultDelayMs);
}
} // namespace

const obs_output_info *GetDelayedVirtualCameraOutputInfo()
{
    static obs_output_info info{};
    static bool initialized = false;
    if (!initialized) {
        info.id = kDelayedVirtualCameraOutputId;
        info.flags = OBS_OUTPUT_VIDEO;
        info.get_name = OutputGetName;
        info.create = OutputCreate;
        info.destroy = OutputDestroy;
        info.start = OutputStart;
        info.stop = OutputStop;
        info.raw_video = OutputRawVideo;
        info.update = OutputUpdate;
        info.get_defaults = OutputDefaults;
        initialized = true;
    }
    return &info;
}
