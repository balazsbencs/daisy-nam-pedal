#include "ui_mode.h"
#include <cassert>
#include <cstdio>

int main()
{
    assert(ShouldRefreshPerformanceScreen(false, false));
    assert(!ShouldRefreshPerformanceScreen(true, false));
    assert(!ShouldRefreshPerformanceScreen(false, true));
    assert(!ShouldRefreshPerformanceScreen(true, true));

    assert(kPerformanceRefreshMs == 100);
    assert(!ShouldRefreshPerformanceScreen(false, false, 0, 99));
    assert(ShouldRefreshPerformanceScreen(false, false, 0, 100));
    assert(!ShouldRefreshPerformanceScreen(true, false, 0, 100));
    assert(!ShouldRefreshPerformanceScreen(false, true, 0, 100));

    assert(kTunerRefreshMs == 40);
    assert(!ShouldRefreshTunerScreen(false, 0, 100)); // inactive never refreshes
    assert(!ShouldRefreshTunerScreen(true, 0, 39));   // interval not yet elapsed
    assert(ShouldRefreshTunerScreen(true, 0, 40));    // interval elapsed
    assert(ShouldRefreshTunerScreen(true, 100, 140));
    std::puts("test_ui_mode: PASS");
    return 0;
}
