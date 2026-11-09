#include "VisualizationPcmTap.hpp"
#include "../visualization/FrameCadence.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main()
{
    // Model a fast GPU and an overloaded software GPU for 300 submissions.
    // Even below 1 FPS the heavy case must yield, rather than running forever
    // at 100% of the submission thread while trying to catch up to a timer.
    for (const int64_t work : {2000000LL, 140000000LL, 2000000000LL}) {
        int64_t deadline = 0, now = 0, idle = 0;
        for (int n=0; n<300; ++n) {
            now += work;
            const auto next = FrameCadence::NextDeadline(deadline, now, 60, work);
            idle += next - now; deadline = now = next;
        }
        CHECK(idle >= 300 * work);
        if (work == 2000000) CHECK(now >= 4900000000LL && now <= 5100000000LL); // retain ~60 FPS
    }
    CHECK(FrameCadence::LowerRate(15) == 10 && FrameCadence::LowerRate(5) == 2);
    CHECK(FrameCadence::LowerRate(2) == 1 && FrameCadence::LowerRate(1) == 1);
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
