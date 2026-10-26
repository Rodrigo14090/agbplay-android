#include "AudioOutput.hpp"
#include "Debug.hpp"
#include "Xcept.hpp"
#include <QJniEnvironment>
#include <QJniObject>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {
void CheckJava(QJniEnvironment &env) {
    if (!env->ExceptionCheck()) return;
    const auto exception = env->ExceptionOccurred();
    env->ExceptionClear();
    const auto object = QJniObject::fromLocalRef(exception);
    throw Xcept("AudioTrack: {}", object.callObjectMethod("toString", "()Ljava/lang/String;").toString().toStdString());
}
class AndroidMediaOutput final : public AudioOutput {
    QJniObject track, array;
    jmethodID play{}, pause{}, flush{}, write{}, close{}, diagnostics{}, priority{};
    std::vector<float> scratch;
    void *context;
    const AudioPcmPull pull;
    size_t offset = 0, remaining = 0, prefilled = 0;
    size_t capacity = 0;
    bool primed = false;
    std::atomic<bool> quit{true};
    std::atomic<int> error{0};
    std::atomic<uint64_t> deferredPulls{0}, shortPulls{0}, pcmWaitNs{0}, pcmWaitMaxNs{0};
    std::atomic<uint64_t> nativeBackpressure{0}, acceptedFrames{0}, pullGapMaxNs{0};
    static uint64_t Elapsed(std::chrono::steady_clock::time_point at) noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - at).count());
    }
    static void Maximum(std::atomic<uint64_t> &value, uint64_t next) noexcept {
        auto old = value.load(std::memory_order_relaxed);
        while (next > old && !value.compare_exchange_weak(old, next, std::memory_order_relaxed)) {}
    }
    std::thread writer;
    std::string errorDetail;
    std::mutex control;
    std::condition_variable changed;
    void Call(QJniEnvironment &env, jmethodID method) {
        env->CallVoidMethod(track.object(), method);
        CheckJava(env);
    }
    void Worker() noexcept {
        try {
            QJniEnvironment env;
            Call(env, priority);
            auto progressed = std::chrono::steady_clock::now();
            auto pcmProgress = progressed;
            auto previousPull = std::chrono::steady_clock::time_point{};
            while (!quit.load(std::memory_order_acquire)) {
                if (!remaining) {
                    const auto wanted = primed ? scratch.size() / 2 : std::min(scratch.size() / 2, capacity - prefilled);
                    if (!wanted) throw Xcept("AudioTrack invalid prefill size");
                    // AudioTrack is a push sink, not a 5 ms device callback.
                    // Native mixer bursts and a late userspace timer must not
                    // turn unavailable PCM into silence on a catch-up read.
                    pull.ready->Arm();
                    const auto frames = pull.read(context, std::span<float>(scratch).first(wanted * 2));
                    if (!frames) {
                        deferredPulls.fetch_add(1, std::memory_order_relaxed);
                        if (std::chrono::steady_clock::now() - pcmProgress > std::chrono::seconds(3)) {
                            pull.ready->Disarm(); error.store(-1006); break;
                        }
                        const auto began = std::chrono::steady_clock::now();
                        pull.ready->Wait(quit, std::chrono::milliseconds(100));
                        const auto waited = Elapsed(began);
                        pcmWaitNs.fetch_add(waited, std::memory_order_relaxed);
                        Maximum(pcmWaitMaxNs, waited);
                        continue;
                    }
                    pull.ready->Disarm();
                    if (frames > wanted) { error.store(-1002); break; }
                    if (frames < wanted) shortPulls.fetch_add(1, std::memory_order_relaxed);
                    if (previousPull != std::chrono::steady_clock::time_point{}) Maximum(pullGapMaxNs, Elapsed(previousPull));
                    previousPull = pcmProgress = std::chrono::steady_clock::now();
                    env->SetFloatArrayRegion(array.object<jfloatArray>(), 0, static_cast<jsize>(frames * 2), scratch.data());
                    CheckJava(env);
                    offset = 0;
                    remaining = frames * 2;
                }
                const auto count = env->CallIntMethod(track.object(), write, array.object<jfloatArray>(),
                    static_cast<jint>(offset), static_cast<jint>(remaining));
                CheckJava(env);
                if (count < 0) { error.store(count); break; }
                if (!count) {
                    if (std::chrono::steady_clock::now() - progressed > std::chrono::seconds(3)) {
                        error.store(-1004); break; // native stream stopped making progress
                    }
                    nativeBackpressure.fetch_add(1, std::memory_order_relaxed);
                    // Backpressure is waited out on this dedicated worker,
                    // never in RenderAudio. Nonblocking writes avoid the race
                    // where pause precedes entry into a blocking native write.
                    std::unique_lock lock(control);
                    changed.wait_for(lock, std::chrono::milliseconds(1), [this] { return quit.load(); });
                    continue;
                }
                if (static_cast<size_t>(count) > remaining || (count & 1)) {
                    error.store(-1002); break; // reject non-frame-aligned / impossible writes
                }
                progressed = pcmProgress = std::chrono::steady_clock::now();
                acceptedFrames.fetch_add(static_cast<size_t>(count) / 2, std::memory_order_relaxed);
                offset += static_cast<size_t>(count);
                remaining -= static_cast<size_t>(count);
                if (!primed) {
                    prefilled += static_cast<size_t>(count) / 2;
                    if (prefilled == capacity) {
                        std::scoped_lock lock(control);
                        if (quit.load()) break;
                        Call(env, play);
                        primed = true;
                    }
                }
            }
        } catch (const std::exception &failure) { errorDetail = failure.what(); error.store(-1003); }
    }
public:
    AndroidMediaOutput(uint32_t rate, void *context, bool suspended, AudioPcmPull pull)
        : scratch(std::max<uint32_t>(1, rate / 200) * 2), context(context), pull(pull) {
        if (!pull.read || !pull.ready) throw Xcept("AudioTrack requires an unpadded PCM pull source");
        QJniEnvironment env;
        auto klass = env.findClass("org/agbplay/android/MediaAudioTrack");
        CheckJava(env);
        if (!klass) throw Xcept("AudioTrack class unavailable");
        auto method = [&](const char *name, const char *signature) {
            auto result = env->GetMethodID(klass, name, signature);
            CheckJava(env);
            if (!result) throw Xcept("AudioTrack method missing: {}", name);
            return result;
        };
        const auto ctor = method("<init>", "(I)V");
        play = method("play", "()V"); pause = method("pause", "()V"); flush = method("flush", "()V");
        write = method("write", "([FII)I"); close = method("close", "()V");
        priority = method("writerPriority", "()V"); diagnostics = method("diagnostics", "()Ljava/lang/String;");
        const auto bufferFrames = method("bufferFrames", "()I");
        track = QJniObject::fromLocalRef(env->NewObject(klass, ctor, static_cast<jint>(rate)));
        CheckJava(env);
        try {
            capacity = static_cast<size_t>(env->CallIntMethod(track.object(), bufferFrames));
            CheckJava(env);
            if (!capacity || capacity > rate * 2u) throw Xcept("AudioTrack invalid native buffer capacity");
            array = QJniObject::fromLocalRef(env->NewFloatArray(static_cast<jsize>(scratch.size())));
            CheckJava(env);
            if (!array.isValid()) throw Xcept("AudioTrack PCM array allocation failed");
            Debug::print("Audio route: requested=MediaCompatibility, actual=AudioTrack streaming float32 / {} Hz / stereo, gain=1; PCM pulls={} frames, native prefill={} frames; native-backpressure clock, producer-event wait, no zero padding", rate, scratch.size() / 2, capacity);
            PollDiagnostics();
            if (!suspended) Suspend(false);
        } catch (...) {
            env->CallVoidMethod(track.object(), close);
            if (env->ExceptionCheck()) env->ExceptionClear();
            throw;
        }
    }
    ~AndroidMediaOutput() override {
        try { Suspend(true); } catch (...) { if (writer.joinable()) writer.join(); }
        QJniEnvironment env;
        env->CallVoidMethod(track.object(), close);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    AudioOutputMode Mode() const override { return AudioOutputMode::MediaCompatibility; }
    void Suspend(bool value) override {
        QJniEnvironment env;
        if (value) {
            quit.store(true, std::memory_order_release);
            changed.notify_all();
            pull.ready->Signal();
            // Writes are nonblocking, so joining cannot wait for a paused
            // native buffer to drain. Retain a partial float block for resume.
            try { std::scoped_lock lock(control); Call(env, pause); }
            catch (const std::exception &failure) {
                Debug::print("AudioTrack pause failed: {}", failure.what());
                int noError = 0; error.compare_exchange_strong(noError, -1005);
            }
            if (writer.joinable()) writer.join();
        } else if (!writer.joinable()) {
            try {
                if (!primed && prefilled == capacity) primed = true;
                if (primed) Call(env, play);
                quit.store(false, std::memory_order_release);
                writer = std::thread([this] { Worker(); });
            } catch (const std::exception &failure) {
                quit.store(true);
                errorDetail = failure.what();
                error.store(-1003);
            }
        }
    }
    void DiscardPending() override {
        if (writer.joinable()) throw Xcept("AudioTrack flush requires suspended writer");
        QJniEnvironment env;
        Call(env, flush);
        remaining = offset = prefilled = 0;
        primed = false;
    }
    std::string TakeError() override {
        const int code = error.load();
        if (!code) return {};
        Suspend(true); // joins before reading the worker-owned error detail
        error.store(0);
        return "AudioTrack writer stopped (code " + std::to_string(code) + "): " + errorDetail;
    }
    void PollDiagnostics() override {
        QJniEnvironment env;
        auto value = QJniObject::fromLocalRef(env->CallObjectMethod(track.object(), diagnostics));
        CheckJava(env);
        Debug::print("{}", value.toString().toStdString());
        Debug::print("AudioTrack push (interval): deferred PCM pulls={}, short PCM pulls={}, PCM wait total/max={:.3f}/{:.3f} ms, max pull gap={:.3f} ms, native backpressure writes={}, accepted frames={}; empty FIFO waits do not insert silence, native underrunCount is reported separately",
            deferredPulls.exchange(0), shortPulls.exchange(0), double(pcmWaitNs.exchange(0)) / 1e6,
            double(pcmWaitMaxNs.exchange(0)) / 1e6, double(pullGapMaxNs.exchange(0)) / 1e6,
            nativeBackpressure.exchange(0), acceptedFrames.exchange(0));
    }
};
}
std::unique_ptr<AudioOutput> OpenAndroidMediaOutput(uint32_t rate, void *context, bool suspended, AudioPcmPull pull) {
    return std::make_unique<AndroidMediaOutput>(rate, context, suspended, pull);
}
