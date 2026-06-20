#include "display/meter_fill.h"
#include "test_harness.h"
using pedal::vmeter_fill;
using pedal::MeterFill;

int main()
{
    // Unipolar: silence = no fill at bottom.
    { MeterFill m = vmeter_fill(100, 0.0f, false);
      CHECK_EQ(m.h, 0); }

    // Unipolar: full signal = full bar.
    { MeterFill m = vmeter_fill(100, 1.0f, false);
      CHECK_EQ(m.y, 0); CHECK_EQ(m.h, 100); }

    // Unipolar: half signal = bottom half.
    { MeterFill m = vmeter_fill(100, 0.5f, false);
      CHECK_EQ(m.y, 50); CHECK_EQ(m.h, 50); }

    // Bipolar: unity gain = no fill.
    { MeterFill m = vmeter_fill(100, 0.0f, true);
      CHECK_EQ(m.h, 0); }

    // Bipolar: +1 (max boost) = top half.
    { MeterFill m = vmeter_fill(100, 1.0f, true);
      CHECK_EQ(m.y, 0); CHECK_EQ(m.h, 50); }

    // Bipolar: -1 (max cut) = bottom half.
    { MeterFill m = vmeter_fill(100, -1.0f, true);
      CHECK_EQ(m.y, 50); CHECK_EQ(m.h, 50); }

    // Unipolar clamp: val > 1 treated as 1.
    { MeterFill m = vmeter_fill(100, 2.0f, false);
      CHECK_EQ(m.h, 100); }

    return test_summary("test_meter_fill");
}
