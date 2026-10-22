#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

// Analyzed on the synthesis worker, never on the device callback. Caller owns
// synchronization. Double energy avoids float overflow and lossy integer scaling.
struct AudioPcmStatistics
{
    double energy = 0.0;
    uint64_t finiteSamples = 0;
    uint64_t clippedSamples = 0;
    uint64_t nonFiniteSamples = 0;
    float peak = 0.0f;
    void Add(std::span<const float> pcm) noexcept
    {
        for (float value : pcm) {
            if (!std::isfinite(value)) { ++nonFiniteSamples; continue; }
            energy += static_cast<double>(value) * value;
            ++finiteSamples;
            peak = std::max(peak, std::abs(value));
            clippedSamples += static_cast<uint64_t>(std::abs(value) >= 1.0f);
        }
    }
    double Rms() const noexcept
    {
        return finiteSamples ? std::sqrt(energy / static_cast<double>(finiteSamples)) : 0.0;
    }
};
