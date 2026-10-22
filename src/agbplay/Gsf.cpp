#include "Gsf.hpp"

#include "Xcept.hpp"
#include "FileReader.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <memory>
#include <regex>
#include <sstream>
#include <zlib.h>

static uint32_t U32(std::span<const uint8_t> data, size_t pos)
{
    return uint32_t(data[pos]) | (uint32_t(data[pos + 1]) << 8) |
           (uint32_t(data[pos + 2]) << 16) | (uint32_t(data[pos + 3]) << 24);
}

static void Decompress(std::span<const uint8_t> compressedData, std::vector<uint8_t> &decompressedData)
{
    int error = Z_OK;
    const size_t CHUNK_SIZE = 4096;
    decompressedData.clear();

    z_stream strm{};
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<uint8_t *>(compressedData.data()));
    strm.avail_in = static_cast<unsigned int>(compressedData.size());

    if (error = inflateInit(&strm); error != Z_OK)
        throw Xcept("inflateInit() failed: {}", zError(error));

    /* abuse std::unique_ptr for RAII management of stack variable strm */
    auto streamDel = [](z_stream *strm) { inflateEnd(strm); };
    std::unique_ptr<z_stream, decltype(streamDel)> strmRaii(&strm, streamDel);

    while (true) {
        if (decompressedData.size() >= 32 * 1024 * 1024 + 4096)
            throw Xcept("GSF decompression exceeds GBA ROM size limit");
        decompressedData.resize(decompressedData.size() + CHUNK_SIZE);
        strm.next_out = reinterpret_cast<Bytef *>(&decompressedData[strm.total_out]);
        strm.avail_out = CHUNK_SIZE;

        error = inflate(&strm, Z_NO_FLUSH);
        if (error == Z_STREAM_END)
            break;
        else if (error != Z_OK)
            throw Xcept("inflate() failed: {}", zError(error));
    }

    decompressedData.resize(strm.total_out);
}

static bool ReadGsfData(
    std::span<const uint8_t> gsfData,
    std::vector<uint8_t> &reservedData,
    std::vector<uint8_t> &programData,
    std::string &tagData
)
{
    if (gsfData.size() < 16)
        return false;

    const uint8_t GSF_VERSION_BYTE = 0x22;
    if (gsfData[0] != 'P' || gsfData[1] != 'S' || gsfData[2] != 'F' || gsfData[3] != GSF_VERSION_BYTE)
        return false;

    const size_t compReservedSize = U32(gsfData, 4);
    const size_t compProgramSize = U32(gsfData, 8);
    const uint32_t compProgramCrc32 = U32(gsfData, 12);

    if (gsfData.size() < (16 + compReservedSize + compProgramSize))
        throw Xcept("ReadGsfData(): ill-formed gsflib, size in header larger than available");

    size_t dataOffset = 16;
    if (compReservedSize > 0) {
        // PSF's reserved section is raw data, not a second zlib stream.
        reservedData.assign(gsfData.begin() + dataOffset, gsfData.begin() + dataOffset + compReservedSize);
        dataOffset += compReservedSize;
    } else {
        reservedData.clear();
    }

    unsigned long crc = crc32_z(0, nullptr, 0);
    crc = crc32_z(crc, reinterpret_cast<const Bytef *>(gsfData.data() + dataOffset), compProgramSize);
    if (crc != compProgramCrc32)
        throw Xcept(
            "ReadGsfData(): program data crc32 mismatch: expected={:#08x} calculated={:#08x}", compProgramCrc32, crc
        );

    Decompress(gsfData.subspan(dataOffset, compProgramSize), programData);
    dataOffset += compProgramSize;

    size_t tagDataSize = gsfData.size() - dataOffset;
    if (tagDataSize > 50000)
        tagDataSize = 50000;    // do I understand Neill Corlett's doc right that this is limited to 50k bytes?

    tagData.assign(reinterpret_cast<const char *>(gsfData.data() + dataOffset), tagDataSize);
    return true;
}

bool Gsf::GetRomData(std::span<const uint8_t> gsfData, std::vector<uint8_t> &resultRomData)
{
    std::vector<uint8_t> reservedData;
    std::string tagData;
    if (!ReadGsfData(gsfData, reservedData, resultRomData, tagData)) {
        resultRomData.clear();
        return false;
    }

    if (resultRomData.size() < 12)
        throw Xcept("Gsf::GetRomData(): program data is ill-formed (size < 12)");

    const size_t entryPoint = U32(resultRomData, 0);
    const size_t loadAddress = U32(resultRomData, 4);
    const size_t romSize = U32(resultRomData, 8);

    // currently unused
    (void)entryPoint;
    // A full GSFLIB loads at the GBA cartridge address. Preserve a nonzero
    // cartridge offset rather than silently relocating all its pointers.
    const size_t offset = loadAddress == 0 ? 0 : loadAddress - 0x08000000;
    if ((loadAddress != 0 && (loadAddress < 0x08000000 || loadAddress >= 0x0A000000)) ||
        romSize > 32 * 1024 * 1024 - offset)
        throw Xcept("GSF program does not fit the GBA cartridge address space (load={:#x}, size={})", loadAddress, romSize);

    resultRomData.erase(resultRomData.begin(), resultRomData.begin() + 12);

    if (resultRomData.size() != romSize)
        throw Xcept("Gsf::GetRomData(): size mismatch between delcared ROM size and decompressed size");
    resultRomData.insert(resultRomData.begin(), offset, uint8_t(0));

    // uncomment this for GSF debugging and examination with hex editor
    // std::ofstream ofs("/tmp/gsf.gba");
    // if (ofs.is_open()) {
    //    ofs.write(reinterpret_cast<const char *>(resultRomData.data()), resultRomData.size());
    //    ofs.close();
    //}

    return true;
}

void Gsf::GetSongInfo(std::span<const uint8_t> gsfData, std::string &name, uint16_t &id)
{
    auto song = ReadSong(gsfData);
    name = std::move(song.title);
    id = song.id;
}

Gsf::Song Gsf::ReadSong(std::span<const uint8_t> gsfData)
{
    Song song;
    std::vector<uint8_t> programData;
    std::vector<uint8_t> reservedData;
    std::string tagData;
    if (!ReadGsfData(gsfData, reservedData, programData, tagData))
        throw Xcept("Gsf::GetSongInfo(): file does not appear to be a minigsf file");

    if (programData.size() == 13) {
        song.id = programData[12];
    } else if (programData.size() == 14) {
        song.id = static_cast<uint16_t>(programData[12] | (programData[13] << 8));
    } else {
        throw Xcept("MINIGSF is not a supported MP2K song selector (expected a 1- or 2-byte patch)");
    }
    if (U32(programData, 8) != programData.size() - 12)
        throw Xcept("MINIGSF declared patch size does not match its payload");

    if (!tagData.starts_with("[TAG]"))
        throw Xcept("Gsf::GetSongInfo(): no tag data found in minigsf");
    tagData = tagData.substr(5);

    /* read all tags and locate title */
    std::stringstream ss(tagData);
    std::string line;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string lowerCaseLine = line;
        std::transform(lowerCaseLine.begin(), lowerCaseLine.end(), lowerCaseLine.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        if (lowerCaseLine.starts_with("title=")) {
            song.title = line.substr(6);
        }
        if (lowerCaseLine.starts_with("_lib=")) song.library = line.substr(5);
        if (lowerCaseLine.starts_with("_lib2="))
            throw Xcept("MINIGSF with multiple libraries is not supported by this MP2K importer");
    }

    if (song.title.empty()) song.title = "<no title in GSF>";
    return song;
}

std::vector<Gsf::Song> Gsf::ReadPlaylist(const std::vector<std::filesystem::path> &paths,
                                      const std::filesystem::path &library, uint16_t songCount,
                                      const std::function<bool()> &cancelled)
{
    std::vector<Song> songs;
    const auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
        return value;
    };
    for (const auto &path : paths) {
        if (cancelled()) throw Xcept("Import cancelled");
        const bool archive = FileReader::cmpPathExt(path, "zip");
        FileReader::forEachInZipOrRaw(path, [](const auto &p) { return FileReader::cmpPathExt(p, "minigsf"); },
            [&](const auto &filename, FileReader &reader) {
                if (cancelled()) throw Xcept("Import cancelled");
                if (songs.size() >= 65535) throw Xcept("Too many MINIGSF entries");
                std::vector<uint8_t> bytes(reader.size());
                reader.read(bytes);
                auto song = ReadSong(bytes);
                song.filename = filename;
                std::replace(song.library.begin(), song.library.end(), '\\', '/');
                const std::filesystem::path dependency(song.library);
                const bool memberPaths = archive && !library.is_absolute();
                const auto resolved = memberPaths ? (filename.parent_path() / dependency).lexically_normal() : dependency.filename();
                const auto expected = memberPaths ? library.lexically_normal() : library.filename();
                if (song.library.empty() || dependency.is_absolute() || (!library.empty() &&
                    lower(resolved.generic_string()) != lower(expected.generic_string())))
                    throw Xcept("MINIGSF '{}' references '{}', not the loaded library '{}'", filename.string(), song.library, library.string());
                if (song.id >= songCount)
                    throw Xcept("MINIGSF '{}' selects song {}, outside the detected song table ({})", filename.string(), song.id, songCount);
                songs.push_back(std::move(song));
                return true;
            });
    }
    std::sort(songs.begin(), songs.end(), [](const auto &a, const auto &b) {
        if (a.filename.filename() != b.filename.filename()) return a.filename.filename() < b.filename.filename();
        return a.filename.generic_string() < b.filename.generic_string();
    });
    return songs;
}

std::string Gsf::GuessGameCodeFromPath(const std::filesystem::path &p)
{
    /* This is not unicode safe, but game codes are 4 byte ASCII anyway,
     * so it ought to be good enough for identifying a GSF. */
    std::string filename = p.stem().string();
    std::regex re("^AGB-([A-Z0-9]{4})-\\w+$");    // example: AGB-AN8J-JPN
    std::smatch matches;

    if (std::regex_match(filename, matches, re)) {
        assert(matches.size() == 2);
        return matches[1].str();
    } else {
        return "";
    }
}
