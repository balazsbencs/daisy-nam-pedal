// footswitch_chord.h — pure both-footswitch chord state machine.
// Decides when the both-foot hold fires and whether individual tap/hold events
// should be suppressed for the current press. Hardware-free so it host-tests.
#pragma once

#include <cstdint>

constexpr uint32_t kFootswitchChordMs = 350;

struct FootswitchChordOut
{
    bool both_hold      = false; // emit fs_both_hold this tick (once per chord)
    bool suppress_indiv = false; // suppress fs1/fs2 tap & hold this tick
};

class FootswitchChord
{
  public:
    void Reset()
    {
        both_hold_sent_ = false;
        suppressed_     = false;
    }

    // fs1_pressed/fs2_pressed: current debounced pressed state.
    // chord_held_ms: how long BOTH have been simultaneously held (min of the two
    //                TimeHeldMs); only consulted while both are pressed.
    FootswitchChordOut Update(bool fs1_pressed, bool fs2_pressed, uint32_t chord_held_ms)
    {
        FootswitchChordOut out;
        const bool         both = fs1_pressed && fs2_pressed;
        const bool         any  = fs1_pressed || fs2_pressed;

        if (both)
        {
            // Once both are down, suppress individual events for this whole press.
            suppressed_ = true;
            if (!both_hold_sent_ && chord_held_ms >= kFootswitchChordMs)
            {
                out.both_hold   = true;
                both_hold_sent_ = true;
            }
        }
        // Report BEFORE resetting so the tick BOTH release is still suppressed.
        // Taps fire on the release edge, so this swallows the release tap of the
        // last switch to come up — exiting a chord never leaks a preset change.
        out.suppress_indiv = suppressed_;

        if (!any)
        {
            // Reset only after BOTH release, so a staggered release of one switch
            // does not leak a stale tap/hold from the one still down.
            suppressed_     = false;
            both_hold_sent_ = false;
        }

        return out;
    }

  private:
    bool both_hold_sent_ = false;
    bool suppressed_     = false;
};
