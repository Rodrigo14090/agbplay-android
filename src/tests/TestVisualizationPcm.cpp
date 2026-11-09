#include "VisualizationPcmTap.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main()
{
    VisualizationPcmTap tap;
    std::array<float, 1024> pcm{};
    tap.Publish(pcm);
    CHECK(tap.Read(pcm).frames == 0); // disabled has no side effects
    tap.Enable(true);
    // Android's 96-frame callbacks must assemble exact stereo snapshots.
    std::vector<float> input(512 * 2 * 20);
    for (size_t f = 0; f < input.size() / 2; ++f) { input[2*f] = float(f); input[2*f+1] = -float(f); }
    for (size_t at = 0; at < input.size();) {
        const auto count = std::min<size_t>(192, input.size() - at);
        tap.Publish(std::span(input).subspan(at, count)); at += count;
    }
    CHECK(tap.Read(pcm).frames == 512);
    CHECK(pcm[0] == float(19*512)); // slow reader receives newest, not old backlog
    for (size_t f=0; f<512; ++f) CHECK(pcm[2*f+1] == -pcm[2*f]);
    tap.Publish(std::span(input).first(2048));
    CHECK(tap.Read(pcm).skippedFrames == 512);
    tap.Publish(pcm); tap.Enable(false); tap.Enable(true);
    CHECK(tap.Read(pcm).frames == 0); // no stale epoch, even across immediate resume
    tap.Publish(std::span(input).first(100));
    tap.Enable(false); tap.Enable(true);
    tap.Publish(std::span(input).subspan(2048,1024));
    CHECK(tap.Read(pcm).frames == 512 && pcm[0] == 1024); // discard partial old block

    std::atomic<bool> done{false};
    std::thread writer([&] {
        std::array<float,192> burst{};
        for (size_t base=0; base<1000000; base+=96) {
            for (size_t f=0; f<96; ++f) { burst[2*f]=float(base+f); burst[2*f+1]=-float(base+f); }
            tap.Publish(burst);
        }
        done.store(true, std::memory_order_release);
    });
    size_t reads=0;
    while (!done.load(std::memory_order_acquire)) {
        if (!tap.Read(pcm).frames) continue;
        for (size_t f=0; f<512; ++f) {
            CHECK(pcm[2*f+1] == -pcm[2*f]);
            CHECK(pcm[2*f] == pcm[0] + float(f));
        }
        ++reads;
        if (reads % 100 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    writer.join();
    CHECK(reads > 0);
    std::cout << "PASS visual tap: 96-frame bursts, full/empty, epochs, latest-wins, concurrent stereo PCM; reads=" << reads << '\n';
}
