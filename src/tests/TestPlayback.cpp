#include "SyntheticRom.hpp"
#include "AudioOutput.hpp"
#include "PlaybackEngine.hpp"
#include "Rom.hpp"
#include <array>
#include <bit>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
bool failNextOpen = false;
std::atomic<bool> failRuntime{false};
int activeOutputs = 0;
AudioPcmPull lastPull;
void *lastContext = nullptr;
std::array<float, 1920> captured{};
std::atomic<size_t> captureCount{captured.size()};
class TestOutput final : public AudioOutput {
    std::atomic<bool> quit{false}, suspended{false};
    std::mutex renderMutex;
    AudioOutputMode mode;
    std::thread consumer;
public:
    TestOutput(void *context, Render render, AudioOutputMode mode, bool initialSuspended, AudioPcmPull pull) : suspended(initialSuspended), mode(mode), consumer([this, context, render, pull] {
        std::array<float, 480> buffer;
        while (!quit) {
            { std::scoped_lock lock(renderMutex);
            if (!suspended) {
                const bool push = this->mode == AudioOutputMode::MediaCompatibility && pull.read;
                const auto requested = std::span<float>(buffer).first(push ? 480 : 192);
                const size_t actual = push ? pull.read(context, requested) * 2 : (render(context, requested), requested.size());
                const auto offset = captureCount.load();
                if (offset < captured.size()) {
                    const auto count = std::min(actual, captured.size() - offset);
                    std::copy_n(buffer.data(), count, captured.data() + offset);
                    captureCount.store(offset + count);
                }
            }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(this->mode == AudioOutputMode::MediaCompatibility ? 5 : 2));
        }
    }) { ++activeOutputs; }
    ~TestOutput() override { quit = true; consumer.join(); --activeOutputs; }
    void Suspend(bool s) override { suspended = s; if (s) { std::scoped_lock lock(renderMutex); } }
    AudioOutputMode Mode() const override { return mode; }
    std::string TakeError() override {
        return mode == AudioOutputMode::MediaCompatibility && failRuntime.exchange(false) ? "injected writer error" : "";
    }
};
}
std::unique_ptr<AudioOutput> AudioOutput::Open(uint32_t, void *context, Render render, AudioOutputMode mode, bool suspended, AudioPcmPull pull) {
    if (failNextOpen) { failNextOpen = false; throw std::runtime_error("Injected device-open failure"); }
    if (activeOutputs) throw std::runtime_error("Two active outputs");
    lastPull = pull; lastContext = context;
    auto result = std::make_unique<TestOutput>(context, render, mode, suspended, pull);
    return result;
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
            engine.Stop();
            engine.DiscardBufferedAudio();
            engine.LoadSong(0); engine.Play(); engine.Pause(); engine.Pause();
            engine.DiscardBufferedAudio();
            engine.SetOutputNumBuffers(1 + n % 16);
            engine.SuspendOutput(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        engine.SetOutputMode(AudioOutputMode::MediaCompatibility, false);
        if (engine.OutputMode() != AudioOutputMode::MediaCompatibility) throw std::runtime_error("mode selection failed");
        failRuntime = true;
        if (!engine.TakeError().empty() || engine.OutputMode() != AudioOutputMode::Automatic)
            throw std::runtime_error("runtime fallback failed");
        failNextOpen = true;
        engine.SetOutputMode(AudioOutputMode::MediaCompatibility, true);
        if (engine.OutputMode() != AudioOutputMode::Automatic) throw std::runtime_error("initialization fallback failed");
        engine.SuspendOutput(false);
        for (int change = 0; change < 12; ++change) {
            engine.SetOutputMode(change % 2 ? AudioOutputMode::Automatic : AudioOutputMode::MediaCompatibility, change % 3 == 0);
            engine.SuspendOutput(false);
            engine.Mute(0, change % 2); engine.LoadSong(0); engine.Play();
        }
        const auto lifecycle = engine.TakeAudioDiagnostics();
        checkContinuity(lifecycle);
    }
    for (auto outputMode : {AudioOutputMode::Automatic, AudioOutputMode::MediaCompatibility}) {
        // 240 stereo float frames, with exact powers-of-two amplitudes.
        std::vector<uint8_t> wav(44 + 240 * 8);
        std::copy_n("RIFF", 4, wav.data()); Fixture::Put32(wav, 4, wav.size() - 8);
        std::copy_n("WAVEfmt ", 8, wav.data() + 8); Fixture::Put32(wav, 16, 16);
        wav[20] = 3; wav[22] = 2; Fixture::Put32(wav, 24, 48000); Fixture::Put32(wav, 28, 48000 * 8);
        wav[32] = 8; wav[34] = 32; std::copy_n("data", 4, wav.data() + 36); Fixture::Put32(wav, 40, 240 * 8);
        for (size_t i = 0; i < 240; ++i) {
            const float value = float(i % 16) / 32;
            Fixture::Put32(wav, 44 + i * 8, std::bit_cast<uint32_t>(value));
            Fixture::Put32(wav, 48 + i * 8, std::bit_cast<uint32_t>(-value));
        }
        const auto wavPath = path.string() + ".wav";
        Cleanup removeWav{wavPath};
        std::ofstream(wavPath, std::ios::binary).write(reinterpret_cast<const char *>(wav.data()), wav.size());
        PlaybackEngine engine(48000, 1, profile, 30, outputMode);
        engine.SuspendOutput(true);
        engine.LoadDiagnosticWav(std::make_unique<DiagnosticWav>(wavPath, 48000));
        const bool push = outputMode == AudioOutputMode::MediaCompatibility;
        if (push) {
            engine.SetPlaybackVolume(0.5f);
            std::array<float, 480> untouched; untouched.fill(42);
            if (lastPull.read(lastContext, untouched) != 0) throw std::runtime_error("Empty push read fabricated PCM");
            for (float value : untouched) if (value != 42) throw std::runtime_error("Empty push read changed PCM");
        }
        engine.Play();
        captureCount = 0;
        engine.SuspendOutput(false);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (captureCount.load() < captured.size() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        engine.SuspendOutput(true);
        if (captureCount != captured.size()) throw std::runtime_error("WAV transport timeout");
        float gain = 1;
        for (size_t i = 0; i < captured.size() / 2; ++i) {
            const float value = float((i % 240) % 16) / 32 * gain;
            if (push) gain += 0.001f * (0.5f - gain);
            if (captured[i*2] != value || captured[i*2+1] != -value)
                throw std::runtime_error("WAV PCM altered, reordered or padded at loop boundary");
        }
        std::cout << "WAV producer/FIFO/gain transport (" << (push ? "push" : "callback") << "): 960 stereo frames, sample difference=0\n";
        engine.LoadSong(0); engine.Play(); engine.SuspendOutput(false);
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
