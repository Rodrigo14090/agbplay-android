#pragma once
#include <algorithm>
#include <cstdint>
#include <initializer_list>

// Control-thread pacing only. A slow renderer must yield between submissions,
// even if its draw time exceeds the lowest nominal frame-rate tier.
namespace FrameCadence {
inline int LowerRate(int fps) noexcept {
    for (const int candidate : {60, 30, 20, 15, 10, 5, 2, 1})
        if (candidate < fps) return candidate;
    return 1;
}
inline int64_t NextDeadline(int64_t previous, int64_t now, int fps, int64_t workNs) noexcept {
    const int64_t period = 1000000000LL / std::max(1, fps);
    int64_t deadline = previous ? previous + period : now;
    if (deadline < now - period) deadline = now;
    // Missed frames are not replayed; do not add an extra delay after rendering.
    (void)workNs;
    return std::max(deadline, now);
}
}
