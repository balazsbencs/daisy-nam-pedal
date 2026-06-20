#pragma once
#include <stdint.h>

namespace pedal {

// Result of vmeter_fill: the fill segment within a bar of [height] pixels.
// y is the top of the segment (pixels from bar top); h is the segment height.
struct MeterFill { int y; int h; };

// Pure geometry for a vertical bar meter.
// height : total bar height in pixels.
// val    : unipolar [0, 1] bottom-fill; bipolar [-1, +1] center-fill.
// bipolar: false = bottom fill from 0 (silence), true = center fill (±dB).
static inline MeterFill vmeter_fill(int height, float val, bool bipolar)
{
    if (bipolar) {
        if (val >  1.0f) val =  1.0f;
        if (val < -1.0f) val = -1.0f;
        int half = height / 2;
        if (val >= 0.0f) {
            int h = static_cast<int>(val * static_cast<float>(half));
            return {half - h, h};
        } else {
            int h = static_cast<int>(-val * static_cast<float>(half));
            return {half, h};
        }
    } else {
        if (val > 1.0f) val = 1.0f;
        if (val < 0.0f) val = 0.0f;
        int h = static_cast<int>(val * static_cast<float>(height));
        return {height - h, h};
    }
}

} // namespace pedal
