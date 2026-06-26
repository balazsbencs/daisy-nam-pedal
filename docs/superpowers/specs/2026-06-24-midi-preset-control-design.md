# MIDI Preset Control Design Spec

**Date:** 2026-06-24

## Goal

Add a TRS MIDI input so external MIDI controllers can switch pedal presets and
enter/exit tuner mode.

Phase one should be simple and dependable:

```text
MIDI controller -> TRS MIDI In -> Program Change -> pedal loads preset
MIDI controller -> TRS MIDI In -> CC 82 -> pedal toggles tuner mode
```

No desktop application work is required for phase one. If configurable MIDI
channels or custom mappings are needed later, that should be a separate feature
with desktop preset/global settings.

## Current Findings

- Preset switching is currently centralized in `main.cpp`.
- FS1 tap advances to the next preset and FS2 tap moves to the previous preset.
- `PresetManager` supports `Next()`, `Prev()`, `Current()`, `Count()`, and
  `Apply(...)`, but does not currently expose `SetCurrent(index)`.
- libDaisy already provides `daisy::MidiUartHandler` and `MidiUartTransport` for
  UART/TRS/DIN MIDI at 31250 baud.
- libDaisy's default UART MIDI pins are USART1 on D13/D14, but this project uses
  D13/D14 for the display.
- UART4 on D11/D12 is available in the current firmware pin map:
  - D11 / PB8 can be UART4 RX.
  - D12 / PB9 can be UART4 TX.
- MIDI phase one only needs MIDI In, but reserving D12 now leaves room for MIDI
  Out or soft Thru later.

## Hardware Decision

Use a TRS MIDI **Input** jack wired as MIDI Type A, with the MIDI input converted
to the Daisy's 3.3 V UART RX level before it reaches D11.

Important electrical constraint: do not wire the TRS jack directly to the Daisy
UART pin. MIDI input should use the normal MIDI input front end, ideally an
opto-isolator or equivalent isolated receiver, with the receiver output feeding
D11 / PB8.

Phase-one pin assignment:

| Function | Daisy pin | STM32 pin | Peripheral |
|----------|-----------|-----------|------------|
| MIDI RX  | D11       | PB8       | UART4 RX   |
| Reserved MIDI TX | D12 | PB9     | UART4 TX   |

D12 does not need to be populated for MIDI In-only hardware, but the firmware can
still configure it as UART TX if libDaisy's MIDI transport requires TX/RX mode.

## MIDI Behavior

Phase-one behavior:

- Listen on all MIDI channels by default.
- Handle `Program Change` messages.
- Program number maps directly to preset index:
  - Program Change 0 selects preset 0.
  - Program Change 1 selects preset 1.
  - Program Change 31 selects preset 31.
- Ignore Program Change values outside the loaded preset count in the application
  helper before changing presets.
- Ignore all other MIDI messages for phase one.
- MIDI preset changes clear unsaved live-edit state, matching footswitch preset
  navigation.
- CC 82 value >= 64 toggles tuner mode:
  - if tuner is inactive, enter tuner mode;
  - if tuner is active, exit tuner mode;
  - ignore CC 82 values below 64 so latch-style controllers do not double-toggle
    on release.

Channel choice:

- Default to Omni because it works with the most controllers without setup.
- Add a compile-time constant for a specific channel later if needed.
- Do not add device UI for MIDI channel selection in phase one.

## Mode Interactions

MIDI preset switching should be active during normal performance mode. MIDI tuner
toggle should be active in performance mode and tuner mode.

To avoid surprising writes or discarding in-progress edits:

- Ignore MIDI preset switching while the edit screen is active.
- Ignore MIDI preset switching while tuner mode is active.
- Allow MIDI tuner toggle while tuner mode is active so the same controller
  command can exit tuning.
- Ignore MIDI tuner toggle while the edit screen is active.
- Ignore MIDI preset switching during USB CDC library sync/update mode.
- Ignore MIDI tuner toggle during USB CDC library sync/update mode.
- If browse mode is active, apply the MIDI preset and return to performance mode,
  because browse mode is a selection overlay rather than a destructive edit.
- If browse mode is active and MIDI tuner toggle arrives, exit browse and enter
  tuner mode.

If phase-one implementation lands before tuner or USB CDC sync, structure the
code so those later modes can block MIDI preset changes with one predicate, for
example `CanApplyExternalPresetChange()`.

## Firmware Architecture

Add two small units:

- `MidiPresetControl`
  - Host-testable mapping from parsed MIDI messages to high-level commands.
  - Converts Program Change into `SelectPreset(index)`.
  - Converts CC 82 value >= 64 into `ToggleTuner`.
  - Holds constants for Omni/channel filtering and supported CCs.
- `MidiHardware`
  - Daisy-only wrapper around `daisy::MidiUartHandler`.
  - Initializes UART4 with D11/D12.
  - Polls MIDI events from the main loop.
  - Emits `MidiPresetCommand` values to `main.cpp`.

`main.cpp` should not parse raw MIDI directly. It should receive a high-level
command and call the same preset-apply helper used by footswitch and browse
navigation, or the same tuner enter/exit helpers used by the both-footswitch
tuner gesture.

Add a helper in `main.cpp`:

```cpp
static void ApplyPresetIndex(uint8_t target_idx, const char* source_label);
```

This helper should:

1. Clamp or reject invalid indexes before changing state.
2. Move `PresetManager` to the requested index.
3. Call `presets.Apply(...)`.
4. Clear `preset_dirty`.
5. Push the performance screen.
6. Print a concise diagnostic with the source label, for example `MIDI`.

This prevents footswitch, browse, and MIDI paths from drifting apart.

## Optional CC Support

Program Change and CC 82 tuner toggle are required phase-one features.

If it is still easy after those work, add fixed CC commands:

- CC 80 value >= 64: next preset.
- CC 81 value >= 64: previous preset.
- CC 82 value >= 64: toggle tuner mode. This one is required, not optional.

These should be documented as convenience defaults only. Do not add configurable
CC mappings until there is a global settings feature or desktop-app support.

## CPU And Reliability

MIDI is cheap compared with the audio DSP:

- UART MIDI bandwidth is tiny.
- libDaisy receives UART data by DMA into a small buffer.
- Event parsing and preset command handling run in the main loop.
- Do not touch preset/model/IR state from interrupt callbacks.
- Do not call `presets.Apply(...)` from a MIDI callback.

The only audible cost is the existing preset load/apply time. That is already
true for footswitch switching.

## Test Strategy

Host tests:

- Program Change on any channel selects the expected preset index in Omni mode.
- Program Change above the loaded preset count is ignored by the application
  helper.
- Non-Program-Change messages are ignored.
- CC 82 values >= 64 map to tuner toggle.
- CC 82 values below 64 are ignored.
- Optional CC next/previous commands map only when value is 64 or greater.

Firmware build checks:

- `MidiHardware` is excluded or shimmed cleanly in `HOST_BUILD`.
- Firmware builds with `hid/midi.h` included.
- UART4 is initialized on D11/D12.

Hardware validation:

- Connect a known TRS MIDI Type A controller.
- Send Program Change 0, 1, and the highest loaded preset index.
- Confirm the screen and audio switch to the expected presets.
- Confirm out-of-range Program Change messages do not crash or change presets.
- Confirm footswitch preset switching still works.
- Confirm MIDI messages do not trigger preset changes while editing.
- Confirm CC 82 enters tuner mode from performance mode.
- Confirm CC 82 exits tuner mode when already tuning.
- Confirm Program Change is ignored while tuner mode is active.

## Non-Goals

- MIDI Out.
- MIDI Thru.
- USB MIDI.
- MIDI clock sync.
- MIDI control of effect parameters.
- Desktop app MIDI configuration.
- Per-preset MIDI mappings.
