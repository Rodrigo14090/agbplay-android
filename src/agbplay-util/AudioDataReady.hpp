#pragma once
#include <atomic>
#include <chrono>
#include <semaphore>

// One push-output consumer. Arm before testing the FIFO, so publication between
// that test and Wait cannot be lost. Never used by a device's realtime callback.
class AudioDataReady {
    std::atomic<bool> waiting{false};
    std::counting_semaphore<> event{0};
public:
    void Arm() noexcept {
        while (event.try_acquire()) {} // retire a notification racing a previous timeout/cancel
        waiting.exchange(true, std::memory_order_acq_rel);
    }
    void Disarm() noexcept {
        // All transitions are acquire/release RMWs. If a publication races
        // disarming, its visibility must carry through to the next Arm/read;
        // a plain store here would break that chain on weakly ordered CPUs.
        waiting.exchange(false, std::memory_order_acq_rel);
    }
    void Signal() noexcept {
        // No syscall or lock on the synthesis worker unless a push consumer
        // actually armed a wait. Qt/PortAudio never arm this event.
        if (waiting.exchange(false, std::memory_order_acq_rel))
            event.release();
    }
    bool Wait(const std::atomic<bool> &quit, std::chrono::milliseconds timeout) noexcept {
        const bool signalled = !quit.load(std::memory_order_acquire) && event.try_acquire_for(timeout);
        Disarm();
        return signalled;
    }
};
