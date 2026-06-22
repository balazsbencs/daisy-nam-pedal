#include "ui_mode.h"
#include <cassert>
#include <cstdio>

int main()
{
    assert(ShouldRefreshPerformanceScreen(false, false));
    assert(!ShouldRefreshPerformanceScreen(true, false));
    assert(!ShouldRefreshPerformanceScreen(false, true));
    assert(!ShouldRefreshPerformanceScreen(true, true));
    std::puts("test_ui_mode: PASS");
    return 0;
}
