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
    Render render;
    const uint32_t rate;
    size_t offset = 0, remaining = 0, prefilled = 0;
    size_t capacity = 0;
    bool primed = false;
    std::atomic<bool> quit{true};
    std::atomic<int> error{0};
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
            auto next = std::chrono::steady_clock::now();
            auto progressed = next;
            while (!quit.load(std::memory_order_acquire)) {
                if (!remaining) {
                    // Pace reads even while pre-filling: AudioTrack may accept a
                    // large native buffer instantly, unlike an AAudio callback.
                    // Never greedily drain the unchanged 30 ms synthesis FIFO.
                    std::unique_lock lock(control);
                    changed.wait_until(lock, next, [this] { return quit.load(); });
                    if (quit.load()) break;
                    lock.unlock();
                    const auto frames = primed ? scratch.size() / 2 : std::min(scratch.size() / 2, capacity - prefilled);
                    if (!frames) throw Xcept("AudioTrack invalid prefill size");
                    render(context, std::span<float>(scratch).first(frames * 2));
                    env->SetFloatArrayRegion(array.object<jfloatArray>(), 0, static_cast<jsize>(frames * 2), scratch.data());
                    CheckJava(env);
                    offset = 0;
                    remaining = frames * 2;
                    next += std::chrono::nanoseconds(static_cast<int64_t>(frames * uint64_t{1000000000} / rate));
                }
                const auto count = env->CallIntMethod(track.object(), write, array.object<jfloatArray>(),
                    static_cast<jint>(offset), static_cast<jint>(remaining));
                CheckJava(env);
                if (count < 0) { error.store(count); break; }
                if (!count) {
                    if (std::chrono::steady_clock::now() - progressed > std::chrono::seconds(3)) {
                        error.store(-1004); break; // native stream stopped making progress
                    }
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
                progressed = std::chrono::steady_clock::now();
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
    AndroidMediaOutput(uint32_t rate, void *context, Render render, bool suspended)
        : scratch(std::max<uint32_t>(1, rate / 200) * 2), context(context), render(render), rate(rate) {
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
            Debug::print("Audio route: requested=MediaCompatibility, actual=AudioTrack streaming float32 / {} Hz / stereo, gain=1; PCM pulls={} frames, native prefill={} frames", rate, scratch.size() / 2, capacity);
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
    }
};
}
std::unique_ptr<AudioOutput> OpenAndroidMediaOutput(uint32_t rate, void *context, AudioOutput::Render render, bool suspended) {
    return std::make_unique<AndroidMediaOutput>(rate, context, render, suspended);
}
