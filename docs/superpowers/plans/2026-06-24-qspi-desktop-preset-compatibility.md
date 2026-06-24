# QSPI Desktop Preset Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the desktop app emit firmware-compatible 98-byte preset blobs with EQ fields, and add tests that catch QSPI image format drift before flashing hardware.

**Architecture:** Keep `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h` as the storage-format authority. Update the desktop app's Rust image builder and preset schema to mirror that layout, normalize old app presets through serde/defaults and frontend helpers, then verify with Rust, TypeScript build, firmware host tests, and image inspection.

**Tech Stack:** C++17 firmware host tests with `make`, Rust/Tauri backend with `cargo test`, React/TypeScript frontend with Vite, Python image inspection tools.

---

## File Structure

- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/image_builder.rs`
  - Owns QSPI image binary packing for the desktop app.
  - Add 98-byte preset packing and unit tests for field offsets.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/types.rs`
  - Owns serialized Rust DTOs for app data.
  - Add EQ fields with serde defaults and migration tests.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/flash.rs`
  - Owns app image build and DFU invocation.
  - Pass EQ fields into the packer and add `-w` to match the firmware script's wait behavior.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`
  - Owns frontend TypeScript DTOs.
  - Add EQ fields plus a small preset normalization helper.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx`
  - Owns preset editing UI.
  - Normalize loaded presets and add EQ gain/frequency controls.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h`
  - Update stale comments that still describe the old 74-byte preset format.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_preset_manager.cpp`
  - Add firmware host coverage for full 98-byte EQ preset load and legacy 74-byte load.

---

### Task 1: Rust Packer Tests For Firmware Layout

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/image_builder.rs`

- [ ] **Step 1: Write failing tests for 98-byte preset layout**

Add this test module to the bottom of `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/image_builder.rs`:

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use byteorder::{ByteOrder, LittleEndian};

    fn f32_at(bytes: &[u8], offset: usize) -> f32 {
        LittleEndian::read_f32(&bytes[offset..offset + 4])
    }

    #[test]
    fn preset_blob_matches_firmware_eq_layout() {
        let blob = pack_preset_blob(
            "Plexi",
            "V30",
            0.9,
            0.75,
            true,
            -3.0,
            2.5,
            4.0,
            120.0,
            800.0,
            3500.0,
        );

        assert_eq!(blob.len(), 98);
        assert_eq!(&blob[0..5], b"Plexi");
        assert_eq!(&blob[31..34], b"V30");
        assert!((f32_at(&blob, 62) - 0.9).abs() < 0.0001);
        assert!((f32_at(&blob, 66) - 0.75).abs() < 0.0001);
        assert_eq!(blob[70], 1);
        assert_eq!(&blob[71..74], &[0, 0, 0]);
        assert!((f32_at(&blob, 74) - (-3.0)).abs() < 0.0001);
        assert!((f32_at(&blob, 78) - 2.5).abs() < 0.0001);
        assert!((f32_at(&blob, 82) - 4.0).abs() < 0.0001);
        assert!((f32_at(&blob, 86) - 120.0).abs() < 0.0001);
        assert!((f32_at(&blob, 90) - 800.0).abs() < 0.0001);
        assert!((f32_at(&blob, 94) - 3500.0).abs() < 0.0001);
    }

    #[test]
    fn image_builder_records_preset_length_and_alignment() {
        let preset = pack_preset_blob(
            "Amp",
            "Cab",
            1.0,
            0.8,
            false,
            0.0,
            0.0,
            0.0,
            100.0,
            750.0,
            4000.0,
        );
        let built = build(&[
            Blob {
                entry_type: ENTRY_MODEL,
                name: "Amp".into(),
                data: vec![0xAA, 0xBB, 0xCC],
                samplerate: 0,
            },
            Blob {
                entry_type: ENTRY_IR,
                name: "Cab".into(),
                data: vec![0; 16],
                samplerate: 48_000,
            },
            Blob {
                entry_type: ENTRY_PRESET,
                name: "Lead".into(),
                data: preset,
                samplerate: 0,
            },
        ]);

        assert_eq!(LittleEndian::read_u32(&built.data[0..4]), NAM_DATA_MAGIC);
        assert_eq!(LittleEndian::read_u16(&built.data[4..6]), NAM_DATA_VERSION);
        assert_eq!(LittleEndian::read_u16(&built.data[6..8]), 3);
        assert!(built.data.len() as u32 <= partition_size());

        assert_eq!(built.entries[0].0, ENTRY_MODEL);
        assert_eq!(built.entries[1].0, ENTRY_IR);
        assert_eq!(built.entries[2].0, ENTRY_PRESET);
        assert_eq!(built.entries[2].3, 98);

        for (_, _, offset, length) in &built.entries {
            assert_eq!(offset % SECTOR_SIZE, 0);
            assert!(*offset + *length <= built.data.len() as u32);
        }
    }
}
```

- [ ] **Step 2: Run tests to verify the new layout test fails**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test image_builder
```

Expected: compile failure because `pack_preset_blob()` currently accepts only five arguments and emits a 74-byte blob.

- [ ] **Step 3: Implement 98-byte preset packing**

Replace the preset documentation and `pack_preset_blob()` in `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/image_builder.rs` with this shape:

```rust
/// NamPreset blob (98 bytes, packed):
///   model_name[31], ir_name[31], input_gain(f32), output_volume(f32),
///   bypass(u8), pad[3], bass/mid/treble gains(f32), bass/mid/treble freqs(f32)
```

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
) -> Vec<u8> {
    let mut buf = Vec::with_capacity(98);
    write_name(&mut buf, model_name);
    write_name(&mut buf, ir_name);
    buf.write_f32::<LittleEndian>(input_gain).unwrap();
    buf.write_f32::<LittleEndian>(output_volume).unwrap();
    buf.write_u8(if bypass { 1 } else { 0 }).unwrap();
    buf.extend_from_slice(&[0u8; 3]);
    buf.write_f32::<LittleEndian>(eq_bass_gain).unwrap();
    buf.write_f32::<LittleEndian>(eq_mid_gain).unwrap();
    buf.write_f32::<LittleEndian>(eq_treble_gain).unwrap();
    buf.write_f32::<LittleEndian>(eq_bass_freq).unwrap();
    buf.write_f32::<LittleEndian>(eq_mid_freq).unwrap();
    buf.write_f32::<LittleEndian>(eq_treble_freq).unwrap();
    debug_assert_eq!(buf.len(), 98, "NamPreset blob size mismatch");
    buf
}
```

- [ ] **Step 4: Run Rust image builder tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test image_builder
```

Expected: the `image_builder` tests pass.

- [ ] **Step 5: Commit Task 1**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src-tauri/src/image_builder.rs
git commit -m "test: lock preset image layout"
```

---

### Task 2: Rust Preset Schema Defaults

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/types.rs`

- [ ] **Step 1: Write serde migration tests**

Add this test module to the bottom of `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/types.rs`:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn old_preset_json_deserializes_with_eq_defaults() {
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
        assert_eq!(preset.eq_bass_gain, 0.0);
        assert_eq!(preset.eq_mid_gain, 0.0);
        assert_eq!(preset.eq_treble_gain, 0.0);
        assert_eq!(preset.eq_bass_freq, 100.0);
        assert_eq!(preset.eq_mid_freq, 750.0);
        assert_eq!(preset.eq_treble_freq, 4000.0);
    }

    #[test]
    fn new_preset_json_preserves_eq_values() {
        let json = r#"{
            "id": "p2",
            "name": "EQ Preset",
            "model_id": null,
            "ir_id": null,
            "input_gain": 0.7,
            "output_volume": 0.6,
            "bypass": true,
            "eq_bass_gain": -2.0,
            "eq_mid_gain": 1.5,
            "eq_treble_gain": 3.0,
            "eq_bass_freq": 120.0,
            "eq_mid_freq": 900.0,
            "eq_treble_freq": 3600.0
        }"#;

        let preset: Preset = serde_json::from_str(json).unwrap();
        assert_eq!(preset.eq_bass_gain, -2.0);
        assert_eq!(preset.eq_mid_gain, 1.5);
        assert_eq!(preset.eq_treble_gain, 3.0);
        assert_eq!(preset.eq_bass_freq, 120.0);
        assert_eq!(preset.eq_mid_freq, 900.0);
        assert_eq!(preset.eq_treble_freq, 3600.0);
    }
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test types
```

Expected: compile failure because `Preset` has no EQ fields.

- [ ] **Step 3: Add EQ defaults and fields**

In `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/types.rs`, add these helpers above `Preset`:

```rust
fn default_eq_bass_freq() -> f32 { 100.0 }
fn default_eq_mid_freq() -> f32 { 750.0 }
fn default_eq_treble_freq() -> f32 { 4000.0 }
```

Then extend `Preset`:

```rust
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Preset {
    pub id:            String,
    pub name:          String,
    pub model_id:      Option<String>,
    pub ir_id:         Option<String>,
    pub input_gain:    f32,   // 0.0..2.0
    pub output_volume: f32,   // 0.0..1.0
    pub bypass:        bool,
    #[serde(default)]
    pub eq_bass_gain:  f32,
    #[serde(default)]
    pub eq_mid_gain:   f32,
    #[serde(default)]
    pub eq_treble_gain: f32,
    #[serde(default = "default_eq_bass_freq")]
    pub eq_bass_freq:  f32,
    #[serde(default = "default_eq_mid_freq")]
    pub eq_mid_freq:   f32,
    #[serde(default = "default_eq_treble_freq")]
    pub eq_treble_freq: f32,
}
```

- [ ] **Step 4: Run serde tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test types
```

Expected: the `types` tests pass.

- [ ] **Step 5: Commit Task 2**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src-tauri/src/types.rs
git commit -m "feat: add preset eq defaults"
```

---

### Task 3: Wire EQ Through Desktop Image Build And DFU Wait

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/flash.rs`

- [ ] **Step 1: Update `build_image()` to pass EQ fields**

In `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/flash.rs`, replace the current preset packer call:

```rust
let data = pack_preset_blob(model_name, ir_name,
                            p.input_gain, p.output_volume, p.bypass);
```

with:

```rust
let data = pack_preset_blob(
    model_name,
    ir_name,
    p.input_gain,
    p.output_volume,
    p.bypass,
    p.eq_bass_gain,
    p.eq_mid_gain,
    p.eq_treble_gain,
    p.eq_bass_freq,
    p.eq_mid_freq,
    p.eq_treble_freq,
);
```

- [ ] **Step 2: Add DFU wait flag**

In the same file, update the `run_dfu_util()` argument list in `flash_image()`:

```rust
let output = run_dfu_util(&app, &[
    "-w",
    "-a", "0",
    "-s", "0x90200000:leave",
    "-D", &image_path,
    "-d", ",0483:df11",
]).await?;
```

- [ ] **Step 3: Run Rust tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test
```

Expected: all Rust tests pass.

- [ ] **Step 4: Commit Task 3**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src-tauri/src/commands/flash.rs
git commit -m "feat: pack preset eq into flash image"
```

---

### Task 4: Frontend Preset Type, Normalization, And EQ Editor

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx`

- [ ] **Step 1: Extend frontend preset type and add normalizer**

In `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`, replace the `Preset` interface with:

```ts
export interface Preset {
  id:             string;
  name:           string;
  model_id:       string | null;
  ir_id:          string | null;
  input_gain:     number;   // 0.0..2.0
  output_volume:  number;   // 0.0..1.0
  bypass:         boolean;
  eq_bass_gain:   number;   // dB
  eq_mid_gain:    number;   // dB
  eq_treble_gain: number;   // dB
  eq_bass_freq:   number;   // Hz
  eq_mid_freq:    number;   // Hz
  eq_treble_freq: number;   // Hz
}
```

Add this immediately after the interface:

```ts
export const DEFAULT_EQ = {
  eq_bass_gain: 0,
  eq_mid_gain: 0,
  eq_treble_gain: 0,
  eq_bass_freq: 100,
  eq_mid_freq: 750,
  eq_treble_freq: 4000,
} satisfies Pick<
  Preset,
  | "eq_bass_gain"
  | "eq_mid_gain"
  | "eq_treble_gain"
  | "eq_bass_freq"
  | "eq_mid_freq"
  | "eq_treble_freq"
>;

export function normalizePreset(preset: Preset): Preset {
  return {
    ...DEFAULT_EQ,
    ...preset,
    eq_bass_gain: preset.eq_bass_gain ?? DEFAULT_EQ.eq_bass_gain,
    eq_mid_gain: preset.eq_mid_gain ?? DEFAULT_EQ.eq_mid_gain,
    eq_treble_gain: preset.eq_treble_gain ?? DEFAULT_EQ.eq_treble_gain,
    eq_bass_freq: preset.eq_bass_freq ?? DEFAULT_EQ.eq_bass_freq,
    eq_mid_freq: preset.eq_mid_freq ?? DEFAULT_EQ.eq_mid_freq,
    eq_treble_freq: preset.eq_treble_freq ?? DEFAULT_EQ.eq_treble_freq,
  };
}
```

- [ ] **Step 2: Import the normalizer in `PresetsPage.tsx`**

Change the type import in `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx` from:

```ts
import type { ModelInfo, IrInfo, Preset } from "@/lib/types";
```

to:

```ts
import { DEFAULT_EQ, normalizePreset, type ModelInfo, type IrInfo, type Preset } from "@/lib/types";
```

- [ ] **Step 3: Add EQ controls helper**

Add this helper above `PresetEditor()` in `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx`:

```tsx
function EqControl({
  label,
  value,
  min,
  max,
  step,
  unit,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit: string;
  onChange: (value: number) => void;
}) {
  return (
    <div className="space-y-2">
      <div className="flex justify-between">
        <Label>{label}</Label>
        <span className="text-xs text-muted-foreground">
          {unit === "Hz" ? Math.round(value) : value.toFixed(1)} {unit}
        </span>
      </div>
      <Slider
        min={min}
        max={max}
        step={step}
        value={[value]}
        onValueChange={(v) => onChange((v as number[])[0])}
      />
    </div>
  );
}
```

- [ ] **Step 4: Add EQ controls to `PresetEditor()`**

In `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/PresetsPage.tsx`, insert this block after the output volume slider and before bypass:

```tsx
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4 rounded-md border p-4">
        <EqControl
          label="Bass gain"
          value={preset.eq_bass_gain}
          min={-12}
          max={12}
          step={0.5}
          unit="dB"
          onChange={(value) => onChange({ ...preset, eq_bass_gain: value })}
        />
        <EqControl
          label="Bass freq"
          value={preset.eq_bass_freq}
          min={40}
          max={400}
          step={5}
          unit="Hz"
          onChange={(value) => onChange({ ...preset, eq_bass_freq: value })}
        />
        <EqControl
          label="Mid gain"
          value={preset.eq_mid_gain}
          min={-12}
          max={12}
          step={0.5}
          unit="dB"
          onChange={(value) => onChange({ ...preset, eq_mid_gain: value })}
        />
        <EqControl
          label="Mid freq"
          value={preset.eq_mid_freq}
          min={250}
          max={2500}
          step={25}
          unit="Hz"
          onChange={(value) => onChange({ ...preset, eq_mid_freq: value })}
        />
        <EqControl
          label="Treble gain"
          value={preset.eq_treble_gain}
          min={-12}
          max={12}
          step={0.5}
          unit="dB"
          onChange={(value) => onChange({ ...preset, eq_treble_gain: value })}
        />
        <EqControl
          label="Treble freq"
          value={preset.eq_treble_freq}
          min={1500}
          max={8000}
          step={50}
          unit="Hz"
          onChange={(value) => onChange({ ...preset, eq_treble_freq: value })}
        />
      </div>
```

- [ ] **Step 5: Normalize loaded, selected, and new presets**

In `makeNewPreset()`, add the defaults:

```ts
function makeNewPreset(): Preset {
  return {
    id: crypto.randomUUID(),
    name: `Preset ${nextNewIdx++}`,
    model_id: null,
    ir_id: null,
    input_gain: 1.0,
    output_volume: 0.8,
    bypass: false,
    ...DEFAULT_EQ,
  };
}
```

In `reload()`, replace:

```ts
setPresets(p);
```

with:

```ts
setPresets(p.map(normalizePreset));
```

In `selectPreset()`, replace:

```ts
setDraft(p ? { ...p } : null);
```

with:

```ts
setDraft(p ? normalizePreset(p) : null);
```

In `handleAdd()`, replace:

```ts
setDraft({ ...p });
```

with:

```ts
setDraft(normalizePreset(p));
```

In `handleSave()`, replace:

```ts
await api.savePreset(draft);
```

with:

```ts
await api.savePreset(normalizePreset(draft));
```

- [ ] **Step 6: Run frontend build**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
npm run build
```

Expected: TypeScript and Vite build pass.

- [ ] **Step 7: Commit Task 4**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src/lib/types.ts src/pages/PresetsPage.tsx
git commit -m "feat: edit preset eq in desktop app"
```

---

### Task 5: Firmware Preset Compatibility Tests And Comments

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_preset_manager.cpp`

- [ ] **Step 1: Add failing firmware tests for stored EQ and legacy blobs**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_preset_manager.cpp`, add this helper after `make_preset_blob()`:

```cpp
static std::vector<uint8_t> make_legacy_preset_blob(const char* mname, const char* irname,
                                                     float gain, float vol, uint8_t bypass)
{
    std::vector<uint8_t> buf(74, 0);
    strncpy(reinterpret_cast<char*>(buf.data()), mname, NAM_DATA_NAME_LEN - 1);
    strncpy(reinterpret_cast<char*>(buf.data() + 31), irname, NAM_DATA_NAME_LEN - 1);
    memcpy(buf.data() + 62, &gain, sizeof(float));
    memcpy(buf.data() + 66, &vol, sizeof(float));
    buf[70] = bypass;
    return buf;
}
```

Add these tests before `main()`:

```cpp
static void test_preset_entries_load_full_eq_blob()
{
    FakeStorage fs;
    auto blob = make_preset_blob("", "", 1.0f, 0.8f, 0);
    NamPreset* raw = reinterpret_cast<NamPreset*>(blob.data());
    raw->eq_bass_gain = -3.0f;
    raw->eq_mid_gain = 2.5f;
    raw->eq_treble_gain = 4.0f;
    raw->eq_bass_freq = 120.0f;
    raw->eq_mid_freq = 800.0f;
    raw->eq_treble_freq = 3500.0f;

    fs.AddEntry(NAM_ENTRY_PRESET, "EQ Lead", blob.data(), (uint32_t)blob.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage storage; storage.Init();
    ModelManager models; models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    const NamPreset& p = presets.ActivePreset();
    CHECK(std::fabs(p.eq_bass_gain - (-3.0f)) < 1e-6f);
    CHECK(std::fabs(p.eq_mid_gain - 2.5f) < 1e-6f);
    CHECK(std::fabs(p.eq_treble_gain - 4.0f) < 1e-6f);
    CHECK(std::fabs(p.eq_bass_freq - 120.0f) < 1e-3f);
    CHECK(std::fabs(p.eq_mid_freq - 800.0f) < 1e-3f);
    CHECK(std::fabs(p.eq_treble_freq - 3500.0f) < 1e-3f);
}

static void test_legacy_preset_blob_zero_fills_eq_fields()
{
    FakeStorage fs;
    auto blob = make_legacy_preset_blob("LegacyAmp", "LegacyCab", 0.7f, 0.6f, 0);
    fs.AddEntry(NAM_ENTRY_PRESET, "Legacy", blob.data(), (uint32_t)blob.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage storage; storage.Init();
    ModelManager models; models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    const NamPreset& p = presets.ActivePreset();
    CHECK_STR(p.model_name, "LegacyAmp");
    CHECK_STR(p.ir_name, "LegacyCab");
    CHECK(std::fabs(p.input_gain - 0.7f) < 1e-6f);
    CHECK(std::fabs(p.output_volume - 0.6f) < 1e-6f);
    CHECK(std::fabs(p.eq_bass_gain) < 1e-6f);
    CHECK(std::fabs(p.eq_mid_gain) < 1e-6f);
    CHECK(std::fabs(p.eq_treble_gain) < 1e-6f);
    CHECK(std::fabs(p.eq_bass_freq) < 1e-6f);
    CHECK(std::fabs(p.eq_mid_freq) < 1e-6f);
    CHECK(std::fabs(p.eq_treble_freq) < 1e-6f);
}
```

Call both tests in `main()` before `test_apply_eq_forwarded()`:

```cpp
    test_preset_entries_load_full_eq_blob();
    test_legacy_preset_blob_zero_fills_eq_fields();
```

- [ ] **Step 2: Run firmware preset test**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_preset_manager && ./test_preset_manager
```

Expected: `preset_manager` passes. A failure here means the existing compatibility path is not behaving as designed; inspect `/Users/bbalazs/daisy/daisy-nam-pedal/PresetManager.cpp` before continuing.

- [ ] **Step 3: Update stale format comment**

In `/Users/bbalazs/daisy/daisy-nam-pedal/data_format.h`, replace:

```c
// first float. Python packer uses "<31s31sffB3x" which also produces 74 bytes.
// static_assert below enforces the match at compile time.
```

with:

```c
// first float. Current packers use "<31s31sffB3x6f" for 98 bytes.
// Firmware still accepts older 74-byte blobs by zero-filling the appended EQ
// fields in PresetManager before applying default EQ frequencies at runtime.
// static_assert below enforces the current match at compile time.
```

- [ ] **Step 4: Run firmware tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make run
```

Expected: all firmware host tests pass.

- [ ] **Step 5: Commit Task 5**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add data_format.h tests/test_preset_manager.cpp
git commit -m "test: cover qspi preset eq compatibility"
```

---

### Task 6: Full Verification And Image Inspection

**Files:**
- No source changes expected.
- Read generated files from app tmp and firmware tools.

- [ ] **Step 1: Run desktop backend tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test
```

Expected: all Rust tests pass.

- [ ] **Step 2: Run desktop frontend build**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
npm run build
```

Expected: TypeScript and Vite build pass.

- [ ] **Step 3: Run firmware tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make run
```

Expected: all firmware host tests pass.

- [ ] **Step 4: Build a firmware fixture image for independent format inspection**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
python3 tools/build_data_image.py data -o /tmp/nam-pedal-data-image.bin
python3 tools/inspect_data_image.py /tmp/nam-pedal-data-image.bin
```

Expected: output reports magic `0x444D414E`, version `1`, aligned entries, and `all entries valid`.

- [ ] **Step 5: Inspect a desktop-built image**

After pressing "Build image" on the desktop app Flash page, run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
APP_IMAGE="$HOME/Library/Application Support/com.bbalazs.nam-platform/tmp/data_image.bin"
test -f "$APP_IMAGE"
python3 tools/inspect_data_image.py "$APP_IMAGE"
```

Expected: output reports magic `0x444D414E`, version `1`, aligned entries, and preset entry lengths of `98` bytes.

---

## Self-Review Checklist

- Spec coverage:
  - 98-byte desktop preset blobs: Task 1 and Task 3.
  - Desktop preset EQ schema and migration: Task 2 and Task 4.
  - Desktop EQ editor: Task 4.
  - Firmware comments and compatibility tests: Task 5.
  - Rust, frontend, firmware verification: Task 6.
  - Image inspection path: Task 6.
  - DFU wait behavior: Task 3.
- Type consistency:
  - Rust and TypeScript EQ field names are identical:
    `eq_bass_gain`, `eq_mid_gain`, `eq_treble_gain`, `eq_bass_freq`, `eq_mid_freq`, `eq_treble_freq`.
  - Packer argument order matches firmware byte order: gains first, then frequencies.
  - Frontend defaults match firmware defaults: `0`, `0`, `0`, `100`, `750`, `4000`.
- Worktree safety:
  - This repository already has unrelated staged and untracked test artifacts.
  - Use path-limited `git add` and commits for each task.
  - Do not revert user or generated changes outside files named in each task.
