#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>

// The backend owns its device and must stop callbacks before destruction returns.
// Render consumes interleaved float stereo, without locks, allocation, I/O or synthesis.
class AudioOutput
{
public:
    using Render = void (*)(void *, std::span<float>) noexcept;
    virtual ~AudioOutput() = default;
    virtual void Suspend(bool suspended) = 0;
    virtual std::string TakeError() { return {}; } // control thread only
    static std::unique_ptr<AudioOutput> Open(uint32_t sampleRate, void *context, Render render);
};
