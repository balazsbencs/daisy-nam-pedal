// Heap that spills into external SDRAM.
//
// The default libnosys _sbrk grows the heap from `end` (top of SRAM .bss) with
// NO upper bound: once malloc/new exhausts the ~330 KB SRAM tail it hands out
// addresses past 0x24080000 into unmapped space, and the first write there
// hard-faults the chip. A single NAM model's runtime conv-history arena is
// ~153 KB, so holding the old model live while decoding the new one (gapless
// preset switching) needs ~2x that and overflows SRAM on every preset change.
//
// This strong _sbrk overrides the library one. It serves allocations from the
// fast SRAM heap first, then permanently spills into the 64 MB SDRAM (mapped
// cacheable by libDaisy's MPU) once SRAM is full. malloc/free/new/delete keep
// working normally — freed blocks are recycled by newlib's free list — so
// there are no leaks and many models can be held resident at once.
//
// SDRAM is only valid after libDaisy brings the FMC up in daisy_seed.Init();
// all large (model) allocations happen well after that, and early/small boot
// allocations stay in SRAM, so the spill region is never touched too early.

#include <cstdint>
#include <errno.h>
#include <sys/types.h>

extern "C"
{
extern char end;          // first free SRAM address (top of .bss), from linker
extern char _esdram_bss;  // first free SDRAM address (after framebuffer .sdram_bss)

// Region tops.
static char* const kSramTop  = reinterpret_cast<char*>(0x24080000UL); // 0x24000000 + 512 KB
static char* const kSdramTop = reinterpret_cast<char*>(0xC4000000UL); // 0xC0000000 + 64 MB

// Diagnostics — readable over serial to confirm the spill behaviour on device.
volatile uint32_t g_sram_heap_used  = 0;
volatile uint32_t g_sdram_heap_used = 0;

caddr_t _sbrk(int incr)
{
    static char* cur = nullptr;
    if (cur == nullptr)
        cur = &end;

    // Fast path: serve from SRAM while the request fits the SRAM remainder.
    if (cur >= reinterpret_cast<char*>(0x24000000UL) && cur < kSramTop)
    {
        if (cur + incr <= kSramTop)
        {
            char* prev = cur;
            cur += incr;
            g_sram_heap_used = static_cast<uint32_t>(cur - &end);
            return reinterpret_cast<caddr_t>(prev);
        }
        // Doesn't fit the SRAM tail — jump to SDRAM once, permanently. The
        // unused SRAM tail is recycled by malloc's free list for later small
        // allocations, so it is not wasted.
        cur = reinterpret_cast<char*>((reinterpret_cast<uintptr_t>(&_esdram_bss) + 7U) & ~uintptr_t(7));
    }

    // SDRAM region.
    if (cur + incr <= kSdramTop)
    {
        char* prev = cur;
        cur += incr;
        g_sdram_heap_used = static_cast<uint32_t>(cur - reinterpret_cast<char*>(
            (reinterpret_cast<uintptr_t>(&_esdram_bss) + 7U) & ~uintptr_t(7)));
        return reinterpret_cast<caddr_t>(prev);
    }

    errno = ENOMEM;
    return reinterpret_cast<caddr_t>(-1);
}
} // extern "C"
