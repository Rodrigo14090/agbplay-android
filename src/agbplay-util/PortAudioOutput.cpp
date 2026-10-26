#include "AudioOutput.hpp"
#include "Debug.hpp"
#include "Xcept.hpp"
#include <algorithm>
#include <cassert>
#include <vector>
#include <bit>
#include <cmath>
#include <cstring>
#include <portaudiocpp/PortAudioCpp.hxx>
#if __has_include(<pa_win_wasapi.h>)
#include <pa_win_wasapi.h>
#endif
#if __has_include(<pa_jack.h>)
#include <pa_jack.h>
namespace { struct JackNameSetter { JackNameSetter() { PaJack_SetClientName("agbplay"); } } jackNameSetter; }
#endif
namespace {
class PortAudioOutput final : public AudioOutput
{
public:
    PortAudioOutput(uint32_t sampleRate, void *context, Render render);
    ~PortAudioOutput() override;
    void Suspend(bool suspended) override {
        if (suspended && audioStream.isActive()) audioStream.stop();
        else if (!suspended && !audioStream.isActive()) audioStream.start();
    }
private:
    static int callback(const void *, void *out, unsigned long frames,
                        const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *user) noexcept {
        auto &self = *static_cast<PortAudioOutput *>(user);
        self.render(self.context, {static_cast<float *>(out), frames * 2});
        return paContinue;
    }
    static const std::vector<PaHostApiTypeId> hostApiPriority;
    portaudio::FunCallbackStream audioStream;
    void *context;
    Render render;
};
const std::vector<PaHostApiTypeId> PortAudioOutput::hostApiPriority = {
    // Unix
    paALSA,
    paJACK,
    // Windows
    paWASAPI,
    paMME,
    // Mac OS
    paCoreAudio,
    paSoundManager,
};

PortAudioOutput::PortAudioOutput(uint32_t sampleRate, void *context, Render render)
    : context(context), render(render)
{
    auto &sys = portaudio::System::instance();

    // init host api
    std::vector<PaHostApiTypeId> hostApiPrioritiesWithFallback = hostApiPriority;
    const auto &defaultHostApi = sys.defaultHostApi();
    const auto f =
        std::find(hostApiPrioritiesWithFallback.begin(), hostApiPrioritiesWithFallback.end(), defaultHostApi.typeId());
    if (f == hostApiPrioritiesWithFallback.end())
        hostApiPrioritiesWithFallback.push_back(defaultHostApi.typeId());

    bool streamOpen = false;

    /* Loop over all wanted host APIs in the prioritized order and use the first one,
     * which can successfully open a stream with the default device. */
    for (const auto apiType : hostApiPrioritiesWithFallback) {
        const PaHostApiIndex hostApiIndex = Pa_HostApiTypeIdToHostApiIndex(apiType);
        if (hostApiIndex < 0)
            continue;

        const auto &currentHostApi = sys.hostApiByIndex(hostApiIndex);

        std::shared_ptr<void> hostApiSpecificStreamInfo;

#if __has_include(<pa_win_wasapi.h>)
        if (apiType == paWASAPI) {
            PaWasapiStreamInfo info;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            info.hostApiType = paWASAPI;
            info.version = 1;
            info.flags = paWinWasapiAutoConvert;
            hostApiSpecificStreamInfo = std::make_shared<PaWasapiStreamInfo>(info);
        }
#endif

        auto &outputDevice = currentHostApi.defaultOutputDevice();

        // Manually set the latency for ALSA on Linux
        // See: https://github.com/ipatix/agbplay/issues/93
        // PortAudio will unfortunately suggest a latency value, which
        // is a very odd number (e.g. 8.7ms on developer machine).
        // PipeWire takes those latency values very literally.
        // With 48 kHz samplerate this causes a period size of 104 samples,
        // and that causes unstable output. Round this up to the closest power of two instead.
        double latency = outputDevice.defaultLowOutputLatency();
        if (apiType == paALSA) {
            long framesPerPeriodSigned = lround(latency * static_cast<double>(sampleRate));
            assert(framesPerPeriodSigned >= 0);
            const unsigned long framesPerPeriod = std::bit_ceil(static_cast<unsigned long>(framesPerPeriodSigned));
            if (framesPerPeriod > 0)
                latency = static_cast<double>(framesPerPeriod) / static_cast<double>(sampleRate);
        }

        portaudio::DirectionSpecificStreamParameters outPars(
            outputDevice,
            2,    // stereo
            portaudio::SampleDataFormat::FLOAT32,
            true,
            latency,
            hostApiSpecificStreamInfo.get()
        );

        portaudio::StreamParameters pars(
            portaudio::DirectionSpecificStreamParameters::null(),
            outPars,
            sampleRate,
            paFramesPerBufferUnspecified,
            paNoFlag
        );

        try {
            audioStream.open(pars, callback, this);

            audioStream.start();
        } catch (portaudio::Exception &e) {
            Debug::print(
                "unable to open/start stream on device <{}> with Host API <{}>: {}",
                outputDevice.name(),
                currentHostApi.name(),
                e.what()
            );
            continue;
        }

        streamOpen = true;
        break;
    }

    if (!streamOpen)
        throw Xcept("Unable to initialize sound output: Host API could not be initialized");
}

PortAudioOutput::~PortAudioOutput()
{
    try {
        audioStream.stop();
        audioStream.close();
    } catch (portaudio::Exception &e) {
        Debug::print("Error while stopping/closing portaudio stream: {}", e.what());
    }
}

} // namespace
std::unique_ptr<AudioOutput> AudioOutput::Open(uint32_t rate, void *context, Render render, AudioOutputMode, bool suspended, AudioPcmPull)
{
    auto result = std::make_unique<PortAudioOutput>(rate, context, render);
    if (suspended) result->Suspend(true);
    return result;
}
