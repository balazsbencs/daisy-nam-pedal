# Code Review Fixes Design

## Goal

Fix every actionable defect from the repository review while reducing steady-state audio CPU usage without changing the pedal's sound when effects are active.

## Preset persistence

The firmware executes from the same QSPI device that stores presets. libDaisy rejects erase and write operations in this mode, and hardware experiments already ruled out a reliable RAM-resident workaround. The firmware will therefore stop claiming that it can persist presets live.

Edits will remain active in RAM for the current session and remain visibly dirty. Save actions will report that desktop/DFU synchronization is required. The dead `QspiStorage::WritePreset` API and its callers will be removed. No bootloader protocol or desktop application work is added.

## Input validation and real-time safety

- `QspiStorage::Init` will validate the directory size, entry types, terminated names, and overflow-safe blob ranges before exposing entries.
- The NAMB loader will reject null buffers, invalid or unaligned weight offsets, arithmetic overflow, inconsistent model-block sizes, and nested condition-DSP weight counts that exceed the available weights.
- `BinaryReader` bounds checks will use subtraction rather than potentially overflowing addition.
- `AudioEngine` will share the hardware block-size constant and output silence instead of writing beyond its scratch buffers when given an oversized block.
- Firmware builds marked `NAM_A2_NANO_ONLY` will reject eight-channel A2 models and omit their implementation from the image.

## CPU reductions

- Flat EQ will return before processing samples. EQ state will be cleared by the audio thread when an active EQ becomes flat so stale state cannot reappear later.
- In `NAM_SAMPLE_FLOAT` builds, the A2 fast path will use the input and output buffers directly instead of copying through `_cond` and `_head_out`.
- Per-sample peak diagnostics and cycle accounting will be compiled out by default behind `NAM_ENABLE_AUDIO_DIAGNOSTICS`; profiling builds can explicitly enable them.
- ITCM relocation and larger audio blocks remain measurement-gated experiments, not part of this fix.

## Tests and maintained patches

Tests will be written or corrected before each production change. The host Makefile conflict markers will be removed, and the stale 511-tap test will follow `kMaxTaps`. New regression coverage will exercise malformed QSPI directories, malformed NAMB layout values, oversized audio blocks, flat-EQ bypass/state clearing, and nano-only compilation.

The root host suite and ARM firmware build must pass. NeuralAmpModelerCore and nam-binary-loader changes will be reflected in their maintained patch files so a clean clone plus `tools/apply_submodule_patches.sh` reproduces the firmware sources.

## Success criteria

- The firmware never reports a preset as persisted when it was not written.
- Malformed QSPI and NAMB data fail safely without out-of-bounds access.
- Unsupported eight-channel models cannot enter the nano-only real-time path.
- Oversized callbacks cannot overwrite scratch memory.
- Flat presets skip EQ work, float A2 builds avoid redundant copies, and diagnostics cost no production callback cycles.
- All applicable host tests and the ARM firmware build pass from the reviewed working tree.
