#pragma once
#include "SyntheticRom.hpp"
#include <zlib.h>
#include <string>
#include <stdexcept>

namespace Fixture {
inline std::vector<uint8_t> ScannableRom(bool gsf = true) {
    auto rom = RomBytes();
    rom.resize(8192);
    for (size_t i = 0; i < 4; ++i) {
        Put32(rom, 0x200 + 8 * i, 0x08000300);
        Put32(rom, 0x204 + 8 * i, gsf ? 1 : 0x00010001);
    }
    Put32(rom, 0x220, 0xffffffff);
    for (size_t i = 0; i < 5; ++i) {
        Put32(rom, 0x900 + i * 8, 0x08001000);
        Put32(rom, 0x904 + i * 8, 0x08000200);
    }
    // Player zero is deliberately stripped, while songs use player one.
    Put32(rom, 0x100c, 0x03000400);
    Put32(rom, 0x1010, 0x03000500);
    rom[0x1014] = 16;
    Put32(rom, 0xa00, 0x08000800);
    Put32(rom, 0xa04, 0x03000100);
    Put32(rom, 0xa08, (1u << 26) | 0x40);
    Put32(rom, 0xa0c, 0x03000200);
    Put32(rom, 0xa10, 0x03000300);
    Put32(rom, 0xa14, 0x0094fc00);
    Put32(rom, 0xa18, 2);
    Put32(rom, 0xa1c, 0x08001000);
    Put32(rom, 0xa20, 0x03000600);
    return rom;
}
inline std::vector<uint8_t> Psf(std::vector<uint8_t> payload, std::string tags = {}, uint32_t address = 0x08000000) {
    std::vector<uint8_t> program(12);
    Put32(program, 0, 0x08000000);
    Put32(program, 4, address);
    Put32(program, 8, static_cast<uint32_t>(payload.size()));
    program.insert(program.end(), payload.begin(), payload.end());
    uLongf size = compressBound(program.size());
    std::vector<uint8_t> compressed(size);
    if (compress2(compressed.data(), &size, program.data(), program.size(), Z_BEST_SPEED) != Z_OK)
        throw std::runtime_error("compress2");
    compressed.resize(size);
    std::vector<uint8_t> psf(16);
    psf[0] = 'P'; psf[1] = 'S'; psf[2] = 'F'; psf[3] = 0x22;
    Put32(psf, 8, static_cast<uint32_t>(size));
    Put32(psf, 12, static_cast<uint32_t>(crc32_z(0, compressed.data(), compressed.size())));
    psf.insert(psf.end(), compressed.begin(), compressed.end());
    if (!tags.empty()) {
        const std::string text = "[TAG]" + tags;
        psf.insert(psf.end(), text.begin(), text.end());
    }
    return psf;
}
}
