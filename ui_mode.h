#pragma once

constexpr bool ShouldRefreshPerformanceScreen(bool browsing, bool editing)
{
    return !browsing && !editing;
}
