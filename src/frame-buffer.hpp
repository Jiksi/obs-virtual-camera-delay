#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace virtual_camera_delay {

constexpr uint32_t kDefaultDelayMs = 1000;
constexpr uint32_t kMaximumDelayMs = 3000;
constexpr size_t kMaximumBufferBytes = 768ULL * 1024ULL * 1024ULL;

struct Nv12FrameView {
    const uint8_t *y = nullptr;
    const uint8_t *uv = nullptr;
    uint32_t yStride = 0;
    uint32_t uvStride = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t timestamp = 0;
};

struct BufferedNv12Frame {
    std::vector<uint8_t> payload;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t timestamp = 0;

    uint8_t *Y() { return payload.data(); }
    const uint8_t *Y() const { return payload.data(); }
    uint8_t *UV() { return payload.data() + static_cast<size_t>(width) * height; }
    const uint8_t *UV() const { return payload.data() + static_cast<size_t>(width) * height; }
    size_t Size() const { return payload.size(); }
};

struct BufferConfiguration {
    uint32_t requestedDelayMs = 0;
    uint32_t delayMs = 0;
    size_t frameBytes = 0;
    size_t frameLimit = 0;
    size_t expectedBytes = 0;
    bool clamped = false;
};

struct BufferStats {
    uint64_t enqueued = 0;
    uint64_t emitted = 0;
    uint64_t dropped = 0;
    uint64_t resets = 0;
};

class TimestampFrameBuffer {
public:
    BufferConfiguration Configure(uint32_t width, uint32_t height,
                                  uint32_t fpsNumerator,
                                  uint32_t fpsDenominator,
                                  uint32_t requestedDelayMs,
                                  size_t maximumBytes = kMaximumBufferBytes);

    std::optional<BufferedNv12Frame> Push(const Nv12FrameView &frame);
    void Reset();

    const BufferConfiguration &Configuration() const { return configuration_; }
    const BufferStats &Stats() const { return stats_; }
    size_t QueuedBytes() const { return queuedBytes_; }
    size_t QueuedFrames() const { return frames_.size(); }

private:
    static size_t FrameBytes(uint32_t width, uint32_t height);
    static void CopyPlane(uint8_t *destination, const uint8_t *source,
                          uint32_t sourceStride, uint32_t rowBytes,
                          uint32_t rows);

    std::deque<BufferedNv12Frame> frames_;
    BufferConfiguration configuration_;
    BufferStats stats_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t delayNs_ = 0;
    size_t maximumBytes_ = kMaximumBufferBytes;
    size_t queuedBytes_ = 0;
};

} // namespace virtual_camera_delay
