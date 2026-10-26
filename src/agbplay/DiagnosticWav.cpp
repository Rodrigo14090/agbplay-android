#include "DiagnosticWav.hpp"
#include "Xcept.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <cmath>

struct DiagnosticWav::Impl {
    SNDFILE *file = nullptr;
    ~Impl() { if (file) sf_close(file); }
};
DiagnosticWav::DiagnosticWav(const std::filesystem::path &path, uint32_t expectedRate) : impl(std::make_unique<Impl>()) {
    SF_INFO info{};
#ifdef _WIN32
    impl->file = sf_wchar_open(path.wstring().c_str(), SFM_READ, &info);
#else
    impl->file = sf_open(path.string().c_str(), SFM_READ, &info);
#endif
    if (!impl->file) throw Xcept("Cannot open diagnostic WAV: {}", sf_strerror(nullptr));
    const int type = info.format & SF_FORMAT_TYPEMASK, subtype = info.format & SF_FORMAT_SUBMASK;
    if ((type != SF_FORMAT_WAV && type != SF_FORMAT_WAVEX) ||
        (subtype != SF_FORMAT_FLOAT && subtype != SF_FORMAT_PCM_16 && subtype != SF_FORMAT_PCM_24 && subtype != SF_FORMAT_PCM_32))
        throw Xcept("Use an uncompressed WAV: float32 or PCM16/24/32");
    if (info.channels != 2 || info.samplerate != static_cast<int>(expectedRate) || info.frames <= 0)
        throw Xcept("Diagnostic WAV must be stereo at {} Hz (file: {} channels, {} Hz). No resampling or downmix is applied", expectedRate, info.channels, info.samplerate);
}
DiagnosticWav::~DiagnosticWav() = default;
size_t DiagnosticWav::Read(std::span<sample> frames) {
    // Avoid assumptions about struct padding/aliasing; sf_readf_float preserves
    // float WAV samples and normalizes integer PCM according to its bit depth.
    std::array<float, 2048> pcm;
    size_t done = 0;
    while (done < frames.size()) {
        const auto wanted = std::min(frames.size() - done, pcm.size() / 2);
        const auto count = sf_readf_float(impl->file, pcm.data(), static_cast<sf_count_t>(wanted));
        if (count < 0 || sf_error(impl->file)) throw Xcept("Diagnostic WAV read failed: {}", sf_strerror(impl->file));
        for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
            if (!std::isfinite(pcm[i * 2]) || !std::isfinite(pcm[i * 2 + 1]))
                throw Xcept("Diagnostic WAV contains nonfinite PCM");
            frames[done + i] = {pcm[i * 2], pcm[i * 2 + 1]};
        }
        done += static_cast<size_t>(count);
        if (static_cast<size_t>(count) != wanted) break;
    }
    std::fill(frames.begin() + static_cast<ptrdiff_t>(done), frames.end(), sample{});
    return done;
}
void DiagnosticWav::Rewind() {
    if (sf_seek(impl->file, 0, SEEK_SET) < 0) throw Xcept("Cannot rewind diagnostic WAV");
}
