#pragma once

#include "AgbTypes.hpp"
#include "Xcept.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

class FileReader;

class Rom
{
private:
    Rom() = default;

public:
    Rom(Rom &&) = default;
    Rom(const Rom &) = delete;
    Rom &operator=(const Rom &) = delete;

    static Rom LoadFromFile(const std::filesystem::path &filePath);
    static Rom LoadFromBufferCopy(std::span<uint8_t> buffer);    // buffer may be freed afterwards
    static Rom LoadFromBufferRef(std::span<uint8_t> buffer);     // buffer must not be freed during object lifetime

    static void CreateInstance(const std::filesystem::path &filePath);
    static Rom &Instance() { return *globalInstance; }

    const uint8_t &operator[](size_t pos) const { CheckRange(pos, 1); return romData[pos]; }

    int8_t ReadS8(size_t pos) const { return static_cast<int8_t>(ReadU8(pos)); }

    uint8_t ReadU8(size_t pos) const
    {
        CheckRange(pos, 1);
        return romData[pos];
    }

    int16_t ReadS16(size_t pos) const { return static_cast<int16_t>(ReadU16(pos)); }
    uint16_t ReadU16(size_t pos) const
    {
        CheckRange(pos, 2);
        return static_cast<uint16_t>(uint16_t(romData[pos]) | (uint16_t(romData[pos + 1]) << 8));
    }

    int32_t ReadS32(size_t pos) const { return static_cast<int32_t>(ReadU32(pos)); }

    uint32_t ReadU32(size_t pos) const
    {
        CheckRange(pos, 4);
        return uint32_t(romData[pos]) | (uint32_t(romData[pos + 1]) << 8) |
               (uint32_t(romData[pos + 2]) << 16) | (uint32_t(romData[pos + 3]) << 24);
    }

    size_t ReadAgbPtrToPos(size_t pos) const
    {
        uint32_t ptr = ReadU32(pos);
        if (!ValidPointer(ptr))
            throw Xcept("Cannot parse pointer at [{:08X}]={:08X}", pos, ptr);
        return ptr - AGB_MAP_ROM;
    }

    const void *GetPtr(size_t pos) const { CheckRange(pos, 0); return romData.data() + pos; }
    size_t Size() const { return romData.size(); }
    bool ValidPointer(uint32_t ptr) const
    {
        return ptr >= AGB_MAP_ROM && size_t(ptr - AGB_MAP_ROM) < romData.size();
    }
    bool ValidRange(size_t pos, size_t len) const
    {
        return pos <= romData.size() && len <= romData.size() - pos;
    }

    std::string ReadString(size_t pos, size_t limit) const;
    std::string GetROMCode() const;

    bool IsGsf() const;
    const std::filesystem::path &GsfPath() const { return gsfPath; }

private:
    void CheckRange(size_t pos, size_t len) const
    {
        if (!ValidRange(pos, len)) [[unlikely]]
            throw Xcept("ROM read out of range: offset={:#x}, length={:#x}, ROM size={:#x}", pos, len, romData.size());
    }
    void Verify();
    void LoadFile(const std::filesystem::path &filePath);
    bool LoadZip(const std::filesystem::path &filePath);
    bool LoadGsflib(const std::filesystem::path &filePath);
    bool LoadGsflib(FileReader &fileReader);
    bool LoadRaw(const std::filesystem::path &filePath);
    bool LoadRaw(FileReader &fileReader);

    std::span<uint8_t> romData;
    std::vector<uint8_t> romContainer;
    std::filesystem::path gsfPath;

    bool isGsf = false;

    static std::unique_ptr<Rom> globalInstance;
};
