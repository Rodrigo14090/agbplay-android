#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include "AudioOutputMode.hpp"
#include "AudioDataReady.hpp"

// Push transports can defer a read while their native device still has PCM.
// read returns actual stereo frames only; no silence, gain advance or PCM loss
// is introduced on a short read. Both pointers live until output destruction.
struct AudioPcmPull {
    size_t (*read)(void *, std::span<float>) noexcept = nullptr;
    AudioDataReady *ready = nullptr;
};

// The backend owns its device and must stop callbacks before destruction returns.
// Render consumes interleaved float stereo, without locks, allocation, I/O or synthesis.
class AudioOutput
{
public:
    using Render = void (*)(void *, std::span<float>) noexcept;
    virtual ~AudioOutput() = default;
    virtual void Suspend(bool suspended) = 0;
    virtual std::string TakeError() { return {}; } // control thread only
    virtual void DiscardPending() {} // suspended control thread only
    virtual void PollDiagnostics() {} // control thread only, at most once per report
    virtual AudioOutputMode Mode() const { return AudioOutputMode::Automatic; }
    static std::unique_ptr<AudioOutput> Open(uint32_t sampleRate, void *context, Render render,
        AudioOutputMode mode = AudioOutputMode::Automatic, bool suspended = false, AudioPcmPull pull = {});
};
