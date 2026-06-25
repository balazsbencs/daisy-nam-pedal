#pragma once
#include <stddef.h>
#include <stdint.h>

#define NAM_RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))

extern "C" {
extern uint8_t _sramfunc;
extern uint8_t _eramfunc;
extern uint8_t _siramfunc;
}

void CopyRamFuncs();
bool RamFuncAddressLooksValid(const void* fn);
