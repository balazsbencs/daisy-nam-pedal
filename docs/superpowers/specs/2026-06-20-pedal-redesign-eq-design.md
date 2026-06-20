# Performance Redesign + 3-Band Post EQ — Design

**Date:** 2026-06-20
**Status:** Approved for planning
**Scope:** Redesign the Performance screen for guitar-pedal readability, add a 3-band
post-IR tone EQ, and map the prototype's five encoders to a virtual amp panel.

---

## 1. Goals

1. Replace the cramped Performance screen with a large, glanceable "channel strip"
   layout readable from across a room.
2. Add a 3-band tone EQ (bass / mid / treble) after the IR stage, with per-band gain
   (live) and per-band frequency (per-preset).
3. Map the five hardware encoders to a virtual amp panel: Gain, Bass, Mid, Treble, Vol.

Non-goals: per-band Q control, EQ on/off toggle, more than one IR, MIDI. (YAGNI.)

---

## 2. Signal chain

```
in → GAIN(input_gain) → NAM model → IR → EQ[bass·mid·treble] → VOL(output_vol) → out (L+R)
```

The EQ sits **after** the IR convolver and **before** the output volume, matching the
"post EQ" request.

---

## 3. EQ DSP design

New self-contained `Eq3` component (own files `Eq3.h` / `Eq3.cpp`), mono, float.

- **Three RBJ-cookbook biquads** (Direct Form I or II transposed):
  - **Bass** — low shelf
  - **Mid** — peaking (bell)
  - **Treble** — high shelf
- **Per-band gain:** −12 … +12 dB, step 0.5 dB, default 0 dB (flat / transparent).
- **Per-band frequency** (defaults & ranges):
  - Bass shelf corner: 50 … 500 Hz, default **100 Hz**
  - Mid peak center: 200 … 3000 Hz, default **750 Hz**, **Q fixed ≈ 0.7**
  - Treble shelf corner: 1.5k … 10 kHz, default **4 kHz**
- **CPU:** 3 biquads × 48 samples ≈ negligible; comfortably inside the 1 ms budget.

### Coefficient updates (real-time safe)

Coefficients are recomputed in the **main loop** whenever a gain or frequency changes,
never in the audio ISR. They are published to the callback using the **double-buffered
block + atomic index** idiom AudioEngine already uses for model/IR swaps:

- `AudioEngine` owns two `Eq3Coeffs` blocks and a `std::atomic<uint8_t> active_eq_`.
- Main loop writes the inactive block, then flips `active_eq_`.
- The ISR reads the active block each call.
- **Filter state** (delay elements `z1/z2` per band) lives ISR-side in `Eq3` and is
  *not* swapped, so audio is continuous across coefficient changes.

`Eq3` exposes: `Reset(sample_rate)`, `SetBand(band, gain_db, freq_hz)` →
recompute coeffs (main-loop side), and `Process(buf, frames)` (ISR side, reads coeffs).

---

## 4. Preset data model

Add to `NamPreset` (in `data_format.h`):

```c
float eq_bass_gain;     // dB, default 0
float eq_mid_gain;      // dB, default 0
float eq_treble_gain;   // dB, default 0
float eq_bass_freq;     // Hz, default 100
float eq_mid_freq;      // Hz, default 750
float eq_treble_freq;   // Hz, default 4000
```

### Backward compatibility

`PresetManager::Init` zero-initializes the struct, then copies
`memcpy(&preset, blob, min(e->length, sizeof(NamPreset)))` instead of requiring
`e->length >= sizeof(NamPreset)`. Older preset blobs (without EQ fields) therefore load
as **flat EQ with default frequencies** rather than being rejected. Synthesized
fallback presets also get flat-EQ defaults.

`data_image.bin` and `presets.json` are rebuilt with the new EQ section (defaults above).

---

## 5. Controls

### Encoders

| Encoder | Param          | A   | B   | Click |
|---------|----------------|-----|-----|-------|
| ENC1    | Gain (input)   | D0  | D1  | D2    |
| ENC2    | Bass gain      | D7  | D8  | —     |
| ENC3    | Mid gain       | D9  | D10 | —     |
| ENC4    | Treble gain    | D27 | D28 | —     |
| ENC5    | Vol (output)   | D29 | D30 | —     |

- **ENC1** uses `daisy::Encoder` (has a click switch on D2).
- **ENC2–ENC5** have no click pin, so they use a new lightweight **click-less quadrature
  decoder** (`QuadEncoder`): A/B GPIO inputs with pull-ups and the same 2-bit shift /
  increment state machine as `daisy::Encoder::Debounce`, minus the `Switch`. Debounced at
  the main-loop rate (≥1 kHz gate via `System::GetNow`).
- Pins verified conflict-free against display (D13/14/18/22/24/26), footswitches
  (D15/16), and ENC1 (D0/1/2).

### Footswitches

- **FS1 tap** = next preset; **FS1 hold (>1 s)** = **Save** live edits to current preset.
- **FS2 tap** = prev preset; **FS2 hold (>1 s)** = **Revert** to stored preset.

### Navigation

- **ENC1 click** → enter **Browse** (preset list); ENC1 turn scrolls; ENC1 click selects.
- **ENC1 long-press** → enter **Edit** (deep settings).

### `ControlEvent` / `Controls` changes

`ControlEvent` gains five encoder deltas (`enc_delta[5]` or named fields) plus
`enc1_click`, `enc1_long`, `fs1_tap/fs1_hold`, `fs2_tap/fs2_hold`. `Controls` owns one
`daisy::Encoder` (ENC1) and four `QuadEncoder` (ENC2–5), plus the two `Switch`es with
tap-vs-hold edge detection.

---

## 6. Live-edit behavior ("Live + manual save")

- Turning any encoder updates the live audio parameter **immediately** (you hear it).
- The first live change sets a **dirty** flag; the Performance screen shows an
  **`● EDITED`** marker beside the ACTIVE/BYPASS pill.
- **FS1 hold** writes the live values into the in-RAM preset **and persists to QSPI
  flash** (see §6a), shows a brief "SAVED" flash, and clears dirty.
- **FS2 hold** reloads the stored preset and clears dirty.
- Switching presets (FS tap) while dirty **discards** live edits (loads target preset).

## 6a. Persistence (QSPI write-back, XIP-safe)

Saved edits survive power-off by writing the preset back into the QSPI data partition
(`0x90200000`). Because the app executes **in place from the same QSPI chip**
(`BOOT_QSPI`), the chip cannot be in memory-mapped mode during an erase/program. The save
routine must therefore be XIP-safe:

1. **Stop audio** (`daisy_seed.StopAudio()` / mute) — save is a deliberate, non-playing
   action, so a brief halt (~tens of ms) is acceptable.
2. Disable interrupts whose handlers live in QSPI; the erase/program routine and anything
   it calls must be **RAM-resident** (placed in ITCM/DTCM, e.g. via section attribute), so
   no instruction/data fetch hits QSPI while it is in indirect mode.
3. Switch `QSPIHandle` to `INDIRECT_POLLING`, **erase** the 4 kB sector(s) covering the
   target preset blob, **program** the updated `NamPreset` bytes, then switch back to
   `MEMORY_MAPPED`.
4. Re-enable interrupts and **resume audio**.

`QspiStorage` gains write support: `Status WritePreset(const NamDataEntry* entry, const
NamPreset& p)` (and the lower-level erase/program helpers), computing the blob's absolute
flash address from the directory entry. Reads remain zero-copy memory-mapped as today.
The directory layout is unchanged — only the existing preset blob bytes are rewritten
(same length), so no directory rebuild is needed.

**Risks / constraints to handle in the plan:** preset blobs are 4 kB **sector-aligned**
(`NAM_DATA_SECTOR_SIZE`), so erasing a preset's own sector does not disturb neighbors (no
read-modify-write needed). Remaining risks: ensuring the save routine + its stack are fully
RAM-resident, and confirming `libDaisy`'s `QSPIHandle::Erase` mode-switch guard
(`CheckProgramMemory`) cooperates while booted from QSPI.

---

## 7. Screens

### Performance (channel strip — approved mockup)

- Header: `NN / NN` preset index (left), `ACTIVE`/`BYPASS` pill (right), `● EDITED`
  marker when dirty, `!OVERLOAD` warning when the audio callback exceeds budget
  (already implemented).
- Large preset name (2× scaled bitmap font).
- **AMP** and **CAB** on separate, larger lines (full names).
- Five vertical bars: Gain · Bass · Mid · Treble · Vol.
  - Gain & Vol fill from the bottom (level meters).
  - Bass/Mid/Treble are **bipolar**: fill **up from center = boost**, **down = cut**
    (cut tinted amber/yellow, boost green).
  - Each bar shows a large bold value and its name beneath.

### Browse

Existing preset-list screen; navigated by ENC1 (scroll/select).

### Edit (scrolling field list)

Fields: `MODEL`, `CAB`, `BASS FREQ`, `MID FREQ`, `TREB FREQ`, `IN GAIN`, `OUT VOL`,
`BYPASS`. ENC1 navigates fields; ENC1 click enters value-edit; FS1 apply / FS2 cancel
(existing Edit interaction model, extended with the three EQ-frequency fields).

---

## 8. Rendering additions

In `display_renderer`:

- **`DrawTextScaled(x, y, str, fg, bg, font, scale)`** — integer upscaling of the existing
  1-bit fonts (each source pixel drawn as `scale × scale`). Used for the big preset name
  (2× of Font_16x26 ≈ 32×52) and bar values (Font_16x26, or 2× where space allows).
- **`VMeter(x, y, w, h, val, bipolar, color)`** — vertical bar. When `bipolar`, `val ∈
  [-1,1]` fills up/down from the vertical center; otherwise `val ∈ [0,1]` fills from the
  bottom.

- **`FillRoundRect(x, y, w, h, r, color, bg)`** — filled rectangle with simple (non-AA)
  rounded corners (corner pixels outside radius `r` set to `bg`). Used for the bar tracks
  and the ACTIVE/BYPASS pill so the device matches the rounded mockup. The bar *fill* stays
  a plain rect (rounding it would leave stray corner pixels at full-scale); the rounded
  track carries the silhouette.

Colors reuse the existing palette (`kColorGreen` boost, `kColorOrange`/`kColorYellow` cut,
`kColorCyan`, `kColorDim`, etc.).

---

## 9. Files touched & testing

**Source:** `data_format.h`, new `Eq3.{h,cpp}`, new `QuadEncoder.{h,cpp}` (or folded into
`Controls`), `AudioEngine.{h,cpp}`, `PresetManager.cpp`, `QspiStorage.{h,cpp}` (add
XIP-safe write-back, §6a), `Controls.{h,cpp}`, `HardwareConfig.h`, `main.cpp`,
`Ui.{h,cpp}`, `display/display_renderer.{h,cpp}`, `Makefile` (new sources).

**Data/tools:** `tools/build_data_image.py`, `data/presets.json` (EQ defaults), rebuild
`data_image.bin`.

**Host tests** (`tests/`, clang on host):
- `test_eq3` — biquad magnitude response: flat at 0 dB; boost/cut at band centers within
  tolerance; shelves asymptote correctly; stability at range extremes.
- Preset round-trip test extended with EQ fields, including the short-blob backward-compat
  path (old blob → flat-EQ defaults).
- `fake_audio_engine.h` extended so PresetManager EQ application is testable on host.
- Persistence serialization test: a fake/stubbed storage verifies the correct blob bytes
  and target address are produced for `WritePreset`. (The actual flash erase/program and
  XIP-safe sequence are hardware-only and verified on-device, not in host tests.)

---

## 10. Open items / defaults to confirm during implementation

- Tunable defaults (cheap to change): **±12 dB / 0.5 dB steps**, **mid Q = 0.7**, the
  **frequency ranges** in §3, **FS hold threshold = 1 s**.
- **GAIN display scale:** internal `input_gain` range vs. an amp-style 0–10 display label —
  resolve during UI implementation (display mapping only, no DSP impact).
- **QSPI persistence robustness** (§6a): sector read-modify-write, RAM-resident save
  routine, and the `BOOT_QSPI` mode-switch guard are the highest-risk items — the plan
  should sequence and verify them carefully on hardware.
