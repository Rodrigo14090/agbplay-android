#pragma once
#include "Types.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <span>
#include <cstdint>

// Single synthesis producer / single device consumer. Storage never moves.
// Monotonic counters use unsigned wrap arithmetic; capacity is a power of two.
class AudioFrameQueue
{
public:
    static constexpr size_t Capacity = 32768;
    static_assert(std::atomic<size_t>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    // Queued/Writable are producer-side queries, Readable is consumer-side.
    // A third observer must use diagnostics instead of sampling both cursors.
    size_t Queued() const noexcept
    {
        const auto w = write.load(std::memory_order_relaxed);
        return w - read.load(std::memory_order_acquire);
    }
    size_t Writable() const noexcept { return Capacity - Queued(); }
    size_t Readable() const noexcept
    {
        return write.load(std::memory_order_acquire) - read.load(std::memory_order_relaxed);
    }
    bool Put(std::span<const sample> frames) noexcept
    {
        const auto w = write.load(std::memory_order_relaxed);
        if (frames.size() > Capacity - (w - read.load(std::memory_order_acquire)))
            return false;
        for (size_t i = 0; i < frames.size(); ++i)
            data[(w + i) & (Capacity - 1)] = frames[i];
        UpdateMaximum(maxStoredObserved, w + frames.size() - read.load(std::memory_order_acquire));
        write.store(w + frames.size(), std::memory_order_release);
        return true;
    }
    // Used for the optional PCM meter tap; never reinterprets float storage as sample.
    bool PutInterleaved(std::span<const float> interleaved) noexcept
    {
        const size_t count = interleaved.size() / 2;
        const auto w = write.load(std::memory_order_relaxed);
        if (interleaved.size() % 2 || count > Capacity - (w - read.load(std::memory_order_acquire)))
            return false;
        for (size_t i = 0; i < count; ++i)
            data[(w + i) & (Capacity - 1)] = {interleaved[2 * i], interleaved[2 * i + 1]};
        write.store(w + count, std::memory_order_release);
        return true;
    }
    // Read only available PCM, without padding or recording audio-output underruns.
    size_t Read(std::span<float> interleaved) noexcept
    {
        const auto r = read.load(std::memory_order_relaxed);
        const auto count = std::min(interleaved.size() / 2, write.load(std::memory_order_acquire) - r);
        for (size_t i = 0; i < count; ++i) {
            const auto frame = data[(r + i) & (Capacity - 1)];
            interleaved[2 * i] = frame.left;
            interleaved[2 * i + 1] = frame.right;
        }
        read.store(r + count, std::memory_order_release);
        return count;
    }
    size_t Take(std::span<float> interleaved) noexcept
    {
        const auto r = read.load(std::memory_order_relaxed);
        const auto requested = interleaved.size() / 2;
        const auto queued = write.load(std::memory_order_acquire) - r;
        const auto count = std::min(requested, queued);
        UpdateMaximum(maxQueuedObserved, queued);
        UpdateMinimum(minQueuedObserved, queued);
        if (count < requested) {
            underrunCallbacks.fetch_add(1, std::memory_order_relaxed);
            underrunFrames.fetch_add(requested - count, std::memory_order_relaxed);
        }
        // Write may advance between the observation and Read. Limit this read
        // to the observed count so counts and silence describe the same block.
        Read(interleaved.first(count * 2));
        auto tail = interleaved.subspan(count * 2);
        std::fill(tail.begin(), tail.end(), 0.0f);
        return count;
    }
    struct Diagnostics {
        uint64_t underrunCallbacks;
        uint64_t underrunFrames;
        size_t minQueued;
        size_t maxQueued;
        size_t maxStored;
    };
    Diagnostics TakeDiagnostics() noexcept
    {
        return {
            underrunCallbacks.exchange(0, std::memory_order_relaxed),
            underrunFrames.exchange(0, std::memory_order_relaxed),
            minQueuedObserved.exchange(Capacity, std::memory_order_relaxed),
            maxQueuedObserved.exchange(0, std::memory_order_relaxed),
            maxStoredObserved.exchange(0, std::memory_order_relaxed)
        };
    }
private:
    static void UpdateMaximum(std::atomic<size_t> &value, size_t candidate) noexcept
    {
        auto current = value.load(std::memory_order_relaxed);
        while (candidate > current &&
               !value.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {}
    }
    static void UpdateMinimum(std::atomic<size_t> &value, size_t candidate) noexcept
    {
        auto current = value.load(std::memory_order_relaxed);
        while (candidate < current &&
               !value.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {}
    }
    std::array<sample, Capacity> data{};
    alignas(64) std::atomic<size_t> write{0};
    alignas(64) std::atomic<size_t> read{0};
    std::atomic<uint64_t> underrunCallbacks{0};
    std::atomic<uint64_t> underrunFrames{0};
    std::atomic<size_t> minQueuedObserved{Capacity};
    std::atomic<size_t> maxQueuedObserved{0};
    std::atomic<size_t> maxStoredObserved{0};
};
