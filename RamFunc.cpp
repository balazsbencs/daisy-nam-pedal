#include "RamFunc.h"
#include <cstring>

void CopyRamFuncs()
{
    const size_t n = static_cast<size_t>(&_eramfunc - &_sramfunc);
    if (n > 0)
        std::memcpy(&_sramfunc, &_siramfunc, n);
}

bool RamFuncAddressLooksValid(const void* fn)
{
    uintptr_t p = reinterpret_cast<uintptr_t>(fn);
    return (p >= 0x24000000u && p < 0x24080000u) ||
           (p >= 0x20000000u && p < 0x20020000u) ||
           (p < 0x00010000u);
}
