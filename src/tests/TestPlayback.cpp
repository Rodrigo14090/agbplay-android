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
class TestOutput final : public AudioOutput {
    std::atomic<bool> quit{false}, suspended{false};
    std::thread consumer;
public:
    TestOutput(void *context, Render render) : consumer([this, context, render] {
        std::array<float, 1024> buffer;
        while (!quit) {
            if (!suspended) render(context, buffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }) {}
    ~TestOutput() override { quit = true; consumer.join(); }
    void Suspend(bool s) override { suspended = s; }
};
}
std::unique_ptr<AudioOutput> AudioOutput::Open(uint32_t, void *context, Render render) {
    return std::make_unique<TestOutput>(context, render);
}
int main(int argc, char **argv) {
    const auto bytes = Fixture::RomBytes();
    if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
        std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        return 0;
    }
    const auto path = std::filesystem::temp_directory_path() / "agbplay-synthetic-test.bin";
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove(p); } } cleanup{path};
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    Rom::CreateInstance(path);
    auto profile = Fixture::PlaybackProfile();
    for (int cycle = 0; cycle < 20; ++cycle) {
        PlaybackEngine engine(48000, 1, profile);
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
