// Controls.h — debounced inputs, emits high-level events once per main-loop tick.
#pragma once
#include "hid/switch.h"
#include "hid/encoder.h"
#include "HardwareConfig.h"

struct ControlEvent
{
    bool next_preset   = false;
    bool prev_preset   = false;
    int8_t enc1_delta  = 0;   // encoder turn count this tick (+/-)
    bool enc1_click    = false;
    bool enc1_long     = false; // held > kLongPressMs
};

class Controls
{
public:
    static constexpr float kLongPressMs = 800.0f;

    void Init();

    // Call once per main-loop iteration; returns events since last call.
    ControlEvent Process();

private:
    daisy::Switch  fs1_, fs2_;
    daisy::Encoder enc1_;
    bool long_was_active_ = false; // for rising-edge detection on enc1_long
};
