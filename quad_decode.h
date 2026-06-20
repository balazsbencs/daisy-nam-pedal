#pragma once
#include <stdint.h>

// Pure click-less quadrature decoder — no Daisy dependency, host-testable.
//
// ah, bh: 8-sample debounce shift registers for pin A and pin B.
//         Initialize to 0xFF (meaning "has been stable high").
// a, b:   current raw pin levels (0 = low, non-zero = high).
// Returns: +1 (CW), -1 (CCW), or 0 (no step).
//
// Detects a step on the falling edge of A (CW) or B (CCW) only when
// the partner pin is already low — matching standard quadrature detent logic.
static inline int8_t quad_decode(uint8_t& ah, uint8_t& bh,
                                 uint8_t a, uint8_t b)
{
    bool prev_a = (ah == 0xFFu);
    bool prev_b = (bh == 0xFFu);
    ah = static_cast<uint8_t>((ah << 1u) | (a ? 1u : 0u));
    bh = static_cast<uint8_t>((bh << 1u) | (b ? 1u : 0u));
    if (prev_a && !a && !b) return  1;
    if (prev_b && !b && !a) return -1;
    return 0;
}
