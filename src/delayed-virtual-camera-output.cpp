#include "delayed-virtual-camera-output.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <new>
#include <vector>

namespace {
constexpr const char *kSettingDelayMs = "delay_ms";
constexpr int64_t kDefaultDelayMs = 3000;
constexpr int64_t kMaxDelayMs = 10000;

struct BufferedVideoFrame {
    std::vector<uint8_t> y;
    std::vector<uint8_t> uv;
    uint64_t timestamp = 0;
};

class DelayedVirtualCameraOutput {
public:
    explicit DelayedVirtualCameraOutput(obs_output_t *output) : output_(output) {}

    bool Start()
    {
        if (!obs_output_can_begin_data_capture(output_, 0))
            return false;

        const uint32_t width = obs_output_get_width(output_);
        const uint32_t height = obs_output_get_height(output_);
        if (!width || !height)
            return false;

        video_scale_info conversion{};
        conversion.format = VIDEO_FORMAT_NV12;
        conversion.width = width;
        conversion.height = height;
        obs_output_set_video_conversion(output_, &conversion);

        width_ = width;
        height_ = height;
        receivedFrames_ = 0;
        delayedFrames_ = 0;
        frames_.clear();
        active_.store(true, std::memory_order_release);

        if (!obs_output_begin_data_capture(output_, 0)) {
            active_.store(false, std::memory_order_release);
            return false;
        }

        blog(LOG_INFO,
             "[obs-virtual-camera-delay] custom output started at %ux%u "
             "with %lld ms delay",
             width_, height_, static_cast<long long>(delayNs_ / 1000000ULL));
        return true;
    }

    void Stop()
    {
        if (!active_.exchange(false, std::memory_order_acq_rel))
            return;

        obs_output_end_data_capture(output_);
        frames_.clear();
        blog(LOG_INFO,
             "[obs-virtual-camera-delay] custom output stopped after receiving "
             "%llu frames (%llu reached the delayed output boundary)",
             static_cast<unsigned long long>(receivedFrames_),
             static_cast<unsigned long long>(delayedFrames_));
    }

    void Update(obs_data_t *settings)
    {
        const int64_t delayMs = std::clamp(
            obs_data_get_int(settings, kSettingDelayMs), int64_t{0},
            kMaxDelayMs);
        obs_data_set_int(settings, kSettingDelayMs, delayMs);
        delayNs_ = static_cast<uint64_t>(delayMs) * 1000000ULL;
    }

    void ReceiveVideo(const video_data *frame)
    {
        if (!active_.load(std::memory_order_acquire) || !frame ||
            !frame->data[0] || !frame->data[1])
            return;

        BufferedVideoFrame copy;
        copy.timestamp = frame->timestamp;
        CopyPlane(copy.y, frame->data[0], frame->linesize[0], height_);
        CopyPlane(copy.uv, frame->data[1], frame->linesize[1],
                  (height_ + 1) / 2);
        frames_.push_back(std::move(copy));
        ++receivedFrames_;

        while (!frames_.empty() &&
               frames_.front().timestamp + delayNs_ <= frame->timestamp) {
            frames_.pop_front();
            ++delayedFrames_;

            if (delayedFrames_ == 1) {
                blog(LOG_INFO,
                     "[obs-virtual-camera-delay] first delayed video frame "
                     "reached the custom output boundary");
            }
        }
    }

private:
    static void CopyPlane(std::vector<uint8_t> &destination,
                          const uint8_t *source, uint32_t linesize,
                          uint32_t rows)
    {
        const size_t bytes = static_cast<size_t>(linesize) * rows;
        destination.assign(source, source + bytes);
    }

    obs_output_t *output_ = nullptr;
    std::deque<BufferedVideoFrame> frames_;
    std::atomic<bool> active_{false};
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t delayNs_ = static_cast<uint64_t>(kDefaultDelayMs) * 1000000ULL;
    uint64_t receivedFrames_ = 0;
    uint64_t delayedFrames_ = 0;
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
    obs_data_set_default_int(settings, kSettingDelayMs, kDefaultDelayMs);
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
