#pragma once
#include "quad_decode.h"

#ifndef HOST_BUILD
#include "daisy_seed.h"

namespace pedal {

// Click-less quadrature encoder (A/B pins only, no click).
// Uses 8-sample debounce history; call Poll() once per main-loop tick.
class QuadEncoder {
public:
    void Init(daisy::Pin pin_a, daisy::Pin pin_b);
    // Returns +1 (CW), -1 (CCW), or 0 (no step) since last call.
    int8_t Poll();

private:
    daisy::GPIO gpio_a_, gpio_b_;
    uint8_t ah_ = 0xFFu;
    uint8_t bh_ = 0xFFu;
};

} // namespace pedal
#endif // HOST_BUILD
