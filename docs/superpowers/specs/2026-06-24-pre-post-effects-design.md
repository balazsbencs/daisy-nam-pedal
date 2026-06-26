# Pre/Post Effects Presets Design Spec

**Date:** 2026-06-24

## Goal

Add efficient per-preset pre and post effects to the Daisy Seed NAM pedal:

- Pre NAM: input noise gate with threshold.
- Pre NAM: compressor with threshold, ratio, attack, and release.
- Post EQ: simple digital delay with time, repeats, mix, and tone.

The feature must preserve existing preset behavior, keep the audio callback cheap,
and keep the desktop app and firmware preset formats locked together with tests.

## Current Findings

- Firmware audio currently flows through `AudioEngine::Process()` in
  `AudioEngine.cpp`.
- `Eq3` exists and preset EQ fields are loaded, but `AudioEngine::Process()` does
  not currently call `eq_.Process()`. The implementation must fix and test the
  post-EQ stage before placing delay after it.
- Preset storage is a packed `NamPreset` in `data_format.h`.
- Existing preset blob sizes are:
  - 74 bytes: original model/IR/gain/volume/bypass layout.
  - 98 bytes: current layout with six appended EQ floats.
- Firmware already zero-fills missing appended fields for legacy blobs.
- The desktop app already mirrors the 98-byte EQ layout in Rust and TypeScript.

## Recommended Design

Use three small hand-written DSP units and append their state to the existing
`NamPreset` record. Do not use DaisySP dynamics or delay classes for this feature:
the needed algorithms are simple enough, and avoiding a dependency keeps CPU,
memory, and behavior predictable.

New signal chain:

```text
input
  -> input_gain
  -> noise gate
  -> compressor
  -> NAM model
  -> IR convolver
  -> 3-band EQ
  -> digital delay
  -> output_volume
  -> mono out copied to L/R by caller
```

The new effects default to bypassed for all legacy presets. The desktop app should
show usable defaults for new presets, but firmware must not enable a new effect
just because an older preset blob zero-filled its appended fields.

## DSP Algorithms

Noise gate:

- Mono envelope follower over `fabs(input)`.
- Threshold stored in dBFS, converted to linear in the non-audio setter.
- No per-sample logarithms, exponentials, allocation, or branching-heavy state.
- Use a fixed smoothing pair internally: fast enough to clamp idle noise, slow
  enough to avoid zippering. A single threshold parameter is enough for the first
  version; use a small fixed hysteresis internally if tests show chatter.

Compressor:

- Feed-forward envelope follower over `fabs(input)`.
- Threshold in dBFS converted to linear in the setter.
- Ratio clamped to a practical range, for example `1.0..20.0`.
- Attack and release converted to one-pole coefficients in the setter.
- Use a cheap linear-domain target gain:
  - below threshold: `target_gain = 1.0`
  - above threshold: compressed envelope is
    `threshold + (env - threshold) / ratio`
  - gain is `compressed_env / env`
- Smooth gain with attack/release. Avoid per-sample `pow`, `log10`, and `exp`.

Delay:

- Static mono circular buffer in `AudioEngine`, not heap allocation from the audio
  callback.
- Maximum delay should be capped conservatively. Start with 750 ms at 48 kHz
  (`36,000` samples, about 144 KB as `float`) unless memory pressure requires
  500 ms.
- Time, repeats, mix, and tone are set from the main loop and copied as scalar
  values for the audio callback.
- No interpolation in the first version; convert time to integer samples. This is
  cheaper and fine for preset-level edits. If future live delay-time sweeps are
  needed, add interpolation then.
- Tone is a one-pole low-pass in the feedback path. Map the UI `tone` value to a
  precomputed coefficient in the setter.

## Preset Format

Keep `NAM_DATA_VERSION` at `1` if the firmware continues to accept both legacy
sizes. Append fields after the current 98-byte EQ block:

```c
uint8_t noise_gate_enabled;   // 0 = bypassed, 1 = active
uint8_t compressor_enabled;   // 0 = bypassed, 1 = active
uint8_t delay_enabled;        // 0 = bypassed, 1 = active
uint8_t fx_pad;               // explicit padding
float   noise_gate_threshold_db;
float   compressor_threshold_db;
float   compressor_ratio;
float   compressor_attack_ms;
float   compressor_release_ms;
float   delay_time_ms;
float   delay_repeats;
float   delay_mix;
float   delay_tone;
```

New packed preset size: 138 bytes.

Desktop and firmware packers should mirror this as:

```text
<31s31sffB3x6f3B1x9f
```

Defaults for new presets:

- `noise_gate_enabled = false`
- `noise_gate_threshold_db = -70.0`
- `compressor_enabled = false`
- `compressor_threshold_db = -18.0`
- `compressor_ratio = 2.0`
- `compressor_attack_ms = 10.0`
- `compressor_release_ms = 100.0`
- `delay_enabled = false`
- `delay_time_ms = 350.0`
- `delay_repeats = 0.25`
- `delay_mix = 0.18`
- `delay_tone = 0.5`

Firmware must clamp loaded values before applying them. For legacy blobs, enabled
flags are zero and therefore bypassed. Parameter fallback should still apply so
the device has valid values if a later UI path enables an effect.

## Desktop App

The desktop app is the primary editor for these parameters.

Add preset fields to:

- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/types.rs`
- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`

Extend the preset editor in:

- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx`

UI should add restrained sections:

- Pre effects:
  - Noise gate switch and threshold slider.
  - Compressor switch and threshold, ratio, attack, release sliders.
- Post effects:
  - Delay switch and time, repeats, mix, tone sliders.

Do not add device-side editing for the new effect parameters in this feature. The
device UI is already dense, and adding nine more parameters would complicate the
current workflow. The firmware can apply and save the fields if they are present,
but desktop editing is the supported path.

## Firmware Files

Expected firmware changes:

- `data_format.h`: append fields to `NamPreset`, update static size assertion and
  comments.
- `PedalEffects.h` / `PedalEffects.cpp`: new small DSP units for gate,
  compressor, and delay.
- `AudioEngine.h` / `AudioEngine.cpp`: own the new effects, expose setters/getters,
  wire the chain, and call `eq_.Process()` before delay.
- `PresetManager.cpp`: apply effect defaults, clamping, and setters.
- `tools/build_data_image.py`: expand firmware-side packer format for local data
  images.
- Tests under `tests/`: cover DSP behavior, preset layout, preset apply, packer
  compatibility, and audio-engine integration.

## Test Plan

Firmware tests:

- `tests/test_data_format.cpp`: assert `NamPreset` is 138 bytes and appended field
  offsets match the packer.
- `tests/test_pedal_effects.cpp`: verify gate attenuation/opening, compressor
  gain reduction/release, delay impulse position, repeats, mix, tone, and bypass
  transparency.
- `tests/test_preset_manager.cpp`: verify 74-byte legacy, 98-byte EQ legacy, and
  138-byte full effects presets all load safely. Confirm legacy presets keep new
  effects bypassed.
- `tests/test_audio_engine.cpp`: update the existing expectation so EQ is active,
  then verify an enabled delay produces a delayed impulse after EQ.
- `tests/test_tools.py`: verify the Python packer emits 138-byte preset blobs with
  the exact same offsets.
- Run `cd /Users/bbalazs/daisy/daisy-nam-pedal/tests && make run`.

Desktop app tests:

- Rust tests in `src-tauri/src/types.rs` for serde defaults on old presets.
- Rust tests in `src-tauri/src/image_builder.rs` for 138-byte blob size and field
  offsets.
- Rust tests for image entries to confirm preset entry length is 138 and offsets
  remain 4 KiB-aligned.
- TypeScript build with `npm run build`.
- Rust tests with `cd src-tauri && cargo test`.

Cross-repo compatibility:

- The firmware Python packer and desktop Rust packer must both assert the same
  format string, size, and offsets.
- Add an app-built image test or helper that includes a model, IR, and preset with
  non-default effect values, then inspect the resulting blob offsets and values.
- Keep legacy 74-byte and 98-byte tests so existing flashed images remain valid.

Hardware verification:

- Flash a preset with all new effects bypassed and confirm it sounds unchanged
  from the same model/IR/EQ preset.
- Flash a preset with each effect enabled one at a time.
- Watch the existing serial CPU peak log. At 48-sample blocks, the callback must
  stay under the 1 ms audio period with meaningful margin. If CPU peak becomes
  unsafe, ship gate and compressor first and leave delay disabled behind the preset
  flag until the audio budget is measured.

## Pushback And Limits

The requested feature is reasonable only if the algorithms stay intentionally
simple. A studio compressor, interpolated/modulated delay, stereo delay, tap tempo,
device-side editing for every parameter, or per-sample dB math is too much for this
POC pedal while NAM and IR are already running.

The first implementation should be mono, preset-edited, non-modulated, and
allocation-free in the audio callback.

## Success Criteria

- Existing presets load and sound unchanged.
- New presets can store and apply all pre/post effect settings.
- Desktop-generated QSPI images match firmware struct layout exactly.
- Firmware host tests, desktop Rust tests, and desktop TypeScript build pass.
- Hardware CPU logging shows the new effects do not jeopardize real-time audio.
