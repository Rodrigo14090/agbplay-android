#pragma once

#include "AudioOutput.hpp"
#include "AudioFrameQueue.hpp"
#include "AudioBufferPolicy.hpp"
#include "AudioPcmStatistics.hpp"
#include "MP2KContext.hpp"
#include "Profile.hpp"

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include <thread>
#include <vector>

class PlaybackEngine
{
public:
    struct AudioDiagnostics {
        AudioFrameQueue::Diagnostics queue;
        uint64_t maxSynthesisNs;
        uint64_t maxProducerGapNs;
        uint64_t maxCallbackNs;
        uint64_t maxCallbackIntervalNs;
        size_t minCallbackFrames;
        size_t maxCallbackFrames;
        double outputRms;
        float outputPeak;
        uint64_t clippedSamples;
        uint64_t nonFiniteSamples;
        size_t targetFrames;
        size_t lowWatermarkFrames;
        size_t synthesisBlockFrames;
        uint32_t sampleRate;
        uint64_t callbackCount;
        uint64_t requestedFrames;
        uint64_t producedFrames;
        uint64_t maxProducerWorkNs;
        uint64_t maxVisualizerNs;
        uint64_t maxWakeLatenessNs;
        uint64_t meterDroppedFrames;
        uint64_t meteredSamples;
        double elapsedSeconds;
    };
    PlaybackEngine(uint32_t sampleRate, float volume, const Profile &profile,
                   uint32_t reserveMs = AudioBufferPolicy::DefaultReserveMs);
    PlaybackEngine(const PlaybackEngine &) = delete;
    PlaybackEngine &operator=(const PlaybackEngine &) = delete;
    ~PlaybackEngine();

    void LoadSong(uint16_t songIdx);
    void Play();
    bool Pause();
    void Stop();
    void SpeedDouble();
    void SpeedHalve();
    bool SongEnded() const;
    void ToggleMute(size_t index);
    void Mute(size_t index, bool mute);
    SongInfo GetSongInfo();
    void UpdateSoundMode();
    void GetVisualizerState(MP2KVisualizerState &visualizerState);
    void SetOutputNumBuffers(size_t numBuffers);
    void SetMaxLoops(int8_t maxLoops);
    void SetPlaybackVolume(float volume);
    void SuspendOutput(bool suspend);
    std::string TakeError();
    AudioDiagnostics TakeAudioDiagnostics() noexcept;

private:
    void threadWorker();
    void updateVisualizerState();
    void InvokeAsPlayer(const std::function<void(void)> &func);
    void InvokeRun();
    void PrimeOutput();
    void DrainOutputMetrics();
    static void RenderAudio(void *user, std::span<float> output) noexcept;
    void Recover(const char *message);
    std::unique_ptr<AudioOutput> output;
    uint32_t speedFactor = 64;
    std::bitset<16> trackMuted;    // TODO replace 16 with constant
    bool paused = false;
    std::unique_ptr<MP2KContext> ctx;

    MP2KVisualizerState visualizerStatePlayer;
    MP2KVisualizerState visualizerStateObserver;
    std::mutex visualizerStateMutex;

    std::mutex playerInvokeMutex;
    std::condition_variable playerInvokeReady;
    std::condition_variable playerInvokeComplete;
    std::atomic<bool> playerInvokePending = false;
    std::unique_ptr<std::thread> playerThread;
    std::atomic<bool> playerThreadQuitRequest = false;
    std::string playerThreadQuitError;
    bool playerThreadQuitComplete = false;
    bool primingRequested = false; // protected by playerInvokeMutex
    uint64_t scheduleRevision = 0; // protected by playerInvokeMutex
    std::atomic<float> playerThreadVolumeTarget;
    float playerThreadVolumeCur;
    const float volumeRampCoeff = 0.001f;

    std::atomic<bool> songEnded = false;

    AudioFrameQueue queue;
    const uint32_t sampleRate;
    const AudioBufferPolicy bufferPolicy;
    std::atomic<size_t> outputNumBuffers{2};
    std::atomic<size_t> callbackFrames{512};
    std::atomic<bool> suspended{false};
    std::atomic<uint64_t> maxSynthesisNs{0};
    std::atomic<uint64_t> maxProducerGapNs{0};
    std::atomic<uint64_t> maxCallbackNs{0};
    std::atomic<uint64_t> maxCallbackIntervalNs{0};
    std::atomic<uint64_t> lastCallbackStartNs{0};
    std::atomic<size_t> minCallbackFrames{static_cast<size_t>(-1)};
    std::atomic<size_t> maxCallbackFrames{0};
    std::atomic<uint64_t> callbackCount{0};
    std::atomic<uint64_t> callbackRequestedFrames{0};
    std::atomic<uint64_t> producedFrames{0};
    std::atomic<uint64_t> maxProducerWorkNs{0};
    std::atomic<uint64_t> maxVisualizerNs{0};
    std::atomic<uint64_t> maxWakeLatenessNs{0};
    std::atomic<uint64_t> meterDroppedFrames{0};
    std::unique_ptr<AudioFrameQueue> meterQueue;
    std::mutex pcmStatsMutex;
    AudioPcmStatistics pcmStats;
    std::chrono::steady_clock::time_point diagnosticsAt = std::chrono::steady_clock::now();
    std::function<void()> playerCommand;
    std::mutex errorMutex;
    std::string playbackError;

    const Profile &profile;
    uint16_t songIdx = 0;
};
