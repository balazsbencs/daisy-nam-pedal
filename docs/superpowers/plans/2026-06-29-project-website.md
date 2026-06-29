# NAM Platform Pedal Website Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and deploy the authoritative static product site, interactive user manual, developer guide, and verified hardware reference for the NAM Platform Pedal.

**Architecture:** Serve handwritten HTML, CSS, and browser-native JavaScript directly from `site/`. Keep simulator transitions in a pure ES module so Node's built-in test runner can verify them independently of the DOM; use a thin browser adapter for controls and rendering. Validate the static site with one Python standard-library checker and deploy the directory unchanged with official GitHub Pages actions.

**Tech Stack:** HTML5, CSS, ES modules, Node built-in `node:test`, Python 3 standard library, GitHub Actions/Pages.

---

## File Map

| File | Responsibility |
|---|---|
| `site/index.html` | Product/features landing page and player/builder entry points |
| `site/manual.html` | Interactive pedal and complete operating manual |
| `site/developers.html` | Firmware, build, data, flash, test, and contribution guide |
| `site/hardware.html` | Verified pin map, defined wiring, assembly checks, and bring-up |
| `site/404.html` | Recovery links for unknown GitHub Pages routes |
| `site/assets/site.css` | Shared Stage Tech visual system and responsive/accessibility rules |
| `site/assets/site.js` | Progressive mobile navigation and copy buttons |
| `site/assets/simulator-state.mjs` | Pure pedal state and transition rules |
| `site/assets/simulator.js` | DOM controls, holds, knob input, and display rendering |
| `tools/check_site.py` | Dependency-free static link/structure validator |
| `tests/site/test_check_site.py` | Validator regression checks |
| `tests/site/simulator-state.test.mjs` | Simulator transition checks |
| `.github/workflows/pages.yml` | Site validation and GitHub Pages deployment |
| `README.md` | Concise repository entry point to the authoritative site |
| `docs/HARDWARE.md` | Compatibility pointer to the authoritative hardware page |

Do not modify the existing unresolved changes in submodules, asset directories, `tests/Makefile`, or unrelated untracked files.

---

### Task 1: Static Site Validator

**Files:**
- Create: `tools/check_site.py`
- Create: `tests/site/test_check_site.py`

- [ ] **Step 1: Write failing validator tests**

Create `tests/site/test_check_site.py` with temporary sites covering a valid relative link, a missing target, a missing fragment, and duplicate IDs:

```python
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "check_site.py"


class SiteCheckerTest(unittest.TestCase):
    def run_checker(self, files):
        with tempfile.TemporaryDirectory() as tmp:
            site = Path(tmp)
            for name, body in files.items():
                path = site / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(body, encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(CHECKER), str(site)],
                text=True,
                capture_output=True,
                check=False,
            )

    def test_accepts_valid_relative_link_and_fragment(self):
        result = self.run_checker({
            "index.html": '<title>Home</title><main id="top"><a href="manual.html#start">Manual</a></main>',
            "manual.html": '<title>Manual</title><main><h1 id="start">Start</h1></main>',
            "developers.html": "<title>Developers</title><main></main>",
            "hardware.html": "<title>Hardware</title><main></main>",
            "404.html": "<title>Not found</title><main></main>",
        })
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_missing_file_and_fragment_and_duplicate_id(self):
        result = self.run_checker({
            "index.html": '<title>Home</title><main id="same"><a href="missing.html">X</a><a href="#nope">Y</a><b id="same"></b></main>',
            "manual.html": "<title>Manual</title><main></main>",
            "developers.html": "<title>Developers</title><main></main>",
            "hardware.html": "<title>Hardware</title><main></main>",
            "404.html": "<title>Not found</title><main></main>",
        })
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing.html", result.stderr)
        self.assertIn("#nope", result.stderr)
        self.assertIn("duplicate id 'same'", result.stderr)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify failure**

Run: `python3 -m unittest tests/site/test_check_site.py -v`

Expected: FAIL because `tools/check_site.py` does not exist.

- [ ] **Step 3: Implement the validator**

Create `tools/check_site.py`. Use `html.parser.HTMLParser` to collect `id`, `href`, and `src` attributes from every HTML file. Require `index.html`, `manual.html`, `developers.html`, `hardware.html`, and `404.html`; require one `<title>` and one `<main>` per page; reject duplicate IDs; ignore `http:`, `https:`, `mailto:`, and `tel:` URLs; resolve local paths relative to the source page; and verify fragment IDs in the target page.

The public interface is:

```python
def check_site(root: Path) -> list[str]:
    """Return human-readable validation errors; an empty list means valid."""

def main(argv: list[str]) -> int:
    root = Path(argv[1] if len(argv) > 1 else "site")
    errors = check_site(root)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Site check passed: {root}")
    return 0
```

Treat query strings as irrelevant to path resolution and URL-decode fragments before matching IDs. Do not fetch or validate external URLs.

- [ ] **Step 4: Run validator tests**

Run: `python3 -m unittest tests/site/test_check_site.py -v`

Expected: 2 tests pass.

- [ ] **Step 5: Commit the validator**

```bash
git add tools/check_site.py tests/site/test_check_site.py
git commit -m "test: add static site validator"
```

---

### Task 2: Shared Site Shell and Landing Page

**Files:**
- Create: `site/assets/site.css`
- Create: `site/assets/site.js`
- Create: `site/index.html`
- Create: `site/manual.html`
- Create: `site/developers.html`
- Create: `site/hardware.html`
- Create: `site/404.html`

- [ ] **Step 1: Add the smallest valid page set**

Create all five pages with relative assets, unique titles, a skip link, shared navigation, one `<main>`, and a shared footer. Use this navigation on every page, changing `aria-current="page"` to the active link:

```html
<a class="skip-link" href="#content">Skip to content</a>
<header class="site-header">
  <a class="wordmark" href="index.html">NAM <span>// PLATFORM</span></a>
  <button class="nav-toggle" type="button" aria-expanded="false" aria-controls="site-nav">Menu</button>
  <nav id="site-nav" aria-label="Primary">
    <a href="index.html">Features</a>
    <a href="manual.html">Manual</a>
    <a href="developers.html">Developers</a>
    <a href="hardware.html">Hardware</a>
    <a href="https://github.com/balazsbencs/daisy-nam-pedal">GitHub <span aria-hidden="true">↗</span></a>
  </nav>
</header>
```

Use `https://github.com/balazsbencs/daisy-nam-pedal` as the absolute repository URL everywhere.

Add `<noscript>` only on `manual.html`, stating that the written manual works without JavaScript but the interactive pedal does not.
Load shared behavior on every page with `<script src="assets/site.js" defer></script>` immediately before `</body>`.

- [ ] **Step 2: Run the checker and verify the shell passes**

Run: `python3 tools/check_site.py site`

Expected: `Site check passed: site`.

- [ ] **Step 3: Implement the Stage Tech visual system**

In `site/assets/site.css`, define the shared tokens and base behavior first:

```css
:root {
  color-scheme: dark;
  --bg: #080a0c;
  --surface: #101416;
  --surface-2: #171c1f;
  --line: #293136;
  --text: #eef1f2;
  --muted: #98a2a7;
  --orange: #ff8a34;
  --cyan: #4bd4d0;
  --green: #55de91;
  --red: #ff5e57;
  --content: 74rem;
  --radius: .75rem;
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body { margin: 0; background: var(--bg); color: var(--text); font-family: Inter, ui-sans-serif, system-ui, sans-serif; line-height: 1.6; }
a { color: var(--cyan); }
:focus-visible { outline: 3px solid var(--orange); outline-offset: 3px; }
.skip-link { position: fixed; left: 1rem; top: -5rem; z-index: 100; background: var(--orange); color: #111; padding: .7rem 1rem; }
.skip-link:focus { top: 1rem; }
@media (prefers-reduced-motion: reduce) { *, *::before, *::after { scroll-behavior: auto !important; animation-duration: .01ms !important; } }
```

Complete the same file with the approved dark product layout: sticky header, two-column hero, orange primary CTA, cyan secondary CTA, four proof points, simulator teaser, paired player/builder cards, signal-flow row, documentation sidebar/TOC, tables with horizontal containment, code blocks, callouts, the corrected portrait pedal shell, and responsive breakpoints at `52rem` and `34rem`. Controls must be at least 44 CSS pixels in both dimensions.

- [ ] **Step 4: Implement progressive shared behavior**

Create `site/assets/site.js` with only mobile navigation and copy-button behavior:

```js
const toggle = document.querySelector('.nav-toggle');
const nav = document.querySelector('#site-nav');
toggle?.addEventListener('click', () => {
  const open = toggle.getAttribute('aria-expanded') !== 'true';
  toggle.setAttribute('aria-expanded', String(open));
  nav?.toggleAttribute('data-open', open);
});

for (const block of document.querySelectorAll('pre')) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'copy-code';
  button.textContent = 'Copy';
  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(block.querySelector('code')?.textContent ?? block.textContent);
      button.textContent = 'Copied';
      setTimeout(() => { button.textContent = 'Copy'; }, 1500);
    } catch {
      button.textContent = 'Copy';
    }
  });
  block.append(button);
}
```

Copy failure must leave the button labeled `Copy`; catch clipboard rejection without hiding the code itself.

- [ ] **Step 5: Build the landing page content**

Implement the approved hierarchy in `site/index.html`:

1. Eyebrow: `OPEN-SOURCE · DAISY SEED · 48 KHZ`.
2. Heading: `Your rig. Your firmware.`
3. One paragraph describing NAM captures, cabinet IRs, EQ, presets, effects, and tuner without claiming browser audio processing.
4. `Try the pedal` link to `manual.html#simulator` and `Build your own` link to `hardware.html#before-you-start`.
5. Four proof points: 48 kHz/48-sample blocks, five live encoders, 6 MiB data partition, open firmware.
6. Corrected product silhouette: five top encoders, centered portrait screen, two bottom footswitches.
7. Paired cards linking to the manual and developer guide.
8. Signal path: input gain, gate, compressor, NAM, IR, EQ, delay, output volume.

Use text and CSS shapes for the product illustration; add no image dependency.

- [ ] **Step 6: Validate and commit the shell**

Run:

```bash
python3 tools/check_site.py site
node --check site/assets/site.js
```

Expected: both exit 0.

```bash
git add site
git commit -m "feat: add static project site shell"
```

---

### Task 3: Simulator State Machine

**Files:**
- Create: `site/assets/simulator-state.mjs`
- Create: `tests/site/simulator-state.test.mjs`

- [ ] **Step 1: Write failing state-transition tests**

Create `tests/site/simulator-state.test.mjs` using only `node:test` and `node:assert/strict`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { createInitialState, transition } from '../../site/assets/simulator-state.mjs';

test('performance edits clamp, become dirty, save, and revert', () => {
  let state = createInitialState();
  state = transition(state, { type: 'TURN', encoder: 0, steps: 30 });
  assert.equal(state.live.inputGain, 2);
  assert.equal(state.dirty, true);
  state = transition(state, { type: 'FS1_HOLD' });
  assert.equal(state.dirty, false);
  state = transition(state, { type: 'TURN', encoder: 0, steps: -3 });
  state = transition(state, { type: 'FS2_HOLD' });
  assert.equal(state.live.inputGain, 2);
  assert.equal(state.dirty, false);
});

test('preset taps wrap and browse selection is bounded', () => {
  let state = createInitialState();
  state = transition(state, { type: 'FS2_TAP' });
  assert.equal(state.activePreset, state.presets.length - 1);
  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'TURN', encoder: 0, steps: -99 });
  assert.equal(state.browseCursor, 0);
  state = transition(state, { type: 'PRIMARY_CLICK' });
  assert.equal(state.mode, 'performance');
  assert.equal(state.activePreset, 0);
});

test('browse hold opens edit; cancel discards and apply saves', () => {
  let state = transition(createInitialState(), { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'PRIMARY_HOLD' });
  assert.equal(state.mode, 'edit');
  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'TURN', encoder: 0, steps: 1 });
  const changedModel = state.edit.values.model;
  state = transition(state, { type: 'FS2_TAP' });
  assert.equal(state.mode, 'performance');
  assert.notEqual(state.live.model, changedModel);

  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'PRIMARY_HOLD' });
  state = transition(state, { type: 'PRIMARY_CLICK' });
  state = transition(state, { type: 'TURN', encoder: 0, steps: 1 });
  state = transition(state, { type: 'FS1_TAP' });
  assert.equal(state.mode, 'performance');
  assert.equal(state.live.model, changedModel);
  assert.equal(state.dirty, false);
});

test('both-switch hold toggles tuner and reset restores defaults', () => {
  let state = transition(createInitialState(), { type: 'BOTH_HOLD' });
  assert.equal(state.mode, 'tuner');
  state = transition(state, { type: 'BOTH_HOLD' });
  assert.equal(state.mode, 'performance');
  state = transition(state, { type: 'FS1_TAP' });
  state = transition(state, { type: 'RESET' });
  assert.deepEqual(state, createInitialState());
});
```

- [ ] **Step 2: Run tests and verify failure**

Run: `node --test tests/site/simulator-state.test.mjs`

Expected: FAIL because `simulator-state.mjs` does not exist.

- [ ] **Step 3: Implement the pure state module**

Export exactly:

```js
export const MODELS = ['BE100', 'PT20 Clean', 'JM50 Crunch'];
export const IRS = ['Off', 'Friedman 212', 'V30 412'];
export const EDIT_FIELDS = ['model', 'ir', 'inputGain', 'outputVolume', 'bypass', 'bassFreq', 'midFreq', 'trebleFreq'];
export function createInitialState() { /* return fresh state */ }
export function transition(state, action) { /* return fresh state */ }
```

Use three demo presets with all fields required by the screen: name, model, IR, input gain, output volume, bypass, EQ gains, and EQ frequencies. Keep a saved preset array and a separate `live` copy. Implement firmware step sizes and bounds:

| Value | Step | Bounds |
|---|---:|---:|
| input gain | 0.10 | 0–2 |
| output volume | 0.10 | 0–1 |
| EQ gain | 1 dB | -12–12 dB |
| bass frequency | 20 Hz | 20–500 Hz |
| mid frequency | 50 Hz | 200–2000 Hz |
| treble frequency | 100 Hz | 1000–8000 Hz |

`FS1_TAP` and `FS2_TAP` wrap presets in performance mode, apply/cancel in edit mode, and cancel browse mode. `PRIMARY_CLICK` enters browse from performance, selects from browse, and toggles field editing in edit. `PRIMARY_HOLD` enters edit only from browse. `BOTH_HOLD` toggles tuner only from performance/tuner. Unknown or unavailable actions return the current state unchanged. `RESET` returns a fresh initial state.

- [ ] **Step 4: Run state tests**

Run: `node --test tests/site/simulator-state.test.mjs`

Expected: 4 tests pass.

- [ ] **Step 5: Commit simulator logic**

```bash
git add site/assets/simulator-state.mjs tests/site/simulator-state.test.mjs
git commit -m "feat: model pedal simulator state"
```

---

### Task 4: Interactive Pedal and Operating Manual

**Files:**
- Create: `site/assets/simulator.js`
- Modify: `site/manual.html`
- Modify: `site/assets/site.css`

- [ ] **Step 1: Add semantic simulator markup**

In `site/manual.html`, add `<section id="simulator">` containing:

- A `.pedal` figure with five top-row encoder controls.
- A centered portrait `.pedal-screen` with `role="status"` and `aria-live="polite"`.
- Left FS2 and right FS1 footswitch buttons on the bottom row.
- A `Reset demo` button.
- A `.mode-help` region headed `What can I do here?`.
- A visible note: `Interface demonstration only — no audio is processed.`

Each encoder is a `<button>` with `data-encoder="0"` through `4`, `aria-label`, and `aria-valuenow`. Footswitches use `data-action="fs2"` and `data-action="fs1"`. Load the module with:

```html
<script type="module" src="assets/simulator.js"></script>
```

- [ ] **Step 2: Implement browser interaction**

Create `site/assets/simulator.js` as a thin adapter around `transition()`:

```js
import { createInitialState, transition } from './simulator-state.mjs';

let state = createInitialState();
const dispatch = action => {
  state = transition(state, action);
  render(state);
};
```

Implement these input mappings:

- Wheel over an encoder: prevent page scroll and dispatch `TURN` with `steps` of `1` or `-1`.
- ArrowUp/ArrowRight and ArrowDown/ArrowLeft: one positive or negative step.
- Home/End: dispatch enough steps to reach the relevant minimum/maximum.
- Pointer drag: accumulate vertical movement and dispatch one step per 12 pixels.
- Primary encoder click: dispatch `PRIMARY_CLICK`.
- Primary encoder pointer hold at 800 ms: dispatch `PRIMARY_HOLD` and suppress the following click.
- Individual footswitch release before 800 ms: `FS1_TAP`/`FS2_TAP`.
- Individual hold at 800 ms: `FS1_HOLD`/`FS2_HOLD` and suppress tap.
- Both footswitches held together for 800 ms: dispatch one `BOTH_HOLD` and suppress both individual actions.
- Reset: dispatch `RESET`.

Use pointer capture and clear every hold timer on `pointerup`, `pointercancel`, and `lostpointercapture` so a released control cannot fire later.

- [ ] **Step 3: Render all four screen modes**

`render(state)` updates the portrait screen without replacing the physical control buttons:

- Performance: preset index/name, active/bypass badge, AMP, CAB, five meters, dirty marker.
- Browse: bounded preset list and cursor.
- Edit: eight fields, selected row, and navigation/edit color state.
- Tuner: deterministic note `A4`, `440.0 Hz`, centered cents marker, and `DEMO SIGNAL` label.

It also updates every encoder's `aria-valuenow`/`aria-valuetext` and the contextual help list. Do not simulate audio meters with random values; fixed values keep screenshots and tests deterministic.

- [ ] **Step 4: Finish the written manual**

After the simulator, write complete sections with anchored headings:

```text
#quick-start        Connections, boot, first sound, safe initial levels
#signal-flow        Gate → compressor → NAM → IR → EQ → delay
#controls           Five encoders and two footswitches
#performance        Live control, preset tap, save/revert holds
#browse             Enter, move, select, cancel
#edit               Enter, navigate, modify, apply/save, cancel
#tuner              Both-switch chord, muted output, exit
#display            Active/bypass, edited, overload, amp/cab, meters
#troubleshooting    No audio, bypassed model, missing IR, no display, ignored control
```

For every gesture, state whether it is a tap, 800 ms hold, encoder turn, or encoder click. Keep the simulator beside the current-mode help on wide layouts and stacked above it on narrow layouts.

- [ ] **Step 5: Verify simulator and manual**

Run:

```bash
node --check site/assets/simulator.js
node --test tests/site/simulator-state.test.mjs
python3 tools/check_site.py site
```

Expected: syntax check exits 0, 4 state tests pass, site check passes.

Manually verify with a local server:

```bash
python3 -m http.server 8000 --directory site
```

Check keyboard, wheel, pointer drag, click/hold suppression, both-switch tuner entry, reset, 390 px responsive layout, and documentation with JavaScript disabled.

- [ ] **Step 6: Commit the manual**

```bash
git add site/manual.html site/assets/site.css site/assets/simulator.js
git commit -m "feat: add interactive pedal manual"
```

---

### Task 5: Developer Documentation

**Files:**
- Modify: `site/developers.html`

- [ ] **Step 1: Write the developer guide from canonical sources**

Use these source files before writing each section:

| Section | Canonical source |
|---|---|
| Build flags/sources | `Makefile` |
| Audio flow | `AudioEngine.h`, `AudioEngine.cpp`, `main.cpp` |
| Controls/UI | `Controls.h`, `Controls.cpp`, `Ui.h`, `main.cpp` |
| Data layout | `data_format.h`, `QspiStorage.*`, `PresetManager.*` |
| Data tools | `tools/README.md`, `tools/build_data_image.py`, `tools/inspect_data_image.py`, `tools/flash_data.sh` |
| Submodule patches | `docs/SUBMODULE_CHANGES.md`, `tools/apply_submodule_patches.sh` |
| Tests/CI | `tests/Makefile`, `.github/workflows/ci.yml`, `.github/workflows/release.yml` |

Write anchored sections for prerequisites, first build, repository map, real-time architecture, UI state flow, QSPI address map, data-image workflow, firmware flashing, data flashing, USB bootloader entry, host tests, CI/releases, submodule patches, and contributing.

Every command must be copyable and begin from a stated working directory. Explain the mandatory `BOOT_QSPI` target and the distinct application/data addresses `0x90040000` and `0x90200000`. State the current supported NAM limitation from the build flags rather than describing general NAM support.

- [ ] **Step 2: Add compact diagrams and reference tables**

Use accessible HTML/CSS, not raster images, for:

- Audio IRQ versus main-loop responsibility.
- QSPI address range table.
- Preset application sequence.
- Model/IR/preset data-image flow.

Add `aria-label` to each diagram and repeat the sequence in text immediately after it.

- [ ] **Step 3: Verify commands and links**

Run:

```bash
python3 tools/build_data_image.py --help
python3 tools/inspect_data_image.py --help
python3 tools/check_site.py site
```

Expected: both tools print usage and exit 0; site check passes. If a tool uses a different help contract, document its actual invocation and add a non-mutating command that proves the documented syntax.

- [ ] **Step 4: Commit the developer guide**

```bash
git add site/developers.html
git commit -m "docs: add developer website guide"
```

---

### Task 6: Verified Hardware and Assembly Guide

**Files:**
- Modify: `site/hardware.html`

- [ ] **Step 1: Add the verified pin table**

Transcribe only `HardwareConfig.h` values:

| Control | Daisy pins |
|---|---|
| FS1 next/save | D15 |
| FS2 previous/revert | D16 |
| Encoder 1 A/B/click | D0 / D1 / D2 |
| Encoder 2 A/B | D7 / D8 |
| Encoder 3 A/B | D9 / D10 |
| Encoder 4 A/B | D27 / D28 |
| Encoder 5 A/B | D29 / D30 |
| Display SCK/MOSI | D22 / D18 |
| Display CS/DC/RST/BLK | D13 / D14 / D26 / D24 |

State that footswitches are active-low with internal pull-ups, encoders 2–5 have no click input, and the display is a 240×320 ST7789 on SPI1. Include STM32 port names only after verifying them against the pinned libDaisy board definition; omit an unverified port name rather than copying the stale page.

- [ ] **Step 2: Write the no-guessing assembly sequence**

Add these sections in order:

1. `Before you start`: explicitly list the defined digital controls/display and the missing analog I/O/power schematic, BOM, and enclosure drawings.
2. Daisy orientation: link to the official Daisy Seed pinout and tell the builder to confirm the D-number labels before wiring.
3. Common wiring rules: shared ground, active-low switch wiring, internal pull-ups, display signal directions, and no reassignment of fixed SPI pins.
4. Per-subsystem wiring tables: display, primary encoder, encoders 2–5, footswitches, fixed codec/QSPI/USB resources.
5. Pre-power checks: inspect shorts, continuity to intended D pins, no continuity between power rails and ground, display supply matches the exact module datasheet, and all active-low inputs rest high.
6. Staged bring-up: Daisy/USB, serial log, display, controls, data partition, then audio using the project's eventual verified analog board.
7. Troubleshooting matrix: symptom, likely defined cause, check, and safe next action.

Never state a display supply voltage, audio jack circuit, anti-pop circuit, power supply topology, enclosure dimension, or part number unless a canonical project source is added first.

- [ ] **Step 3: Add the corrected product/wiring overview**

Use CSS boxes and lines to show five top encoders, centered portrait display, and two bottom footswitches. Label the visual `Conceptual control placement — not a mechanical drawing`. Add a separate Daisy connection map keyed by D-number; do not imply physical wire routing.

- [ ] **Step 4: Validate and commit hardware documentation**

Run:

```bash
python3 tools/check_site.py site
rg -n "ENC2_PRESENT|optional|ENC2_CLICK|D9.*click" site/hardware.html
```

Expected: site check passes; the stale encoder terms produce no matches.

```bash
git add site/hardware.html
git commit -m "docs: add verified hardware website guide"
```

---

### Task 7: Make the Website Authoritative

**Files:**
- Modify: `README.md`
- Modify: `docs/HARDWARE.md`

- [ ] **Step 1: Replace stale duplicate documentation**

Reduce `README.md` to:

- Project name and one-paragraph purpose.
- Correct full signal chain.
- Links to `https://balazsbencs.github.io/daisy-nam-pedal/`, including its manual, developer guide, and hardware guide pages.
- A short local build block: clone, recursive submodules, patch script, libDaisy/DaisySP builds, and `make`.
- License/repository status if already defined by the repository.

Do not retain the stale partial pin table or duplicate long build explanations.

Replace `docs/HARDWARE.md` with a short compatibility notice linking to `../site/hardware.html` for source checkouts and `https://balazsbencs.github.io/daisy-nam-pedal/hardware.html` for readers on GitHub. State that `HardwareConfig.h` remains the firmware source of truth.

- [ ] **Step 2: Check documentation references**

Run:

```bash
rg -n "docs/HARDWARE.md|ENC2_PRESENT|ENC2_CLICK" README.md docs site
python3 tools/check_site.py site
```

Expected: only the intentional compatibility pointer may mention `docs/HARDWARE.md`; stale encoder constants produce no documentation matches; site check passes.

- [ ] **Step 3: Commit authoritative-doc cleanup**

```bash
git add README.md docs/HARDWARE.md
git commit -m "docs: make project website authoritative"
```

---

### Task 8: GitHub Pages CI and Final Verification

**Files:**
- Create: `.github/workflows/pages.yml`

- [ ] **Step 1: Add validation and deployment workflow**

Create `.github/workflows/pages.yml` with:

```yaml
name: Pages

on:
  pull_request:
    branches: [main]
    paths:
      - 'site/**'
      - 'tools/check_site.py'
      - 'tests/site/**'
      - '.github/workflows/pages.yml'
  push:
    branches: [main]
    paths:
      - 'site/**'
      - 'tools/check_site.py'
      - 'tests/site/**'
      - '.github/workflows/pages.yml'
  workflow_dispatch:

concurrency:
  group: pages-${{ github.ref }}
  cancel-in-progress: true

jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
      - run: python3 -m unittest tests/site/test_check_site.py -v
      - run: python3 tools/check_site.py site
      - run: node --check site/assets/site.js
      - run: node --check site/assets/simulator.js
      - run: node --test tests/site/simulator-state.test.mjs

  deploy:
    if: github.event_name != 'pull_request'
    needs: validate
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    permissions:
      contents: read
      pages: write
      id-token: write
    steps:
      - uses: actions/checkout@v7
      - uses: actions/configure-pages@v6
      - uses: actions/upload-pages-artifact@v5
        with:
          path: site
      - id: deployment
        uses: actions/deploy-pages@v5
```

These major versions were verified against the official action repositories on 2026-06-29. Do not add broad workflow-level write permissions.

- [ ] **Step 2: Run the complete local site suite**

Run:

```bash
python3 -m unittest tests/site/test_check_site.py -v
python3 tools/check_site.py site
node --check site/assets/site.js
node --check site/assets/simulator.js
node --test tests/site/simulator-state.test.mjs
git diff --check
```

Expected: 2 Python tests pass, site check passes, both syntax checks exit 0, 4 Node tests pass, and `git diff --check` prints nothing.

- [ ] **Step 3: Perform browser acceptance checks**

Serve `site/` locally and verify:

- All primary navigation and in-page TOC links on desktop and at 390 px.
- Five encoders remain top-aligned, portrait display remains centered, and two footswitches remain below.
- Simulator works with keyboard, mouse wheel, pointer drag, touch-style taps, individual holds, and both-switch hold.
- Written manual remains readable with JavaScript disabled.
- Focus order follows the visual order and every interactive control has an accessible label/value.
- Reduced-motion mode removes nonessential animation.
- Unknown route content in `404.html` exposes links to all four main pages.
- Pages load correctly from a subpath, e.g. `http://localhost:8000/repository/`, by testing a copy of `site/` nested under that directory.

- [ ] **Step 4: Commit Pages deployment**

```bash
git add .github/workflows/pages.yml
git commit -m "ci: deploy project website to pages"
```

- [ ] **Step 5: Review final change scope**

Run:

```bash
git status --short
git log --oneline --max-count=10
```

Expected: only the user's pre-existing unrelated modifications/untracked files remain; website work is represented by focused commits from Tasks 1–8.
