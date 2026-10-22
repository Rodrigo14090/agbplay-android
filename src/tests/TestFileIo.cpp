#include "SyntheticRom.hpp"
#include "Gsf.hpp"
#include "Rom.hpp"
#include "Settings.hpp"
#include "SoundExporter.hpp"
#include <fstream>
#include <iostream>
#include <sndfile.h>
#include <zip.h>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main() {
    const auto dir = std::filesystem::temp_directory_path() / "agbplay-fileio-test";
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
        found = true;
    }
    CHECK(found);
    std::cout << "50 ZIP load/free cycles and float stereo WAV export passed\n";
}
