# Preserve the Active UI Screen

## Problem

The once-per-second diagnostics block always calls `PushPerformanceScreen()`.
Entering Browse or Edit therefore updates the display correctly at first, but
the next diagnostics tick replaces it with the Performance screen even though
the application remains in Browse or Edit mode.

## Design

Keep the diagnostics calculations and logging unchanged. Gate the periodic
Performance-screen refresh on the existing UI mode flags: refresh only when
both `browsing` and `editing` are false. Browse and Edit screens already redraw
when their state changes, so they need no periodic refresh.

Extract the mode decision into a small hardware-independent helper so the
behavior can be covered by a host-side regression test. The helper returns true
only for Performance mode (`!browsing && !editing`).

## Testing

Add a host test proving that the periodic Performance refresh is enabled in
Performance mode and disabled in both Browse and Edit modes. Run that test,
then run the existing host test suite and the firmware build.

## Scope

No encoder semantics, screen transitions, rendering, or diagnostics timing
will change. The fix only prevents the diagnostics tick from replacing a
non-Performance screen.
