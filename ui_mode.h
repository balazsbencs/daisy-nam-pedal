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
