#include "frame-buffer.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace virtual_camera_delay {

size_t TimestampFrameBuffer::FrameBytes(uint32_t width, uint32_t height)
{
    const size_t y = static_cast<size_t>(width) * height;
    const size_t uv = static_cast<size_t>(width) * ((height + 1U) / 2U);
    if (y > std::numeric_limits<size_t>::max() - uv)
        return 0;
    return y + uv;
}

BufferConfiguration TimestampFrameBuffer::Configure(
    uint32_t width, uint32_t height, uint32_t fpsNumerator,
    uint32_t fpsDenominator, uint32_t requestedDelayMs, size_t maximumBytes)
{
    Reset();
    width_ = width;
    height_ = height;
    maximumBytes_ = maximumBytes;

    configuration_ = {};
    configuration_.requestedDelayMs =
        std::min(requestedDelayMs, kMaximumDelayMs);
    configuration_.frameBytes = FrameBytes(width, height);
    configuration_.frameLimit = configuration_.frameBytes
                                    ? maximumBytes / configuration_.frameBytes
                                    : 0;

    uint32_t safeDelay = configuration_.requestedDelayMs;
    if (!configuration_.frameBytes || !configuration_.frameLimit ||
        !fpsNumerator || !fpsDenominator) {
        safeDelay = 0;
    } else {
        // One extra frame covers timestamp rounding at the delay boundary.
        const uint64_t usableFrames = configuration_.frameLimit > 1
                                          ? configuration_.frameLimit - 1
                                          : 0;
        const uint64_t maxDelay =
            usableFrames * 1000ULL * fpsDenominator / fpsNumerator;
        safeDelay = static_cast<uint32_t>(
            std::min<uint64_t>(safeDelay, std::min<uint64_t>(maxDelay, kMaximumDelayMs)));
    }

    configuration_.delayMs = safeDelay;
    configuration_.clamped = safeDelay != requestedDelayMs;
    const uint64_t expectedFrames =
        fpsNumerator && fpsDenominator
            ? (static_cast<uint64_t>(safeDelay) * fpsNumerator +
               1000ULL * fpsDenominator - 1) /
                  (1000ULL * fpsDenominator) +
                  1
            : 0;
    if (!configuration_.frameBytes) {
        configuration_.expectedBytes = 0;
    } else if (expectedFrames >
               maximumBytes / configuration_.frameBytes) {
        configuration_.expectedBytes = maximumBytes;
    } else {
        configuration_.expectedBytes = static_cast<size_t>(
            expectedFrames * configuration_.frameBytes);
    }
    delayNs_ = static_cast<uint64_t>(safeDelay) * 1000000ULL;
    return configuration_;
}

void TimestampFrameBuffer::CopyPlane(uint8_t *destination,
                                     const uint8_t *source,
                                     uint32_t sourceStride,
                                     uint32_t rowBytes, uint32_t rows)
{
    for (uint32_t row = 0; row < rows; ++row) {
        std::memcpy(destination + static_cast<size_t>(row) * rowBytes,
                    source + static_cast<size_t>(row) * sourceStride,
                    rowBytes);
    }
}

std::optional<BufferedNv12Frame>
TimestampFrameBuffer::Push(const Nv12FrameView &frame)
{
    if (!frame.y || !frame.uv || frame.width != width_ ||
        frame.height != height_ || frame.yStride < width_ ||
        frame.uvStride < width_ || !configuration_.frameBytes)
        return std::nullopt;

    while (!frames_.empty() &&
           queuedBytes_ + configuration_.frameBytes > maximumBytes_) {
        queuedBytes_ -= frames_.front().Size();
        frames_.pop_front();
        ++stats_.dropped;
    }
    if (configuration_.frameBytes > maximumBytes_) {
        ++stats_.dropped;
        return std::nullopt;
    }

    BufferedNv12Frame copy;
    copy.width = width_;
    copy.height = height_;
    copy.timestamp = frame.timestamp;
    copy.payload.resize(configuration_.frameBytes);
    CopyPlane(copy.Y(), frame.y, frame.yStride, width_, height_);
    CopyPlane(copy.UV(), frame.uv, frame.uvStride, width_, (height_ + 1U) / 2U);
    queuedBytes_ += copy.Size();
    frames_.push_back(std::move(copy));
    ++stats_.enqueued;

    const uint64_t threshold = frame.timestamp >= delayNs_
                                   ? frame.timestamp - delayNs_
                                   : 0;
    if (frames_.front().timestamp > threshold)
        return std::nullopt;

    BufferedNv12Frame ready = std::move(frames_.front());
    frames_.pop_front();
    queuedBytes_ -= ready.Size();
    ++stats_.emitted;
    return ready;
}

void TimestampFrameBuffer::Reset()
{
    frames_.clear();
    queuedBytes_ = 0;
    ++stats_.resets;
}

} // namespace virtual_camera_delay
