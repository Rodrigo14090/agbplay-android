#include "SyntheticGsf.hpp"
#include "Rom.hpp"
#include "ProfileManager.hpp"
#include "MP2KContext.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const auto data = Fixture::Psf(Fixture::ScannableRom());
    const std::filesystem::path file(argv[1]);
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), data.size());
    auto rom = Rom::LoadFromFile(file);
    ProfileManager manager;
    auto profiles = manager.GetProfiles(rom);
    const auto &p = *profiles.at(0);
    std::cout << "players=" << p.playerTablePlayback.size() << " player0.tracks=" << int(p.playerTablePlayback[0].maxTracks)
              << " player1.tracks=" << int(p.playerTablePlayback[1].maxTracks) << '\n';
    MP2KContext ctx(48000, 1, rom, p.mp2kSoundModePlayback, p.agbplaySoundMode,
                    p.songTableInfoPlayback, p.playerTablePlayback);
    ctx.m4aSongNumStart(0);
    double energy = 0;
    for (int i = 0; i < 100; ++i) {
        ctx.m4aSoundMain();
        for (auto s : ctx.masterAudioBuffer) energy += s.left * s.left;
    }
    std::cout << "rendered energy=" << energy << '\n';
    std::filesystem::remove(file);
    return energy > .01 ? 0 : 1;
}
