#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <functional>

namespace Gsf
{
    bool GetRomData(std::span<const uint8_t> gsfData, std::vector<uint8_t> &resultRomData);
    void GetSongInfo(std::span<const uint8_t> gsfData, std::string &name, uint16_t &id);
    struct Song {
        std::filesystem::path filename;
        std::string title;
        uint16_t id = 0;
        std::string library;
    };
    Song ReadSong(std::span<const uint8_t> gsfData);
    std::vector<Song> ReadPlaylist(const std::vector<std::filesystem::path> &paths,
                                 const std::filesystem::path &library, uint16_t songCount,
                                 const std::function<bool()> &cancelled = [] { return false; },
                                 bool matchingLibraryOnly = false);
    std::string GuessGameCodeFromPath(const std::filesystem::path &p);
}    // namespace Gsf
