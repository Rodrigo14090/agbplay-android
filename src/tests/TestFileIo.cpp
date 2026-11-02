#include "SyntheticRom.hpp"
#include "Gsf.hpp"
#include "Rom.hpp"
#include "Settings.hpp"
#include "SoundExporter.hpp"
#include "DiagnosticWav.hpp"
#include "MP2KContext.hpp"
#include <cmath>
#include <limits>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sndfile.h>
#include <zip.h>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main() {
    std::cout << std::unitbuf;
    const auto dir = std::filesystem::temp_directory_path() / ("agbplay-fileio-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{dir};
    auto bytes = Fixture::RomBytes();
    const auto zipPath = dir / "fixture.zip";
    int error = 0;
    auto *archive = zip_open(zipPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    CHECK(archive);
    auto *source = zip_source_buffer(archive, bytes.data(), bytes.size(), 0);
    CHECK(source);
    CHECK(zip_file_add(archive, "fixture.gba", source, 0) >= 0);
    CHECK(zip_close(archive) == 0);
    for (int n = 0; n < 50; ++n) {
        auto rom = Rom::LoadFromFile(zipPath);
        CHECK(rom.Size() == bytes.size());
        CHECK(rom.GetROMCode() == "TEST");
    }
    Rom::CreateInstance(zipPath);
    auto profile = Fixture::PlaybackProfile();
    profile.playlist.push_back({"Synthetic PCM", 0});
    Settings settings;
    settings.exportSampleRate = 48000;
    settings.exportBitDepth = 32;
    settings.exportMaxLoops = 1;
    settings.exportPadStart = 0;
    settings.exportPadEnd = 0;
    SoundExporter exporter(dir, {}, settings, profile, false, false);
    exporter.Export();
    bool found = false;
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".wav") continue;
        SF_INFO info{};
        auto *file = sf_open(entry.path().c_str(), SFM_READ, &info);
        CHECK(file);
        CHECK(info.channels == 2 && info.samplerate == 48000 && info.frames > 0);
        CHECK((info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT);
        sf_close(file);
        DiagnosticWav wav(entry.path(), 48000);
        MP2KContext reference(48000, 1, Rom::Instance(), profile.mp2kSoundModePlayback,
            profile.agbplaySoundMode, profile.songTableInfoPlayback, profile.playerTablePlayback);
        reference.m4aSongNumStart(0);
        std::vector<sample> actual(reference.mixer.GetSamplesPerBuffer());
        double squared = 0, difference = 0;
        float peak = 0;
        for (int block = 0; block < 16; ++block) {
            reference.m4aSoundMain();
            CHECK(wav.Read(actual) == actual.size());
            for (size_t frame = 0; frame < actual.size(); ++frame) {
                CHECK(actual[frame].left == reference.masterAudioBuffer[frame].left);
                CHECK(actual[frame].right == reference.masterAudioBuffer[frame].right);
                for (float value : {actual[frame].left, actual[frame].right}) {
                    squared += double(value) * value;
                    peak = std::max(peak, std::abs(value));
                }
            }
        }
        std::cout << "Export/reference PCM: RMS=" << std::sqrt(squared / (16 * actual.size() * 2))
                  << " peak=" << peak << " max difference=" << difference << " (bit exact float samples)\n";
        bool rejected = false;
        try { DiagnosticWav wrongRate(entry.path(), 44100); } catch (const std::exception &) { rejected = true; }
        CHECK(rejected);
        found = true;
    }
    CHECK(found);
    for (int encoding : {SF_FORMAT_FLOAT, SF_FORMAT_PCM_16, SF_FORMAT_PCM_24, SF_FORMAT_PCM_32}) {
        const auto filePath = dir / (std::to_string(encoding) + ".wav");
        SF_INFO info{}; info.format = SF_FORMAT_WAV | encoding; info.samplerate = 48000; info.channels = 2;
        auto *file = sf_open(filePath.c_str(), SFM_WRITE, &info); CHECK(file);
        const std::array<float, 6> values{0.25f, -0.5f, 0.125f, 0.75f, -0.875f, 0.0625f};
        CHECK(sf_writef_float(file, values.data(), 3) == 3); CHECK(sf_close(file) == 0);
        DiagnosticWav wav(filePath, 48000);
        std::array<sample, 5> pcm{};
        CHECK(wav.Read(pcm) == 3);
        const int bits = encoding == SF_FORMAT_PCM_16 ? 16 : encoding == SF_FORMAT_PCM_24 ? 24 : 32;
        const float tolerance = encoding == SF_FORMAT_FLOAT ? 0.0f : std::ldexp(1.0f, 1 - bits);
        for (size_t i = 0; i < 3; ++i) {
            CHECK(std::abs(pcm[i].left - values[i*2]) <= tolerance);
            CHECK(std::abs(pcm[i].right - values[i*2+1]) <= tolerance);
        }
        CHECK(pcm[3].left == 0 && pcm[4].right == 0);
        wav.Rewind(); CHECK(wav.Read(pcm) == 3);
    }
    const auto invalid = dir / "invalid.wav";
    std::ofstream(invalid) << "RIFF invalid";
    bool rejected = false;
    try { DiagnosticWav bad(invalid, 48000); } catch (const std::exception &) { rejected = true; }
    CHECK(rejected);
    std::cout << "50 ZIP load/free cycles and float stereo WAV export passed\n";
}
