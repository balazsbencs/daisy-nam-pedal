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
    std::puts("test_ui_mode: PASS");
    return 0;
}
