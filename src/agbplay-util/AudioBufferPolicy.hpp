#pragma once
#include "AudioFrameQueue.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>

// Producer-side scheduling only. The consumer never wakes or waits on a mutex.
class AudioBufferPolicy
{
public:
#ifdef __ANDROID__
    static constexpr uint32_t DefaultReserveMs = 30;
#else
    static constexpr uint32_t DefaultReserveMs = 0; // retain desktop buffer preference
#endif
    AudioBufferPolicy(uint32_t rate, uint32_t reserveMs = DefaultReserveMs)
        : rate(rate), reserveMs(std::min(reserveMs, 250u)) {}

    size_t Target(size_t periods, size_t callbackFrames) const noexcept
    {
        const size_t periodTarget = std::clamp<size_t>(periods, 1, 16) *
                                    std::clamp<size_t>(callbackFrames, 1, AudioFrameQueue::Capacity);
        const size_t timeTarget = (static_cast<size_t>(rate) * reserveMs + 999) / 1000;
        return std::clamp(std::max(timeTarget, periodTarget), size_t{1}, AudioFrameQueue::Capacity / 2);
    }
    size_t LowWatermark(size_t target) const noexcept
    {
        if (!reserveMs) return target;
        // Refill roughly 5 ms at a time, keeping >=25 ms at the default 30 ms target.
        return target - std::min(target / 3, static_cast<size_t>(rate) * 5 / 1000);
    }
    std::chrono::nanoseconds WaitForRefill(size_t queued, size_t target) const noexcept
    {
        if (!reserveMs) return std::chrono::milliseconds(1);
        const auto low = LowWatermark(target);
        const size_t frames = queued > low ? queued - low : 1;
        return std::chrono::nanoseconds(static_cast<int64_t>(frames * uint64_t{1000000000} / rate));
    }
private:
    const uint32_t rate;
    const uint32_t reserveMs;
};
