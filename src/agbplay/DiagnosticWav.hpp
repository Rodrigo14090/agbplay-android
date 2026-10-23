#pragma once
#include "Types.hpp"
#include <filesystem>
#include <memory>
#include <span>

// Streaming PCM reader for output-route comparisons; never used on a callback.
class DiagnosticWav {
public:
    DiagnosticWav(const std::filesystem::path &path, uint32_t expectedRate);
    ~DiagnosticWav();
    DiagnosticWav(const DiagnosticWav &) = delete;
    size_t Read(std::span<sample> frames);
    void Rewind();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
