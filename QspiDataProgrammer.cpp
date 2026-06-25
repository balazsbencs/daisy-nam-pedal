#include "QspiDataProgrammer.h"
#include "RamFunc.h"

extern "C" NAM_RAMFUNC uint32_t QspiProgrammerRamProbeAddImpl(uint32_t a, uint32_t b)
{
    return a + b + 0x12340000u;
}

bool QspiProgrammerRamProbeAddressOk()
{
    return RamFuncAddressLooksValid(reinterpret_cast<const void*>(&QspiProgrammerRamProbeAddImpl));
}

uint32_t QspiProgrammerRamProbeAdd(uint32_t a, uint32_t b)
{
    return QspiProgrammerRamProbeAddImpl(a, b);
}
