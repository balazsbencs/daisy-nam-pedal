# Pre/Post Effects Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add efficient per-preset pre effects (noise gate, compressor) and post effects (delay after EQ), with firmware and desktop app preset compatibility tests.

**Architecture:** Add one small firmware DSP module for the three effects, wire it into `AudioEngine` around the existing NAM/IR/EQ chain, and append effect settings to the packed `NamPreset` record while preserving 74-byte and 98-byte legacy preset compatibility. Extend the desktop app's Rust/TypeScript preset schema, image packer, and preset editor so app-built QSPI images match firmware byte-for-byte.

**Tech Stack:** C++17 firmware, host tests with `make`, Python QSPI image packer tests, Rust/Tauri backend tests with `cargo test`, React/TypeScript frontend with Vite.

---

## Ground Rules

- Do not create a worktree for this feature.
- Keep all audio-callback code allocation-free and deterministic.
- Keep new effect editing desktop-first; do not add device-side parameter editing unless the user explicitly asks later.
- Preserve legacy presets: 74-byte and 98-byte blobs must load with all new effects bypassed.
- Treat `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h` as the firmware format authority.

## File Structure

- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/PedalEffects.h`
  - Owns `NoiseGate`, `Compressor`, and `DelayLine` APIs and parameter structs.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/PedalEffects.cpp`
  - Implements cheap mono DSP algorithms.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.h`
  - Owns effect instances and exposes setters/getters used by `PresetManager`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.cpp`
  - Wires `input_gain -> gate -> compressor -> NAM -> IR -> EQ -> delay -> output_volume`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/PresetManager.cpp`
  - Applies defaults, clamps values, and forwards preset effect settings to `AudioEngine`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h`
  - Extends `NamPreset` from 98 to 138 bytes.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tools/build_data_image.py`
  - Emits 138-byte preset blobs from local `presets.json`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`
  - Adds `test_pedal_effects`.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_pedal_effects.cpp`
  - Unit tests for gate, compressor, and delay.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_audio_engine.cpp`
  - Changes EQ integration expectation and verifies delay placement.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_data_format.cpp`
  - Asserts 138-byte layout and offsets.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_preset_manager.cpp`
  - Adds legacy/full-effect preset load and apply tests.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_tools.py`
  - Updates Python format mirror and round-trip tests.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`
  - Adds `PedalEffects.cpp` to firmware build sources.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/types.rs`
  - Adds effect preset fields with serde defaults.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/image_builder.rs`
  - Packs 138-byte preset blobs and tests offsets.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/flash.rs`
  - Passes effect fields into `pack_preset_blob()`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`
  - Adds effect fields and normalization defaults.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx`
  - Adds desktop controls for pre effects and post delay.

---

### Task 1: Firmware Effect DSP Tests

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_pedal_effects.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Write the failing effect tests**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_pedal_effects.cpp`:

```cpp
#include "PedalEffects.h"
#include "test_harness.h"
#include <algorithm>
#include <cmath>
#include <vector>

static float max_abs(const std::vector<float>& v)
{
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

static void test_noise_gate_bypass_is_transparent()
{
    NoiseGate gate;
    gate.Init(48000.0f);
    gate.SetEnabled(false);
    gate.SetThresholdDb(-30.0f);

    std::vector<float> x = {0.0f, 0.1f, -0.2f, 0.3f};
    std::vector<float> y = x;
    gate.Process(y.data(), y.size());

    for (size_t i = 0; i < x.size(); ++i)
        CHECK(std::fabs(x[i] - y[i]) < 1e-7f);
}

static void test_noise_gate_reduces_signal_below_threshold()
{
    NoiseGate gate;
    gate.Init(48000.0f);
    gate.SetEnabled(true);
    gate.SetThresholdDb(-30.0f);

    std::vector<float> y(512, 0.001f);
    gate.Process(y.data(), y.size());

    CHECK(max_abs(y) < 0.0005f);
}

static void test_noise_gate_opens_above_threshold()
{
    NoiseGate gate;
    gate.Init(48000.0f);
    gate.SetEnabled(true);
    gate.SetThresholdDb(-40.0f);

    std::vector<float> y(512, 0.25f);
    gate.Process(y.data(), y.size());

    CHECK(y.back() > 0.20f);
}

static void test_compressor_bypass_is_transparent()
{
    Compressor comp;
    comp.Init(48000.0f);
    comp.SetEnabled(false);
    comp.SetParams(-18.0f, 4.0f, 5.0f, 50.0f);

    std::vector<float> x = {0.0f, 0.1f, -0.2f, 0.7f};
    std::vector<float> y = x;
    comp.Process(y.data(), y.size());

    for (size_t i = 0; i < x.size(); ++i)
        CHECK(std::fabs(x[i] - y[i]) < 1e-7f);
}

static void test_compressor_reduces_loud_signal()
{
    Compressor comp;
    comp.Init(48000.0f);
    comp.SetEnabled(true);
    comp.SetParams(-18.0f, 4.0f, 1.0f, 100.0f);

    std::vector<float> y(2048, 0.9f);
    comp.Process(y.data(), y.size());

    CHECK(y.back() < 0.65f);
    CHECK(y.back() > 0.10f);
}

static void test_delay_bypass_is_transparent()
{
    DelayLine delay;
    delay.Init(48000.0f);
    delay.SetEnabled(false);
    delay.SetParams(10.0f, 0.5f, 0.5f, 0.5f);

    std::vector<float> x = {1.0f, 0.0f, 0.0f, 0.25f};
    std::vector<float> y = x;
    delay.Process(y.data(), y.size());

    for (size_t i = 0; i < x.size(); ++i)
        CHECK(std::fabs(x[i] - y[i]) < 1e-7f);
}

static void test_delay_outputs_delayed_impulse()
{
    DelayLine delay;
    delay.Init(48000.0f);
    delay.SetEnabled(true);
    delay.SetParams(1.0f, 0.0f, 0.5f, 1.0f); // 48 samples

    std::vector<float> y(96, 0.0f);
    y[0] = 1.0f;
    delay.Process(y.data(), y.size());

    CHECK(std::fabs(y[0] - 0.5f) < 1e-5f);
    CHECK(std::fabs(y[48] - 0.5f) < 1e-5f);
}

int main()
{
    test_noise_gate_bypass_is_transparent();
    test_noise_gate_reduces_signal_below_threshold();
    test_noise_gate_opens_above_threshold();
    test_compressor_bypass_is_transparent();
    test_compressor_reduces_loud_signal();
    test_delay_bypass_is_transparent();
    test_delay_outputs_delayed_impulse();
    return test_summary("test_pedal_effects");
}
```

- [ ] **Step 2: Add the test target**

Modify `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`:

```make
EFFECT_SRCS    = ../PedalEffects.cpp
BINARIES = test_data_format test_qspi_storage test_preset_manager test_ir_loader test_eq3 test_audio_engine test_pedal_effects test_quad_encoder test_meter_fill test_real_fft_128 test_partitioned_convolver test_display_transfer test_ui_mode

test_pedal_effects: test_pedal_effects.cpp $(EFFECT_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

Add this to the `run` target after `test_audio_engine`:

```make
	@echo "=== test_pedal_effects ==="
	./test_pedal_effects
```

- [ ] **Step 3: Run the test and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_pedal_effects
```

Expected: fail because `PedalEffects.h` and `PedalEffects.cpp` do not exist.

---

### Task 2: Firmware Effect DSP Implementation

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/PedalEffects.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/PedalEffects.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`

- [ ] **Step 1: Create the DSP header**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/PedalEffects.h`:

```cpp
#pragma once
#include <stddef.h>

class NoiseGate
{
public:
    void Init(float sample_rate);
    void SetEnabled(bool enabled);
    void SetThresholdDb(float threshold_db);
    void Reset();
    void Process(float* buf, size_t n);

    bool  Enabled() const { return enabled_; }
    float ThresholdDb() const { return threshold_db_; }

private:
    float sample_rate_ = 48000.0f;
    float threshold_db_ = -70.0f;
    float threshold_lin_ = 0.000316f;
    float env_ = 0.0f;
    float gain_ = 1.0f;
    bool  enabled_ = false;
};

class Compressor
{
public:
    void Init(float sample_rate);
    void SetEnabled(bool enabled);
    void SetParams(float threshold_db, float ratio, float attack_ms, float release_ms);
    void Reset();
    void Process(float* buf, size_t n);

    bool  Enabled() const { return enabled_; }
    float ThresholdDb() const { return threshold_db_; }
    float Ratio() const { return ratio_; }
    float AttackMs() const { return attack_ms_; }
    float ReleaseMs() const { return release_ms_; }

private:
    float Coeff(float ms) const;

    float sample_rate_ = 48000.0f;
    float threshold_db_ = -18.0f;
    float threshold_lin_ = 0.125893f;
    float ratio_ = 2.0f;
    float attack_ms_ = 10.0f;
    float release_ms_ = 100.0f;
    float attack_coeff_ = 0.99791884f;
    float release_coeff_ = 0.99979168f;
    float env_ = 0.0f;
    float gain_ = 1.0f;
    bool  enabled_ = false;
};

class DelayLine
{
public:
    static constexpr size_t kMaxDelaySamples = 36000; // 750 ms at 48 kHz

    void Init(float sample_rate);
    void SetEnabled(bool enabled);
    void SetParams(float time_ms, float repeats, float mix, float tone);
    void Reset();
    void Process(float* buf, size_t n);

    bool  Enabled() const { return enabled_; }
    float TimeMs() const { return time_ms_; }
    float Repeats() const { return repeats_; }
    float Mix() const { return mix_; }
    float Tone() const { return tone_; }

private:
    float sample_rate_ = 48000.0f;
    float time_ms_ = 350.0f;
    float repeats_ = 0.25f;
    float mix_ = 0.18f;
    float tone_ = 0.5f;
    float tone_coeff_ = 0.5f;
    float tone_state_ = 0.0f;
    size_t delay_samples_ = 16800;
    size_t write_idx_ = 0;
    bool enabled_ = false;
    float buffer_[kMaxDelaySamples] = {};
};
```

- [ ] **Step 2: Implement the DSP classes**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/PedalEffects.cpp`:

```cpp
#include "PedalEffects.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float db_to_lin(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

void NoiseGate::Init(float sample_rate)
{
    sample_rate_ = sample_rate > 1000.0f ? sample_rate : 48000.0f;
    SetThresholdDb(threshold_db_);
    Reset();
}

void NoiseGate::SetEnabled(bool enabled) { enabled_ = enabled; }

void NoiseGate::SetThresholdDb(float threshold_db)
{
    threshold_db_ = clampf(threshold_db, -90.0f, -20.0f);
    threshold_lin_ = db_to_lin(threshold_db_);
}

void NoiseGate::Reset()
{
    env_ = 0.0f;
    gain_ = 1.0f;
}

void NoiseGate::Process(float* buf, size_t n)
{
    if (!enabled_) return;

    const float env_attack = 0.05f;
    const float env_release = 0.002f;
    const float gain_attack = 0.02f;
    const float gain_release = 0.0008f;
    const float open_threshold = threshold_lin_ * 1.41254f; // +3 dB

    for (size_t i = 0; i < n; ++i)
    {
        float x = buf[i];
        float ax = std::fabs(x);
        float ec = ax > env_ ? env_attack : env_release;
        env_ += ec * (ax - env_);

        float target = env_ >= open_threshold ? 1.0f : (env_ <= threshold_lin_ ? 0.0f : gain_);
        float gc = target > gain_ ? gain_attack : gain_release;
        gain_ += gc * (target - gain_);
        buf[i] = x * gain_;
    }
}

void Compressor::Init(float sample_rate)
{
    sample_rate_ = sample_rate > 1000.0f ? sample_rate : 48000.0f;
    SetParams(threshold_db_, ratio_, attack_ms_, release_ms_);
    Reset();
}

float Compressor::Coeff(float ms) const
{
    ms = clampf(ms, 0.1f, 1000.0f);
    return std::exp(-1.0f / (0.001f * ms * sample_rate_));
}

void Compressor::SetEnabled(bool enabled) { enabled_ = enabled; }

void Compressor::SetParams(float threshold_db, float ratio, float attack_ms, float release_ms)
{
    threshold_db_ = clampf(threshold_db, -60.0f, 0.0f);
    threshold_lin_ = db_to_lin(threshold_db_);
    ratio_ = clampf(ratio, 1.0f, 20.0f);
    attack_ms_ = clampf(attack_ms, 0.1f, 200.0f);
    release_ms_ = clampf(release_ms, 5.0f, 1000.0f);
    attack_coeff_ = Coeff(attack_ms_);
    release_coeff_ = Coeff(release_ms_);
}

void Compressor::Reset()
{
    env_ = 0.0f;
    gain_ = 1.0f;
}

void Compressor::Process(float* buf, size_t n)
{
    if (!enabled_) return;

    for (size_t i = 0; i < n; ++i)
    {
        float x = buf[i];
        float ax = std::fabs(x);
        float ec = ax > env_ ? attack_coeff_ : release_coeff_;
        env_ = (ec * env_) + ((1.0f - ec) * ax);

        float target_gain = 1.0f;
        if (env_ > threshold_lin_ && env_ > 1e-9f)
        {
            float compressed = threshold_lin_ + (env_ - threshold_lin_) / ratio_;
            target_gain = compressed / env_;
        }

        float gc = target_gain < gain_ ? attack_coeff_ : release_coeff_;
        gain_ = (gc * gain_) + ((1.0f - gc) * target_gain);
        buf[i] = x * gain_;
    }
}

void DelayLine::Init(float sample_rate)
{
    sample_rate_ = sample_rate > 1000.0f ? sample_rate : 48000.0f;
    SetParams(time_ms_, repeats_, mix_, tone_);
    Reset();
}

void DelayLine::SetEnabled(bool enabled)
{
    if (enabled_ != enabled && !enabled) Reset();
    enabled_ = enabled;
}

void DelayLine::SetParams(float time_ms, float repeats, float mix, float tone)
{
    time_ms_ = clampf(time_ms, 1.0f, 750.0f);
    repeats_ = clampf(repeats, 0.0f, 0.95f);
    mix_ = clampf(mix, 0.0f, 1.0f);
    tone_ = clampf(tone, 0.0f, 1.0f);
    tone_coeff_ = 0.05f + tone_ * 0.90f;

    size_t samples = static_cast<size_t>((time_ms_ * sample_rate_ * 0.001f) + 0.5f);
    delay_samples_ = std::max<size_t>(1, std::min(samples, kMaxDelaySamples - 1));
}

void DelayLine::Reset()
{
    std::memset(buffer_, 0, sizeof(buffer_));
    write_idx_ = 0;
    tone_state_ = 0.0f;
}

void DelayLine::Process(float* buf, size_t n)
{
    if (!enabled_) return;

    for (size_t i = 0; i < n; ++i)
    {
        size_t read_idx = write_idx_ >= delay_samples_
            ? write_idx_ - delay_samples_
            : kMaxDelaySamples + write_idx_ - delay_samples_;

        float dry = buf[i];
        float delayed = buffer_[read_idx];
        tone_state_ += tone_coeff_ * (delayed - tone_state_);
        float feedback = tone_state_ * repeats_;
        buffer_[write_idx_] = dry + feedback;

        buf[i] = dry * (1.0f - mix_) + delayed * mix_;

        write_idx_++;
        if (write_idx_ >= kMaxDelaySamples) write_idx_ = 0;
    }
}
```

- [ ] **Step 3: Add firmware source**

Modify `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile` and add `PedalEffects.cpp` next to `Eq3.cpp` in the source list.

- [ ] **Step 4: Run effect tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_pedal_effects && ./test_pedal_effects
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add PedalEffects.h PedalEffects.cpp tests/test_pedal_effects.cpp tests/Makefile Makefile
git commit -m "feat: add lightweight pedal effects"
```

---

### Task 3: Extend Firmware Preset Layout

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_data_format.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tools/build_data_image.py`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_tools.py`

- [ ] **Step 1: Update firmware layout tests first**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_data_format.cpp`, update the preset section to assert:

```cpp
CHECK_EQ(sizeof(NamPreset), 138u);
CHECK_EQ(offsetof(NamPreset, model_name),   0u);
CHECK_EQ(offsetof(NamPreset, ir_name),      31u);
CHECK_EQ(offsetof(NamPreset, input_gain),   62u);
CHECK_EQ(offsetof(NamPreset, output_volume),66u);
CHECK_EQ(offsetof(NamPreset, bypass),       70u);
CHECK_EQ(offsetof(NamPreset, eq_bass_gain), 74u);
CHECK_EQ(offsetof(NamPreset, eq_treble_freq), 94u);
CHECK_EQ(offsetof(NamPreset, noise_gate_enabled), 98u);
CHECK_EQ(offsetof(NamPreset, compressor_enabled), 99u);
CHECK_EQ(offsetof(NamPreset, delay_enabled), 100u);
CHECK_EQ(offsetof(NamPreset, noise_gate_threshold_db), 102u);
CHECK_EQ(offsetof(NamPreset, compressor_threshold_db), 106u);
CHECK_EQ(offsetof(NamPreset, compressor_ratio), 110u);
CHECK_EQ(offsetof(NamPreset, compressor_attack_ms), 114u);
CHECK_EQ(offsetof(NamPreset, compressor_release_ms), 118u);
CHECK_EQ(offsetof(NamPreset, delay_time_ms), 122u);
CHECK_EQ(offsetof(NamPreset, delay_repeats), 126u);
CHECK_EQ(offsetof(NamPreset, delay_mix), 130u);
CHECK_EQ(offsetof(NamPreset, delay_tone), 134u);
```

- [ ] **Step 2: Run layout test and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_data_format
```

Expected: fail because `NamPreset` does not have the new fields yet.

- [ ] **Step 3: Append fields to `NamPreset`**

In `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h`, keep all existing fields
unchanged and append:

```cpp
    // --- Pre/post effects block (appended; older blobs lack this) ---
    uint8_t noise_gate_enabled;       //  98      0 = bypassed, 1 = active
    uint8_t compressor_enabled;       //  99      0 = bypassed, 1 = active
    uint8_t delay_enabled;            // 100      0 = bypassed, 1 = active
    uint8_t fx_pad;                   // 101      explicit padding
    float   noise_gate_threshold_db;  // 102..105 dBFS [-90,-20]
    float   compressor_threshold_db;  // 106..109 dBFS [-60,0]
    float   compressor_ratio;         // 110..113 [1,20]
    float   compressor_attack_ms;     // 114..117 [0.1,200]
    float   compressor_release_ms;    // 118..121 [5,1000]
    float   delay_time_ms;            // 122..125 [1,750]
    float   delay_repeats;            // 126..129 [0,0.95]
    float   delay_mix;                // 130..133 [0,1]
    float   delay_tone;               // 134..137 [0,1]
```

Update the static assert:

```cpp
static_assert(sizeof(NamPreset)     == 138, "NamPreset size mismatch — check packing vs Python/Rust packers");
```

- [ ] **Step 4: Update firmware Python packer tests**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_tools.py`:

```python
PRESET_FMT  = "<31s31sffB3x6f3B1x9f"
```

Update `pack_preset()` to include:

```python
noise_gate_enabled=0, compressor_enabled=0, delay_enabled=0,
noise_gate_threshold_db=-70.0,
compressor_threshold_db=-18.0, compressor_ratio=2.0,
compressor_attack_ms=10.0, compressor_release_ms=100.0,
delay_time_ms=350.0, delay_repeats=0.25, delay_mix=0.18, delay_tone=0.5
```

Append those values to `struct.pack()`. Add assertions that `struct.calcsize(PRESET_FMT) == 138`, `raw[98] == 0`, `raw[99] == 0`, `raw[100] == 0`, and the delay mix at offset `130` round-trips.

- [ ] **Step 5: Update firmware local image packer**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tools/build_data_image.py`:

```python
PRESET_FMT = "<31s31sffB3x6f3B1x9f"
```

In `pack_preset(p)`, read:

```python
gate = p.get("noise_gate", {})
comp = p.get("compressor", {})
delay = p.get("delay", {})
```

Append to `struct.pack()`:

```python
1 if gate.get("enabled", False) else 0,
1 if comp.get("enabled", False) else 0,
1 if delay.get("enabled", False) else 0,
float(gate.get("threshold_db", -70.0)),
float(comp.get("threshold_db", -18.0)),
float(comp.get("ratio", 2.0)),
float(comp.get("attack_ms", 10.0)),
float(comp.get("release_ms", 100.0)),
float(delay.get("time_ms", 350.0)),
float(delay.get("repeats", 0.25)),
float(delay.get("mix", 0.18)),
float(delay.get("tone", 0.5)),
```

- [ ] **Step 6: Run layout and tool tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_data_format && ./test_data_format
python3 test_tools.py
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add data_format.h tools/build_data_image.py tests/test_data_format.cpp tests/test_tools.py
git commit -m "feat: extend preset layout for pre and post effects"
```

---

### Task 4: Apply Effect Settings From Presets

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/PresetManager.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_preset_manager.cpp`

- [ ] **Step 1: Add failing `PresetManager` tests**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_preset_manager.cpp`, add tests:

```cpp
static std::vector<uint8_t> make_eq_legacy_preset_blob()
{
    NamPreset p{};
    p.input_gain = 1.0f;
    p.output_volume = 0.8f;
    p.bypass = 0;
    p.eq_bass_gain = -3.0f;
    p.eq_mid_gain = 2.0f;
    p.eq_treble_gain = 4.0f;
    p.eq_bass_freq = 120.0f;
    p.eq_mid_freq = 800.0f;
    p.eq_treble_freq = 3500.0f;
    std::vector<uint8_t> buf(98);
    memcpy(buf.data(), &p, 98);
    return buf;
}

static void test_98_byte_eq_legacy_keeps_new_effects_bypassed()
{
    FakeStorage fs;
    auto blob = make_eq_legacy_preset_blob();
    fs.AddEntry(NAM_ENTRY_PRESET, "EQ Legacy", blob.data(), (uint32_t)blob.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage storage;
    storage.Init();
    ModelManager models;
    models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    const NamPreset& p = presets.ActivePreset();
    CHECK_EQ(p.noise_gate_enabled, 0u);
    CHECK_EQ(p.compressor_enabled, 0u);
    CHECK_EQ(p.delay_enabled, 0u);
}

static void test_full_effect_preset_loads_and_applies()
{
    QspiStorage storage;
    ModelManager models;
    AudioEngine engine;
    engine.Init(48, 48000.0f);
    PresetManager pm;

    NamPreset p{};
    p.input_gain = 1.0f;
    p.output_volume = 0.8f;
    p.noise_gate_enabled = 1;
    p.compressor_enabled = 1;
    p.delay_enabled = 1;
    p.noise_gate_threshold_db = -55.0f;
    p.compressor_threshold_db = -20.0f;
    p.compressor_ratio = 3.0f;
    p.compressor_attack_ms = 7.0f;
    p.compressor_release_ms = 120.0f;
    p.delay_time_ms = 250.0f;
    p.delay_repeats = 0.4f;
    p.delay_mix = 0.3f;
    p.delay_tone = 0.7f;

    pm.ApplyPreset(p, engine, storage, models, 48000.0f, 48);

    CHECK(engine.GetNoiseGateEnabled());
    CHECK(engine.GetCompressorEnabled());
    CHECK(engine.GetDelayEnabled());
    CHECK(std::fabs(engine.GetNoiseGateThresholdDb() - (-55.0f)) < 1e-5f);
    CHECK(std::fabs(engine.GetCompressorRatio() - 3.0f) < 1e-5f);
    CHECK(std::fabs(engine.GetDelayMix() - 0.3f) < 1e-5f);
}
```

Add both new test functions to `main()` before `return test_summary("preset_manager");`:

```cpp
    test_98_byte_eq_legacy_keeps_new_effects_bypassed();
    test_full_effect_preset_loads_and_applies();
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_preset_manager
```

Expected: compile failure because `AudioEngine` has no effect getters/setters.

- [ ] **Step 3: Add `AudioEngine` setters/getters**

In `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.h`, include `PedalEffects.h`,
add members:

```cpp
    NoiseGate gate_;
    Compressor compressor_;
    DelayLine delay_;
```

Add public methods:

```cpp
    void SetNoiseGate(bool enabled, float threshold_db);
    void SetCompressor(bool enabled, float threshold_db, float ratio, float attack_ms, float release_ms);
    void SetDelay(bool enabled, float time_ms, float repeats, float mix, float tone);

    bool  GetNoiseGateEnabled() const { return gate_.Enabled(); }
    float GetNoiseGateThresholdDb() const { return gate_.ThresholdDb(); }
    bool  GetCompressorEnabled() const { return compressor_.Enabled(); }
    float GetCompressorThresholdDb() const { return compressor_.ThresholdDb(); }
    float GetCompressorRatio() const { return compressor_.Ratio(); }
    float GetCompressorAttackMs() const { return compressor_.AttackMs(); }
    float GetCompressorReleaseMs() const { return compressor_.ReleaseMs(); }
    bool  GetDelayEnabled() const { return delay_.Enabled(); }
    float GetDelayTimeMs() const { return delay_.TimeMs(); }
    float GetDelayRepeats() const { return delay_.Repeats(); }
    float GetDelayMix() const { return delay_.Mix(); }
    float GetDelayTone() const { return delay_.Tone(); }
```

Implement the setters in `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.cpp`:

```cpp
void AudioEngine::SetNoiseGate(bool enabled, float threshold_db)
{
    gate_.SetThresholdDb(threshold_db);
    gate_.SetEnabled(enabled);
}

void AudioEngine::SetCompressor(bool enabled, float threshold_db, float ratio, float attack_ms, float release_ms)
{
    compressor_.SetParams(threshold_db, ratio, attack_ms, release_ms);
    compressor_.SetEnabled(enabled);
}

void AudioEngine::SetDelay(bool enabled, float time_ms, float repeats, float mix, float tone)
{
    delay_.SetParams(time_ms, repeats, mix, tone);
    delay_.SetEnabled(enabled);
}
```

- [ ] **Step 4: Initialize effects**

In `AudioEngine::Init()`:

```cpp
    gate_.Init(sample_rate);
    compressor_.Init(sample_rate);
    delay_.Init(sample_rate);
```

- [ ] **Step 5: Apply defaults in `PresetManager`**

In `/Users/bbalazs/daisy/daisy-nam-pedal/PresetManager.cpp`, add helpers:

```cpp
static inline float param_or(float stored, float dflt) { return stored > 0.0f ? stored : dflt; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
```

In `ApplyPreset()` after EQ setters:

```cpp
    engine.SetNoiseGate(
        p.noise_gate_enabled != 0,
        clampf(p.noise_gate_threshold_db == 0.0f ? -70.0f : p.noise_gate_threshold_db, -90.0f, -20.0f));
    engine.SetCompressor(
        p.compressor_enabled != 0,
        clampf(p.compressor_threshold_db == 0.0f ? -18.0f : p.compressor_threshold_db, -60.0f, 0.0f),
        clampf(param_or(p.compressor_ratio, 2.0f), 1.0f, 20.0f),
        clampf(param_or(p.compressor_attack_ms, 10.0f), 0.1f, 200.0f),
        clampf(param_or(p.compressor_release_ms, 100.0f), 5.0f, 1000.0f));
    engine.SetDelay(
        p.delay_enabled != 0,
        clampf(param_or(p.delay_time_ms, 350.0f), 1.0f, 750.0f),
        clampf(p.delay_repeats, 0.0f, 0.95f),
        clampf(p.delay_mix, 0.0f, 1.0f),
        clampf(p.delay_tone == 0.0f ? 0.5f : p.delay_tone, 0.0f, 1.0f));
```

- [ ] **Step 6: Run preset tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_preset_manager && ./test_preset_manager
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add AudioEngine.h AudioEngine.cpp PresetManager.cpp tests/test_preset_manager.cpp
git commit -m "feat: apply effect settings from presets"
```

---

### Task 5: Wire Audio Chain And Restore EQ Processing

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_audio_engine.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Replace audio-engine test expectations**

Update `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_audio_engine.cpp` so the
EQ test expects audible gain, and add a delay placement test:

```cpp
static void test_eq_is_in_audio_path()
{
    AudioEngine eng;
    eng.Init(48, 48000.0f);
    eng.SetBypass(false);
    eng.SetInputGain(1.0f);
    eng.SetOutputVol(1.0f);
    eng.SetEqBand(Eq3::Band::Mid, 12.0f, 750.0f);

    const float fs = 48000.0f;
    const int N = 48;
    const int blocks = 200;
    double in_sq = 0.0, out_sq = 0.0;
    int idx = 0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i, ++idx) in[i] = std::sin(2.0 * M_PI * 750.0 * idx / fs);
        eng.Process(in.data(), out.data(), N);
        if (blk >= 100)
            for (int i = 0; i < N; ++i) { in_sq += in[i] * in[i]; out_sq += out[i] * out[i]; }
    }
    float gain_db = 10.0f * std::log10(out_sq / in_sq);
    CHECK(gain_db > 8.0f);
}

static void test_delay_is_after_eq()
{
    AudioEngine eng;
    eng.Init(48, 48000.0f);
    eng.SetBypass(false);
    eng.SetInputGain(1.0f);
    eng.SetOutputVol(1.0f);
    eng.SetEqBand(Eq3::Band::Mid, 0.0f, 750.0f);
    eng.SetDelay(true, 1.0f, 0.0f, 0.5f, 1.0f);

    std::vector<float> in(96, 0.0f), out(96, 0.0f);
    in[0] = 1.0f;
    eng.Process(in.data(), out.data(), 48);
    eng.Process(in.data() + 48, out.data() + 48, 48);

    CHECK(std::fabs(out[0] - 0.5f) < 1e-5f);
    CHECK(std::fabs(out[48] - 0.5f) < 1e-5f);
}
```

Call both tests from `main()`.

- [ ] **Step 2: Run and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_audio_engine && ./test_audio_engine
```

Expected: fail because EQ is currently omitted from `AudioEngine::Process()`.

- [ ] **Step 3: Add `PedalEffects.cpp` to audio-engine test sources**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`:

```make
AUDIO_SRCS     = ../AudioEngine.cpp ../Eq3.cpp ../PedalEffects.cpp
```

- [ ] **Step 4: Wire the chain**

In `/Users/bbalazs/daisy/daisy-nam-pedal/AudioEngine.cpp`, after input gain:

```cpp
    gate_.Process(scratch_in_, frames);
    compressor_.Process(scratch_in_, frames);
```

After IR processing and before output volume:

```cpp
    eq_.Process(scratch_out_, frames);
    delay_.Process(scratch_out_, frames);
```

Update the header comment in `AudioEngine.h` to:

```cpp
// Signal chain (mono, 48 kHz):
//   in -> input_gain -> gate -> compressor -> NAM -> IR -> EQ -> delay -> output_vol -> out
```

- [ ] **Step 5: Run audio-engine tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_audio_engine && ./test_audio_engine
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add AudioEngine.h AudioEngine.cpp tests/test_audio_engine.cpp tests/Makefile
git commit -m "feat: wire pre and post effects in audio engine"
```

---

### Task 6: Desktop Rust Schema Defaults

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/types.rs`

- [ ] **Step 1: Add failing serde tests**

Extend the existing tests in `src-tauri/src/types.rs`:

```rust
#[test]
fn old_preset_json_deserializes_with_effect_defaults() {
    let json = r#"{
        "id": "p1",
        "name": "Old Preset",
        "model_id": "m1",
        "ir_id": null,
        "input_gain": 1.0,
        "output_volume": 0.8,
        "bypass": false
    }"#;

    let preset: Preset = serde_json::from_str(json).unwrap();
    assert!(!preset.noise_gate_enabled);
    assert_eq!(preset.noise_gate_threshold_db, -70.0);
    assert!(!preset.compressor_enabled);
    assert_eq!(preset.compressor_threshold_db, -18.0);
    assert_eq!(preset.compressor_ratio, 2.0);
    assert_eq!(preset.compressor_attack_ms, 10.0);
    assert_eq!(preset.compressor_release_ms, 100.0);
    assert!(!preset.delay_enabled);
    assert_eq!(preset.delay_time_ms, 350.0);
    assert_eq!(preset.delay_repeats, 0.25);
    assert_eq!(preset.delay_mix, 0.18);
    assert_eq!(preset.delay_tone, 0.5);
}
```

- [ ] **Step 2: Run and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test types
```

Expected: fail because `Preset` does not have effect fields.

- [ ] **Step 3: Add defaults and fields**

In `src-tauri/src/types.rs`, add:

```rust
fn default_noise_gate_threshold_db() -> f32 { -70.0 }
fn default_compressor_threshold_db() -> f32 { -18.0 }
fn default_compressor_ratio() -> f32 { 2.0 }
fn default_compressor_attack_ms() -> f32 { 10.0 }
fn default_compressor_release_ms() -> f32 { 100.0 }
fn default_delay_time_ms() -> f32 { 350.0 }
fn default_delay_repeats() -> f32 { 0.25 }
fn default_delay_mix() -> f32 { 0.18 }
fn default_delay_tone() -> f32 { 0.5 }
```

Append to `Preset`:

```rust
    #[serde(default)]
    pub noise_gate_enabled: bool,
    #[serde(default = "default_noise_gate_threshold_db")]
    pub noise_gate_threshold_db: f32,
    #[serde(default)]
    pub compressor_enabled: bool,
    #[serde(default = "default_compressor_threshold_db")]
    pub compressor_threshold_db: f32,
    #[serde(default = "default_compressor_ratio")]
    pub compressor_ratio: f32,
    #[serde(default = "default_compressor_attack_ms")]
    pub compressor_attack_ms: f32,
    #[serde(default = "default_compressor_release_ms")]
    pub compressor_release_ms: f32,
    #[serde(default)]
    pub delay_enabled: bool,
    #[serde(default = "default_delay_time_ms")]
    pub delay_time_ms: f32,
    #[serde(default = "default_delay_repeats")]
    pub delay_repeats: f32,
    #[serde(default = "default_delay_mix")]
    pub delay_mix: f32,
    #[serde(default = "default_delay_tone")]
    pub delay_tone: f32,
```

- [ ] **Step 4: Run schema tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test types
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src-tauri/src/types.rs
git commit -m "feat: add preset effect defaults"
```

---

### Task 7: Desktop Rust Packer Compatibility

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/image_builder.rs`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/flash.rs`

- [ ] **Step 1: Update packer tests first**

In `src-tauri/src/image_builder.rs`, update `preset_blob_matches_firmware_eq_layout`
to `preset_blob_matches_firmware_effect_layout` and assert:

```rust
assert_eq!(blob.len(), 138);
assert_eq!(blob[98], 1);
assert_eq!(blob[99], 1);
assert_eq!(blob[100], 1);
assert_eq!(blob[101], 0);
assert!((f32_at(&blob, 102) - (-55.0)).abs() < 0.0001);
assert!((f32_at(&blob, 106) - (-20.0)).abs() < 0.0001);
assert!((f32_at(&blob, 110) - 3.0).abs() < 0.0001);
assert!((f32_at(&blob, 114) - 7.0).abs() < 0.0001);
assert!((f32_at(&blob, 118) - 120.0).abs() < 0.0001);
assert!((f32_at(&blob, 122) - 250.0).abs() < 0.0001);
assert!((f32_at(&blob, 126) - 0.4).abs() < 0.0001);
assert!((f32_at(&blob, 130) - 0.3).abs() < 0.0001);
assert!((f32_at(&blob, 134) - 0.7).abs() < 0.0001);
```

Update image-entry length assertions from `98` to `138`.

- [ ] **Step 2: Run and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test image_builder
```

Expected: fail because `pack_preset_blob()` still emits 98 bytes.

- [ ] **Step 3: Extend `pack_preset_blob()`**

Change the signature to:

```rust
pub fn pack_preset_blob(
    model_name: &str,
    ir_name: &str,
    input_gain: f32,
    output_volume: f32,
    bypass: bool,
    eq_bass_gain: f32,
    eq_mid_gain: f32,
    eq_treble_gain: f32,
    eq_bass_freq: f32,
    eq_mid_freq: f32,
    eq_treble_freq: f32,
    noise_gate_enabled: bool,
    compressor_enabled: bool,
    delay_enabled: bool,
    noise_gate_threshold_db: f32,
    compressor_threshold_db: f32,
    compressor_ratio: f32,
    compressor_attack_ms: f32,
    compressor_release_ms: f32,
    delay_time_ms: f32,
    delay_repeats: f32,
    delay_mix: f32,
    delay_tone: f32,
) -> Vec<u8>
```

After the existing six EQ floats, append:

```rust
    buf.write_u8(if noise_gate_enabled { 1 } else { 0 }).unwrap();
    buf.write_u8(if compressor_enabled { 1 } else { 0 }).unwrap();
    buf.write_u8(if delay_enabled { 1 } else { 0 }).unwrap();
    buf.write_u8(0).unwrap();
    buf.write_f32::<LittleEndian>(noise_gate_threshold_db).unwrap();
    buf.write_f32::<LittleEndian>(compressor_threshold_db).unwrap();
    buf.write_f32::<LittleEndian>(compressor_ratio).unwrap();
    buf.write_f32::<LittleEndian>(compressor_attack_ms).unwrap();
    buf.write_f32::<LittleEndian>(compressor_release_ms).unwrap();
    buf.write_f32::<LittleEndian>(delay_time_ms).unwrap();
    buf.write_f32::<LittleEndian>(delay_repeats).unwrap();
    buf.write_f32::<LittleEndian>(delay_mix).unwrap();
    buf.write_f32::<LittleEndian>(delay_tone).unwrap();
    debug_assert_eq!(buf.len(), 138, "NamPreset blob size mismatch");
```

- [ ] **Step 4: Pass fields from flash command**

In `src-tauri/src/commands/flash.rs`, pass the new fields from `p` into
`pack_preset_blob()` after EQ fields:

```rust
            p.noise_gate_enabled,
            p.compressor_enabled,
            p.delay_enabled,
            p.noise_gate_threshold_db,
            p.compressor_threshold_db,
            p.compressor_ratio,
            p.compressor_attack_ms,
            p.compressor_release_ms,
            p.delay_time_ms,
            p.delay_repeats,
            p.delay_mix,
            p.delay_tone,
```

- [ ] **Step 5: Run Rust packer tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test image_builder
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src-tauri/src/image_builder.rs src-tauri/src/commands/flash.rs
git commit -m "feat: pack preset effect fields"
```

---

### Task 8: Desktop TypeScript Types And Editor

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx`

- [ ] **Step 1: Extend TypeScript preset defaults**

In `src/lib/types.ts`, append fields to `Preset`:

```ts
  noise_gate_enabled:       boolean;
  noise_gate_threshold_db:  number;
  compressor_enabled:      boolean;
  compressor_threshold_db: number;
  compressor_ratio:        number;
  compressor_attack_ms:    number;
  compressor_release_ms:   number;
  delay_enabled:           boolean;
  delay_time_ms:           number;
  delay_repeats:           number;
  delay_mix:               number;
  delay_tone:              number;
```

Add:

```ts
export const DEFAULT_EFFECTS = {
  noise_gate_enabled: false,
  noise_gate_threshold_db: -70,
  compressor_enabled: false,
  compressor_threshold_db: -18,
  compressor_ratio: 2,
  compressor_attack_ms: 10,
  compressor_release_ms: 100,
  delay_enabled: false,
  delay_time_ms: 350,
  delay_repeats: 0.25,
  delay_mix: 0.18,
  delay_tone: 0.5,
} satisfies Pick<
  Preset,
  | "noise_gate_enabled"
  | "noise_gate_threshold_db"
  | "compressor_enabled"
  | "compressor_threshold_db"
  | "compressor_ratio"
  | "compressor_attack_ms"
  | "compressor_release_ms"
  | "delay_enabled"
  | "delay_time_ms"
  | "delay_repeats"
  | "delay_mix"
  | "delay_tone"
>;
```

Update `normalizePreset()`:

```ts
    ...DEFAULT_EFFECTS,
```

and explicitly coalesce each effect field with `?? DEFAULT_EFFECTS.<field>`.

- [ ] **Step 2: Add new presets with defaults**

In `src/pages/PresetsPage.tsx`, import `DEFAULT_EFFECTS` and add it to
`makeNewPreset()`:

```ts
    ...DEFAULT_EQ,
    ...DEFAULT_EFFECTS,
```

- [ ] **Step 3: Reuse the existing slider control**

Keep the existing `EqControl` component and use it for effect sliders too. It
already accepts label, value, min, max, step, unit, and `onChange`, so a rename is
not needed for this feature.

- [ ] **Step 4: Add desktop editor sections**

In `PresetEditor`, add a section before EQ:

```tsx
<div className="grid grid-cols-1 md:grid-cols-2 gap-4 rounded-md border p-4">
  <div className="flex items-center justify-between md:col-span-2">
    <Label>Noise gate</Label>
    <Switch
      checked={preset.noise_gate_enabled}
      onCheckedChange={(v) => onChange({ ...preset, noise_gate_enabled: v })}
    />
  </div>
  <EqControl
    label="Gate threshold"
    value={preset.noise_gate_threshold_db}
    min={-90}
    max={-20}
    step={1}
    unit="dB"
    onChange={(value) => onChange({ ...preset, noise_gate_threshold_db: value })}
  />
  <div className="flex items-center justify-between md:col-span-2">
    <Label>Compressor</Label>
    <Switch
      checked={preset.compressor_enabled}
      onCheckedChange={(v) => onChange({ ...preset, compressor_enabled: v })}
    />
  </div>
  <EqControl label="Comp threshold" value={preset.compressor_threshold_db} min={-60} max={0} step={1} unit="dB" onChange={(value) => onChange({ ...preset, compressor_threshold_db: value })} />
  <EqControl label="Ratio" value={preset.compressor_ratio} min={1} max={20} step={0.5} unit=":1" onChange={(value) => onChange({ ...preset, compressor_ratio: value })} />
  <EqControl label="Attack" value={preset.compressor_attack_ms} min={0.1} max={200} step={0.1} unit="ms" onChange={(value) => onChange({ ...preset, compressor_attack_ms: value })} />
  <EqControl label="Release" value={preset.compressor_release_ms} min={5} max={1000} step={5} unit="ms" onChange={(value) => onChange({ ...preset, compressor_release_ms: value })} />
</div>
```

Add a section after EQ:

```tsx
<div className="grid grid-cols-1 md:grid-cols-2 gap-4 rounded-md border p-4">
  <div className="flex items-center justify-between md:col-span-2">
    <Label>Delay</Label>
    <Switch
      checked={preset.delay_enabled}
      onCheckedChange={(v) => onChange({ ...preset, delay_enabled: v })}
    />
  </div>
  <EqControl label="Delay time" value={preset.delay_time_ms} min={1} max={750} step={1} unit="ms" onChange={(value) => onChange({ ...preset, delay_time_ms: value })} />
  <EqControl label="Repeats" value={preset.delay_repeats} min={0} max={0.95} step={0.01} unit="" onChange={(value) => onChange({ ...preset, delay_repeats: value })} />
  <EqControl label="Mix" value={preset.delay_mix} min={0} max={1} step={0.01} unit="" onChange={(value) => onChange({ ...preset, delay_mix: value })} />
  <EqControl label="Tone" value={preset.delay_tone} min={0} max={1} step={0.01} unit="" onChange={(value) => onChange({ ...preset, delay_tone: value })} />
</div>
```

- [ ] **Step 5: Run frontend build**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
npm run build
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src/lib/types.ts src/pages/PresetsPage.tsx
git commit -m "feat: edit preset effects in desktop app"
```

---

### Task 9: Full Verification

**Files:**
- No new files unless a previous task needs a small fixture.

- [ ] **Step 1: Run firmware host tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make clean
make run
```

Expected: all firmware host tests pass.

- [ ] **Step 2: Run desktop Rust tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test
```

Expected: all Rust tests pass.

- [ ] **Step 3: Run desktop frontend build**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
npm run build
```

Expected: TypeScript and Vite build pass.

- [ ] **Step 4: Build a firmware data image with effect fields**

Add one temporary preset entry to a scratch copy of `data/presets.json` or use the
desktop app's app-data store. For the firmware packer path, create a local scratch
directory under `/private/tmp/nam-fx-image` with one `.namb`, one `.wav`, and:

```json
[
  {
    "name": "FX Smoke",
    "model": "be100",
    "ir": "ir",
    "input_gain": 1.0,
    "output_volume": 0.8,
    "bypass": false,
    "eq": {
      "bass_gain": 0.0,
      "mid_gain": 0.0,
      "treble_gain": 0.0,
      "bass_freq": 100.0,
      "mid_freq": 750.0,
      "treble_freq": 4000.0
    },
    "noise_gate": { "enabled": true, "threshold_db": -55.0 },
    "compressor": {
      "enabled": true,
      "threshold_db": -20.0,
      "ratio": 3.0,
      "attack_ms": 7.0,
      "release_ms": 120.0
    },
    "delay": {
      "enabled": true,
      "time_ms": 250.0,
      "repeats": 0.4,
      "mix": 0.3,
      "tone": 0.7
    }
  }
]
```

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
python3 tools/build_data_image.py /private/tmp/nam-fx-image -o /private/tmp/nam-fx-image.bin
python3 tools/inspect_data_image.py /private/tmp/nam-fx-image.bin
```

Expected: inspect succeeds and the preset entry length is `138`.

- [ ] **Step 5: Hardware smoke test**

Build and flash firmware and data through the existing project flow. Confirm:

- A legacy preset still sounds unchanged with all new effects bypassed.
- Gate enabled with a high threshold audibly closes idle input noise.
- Compressor enabled with low threshold reduces loud input.
- Delay enabled produces repeats after EQ.
- Serial CPU peak stays below the 1 ms audio period with margin. If it does not,
  keep the preset format and UI fields but force `delay_enabled` false in
  `PresetManager::ApplyPreset()` until the budget is recovered.

- [ ] **Step 6: Final commit**

If all verification passes:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git status --short
```

Commit any remaining firmware changes in this repo:

```bash
git add .
git commit -m "feat: add preset pre and post effects"
```

Then commit any remaining desktop changes:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git status --short
git add .
git commit -m "feat: support preset pre and post effects"
```

## Final Review Checklist

- [ ] `NamPreset` is exactly 138 bytes in firmware tests, Python tests, and Rust tests.
- [ ] Legacy 74-byte presets keep EQ defaults and all new effects bypassed.
- [ ] Legacy 98-byte EQ presets keep all new effects bypassed.
- [ ] Desktop `Preset` serde loads old JSON without missing-field errors.
- [ ] App-built preset blobs write effect flags at offsets 98, 99, and 100.
- [ ] `AudioEngine::Process()` calls `eq_.Process()` before delay.
- [ ] No new audio-callback allocations.
- [ ] Firmware and app verification commands pass.
