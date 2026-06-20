# Pedal EQ — Plan B: Controls & UI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Depends on Plan A** (`2026-06-20-pedal-eq-plan-A-audio-data.md`) being complete: `Eq3`, `AudioEngine::SetEqBand/GetEqGain/GetEqFreq`, `NamPreset` EQ fields, and `QspiStorage::WritePreset` must already exist.

**Goal:** Map the five hardware encoders to a live amp panel (Gain/Bass/Mid/Treble/Vol), redraw the Performance screen as a large channel strip, add EQ-frequency fields to the Edit screen, and wire live-edit + dirty + save (XIP-safe flash write) / revert.

**Architecture:** ENC1 keeps libDaisy's `Encoder` (it has a click); ENC2–5 use a new click-less `QuadEncoder`. `Controls` emits a 5-delta `ControlEvent` plus footswitch tap/hold and ENC1 click/long. The audio engine's getters are the single source of truth for live values; the Performance screen reads them each refresh and shows an `● EDITED` marker. Save copies live values into the preset and persists the preset's 4 KB sector with audio halted.

**Tech Stack:** C++17 (gnu++17), libDaisy/DaisySP, STM32H750 @ 480 MHz, ST7789 240×320 SPI display. Host tests via clang++/ASan where logic is pure; everything touching GPIO/SPI/flash is build-verified + on-device checklist.

## Global Constraints

- EQ gain range **−12 … +12 dB, 0.5 dB/detent**. Input gain `[0,2]` at 0.05/detent. Output vol `[0,1]` at 0.05/detent. Frequencies are NOT edited by encoders (Edit screen only).
- Footswitch **hold threshold = 1000 ms**; ENC1 long-press = 800 ms (existing `Controls::kLongPressMs`).
- Encoder pins (verified conflict-free): ENC1 D0/D1/**D2(click)**; ENC2 D7/D8; ENC3 D9/D10; ENC4 D27/D28; ENC5 D29/D30. ENC2–5 have **no click**.
- Display: 240×320 portrait, RGB565, bitmap fonts (`Font_7x10`, `Font_11x18`, `Font_16x26`). New big text via integer-scaled draw. Frame push is blocking (~25 ms) and only happens in the main loop.
- **QSPI write-back runs execute-in-place from the same chip**: a save MUST stop audio and disable interrupts around `QspiStorage::WritePreset`. This is the highest-risk task (B7) — verify on hardware.
- **This tree is not a git repo.** Treat "Commit / checkpoint" as a gate: build the firmware (`make`) and, where host tests exist, `cd tests && make run` → `0 failed`.
- Encoder band order in `ControlEvent::enc_delta[5]`: `0=Gain 1=Bass 2=Mid 3=Treble 4=Vol`.

---

### Task B1: Click-less quadrature decoder (`QuadEncoder`)

**Files:**
- Create: `display/../quad_decode.h` → actually `quad_decode.h` (repo root, pure, no deps)
- Create: `QuadEncoder.h`, `QuadEncoder.cpp`
- Create: `tests/test_quad_encoder.cpp`
- Modify: `tests/Makefile`

**Interfaces:**
- Produces:
  - `int quad_decode(uint8_t& a_hist, uint8_t& b_hist, bool a_level, bool b_level);` (pure; shifts new levels into the 8-bit histories, returns +1 / −1 / 0). In `quad_decode.h`, includable with no Daisy headers.
  - `class QuadEncoder { void Init(daisy::Pin a, daisy::Pin b); void Debounce(); int Increment() const; };`

- [ ] **Step 1: Write the failing test** — create `tests/test_quad_encoder.cpp`:

```cpp
#include "quad_decode.h"
#include "test_harness.h"

int main()
{
    // Clockwise detent: A falls to 0 while B already 0 -> +1.
    {
        uint8_t ah = 0xFF, bh = 0xFF;
        CHECK_EQ(quad_decode(ah, bh, 1, 0), 0);  // A high, B low (setup)
        CHECK_EQ(quad_decode(ah, bh, 0, 0), 1);  // A high->low, B low => CW
    }
    // Counter-clockwise detent: B falls to 0 while A already 0 -> -1.
    {
        uint8_t ah = 0xFF, bh = 0xFF;
        CHECK_EQ(quad_decode(ah, bh, 0, 1), 0);  // B high, A low (setup)
        CHECK_EQ(quad_decode(ah, bh, 0, 0), -1); // B high->low, A low => CCW
    }
    // No motion (levels stable) -> 0.
    {
        uint8_t ah = 0xFF, bh = 0xFF;
        CHECK_EQ(quad_decode(ah, bh, 1, 1), 0);
        CHECK_EQ(quad_decode(ah, bh, 1, 1), 0);
    }
    return test_summary("test_quad_encoder");
}
```

- [ ] **Step 2: Create `quad_decode.h`** (pure, repo root):

```cpp
// quad_decode.h — pure quadrature step decoder (no hardware deps; host-testable).
// Mirrors libDaisy Encoder::Debounce's 2-bit edge rule, without the click switch.
#pragma once
#include <stdint.h>

static inline int quad_decode(uint8_t& a_hist, uint8_t& b_hist, bool a_level, bool b_level)
{
    a_hist = static_cast<uint8_t>((a_hist << 1) | (a_level ? 1u : 0u));
    b_hist = static_cast<uint8_t>((b_hist << 1) | (b_level ? 1u : 0u));
    if ((a_hist & 0x03u) == 0x02u && (b_hist & 0x03u) == 0x00u) return 1;
    if ((b_hist & 0x03u) == 0x02u && (a_hist & 0x03u) == 0x00u) return -1;
    return 0;
}
```

- [ ] **Step 3: Run test to verify it passes** (the decoder is the whole logic):

Run: `cd tests && make test_quad_encoder && ./test_quad_encoder`
Expected: PASS — `test_quad_encoder: N passed, 0 failed`.

Wire `tests/Makefile`: add `test_quad_encoder` to `BINARIES`, then:

```make
test_quad_encoder: test_quad_encoder.cpp ../quad_decode.h
	$(CXX) $(CXXFLAGS) $< -o $@
```
and in `run:`:
```make
	@echo "=== test_quad_encoder ==="
	./test_quad_encoder
```

- [ ] **Step 4: Create `QuadEncoder.h`:**

```cpp
// QuadEncoder.h — A/B quadrature encoder with no click switch (ENC2..ENC5).
#pragma once
#include "per/gpio.h"
#include "sys/system.h"
#include "daisy_core.h"
#include "quad_decode.h"

class QuadEncoder
{
public:
    void Init(daisy::Pin a, daisy::Pin b);
    void Debounce();                               // call once per main-loop tick
    int  Increment() const { return updated_ ? inc_ : 0; }

private:
    daisy::GPIO hw_a_, hw_b_;
    uint32_t    last_update_ = 0;
    bool        updated_     = false;
    uint8_t     a_ = 0xFF, b_ = 0xFF;
    int         inc_ = 0;
};
```

- [ ] **Step 5: Create `QuadEncoder.cpp`:**

```cpp
#include "QuadEncoder.h"
using namespace daisy;

void QuadEncoder::Init(Pin a, Pin b)
{
    hw_a_.Init(a, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    hw_b_.Init(b, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    last_update_ = System::GetNow();
    a_ = b_ = 0xFF;
    inc_ = 0;
    updated_ = false;
}

void QuadEncoder::Debounce()
{
    uint32_t now = System::GetNow();
    updated_ = false;
    if (now - last_update_ >= 1)   // gate to ~1 kHz, like daisy::Encoder
    {
        last_update_ = now;
        updated_ = true;
        inc_ = quad_decode(a_, b_, hw_a_.Read(), hw_b_.Read());
    }
}
```

- [ ] **Step 6: Add `QuadEncoder.cpp` to the firmware build** — root `Makefile` `CPP_SOURCES`, after `Controls.cpp`:

```make
  QuadEncoder.cpp \
```

- [ ] **Step 7: Checkpoint** — `cd tests && make run` (test_quad_encoder green); root `make` still links.

---

### Task B2: Encoder pins in HardwareConfig

**Files:**
- Modify: `HardwareConfig.h:46-57`

**Interfaces:**
- Produces: `hw::PIN_ENC2_A/B … PIN_ENC5_A/B` (no click pins for 2–5).

- [ ] **Step 1: Replace the encoder pin block** in `HardwareConfig.h`:

```cpp
// ---------------------------------------------------------------------------
// Encoders. ENC1 has a click (D2); ENC2..ENC5 are A/B only (no click).
//   ENC1 Gain | ENC2 Bass | ENC3 Mid | ENC4 Treble | ENC5 Vol
// ---------------------------------------------------------------------------
constexpr Pin PIN_ENC1_A     = seed::D0;
constexpr Pin PIN_ENC1_B     = seed::D1;
constexpr Pin PIN_ENC1_CLICK = seed::D2;

constexpr Pin PIN_ENC2_A = seed::D7;   constexpr Pin PIN_ENC2_B = seed::D8;
constexpr Pin PIN_ENC3_A = seed::D9;   constexpr Pin PIN_ENC3_B = seed::D10;
constexpr Pin PIN_ENC4_A = seed::D27;  constexpr Pin PIN_ENC4_B = seed::D28;
constexpr Pin PIN_ENC5_A = seed::D29;  constexpr Pin PIN_ENC5_B = seed::D30;
```

(Remove the old `PIN_ENC2_*` D7/D8/D9 + `ENC2_PRESENT` lines.)

- [ ] **Step 2: Build to verify pins resolve**

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make 2>&1 | tail -5`
Expected: compiles (no "no member named D27" etc.). If a `seed::Dxx` is undefined, confirm against the Daisy Seed pinout — all of D0–D30 exist.

- [ ] **Step 3: Checkpoint** — firmware links.

---

### Task B3: Five-encoder Controls + footswitch tap/hold

**Files:**
- Modify: `Controls.h`, `Controls.cpp`

**Interfaces:**
- Consumes: `QuadEncoder` (B1), encoder pins (B2).
- Produces:
```cpp
struct ControlEvent {
    int8_t enc_delta[5] = {0,0,0,0,0}; // 0=Gain 1=Bass 2=Mid 3=Treble 4=Vol
    bool   enc1_click = false;         // rising edge of ENC1 button
    bool   enc1_long  = false;         // ENC1 held > kLongPressMs (once)
    bool   fs1_tap = false, fs1_hold = false;
    bool   fs2_tap = false, fs2_hold = false;
};
```

- [ ] **Step 1: Rewrite `Controls.h`:**

```cpp
// Controls.h — debounced inputs; emits high-level events once per main-loop tick.
#pragma once
#include "hid/switch.h"
#include "hid/encoder.h"
#include "QuadEncoder.h"
#include "HardwareConfig.h"

struct ControlEvent
{
    int8_t enc_delta[5] = {0,0,0,0,0}; // 0=Gain 1=Bass 2=Mid 3=Treble 4=Vol
    bool   enc1_click = false;
    bool   enc1_long  = false;
    bool   fs1_tap = false, fs1_hold = false;
    bool   fs2_tap = false, fs2_hold = false;
};

class Controls
{
public:
    static constexpr float kLongPressMs = 800.0f;   // ENC1 long-press -> Edit
    static constexpr float kFsHoldMs    = 1000.0f;   // footswitch hold -> save/revert

    void Init();
    ControlEvent Process();   // call once per main-loop iteration

private:
    daisy::Switch  fs1_, fs2_;
    daisy::Encoder enc1_;                 // has click
    QuadEncoder    enc2_, enc3_, enc4_, enc5_;

    bool enc1_long_was_ = false;
    bool fs1_hold_fired_ = false;
    bool fs2_hold_fired_ = false;
};
```

- [ ] **Step 2: Rewrite `Controls.cpp`:**

```cpp
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
    enc2_.Debounce();
    enc3_.Debounce();
    enc4_.Debounce();
    enc5_.Debounce();

    ControlEvent ev;
    ev.enc_delta[0] = static_cast<int8_t>(enc1_.Increment());
    ev.enc_delta[1] = static_cast<int8_t>(enc2_.Increment());
    ev.enc_delta[2] = static_cast<int8_t>(enc3_.Increment());
    ev.enc_delta[3] = static_cast<int8_t>(enc4_.Increment());
    ev.enc_delta[4] = static_cast<int8_t>(enc5_.Increment());

    ev.enc1_click = enc1_.RisingEdge();

    bool long_active = enc1_.TimeHeldMs() >= kLongPressMs && enc1_.Pressed();
    ev.enc1_long = long_active && !enc1_long_was_;
    enc1_long_was_ = long_active;

    // Footswitch tap vs hold: hold fires once at the threshold; tap fires on
    // release only if the hold never fired.
    if (fs1_.Pressed() && fs1_.TimeHeldMs() >= kFsHoldMs && !fs1_hold_fired_)
    { ev.fs1_hold = true; fs1_hold_fired_ = true; }
    if (fs1_.FallingEdge())
    { if (!fs1_hold_fired_) ev.fs1_tap = true; fs1_hold_fired_ = false; }

    if (fs2_.Pressed() && fs2_.TimeHeldMs() >= kFsHoldMs && !fs2_hold_fired_)
    { ev.fs2_hold = true; fs2_hold_fired_ = true; }
    if (fs2_.FallingEdge())
    { if (!fs2_hold_fired_) ev.fs2_tap = true; fs2_hold_fired_ = false; }

    return ev;
}
```

- [ ] **Step 3: Build** (main.cpp won't compile yet — it still references old `ControlEvent` fields; that's wired in B7). Verify Controls itself compiles:

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make build/Controls.o 2>&1 | tail -5` (or `make` and expect errors only in `main.cpp`).
Expected: `Controls.cpp` compiles; remaining errors, if any, are in `main.cpp`.

- [ ] **Step 4: Checkpoint** — Controls + QuadEncoder objects compile.

---

### Task B4: Renderer additions — scaled text + bipolar meter

**Files:**
- Create: `display/meter_fill.h` (pure fill math)
- Modify: `display/display_renderer.h`, `display/display_renderer.cpp`
- Create: `tests/test_meter_fill.cpp`; Modify `tests/Makefile`

**Interfaces:**
- Produces:
  - `pedal::MeterFill pedal::vmeter_fill(int h, float val, bool bipolar);` → `{int y_off, int height}` within a bar of height `h`.
  - `DisplayRenderer::DrawTextScaled(x, y, str, fg, bg, font, scale)` — integer upscaling.
  - `DisplayRenderer::FillRoundRect(x, y, w, h, r, color, bg)` — filled rect with non-AA rounded corners (corner pixels outside radius `r` painted `bg`).
  - `DisplayRenderer::VMeter(x, y, w, h, val, bipolar, color)` — vertical bar (bottom-fill or center boost/cut), drawn with a rounded track.

- [ ] **Step 1: Write the failing test** — create `tests/test_meter_fill.cpp`:

```cpp
#include "display/meter_fill.h"
#include "test_harness.h"

int main()
{
    using pedal::vmeter_fill;
    // Unipolar: bottom-anchored fill.
    { auto m = vmeter_fill(100, 0.5f, false); CHECK_EQ(m.height, 50); CHECK_EQ(m.y_off, 50); }
    { auto m = vmeter_fill(100, 0.0f, false); CHECK_EQ(m.height, 0);  CHECK_EQ(m.y_off, 100); }
    { auto m = vmeter_fill(100, 2.0f, false); CHECK_EQ(m.height, 100); }  // clamps to 1.0
    // Bipolar: center boost/cut.
    { auto m = vmeter_fill(100, 0.0f, true);  CHECK_EQ(m.height, 0); }
    { auto m = vmeter_fill(100, 1.0f, true);  CHECK_EQ(m.y_off, 0);  CHECK_EQ(m.height, 50); } // full boost: center up
    { auto m = vmeter_fill(100, -1.0f, true); CHECK_EQ(m.y_off, 50); CHECK_EQ(m.height, 50); } // full cut: center down
    return test_summary("test_meter_fill");
}
```

- [ ] **Step 2: Create `display/meter_fill.h`** (pure):

```cpp
// meter_fill.h — pure geometry for vertical bar meters (host-testable).
#pragma once

namespace pedal {

struct MeterFill { int y_off; int height; }; // y_off from bar top; height in px

inline MeterFill vmeter_fill(int h, float val, bool bipolar)
{
    if (bipolar)
    {
        if (val >  1.0f) val =  1.0f;
        if (val < -1.0f) val = -1.0f;
        int half = h / 2;
        if (val >= 0.0f) { int len = (int)(val * half + 0.5f);  return { half - len, len }; }
        else             { int len = (int)(-val * half + 0.5f); return { half, len }; }
    }
    if (val > 1.0f) val = 1.0f;
    if (val < 0.0f) val = 0.0f;
    int len = (int)(val * h + 0.5f);
    return { h - len, len };
}

} // namespace pedal
```

- [ ] **Step 3: Wire & run the test**

`tests/Makefile`: add `test_meter_fill` to `BINARIES`, then:
```make
test_meter_fill: test_meter_fill.cpp ../display/meter_fill.h
	$(CXX) $(CXXFLAGS) $< -o $@
```
and in `run:`:
```make
	@echo "=== test_meter_fill ==="
	./test_meter_fill
```
Run: `cd tests && make test_meter_fill && ./test_meter_fill`
Expected: PASS.

- [ ] **Step 4: Declare the new methods** in `display/display_renderer.h` (inside `class DisplayRenderer`, after `DrawText`):

```cpp
    /// Integer-upscaled text: each font pixel becomes scale×scale.
    static void DrawTextScaled(uint16_t x, uint16_t y, const char* str,
                               uint16_t fg, uint16_t bg, const FontDef& font, uint8_t scale);

    /// Filled rectangle with non-anti-aliased rounded corners. Corner pixels
    /// outside the quarter-circle of radius r are painted `bg`.
    static void FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              uint16_t r, uint16_t color, uint16_t bg);

    /// Vertical meter with a rounded track. bipolar=false: fills from bottom for
    /// val∈[0,1]. bipolar=true: fills up (boost) / down (cut) from center for val∈[-1,1].
    static void VMeter(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       float val, bool bipolar, uint16_t color);
```
Add `#include "meter_fill.h"` near the top of `display_renderer.h`.

- [ ] **Step 5: Implement in `display/display_renderer.cpp`** (append before the closing namespace):

```cpp
void DisplayRenderer::DrawTextScaled(uint16_t x, uint16_t y, const char* str,
                                     uint16_t fg, uint16_t bg, const FontDef& font,
                                     uint8_t scale)
{
    if (!str || scale == 0) { return; }
    uint16_t cx = x;
    while (*str)
    {
        char ch = *str++;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const uint16_t* gd = &font.data[static_cast<uint16_t>(ch - 0x20) * font.FontHeight];
        for (uint8_t row = 0; row < font.FontHeight; ++row)
        {
            const uint16_t bits = gd[row];
            for (uint8_t col = 0; col < font.FontWidth; ++col)
            {
                const uint16_t color = ((bits << col) & 0x8000u) ? fg : bg;
                FillRect(static_cast<uint16_t>(cx + col * scale),
                         static_cast<uint16_t>(y + row * scale),
                         scale, scale, color);
            }
        }
        cx = static_cast<uint16_t>(cx + font.FontWidth * scale);
    }
}

void DisplayRenderer::FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                    uint16_t r, uint16_t color, uint16_t bg)
{
    FillRect(x, y, w, h, color);
    // Clamp radius so it can't exceed half the smaller side.
    uint16_t rmax = (w < h ? w : h) / 2u;
    if (r > rmax) r = rmax;
    if (r == 0) return;
    for (uint16_t dy = 0; dy < r; ++dy)
        for (uint16_t dx = 0; dx < r; ++dx)
        {
            uint16_t ex = static_cast<uint16_t>(r - dx);   // distance from arc origin
            uint16_t ey = static_cast<uint16_t>(r - dy);
            if (ex * ex + ey * ey > r * r)                  // pixel outside the arc
            {
                PutPixel(static_cast<uint16_t>(x + dx),         static_cast<uint16_t>(y + dy),         bg);
                PutPixel(static_cast<uint16_t>(x + w - 1 - dx), static_cast<uint16_t>(y + dy),         bg);
                PutPixel(static_cast<uint16_t>(x + dx),         static_cast<uint16_t>(y + h - 1 - dy), bg);
                PutPixel(static_cast<uint16_t>(x + w - 1 - dx), static_cast<uint16_t>(y + h - 1 - dy), bg);
            }
        }
}

void DisplayRenderer::VMeter(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             float val, bool bipolar, uint16_t color)
{
    constexpr uint16_t kRadius = 3;
    // Rounded track (dark grey) on the black background.
    FillRoundRect(x, y, w, h, kRadius, 0x1082 /* dark grey */, kColorBlack);
    if (bipolar)
        HLine(x, static_cast<uint16_t>(y + h / 2), w, kColorDim);   // center reference

    MeterFill m = vmeter_fill(static_cast<int>(h), val, bipolar);
    if (m.height <= 0) return;
    // Fill is a plain rect: its vertical sides align with the track's straight
    // sides, so the rounded track defines the silhouette. We deliberately do NOT
    // round the fill — rounding it against the track grey would leave grey nubs
    // over the track's black corners when the fill reaches an extreme end. The
    // only artifact is a square corner at full-scale fills, which is unobtrusive.
    FillRect(x, static_cast<uint16_t>(y + m.y_off), w, static_cast<uint16_t>(m.height), color);
}
```

- [ ] **Step 6: Build firmware** to confirm the renderer compiles:

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make build/display_renderer.o 2>&1 | tail -5`
Expected: compiles.

- [ ] **Step 7: Checkpoint** — `cd tests && make run` (test_meter_fill green); renderer object builds.

---

### Task B5: Performance screen — channel strip

**Files:**
- Modify: `Ui.h` (PerformanceState fields), `Ui.cpp` (RenderPerformance)

**Interfaces:**
- Consumes: `DrawTextScaled`, `VMeter` (B4).
- Produces: `PerformanceState` gains EQ + dirty fields:
```cpp
    float eq_bass;     // dB [-12,12]
    float eq_mid;      // dB
    float eq_treble;   // dB
    bool  dirty;       // show "● EDITED"
```
(`input_gain`, `output_vol`, `overload`, `bypass`, names, `preset_idx/count` already exist.)

- [ ] **Step 1: Add fields to `Ui.h` `PerformanceState`** (after `bool overload;`):

```cpp
        float       eq_bass;       // dB, bipolar bar
        float       eq_mid;        // dB
        float       eq_treble;     // dB
        bool        dirty;         // unsaved live edits -> "● EDITED"
```

- [ ] **Step 2: Replace `Ui::RenderPerformance()` in `Ui.cpp`** with the channel-strip layout:

```cpp
void Ui::RenderPerformance()
{
    using DR = DisplayRenderer;
    DR::Clear(kColorBlack);

    // --- Header: preset index + status pill ---------------------------------
    char idx_buf[16];
    snprintf(idx_buf, sizeof(idx_buf), "%02u / %02u",
             (unsigned)(perf_.preset_idx + 1), (unsigned)perf_.preset_count);
    DR::DrawText(kMargin, 4, idx_buf, kColorDim, kColorBlack, Font_7x10);

    const uint16_t pill_x = 240 - 56;
    const uint16_t pill_c = perf_.bypass ? kColorRed : kColorGreen;
    DR::FillRoundRect(pill_x, 4, 50, 14, 4, pill_c, kColorBlack);   // rounded pill
    DR::DrawText(pill_x + 4, 6, perf_.bypass ? "BYPASS" : "ACTIVE",
                 kColorBlack, pill_c, Font_7x10);

    // "EDITED" marker between index and pill when dirty.
    if (perf_.dirty)
        DR::DrawText(96, 4, "* EDITED", kColorYellow, kColorBlack, Font_7x10);

    // --- Preset name (big: 2x Font_16x26) -----------------------------------
    const char* pname = perf_.preset_name ? perf_.preset_name : "---";
    DR::DrawTextScaled(kMargin, 16, pname, kColorWhite, kColorBlack, Font_16x26, 2);

    // --- AMP / CAB on their own lines (Font_11x18) --------------------------
    char line[40];
    snprintf(line, sizeof(line), "AMP %s", perf_.model_name ? perf_.model_name : "---");
    DR::DrawText(kMargin, 74, line, kColorWhite, kColorBlack, Font_11x18);
    snprintf(line, sizeof(line), "CAB %s", perf_.ir_name ? perf_.ir_name : "Off");
    DR::DrawText(kMargin, 94, line, kColorWhite, kColorBlack, Font_11x18);

    DR::HLine(kMargin, 118, 240 - 2 * kMargin, kColorDim);

    // --- Five bars: Gain | Bass | Mid | Treble | Vol ------------------------
    struct BarDef { const char* name; float val; bool bipolar; uint16_t color; char txt[8]; };
    BarDef bars[5];
    // Gain: input_gain [0,2] -> bar 0..1; display 0.0..10.0 (×5).
    bars[0] = {"GAIN", perf_.input_gain / 2.0f, false, kColorCyan, {}};
    snprintf(bars[0].txt, sizeof(bars[0].txt), "%.1f", (double)(perf_.input_gain * 5.0f));
    // EQ bands: dB/12 -> [-1,1]; boost green, cut orange.
    auto eqbar = [](BarDef& b, const char* n, float db) {
        b.name = n; b.val = db / 12.0f; b.bipolar = true;
        b.color = (db >= 0.0f) ? kColorGreen : kColorOrange;
        snprintf(b.txt, sizeof(b.txt), "%+d", (int)(db >= 0 ? db + 0.5f : db - 0.5f));
    };
    eqbar(bars[1], "BASS", perf_.eq_bass);
    eqbar(bars[2], "MID",  perf_.eq_mid);
    eqbar(bars[3], "TREB", perf_.eq_treble);
    // Vol: output_vol [0,1]; display percent.
    bars[4] = {"VOL", perf_.output_vol, false, kColorCyan, {}};
    snprintf(bars[4].txt, sizeof(bars[4].txt), "%d%%", (int)(perf_.output_vol * 100.0f + 0.5f));

    const uint16_t bar_top = 126, bar_h = 150, bar_w = 24;
    const uint16_t inner = 240 - 2 * kMargin;          // 228
    const uint16_t slot  = inner / 5;                  // ~45
    for (int i = 0; i < 5; ++i)
    {
        uint16_t slot_x = static_cast<uint16_t>(kMargin + slot * i);
        uint16_t bx     = static_cast<uint16_t>(slot_x + (slot - bar_w) / 2);
        DR::VMeter(bx, bar_top, bar_w, bar_h, bars[i].val, bars[i].bipolar, bars[i].color);
        // value (Font_11x18) + name (Font_7x10) beneath the bar
        DR::DrawText(slot_x + 2, bar_top + bar_h + 4, bars[i].txt,
                     kColorWhite, kColorBlack, Font_11x18);
        DR::DrawText(slot_x + 2, bar_top + bar_h + 24, bars[i].name,
                     kColorDim, kColorBlack, Font_7x10);
    }

    // --- Footer: overload warning or hint -----------------------------------
    if (perf_.overload)
        DR::DrawText(kMargin, 306, "! AUDIO OVERLOAD", kColorRed, kColorBlack, Font_7x10);
    else
        DR::DrawText(kMargin, 306, "FS1:next/save  FS2:prev/revert",
                     kColorDim, kColorBlack, Font_7x10);
}
```

(Remove the now-unused old performance layout constants if the compiler warns; leave Browse/Edit constants intact.)

- [ ] **Step 3: Build firmware**

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make 2>&1 | tail -8`
Expected: links (main.cpp still old → if it errors, that's fixed in B7; to isolate, `make build/Ui.o`).

- [ ] **Step 4: Checkpoint** — `make build/Ui.o` compiles cleanly.

---

### Task B6: Edit screen — EQ frequency fields

**Files:**
- Modify: `Ui.h` (EditState EQ-freq fields), `Ui.cpp` (RenderEdit field list)

**Interfaces:**
- Produces: `EditState` adds `float eq_bass_freq, eq_mid_freq, eq_treble_freq;` and the field count grows from 5 to 8 (MODEL, CAB, BASS FREQ, MID FREQ, TREB FREQ, IN GAIN, OUT VOL, BYPASS). `main.cpp` (B7) edits these.

- [ ] **Step 1: Add fields to `Ui.h` `EditState`** (after the existing float fields):

```cpp
        float eq_bass_freq;   // Hz
        float eq_mid_freq;    // Hz
        float eq_treble_freq; // Hz
```

- [ ] **Step 2: Extend the field table in `Ui::RenderEdit()`** — replace the `kFields` array and bump the loop bound from 5 to 8:

```cpp
    static constexpr FieldRow kFields[8] = {
        {"MODEL",     24},
        {"CAB",       50},
        {"BASS FREQ", 76},
        {"MID FREQ",  102},
        {"TREB FREQ", 128},
        {"IN GAIN",   154},
        {"OUT VOL",   180},
        {"BYPASS",    206},
    };
```
Change `for (uint8_t f = 0; f < 5; ++f)` → `for (uint8_t f = 0; f < 8; ++f)`. Replace the `switch (f)` value cases to match the new indices:

```cpp
        switch (f)
        {
        case 0: { const char* n = (edit_.model_count>0 && edit_.model_names) ? edit_.model_names[edit_.model_idx] : "---";
                  strncpy(val, n ? n : "---", sizeof(val)-1); break; }
        case 1: { if (edit_.ir_idx==0 || !edit_.ir_names) strncpy(val,"Off",sizeof(val)-1);
                  else { const char* n=edit_.ir_names[edit_.ir_idx]; strncpy(val,n?n:"Off",sizeof(val)-1);} break; }
        case 2: snprintf(val, sizeof(val), "%d Hz", (int)(edit_.eq_bass_freq   + 0.5f)); break;
        case 3: snprintf(val, sizeof(val), "%d Hz", (int)(edit_.eq_mid_freq    + 0.5f)); break;
        case 4: snprintf(val, sizeof(val), "%d Hz", (int)(edit_.eq_treble_freq + 0.5f)); break;
        case 5: snprintf(val, sizeof(val), "%.2f", (double)edit_.input_gain); break;
        case 6: snprintf(val, sizeof(val), "%.2f", (double)edit_.output_vol); break;
        case 7: strncpy(val, edit_.bypass ? "ON" : "OFF", sizeof(val)-1); break;
        }
```

- [ ] **Step 3: Build**

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make build/Ui.o 2>&1 | tail -5`
Expected: compiles.

- [ ] **Step 4: Checkpoint** — Ui.o builds; field count is 8.

---

### Task B7: main.cpp wiring — live edit, menu nav, save/revert, persistence

**Files:**
- Modify: `PresetManager.h`, `PresetManager.cpp` (expose backing entry for persistence)
- Modify: `main.cpp` (control handling, screen population, save sequence)

**Interfaces:**
- Consumes: everything above + Plan A's `AudioEngine` EQ getters/setters and `QspiStorage::WritePreset`.
- Produces: `const NamDataEntry* PresetManager::Entry(uint8_t idx) const;` (nullptr for synthesized presets) and the wired main loop.

- [ ] **Step 1: Store entry pointers in PresetManager** — `PresetManager.h`: add member + accessor:

```cpp
    const NamDataEntry* Entry(uint8_t idx) const { return (idx < count_) ? entries_[idx] : nullptr; }
```
and in the private data: `const NamDataEntry* entries_[kMaxPresets] = {};`

`PresetManager.cpp` `Init`: when a preset blob is accepted, record its entry; for synthesized presets set `entries_[count_] = nullptr;`. In the QSPI-scan loop, before `count_++`:
```cpp
        entries_[count_] = e;
```
and in the synthesized loop and the final fallback: `entries_[count_] = nullptr;` before each `count_++`.

- [ ] **Step 2: Add EQ + dirty state and helpers to `main.cpp`** — near the other UI-mode globals:

```cpp
static bool preset_dirty = false;

// Encoder steps.
static constexpr float kGainStep = 0.05f;   // input_gain [0,2]
static constexpr float kVolStep  = 0.05f;   // output_vol [0,1]
static constexpr float kEqStep   = 0.5f;    // dB
static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }
```

- [ ] **Step 3: Populate the channel-strip Performance screen** — replace `PushPerformanceScreen()` body so it reads live EQ from the engine and the dirty flag:

```cpp
static void PushPerformanceScreen()
{
    const NamPreset& p = presets.ActivePreset();
    Ui::PerformanceState s{};
    s.preset_name  = presets.Name(presets.Current());
    s.model_name   = p.model_name[0] ? p.model_name : "---";
    s.ir_name      = p.ir_name[0]    ? p.ir_name    : "Off";
    s.input_gain   = audio_engine.GetInputGain();
    s.output_vol   = audio_engine.GetOutputVol();
    s.bypass       = audio_engine.GetBypass();
    s.overload     = audio_overload;
    s.eq_bass      = audio_engine.GetEqGain(Eq3::Band::Bass);
    s.eq_mid       = audio_engine.GetEqGain(Eq3::Band::Mid);
    s.eq_treble    = audio_engine.GetEqGain(Eq3::Band::Treble);
    s.dirty        = preset_dirty;
    s.preset_idx   = presets.Current();
    s.preset_count = presets.Count();
    ui.ShowPerformance(s);
}
```

- [ ] **Step 4: Add the XIP-safe save helper to `main.cpp`** (above `main`):

```cpp
// Persist the current live values into the active preset and write its sector
// back to QSPI. Audio is halted and IRQs disabled across the flash op because
// the app executes-in-place from the same QSPI chip.
static void SaveActivePreset()
{
    uint8_t cur = presets.Current();
    NamPreset& p = presets.EditablePreset(cur);
    p.input_gain    = audio_engine.GetInputGain();
    p.output_volume = audio_engine.GetOutputVol();
    p.bypass        = audio_engine.GetBypass() ? 1 : 0;
    p.eq_bass_gain   = audio_engine.GetEqGain(Eq3::Band::Bass);
    p.eq_mid_gain    = audio_engine.GetEqGain(Eq3::Band::Mid);
    p.eq_treble_gain = audio_engine.GetEqGain(Eq3::Band::Treble);
    p.eq_bass_freq   = audio_engine.GetEqFreq(Eq3::Band::Bass);
    p.eq_mid_freq    = audio_engine.GetEqFreq(Eq3::Band::Mid);
    p.eq_treble_freq = audio_engine.GetEqFreq(Eq3::Band::Treble);

    const NamDataEntry* e = presets.Entry(cur);
    if (e)   // synthesized presets have no flash entry -> in-RAM only
    {
        daisy_seed.StopAudio();
        __disable_irq();
        storage.WritePreset(e, p);     // erase+program this preset's 4 KB sector
        __enable_irq();
        daisy_seed.StartAudio(AudioCallback);
    }
    preset_dirty = false;
}
```

- [ ] **Step 5: Rewrite the Performance-mode branch of the main loop** (`if (!browsing && !editing)`):

```cpp
        if (!browsing && !editing)
        {
            // FS taps change preset; FS holds save / revert.
            if (ev.fs1_tap && presets.Count() > 1) {
                presets.Next();
                presets.Apply(audio_engine, storage, models, hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                preset_dirty = false; PushPerformanceScreen();
            } else if (ev.fs2_tap && presets.Count() > 1) {
                presets.Prev();
                presets.Apply(audio_engine, storage, models, hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                preset_dirty = false; PushPerformanceScreen();
            }
            if (ev.fs1_hold) { SaveActivePreset(); PushPerformanceScreen(); }
            if (ev.fs2_hold) {  // revert: reload stored preset
                presets.Apply(audio_engine, storage, models, hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                preset_dirty = false; PushPerformanceScreen();
            }

            // Live knobs.
            if (ev.enc_delta[0]) {
                audio_engine.SetInputGain(clampf(audio_engine.GetInputGain() + ev.enc_delta[0]*kGainStep, 0.0f, 2.0f));
                preset_dirty = true; PushPerformanceScreen();
            }
            if (ev.enc_delta[1]) {
                float d = clampf(audio_engine.GetEqGain(Eq3::Band::Bass) + ev.enc_delta[1]*kEqStep, -12.0f, 12.0f);
                audio_engine.SetEqBand(Eq3::Band::Bass, d, audio_engine.GetEqFreq(Eq3::Band::Bass));
                preset_dirty = true; PushPerformanceScreen();
            }
            if (ev.enc_delta[2]) {
                float d = clampf(audio_engine.GetEqGain(Eq3::Band::Mid) + ev.enc_delta[2]*kEqStep, -12.0f, 12.0f);
                audio_engine.SetEqBand(Eq3::Band::Mid, d, audio_engine.GetEqFreq(Eq3::Band::Mid));
                preset_dirty = true; PushPerformanceScreen();
            }
            if (ev.enc_delta[3]) {
                float d = clampf(audio_engine.GetEqGain(Eq3::Band::Treble) + ev.enc_delta[3]*kEqStep, -12.0f, 12.0f);
                audio_engine.SetEqBand(Eq3::Band::Treble, d, audio_engine.GetEqFreq(Eq3::Band::Treble));
                preset_dirty = true; PushPerformanceScreen();
            }
            if (ev.enc_delta[4]) {
                audio_engine.SetOutputVol(clampf(audio_engine.GetOutputVol() + ev.enc_delta[4]*kVolStep, 0.0f, 1.0f));
                preset_dirty = true; PushPerformanceScreen();
            }

            // ENC1 click -> Browse; long-press -> Edit.
            if (ev.enc1_click) { browse_cursor = presets.Current(); browsing = true; PushBrowseScreen(); }
            if (ev.enc1_long)  { editing = true; PushEditScreen(presets.Current()); }
        }
```

- [ ] **Step 6: Update Browse and Edit branches to the new event names** — in the Browse branch replace `ev.enc1_delta` with `ev.enc_delta[0]`, and replace the cancel condition `if (ev.next_preset || ev.prev_preset)` with `if (ev.fs1_tap || ev.fs2_tap)`. In the Edit branch: replace every `ev.enc1_delta` with `ev.enc_delta[0]`; replace `ev.next_preset` (apply) with `ev.fs1_tap` and `ev.prev_preset` (cancel) with `ev.fs2_tap`. For the three new EQ-frequency fields, add value-edit cases (in the `edit_state.editing` delta switch), and seed/write them in `PushEditScreen` / the apply path:

In `PushEditScreen`, after copying gains, seed freqs from the preset:
```cpp
    edit_state.eq_bass_freq   = p.eq_bass_freq   > 0 ? p.eq_bass_freq   : 100.0f;
    edit_state.eq_mid_freq    = p.eq_mid_freq    > 0 ? p.eq_mid_freq    : 750.0f;
    edit_state.eq_treble_freq = p.eq_treble_freq > 0 ? p.eq_treble_freq : 4000.0f;
```
Field navigation max changes from 4 to 7: `if (f > 7) f = 7;`. Value-edit switch — replace old cases with the 8-field mapping (freq steps with clamps):
```cpp
                    case 0: /* MODEL */   /* unchanged model_idx logic */ break;
                    case 1: /* CAB */     /* unchanged ir_idx logic */ break;
                    case 2: edit_state.eq_bass_freq   = clampf(edit_state.eq_bass_freq   + ev.enc_delta[0]*10.0f, 50.0f, 500.0f);   break;
                    case 3: edit_state.eq_mid_freq    = clampf(edit_state.eq_mid_freq    + ev.enc_delta[0]*25.0f, 200.0f, 3000.0f); break;
                    case 4: edit_state.eq_treble_freq = clampf(edit_state.eq_treble_freq + ev.enc_delta[0]*100.0f, 1500.0f, 10000.0f); break;
                    case 5: { float v = edit_state.input_gain + ev.enc_delta[0]*0.05f; edit_state.input_gain = clampf(v,0.0f,2.0f); break; }
                    case 6: { float v = edit_state.output_vol + ev.enc_delta[0]*0.05f; edit_state.output_vol = clampf(v,0.0f,1.0f); break; }
                    case 7: edit_state.bypass = !edit_state.bypass; break;
```
In the Edit apply path (`ev.fs1_tap`), write freqs back too:
```cpp
                p.eq_bass_freq   = edit_state.eq_bass_freq;
                p.eq_mid_freq    = edit_state.eq_mid_freq;
                p.eq_treble_freq = edit_state.eq_treble_freq;
```
then call `presets.ApplyPreset(...)` (already there) and **also** `SaveActivePreset()` if you want frequency edits persisted immediately (recommended, since Edit is an explicit commit). Set `editing = false; PushPerformanceScreen();`.

- [ ] **Step 7: Build the firmware**

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make 2>&1 | tail -12`
Expected: links cleanly to `build/NamPlatform.bin`; memory-usage table prints; no errors. Fix any remaining references to old `ControlEvent` fields (`next_preset`, `prev_preset`, `enc1_delta`) the compiler flags.

- [ ] **Step 8: Checkpoint** — full firmware builds; `cd tests && make run` all green (host layer unaffected).

---

### Task B8: Integration build + on-device verification

**Files:** none (verification only).

- [ ] **Step 1: Clean build + data image**

Run: `cd /Users/bbalazs/daisy/daisy-nam-pedal && make clean && make && make data-image`
Expected: `build/NamPlatform.bin` and `data_image.bin` produced, no errors.

- [ ] **Step 2: Flash app + data, then verify on hardware** (use `tools/flash_app.sh` / `tools/flash_data.sh`). Walk this checklist:

  - [ ] Boots to the channel-strip Performance screen: big preset name, AMP/CAB lines, five bars (Gain, Bass, Mid, Treble, Vol), ACTIVE pill.
  - [ ] Turning ENC1 moves the Gain bar + value; ENC5 moves Vol; ENC2/3/4 move Bass/Mid/Treble bars (boost up = green, cut down = orange). Audio changes accordingly.
  - [ ] First knob turn shows `* EDITED`. Serial `peak=` stays well under 1 ms with EQ active.
  - [ ] **ENC1 click** opens Browse; turning ENC1 scrolls; click selects and returns. **ENC1 long-press** opens Edit.
  - [ ] Edit shows MODEL, CAB, BASS/MID/TREB FREQ, IN GAIN, OUT VOL, BYPASS; editing a frequency changes the tone; FS1 applies, FS2 cancels.
  - [ ] **FS1 tap** = next preset, **FS2 tap** = prev (each clears `* EDITED`).
  - [ ] **FS1 hold** = save: audio mutes briefly, returns; `* EDITED` clears. **Power-cycle** → the saved values persist. **FS2 hold** = revert to stored values.
  - [ ] **⚠ Persistence risk gate:** if FS1-hold hardfaults or hangs (screen freezes, audio doesn't resume), the QSPI erase/program is executing from QSPI. Contingency: relocate the erase/program routine (and `SaveActivePreset`) to RAM — mark them `__attribute__((section(".sram1_bss")))`-adjacent isn't enough for *code*; place the function in ITCM via libDaisy's RAM-function macro (see `libDaisy` `qspi` examples / `DTCM`/`ITCM` attributes) and ensure no QSPI-resident IRQ runs while indirect mode is active. Re-test until save completes and resumes audio cleanly.

- [ ] **Step 3: Done** — Plan B deliverable: the full amp-panel UI with live EQ, the redesigned Performance screen, EQ-frequency editing, and power-cycle-persistent saves.

---

## Self-Review notes

- **Spec coverage:** §5 controls → B1/B2/B3; §6 live-edit+dirty → B5/B7; §6a persistence orchestration → B7 (uses Plan A's `WritePreset`); §7 Performance → B5, Browse (existing, event-renamed) → B7 step 6, Edit + EQ freq → B6/B7; §8 rendering → B4. Tunable defaults (§10) encoded as named constants in B3/B7.
- **Types consistent across tasks:** `ControlEvent` (B3) consumed verbatim in B7; `quad_decode`/`QuadEncoder` (B1) used in B3; `vmeter_fill`/`DrawTextScaled`/`VMeter` (B4) used in B5; `PerformanceState`/`EditState` fields (B5/B6) populated in B7; `PresetManager::Entry` (B7 step 1) used by `SaveActivePreset` (B7 step 4); `AudioEngine` EQ getters/setters from Plan A.
- **Highest risk:** B8 persistence gate — the XIP-safe flash write is the one item that may need the RAM-relocation contingency; it is explicitly called out with a fallback rather than assumed working.
- **No placeholders:** every code/test step carries literal code and exact commands.
```
