# Footswitch Tuner Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for detector/control logic, then superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fast muted chromatic tuner mode entered by holding both footswitches, with an optional phase-two feasibility spike for six-string polyphonic tuning.

**Architecture:** Add a both-footswitch chord event, a fixed-buffer monophonic tuner detector, a tuner UI screen, and a main-loop tuner mode. While tuner mode is active, the audio callback captures dry input for the detector, writes silence to the outputs, and skips the normal amp/effects path.

**Tech Stack:** C++17 firmware, libDaisy controls/audio/display, fixed-size DSP buffers, host tests with the existing `tests/Makefile`.

---

## Scope And Ordering

- Do not use worktrees.
- Implement this after, or rebased on top of, the pre/post effects feature.
- Tuner mode is not saved in presets.
- No desktop application changes are required for phase one.
- Phase one ships monophonic chromatic tuning only.
- Phase two is a feasibility spike, not a committed shipped feature.

## File Structure

Firmware repo:

- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Controls.h`
  - Add `fs_both_hold` to `ControlEvent`.
  - Add current pressed-state helpers if useful for tests/debug.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Controls.cpp`
  - Detect both-foot chord before individual long holds.
  - Suppress individual tap/hold events for chord presses.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/TunerDetector.h`
  - Declares tuner capture, pitch result, note mapping, and detector API.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/TunerDetector.cpp`
  - Implements decimated ring capture and monophonic YIN-style pitch detection.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Ui.h`
  - Add `Ui::Screen::Tuner` and `TunerState`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Ui.cpp`
  - Add `ShowTuner()` and `RenderTuner()`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/ui_mode.h`
  - Add small helper(s) for tuner refresh if needed.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`
  - Add tuner mode state.
  - Route footswitch chord/tap events.
  - Mute audio and feed tuner input while active.
  - Update tuner analysis and UI from the main loop.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`
  - Add `TunerDetector.cpp`.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_tuner_detector.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_controls_footswitch_chord.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_ui_mode.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

---

### Task 1: Add Host Tests For Tuner Math

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_tuner_detector.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Add a sine generator test helper**

Create a test helper that writes decimated-rate samples into the detector as if
they came from the audio callback. Keep it deterministic:

```cpp
static void FeedSine(TunerDetector& detector,
                     float hz,
                     float seconds,
                     float amplitude = 0.5f,
                     float sample_rate = 48000.0f);
```

The helper should feed 48 kHz samples through the public capture API, not bypass
the decimator.

- [ ] **Step 2: Test open guitar strings**

Add assertions for:

- E2: 82.41 Hz
- A2: 110.00 Hz
- D3: 146.83 Hz
- G3: 196.00 Hz
- B3: 246.94 Hz
- E4: 329.63 Hz

Expected: detector reports a stable pitch within about 2 Hz for low strings and
within about 1% for all strings.

- [ ] **Step 3: Test chromatic note mapping**

Add direct note-mapping tests for:

- A4 440 Hz => note `A4`, cents near 0.
- 445 Hz => note `A4`, positive cents.
- 435 Hz => note `A4`, negative cents.
- 466.16 Hz => note `A#4`, cents near 0.

- [ ] **Step 4: Test rejection paths**

Add tests for:

- silence returns `stable == false`;
- very low-level noise returns `stable == false`;
- two simultaneous sine waves returns `stable == false` or low confidence for
  phase one.

- [ ] **Step 5: Wire the test into `tests/Makefile`**

Add `test_tuner_detector` to the default host test target.

Run:

```sh
make -C tests test_tuner_detector
```

Expected: tests fail until the detector is implemented.

---

### Task 2: Implement Fixed-Buffer Monophonic Tuner Detector

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/TunerDetector.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/TunerDetector.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`

- [ ] **Step 1: Define public types**

Create `TunerDetector.h` with:

```cpp
struct TunerPitch {
    bool  signal_present = false;
    bool  stable         = false;
    float frequency_hz   = 0.0f;
    float cents          = 0.0f;
    float confidence     = 0.0f;
    char  note[4]        = {'-', '-', '\0', '\0'};
    int   octave         = 0;
};

class TunerDetector {
  public:
    void Reset();
    void PushAudioBlock(const float* mono_input, size_t frames);
    bool Analyze(TunerPitch& out);
};
```

Use a tiny `TunerNoteFromFrequency(float hz, TunerPitch& out)` helper that is
unit-testable without feeding audio.

- [ ] **Step 2: Implement audio-callback capture**

In `PushAudioBlock()`:

- apply a one-pole low-pass to the raw 48 kHz input;
- decimate by 4 to 12 kHz;
- write decimated samples into a power-of-two ring buffer;
- avoid heap allocation and locks.

Use fixed constants:

```cpp
constexpr float  kInputSampleRate = 48000.0f;
constexpr uint32_t kDecimation = 4;
constexpr float  kDetectorSampleRate = kInputSampleRate / kDecimation;
constexpr size_t kRingSize = 4096;
constexpr size_t kWindowSize = 2048;
```

- [ ] **Step 3: Implement YIN analysis**

In `Analyze()`:

- copy the latest `kWindowSize` decimated samples into a local fixed window;
- compute RMS/peak and reject silence before the expensive pass;
- compute the difference function for tau `8..200`;
- compute cumulative mean normalized difference;
- select the first tau below `0.12`;
- refine with parabolic interpolation;
- compute `frequency_hz`;
- reject implausible values outside `60..1500` Hz.

Keep the difference/CMNDF arrays fixed-size members or stack arrays with bounded
size. Do not allocate.

- [ ] **Step 4: Implement note and cents mapping**

Map frequency to MIDI-like semitone:

```cpp
note_number = roundf(69.0f + 12.0f * log2f(frequency_hz / 440.0f));
cents = 1200.0f * log2f(frequency_hz / note_frequency);
```

Use sharps only:

```cpp
C, C#, D, D#, E, F, F#, G, G#, A, A#, B
```

- [ ] **Step 5: Add firmware build source**

Add `TunerDetector.cpp` to the firmware `Makefile`.

- [ ] **Step 6: Run detector tests**

Run:

```sh
make -C tests test_tuner_detector
```

Expected: detector tests pass.

---

### Task 3: Add Both-Footswitch Chord Event

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Controls.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Controls.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_controls_footswitch_chord.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Add the event field**

Extend `ControlEvent`:

```cpp
bool fs_both_hold = false;
```

- [ ] **Step 2: Track chord state**

Add private state in `Controls`:

```cpp
uint32_t fs_both_down_ms_ = 0;
bool     fs_both_hold_sent_ = false;
bool     fs_chord_suppressed_ = false;
```

Use `constexpr uint32_t kFootswitchChordMs = 350;`.

- [ ] **Step 3: Detect chord before individual holds**

In `Controls::Process()`:

- process debounce as today;
- if both switches are currently pressed, start or continue chord timing;
- once both have been down for `kFootswitchChordMs`, emit `fs_both_hold`;
- mark the press as chord-suppressed;
- while chord-suppressed, do not emit `fs1_hold`, `fs2_hold`, `fs1_tap`, or
  `fs2_tap`;
- reset chord state only after both switches are released.

- [ ] **Step 4: Add host tests**

If `Controls` is currently hard to host-test because it directly depends on
hardware switches, extract the pure footswitch transition logic into a small
testable helper instead of mocking all libDaisy controls.

Tests:

- both down for 349 ms emits nothing;
- both down for 350 ms emits `fs_both_hold`;
- holding both past 800 ms does not emit individual hold events;
- normal FS1 hold still emits `fs1_hold`;
- normal FS2 hold still emits `fs2_hold`;
- normal taps still emit tap events.

- [ ] **Step 5: Run control tests**

Run:

```sh
make -C tests test_controls_footswitch_chord
```

Expected: chord tests pass.

---

### Task 4: Add Tuner UI Screen

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Ui.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Ui.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/ui_mode.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_ui_mode.cpp`

- [ ] **Step 1: Add `TunerState`**

In `Ui.h`, define:

```cpp
struct TunerState {
    bool  signal_present = false;
    bool  stable         = false;
    char  note[4]        = {'-', '-', '\0', '\0'};
    int   octave         = 0;
    float cents          = 0.0f;
    float frequency_hz   = 0.0f;
    float confidence     = 0.0f;
};
```

- [ ] **Step 2: Add `Screen::Tuner` and `ShowTuner()`**

Extend `Ui::Screen` and add:

```cpp
void ShowTuner(const TunerState& state);
```

Store the last tuner state in `Ui` and mark the screen dirty when meaningful
values change.

- [ ] **Step 3: Render the minimal tuner**

Implement `RenderTuner()`:

- clear background;
- draw `TUNER` top label;
- draw `MUTED`;
- draw large `--` when no stable pitch;
- draw large note and octave when stable;
- draw a horizontal cents bar from `-50` to `+50`;
- draw a fixed center marker and a moving indicator based on `cents`;
- clamp cents before drawing.

Use existing `DisplayRenderer` primitives. Avoid new fonts or image assets.

- [ ] **Step 4: Add UI refresh helper if useful**

If the main loop needs a separate cadence, add:

```cpp
constexpr uint32_t kTunerRefreshMs = 40;
```

and a small `ShouldRefreshTunerScreen(active, last, now)` helper in
`ui_mode.h`.

- [ ] **Step 5: Update UI tests**

Extend `tests/test_ui_mode.cpp` for any new refresh helper and ensure existing
performance refresh tests still pass.

Run:

```sh
make -C tests test_ui_mode
```

Expected: UI mode tests pass.

---

### Task 5: Integrate Tuner Mode In `main.cpp`

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`

- [ ] **Step 1: Add global/static tuner state**

Add:

```cpp
static TunerDetector tuner_detector;
static volatile bool tuner_active = false;
static TunerPitch tuner_pitch;
```

Prefer local namespace-static state matching the style already used in
`main.cpp`.

- [ ] **Step 2: Capture and mute in the audio callback**

At the top of the audio callback after mono input is prepared:

```cpp
if (tuner_active) {
    tuner_detector.PushAudioBlock(mono_in, frames);
    for (...) {
        out[0][i] = 0.0f;
        out[1][i] = 0.0f;
    }
    return;
}
```

Do not call `AudioEngine::Process()` while tuner mode is active.

- [ ] **Step 3: Route footswitch events**

In the main event handling:

- if `event.fs_both_hold` and tuner is inactive:
  - set `tuner_active = true`;
  - call `tuner_detector.Reset()`;
  - show tuner UI immediately;
  - do not run preset save/revert/navigation actions for that event.
- if tuner is active and `event.fs_both_hold`, `event.fs1_tap`, or
  `event.fs2_tap`:
  - set `tuner_active = false`;
  - reset the detector;
  - return to performance screen;
  - do not run preset actions for that event.
- while tuner is active:
  - ignore browse/edit/performance footswitch actions;
  - keep encoder behavior disabled unless a trivial exit gesture already exists.

- [ ] **Step 4: Analyze from the main loop**

Every `kTunerRefreshMs` while active:

- call `tuner_detector.Analyze(tuner_pitch)`;
- convert `TunerPitch` to `Ui::TunerState`;
- call `ui.ShowTuner(state)`.

Keep UI updates rate-limited. Do not analyze on every main-loop iteration.

- [ ] **Step 5: Confirm mode interactions**

Manual behavior expectations:

- entering tuner from performance mode returns to performance mode on exit;
- entering tuner while browsing/editing can either be blocked or exit back to
  performance mode. Prefer blocking tuner entry outside performance mode for
  phase one to reduce state complexity;
- preset save/revert never fires from the both-foot tuner chord.

---

### Task 6: Full Firmware And Host Verification

**Files:**
- Modify as needed based on build failures only.

- [ ] **Step 1: Run host tests**

Run:

```sh
make -C tests
```

Expected: all host tests pass.

- [ ] **Step 2: Build firmware**

Run:

```sh
make
```

Expected: firmware builds with `TunerDetector.cpp` included.

- [ ] **Step 3: Hardware smoke test**

On pedal hardware:

- boot normally;
- hold both footswitches and confirm tuner screen appears quickly;
- confirm output is muted;
- play each open string and compare against a known-good tuner;
- press either footswitch and confirm performance screen returns;
- confirm FS1 tap, FS2 tap, FS1 hold save, and FS2 hold revert still work outside
  tuner mode;
- confirm no preset data changes after entering/exiting tuner.

- [ ] **Step 4: Stress check with existing feature stack**

With a preset that has model, IR, EQ, pre-effects, and delay configured:

- enter tuner;
- confirm output still mutes;
- confirm pitch detection remains responsive;
- exit tuner;
- confirm normal processed audio resumes.

---

## Phase Two: Polyphonic Feasibility Spike

Do not start this until phase-one tuner mode is implemented and verified.

**Goal:** Determine whether a standard six-string open-strum tuner is shippable.

**Files:**
- Create only if the spike proceeds:
  - `/Users/bbalazs/daisy/daisy-nam-pedal/PolyTunerDetector.h`
  - `/Users/bbalazs/daisy/daisy-nam-pedal/PolyTunerDetector.cpp`
  - `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_poly_tuner_detector.cpp`

- [ ] **Step 1: Collect or synthesize test material**

Prepare deterministic host test inputs:

- synthetic six-string open chord;
- synthetic six-string open chord with one string at `-20` cents;
- synthetic six-string open chord with one string at `+20` cents;
- missing-string cases;
- two adjacent strings only.

If possible, add recorded clean DI clips later, but do not require recordings for
the first spike.

- [ ] **Step 2: Prototype a six-string detector**

Start with a Goertzel/resonator bank:

- expected strings: E2, A2, D3, G3, B3, E4;
- evaluate target frequency and nearby cent offsets;
- include first few harmonics per string;
- score each string independently;
- reject strings with weak or inconsistent harmonic energy.

Avoid a general-purpose arbitrary chromatic multi-pitch detector in this spike.

- [ ] **Step 3: Define pass/fail criteria**

Continue only if the prototype:

- identifies all six standard open strings in clean synthetic tests;
- handles one string being moderately sharp/flat;
- rejects missing strings;
- runs in tuner mode without audible/UI lag on hardware;
- does not increase audio callback cost beyond capture work.

- [ ] **Step 4: Decide ship/no-ship**

If pass criteria are met, write a separate implementation plan for a UI mode that
shows six compact string indicators.

If criteria are not met, document the result and keep the shipped tuner
monophonic.
