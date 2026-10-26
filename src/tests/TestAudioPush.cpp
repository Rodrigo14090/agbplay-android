#include "AudioFrameQueue.hpp"
#include "AudioBufferPolicy.hpp"
#include "AudioDataReady.hpp"
#include <chrono>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
namespace {
sample Frame(size_t index) { const float value = 0.125f + float(index % 8192) / 32768; return {value, -value}; }
struct Result { uint64_t missing, nativeMissing, deferred, played; };
Result Simulate(bool paddedClock, size_t blockFrames, int wakeDelayUs, int seconds) {
    // Two independent clocks: the Android mixer drains bursts of 20 ms; the
    // old writer catches up its 5 ms deadlines after a delayed/full-buffer wait.
    // New code follows native backpressure and defers unavailable PCM instead.
    constexpr size_t capacity = 1924, burst = 960, request = 240;
    AudioFrameQueue queue;
    AudioBufferPolicy policy(48000, 30);
    std::vector<sample> block(blockFrames);
    std::deque<sample> native;
    std::array<float, request * 2> pending{};
    size_t generated = 0, consumed = 0, offset = 0, remaining = 0;
    uint64_t deferred = 0, nativeMissing = 0, played = 0;
    int64_t mixAt = 0, writerAt = 0, nativeAt = 0, nextRead = 0;
    bool mixing = false, started = false, waitPcm = false;
    int mixes = 0;
    auto put = [&] {
        for (auto &value : block) value = Frame(generated++);
        CHECK(queue.Put(block));
    };
    while (queue.Queued() < policy.Target(2, request)) put();
    const int64_t end = int64_t(seconds) * 1000000;
    for (int64_t now = 0; now < end; now += 100) {
        if (started && now >= nativeAt) {
            for (size_t frame = 0; frame < burst; ++frame) {
                if (native.empty()) { ++nativeMissing; continue; }
                const auto value = native.front(); native.pop_front();
                if (value.left != 0) {
                    const auto reference = Frame(consumed++);
                    CHECK(value.left == reference.left && value.right == reference.right);
                } else CHECK(paddedClock); // no fabricated silence in push mode
                ++played;
            }
            nativeAt += 20000;
        }
        if (now >= mixAt) {
            if (mixing) {
                put(); mixing = false;
                if (waitPcm) { writerAt = now; waitPcm = false; }
            }
            if (queue.Queued() < policy.Target(2, request)) {
                // Average cost is below 4.17 ms/200 frames, with observed
                // background-sized spikes, distinct from producer wake delay.
                const int64_t cost = (++mixes % 7 == 0) ? 9500 : 1700;
                mixing = true; mixAt = now + cost;
            } else mixAt = now + policy.WaitForRefill(queue.Queued(), policy.Target(2, request)).count() / 1000 + 1000;
        }
        if (now < writerAt) continue;
        for (int iteration = 0; iteration < 30; ++iteration) {
            if (!remaining) {
                if (paddedClock && now < nextRead) { writerAt = nextRead + wakeDelayUs; break; }
                const auto wanted = started ? request : std::min(request, capacity - native.size());
                const auto count = paddedClock ? queue.Take(std::span<float>(pending).first(wanted * 2))
                                              : queue.TakeAvailable(std::span<float>(pending).first(wanted * 2));
                if (!paddedClock && !count) { ++deferred; waitPcm = true; writerAt = end; break; }
                offset = 0; remaining = paddedClock ? wanted : count;
                if (paddedClock) nextRead += int64_t(wanted) * 1000000 / 48000;
            }
            const auto accepted = std::min(remaining, capacity - native.size());
            for (size_t i = 0; i < accepted; ++i)
                native.push_back({pending[(offset + i) * 2], pending[(offset + i) * 2 + 1]});
            offset += accepted; remaining -= accepted;
            if (!started && native.size() == capacity) { started = true; nativeAt = now + 20000; }
            if (!accepted) { writerAt = now + 1000 + wakeDelayUs; break; }
        }
    }
    return {queue.TakeDiagnostics().underrunFrames, nativeMissing, deferred, played};
}
void Boundaries() {
    AudioFrameQueue queue;
    std::array<sample, 200> block;
    for (size_t i = 0; i < block.size(); ++i) block[i] = Frame(i);
    CHECK(queue.Put(block));
    std::array<float, 480> output; output.fill(42);
    CHECK(queue.TakeAvailable(output) == 200);
    for (size_t i = 0; i < 200; ++i) { CHECK(output[i*2] == block[i].left); CHECK(output[i*2+1] == block[i].right); }
    for (size_t i = 400; i < output.size(); ++i) CHECK(output[i] == 42);
    CHECK(queue.TakeAvailable(output) == 0);
    CHECK(queue.TakeDiagnostics().underrunFrames == 0);
    CHECK(queue.Put(block)); queue.DiscardPending(); CHECK(queue.Put(block));
    CHECK(queue.TakeAvailable(output) == 200 && output[0] == block[0].left);
}
void PublicationAndCancel() {
    AudioFrameQueue queue;
    AudioDataReady ready;
    std::atomic<bool> quit{false}, done{false};
    constexpr size_t total = 400000;
    std::thread producer([&] {
        std::array<sample, 200> block;
        for (size_t base = 0; base < total; base += block.size()) {
            for (size_t i = 0; i < block.size(); ++i) block[i] = Frame(base + i);
            while (!queue.Put(block)) std::this_thread::yield();
            ready.Signal();
            if (base % 1400 == 0) std::this_thread::yield();
        }
        done.store(true); ready.Signal();
    });
    size_t read = 0;
    std::array<float, 480> pcm;
    while (read < total) {
        ready.Arm();
        const auto count = queue.TakeAvailable(pcm);
        if (!count) { ready.Wait(quit, std::chrono::seconds(1)); continue; }
        ready.Disarm();
        for (size_t i = 0; i < count; ++i) {
            const auto expected = Frame(read++);
            CHECK(pcm[i*2] == expected.left && pcm[i*2+1] == expected.right);
        }
    }
    producer.join(); CHECK(done); CHECK(queue.Readable() == 0);
    ready.Arm();
    std::thread cancel([&] { quit.store(true); ready.Signal(); });
    const auto began = std::chrono::steady_clock::now();
    ready.Wait(quit, std::chrono::seconds(5));
    cancel.join();
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(3));
}
}
int main() {
    Boundaries(); PublicationAndCancel();
    const auto old = Simulate(true, 200, 2200, 30);
    std::cout << "Old timed/padded writer: missing=" << old.missing << " native missing=" << old.nativeMissing << '\n';
    CHECK(old.missing > 0 && old.nativeMissing == 0);
    for (size_t block : {200u, 240u, 400u}) {
        for (int delay : {2200, 10000, 22000}) {
            const auto fixed = Simulate(false, block, delay, 30);
            std::cout << "Push writer: block=" << block << " wake delay=" << delay << " us, missing=" << fixed.missing
                      << " native missing=" << fixed.nativeMissing << " deferred=" << fixed.deferred << " played=" << fixed.played << '\n';
            CHECK(fixed.missing == 0 && fixed.nativeMissing == 0 && fixed.played > 1000000);
        }
    }
    const auto overBudget = Simulate(false, 200, 60000, 3);
    CHECK(overBudget.missing == 0 && overBudget.nativeMissing > 0);
    std::cout << "Excessive native scheduling delay remains visible: native missing=" << overBudget.nativeMissing << '\n';

}
