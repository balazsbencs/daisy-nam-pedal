# Pedal EQ — Plan A: Audio & Data Layer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 3-band post-IR tone EQ (shelf/peak/shelf) with per-preset gains & frequencies, store it in the preset format, run it real-time-safely in the audio engine, and persist edits to QSPI flash.

**Architecture:** A self-contained `Eq3` DSP block (3 RBJ biquads) lives inside `AudioEngine`, after the IR stage. Coefficients are computed in the main loop and published to the audio ISR via a double-buffered set with an atomic index; filter state stays ISR-side. `NamPreset` gains six new fields (3 gains, 3 frequencies); `PresetManager` applies them and tolerates older short blobs. `QspiStorage` gains an XIP-safe write-back so a saved preset survives power-off.

**Tech Stack:** C++17 (gnu++17), libDaisy/DaisySP, ARM Cortex-M7 (STM32H750, 480 MHz). Host tests: clang++ with ASan/UBSan, no framework (`CHECK` macro). Python 3 packer.

## Global Constraints

- `NamPreset` is `__attribute__((packed))` and MUST stay byte-identical to the Python `PRESET_FMT` in `tools/build_data_image.py`. The `static_assert(sizeof(NamPreset) == …)` in `data_format.h` is the guard — update it to the new size.
- New preset fields are **appended after** the existing ones; existing field offsets MUST NOT change (backward compat with old blobs).
- EQ gain range: **−12.0 … +12.0 dB**. Frequency defaults: **Bass 100 Hz, Mid 750 Hz (Q 0.7), Treble 4000 Hz**. A stored frequency is always `> 0`; `freq == 0` means "field absent → use default".
- Audio block: **48 samples @ 48 kHz** (1 ms budget). No heap allocation, no math, and no blocking in the audio callback. Coefficient recompute happens only in the main loop.
- Preset blobs are **4096-byte sector-aligned** in the partition (`NAM_DATA_SECTOR_SIZE`), so a write-back erases only the preset's own sector(s).
- **This tree is not a git repo.** Treat each "Commit / checkpoint" step as a gate: run the full host suite (`cd tests && make run`) and confirm `0 failed` before moving on. Initialize git first if you want real commits.
- Host tests live in `tests/`, built with `cd tests && make` and run with `make run`.

---

### Task A1: Add EQ fields to the preset format

**Files:**
- Modify: `data_format.h:87-105` (NamPreset struct + static_assert)
- Test: `tests/test_data_format.cpp`

**Interfaces:**
- Produces: `NamPreset` with new members `float eq_bass_gain, eq_mid_gain, eq_treble_gain, eq_bass_freq, eq_mid_freq, eq_treble_freq;` (all appended after `pad[3]`). New `sizeof(NamPreset) == 98`.

- [ ] **Step 1: Write the failing test** — append to `tests/test_data_format.cpp` (inside `main`, before `test_summary`):

```cpp
    // EQ fields appended; struct grows by 6 floats (24 bytes) to 98.
    CHECK_EQ(sizeof(NamPreset), 98u);
    {
        NamPreset p{};
        p.eq_bass_gain = -3.0f; p.eq_mid_gain = 2.5f; p.eq_treble_gain = 4.0f;
        p.eq_bass_freq = 120.0f; p.eq_mid_freq = 800.0f; p.eq_treble_freq = 3500.0f;
        // Offsets of legacy fields must be unchanged.
        CHECK_EQ(offsetof(NamPreset, input_gain), 62u);
        CHECK_EQ(offsetof(NamPreset, bypass), 70u);
        // EQ block starts right after the 74-byte legacy record.
        CHECK_EQ(offsetof(NamPreset, eq_bass_gain), 74u);
    }
```

Ensure `#include <cstddef>` (for `offsetof`) is present at the top of the file; add it if missing.

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests && make test_data_format && ./test_data_format`
Expected: FAIL — `sizeof(NamPreset)` is still 74 and the `static_assert` in the header still says 74 (compile error or runtime CHECK fail).

- [ ] **Step 3: Implement** — edit `data_format.h`. Replace the struct tail and assert:

```c
typedef struct __attribute__((packed))
{
    char    model_name[NAM_DATA_NAME_LEN]; //  0..30
    char    ir_name[NAM_DATA_NAME_LEN];    // 31..61  (empty string = IR bypassed)
    float   input_gain;                    // 62..65
    float   output_volume;                 // 66..69
    uint8_t bypass;                        // 70      (0 = active, 1 = passthrough)
    uint8_t pad[3];                        // 71..73  explicit padding
    // --- EQ block (appended; older blobs lack this — see PresetManager) ---
    float   eq_bass_gain;                  // 74..77   dB  [-12,12]
    float   eq_mid_gain;                   // 78..81   dB
    float   eq_treble_gain;                // 82..85   dB
    float   eq_bass_freq;                  // 86..89   Hz  (0 = use default)
    float   eq_mid_freq;                   // 90..93   Hz
    float   eq_treble_freq;                // 94..97   Hz
} NamPreset;
```

And update the assert:

```c
static_assert(sizeof(NamPreset) == 98, "NamPreset size mismatch — check packing vs Python PRESET_FMT");
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tests && make test_data_format && ./test_data_format`
Expected: PASS — `test_data_format: N passed, 0 failed`.

- [ ] **Step 5: Checkpoint** — `cd tests && make run` (note: `test_preset_manager`/`test_tools` may fail until A2/A5; that's expected — `test_data_format` must pass).

---

### Task A2: Update the Python packer and presets.json

**Files:**
- Modify: `tools/build_data_image.py:42` (PRESET_FMT) and the preset-packing function
- Modify: `data/presets.json` (add EQ defaults to each preset)
- Test: `tests/test_tools.py`

**Interfaces:**
- Produces: `PRESET_FMT = "<31s31sffB3x6f"` (98 bytes); each preset dict may carry `eq` keys.

- [ ] **Step 1: Write the failing test** — add to `tests/test_tools.py` (follow the file's existing test style; if it uses `unittest`, add a method, else add an `assert` block in its `main`):

```python
import struct
PRESET_FMT = "<31s31sffB3x6f"
assert struct.calcsize(PRESET_FMT) == 98, "PRESET_FMT must be 98 bytes"
# round-trip a preset with EQ
packed = struct.pack(PRESET_FMT, b"amp\x00", b"cab\x00", 1.0, 0.8, 0,
                     -3.0, 2.5, 4.0, 120.0, 800.0, 3500.0)
fields = struct.unpack(PRESET_FMT, packed)
assert abs(fields[5] - (-3.0)) < 1e-6   # eq_bass_gain
assert abs(fields[10] - 3500.0) < 1e-3  # eq_treble_freq
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests && python3 test_tools.py`
Expected: FAIL — `AssertionError` on the 98-byte check (current format is 74).

- [ ] **Step 3: Implement** — in `tools/build_data_image.py`:

```python
PRESET_FMT = "<31s31sffB3x6f"  # +eq_bass/mid/treble gain, +bass/mid/treble freq -> 98
```

Find the function that packs a preset (it currently calls `struct.pack(PRESET_FMT, model, ir, in_gain, out_vol, bypass)`). Extend it to read an optional `eq` object with defaults:

```python
def pack_preset(p):
    eq = p.get("eq", {})
    return struct.pack(
        PRESET_FMT,
        encode_name(p.get("model", "")),
        encode_name(p.get("ir", "")),
        float(p.get("input_gain", 1.0)),
        float(p.get("output_volume", 0.85)),
        1 if p.get("bypass", False) else 0,
        float(eq.get("bass_gain", 0.0)),
        float(eq.get("mid_gain", 0.0)),
        float(eq.get("treble_gain", 0.0)),
        float(eq.get("bass_freq", 100.0)),
        float(eq.get("mid_freq", 750.0)),
        float(eq.get("treble_freq", 4000.0)),
    )
```

(Adapt names to match the file's existing preset-packing code — keep its existing key handling, only add the six EQ values. `encode_name` already exists.)

- [ ] **Step 4: Add EQ to `data/presets.json`** — add an `"eq"` block to at least one preset, e.g.:

```json
{
  "name": "Crunch",
  "model": "marshall",
  "ir": "",
  "input_gain": 1.0,
  "output_volume": 0.85,
  "bypass": false,
  "eq": { "bass_gain": 2.0, "mid_gain": -1.5, "treble_gain": 1.0,
          "bass_freq": 100, "mid_freq": 750, "treble_freq": 4000 }
}
```

(Presets without an `"eq"` block still pack, getting flat-gain / default-freq values.)

- [ ] **Step 5: Run test to verify it passes**

Run: `cd tests && python3 test_tools.py`
Expected: PASS (no AssertionError; existing tests still pass).

- [ ] **Step 6: Rebuild the data image**

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make data-image`
Expected: regenerates `data_image.bin` with 98-byte preset records, no errors.

- [ ] **Step 7: Checkpoint** — `python3 tools/inspect_data_image.py data_image.bin` shows the preset entries with the new length.

---

### Task A3: Eq3 DSP component (3 RBJ biquads, RT-safe coeff swap)

**Files:**
- Create: `Eq3.h`, `Eq3.cpp`
- Create: `tests/test_eq3.cpp`
- Modify: `tests/Makefile` (add `test_eq3` target + `EQ_SRCS`)

**Interfaces:**
- Produces:
  - `enum class Eq3::Band { Bass = 0, Mid = 1, Treble = 2 };`
  - `void Eq3::Reset(float sample_rate);` — clears state, sets all bands flat (0 dB at default freqs), call from main loop before audio starts.
  - `void Eq3::SetBand(Band b, float gain_db, float freq_hz);` — main-loop only; recomputes that band's coeffs and atomically publishes the full set.
  - `void Eq3::Process(float* buf, size_t n);` — ISR only; in-place, reads the active coeff set.
  - `float Eq3::GetGainDb(Band b) const;` / `float Eq3::GetFreq(Band b) const;` — main-loop read-back of the last set values (for tests and the UI).

- [ ] **Step 1: Write the failing test** — create `tests/test_eq3.cpp`:

```cpp
#include "Eq3.h"
#include "test_harness.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Process a steady sine of `freq` through `eq`, return output/input RMS in dB
// (after discarding a settling transient).
static float band_gain_db(Eq3& eq, float fs, float freq)
{
    const int total = 8192, skip = 4096;
    double in_sq = 0, out_sq = 0;
    std::vector<float> buf(total);
    for (int i = 0; i < total; ++i)
        buf[i] = std::sin(2.0 * M_PI * freq * i / fs);
    std::vector<float> dry = buf;
    eq.Process(buf.data(), buf.size());
    for (int i = skip; i < total; ++i) { in_sq += dry[i]*dry[i]; out_sq += buf[i]*buf[i]; }
    return 10.0f * std::log10(out_sq / in_sq);
}

int main()
{
    const float fs = 48000.0f;

    // Flat EQ is transparent.
    {
        Eq3 eq; eq.Reset(fs);
        std::vector<float> x(256); for (size_t i=0;i<x.size();++i) x[i]=std::sin(0.1f*i);
        std::vector<float> y = x; eq.Process(y.data(), y.size());
        float err = 0; for (size_t i=0;i<x.size();++i) err = std::max(err, std::fabs(x[i]-y[i]));
        CHECK(err < 1e-3f);
    }
    // Mid peak +6 dB at its center frequency.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Mid, 6.0f, 750.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 750.0f) - 6.0f) < 1.0f);
    }
    // Mid cut −6 dB at center.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Mid, -6.0f, 750.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 750.0f) - (-6.0f)) < 1.0f);
    }
    // Bass low-shelf +6 dB: well below corner ≈ +6, well above ≈ 0.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Bass, 6.0f, 200.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 40.0f) - 6.0f) < 1.5f);
        CHECK(std::fabs(band_gain_db(eq, fs, 6000.0f)) < 1.0f);
    }
    // Treble high-shelf +6 dB: well above corner ≈ +6, well below ≈ 0.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Treble, 6.0f, 3000.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 12000.0f) - 6.0f) < 1.5f);
        CHECK(std::fabs(band_gain_db(eq, fs, 200.0f)) < 1.0f);
    }
    return test_summary("test_eq3");
}
```

- [ ] **Step 2: Create `Eq3.h`:**

```cpp
// Eq3.h — 3-band tone EQ (low shelf / peaking / high shelf), RT-safe.
//
// SetBand() runs in the main loop and publishes coefficients to the audio ISR
// via a double-buffered set + atomic index. Process() runs in the ISR and only
// ever reads the active set. Filter state (z1/z2) is ISR-side and never swapped.
#pragma once
#include <atomic>
#include <stddef.h>

class Eq3
{
public:
    enum class Band { Bass = 0, Mid = 1, Treble = 2 };

    void Reset(float sample_rate);                       // main loop, pre-audio
    void SetBand(Band b, float gain_db, float freq_hz);  // main loop
    void Process(float* buf, size_t n);                  // audio ISR, in-place

    // Read-back of the last set values (main loop / UI). Not used by the ISR.
    float GetGainDb(Band b) const { return gain_db_[static_cast<int>(b)]; }
    float GetFreq(Band b)   const { return freq_[static_cast<int>(b)]; }

private:
    struct BiquadCoeffs { float b0, b1, b2, a1, a2; };   // normalized (a0 = 1)
    struct BiquadState  { float z1, z2; };

    static BiquadCoeffs MakeLowShelf (float fs, float f0, float gain_db);
    static BiquadCoeffs MakePeaking  (float fs, float f0, float gain_db, float q);
    static BiquadCoeffs MakeHighShelf(float fs, float f0, float gain_db);

    void Publish();  // copy staged_ -> inactive buffer, flip index

    float sample_rate_ = 48000.0f;
    static constexpr float kMidQ = 0.7f;

    BiquadCoeffs staged_[3] = {};        // main-loop working copy (all 3 bands)
    BiquadCoeffs active_[2][3] = {};     // double buffer read by ISR
    std::atomic<int> idx_{0};            // which active_ row the ISR reads
    BiquadState state_[3] = {};          // ISR-side filter memory

    float gain_db_[3] = {0.0f, 0.0f, 0.0f};        // last set, for read-back
    float freq_[3]    = {100.0f, 750.0f, 4000.0f}; // last set, for read-back
};
```

- [ ] **Step 3: Create `Eq3.cpp`** (RBJ Audio-EQ-Cookbook coefficients):

```cpp
#include "Eq3.h"
#include <cmath>
#include <cstring>

void Eq3::Reset(float sample_rate)
{
    sample_rate_ = sample_rate;
    for (int b = 0; b < 3; ++b) state_[b] = {0.0f, 0.0f};
    gain_db_[0] = gain_db_[1] = gain_db_[2] = 0.0f;
    freq_[0] = 100.0f; freq_[1] = 750.0f; freq_[2] = 4000.0f;
    staged_[0] = MakeLowShelf (sample_rate_, 100.0f,  0.0f);
    staged_[1] = MakePeaking  (sample_rate_, 750.0f,  0.0f, kMidQ);
    staged_[2] = MakeHighShelf(sample_rate_, 4000.0f, 0.0f);
    active_[0][0] = active_[1][0] = staged_[0];
    active_[0][1] = active_[1][1] = staged_[1];
    active_[0][2] = active_[1][2] = staged_[2];
    idx_.store(0, std::memory_order_release);
}

void Eq3::SetBand(Band b, float gain_db, float freq_hz)
{
    int i = static_cast<int>(b);
    gain_db_[i] = gain_db;
    freq_[i]    = freq_hz;
    switch (b)
    {
    case Band::Bass:   staged_[i] = MakeLowShelf (sample_rate_, freq_hz, gain_db); break;
    case Band::Mid:    staged_[i] = MakePeaking  (sample_rate_, freq_hz, gain_db, kMidQ); break;
    case Band::Treble: staged_[i] = MakeHighShelf(sample_rate_, freq_hz, gain_db); break;
    }
    Publish();
}

void Eq3::Publish()
{
    int next = 1 - idx_.load(std::memory_order_relaxed);
    active_[next][0] = staged_[0];
    active_[next][1] = staged_[1];
    active_[next][2] = staged_[2];
    idx_.store(next, std::memory_order_release);
}

void Eq3::Process(float* buf, size_t n)
{
    int i = idx_.load(std::memory_order_acquire);
    const BiquadCoeffs* c = active_[i];
    for (size_t s = 0; s < n; ++s)
    {
        float x = buf[s];
        for (int b = 0; b < 3; ++b)
        {
            // Transposed Direct Form II.
            float y = c[b].b0 * x + state_[b].z1;
            state_[b].z1 = c[b].b1 * x - c[b].a1 * y + state_[b].z2;
            state_[b].z2 = c[b].b2 * x - c[b].a2 * y;
            x = y;
        }
        buf[s] = x;
    }
}

Eq3::BiquadCoeffs Eq3::MakePeaking(float fs, float f0, float gain_db, float q)
{
    float A    = std::pow(10.0f, gain_db / 40.0f);
    float w0   = 2.0f * static_cast<float>(M_PI) * f0 / fs;
    float cw   = std::cos(w0), sw = std::sin(w0);
    float alpha = sw / (2.0f * q);
    float a0 = 1.0f + alpha / A;
    BiquadCoeffs c;
    c.b0 = (1.0f + alpha * A) / a0;
    c.b1 = (-2.0f * cw)       / a0;
    c.b2 = (1.0f - alpha * A) / a0;
    c.a1 = (-2.0f * cw)       / a0;
    c.a2 = (1.0f - alpha / A) / a0;
    return c;
}

Eq3::BiquadCoeffs Eq3::MakeLowShelf(float fs, float f0, float gain_db)
{
    float A    = std::pow(10.0f, gain_db / 40.0f);
    float w0   = 2.0f * static_cast<float>(M_PI) * f0 / fs;
    float cw   = std::cos(w0), sw = std::sin(w0);
    float alpha = sw / 2.0f * std::sqrt(2.0f);          // fixed shelf shape (S=1)
    float ap1 = A + 1.0f, am1 = A - 1.0f, ta = 2.0f * std::sqrt(A) * alpha;
    float a0 =  ap1 + am1 * cw + ta;
    BiquadCoeffs c;
    c.b0 =  A * (ap1 - am1 * cw + ta) / a0;
    c.b1 =  2.0f * A * (am1 - ap1 * cw) / a0;
    c.b2 =  A * (ap1 - am1 * cw - ta) / a0;
    c.a1 = -2.0f * (am1 + ap1 * cw) / a0;
    c.a2 =  (ap1 + am1 * cw - ta) / a0;
    return c;
}

Eq3::BiquadCoeffs Eq3::MakeHighShelf(float fs, float f0, float gain_db)
{
    float A    = std::pow(10.0f, gain_db / 40.0f);
    float w0   = 2.0f * static_cast<float>(M_PI) * f0 / fs;
    float cw   = std::cos(w0), sw = std::sin(w0);
    float alpha = sw / 2.0f * std::sqrt(2.0f);
    float ap1 = A + 1.0f, am1 = A - 1.0f, ta = 2.0f * std::sqrt(A) * alpha;
    float a0 =  ap1 - am1 * cw + ta;
    BiquadCoeffs c;
    c.b0 =  A * (ap1 + am1 * cw + ta) / a0;
    c.b1 = -2.0f * A * (am1 + ap1 * cw) / a0;
    c.b2 =  A * (ap1 + am1 * cw - ta) / a0;
    c.a1 =  2.0f * (am1 - ap1 * cw) / a0;
    c.a2 =  (ap1 - am1 * cw - ta) / a0;
    return c;
}
```

- [ ] **Step 4: Wire `tests/Makefile`** — add near the other `*_SRCS`:

```make
EQ_SRCS = ../Eq3.cpp
```

Add `test_eq3` to `BINARIES`, add a target:

```make
# EQ magnitude response
test_eq3: test_eq3.cpp $(EQ_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

And add to `run:` after the data_format line:

```make
	@echo "=== test_eq3 ==="
	./test_eq3
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd tests && make test_eq3 && ./test_eq3`
Expected: PASS — `test_eq3: N passed, 0 failed`. (If a shelf assertion is borderline, the tolerance bands in the test are intentionally ±1–1.5 dB; the math should land well inside.)

- [ ] **Step 6: Checkpoint** — `cd tests && make run` (test_eq3 + test_data_format green).

---

### Task A4: Integrate Eq3 into AudioEngine

**Files:**
- Modify: `AudioEngine.h` (add Eq3 member + setters), `AudioEngine.cpp` (Process chain + Init)
- Modify: `tests/Makefile` (`AUDIO_SRCS` must also compile `../Eq3.cpp`)
- Test: extend `tests/test_preset_manager.cpp` OR add `tests/test_audio_engine.cpp`

**Interfaces:**
- Consumes: `Eq3` from Task A3.
- Produces on `AudioEngine`:
  - `void SetEqBand(Eq3::Band b, float gain_db, float freq_hz);` (main loop → forwards to `eq_.SetBand`)
  - `float GetEqGain(Eq3::Band b) const;` / `float GetEqFreq(Eq3::Band b) const;` (read-back for tests + UI)
  - EQ runs in `Process` after IR, before output volume.
  - `Init` calls `eq_.Reset(sample_rate)`.

- [ ] **Step 1: Write the failing test** — add `tests/test_audio_engine.cpp`:

```cpp
#include "AudioEngine.h"
#include "test_harness.h"
#include <cmath>
#include <vector>

int main()
{
    AudioEngine eng;
    eng.Init(48, 48000.0f);
    eng.SetBypass(false);
    eng.SetInputGain(1.0f);
    eng.SetOutputVol(1.0f);
    // No model, no IR -> EQ is the only processing. Boost mid +12 dB @ 750 Hz.
    eng.SetEqBand(Eq3::Band::Mid, 12.0f, 750.0f);

    const float fs = 48000.0f; const int N = 48; const int blocks = 200;
    double in_sq = 0, out_sq = 0; int idx = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i, ++idx) in[i] = std::sin(2.0*M_PI*750.0*idx/fs);
        eng.Process(in.data(), out.data(), N);
        if (blk >= 100) for (int i=0;i<N;++i){ in_sq+=in[i]*in[i]; out_sq+=out[i]*out[i]; }
    }
    float g = 10.0f * std::log10(out_sq / in_sq);
    CHECK(g > 9.0f);                 // ~+12 dB, allow margin
    return test_summary("test_audio_engine");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests && make test_audio_engine 2>&1 | head`
Expected: FAIL to compile — `SetEqBand` / `Eq3` not declared in `AudioEngine`.

- [ ] **Step 3: Implement `AudioEngine.h`** — add include, member, setter:

```cpp
#include "Eq3.h"
```
Add to the public setters block:
```cpp
    // Update one EQ band (main loop only). gain in dB, freq in Hz.
    void  SetEqBand(Eq3::Band b, float gain_db, float freq_hz) { eq_.SetBand(b, gain_db, freq_hz); }
    float GetEqGain(Eq3::Band b) const { return eq_.GetGainDb(b); }
    float GetEqFreq(Eq3::Band b) const { return eq_.GetFreq(b); }
```
Add to the private members:
```cpp
    Eq3 eq_;
```

- [ ] **Step 4: Implement `AudioEngine.cpp`** — in `Init`, after setting members:

```cpp
    eq_.Reset(sample_rate);
```
In `Process`, insert the EQ stage **after** the IR block and **before** the output-volume loop:

```cpp
    // IR convolution stage.
    IIRConvolver* ir = active_ir_.load();
    if (ir)
        ir->Process(scratch_out_, scratch_out_, frames);

    // Tone EQ stage (post-IR).
    eq_.Process(scratch_out_, frames);

    // Apply output volume and write result.
    for (size_t i = 0; i < frames; ++i)
        out[i] = scratch_out_[i] * vol;
```

- [ ] **Step 5: Wire test build** — in `tests/Makefile`, make `AUDIO_SRCS` pull in Eq3 so every binary that links AudioEngine also gets it:

```make
AUDIO_SRCS = ../AudioEngine.cpp ../Eq3.cpp
```

Add `test_audio_engine` to `BINARIES` and a target:

```make
test_audio_engine: test_audio_engine.cpp $(AUDIO_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@
```
Add to `run:`:
```make
	@echo "=== test_audio_engine ==="
	./test_audio_engine
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd tests && make test_audio_engine && ./test_audio_engine`
Expected: PASS — measured gain at 750 Hz is ~+12 dB (`> 9 dB`).

- [ ] **Step 7: Checkpoint** — `cd tests && make run`; `test_audio_engine`, `test_eq3`, `test_data_format` all green.

---

### Task A5: Apply EQ in PresetManager + backward-compat load

**Files:**
- Modify: `PresetManager.cpp` (Init load path; ApplyPreset)
- Test: `tests/test_preset_manager.cpp`

**Interfaces:**
- Consumes: `AudioEngine::SetEqBand` / `GetEqGain` / `GetEqFreq` (Task A4), `PresetManager::ApplyPreset` (existing public method), `NamPreset` EQ fields.
- Produces: after `ApplyPreset`, the engine's three EQ bands reflect the preset (with default freqs substituted when a stored freq is `0`). Short legacy blobs load as flat EQ.

> Note: `test_preset_manager` uses the **real** `AudioEngine` (not `FakeAudioEngine`), so we assert through `AudioEngine::GetEqGain/GetEqFreq`. An empty `model_name`/`ir_name` makes `ApplyPreset` skip model/IR loading, so an uninitialized `QspiStorage`/`ModelManager` are fine as arguments.

- [ ] **Step 1: Write the failing test** — add `#include <cmath>` at the top of `tests/test_preset_manager.cpp` if absent, then add this test body inside `main`:

```cpp
    // ApplyPreset forwards explicit EQ values to the engine.
    {
        AudioEngine engine; engine.Init(48, 48000.0f);
        QspiStorage storage;   // not Init'd; empty model/IR names skip those paths
        ModelManager models;
        PresetManager pm;
        NamPreset p{};
        p.input_gain = 1.0f; p.output_volume = 0.8f; p.bypass = 0;
        p.eq_bass_gain = 3.0f; p.eq_mid_gain = -2.0f; p.eq_treble_gain = 1.0f;
        p.eq_bass_freq = 120.0f; p.eq_mid_freq = 800.0f; p.eq_treble_freq = 3500.0f;
        pm.ApplyPreset(p, engine, storage, models, 48000.0f, 48);
        CHECK(std::fabs(engine.GetEqGain(Eq3::Band::Bass) - 3.0f)  < 1e-6);
        CHECK(std::fabs(engine.GetEqGain(Eq3::Band::Mid)  - (-2.0f)) < 1e-6);
        CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Mid)  - 800.0f) < 1e-3);
    }
    // Zeroed freq (legacy/short blob → memset to 0) falls back to defaults.
    {
        AudioEngine engine; engine.Init(48, 48000.0f);
        QspiStorage storage; ModelManager models; PresetManager pm;
        NamPreset p{};   // all-zero EQ
        pm.ApplyPreset(p, engine, storage, models, 48000.0f, 48);
        CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Bass)   - 100.0f)  < 1e-3);
        CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Mid)    - 750.0f)  < 1e-3);
        CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Treble) - 4000.0f) < 1e-3);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests && make test_preset_manager && ./test_preset_manager`
Expected: FAIL — `GetEqGain`/`GetEqFreq` return defaults (0 dB / 100·750·4000) because `ApplyPreset` doesn't forward EQ yet, so the first block's gain/freq CHECKs fail.

- [ ] **Step 3: Implement the freq-default + apply logic in `PresetManager.cpp`** — add a helper and call it from `ApplyPreset` (after the input/output/bypass setters):

```cpp
// File-local: substitute default frequency when a stored value is absent (0).
static inline float eq_freq_or(float stored, float dflt) { return stored > 0.0f ? stored : dflt; }

// inside ApplyPreset, after engine.SetOutputVol(...):
    engine.SetEqBand(Eq3::Band::Bass,   p.eq_bass_gain,   eq_freq_or(p.eq_bass_freq,   100.0f));
    engine.SetEqBand(Eq3::Band::Mid,    p.eq_mid_gain,    eq_freq_or(p.eq_mid_freq,    750.0f));
    engine.SetEqBand(Eq3::Band::Treble, p.eq_treble_gain, eq_freq_or(p.eq_treble_freq, 4000.0f));
```

Add `#include "Eq3.h"` to `PresetManager.cpp` if not already pulled via `AudioEngine.h`.

- [ ] **Step 4: Backward-compat blob load in `PresetManager::Init`** — replace the strict length check + copy:

```cpp
        const uint8_t* blob = storage.BlobPtr(e);
        if (!blob || e->length < NAM_LEGACY_PRESET_SIZE)   // 74 = legacy minimum
            continue;
        memset(&presets_[count_], 0, sizeof(NamPreset));   // EQ defaults to 0 -> freq_or() supplies defaults
        size_t n = e->length < sizeof(NamPreset) ? e->length : sizeof(NamPreset);
        memcpy(&presets_[count_], blob, n);
```

Define near the top of the file: `static constexpr uint32_t NAM_LEGACY_PRESET_SIZE = 74;`

Also set EQ defaults in the **synthesized** preset path (the `count_ == 0` per-model loop) so generated presets are flat:

```cpp
            p.eq_bass_gain = p.eq_mid_gain = p.eq_treble_gain = 0.0f;
            p.eq_bass_freq = 100.0f; p.eq_mid_freq = 750.0f; p.eq_treble_freq = 4000.0f;
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd tests && make test_preset_manager && ./test_preset_manager`
Expected: PASS — explicit EQ forwarded; zeroed freqs become 100/750/4000.

- [ ] **Step 6: Checkpoint** — `cd tests && make run`; all host suites green.

---

### Task A6: QSPI write-back (serialization helper + XIP-safe write)

**Files:**
- Modify: `QspiStorage.h`, `QspiStorage.cpp`
- Test: `tests/test_qspi_storage.cpp`

**Interfaces:**
- Produces:
  - Pure, host-testable: `static uint32_t QspiStorage::BlobFlashOffset(const NamDataEntry* e)` → `NAM_DATA_PARTITION_OFFSET + e->offset` (QSPI-relative address passed to `QSPIHandle`).
  - Hardware: `Status QspiStorage::WritePreset(const NamDataEntry* e, const NamPreset& p)` — erases the preset's sector(s) and programs the updated record. Returns `OK` / an error status. **Caller must have stopped audio and disabled QSPI-resident IRQs** (orchestration lives in Plan B / `main.cpp`).

- [ ] **Step 1: Write the failing test** — add to `tests/test_qspi_storage.cpp`:

```cpp
    // Flash offset is partition offset + entry offset (QSPI-relative).
    {
        NamDataEntry e{};
        e.type = NAM_ENTRY_PRESET;
        e.offset = 0x5000;           // some 4 KiB-aligned blob offset
        e.length = sizeof(NamPreset);
        CHECK_EQ(QspiStorage::BlobFlashOffset(&e), NAM_DATA_PARTITION_OFFSET + 0x5000u);
        // Blob sits at a sector boundary, so the erase only touches its own sector.
        CHECK_EQ(e.offset % NAM_DATA_SECTOR_SIZE, 0u);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests && make test_qspi_storage && ./test_qspi_storage`
Expected: FAIL to compile — `BlobFlashOffset` not declared.

- [ ] **Step 3: Implement the pure helper in `QspiStorage.h`** (public, `static`, no hardware):

```cpp
    // QSPI-relative address of an entry's blob (argument to QSPIHandle Erase/Write).
    static uint32_t BlobFlashOffset(const NamDataEntry* entry)
    {
        return NAM_DATA_PARTITION_OFFSET + entry->offset;
    }
```

- [ ] **Step 4: Implement `WritePreset`** in `QspiStorage.cpp` (guarded so the host build, which lacks a real QSPI peripheral, still compiles — match how the rest of the file handles `HOST_BUILD`):

```cpp
QspiStorage::Status QspiStorage::WritePreset(const NamDataEntry* entry, const NamPreset& p)
{
    if (status_ != Status::OK || !entry) return Status::NOT_INIT;

#ifdef HOST_BUILD
    (void)p;                      // no real flash on host; serialization covered by tests
    return Status::OK;
#else
    uint32_t addr = BlobFlashOffset(entry);                 // QSPI-relative
    // Blobs are 4 KiB-aligned, so erasing this blob's sector won't disturb neighbors.
    if (qspi_.Erase(addr, addr + NAM_DATA_SECTOR_SIZE - 1) != daisy::QSPIHandle::Result::OK)
        return Status::BAD_MAGIC;                            // reuse as generic write error
    if (qspi_.Write(addr, sizeof(NamPreset),
                    reinterpret_cast<uint8_t*>(const_cast<NamPreset*>(&p))) != daisy::QSPIHandle::Result::OK)
        return Status::BAD_MAGIC;
    return Status::OK;
#endif
}
```

Declare it in `QspiStorage.h`:

```cpp
    // Erase+program one preset blob in place. HARDWARE: caller MUST stop audio and
    // ensure no QSPI-resident code/IRQ runs during the call (see plan §6a).
    Status WritePreset(const NamDataEntry* entry, const NamPreset& p);
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd tests && make test_qspi_storage && ./test_qspi_storage`
Expected: PASS.

- [ ] **Step 6: Checkpoint** — `cd tests && make run`; entire host suite green.

> **On-device note (verified in Plan B, not here):** the actual save sequence — `StopAudio()` → disable IRQs → `WritePreset` (runs the erase/program; the routine and its stack must be RAM-resident) → re-enable IRQs → `StartAudio()` — is wired in `main.cpp` under FS1-hold. This task only delivers the storage primitive + its host-testable address math.

---

### Task A7: Add Eq3 to the firmware build & confirm it compiles for ARM

**Files:**
- Modify: `Makefile` (root) — add `Eq3.cpp` to `CPP_SOURCES`

**Interfaces:** none (build wiring).

- [ ] **Step 1: Add the source** — in the root `Makefile`, add to `CPP_SOURCES` (after `AudioEngine.cpp`):

```make
  Eq3.cpp \
```

- [ ] **Step 2: Build the firmware**

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make 2>&1 | tail -20`
Expected: links cleanly to `build/NamPlatform.bin` (`Memory region … QSPIFLASH … %age Used` table prints, no errors). Eq3.o appears in the link line.

- [ ] **Step 3: Final checkpoint for Plan A**

Run: `cd tests && make run` → all suites `0 failed`; then root `make` succeeds.
Plan A deliverable: firmware with a working post-IR EQ driven by preset data, plus the QSPI write-back primitive — ready for Plan B (controls + UI) to expose it.

---

## Self-Review notes

- **Spec coverage:** §3 EQ DSP → A3; coeff swap → A3 (atomic double buffer); §4 preset model + backward compat → A1/A2/A5; §6a persistence primitive → A6 (orchestration deferred to Plan B, explicitly); signal-chain placement → A4; data image/json → A2; tests → A1/A3/A4/A5/A6. **§5 controls, §6 live-edit/dirty, §7 screens, §8 rendering are Plan B** (out of scope here, by design).
- **Types:** `Eq3::Band`, `Eq3::Reset/SetBand/Process`, `AudioEngine::SetEqBand`, `QspiStorage::BlobFlashOffset/WritePreset`, `NamPreset` EQ fields — consistent across A1–A7.
- **No placeholders:** every code/test step contains the literal code or command.
