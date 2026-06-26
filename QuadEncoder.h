#pragma once
#include "quad_decode.h"

#ifndef HOST_BUILD
#include "daisy_seed.h"
#include <atomic>

namespace pedal {

// Click-less quadrature encoder (A/B pins only, no click).
//
// PollIsr() samples + decodes at a steady rate from the audio ISR, so detents
// are never dropped by main-loop / display stalls. Decoded steps accumulate in
// an atomic counter; the main loop calls Drain() to collect the net movement.
class QuadEncoder {
public:
    void Init(daisy::Pin pin_a, daisy::Pin pin_b);

    // ISR context: sample pins once, decode, accumulate a step if detected.
    void PollIsr();

    // Main-loop context: net steps (+CW / -CCW) since the previous Drain().
    int8_t Drain()
    {
        int32_t v = accum_.exchange(0, std::memory_order_relaxed);
        if (v >  127) v =  127;
        if (v < -127) v = -127;
        return static_cast<int8_t>(v);
    }

private:
    daisy::GPIO gpio_a_, gpio_b_;
    uint8_t ah_ = 0xFFu;
    uint8_t bh_ = 0xFFu;
    std::atomic<int32_t> accum_{0};  // ISR writes (fetch_add), main loop drains
};

} // namespace pedal
#endif // HOST_BUILD
