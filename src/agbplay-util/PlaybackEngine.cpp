#include "PlaybackEngine.hpp"
#include "Debug.hpp"
#include "Xcept.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <utility>

PlaybackEngine::PlaybackEngine(uint32_t sampleRate, float volume, const Profile &profile)
    : playerThreadVolumeTarget(volume), playerThreadVolumeCur(volume), profile(profile)
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
    output = AudioOutput::Open(sampleRate, this, &PlaybackEngine::RenderAudio);
    try {
        playerThread = std::make_unique<std::thread>(&PlaybackEngine::threadWorker, this);
    } catch (...) {
        output.reset();
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
        playerThreadQuitRequest = true;
        playerInvokeReady.notify_all();
        playerThread->join();
    }

}

void PlaybackEngine::LoadSong(uint16_t songIdx)
{
    auto func = [this, songIdx]() {
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

void PlaybackEngine::Play()
{
    auto func = [this]() {
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
    outputNumBuffers.store(std::clamp<size_t>(numBuffers, 1, 16));
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
    try {
        std::vector<sample> silenceBuffer(ctx->mixer.GetSamplesPerBuffer());
        while (!playerThreadQuitRequest) {
            InvokeRun();
            if (playerThreadQuitRequest)
                break;
            const auto target = std::min(AudioFrameQueue::Capacity / 2,
                                         outputNumBuffers.load() * callbackFrames.load());
            if (suspended.load() || queue.Queued() > target) {
                std::unique_lock lock(playerInvokeMutex);
                playerInvokeReady.wait_for(lock, 1ms, [this] {
                    return playerInvokePending || playerThreadQuitRequest;
                });
                continue;
            }
            try {
                if (paused) {
                    queue.Put(silenceBuffer);
                } else {
                    ctx->m4aSoundMain();
                    updateVisualizerState();
                    if (!queue.Put(ctx->masterAudioBuffer))
                        throw Xcept("Synthesis frame exceeds audio queue capacity");
                }
                if (ctx->SongEnded() && !songEnded) {
                    ctx->m4aMPlayAllStop();
                    ctx->m4aSoundClear();
                    songEnded = true;
                }
            } catch (const std::exception &e) {
                // Bad ROM data must not terminate the worker or strand UI commands.
                Recover(e.what());
            }
        }
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
    std::scoped_lock lock(errorMutex);
    return std::exchange(playbackError, {});
}

void PlaybackEngine::SuspendOutput(bool suspend)
{
    suspended.store(suspend);
    output->Suspend(suspend);
    playerInvokeReady.notify_one();
}

void PlaybackEngine::updateVisualizerState()
{
    /* We assume that GetVisualizerState may be more expensive than a simple copy.
     * Accordingly we generate the state, then we lock the mutex and publish the state.
     * That way the mutex is only locked for the smallest duration necessary. */

    ctx->GetVisualizerState(visualizerStatePlayer);

    std::scoped_lock l(visualizerStateMutex);

    visualizerStateObserver = visualizerStatePlayer;
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
    self.callbackFrames.store(std::max<size_t>(1, output.size() / 2), std::memory_order_relaxed);
    self.queue.Take(output);
    const float target = self.playerThreadVolumeTarget.load(std::memory_order_relaxed);
    for (size_t i = 0; i + 1 < output.size(); i += 2) {
        output[i] *= self.playerThreadVolumeCur;
        output[i + 1] *= self.playerThreadVolumeCur;
        self.playerThreadVolumeCur += self.volumeRampCoeff * (target - self.playerThreadVolumeCur);
    }
}
