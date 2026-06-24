#pragma once

#include <cstdint>

constexpr uint32_t kPerformanceRefreshMs = 100;

constexpr bool ShouldRefreshPerformanceScreen(bool browsing, bool editing)
{
    return !browsing && !editing;
}

constexpr bool ShouldRefreshPerformanceScreen(bool     browsing,
                                              bool     editing,
                                              uint32_t last_refresh_ms,
                                              uint32_t now_ms)
{
    return ShouldRefreshPerformanceScreen(browsing, editing)
        && (now_ms - last_refresh_ms >= kPerformanceRefreshMs);
}

constexpr uint32_t kTunerRefreshMs = 40;

// Tuner analysis/UI cadence: refresh while active and the interval has elapsed.
constexpr bool ShouldRefreshTunerScreen(bool     active,
                                        uint32_t last_refresh_ms,
                                        uint32_t now_ms)
{
    return active && (now_ms - last_refresh_ms >= kTunerRefreshMs);
}
