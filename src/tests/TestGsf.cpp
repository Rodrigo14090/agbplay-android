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
#include <cstdlib>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
template<class F> void Reject(F f) { bool failed = false; try { f(); } catch (const std::exception &) { failed = true; } CHECK(failed); }
int main() {
    const auto dir = std::filesystem::temp_directory_path() / "agbplay-gsf-regression";
    std::filesystem::create_directories(dir);
#ifndef _WIN32
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
#endif
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::filesystem::remove_all(p); } } cleanup{dir};
    auto save = [&](const auto &name, const auto &data) {
        const auto path = dir / name;
        std::filesystem::create_directories(path.parent_path());
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
    configured->songTableInfoConfig.count = 1; // manual tables need not satisfy the auto-scan minimum of four songs
    MP2KScanner(rom).Scan(configured);
    CHECK(configured->songTableInfoScanned.count == 1);
    configured->songTableInfoConfig = profile.songTableInfoPlayback;
    const auto original = profile.playerTablePlayback;
    profile.playerTablePlayback[1].maxTracks = 0;
    profile.playerTablePlayback[0].maxTracks = 1; // not an all-zero table: song targets an inactive slot
    Reject(render);
    profile.playerTablePlayback = original;
    profile.playerTablePlayback[1].maxTracks = 17;
    Reject(render);
    profile.playerTablePlayback = original;
    render();
    Reject([&] { Gsf::ReadPlaylist({a}, "other.gsflib", 4); });
    CHECK(Gsf::ReadPlaylist({a}, "other.gsflib", 4, [] { return false; }, true).empty());
    CHECK(Gsf::ReadPlaylist({b, a}, rom.GsfPath(), 4, [] { return false; }, true).size() == 2);
    Reject([&] { Gsf::ReadPlaylist({b}, rom.GsfPath(), 1); });
    Reject([&] { Gsf::ReadPlaylist({a}, rom.GsfPath(), 4, [] { return true; }); });
    const auto copiedA = save("zz-import/01.minigsf", Fixture::Psf({0}, "title=First\n_lib=fixture.gsflib\n"));
    const auto copiedB = save("aa-import/02.minigsf", Fixture::Psf({1}, "title=Second\n_lib=fixture.gsflib\n"));
    CHECK(Gsf::ReadPlaylist({copiedB, copiedA}, rom.GsfPath(), 4)[0].title == "First");
    const auto wide = Gsf::ReadSong(Fixture::Psf({0x34, 0x12}, "TITLE=Canción ♫\r\n_LIB=fixture.gsflib\r\n"));
    CHECK(wide.id == 0x1234 && wide.title == "Canción ♫" && wide.library == "fixture.gsflib");
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
    auto reserved = lib;
    reserved.insert(reserved.begin() + 16, {1, 2, 3});
    Fixture::Put32(reserved, 4, 3);
    CHECK(Gsf::GetRomData(reserved, reconstructed) && reconstructed == bytes);
    auto gba = Rom::LoadFromFile(save("regular.gba", Fixture::ScannableRom(false)));
    const auto gbaScan = MP2KScanner(gba).Scan(nullptr);
    CHECK(gbaScan.size() == 1 && gbaScan[0].playerTableInfo[1].maxTracks == 16);
#ifndef _WIN32
    // Persist a manual player table, including inactive slots and priority.
    configured->gameMatch.gameCodes = {"TEST"};
    configured->path = dir / "agbplay/profiles/manual.json";
    configured->playerTableConfig[1].usePriority = 1;
    configured->dirty = true;
    std::filesystem::create_directories(configured->path.parent_path());
    manager.GetAllProfiles().clear();
    manager.GetAllProfiles().push_back(configured);
    manager.SaveProfiles();
    std::ofstream(dir / "agbplay/profiles/bad.json") << R"({"gameMatch":{"gameCodes":["TEST"]},"playerTable":[{"maxTracks":256}]})";
    ProfileManager restored;
    restored.LoadProfiles();
    CHECK(restored.GetAllProfiles().size() == 1);
    const auto restoredProfiles = restored.GetProfiles(rom);
    CHECK(restoredProfiles.size() == 1);
    CHECK(restoredProfiles[0]->playerTablePlayback[0].maxTracks == 0);
    CHECK(restoredProfiles[0]->playerTablePlayback[1].usePriority == 1);
#endif
    std::cout << "GSFLIB scan/profile/PCM, inactive players, MINIGSF associations and ZIP passed\n";
}
