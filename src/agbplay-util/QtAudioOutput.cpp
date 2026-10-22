#include "AudioOutput.hpp"
#include "Xcept.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <QAudioSink>
#include <QMediaDevices>
#include <QIODevice>

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
public:
    QtAudioOutput(uint32_t rate, void *context, Render render) : source(context, render) {
        QAudioFormat format;
        format.setSampleRate(static_cast<int>(rate));
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Float);
        const auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(format))
            throw Xcept("Audio device does not support {} Hz float stereo; choose a supported sample rate", rate);
        sink = std::make_unique<QAudioSink>(device, format);
        sink->setBufferSize(static_cast<qsizetype>(rate / 50) * 8); // 20 ms request; backend may adjust
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
        sink->start([context, render](QSpan<float> samples) noexcept {
            render(context, {samples.data(), static_cast<size_t>(samples.size())});
        });
#else
        sink->start(&source);
#endif
        if (sink->error() != QtAudio::NoError)
            throw Xcept("Qt could not start audio output (error {})", static_cast<int>(sink->error()));
    }
    ~QtAudioOutput() override { sink->stop(); }
    void Suspend(bool suspended) override { if (suspended) sink->suspend(); else sink->resume(); }
};
}
std::unique_ptr<AudioOutput> AudioOutput::Open(uint32_t rate, void *context, Render render)
{
    return std::make_unique<QtAudioOutput>(rate, context, render);
}
