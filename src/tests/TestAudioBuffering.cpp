#include "AudioBufferPolicy.hpp"
#include "AudioPcmStatistics.hpp"
#include "SyntheticRom.hpp"
#include "MP2KContext.hpp"
#include "Rom.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
namespace {
sample Frame(uint64_t index)
{
    const float value = 0.125f + static_cast<float>(index % 32768) / 131072.0f;
    return {value, -value};
}
struct SimulationResult { uint64_t underruns, missing, callbacks; size_t minQueued, maxStored; };
SimulationResult Simulate(size_t blockSize, uint32_t reserveMs, uint64_t wakeDelayNs, int seconds,
                          uint64_t fixedMixCostNs = 0)
{
    // Discrete events exercise the production policy without host scheduler
    // noise. AAudio asks 96 frames exactly every 2 ms. Wake lateness is distinct
    // from synthesis cost, including occasional 5 ms synthesis spikes.
    constexpr uint64_t periodNs = 2000000;
    AudioBufferPolicy policy(48000, reserveMs);
    AudioFrameQueue queue;
    const size_t target = policy.Target(2, 96);
    std::vector<sample> block(blockSize);
    uint64_t generated = 0, consumed = 0, mixes = 0;
    auto put = [&] {
        CHECK(queue.Writable() >= blockSize);
        for (auto &frame : block) frame = Frame(generated++);
        CHECK(queue.Put(block));
        CHECK(queue.Queued() <= AudioFrameQueue::Capacity);
    };
    // Prime both policies to compare steady-state failures, without counting
    // the old implementation's separate empty-start defect.
    while (queue.Queued() < target) put();
    uint64_t callbackAt = periodNs;
    uint64_t producerAt = static_cast<uint64_t>(policy.WaitForRefill(queue.Queued(), target).count()) + wakeDelayNs;
    bool synthesizing = false;
    uint64_t callbacks = 0;
    std::array<float, 192> pcm;
    const uint64_t end = uint64_t(seconds) * 1000000000;
    while (std::min(callbackAt, producerAt) <= end) {
        if (callbackAt <= producerAt) {
            const auto count = queue.Take(pcm);
            for (size_t i = 0; i < count; ++i) {
                const auto expected = Frame(consumed++);
                CHECK(pcm[2 * i] == expected.left && pcm[2 * i + 1] == expected.right);
            }
            for (size_t i = count * 2; i < pcm.size(); ++i) CHECK(pcm[i] == 0.0f);
            callbackAt += periodNs;
            ++callbacks;
        } else if (synthesizing) {
            put();
            synthesizing = false; // immediately reevaluate, fill the entire reserve in a batch
        } else if ((reserveMs ? queue.Queued() < target : queue.Queued() <= target) &&
                   queue.Writable() >= blockSize) {
            const uint64_t mixCost = fixedMixCostNs ? fixedMixCostNs :
                ((++mixes % 257 == 0) ? 5000000 : blockSize * 1000000000ull / (48000 * 16));
            producerAt += mixCost;
            synthesizing = true;
        } else {
            producerAt += static_cast<uint64_t>(policy.WaitForRefill(queue.Queued(), target).count()) + wakeDelayNs;
        }
    }
    const auto stats = queue.TakeDiagnostics();
    CHECK(generated >= consumed);
    CHECK(generated - consumed == queue.Queued());
    CHECK(consumed + stats.underrunFrames == callbacks * 96);
    return {stats.underrunCallbacks, stats.underrunFrames, callbacks, stats.minQueued, stats.maxStored};
}
void BoundariesAndMeter()
{
    AudioFrameQueue queue;
    std::vector<sample> full(AudioFrameQueue::Capacity);
    for (size_t i = 0; i < full.size(); ++i) full[i] = Frame(i);
    CHECK(queue.Put(full));
    CHECK(queue.Writable() == 0);
    CHECK(!queue.Put(std::span<const sample>(full).first(1)));
    std::array<float, 192> pcm;
    uint64_t expected = 0;
    while (queue.Readable()) {
        const size_t count = queue.Read(pcm);
        for (size_t i = 0; i < count; ++i) {
            const auto frame = Frame(expected++);
            CHECK(pcm[2 * i] == frame.left && pcm[2 * i + 1] == frame.right);
        }
    }
    CHECK(expected == AudioFrameQueue::Capacity);
    CHECK(queue.Read(pcm) == 0);
    CHECK(queue.Take(pcm) == 0);
    const auto stats = queue.TakeDiagnostics();
    CHECK(stats.underrunCallbacks == 1 && stats.underrunFrames == 96);
    CHECK(stats.maxStored == AudioFrameQueue::Capacity);
    const std::array<float, 8> special = {0.0f, -0.0f, 0.125f, -0.5f,
        std::numeric_limits<float>::denorm_min(), 1.0f,
        std::bit_cast<float>(uint32_t{0x7fc01234}), std::numeric_limits<float>::infinity()};
    CHECK(!queue.PutInterleaved(std::span<const float>(special).first(3)));
    CHECK(queue.PutInterleaved(special));
    CHECK(queue.Read(pcm) == special.size() / 2);
    for (size_t i = 0; i < special.size(); ++i)
        CHECK(std::bit_cast<uint32_t>(special[i]) == std::bit_cast<uint32_t>(pcm[i]));
    AudioPcmStatistics meter;
    meter.Add(special);
    CHECK(meter.nonFiniteSamples == 2 && meter.clippedSamples == 1 && meter.finiteSamples == 6);
    meter = {};
    std::vector<float> sine(48000);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = 0.375f * static_cast<float>(std::sin(2.0 * 3.141592653589793 * i / 48.0));
    meter.Add(sine);
    CHECK(std::abs(meter.Rms() - 0.375 / std::sqrt(2.0)) < 1e-7);
    CHECK(meter.peak == 0.375f && meter.clippedSamples == 0 && meter.nonFiniteSamples == 0);
    meter.Add(std::array<float, 2>{std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()});
    CHECK(std::isfinite(meter.Rms())); // no float-to-integer overflow in telemetry
}

void TransportDiscard()
{
    AudioFrameQueue queue;
    std::vector<sample> old(1600, {1, -1}), fresh(1600, {2, -2});
    CHECK(queue.Put(old));
    queue.DiscardPending();
    CHECK(queue.Queued() == 0);
    CHECK(queue.Writable() == AudioFrameQueue::Capacity - old.size());
    CHECK(queue.Put(fresh)); // prefill before restarting a suspended device
    CHECK(queue.Queued() == fresh.size());
    std::array<float, 192> pcm;
    CHECK(queue.Take(pcm) == 96);
    for (size_t i = 0; i < 96; ++i) CHECK(pcm[2 * i] == 2 && pcm[2 * i + 1] == -2);
    CHECK(queue.TakeDiagnostics().underrunCallbacks == 0);
    queue.DiscardPending();
    CHECK(queue.Put(old));
    queue.DiscardPending(); // several transport changes before the device restarts
    CHECK(queue.Put(fresh));
    CHECK(queue.Take(pcm) == 96);
    CHECK(pcm[0] == 2 && pcm[1] == -2);

    AudioFrameQueue concurrent;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        std::array<sample, 64> block;
        for (size_t base = 0; base < 200000; base += block.size()) {
            for (size_t i = 0; i < block.size(); ++i) {
                const auto value = static_cast<float>(base + i + 1);
                block[i] = {value, -value};
            }
            if (base % (64 * 17) == 0) concurrent.DiscardPending();
            while (!concurrent.Put(block)) std::this_thread::yield();
            CHECK(concurrent.Queued() <= AudioFrameQueue::Capacity);
        }
        done.store(true, std::memory_order_release);
    });
    float previous = 0;
    do {
        const auto count = concurrent.Read(pcm);
        for (size_t i = 0; i < count; ++i) {
            CHECK(pcm[2 * i] > previous); // gaps allowed, never repeated/reordered frames
            CHECK(pcm[2 * i + 1] == -pcm[2 * i]);
            previous = pcm[2 * i];
        }
    } while (!done.load(std::memory_order_acquire) || concurrent.Readable());
    producer.join();
    CHECK(previous > 0);
}
void PcmTransport()
{
    auto bytes = Fixture::RomBytes();
    auto rom = Rom::LoadFromBufferRef(bytes);
    for (auto resampler : {ResamplerType::SINC, ResamplerType::BLEP, ResamplerType::BLAMP}) {
        auto profile = Fixture::PlaybackProfile();
        profile.agbplaySoundMode.resamplerTypeNormal = resampler;
        MP2KContext ctx(48000, 1, rom, profile.mp2kSoundModePlayback, profile.agbplaySoundMode,
                        profile.songTableInfoPlayback, profile.playerTablePlayback);
        ctx.m4aSongNumStart(0);
        std::vector<float> reference, actual;
        AudioFrameQueue queue;
        std::array<float, 192> output;
        for (int n = 0; n < 600; ++n) {
            ctx.m4aSoundMain();
            for (const auto &s : ctx.masterAudioBuffer) { reference.push_back(s.left); reference.push_back(s.right); }
            CHECK(queue.Put(ctx.masterAudioBuffer));
            while (queue.Readable() >= 96) {
                CHECK(queue.Take(output) == 96);
                actual.insert(actual.end(), output.begin(), output.end());
            }
        }
        const size_t remaining = queue.Read(output);
        actual.insert(actual.end(), output.begin(), output.begin() + static_cast<ptrdiff_t>(remaining * 2));
        CHECK(reference.size() == actual.size());
        AudioPcmStatistics expectedMeter, actualMeter;
        expectedMeter.Add(reference); actualMeter.Add(actual);
        CHECK(expectedMeter.Rms() == actualMeter.Rms() && expectedMeter.peak == actualMeter.peak);
        for (size_t i = 0; i < actual.size(); ++i)
            CHECK(std::bit_cast<uint32_t>(actual[i]) == std::bit_cast<uint32_t>(reference[i]));
        CHECK(queue.TakeDiagnostics().underrunCallbacks == 0);
        std::cout << "PCM transport resampler=" << static_cast<int>(resampler) << " RMS=" << actualMeter.Rms()
                  << " peak=" << actualMeter.peak << " max sample difference=0 (bit exact)\n";
    }
}
void ConcurrentFifo()
{
    AudioFrameQueue queue;
    constexpr uint64_t total = 500000;
    std::thread producer([&] {
        std::array<sample, 1024> block;
        uint64_t generated = 0;
        const std::array<size_t, 5> sizes{1, 64, 200, 511, 1024};
        size_t iteration = 0;
        while (generated < total) {
            const size_t count = std::min<size_t>(sizes[iteration++ % sizes.size()], total - generated);
            for (size_t i = 0; i < count; ++i) block[i] = Frame(generated + i);
            while (!queue.Put(std::span<const sample>(block).first(count))) std::this_thread::yield();
            generated += count;
        }
    });
    std::array<float, 192> output;
    uint64_t consumed = 0;
    bool valid = true;
    while (consumed < total) {
        const size_t count = queue.Take(output);
        for (size_t i = 0; i < count; ++i) {
            const auto expected = Frame(consumed++);
            valid &= output[2 * i] == expected.left && output[2 * i + 1] == expected.right;
        }
        for (size_t i = count * 2; i < output.size(); ++i) valid &= output[i] == 0.0f;
        if (!count) std::this_thread::yield();
    }
    producer.join();
    CHECK(valid && consumed == total && queue.Readable() == 0);
}
}
int main()
{
    TransportDiscard();
    AudioBufferPolicy policy(48000, 30);
    CHECK(policy.Target(2, 96) == 1440 && policy.LowWatermark(1440) == 1200);
    CHECK(policy.Target(16, AudioFrameQueue::Capacity) == AudioFrameQueue::Capacity / 2);
    CHECK(policy.WaitForRefill(1440, 1440) == std::chrono::milliseconds(5));
    for (uint32_t rate : {8000u, 22050u, 44100u, 48000u, 96000u, 192000u}) {
        AudioBufferPolicy atRate(rate, 30);
        const size_t target = atRate.Target(2, 96);
        CHECK(target >= (static_cast<size_t>(rate) * 30 + 999) / 1000);
        CHECK(target <= AudioFrameQueue::Capacity / 2);
        CHECK(atRate.WaitForRefill(target, target).count() > 0);
    }
    BoundariesAndMeter();
    ConcurrentFifo();
    const auto oldPolicy = Simulate(200, 0, 10000000, 60);
    CHECK(oldPolicy.underruns > 0 && oldPolicy.missing > 0);
    std::cout << "Legacy 2-period target with 10ms wake delay: underruns=" << oldPolicy.underruns
              << " missing=" << oldPolicy.missing << '\n';
    for (size_t block : {64, 100, 200, 256, 480, 1024}) {
        const auto result = Simulate(block, 30, 10000000, 60);
        CHECK(result.underruns == 0 && result.missing == 0);
        std::cout << "30ms reserve / 10ms wake delay / block=" << block << ": callbacks=" << result.callbacks
                  << " underruns=0 min=" << result.minQueued << " max stored=" << result.maxStored << '\n';
    }
    const auto longRun = Simulate(200, 30, 15000000, 600);
    CHECK(longRun.underruns == 0);
    std::cout << "600s simulated, 15ms wake lateness: " << longRun.callbacks << " callbacks, underruns=0\n";
    for (uint32_t reserve : {20u, 40u}) {
        const auto result = Simulate(200, reserve, 10000000, 60);
        std::cout << reserve << "ms reserve / 10ms wake delay: underruns=" << result.underruns
                  << " missing=" << result.missing << " min=" << result.minQueued << '\n';
        if (reserve == 40) CHECK(result.underruns == 0);
    }
    const auto slowMixer = Simulate(200, 30, 0, 60, 5000000);
    CHECK(slowMixer.underruns > 0); // 40k produced frames/s cannot sustain a 48k device
    std::cout << "Sustained synthesis slower than realtime: detected underruns=" << slowMixer.underruns << '\n';
    const auto overload = Simulate(200, 30, 45000000, 60);
    CHECK(overload.underruns > 0 && overload.missing > 0); // finite reserve cannot cover an unbounded stall
    std::cout << "45ms wake delay: detected/recovered underruns=" << overload.underruns << " without PCM reordering\n";
    PcmTransport();
}
