// Host shim: satisfies #include "daisy_seed.h" without Daisy SDK.
#pragma once
#include <cstdio>
#include <cstdarg>

namespace daisy {

struct DaisySeed {
    void PrintLine(const char* fmt, ...) {
        va_list a; va_start(a, fmt);
        vprintf(fmt, a); printf("\n");
        va_end(a);
    }
};

} // namespace daisy
