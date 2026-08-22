#include "frame-buffer.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace virtual_camera_delay;

namespace {
struct TestFrame {
    std::vector<uint8_t> y;
    std::vector<uint8_t> uv;

    explicit TestFrame(uint8_t value) : y(16, value), uv(8, value) {}

    Nv12FrameView View(uint64_t timestamp) const
    {
        return {y.data(), uv.data(), 4, 4, 4, 4, timestamp};
    }
};

void TestZeroDelay()
{
    TimestampFrameBuffer buffer;
    buffer.Configure(4, 4, 30, 1, 0);
    TestFrame frame(7);
    auto ready = buffer.Push(frame.View(100));
    assert(ready && ready->timestamp == 100 && ready->Y()[0] == 7);
}

void TestOneSecondOrdering()
{
    TimestampFrameBuffer buffer;
    buffer.Configure(4, 4, 30, 1, 1000);
    TestFrame first(1), second(2), third(3);
    assert(!buffer.Push(first.View(0)));
    assert(!buffer.Push(second.View(500000000)));
    auto ready = buffer.Push(third.View(1000000000));
    assert(ready && ready->timestamp == 0 && ready->Y()[0] == 1);
    auto next = buffer.Push(third.View(1500000000));
    assert(next && next->timestamp == 500000000 && next->Y()[0] == 2);
}

void TestResetAndRepeatedLifecycle()
{
    TimestampFrameBuffer buffer;
    TestFrame frame(9);
    for (int cycle = 0; cycle < 20; ++cycle) {
        buffer.Configure(4, 4, 60, 1, 1000);
        buffer.Push(frame.View(static_cast<uint64_t>(cycle) * 1000000000ULL));
        assert(buffer.QueuedFrames() == 1);
        buffer.Reset();
        assert(buffer.QueuedFrames() == 0 && buffer.QueuedBytes() == 0);
    }
}

void TestMemoryClampAndOverflow()
{
    TimestampFrameBuffer buffer;
    TestFrame frame(4);
    const auto config = buffer.Configure(4, 4, 60, 1, 3000, 48);
    assert(config.clamped);
    assert(config.frameLimit == 2);
    buffer.Push(frame.View(100));
    buffer.Push(frame.View(101));
    buffer.Push(frame.View(102));
    assert(buffer.QueuedBytes() <= 48);
    assert(buffer.Stats().dropped > 0);
}

void TestGeometryMismatchIsRejected()
{
    TimestampFrameBuffer buffer;
    buffer.Configure(4, 4, 30, 1, 1000);
    TestFrame frame(1);
    auto view = frame.View(0);
    view.width = 2;
    assert(!buffer.Push(view));
    assert(buffer.QueuedFrames() == 0);
}

void TestInvalidConfigurationIsSafe()
{
    TimestampFrameBuffer buffer;
    const auto config = buffer.Configure(0, 1080, 60, 1, 3000);
    assert(config.frameBytes == 0);
    assert(config.expectedBytes == 0);
    assert(config.delayMs == 0);
}
} // namespace

int main()
{
    TestZeroDelay();
    TestOneSecondOrdering();
    TestResetAndRepeatedLifecycle();
    TestMemoryClampAndOverflow();
    TestGeometryMismatchIsRejected();
    TestInvalidConfigurationIsSafe();
    std::cout << "frame-buffer tests passed\n";
    return 0;
}
