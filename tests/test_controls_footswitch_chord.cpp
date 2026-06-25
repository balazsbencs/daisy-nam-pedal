// test_controls_footswitch_chord.cpp — pure both-footswitch chord state machine.
#include "footswitch_chord.h"
#include "test_harness.h"

int main()
{
    // Both down for 349 ms: no chord event yet, but individuals already suppressed.
    {
        FootswitchChord c;
        auto            o = c.Update(true, true, 349);
        CHECK(!o.both_hold);
        CHECK(o.suppress_indiv);
    }

    // Both down for 350 ms: chord fires exactly once, even if held longer.
    {
        FootswitchChord c;
        c.Update(true, true, 100);
        auto o = c.Update(true, true, 350);
        CHECK(o.both_hold);
        CHECK(o.suppress_indiv);
        auto o2 = c.Update(true, true, 900);
        CHECK(!o2.both_hold);
        CHECK(o2.suppress_indiv);
    }

    // Holding both past 800 ms keeps individual events suppressed the whole time.
    {
        FootswitchChord c;
        uint32_t        times[] = {0, 350, 800, 1200};
        for (uint32_t t : times)
            CHECK(c.Update(true, true, t).suppress_indiv);
    }

    // Normal FS1 hold (only FS1 pressed): never suppressed, no chord.
    {
        FootswitchChord c;
        auto            o = c.Update(true, false, 800);
        CHECK(!o.both_hold);
        CHECK(!o.suppress_indiv);
    }

    // Normal FS2 hold (only FS2 pressed): never suppressed.
    {
        FootswitchChord c;
        CHECK(!c.Update(false, true, 800).suppress_indiv);
    }

    // Normal taps (brief single presses): never suppressed.
    {
        FootswitchChord c;
        CHECK(!c.Update(true, false, 5).suppress_indiv);
        CHECK(!c.Update(false, false, 0).suppress_indiv);
        CHECK(!c.Update(false, true, 5).suppress_indiv);
    }

    // After a chord, suppression holds until BOTH release, AND through the tick
    // they release (taps fire on release — this swallows the release tap), then
    // resets clean.
    {
        FootswitchChord c;
        c.Update(true, true, 350);                          // chord fires
        CHECK(c.Update(true, false, 360).suppress_indiv);   // one still down → suppressed
        CHECK(c.Update(false, false, 0).suppress_indiv);    // both release THIS tick → still suppressed
        auto o = c.Update(true, false, 5);                  // fresh single press, after reset
        CHECK(!o.suppress_indiv);
        CHECK(!o.both_hold);
    }

    return test_summary("test_controls_footswitch_chord");
}
