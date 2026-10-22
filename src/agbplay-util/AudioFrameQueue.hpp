#pragma once
#include "Types.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <span>

// Single synthesis producer / single device consumer. Storage never moves.
// Monotonic counters use unsigned wrap arithmetic; capacity is a power of two.
class AudioFrameQueue
{
public:
    static constexpr size_t Capacity = 32768;
    static_assert(std::atomic<size_t>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);
    size_t Queued() const noexcept
    {
        const auto w = write.load(std::memory_order_relaxed);
        return w - read.load(std::memory_order_acquire);
    }
    bool Put(std::span<const sample> frames) noexcept
    {
        const auto w = write.load(std::memory_order_relaxed);
        if (frames.size() > Capacity - (w - read.load(std::memory_order_acquire)))
            return false;
        for (size_t i = 0; i < frames.size(); ++i)
            data[(w + i) & (Capacity - 1)] = frames[i];
        write.store(w + frames.size(), std::memory_order_release);
        return true;
    }
    void Take(std::span<float> interleaved) noexcept
    {
        const auto r = read.load(std::memory_order_relaxed);
        const auto count = std::min(interleaved.size() / 2, write.load(std::memory_order_acquire) - r);
        for (size_t i = 0; i < count; ++i) {
            const auto frame = data[(r + i) & (Capacity - 1)];
            interleaved[2 * i] = frame.left;
            interleaved[2 * i + 1] = frame.right;
        }
        std::fill(interleaved.begin() + count * 2, interleaved.end(), 0.0f);
        read.store(r + count, std::memory_order_release);
    }
private:
    std::array<sample, Capacity> data{};
    alignas(64) std::atomic<size_t> write{0};
    alignas(64) std::atomic<size_t> read{0};
};
