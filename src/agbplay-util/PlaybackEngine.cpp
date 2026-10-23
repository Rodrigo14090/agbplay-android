#include "PlaybackEngine.hpp"
#include "Debug.hpp"
#include "Xcept.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <utility>
#ifdef __ANDROID__
#include <QJniEnvironment>
#endif

#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
namespace {
template<class T> void UpdateMaximum(std::atomic<T> &value, T candidate) noexcept
{
    auto current = value.load(std::memory_order_relaxed);
    while (candidate > current &&
           !value.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {}
}
template<class T> void UpdateMinimum(std::atomic<T> &value, T candidate) noexcept
{
    auto current = value.load(std::memory_order_relaxed);
    while (candidate < current &&
           !value.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {}
}
uint64_t ElapsedNs(std::chrono::steady_clock::time_point start) noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count());
}
}
#endif

PlaybackEngine::PlaybackEngine(uint32_t sampleRate, float volume, const Profile &profile, uint32_t reserveMs, AudioOutputMode outputMode)
    : playerThreadVolumeTarget(volume), playerThreadVolumeCur(volume), sampleRate(sampleRate),
      bufferPolicy(sampleRate, reserveMs), profile(profile)
{
    ctx = std::make_unique<MP2KContext>(
        sampleRate,
        1,
        Rom::Instance(),
        profile.mp2kSoundModePlayback,
        profile.agbplaySoundMode,
        profile.songTableInfoPlayback,
        profile.playerTablePlayback
    );
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
    meterQueue = std::make_unique<AudioFrameQueue>();
#endif
    try {
        playerThread = std::make_unique<std::thread>(&PlaybackEngine::threadWorker, this);
        PrimeOutput(); // PCM is available before the backend can call RenderAudio.
        output = AudioOutput::Open(sampleRate, this, &PlaybackEngine::RenderAudio, outputMode);
    } catch (...) {
        output.reset();
        {
            std::scoped_lock lock(playerInvokeMutex);
            playerThreadQuitRequest = true;
        }
        playerInvokeReady.notify_all();
        if (playerThread && playerThread->joinable()) playerThread->join();
        throw;
    }
#ifdef __linux__
    pthread_setname_np(playerThread->native_handle(), "mixer thread");
#endif
}

PlaybackEngine::~PlaybackEngine()
{
    output.reset(); // joins/stops the device callback before queue/context destruction
    // stop and deallocate player thread if required
    if (playerThread) {
        {
            std::scoped_lock lock(playerInvokeMutex);
            playerThreadQuitRequest = true;
        }
        playerInvokeReady.notify_all();
        playerThread->join();
    }

}

void PlaybackEngine::LoadSong(uint16_t songIdx)
{
    auto func = [this, songIdx]() {
        diagnosticWav.reset();
        trackMuted.reset();
        const uint8_t playerIdx = ctx->primaryPlayer;
        if (playerIdx >= ctx->players.size())
            return;

        ctx->m4aMPlayAllStop();
        ctx->m4aSongNumStart(songIdx);
        ctx->m4aMPlayStop(ctx->primaryPlayer);
        paused = false;
    };

    InvokeAsPlayer(func);
}

void PlaybackEngine::LoadDiagnosticWav(std::unique_ptr<DiagnosticWav> wav)
{
    if (!suspended.load()) throw Xcept("Suspend audio before loading a diagnostic WAV");
    InvokeAsPlayer([this, &wav] {
        ctx->m4aMPlayAllStop();
        diagnosticWav = std::move(wav);
        paused = true;
        songEnded = false;
    });
    DiscardBufferedAudio();
}

void PlaybackEngine::Play()
{
    auto func = [this]() {
        if (diagnosticWav) {
            if (songEnded) diagnosticWav->Rewind();
            paused = false; songEnded = false; return;
        }
        const uint8_t playerIdx = ctx->primaryPlayer;
        if (playerIdx >= ctx->players.size())
            return;

        if (paused) {
            paused = false;
        } else {
            MP2KPlayer &player = ctx->players.at(playerIdx);
            ctx->m4aMPlayStart(playerIdx, player.songHeaderPos);
            for (size_t i = 0; i < std::min(player.tracks.size(), trackMuted.size()); i++)
                player.tracks.at(i).muted = trackMuted[i];
            songEnded = false;
        }
    };

    InvokeAsPlayer(func);
}

bool PlaybackEngine::Pause()
{
    bool playingAgain = false;
    auto func = [this, &playingAgain]() {
        if (diagnosticWav) { paused = !paused; playingAgain = !paused; return; }
        const uint8_t playerIdx = ctx->primaryPlayer;
        if (playerIdx >= ctx->players.size())
            return;

        const bool playing = ctx->m4aMPlayIsPlaying(playerIdx);
        if (playing) {
            paused = !paused;
        } else {
            MP2KPlayer &player = ctx->players.at(playerIdx);
            ctx->m4aMPlayStart(playerIdx, ctx->players.at(playerIdx).songHeaderPos);
            for (size_t i = 0; i < std::min(player.tracks.size(), trackMuted.size()); i++)
                player.tracks.at(i).muted = trackMuted[i];
            songEnded = false;
            paused = false;
        }
        playingAgain = !paused;
    };

    InvokeAsPlayer(func);

    /* Return true if the player was paused an is now playing again. False otherwise. */
    return playingAgain;
}

void PlaybackEngine::Stop()
{
    auto func = [this]() {
        if (diagnosticWav) { diagnosticWav->Rewind(); paused = true; songEnded = false; return; }
        const uint8_t playerIdx = ctx->primaryPlayer;
        if (playerIdx >= ctx->players.size())
            return;

        ctx->m4aMPlayAllStop();
        ctx->m4aMPlayStart(playerIdx, ctx->players.at(playerIdx).songHeaderPos);
        ctx->m4aMPlayStop(playerIdx);
        songEnded = false;
        paused = false;
    };

    InvokeAsPlayer(func);
}

void PlaybackEngine::SpeedDouble()
{
    auto func = [this]() {
        speedFactor = std::min<uint32_t>(speedFactor * 2, 1024);
        // TODO replace this with m4aMPlayTempoControl
        ctx->reader.SetSpeedFactor(float(speedFactor) / 64.0f);
    };

    InvokeAsPlayer(func);
}

void PlaybackEngine::SpeedHalve()
{
    auto func = [this]() {
        speedFactor = std::max<uint32_t>(speedFactor / 2, 1);
        // TODO replace this with m4aMPlayTempoControl
        ctx->reader.SetSpeedFactor(float(speedFactor) / 64.0f);
    };

    InvokeAsPlayer(func);
}

bool PlaybackEngine::SongEnded() const
{
    return songEnded;
}

void PlaybackEngine::ToggleMute(size_t index)
{
    if (index >= trackMuted.size())
        return;

    auto func = [this, index]() {
        trackMuted[index] = !trackMuted[index];
        const uint8_t playerIdx = ctx->primaryPlayer;
        if (playerIdx >= ctx->players.size())
            return;
        MP2KPlayer &player = ctx->players.at(playerIdx);
        if (index >= player.tracks.size())
            return;
        MP2KTrack &trk = player.tracks.at(index);
        trk.muted = !trk.muted;
    };

    InvokeAsPlayer(func);
}

void PlaybackEngine::Mute(size_t index, bool mute)
{
    if (index >= trackMuted.size())
        return;

    auto func = [this, index, mute]() {
        trackMuted[index] = mute;
        const uint8_t playerIdx = ctx->primaryPlayer;
        if (playerIdx >= ctx->players.size())
            return;
        MP2KPlayer &player = ctx->players.at(playerIdx);
        if (index >= player.tracks.size())
            return;
        MP2KTrack &trk = player.tracks.at(index);
        trk.muted = mute;
    };

    InvokeAsPlayer(func);
}

SongInfo PlaybackEngine::GetSongInfo()
{
    SongInfo songInfo;

    auto func = [this, &songInfo]() {
        const MP2KPlayer &player = ctx->players.at(ctx->primaryPlayer);

        songInfo.songHeaderPos = player.songHeaderPos;
        songInfo.voiceTablePos = player.bankPos;
        songInfo.reverb = player.reverb;
        songInfo.priority = player.priority;
        songInfo.playerIdx = player.playerIdx;
    };

    InvokeAsPlayer(func);

    return songInfo;
}

void PlaybackEngine::UpdateSoundMode()
{
    /* Update the sound mode live. This may happen if the used has edited
     * the profile in the profile editor. No reloading is required and changes should
     * apply immediately. */

    const auto newAgbMode = profile.agbplaySoundMode;
    const auto newMode = profile.mp2kSoundModePlayback;
    auto func = [this, newAgbMode, newMode]() {
        ctx->agbplaySoundMode = newAgbMode;
        ctx->m4aSoundModeReverb(newMode.rev);
        ctx->m4aSoundModePCMVol(newMode.vol);
        ctx->m4aSoundModePCMFreq(newMode.freq);
        ctx->m4aSoundModeDacConfig(newMode.dacConfig);
        /* Do not update the playertable since it may cause playback issues.
         * The user is supposed to reload the game if that was changed with the
         * profile editor. */
    };

    InvokeAsPlayer(func);
}

void PlaybackEngine::GetVisualizerState(MP2KVisualizerState &visualizerState)
{
    /* We do not use InvokeAsPlayer, as that would block the call until the background thread
     * makes the data available. Like this, the mutex only blocks for the actual copy. */
    std::scoped_lock l(visualizerStateMutex);

    visualizerState = visualizerStateObserver;
}

void PlaybackEngine::SetOutputNumBuffers(size_t numBuffers) {
    {
        std::scoped_lock lock(playerInvokeMutex);
        outputNumBuffers.store(std::clamp<size_t>(numBuffers, 1, 16));
        ++scheduleRevision;
    }
    playerInvokeReady.notify_one();
}

void PlaybackEngine::SetMaxLoops(int8_t maxLoops) {
    auto func = [this, maxLoops]() {
        ctx->m4aSetMaxLoops(maxLoops);
    };

    InvokeAsPlayer(func);
}

void PlaybackEngine::SetPlaybackVolume(float volume)
{
    playerThreadVolumeTarget = volume;
}

/*
 * private PlaybackEngine
 */

void PlaybackEngine::threadWorker()
{
    using namespace std::chrono_literals;
#ifdef __ANDROID__
    // Android gives normal app threads a lower scheduling class than its audio
    // callback. Promote only this synthesis worker so UI activity cannot be
    // the event that makes the producer run often enough.
    {
        QJniEnvironment env;
        bool prioritySet = false;
        jclass process = env->FindClass("android/os/Process");
        if (process) {
            jmethodID setPriority = env->GetStaticMethodID(process, "setThreadPriority", "(I)V");
            if (setPriority) {
                env->CallStaticVoidMethod(process, setPriority, -16); // THREAD_PRIORITY_AUDIO
                prioritySet = !env->ExceptionCheck();
            }
            env->DeleteLocalRef(process);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            Debug::print("Could not set Android audio thread priority");
        } else if (prioritySet) {
            Debug::print("Android synthesis thread priority set to THREAD_PRIORITY_AUDIO (-16)");
        } else {
            Debug::print("Android audio thread priority API unavailable");
        }
    }
#endif
    try {
        const size_t blockFrames = ctx->mixer.GetSamplesPerBuffer();
        if (!blockFrames || blockFrames > AudioFrameQueue::Capacity)
            throw Xcept("Invalid synthesis block size: {}", blockFrames);
        std::vector<sample> silenceBuffer(blockFrames);
        std::vector<sample> wavBuffer(blockFrames);
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
        auto lastProducedAt = std::chrono::steady_clock::time_point{};
#endif
        while (!playerThreadQuitRequest) {
            InvokeRun();
            DrainOutputMetrics();
            if (playerThreadQuitRequest) break;
            if (suspended.load()) {
                std::unique_lock lock(playerInvokeMutex);
                playerInvokeReady.wait(lock, [this] {
                    return playerInvokePending || playerThreadQuitRequest || !suspended.load();
                });
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
                lastProducedAt = {}; // intentional suspension is not a producer stall
#endif
                continue;
            }
            const size_t target = bufferPolicy.Target(outputNumBuffers.load(), callbackFrames.load());
            const size_t queued = queue.Queued();
            if (queued >= target || queue.Writable() < blockFrames) {
                std::unique_lock lock(playerInvokeMutex);
                // A control command may have changed the buffer preference
                // between calculating target and acquiring this mutex.
                if (target != bufferPolicy.Target(outputNumBuffers.load(), callbackFrames.load())) continue;
                if ((queued >= target || (queued > 0 && queue.Writable() < blockFrames)) && primingRequested) {
                    primingRequested = false;
                    playerInvokeComplete.notify_all();
                }
                const auto revision = scheduleRevision;
                const auto deadline = std::chrono::steady_clock::now() + bufferPolicy.WaitForRefill(queued, target);
                const bool interrupted = playerInvokeReady.wait_until(lock, deadline, [this, revision] {
                    return playerInvokePending || playerThreadQuitRequest || suspended.load() ||
                           scheduleRevision != revision;
                });
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
                if (!interrupted && std::chrono::steady_clock::now() > deadline)
                    UpdateMaximum(maxWakeLatenessNs, ElapsedNs(deadline));
#else
                (void)interrupted;
#endif
                continue;
            }
            try {
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
                const auto startedAt = std::chrono::steady_clock::now();
                if (lastProducedAt != std::chrono::steady_clock::time_point{})
                    UpdateMaximum(maxProducerGapNs, static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(startedAt - lastProducedAt).count()));
                lastProducedAt = startedAt;
#endif
                if (!paused && diagnosticWav) {
                    size_t read = diagnosticWav->Read(wavBuffer);
                    while (read < blockFrames) {
                        diagnosticWav->Rewind();
                        const auto count = diagnosticWav->Read(std::span<sample>(wavBuffer).subspan(read));
                        if (!count) throw Xcept("Diagnostic WAV contains no readable frames");
                        read += count;
                    }
                } else if (!paused) ctx->m4aSoundMain();
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
                UpdateMaximum(maxSynthesisNs, ElapsedNs(startedAt));
                const auto visualizerAt = std::chrono::steady_clock::now();
#endif
                if (!paused && !diagnosticWav) updateVisualizerState();
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
                UpdateMaximum(maxVisualizerNs, ElapsedNs(visualizerAt));
#endif
                const std::span<const sample> pcm = paused ? std::span<const sample>(silenceBuffer)
                                                         : diagnosticWav ? std::span<const sample>(wavBuffer)
                                                                         : std::span<const sample>(ctx->masterAudioBuffer);
                // Space was checked before synthesis. Only the consumer can
                // advance meanwhile, so failure is an invariant violation.
                if (!queue.Put(pcm)) throw Xcept("Synthesis frame exceeds available audio queue space");
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
                producedFrames.fetch_add(pcm.size(), std::memory_order_relaxed);
                UpdateMaximum(maxProducerWorkNs, ElapsedNs(startedAt));
#endif
                if (!diagnosticWav && ctx->SongEnded() && !songEnded) {
                    ctx->m4aMPlayAllStop();
                    ctx->m4aSoundClear();
                    songEnded = true;
                }
            } catch (const std::exception &e) {
                Recover(e.what());
            }
        }
        DrainOutputMetrics();
    } catch (const std::exception &e) {
        std::scoped_lock lock(playerInvokeMutex);
        playerThreadQuitError = e.what();
    }
    {
        std::scoped_lock lock(playerInvokeMutex);
        playerThreadQuitComplete = true;
    }
    playerInvokeComplete.notify_all();
}

void PlaybackEngine::Recover(const char *message)
{
    Debug::print("Song stopped: {}", message);
    // Stop channels before track state is reused; retain the mixer/reverb settings.
    ctx->m4aMPlayAllStop();
    ctx->m4aSoundClear();
    for (auto &player : ctx->players)
        player.Init(ctx->rom, 0);
    ctx->primaryPlayer = 0;
    paused = true;
    songEnded = false; // do not automatically advance through broken table entries
    {
        std::scoped_lock lock(errorMutex);
        playbackError = message;
    }
    updateVisualizerState();
}

std::string PlaybackEngine::TakeError()
{
    if (!output) return "No audio output available; reopen the ROM to retry";
    if (auto deviceError = output->TakeError(); !deviceError.empty())
    {
        if (output->Mode() == AudioOutputMode::MediaCompatibility) {
            Debug::print("AudioTrack runtime failure: {}; attempting Qt fallback", deviceError);
            try { SetOutputMode(AudioOutputMode::Automatic, suspended.load()); return {}; }
            catch (const std::exception &error) { return deviceError + "; fallback failed: " + error.what(); }
        }
        return deviceError;
    }
    std::scoped_lock lock(errorMutex);
    return std::exchange(playbackError, {});
}

void PlaybackEngine::PrimeOutput()
{
    std::unique_lock lock(playerInvokeMutex);
    primingRequested = true;
    suspended.store(false);
    ++scheduleRevision;
    playerInvokeReady.notify_one();
    playerInvokeComplete.wait(lock, [this] { return !primingRequested || playerThreadQuitComplete; });
    if (playerThreadQuitComplete) throw Xcept("Playback thread stopped while priming: {}", playerThreadQuitError);
}

void PlaybackEngine::SuspendOutput(bool suspend)
{
    if (!output) return;
    if (suspend) {
        // Keep supplying PCM until the device has stopped consuming it.
        output->Suspend(true);
        {
            std::scoped_lock lock(playerInvokeMutex);
            suspended.store(true);
            ++scheduleRevision;
        }
        playerInvokeReady.notify_one();
    } else {
        PrimeOutput(); // fill while the device is still suspended
        output->Suspend(false);
    }
}

void PlaybackEngine::DiscardBufferedAudio()
{
    if (output) output->DiscardPending();
    InvokeAsPlayer([this] { queue.DiscardPending(); ++scheduleRevision; });
}

void PlaybackEngine::SetOutputMode(AudioOutputMode mode, bool keepSuspended)
{
    // UI/control thread only. Stop and join the old consumer before opening
    // another. The one producer, mixer, gain and FIFO survive the switch.
    if (output) SuspendOutput(true);
    output.reset();
    PrimeOutput();
    try {
        output = AudioOutput::Open(sampleRate, this, &PlaybackEngine::RenderAudio, mode, keepSuspended);
    } catch (...) {
        if (mode == AudioOutputMode::Automatic) throw;
        Debug::print("Audio mode change failed; reopening Qt output");
        output = AudioOutput::Open(sampleRate, this, &PlaybackEngine::RenderAudio, AudioOutputMode::Automatic, keepSuspended);
    }
    if (keepSuspended) SuspendOutput(true);
}

void PlaybackEngine::PollOutputDiagnostics()
{
    if (!output) return;
    try { output->PollDiagnostics(); }
    catch (const std::exception &error) { Debug::print("Output diagnostics unavailable: {}", error.what()); }
}

void PlaybackEngine::updateVisualizerState()
{
    // Generate privately, then publish without waiting for the UI reader.

    ctx->GetVisualizerState(visualizerStatePlayer);

    // The UI may allocate/copy a large snapshot while it owns this mutex.
    // Publishing is optional; never delay the PCM producer waiting for the UI.
    std::unique_lock lock(visualizerStateMutex, std::try_to_lock);
    if (lock.owns_lock()) visualizerStateObserver = visualizerStatePlayer;
}

void PlaybackEngine::InvokeAsPlayer(const std::function<void(void)> &func)
{
    // UI thread only. Execute commands on the synthesis worker; predicate waits
    // handle spurious wakeups and worker termination.
    assert(std::this_thread::get_id() != playerThread->get_id());
    std::unique_lock lock(playerInvokeMutex);
    if (playerThreadQuitComplete)
        throw Xcept("Playback thread stopped: {}", playerThreadQuitError);
    playerCommand = func;
    playerInvokePending = true;
    playerInvokeReady.notify_one();
    playerInvokeComplete.wait(lock, [this] { return !playerInvokePending || playerThreadQuitComplete; });
    if (playerThreadQuitComplete)
        throw Xcept("Playback thread stopped: {}", playerThreadQuitError);
}

void PlaybackEngine::InvokeRun()
{
    std::unique_lock lock(playerInvokeMutex);
    if (!playerInvokePending)
        return;
    try {
        playerCommand();
    } catch (const std::exception &e) {
        Recover(e.what());
    }
    playerCommand = {};
    playerInvokePending = false;
    lock.unlock();
    playerInvokeComplete.notify_one();
}

void PlaybackEngine::RenderAudio(void *user, std::span<float> output) noexcept
{
    auto &self = *static_cast<PlaybackEngine *>(user);
    const size_t frames = output.size() / 2;
    self.callbackFrames.store(std::max<size_t>(1, frames), std::memory_order_relaxed);
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
    const auto startedAt = std::chrono::steady_clock::now();
    const auto startedNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(startedAt.time_since_epoch()).count());
    const auto previousCallbackNs = self.lastCallbackStartNs.exchange(startedNs, std::memory_order_relaxed);
    if (previousCallbackNs != 0 && startedNs >= previousCallbackNs)
        UpdateMaximum(self.maxCallbackIntervalNs, startedNs - previousCallbackNs);
    UpdateMinimum(self.minCallbackFrames, frames);
    UpdateMaximum(self.maxCallbackFrames, frames);
    self.callbackCount.fetch_add(1, std::memory_order_relaxed);
    self.callbackRequestedFrames.fetch_add(frames, std::memory_order_relaxed);
#endif
    self.queue.Take(output);
    const float target = self.playerThreadVolumeTarget.load(std::memory_order_relaxed);
    for (size_t i = 0; i + 1 < output.size(); i += 2) {
        output[i] *= self.playerThreadVolumeCur;
        output[i + 1] *= self.playerThreadVolumeCur;
        self.playerThreadVolumeCur += self.volumeRampCoeff * (target - self.playerThreadVolumeCur);
    }
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
    // Bounded PCM tap: only a copy on the realtime thread. Analysis runs on
    // the producer. If telemetry fills, drop telemetry, never playback PCM.
    if (!self.meterQueue->PutInterleaved(output))
        self.meterDroppedFrames.fetch_add(frames, std::memory_order_relaxed);
    UpdateMaximum(self.maxCallbackNs, ElapsedNs(startedAt));
#endif
}

void PlaybackEngine::DrainOutputMetrics()
{
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
    std::unique_lock lock(pcmStatsMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    std::array<float, 1024> pcm;
    // Bound analysis even if the consumer is continuously filling the tap.
    for (int block = 0; block < 8; ++block) {
        const size_t frames = meterQueue->Read(pcm);
        if (!frames) break;
        pcmStats.Add(std::span<const float>(pcm).first(frames * 2));
    }
#endif
}

PlaybackEngine::AudioDiagnostics PlaybackEngine::TakeAudioDiagnostics() noexcept
{
    AudioDiagnostics result{};
    result.queue = queue.TakeDiagnostics();
    result.sampleRate = sampleRate;
    result.targetFrames = bufferPolicy.Target(outputNumBuffers.load(), callbackFrames.load());
    result.lowWatermarkFrames = bufferPolicy.LowWatermark(result.targetFrames);
    result.synthesisBlockFrames = ctx->mixer.GetSamplesPerBuffer(); // immutable after construction
#if defined(__ANDROID__) || defined(AGBPLAY_AUDIO_DIAGNOSTICS)
    const auto now = std::chrono::steady_clock::now();
    result.elapsedSeconds = std::chrono::duration<double>(now - diagnosticsAt).count();
    diagnosticsAt = now;
    result.maxSynthesisNs = maxSynthesisNs.exchange(0, std::memory_order_relaxed);
    result.maxProducerGapNs = maxProducerGapNs.exchange(0, std::memory_order_relaxed);
    result.maxCallbackNs = maxCallbackNs.exchange(0, std::memory_order_relaxed);
    result.maxCallbackIntervalNs = maxCallbackIntervalNs.exchange(0, std::memory_order_relaxed);
    result.minCallbackFrames = minCallbackFrames.exchange(static_cast<size_t>(-1), std::memory_order_relaxed);
    if (result.minCallbackFrames == static_cast<size_t>(-1)) result.minCallbackFrames = 0;
    result.maxCallbackFrames = maxCallbackFrames.exchange(0, std::memory_order_relaxed);
    result.callbackCount = callbackCount.exchange(0, std::memory_order_relaxed);
    result.requestedFrames = callbackRequestedFrames.exchange(0, std::memory_order_relaxed);
    result.producedFrames = producedFrames.exchange(0, std::memory_order_relaxed);
    result.maxProducerWorkNs = maxProducerWorkNs.exchange(0, std::memory_order_relaxed);
    result.maxVisualizerNs = maxVisualizerNs.exchange(0, std::memory_order_relaxed);
    result.maxWakeLatenessNs = maxWakeLatenessNs.exchange(0, std::memory_order_relaxed);
    result.meterDroppedFrames = meterDroppedFrames.exchange(0, std::memory_order_relaxed);
    {
        std::scoped_lock lock(pcmStatsMutex);
        result.outputRms = pcmStats.Rms();
        result.outputPeak = pcmStats.peak;
        result.clippedSamples = pcmStats.clippedSamples;
        result.nonFiniteSamples = pcmStats.nonFiniteSamples;
        result.meteredSamples = pcmStats.finiteSamples + pcmStats.nonFiniteSamples;
        pcmStats = {};
    }
#endif
    return result;
}
