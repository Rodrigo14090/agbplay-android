#include "SyntheticGsf.hpp"
#include "Gsf.hpp"
#include "Rom.hpp"
#include "ProfileManager.hpp"
#include "MP2KContext.hpp"
#include "MP2KScanner.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <zip.h>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
template<class F> void Reject(F f) { bool failed = false; try { f(); } catch (const std::exception &) { failed = true; } CHECK(failed); }
int main() {
    const auto dir = std::filesystem::temp_directory_path() / "agbplay-gsf-regression";
    std::filesystem::create_directories(dir);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{dir};
    auto save = [&](const auto &name, const auto &data) {
        const auto path = dir / name;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(data.data()), data.size());
        return path;
    };
    const auto bytes = Fixture::ScannableRom();
    const auto lib = Fixture::Psf(bytes);
    const auto path = save("fixture.gsflib", lib);
    auto rom = Rom::LoadFromFile(path);
    CHECK(rom.Size() == bytes.size());
    CHECK(rom.ReadU8(0x1014) == 16 && rom.ReadU8(0x1008) == 0);
    ProfileManager manager;
    auto profiles = manager.GetProfiles(rom);
    CHECK(profiles.size() == 1);
    auto &profile = *profiles[0];
    CHECK(profile.playerTableScanned[0].maxTracks == 0);
    CHECK(profile.playerTablePlayback[1].maxTracks == 16);
    auto render = [&] {
        MP2KContext ctx(48000, 1, rom, profile.mp2kSoundModePlayback, profile.agbplaySoundMode,
                        profile.songTableInfoPlayback, profile.playerTablePlayback);
        ctx.m4aSongNumStart(0);
        double energy = 0;
        for (int i = 0; i < 100; ++i) {
            ctx.m4aSoundMain();
            for (auto s : ctx.masterAudioBuffer) { CHECK(std::isfinite(s.left)); energy += s.left * s.left; }
        }
        CHECK(energy > 0.01);
    };
    // A playlist does not participate in context initialization (Yes and No).
    render();
    const auto a = save("01.minigsf", Fixture::Psf({0}, "title=Tema uno\r\n_lib=fixture.gsflib\r\n", 0x08000700));
    const auto b = save("02.minigsf", Fixture::Psf({1, 0}, "title=Tema dos\n_lib=fixture.gsflib\n", 0x08000700));
    const auto songs = Gsf::ReadPlaylist({b, a}, rom.GsfPath(), profile.songTableInfoPlayback.count);
    CHECK(songs.size() == 2 && songs[0].id == 0 && songs[1].id == 1);
    CHECK(songs[0].title == "Tema uno" && songs[1].title == "Tema dos");
    for (auto &song : songs) profile.playlist.push_back({song.title, song.id});
    render();
    auto configured = std::make_shared<Profile>();
    configured->songTableInfoConfig = profile.songTableInfoPlayback;
    configured->playerTableConfig = profile.playerTablePlayback;
    configured->mp2kSoundModeConfig = profile.mp2kSoundModePlayback;
    // Exercise the scanner's explicit-profile branch, not only automatic scans.
    MP2KScanner(rom).Scan(configured);
    CHECK(configured->ScanOk());
    configured->ApplyScanToPlayback();
    CHECK(configured->playerTablePlayback[1].maxTracks == 16);
    const auto original = profile.playerTablePlayback;
    profile.playerTablePlayback[1].maxTracks = 0;
    Reject(render);
    profile.playerTablePlayback = original;
    profile.playerTablePlayback[1].maxTracks = 17;
    Reject(render);
    profile.playerTablePlayback = original;
    render();
    Reject([&] { Gsf::ReadPlaylist({a}, "other.gsflib", 4); });
    Reject([&] { Gsf::ReadPlaylist({b}, rom.GsfPath(), 1); });
    Reject([&] { Gsf::ReadPlaylist({a}, rom.GsfPath(), 4, [] { return true; }); });
    int error = 0;
    auto *zip = zip_open((dir / "set.zip").c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    CHECK(zip);
    const auto mini = Fixture::Psf({0}, "title=ZIP song\n_lib=fixture.gsflib\n", 0x08000700);
    CHECK(zip_file_add(zip, "set/fixture.gsflib", zip_source_buffer(zip, lib.data(), lib.size(), 0), 0) >= 0);
    CHECK(zip_file_add(zip, "set/01.minigsf", zip_source_buffer(zip, mini.data(), mini.size(), 0), 0) >= 0);
    CHECK(zip_close(zip) == 0);
    auto zipped = Rom::LoadFromFile(dir / "set.zip");
    CHECK(Gsf::ReadPlaylist({dir / "set.zip"}, zipped.GsfPath(), 4).size() == 1);
    std::vector<uint8_t> reconstructed;
    CHECK(Gsf::GetRomData(Fixture::Psf({1, 2}, {}, 0x08000200), reconstructed));
    CHECK(reconstructed.size() == 0x202 && reconstructed[0x200] == 1);
    Reject([&] { Gsf::GetRomData(Fixture::Psf({1}, {}, 0x02000000), reconstructed); });
    auto corrupt = lib; corrupt[16] ^= 1;
    Reject([&] { Gsf::GetRomData(corrupt, reconstructed); });
    std::cout << "GSFLIB scan/profile/PCM, inactive players, MINIGSF associations and ZIP passed\n";
}
