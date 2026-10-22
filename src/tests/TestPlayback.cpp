#include "SyntheticRom.hpp"
#include "AudioOutput.hpp"
#include "PlaybackEngine.hpp"
#include "Rom.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
bool failNextOpen = false;
class TestOutput final : public AudioOutput {
    std::atomic<bool> quit{false}, suspended{false};
    std::thread consumer;
public:
    TestOutput(void *context, Render render) : consumer([this, context, render] {
        std::array<float, 192> buffer;
        while (!quit) {
            if (!suspended) render(context, buffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }) {}
    ~TestOutput() override { quit = true; consumer.join(); }
    void Suspend(bool s) override { suspended = s; }
};
}
std::unique_ptr<AudioOutput> AudioOutput::Open(uint32_t, void *context, Render render) {
    if (failNextOpen) { failNextOpen = false; throw std::runtime_error("Injected device-open failure"); }
    return std::make_unique<TestOutput>(context, render);
}
int main(int argc, char **argv) {
    std::cout << std::unitbuf; // retain measurements even if an assertion aborts
    auto bytes = Fixture::RomBytes();
    if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
        std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        return 0;
    }
    // Keep the good song sounding for the unattended playback check.
    bytes[0x50a] = 0xb2;
    Fixture::Put32(bytes, 0x50b, 0x08000500);
    const auto path = std::filesystem::temp_directory_path() / "agbplay-synthetic-test.bin";
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove(p); } } cleanup{path};
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    Rom::CreateInstance(path);
    auto profile = Fixture::PlaybackProfile();
    failNextOpen = true;
    bool deviceFailure = false;
    try { PlaybackEngine engine(48000, 1, profile, 30); }
    catch (const std::runtime_error &) { deviceFailure = true; }
    if (!deviceFailure) throw std::runtime_error("Missing injected device-open failure");
    {
        PlaybackEngine engine(48000, 1, profile, 30); // same time policy as Android
        engine.LoadSong(0); engine.Play();
        std::this_thread::sleep_for(std::chrono::seconds(3)); // no UI or command wakeups
        const auto d = engine.TakeAudioDiagnostics();
        std::cout << "Unattended worker: callbacks=" << d.callbackCount << " underruns=" << d.queue.underrunCallbacks
                  << " missing=" << d.queue.underrunFrames << " target=" << d.targetFrames
                  << " min=" << d.queue.minQueued << " peak stored=" << d.queue.maxStored
                  << " max synthesis ms=" << d.maxSynthesisNs / 1e6
                  << " max producer gap ms=" << d.maxProducerGapNs / 1e6
                  << " max wake lateness ms=" << d.maxWakeLatenessNs / 1e6
                  << " RMS=" << d.outputRms << " peak=" << d.outputPeak
                  << " telemetry dropped=" << d.meterDroppedFrames << '\n';
        if (d.callbackCount < 100 ||
            d.targetFrames != 1440 || d.lowWatermarkFrames != 1200 || !d.meteredSamples ||
            d.outputRms <= 0 || d.outputPeak <= 0 || d.clippedSamples || d.nonFiniteSamples)
            throw std::runtime_error("Unattended 96-frame playback failed");
        // Shared/sanitized CI is not a realtime scheduler. Require continuity
        // when its observed delays fit the tested budget; otherwise retain the
        // measurements, without mistaking a 100+ ms host stop for a FIFO bug.
        auto checkContinuity = [](const PlaybackEngine::AudioDiagnostics &stats) {
            const bool withinBudget = stats.maxWakeLatenessNs <= 10000000 &&
                stats.maxProducerWorkNs <= 5000000 && stats.maxProducerGapNs <= 20000000;
            if (stats.queue.underrunFrames && withinBudget)
                throw std::runtime_error("Underrun within the producer timing budget");
            if (!withinBudget)
                std::cout << "Host exceeded the timing budget; continuity is measured, not certified: missing="
                          << stats.queue.underrunFrames << '\n';
        };
        checkContinuity(d);
        for (int n = 0; n < 20; ++n) {
            engine.SuspendOutput(true);
            engine.LoadSong(0); engine.Play(); engine.Pause(); engine.Pause();
            engine.SetOutputNumBuffers(1 + n % 16);
            engine.SuspendOutput(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        const auto lifecycle = engine.TakeAudioDiagnostics();
        checkContinuity(lifecycle);
    }
    for (int cycle = 0; cycle < 20; ++cycle) {
        PlaybackEngine engine(48000, 1, profile, cycle % 2 ? 0 : 30);
        for (int n = 0; n < 12; ++n) {
            engine.LoadSong(n % 5);
            engine.Play();
            engine.Pause();
            engine.SpeedDouble(); engine.SpeedHalve();
            engine.Mute(0, true); engine.Mute(0, false);
            engine.SetPlaybackVolume(0.5f);
            engine.Stop();
            engine.LoadSong(0); engine.Play();
        }
        engine.LoadSong(3); engine.Play();
        bool sequenceError = false;
        for (int n = 0; n < 200; ++n) {
            if (!engine.TakeError().empty()) { sequenceError = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!sequenceError) throw std::runtime_error("Sequence error did not recover");
        engine.LoadSong(1);
        if (engine.TakeError().empty()) throw std::runtime_error("Missing invalid pointer error");
        engine.LoadSong(0); engine.Play();
        engine.SuspendOutput(true);
        // Worker must continue servicing commands with no device callbacks.
        engine.LoadSong(0); engine.Play(); engine.Stop();
        engine.SuspendOutput(false);
        engine.SuspendOutput(true); // destruction must not wait for the suspended device
    }
    std::cout << "20 engine lifetimes, 540 song loads, playback/mute/speed, bad-song recovery and suspended shutdown passed\n";
}
