#include "SyntheticRom.hpp"
#include "AudioFrameQueue.hpp"
#include "MP2KContext.hpp"
#include "Rom.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
template<class F> void Reject(F &&f, const char *label) {
    bool threw = false;
    try { f(); } catch (const std::exception &) { threw = true; }
    if (!threw) throw std::runtime_error(std::string("Expected rejection: ") + label);
}
int main() {
    auto bytes = Fixture::RomBytes();
    auto rom = Rom::LoadFromBufferRef(bytes);
    CHECK(rom.ValidRange(rom.Size() - 4, 4));
    CHECK(!rom.ValidRange(std::numeric_limits<size_t>::max(), 2));
    CHECK(!rom.ValidRange(2, std::numeric_limits<size_t>::max()));
    Reject([&] { rom.ReadU32(std::numeric_limits<size_t>::max() - 1); }, "rom.ReadU32(std::numeric_limits<size_t>::max() - 1);");
    Reject([&] { rom.ReadU16(rom.Size() - 1); }, "rom.ReadU16(rom.Size() - 1);");
    Reject([&] { rom.GetPtr(rom.Size() + 1); }, "rom.GetPtr(rom.Size() + 1);");
    auto corrupt = bytes;
    corrupt[4] ^= 1;
    Reject([&] { Rom::LoadFromBufferCopy(corrupt); }, "Rom::LoadFromBufferCopy(corrupt);");
    corrupt.resize(32);
    Reject([&] { Rom::LoadFromBufferCopy(corrupt); }, "Rom::LoadFromBufferCopy(corrupt);");
    auto profile = Fixture::PlaybackProfile();
    MP2KContext ctx(48000, 1, rom, profile.mp2kSoundModePlayback, profile.agbplaySoundMode,
                    profile.songTableInfoPlayback, profile.playerTablePlayback);
    Reject([&] { ctx.m4aSongNumStart(1); }, "ctx.m4aSongNumStart(1);");
    Reject([&] { ctx.m4aSongNumStart(2); }, "ctx.m4aSongNumStart(2);");
    Reject([&] { ctx.m4aSongNumStart(4); }, "ctx.m4aSongNumStart(4);");
    ctx.m4aSongNumStart(3);
    Reject([&] { ctx.m4aSoundMain(); }, "ctx.m4aSoundMain();");
    ctx.m4aMPlayAllStop(); ctx.m4aSoundClear();
    ctx.m4aSongNumStart(0);
    double energy = 0;
    for (int frame = 0; frame < 600; ++frame) {
        ctx.m4aSoundMain();
        for (const auto &s : ctx.masterAudioBuffer) {
            CHECK(std::isfinite(s.left) && std::isfinite(s.right));
            energy += double(s.left) * s.left + double(s.right) * s.right;
        }
    }
    CHECK(energy > 0.01);
    // Exercise FIFO order across repeated wraps and changing callback sizes.
    AudioFrameQueue queue;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (int n = 1; n <= 100000; ++n) {
            const sample frame{float(n), -float(n)};
            while (!queue.Put({&frame, 1})) std::this_thread::yield();
        }
        done = true;
    });
    std::array<float, 514> output;
    int expected = 1;
    while (!done || expected <= 100000) {
        queue.Take(output);
        for (size_t i = 0; i < output.size(); i += 2) {
            if (output[i] == 0) { CHECK(output[i + 1] == 0); continue; }
            CHECK(output[i] == float(expected));
            CHECK(output[i + 1] == -float(expected));
            ++expected;
        }
    }
    producer.join();
    CHECK(expected == 100001);
    std::cout << "ROM boundaries, corrupt input, sequence budget, PCM playback, FIFO stress passed; energy=" << energy << '\n';
}
