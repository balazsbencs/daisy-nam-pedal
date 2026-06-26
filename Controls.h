// Controls.h — debounced inputs, emits high-level events once per main-loop tick.
#pragma once
#include "hid/switch.h"
#include "hid/encoder.h"
#include "QuadEncoder.h"
#include "HardwareConfig.h"
#include "footswitch_chord.h"

struct ControlEvent
{
    // enc_delta[0] = ENC1 (Gain, with click), [1]=Bass, [2]=Mid, [3]=Treble, [4]=Vol
    int8_t enc_delta[5] = {};
    bool   enc1_click   = false;
    bool   enc1_long    = false; // held > kLongPressMs, fires once on threshold
    bool   fs1_tap      = false; // FS1 momentary press (next preset)
    bool   fs1_hold     = false; // FS1 held > kLongPressMs (save)
    bool   fs2_tap      = false; // FS2 momentary press (prev preset)
    bool   fs2_hold     = false; // FS2 held > kLongPressMs (revert)
    bool   fs_both_hold = false; // both footswitches held > kFootswitchChordMs (tuner)
};

class Controls
{
public:
    static constexpr float kLongPressMs = 800.0f;

    void Init();

    // Call once per main-loop iteration; returns events since last call.
    ControlEvent Process();

    // Call from the audio ISR (steady rate): samples the Bass/Mid/Treble/Vol
    // quadrature encoders so detents aren't dropped when the main loop stalls
    // on display work. Steps accumulate until Process() drains them.
    void PollEncodersIsr();

private:
    daisy::Switch   fs1_, fs2_;
    daisy::Encoder  enc1_;
    pedal::QuadEncoder enc2_, enc3_, enc4_, enc5_;

    bool enc1_long_was_active_ = false;
    bool fs1_hold_was_active_  = false;
    bool fs2_hold_was_active_  = false;

    // After a chord, eat each switch's trailing release tap (FallingEdge lags
    // Pressed() by several debounce ticks, so it escapes the chord's window).
    bool eat_fs1_release_ = false;
    bool eat_fs2_release_ = false;

    FootswitchChord chord_;
};
