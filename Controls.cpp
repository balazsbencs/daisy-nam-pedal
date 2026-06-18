#include "Controls.h"

void Controls::Init()
{
    fs1_.Init(hw::PIN_FS1);
    fs2_.Init(hw::PIN_FS2);
    enc1_.Init(hw::PIN_ENC1_A, hw::PIN_ENC1_B, hw::PIN_ENC1_CLICK);
}

ControlEvent Controls::Process()
{
    fs1_.Debounce();
    fs2_.Debounce();
    enc1_.Debounce();

    // Rising-edge detection for long-press: fire once when threshold is crossed,
    // not every loop iteration while the encoder stays held.
    bool long_active = enc1_.TimeHeldMs() >= kLongPressMs && enc1_.Pressed();

    ControlEvent ev;
    ev.next_preset  = fs1_.RisingEdge();
    ev.prev_preset  = fs2_.RisingEdge();
    ev.enc1_delta   = static_cast<int8_t>(enc1_.Increment());
    ev.enc1_click   = enc1_.RisingEdge();
    ev.enc1_long    = long_active && !long_was_active_;
    long_was_active_ = long_active;
    return ev;
}
