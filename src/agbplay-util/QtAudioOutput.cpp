#include "AudioOutput.hpp"
#include "Xcept.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <QAudioSink>
#include <QMediaDevices>
#include <QIODevice>
#include "Debug.hpp"

#ifdef __ANDROID__
std::unique_ptr<AudioOutput> OpenAndroidMediaOutput(uint32_t, void *, bool, AudioPcmPull);
#endif

namespace {
class QtAudioOutput final : public AudioOutput
{
    // Compatibility path for desktop Qt 6.8-6.10. Android requires 6.11 callback API.
    class Source final : public QIODevice {
    public:
        Source(void *context, Render render) : context(context), render(render) { open(ReadOnly); }
        bool isSequential() const override { return true; }
        qint64 bytesAvailable() const override { return 65536 + QIODevice::bytesAvailable(); }
    protected:
        qint64 readData(char *data, qint64 bytes) override {
            // QIODevice buffers need not be float-aligned. Use fixed, aligned scratch.
            const auto frames = std::min<qint64>(bytes / 8, 4096);
            render(context, {scratch.data(), static_cast<size_t>(frames) * 2});
            std::memcpy(data, scratch.data(), static_cast<size_t>(frames) * 8);
            return frames * 8;
        }
        qint64 writeData(const char *, qint64) override { return -1; }
    private:
        void *context;
        Render render;
        std::array<float, 8192> scratch{};
    } source;
    std::unique_ptr<QAudioSink> sink;
#ifdef __ANDROID__
    QMediaDevices devices;
#endif
    void *context;
    Render render;
    bool started = false;
    QAudio::Error reportedError = QAudio::NoError;
public:
    QtAudioOutput(uint32_t rate, void *context, Render render, bool suspended) : source(context, render), context(context), render(render) {
        QAudioFormat format;
        format.setSampleRate(static_cast<int>(rate));
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Float);
        const auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(format))
            throw Xcept("Audio device does not support {} Hz float stereo; choose a supported sample rate", rate);
        sink = std::make_unique<QAudioSink>(device, format);
#if QT_VERSION < QT_VERSION_CHECK(6, 11, 0)
        sink->setBufferSize(static_cast<qsizetype>(rate / 50) * 8); // 20 ms request; backend may adjust
#endif
#ifdef __ANDROID__
        QObject::connect(&devices, &QMediaDevices::audioOutputsChanged, &devices, [] {
            const auto selected = QMediaDevices::defaultAudioOutput();
            Debug::print("Qt device list changed: default id={}, name={}; native AAudio route not publicly exposed", selected.id().toStdString(), selected.description().toStdString());
        });
#endif
        if (!suspended) Start();
        Debug::print("Qt audio output: device='{}', requested={} Hz / 2 channels / float32, "
                     "QAudioSink format={} Hz / {} channels / format={}, Qt ring-buffer report={} bytes (not the native callback buffer), volume={:.3f}",
                     device.description().toStdString(), rate, sink->format().sampleRate(),
                     sink->format().channelCount(), static_cast<int>(sink->format().sampleFormat()),
                     sink->bufferSize(), sink->volume());
    }
    void Start() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
        sink->start([context = context, render = render](QSpan<float> samples) noexcept {
            render(context, {samples.data(), static_cast<size_t>(samples.size())});
        });
#else
        sink->start(&source);
#endif
        if (sink->error() != QAudio::NoError)
            throw Xcept("Qt could not start audio output (error {})", static_cast<int>(sink->error()));
        started = true;
    }
    ~QtAudioOutput() override {
        sink->stop();
    }
    void PollDiagnostics() override {
        Debug::print("Qt output telemetry: state={}, error={}, processed={} us; QAudioSink does not expose native AAudio xrun/session/MMAP counters",
            int(sink->state()), int(sink->error()), sink->processedUSecs());
    }
    std::string TakeError() override {
        const auto error = sink->error();
        if (error == QAudio::NoError) { reportedError = error; return {}; }
        if (error == reportedError) return {};
        reportedError = error;
        return "Audio device stopped (Qt error " + std::to_string(int(error)) + "). Reopen the ROM to retry audio output.";
    }
    void Suspend(bool suspended) override {
        // Android focus/lifecycle is owned by PlaybackService. The backend only
        // transports PCM and obeys explicit, primed suspend/resume commands.
        if (suspended) { if (started) sink->suspend(); }
        else if (!started) Start();
        else sink->resume();
    }
};
}
std::unique_ptr<AudioOutput> AudioOutput::Open(uint32_t rate, void *context, Render render, AudioOutputMode mode, bool suspended, AudioPcmPull pull)
{
#ifdef __ANDROID__
    if (mode == AudioOutputMode::MediaCompatibility) {
        try { return OpenAndroidMediaOutput(rate, context, suspended, pull); }
        catch (const std::exception &error) {
            Debug::print("Audio route: requested=AudioTrack, initialization failed: {}; fallback=Qt/AAudio", error.what());
        }
    }
    Debug::print("Audio route: requested={}, actual=Qt {} QAudioSink/AAudio, MEDIA/MUSIC defaults, shared, low-latency requested; native session/MMAP/performance not exposed by QAudioSink",
        mode == AudioOutputMode::Automatic ? "Automatic" : "MediaCompatibility", QT_VERSION_STR);
#else
    (void)mode; (void)pull;
#endif
    return std::make_unique<QtAudioOutput>(rate, context, render, suspended);
}
