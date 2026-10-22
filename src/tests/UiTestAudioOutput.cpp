#include "AudioOutput.hpp"
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
namespace {
class Output final : public AudioOutput {
    std::atomic<bool> suspended{false};
    std::jthread consumer;
public:
    Output(void *context, Render render) : consumer([this, context, render](std::stop_token stop) {
        std::array<float, 192> pcm{};
        while (!stop.stop_requested()) {
            if (!suspended) render(context, pcm);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }) {}
    ~Output() override { consumer.request_stop(); consumer.join(); }
    void Suspend(bool value) override { suspended = value; }
};
}
// Resolve the backend at link time in this executable only. Production UI and
// PlaybackEngine use their unchanged Qt/PortAudio backend implementations.
std::unique_ptr<AudioOutput> AudioOutput::Open(uint32_t, void *context, Render render)
{
    return std::make_unique<Output>(context, render);
}
