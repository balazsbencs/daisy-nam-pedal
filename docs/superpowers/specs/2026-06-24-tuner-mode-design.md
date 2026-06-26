# Footswitch Tuner Mode Design Spec

**Date:** 2026-06-24

## Goal

Add a fast, simple chromatic guitar tuner mode to the pedal:

```text
hold both footswitches -> audio mutes -> tuner screen -> tune -> exit back to pedal
```

The first shipped version should be monophonic. It should detect one played note
quickly and show the nearest chromatic note, octave, and cents error.

The optional second phase is a feasibility spike for a polyphonic six-string
standard guitar tuner. A true arbitrary chromatic polyphonic tuner is out of
scope unless the spike proves it can be made reliable within the available CPU
and UI budget.

## Current Findings

- `Controls` already debounces both footswitches and emits individual tap/hold
  events.
- Current footswitch long-press actions are:
  - FS1 hold: save current preset.
  - FS2 hold: revert current preset.
- `Controls` does not currently expose simultaneous footswitch state or a chord
  event.
- `main.cpp` owns the current performance/browse/edit mode decisions.
- `AudioEngine::Process()` owns the main DSP path. The audio callback currently
  calls it for every block and writes the processed mono signal to both outputs.
- `Ui` already has explicit screens for performance, browse, and edit.
- `DisplayRenderer` has enough primitives for a simple tuner: scaled text, bars,
  lines, rectangles, and meters.
- A `RealFft128` helper exists, but 128 samples at 48 kHz gives coarse frequency
  resolution and is not suitable for an accurate guitar tuner by itself.

## Design Summary

Add a dedicated tuner mode that temporarily takes over audio and UI:

1. Detect a both-footswitch chord before either individual hold action fires.
2. Enter tuner mode and mark the UI dirty.
3. In the audio callback, keep capturing dry input for pitch detection, but write
   silence to both outputs and skip the amp/effects DSP path.
4. Run pitch analysis from the main loop at a fixed UI-friendly rate, not from
   the audio interrupt.
5. Render a minimal chromatic tuner screen.
6. Exit tuner mode with another both-footswitch chord, or with a single
   footswitch tap while already in tuner mode.

The tuner is not a preset parameter and does not require desktop application
changes. It should coexist with the pre/post effects feature by bypassing all
processing while tuning.

## Footswitch Behavior

Add a new chord event to `Controls`:

```cpp
struct ControlEvent {
    bool fs1_tap;
    bool fs1_hold;
    bool fs2_tap;
    bool fs2_hold;
    bool fs_both_hold;
};
```

Recommended timing:

- `fs_both_hold` fires once when both footswitches have been held for 350 ms.
- Individual `fs1_hold` and `fs2_hold` still use the existing 800 ms threshold.
- When both switches are down together, suppress individual hold events until the
  chord is resolved. This prevents accidental save/revert while entering tuner.
- Individual taps should not fire when leaving a chord press.

This makes tuner entry feel faster than a normal long press and avoids changing
the existing save/revert gestures.

## Audio Architecture

Create a small tuner input capture and pitch detector module:

- Audio callback work:
  - read mono dry input before `AudioEngine`;
  - apply a cheap one-pole low-pass;
  - decimate by 4 from 48 kHz to 12 kHz;
  - write decimated samples into a fixed-size ring buffer;
  - output zeros while tuner mode is active.
- Main-loop work:
  - snapshot the latest decimated samples;
  - run pitch detection every 40-60 ms;
  - update a compact `TunerState` for the UI.

Do not allocate memory in the audio callback. Do not run expensive pitch analysis
in the audio callback.

## Pitch Detection

Use a monophonic YIN-style detector over decimated audio:

- Input sample rate after decimation: 12 kHz.
- Analysis window: 2048 decimated samples.
- Detection range: roughly 60 Hz to 1500 Hz.
- Tau range:
  - `tau_min = sample_rate / 1500`
  - `tau_max = sample_rate / 60`
- Reject silence/noise using RMS or peak threshold before running the full
  detector.
- Compute the cumulative mean normalized difference function.
- Pick the first tau below a confidence threshold, for example `0.12`.
- Refine frequency with parabolic interpolation around the selected tau.
- Convert frequency to nearest chromatic note using A4 = 440 Hz.
- Show cents offset in the range `[-50, +50]`.

This is a good fit because the tuner mode mutes and skips the heavy NAM/IR/effects
path, leaving enough CPU for main-loop analysis. It also avoids large FFTs and
dependency churn.

Expected response:

- Higher notes should appear almost immediately.
- Low E needs a longer window, so first stable output around 150-250 ms is
  acceptable.
- Display should keep the previous stable note briefly rather than flickering to
  `--` between string plucks.

## Tuner UI

Add `Ui::Screen::Tuner` and a `TunerState` value object:

```cpp
struct TunerState {
    bool  active;
    bool  signal_present;
    bool  stable;
    char  note[4];       // "E", "F#", "Bb" if flats are later supported
    int   octave;
    float cents;         // -50..+50
    float frequency_hz;
    float confidence;    // 0..1 display/debug only
};
```

Minimal screen layout:

- Top: `TUNER`
- Center: large note name and octave, for example `E2`
- Middle/bottom: horizontal cents bar with center marker
- Footer: `MUTED` and short exit hint
- No pitch: show `--` and an idle centered bar

Use sharps only for phase one to keep mapping simple: `C C# D D# E F F# G G# A A# B`.

## CPU And Memory Budget

The phase-one tuner is acceptable if these constraints hold:

- Audio callback tuner path is O(number of samples) with tiny constant cost.
- YIN analysis runs only in tuner mode.
- NAM, IR, EQ, pre-effects, and delay are skipped while tuner mode is active.
- All buffers are fixed-size:
  - decimated ring buffer: 4096 floats, about 16 KiB;
  - analysis window: 2048 floats, about 8 KiB;
  - difference/CMNDF array: about 201 floats, less than 1 KiB.

If CPU is still tight on hardware, reduce analysis rate before weakening pitch
accuracy. A 15-20 Hz pitch update rate is still usable.

## Polyphonic Phase Two Feasibility

Pushback: a real polyphonic chromatic tuner is a significantly harder DSP
problem than a monophonic tuner. It is not just "run six tuners." String
fundamentals and harmonics overlap, pluck transients are messy, and reliable
multi-pitch detection usually needs longer windows and more CPU.

Feasible second-phase target:

- Standard six-string guitar tuning only: `E2 A2 D3 G3 B3 E4`.
- Tuner mode still mutes and skips all amp/effects DSP.
- User strums open strings and sees six status indicators.
- Each string shows flat/in-tune/sharp/no-signal.
- Optional later: alternate tunings selected from the desktop app or firmware
  constants, but not in the first spike.

Recommended spike algorithms:

- Start with a Goertzel or resonator bank around the six expected fundamentals
  and their first few harmonics.
- Test tolerance across `-50..+50` cents per string.
- Use energy consistency across harmonics to reject false positives.
- Compare with a longer-window FFT/harmonic-product-spectrum prototype only if
  the resonator bank is not good enough.

Ship criteria for phase two:

- Detects a clean open-string strum within about one second.
- Correctly identifies each of the six strings in recorded test clips.
- Does not regress phase-one monophonic tuner behavior.
- Does not require running the full pedal DSP while tuning.

If the spike cannot meet those criteria, keep phase two as an experimental branch
or omit it. The monophonic tuner is the useful core feature.

## Test Strategy

Host tests:

- `TunerDetector`:
  - detects E2, A2, D3, G3, B3, E4 sine waves;
  - detects A4 at approximately 440 Hz;
  - reports expected cents for slightly sharp/flat tones;
  - rejects silence and low-level noise;
  - returns no stable pitch for two mixed notes in phase one.
- `Controls`:
  - both-foot hold emits `fs_both_hold`;
  - both-foot hold suppresses `fs1_hold` and `fs2_hold`;
  - normal individual taps and holds still work.
- UI mode:
  - tuner mode blocks normal preset navigation/save/revert actions;
  - exiting tuner returns to performance screen.

Hardware validation:

- Enter tuner with both footswitches and confirm no preset save/revert occurs.
- Confirm outputs are muted in tuner mode.
- Tune each open string and compare with a known-good external tuner.
- Confirm preset navigation and existing hold actions still work outside tuner.
- Confirm tuner still works when a model, IR, EQ, pre-effects, and delay are all
  configured.
