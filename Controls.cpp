#include "Controls.h"

void Controls::Init()
{
    fs1_.Init(hw::PIN_FS1);
    fs2_.Init(hw::PIN_FS2);
    enc1_.Init(hw::PIN_ENC1_A, hw::PIN_ENC1_B, hw::PIN_ENC1_CLICK);
    enc2_.Init(hw::PIN_ENC2_A, hw::PIN_ENC2_B);
    enc3_.Init(hw::PIN_ENC3_A, hw::PIN_ENC3_B);
    enc4_.Init(hw::PIN_ENC4_A, hw::PIN_ENC4_B);
    enc5_.Init(hw::PIN_ENC5_A, hw::PIN_ENC5_B);
}

ControlEvent Controls::Process()
{
    fs1_.Debounce();
    fs2_.Debounce();
    enc1_.Debounce();

    // Long-press: fire once when threshold is crossed.
    bool enc1_long_active = enc1_.TimeHeldMs() >= kLongPressMs && enc1_.Pressed();
    bool fs1_hold_active  = fs1_.TimeHeldMs()  >= kLongPressMs && fs1_.Pressed();
    bool fs2_hold_active  = fs2_.TimeHeldMs()  >= kLongPressMs && fs2_.Pressed();

    ControlEvent ev;
    ev.enc_delta[0]  = static_cast<int8_t>(enc1_.Increment());
    ev.enc_delta[1]  = enc2_.Poll();
    ev.enc_delta[2]  = enc3_.Poll();
    ev.enc_delta[3]  = enc4_.Poll();
    ev.enc_delta[4]  = enc5_.Poll();
    ev.enc1_click    = enc1_.RisingEdge();
    ev.enc1_long     = enc1_long_active && !enc1_long_was_active_;
    // Tap fires on the RELEASE edge (not press), so the both-footswitch chord
    // can suppress it: a press-edge tap escapes a debounce tick BEFORE the other
    // switch reads Pressed(), so the chord never sees `both` in time. Releasing
    // also means a hold (>kLongPressMs) already fired, so it isn't a tap.
    const bool fs1_fall = fs1_.FallingEdge();
    const bool fs2_fall = fs2_.FallingEdge();
    ev.fs1_tap  = fs1_fall && !fs1_hold_was_active_;
    ev.fs1_hold = fs1_hold_active && !fs1_hold_was_active_;
    ev.fs2_tap  = fs2_fall && !fs2_hold_was_active_;
    ev.fs2_hold = fs2_hold_active && !fs2_hold_was_active_;

    enc1_long_was_active_ = enc1_long_active;
    fs1_hold_was_active_  = fs1_hold_active;
    fs2_hold_was_active_  = fs2_hold_active;

    // Both-footswitch chord: detect before individual holds/taps escape. When
    // both have been down kFootswitchChordMs, emit fs_both_hold and suppress the
    // individual events for the whole press.
    bool     fs1_pressed = fs1_.Pressed();
    bool     fs2_pressed = fs2_.Pressed();
    uint32_t chord_held  = 0;
    if (fs1_pressed && fs2_pressed)
    {
        uint32_t h1 = static_cast<uint32_t>(fs1_.TimeHeldMs());
        uint32_t h2 = static_cast<uint32_t>(fs2_.TimeHeldMs());
        chord_held  = (h1 < h2) ? h1 : h2;
    }
    FootswitchChordOut chord = chord_.Update(fs1_pressed, fs2_pressed, chord_held);
    ev.fs_both_hold          = chord.both_hold;
    if (chord.suppress_indiv)
    {
        ev.fs1_tap = ev.fs1_hold = ev.fs2_tap = ev.fs2_hold = false;
        // Each switch still emits a release (FallingEdge) tap when it comes up,
        // but FallingEdge lands ~7 debounce ticks AFTER Pressed() drops (plus
        // contact bounce) — i.e. after the chord has already reset. Latch it so
        // each switch's trailing release tap is eaten and never leaks a preset
        // change when the chord ends.
        eat_fs1_release_ = true;
        eat_fs2_release_ = true;
    }
    if (fs1_fall && eat_fs1_release_) { ev.fs1_tap = false; eat_fs1_release_ = false; }
    if (fs2_fall && eat_fs2_release_) { ev.fs2_tap = false; eat_fs2_release_ = false; }
    return ev;
}
