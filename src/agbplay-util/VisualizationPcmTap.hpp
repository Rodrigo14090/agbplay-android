#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>

// One audio writer, one visual reader. Triple-buffer ownership, not a playback
// FIFO: neither side can wait, and the writer can replace an unread snapshot.
// The reader's slot is never written until it returns ownership by exchange.
class VisualizationPcmTap
{
public:
    static constexpr size_t Frames = 512;
    struct Result { size_t frames = 0; uint64_t skippedFrames = 0; };
    static_assert(std::atomic<unsigned>::is_always_lock_free);

    void Enable(bool value) noexcept {
        const unsigned epoch = control.load(std::memory_order_relaxed) + 2U;
        const unsigned next = value ? (epoch | 1U) : (epoch & ~1U);
        control.store(next, std::memory_order_release);
    }

    void Publish(std::span<const float> stereo) noexcept {
        const auto epoch = control.load(std::memory_order_acquire);
        if (!(epoch & 1U)) { pending = 0; writerEpoch = 0; return; }
        if (writerEpoch != epoch) { pending = 0; writerEpoch = epoch; }
        while (stereo.size() >= 2) {
            auto &slot = slots[back];
            const size_t count = std::min(Frames - pending, stereo.size() / 2);
            std::copy_n(stereo.data(), count * 2, slot.pcm.data() + pending * 2);
            pending += count;
            stereo = stereo.subspan(count * 2);
            if (pending == Frames) {
                slot.epoch = epoch;
                slot.sequence = ++written;
                back = middle.exchange(back | Dirty, std::memory_order_acq_rel) & Mask;
                pending = 0;
            }
        }
    }

    Result Read(std::span<float> stereo) noexcept {
        if (stereo.size() < Frames * 2 || !(middle.load(std::memory_order_acquire) & Dirty)) return {};
        front = middle.exchange(front, std::memory_order_acq_rel) & Mask;
        const auto &slot = slots[front];
        const auto epoch = control.load(std::memory_order_acquire);
        if (!(epoch & 1U) || slot.epoch != epoch) return {};
        const auto skipped = readEpoch == epoch && slot.sequence > lastRead ? slot.sequence - lastRead - 1 : 0;
        std::copy(slot.pcm.begin(), slot.pcm.end(), stereo.begin());
        lastRead = slot.sequence;
        readEpoch = epoch;
        return {Frames, skipped * Frames};
    }

private:
    static constexpr unsigned Dirty = 4, Mask = 3;
    struct Slot { std::array<float, Frames * 2> pcm{}; unsigned epoch = 0; uint64_t sequence = 0; };
    std::array<Slot, 3> slots{};
    alignas(64) std::atomic<unsigned> middle{1};
    alignas(64) std::atomic<unsigned> control{0};
    unsigned back = 2, writerEpoch = 0;
    size_t pending = 0;
    uint64_t written = 0;
    unsigned front = 0, readEpoch = 0;
    uint64_t lastRead = 0;
};
