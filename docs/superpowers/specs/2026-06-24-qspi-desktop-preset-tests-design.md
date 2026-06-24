# QSPI Desktop Preset Compatibility — Design Spec

**Date:** 2026-06-24

## Goal

Make the Daisy Seed firmware and desktop app agree on the QSPI data image format,
especially preset blobs that reference NAM models, cabinet IRs, and EQ settings.
Add tests that catch format drift before flashing hardware.

## Current Findings

- Firmware `NamPreset` is 98 bytes and includes six appended EQ floats.
- Firmware accepts legacy 74-byte preset blobs by zero-filling missing EQ values
  and falling back to default EQ frequencies when stored frequencies are zero.
- The desktop app currently stores presets without EQ fields.
- The desktop app currently packs preset blobs as 74 bytes, so EQ settings cannot
  reach the pedal from the app.
- The desktop flash target address is `0x90200000`, which matches the firmware
  QSPI data partition.

## Design

The firmware `data_format.h` remains the source of truth:

- `NamDataHeader`: 8 bytes
- `NamDataEntry`: 48 bytes
- `NamPreset`: 98 bytes
- data partition address: `0x90200000`
- data partition size: 6 MiB
- model entries: raw `.namb`
- IR entries: float32 little-endian mono taps, trimmed to firmware limits
- preset entries: packed model name, IR name, gain, volume, bypass, EQ gains,
  and EQ frequencies

The desktop app will mirror the firmware preset layout directly in Rust. TypeScript
and Rust preset models will both gain EQ fields with safe defaults:

- bass, mid, and treble gain default to `0.0` dB
- bass frequency defaults to `100.0` Hz
- mid frequency defaults to `750.0` Hz
- treble frequency defaults to `4000.0` Hz

Existing app `presets.json` files remain loadable through serde defaults.

## Desktop App Changes

- Extend `src-tauri/src/types.rs::Preset` with EQ fields.
- Extend `src/lib/types.ts::Preset` with matching EQ fields.
- Update `src/pages/PresetsPage.tsx` with EQ controls.
- Update `src-tauri/src/image_builder.rs::pack_preset_blob()` to emit the full
  98-byte preset blob.
- Update `src-tauri/src/commands/flash.rs::build_image()` to pass EQ values into
  the packer.
- Keep the existing flash address unless hardware read-back proves otherwise.

## Firmware Changes

Firmware behavior stays unchanged unless tests expose a real compatibility bug.
The firmware-side work is limited to tests and storage-format comments.

- Update comments in `data_format.h` that still mention the older 74-byte Python
  format.
- Add or expand tests around legacy 74-byte preset loading and full 98-byte EQ
  preset loading.

## Test Plan

Firmware tests:

- Keep `cd tests && make run` as the main firmware host verification.
- Add coverage for loading a 98-byte preset with non-default EQ and confirming
  `PresetManager::ApplyPreset()` forwards all EQ values to `AudioEngine`.
- Keep legacy 74-byte preset coverage to protect backwards compatibility.
- Use fixture models and IRs from `models/` and `ir/` for image-level smoke tests
  where host runtime cost stays reasonable.

Desktop Rust tests:

- Assert `pack_preset_blob()` returns exactly 98 bytes.
- Assert field offsets match firmware:
  model at 0, IR at 31, input gain at 62, output volume at 66, bypass at 70,
  EQ gains at 74/78/82, EQ frequencies at 86/90/94.
- Assert default EQ values are written for old or newly-created presets.
- Assert `build()` emits valid headers, directory entries, 4 KiB-aligned blobs,
  correct entry lengths, and an image within the 6 MiB partition.

Desktop frontend checks:

- `npm run build` must pass.
- Preset creation and load paths normalize missing EQ fields before the UI edits
  or saves a preset.

Cross-repo compatibility checks:

- Build a desktop image containing at least one model, one IR, and one EQ preset.
- Parse the image with firmware-compatible struct assumptions.
- Confirm the preset blob length is 98 bytes and EQ values round-trip.
- Run the firmware `tools/inspect_data_image.py` against app-built output as a
  cheap external validator when the image is available as a file.

Hardware verification:

- Use app flashing and the existing firmware `tools/flash_data.sh` path as
  comparable flows.
- Add read-back verification if `dfu-util` supports it reliably with this board:
  read the data partition header after flashing, inspect magic/version/count,
  and report the visible entries.
- Consider adding `-w` to the app's `dfu-util` invocation so the app can wait for
  DFU the same way `tools/flash_data.sh` does.

## Success Criteria

- Desktop-created preset blobs are 98 bytes and include EQ.
- Existing desktop presets migrate without crashing or losing model/IR/gain data.
- Firmware host tests, desktop Rust tests, and desktop frontend build all pass.
- A desktop-built image can be inspected and shows model, IR, and preset entries
  with aligned offsets and expected lengths.
- Flashing continues to target `0x90200000`, and any remaining hardware visibility
  issue has a read-back diagnostic path.

## Out Of Scope

- Changing the firmware storage version.
- Removing legacy 74-byte preset compatibility.
- Reworking NAM model parsing or neural inference.
- Replacing the app's library/import workflow.
