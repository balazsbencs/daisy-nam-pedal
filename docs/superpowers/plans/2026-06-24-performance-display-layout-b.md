# Performance Display Layout B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the performance screen with mockup B: bigger preset name, two large single-line AMP/CAB blocks, and smaller bottom vertical meters.

**Architecture:** Keep the change inside `Ui::RenderPerformance()` and reuse existing `DisplayRenderer` primitives. No new renderer helpers, no new state, no dependency changes.

**Tech Stack:** C++ firmware, libDaisy fonts, ST7789 `DisplayRenderer`, existing RGB565 color constants.

---

## File Structure

- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Ui.cpp:109-186`
  - Replace only the performance screen layout.
  - Keep browse/edit screens unchanged.
- Reference only: `/Users/bbalazs/daisy/daisy-nam-pedal/docs/mockups/display-layout-mockups.svg`
  - Use middle mockup B as the visual target.

---

### Task 1: Patch The Performance Layout

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Ui.cpp:109-186`

- [ ] **Step 1: Replace the current performance layout constants and drawing body**

In `/Users/bbalazs/daisy/daisy-nam-pedal/Ui.cpp`, replace the full body of `Ui::RenderPerformance()` with this:

```cpp
void Ui::RenderPerformance()
{
    using DR = DisplayRenderer;
    DR::Clear(kColorBlack);

    constexpr uint16_t kEqMax = 12;

    // Header: preset index, larger preset name, bypass pill.
    char idx_buf[8];
    snprintf(idx_buf, sizeof(idx_buf), "%02u/%02u",
             (unsigned)(perf_.preset_idx + 1), (unsigned)perf_.preset_count);
    DR::DrawText(kMargin, kRowHeader, idx_buf, kColorDim, kColorBlack, Font_7x10);

    const uint16_t pill_x = 240 - 56;
    const uint16_t pill_c = perf_.bypass ? kColorRed : kColorGreen;
    DR::FillRect(pill_x, kRowHeader, 50, 12, pill_c);
    DR::DrawText(pill_x + 4, kRowHeader + 1,
                 perf_.bypass ? "BYPASS" : "ACTIVE",
                 kColorBlack, pill_c, Font_7x10);

    const char* pname = perf_.preset_name ? perf_.preset_name : "---";
    size_t pname_len = 0; while (pname[pname_len]) pname_len++;
    uint16_t pname_w = static_cast<uint16_t>(pname_len * Font_7x10.FontWidth * 2u);
    uint16_t pname_x = (pname_w < 124u) ? static_cast<uint16_t>(58u + (124u - pname_w) / 2u) : 56u;
    DR::DrawTextScaled(pname_x, 16, pname, kColorWhite, kColorBlack, Font_7x10, 2);

    if (perf_.dirty)
        DR::DrawText(76, 38, "* EDITED", kColorYellow, kColorBlack, Font_7x10);

    // Large AMP block.
    DR::FillRoundRect(10, 34, 220, 82, 5, 0x1082u, kColorBlack);
    DR::HLine(10, 34, 220, kColorOrange);
    DR::HLine(10, 115, 220, kColorOrange);
    DR::VLine(10, 35, 80, kColorOrange);
    DR::VLine(229, 35, 80, kColorOrange);
    DR::DrawText(20, 53, "AMP", kColorDim, 0x1082u, Font_7x10);
    DR::DrawTextScaled(20, 80, perf_.model_name ? perf_.model_name : "---",
                       kColorWhite, 0x1082u, Font_7x10, 2);

    // Large CAB/IR block.
    DR::FillRoundRect(10, 126, 220, 82, 5, 0x1082u, kColorBlack);
    DR::HLine(10, 126, 220, kColorCyan);
    DR::HLine(10, 207, 220, kColorCyan);
    DR::VLine(10, 127, 80, kColorCyan);
    DR::VLine(229, 127, 80, kColorCyan);
    DR::DrawText(20, 145, "CAB", kColorDim, 0x1082u, Font_7x10);
    DR::DrawTextScaled(20, 172, perf_.ir_name ? perf_.ir_name : "Off",
                       kColorWhite, 0x1082u, Font_7x10, 2);

    // Compact meters, about 70% shorter than the old 182px channel strip.
    struct MeterDef { float val; bool bipolar; uint16_t color; const char* lbl; };
    MeterDef meters[5] = {
        { perf_.input_gain / 2.0f,              false, kColorCyan,  "GAIN" },
        { perf_.eq_bass    / (float)kEqMax,     true,  kColorGreen, "BASS" },
        { perf_.eq_mid     / (float)kEqMax,     true,  kColorGreen, "MID"  },
        { perf_.eq_treble  / (float)kEqMax,     true,  kColorGreen, "TRE"  },
        { perf_.output_vol,                     false, kColorCyan,  "VOL"  },
    };

    constexpr uint16_t kMW   = 26;
    constexpr uint16_t kMH   = 52;
    constexpr uint16_t kMY   = 225;
    constexpr uint16_t kMX0  = 25;
    constexpr uint16_t kMGap = 16;

    for (int i = 0; i < 5; ++i) {
        uint16_t mx = static_cast<uint16_t>(kMX0 + i * (kMW + kMGap));
        DR::VMeter(mx, kMY, kMW, kMH, meters[i].val, meters[i].bipolar, meters[i].color);

        uint16_t lbl_len = 0; while (meters[i].lbl[lbl_len]) lbl_len++;
        uint16_t lbl_w   = static_cast<uint16_t>(lbl_len * Font_7x10.FontWidth);
        uint16_t lbl_x   = static_cast<uint16_t>(mx + (kMW > lbl_w ? (kMW - lbl_w) / 2u : 0u));
        DR::DrawText(lbl_x, kMY + kMH + 6u, meters[i].lbl,
                     meters[i].color, kColorBlack, Font_7x10);
    }

    if (perf_.overload)
        DR::DrawText(kMargin, kRowHint, "! AUDIO OVERLOAD", kColorRed, kColorBlack, Font_7x10);
    else
        DR::DrawText(kMargin, kRowHint, "FS1:next/SAVE  FS2:prev/RVRT",
                     kColorDim, kColorBlack, Font_7x10);
}
```

- [ ] **Step 2: Build-check the firmware**

Run:

```bash
make -j
```

Expected: build succeeds. If the full firmware build fails because local submodules or ARM tooling are unavailable, run the narrowest available compile command already used by this repo and record the exact blocker.

- [ ] **Step 3: Check the diff is scoped**

Run:

```bash
git diff -- Ui.cpp docs/mockups/display-layout-mockups.svg
```

Expected:
- `Ui.cpp` changes only `RenderPerformance()`.
- `docs/mockups/display-layout-mockups.svg` remains the selected mockup reference.
- No browse/edit/layout-driver code changes.

- [ ] **Step 4: Commit if requested**

Only commit if the user asks for it:

```bash
git add Ui.cpp docs/mockups/display-layout-mockups.svg
git commit -m "ui: enlarge amp and cab on performance screen"
```

---

## Verification Notes

- This is a static UI layout change, so the useful automated check is build success.
- On hardware, verify from standing/looking-down distance:
  - preset name is readable but secondary;
  - AMP and CAB names are the most prominent screen content;
  - five meters still fit and update;
  - `BYPASS`, `* EDITED`, overload footer, and footswitch hint remain visible.

## Self-Review

- Spec coverage: mockup B is implemented with single-line AMP/CAB names and a bigger preset name.
- Placeholder scan: no TBD/TODO/fill-in steps.
- Type consistency: uses existing `PerformanceState`, `DisplayRenderer`, fonts, and color constants only.
