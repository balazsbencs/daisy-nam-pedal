# MIDI Preset Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for mapping/state logic, then superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add TRS MIDI In support so Program Change messages from an external MIDI controller switch pedal presets, and CC 82 toggles tuner mode.

**Architecture:** Use libDaisy's UART MIDI handler on UART4 with D11 as MIDI RX and D12 reserved as TX. Keep raw MIDI handling in a Daisy-only wrapper, keep MIDI-to-command mapping host-testable, route preset commands through one shared preset-apply helper in `main.cpp`, and route tuner commands through the tuner mode helpers from the tuner feature.

**Tech Stack:** C++17 firmware, libDaisy `MidiUartHandler`, UART4 at 31250 baud, fixed-size MIDI event queues, existing host test `Makefile`.

---

## Scope And Ordering

- Do not use worktrees.
- Phase one is MIDI In only.
- Phase one requires Program Change preset selection.
- Phase one requires CC 82 value >= 64 to toggle tuner mode in/out.
- Optional fixed CC next/previous support may be added after Program Change works.
- No desktop application changes are required.
- Do not add configurable MIDI mappings or channel settings in this feature.
- Implement after, or rebase onto, the tuner and USB CDC plans if those modes already exist.

## File Structure

Firmware repo:

- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/HardwareConfig.h`
  - Add D11/D12 MIDI pin constants under `#ifndef HOST_BUILD`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/docs/HARDWARE.md`
  - Document TRS MIDI Type A input and D11/D12 pin usage.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiPresetControl.h`
  - Defines host-testable MIDI message and preset command types.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiPresetControl.cpp`
  - Maps Program Change, tuner toggle CC, and optional next/previous CC messages
    to high-level commands.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiHardware.h`
  - Declares Daisy-only MIDI UART wrapper.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiHardware.cpp`
  - Initializes `daisy::MidiUartHandler` on UART4 and polls events.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`
  - Add shared preset apply helper.
  - Reuse tuner enter/exit helpers when the tuner feature is present.
  - Initialize and poll MIDI.
  - Apply MIDI preset commands when allowed.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`
  - Add `MidiPresetControl.cpp` and `MidiHardware.cpp`.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_midi_preset_control.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

---

### Task 1: Add Host Tests For MIDI Preset Mapping

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_midi_preset_control.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Create the failing test file**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_midi_preset_control.cpp`:

```cpp
#include "../MidiPresetControl.h"
#include <cassert>
#include <cstdio>

static MidiPresetMessage Msg(MidiPresetMessageType type,
                             uint8_t channel,
                             uint8_t data1,
                             uint8_t data2 = 0)
{
    MidiPresetMessage msg{};
    msg.type = type;
    msg.channel = channel;
    msg.data1 = data1;
    msg.data2 = data2;
    return msg;
}

static void test_program_change_selects_index()
{
    MidiPresetConfig cfg{};
    cfg.omni = true;

    MidiPresetCommand cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ProgramChange, 0, 0), cfg);
    assert(cmd.type == MidiPresetCommandType::SelectPreset);
    assert(cmd.preset_index == 0);

    cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ProgramChange, 12, 7), cfg);
    assert(cmd.type == MidiPresetCommandType::SelectPreset);
    assert(cmd.preset_index == 7);
}

static void test_channel_filter()
{
    MidiPresetConfig cfg{};
    cfg.omni = false;
    cfg.channel = 2;

    MidiPresetCommand cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ProgramChange, 1, 3), cfg);
    assert(cmd.type == MidiPresetCommandType::None);

    cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ProgramChange, 2, 3), cfg);
    assert(cmd.type == MidiPresetCommandType::SelectPreset);
    assert(cmd.preset_index == 3);
}

static void test_non_program_change_ignored()
{
    MidiPresetConfig cfg{};
    cfg.omni = true;

    MidiPresetCommand cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::NoteOn, 0, 60, 100), cfg);
    assert(cmd.type == MidiPresetCommandType::None);
}

static void test_tuner_toggle_cc()
{
    MidiPresetConfig cfg{};
    cfg.omni = true;

    MidiPresetCommand cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ControlChange, 0, 82, 63), cfg);
    assert(cmd.type == MidiPresetCommandType::None);

    cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ControlChange, 0, 82, 64), cfg);
    assert(cmd.type == MidiPresetCommandType::ToggleTuner);

    cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ControlChange, 0, 82, 127), cfg);
    assert(cmd.type == MidiPresetCommandType::ToggleTuner);
}

static void test_optional_cc_next_prev()
{
    MidiPresetConfig cfg{};
    cfg.omni = true;
    cfg.enable_cc_next_prev = true;

    MidiPresetCommand cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ControlChange, 0, 80, 63), cfg);
    assert(cmd.type == MidiPresetCommandType::None);

    cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ControlChange, 0, 80, 64), cfg);
    assert(cmd.type == MidiPresetCommandType::NextPreset);

    cmd = MapMidiPresetMessage(Msg(MidiPresetMessageType::ControlChange, 0, 81, 127), cfg);
    assert(cmd.type == MidiPresetCommandType::PreviousPreset);
}

int main()
{
    test_program_change_selects_index();
    test_channel_filter();
    test_non_program_change_ignored();
    test_tuner_toggle_cc();
    test_optional_cc_next_prev();
    std::puts("test_midi_preset_control: PASS");
    return 0;
}
```

- [ ] **Step 2: Add the test target**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`, add
`test_midi_preset_control` to `BINARIES`:

```make
BINARIES = test_data_format test_qspi_storage test_preset_manager test_ir_loader test_eq3 test_audio_engine test_quad_encoder test_meter_fill test_real_fft_128 test_partitioned_convolver test_display_transfer test_ui_mode test_midi_preset_control
```

Add the target:

```make
test_midi_preset_control: test_midi_preset_control.cpp ../MidiPresetControl.cpp ../MidiPresetControl.h
	$(CXX) $(CXXFLAGS) $^ -o $@
```

Add to the `run` target before Python tools:

```make
	@echo "=== test_midi_preset_control ==="
	./test_midi_preset_control
```

- [ ] **Step 3: Run the failing test**

Run:

```sh
make -C tests test_midi_preset_control
```

Expected: fails because `MidiPresetControl.h` does not exist yet.

---

### Task 2: Implement Host-Testable MIDI Mapping

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiPresetControl.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiPresetControl.cpp`

- [ ] **Step 1: Create the header**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/MidiPresetControl.h`:

```cpp
#pragma once
#include <stdint.h>

enum class MidiPresetMessageType : uint8_t
{
    Other = 0,
    NoteOn,
    ControlChange,
    ProgramChange,
};

struct MidiPresetMessage
{
    MidiPresetMessageType type = MidiPresetMessageType::Other;
    uint8_t channel = 0; // 0..15, matching libDaisy's parsed channel value.
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

enum class MidiPresetCommandType : uint8_t
{
    None = 0,
    SelectPreset,
    NextPreset,
    PreviousPreset,
    ToggleTuner,
};

struct MidiPresetCommand
{
    MidiPresetCommandType type = MidiPresetCommandType::None;
    uint8_t preset_index = 0;
};

struct MidiPresetConfig
{
    bool omni = true;
    uint8_t channel = 0; // used only when omni == false
    bool enable_cc_next_prev = false;
    uint8_t cc_next = 80;
    uint8_t cc_previous = 81;
    uint8_t cc_tuner_toggle = 82;
};

MidiPresetCommand MapMidiPresetMessage(const MidiPresetMessage& msg,
                                       const MidiPresetConfig& config);
```

- [ ] **Step 2: Create the mapper implementation**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/MidiPresetControl.cpp`:

```cpp
#include "MidiPresetControl.h"

static bool ChannelMatches(uint8_t msg_channel, const MidiPresetConfig& config)
{
    return config.omni || msg_channel == config.channel;
}

MidiPresetCommand MapMidiPresetMessage(const MidiPresetMessage& msg,
                                       const MidiPresetConfig& config)
{
    MidiPresetCommand cmd{};

    if (!ChannelMatches(msg.channel, config))
        return cmd;

    if (msg.type == MidiPresetMessageType::ProgramChange)
    {
        cmd.type = MidiPresetCommandType::SelectPreset;
        cmd.preset_index = msg.data1;
        return cmd;
    }

    if (msg.type == MidiPresetMessageType::ControlChange)
    {
        if (msg.data2 < 64)
            return cmd;

        if (msg.data1 == config.cc_tuner_toggle)
            cmd.type = MidiPresetCommandType::ToggleTuner;
        else if (config.enable_cc_next_prev && msg.data1 == config.cc_next)
            cmd.type = MidiPresetCommandType::NextPreset;
        else if (config.enable_cc_next_prev && msg.data1 == config.cc_previous)
            cmd.type = MidiPresetCommandType::PreviousPreset;
    }

    return cmd;
}
```

- [ ] **Step 3: Run the mapping test**

Run:

```sh
make -C tests test_midi_preset_control
```

Expected: `test_midi_preset_control: PASS`.

---

### Task 3: Add MIDI Hardware Pin Config And Docs

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/HardwareConfig.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/docs/HARDWARE.md`

- [ ] **Step 1: Add MIDI pins to `HardwareConfig.h`**

In `/Users/bbalazs/daisy/daisy-nam-pedal/HardwareConfig.h`, after the encoder
pin constants and before the display section, add:

```cpp
// ---------------------------------------------------------------------------
// TRS MIDI input (UART4, 31250 baud)
// MIDI RX must be driven by a proper MIDI input receiver/opto-isolator.
// D12 is reserved for future MIDI Out/Thru even though phase one is MIDI In.
// ---------------------------------------------------------------------------
constexpr Pin PIN_MIDI_RX = seed::D11; // PB8, UART4 RX
constexpr Pin PIN_MIDI_TX = seed::D12; // PB9, UART4 TX
```

- [ ] **Step 2: Update `docs/HARDWARE.md`**

Add this section after the footswitch section:

```markdown
### TRS MIDI input

Phase one is MIDI In only. Use a TRS MIDI Type A jack and a proper MIDI input
receiver/opto-isolator before the Daisy UART pin. Do not connect the TRS jack
directly to D11.

| Function | Daisy pin | STM32 pin | Notes |
|----------|-----------|-----------|-------|
| MIDI RX  | D11       | PB8       | UART4 RX, 31250 baud |
| Reserved MIDI TX | D12 | PB9 | UART4 TX, reserved for future Out/Thru |
```

Add a constraint bullet:

```markdown
- **MIDI input electrical interface:** D11 expects a 3.3 V UART-level signal from
  a MIDI receiver/opto-isolator, not the raw TRS jack.
```

---

### Task 4: Add Daisy MIDI UART Wrapper

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiHardware.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/MidiHardware.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`

- [ ] **Step 1: Create `MidiHardware.h`**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/MidiHardware.h`:

```cpp
#pragma once
#include "MidiPresetControl.h"

#ifndef HOST_BUILD
#include "daisy_seed.h"
#include "hid/midi.h"

class MidiHardware
{
public:
    void Init();
    bool PollCommand(MidiPresetCommand& out);

private:
    static MidiPresetMessage Convert(const daisy::MidiEvent& event);

    daisy::MidiUartHandler midi_;
    MidiPresetConfig config_{};
};
#else
class MidiHardware
{
public:
    void Init() {}
    bool PollCommand(MidiPresetCommand&) { return false; }
};
#endif
```

- [ ] **Step 2: Create `MidiHardware.cpp`**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/MidiHardware.cpp`:

```cpp
#include "MidiHardware.h"

#ifndef HOST_BUILD
#include "HardwareConfig.h"

void MidiHardware::Init()
{
    daisy::MidiUartHandler::Config midi_config;
    midi_config.transport_config.periph =
        daisy::UartHandler::Config::Peripheral::UART_4;
    midi_config.transport_config.rx = hw::PIN_MIDI_RX;
    midi_config.transport_config.tx = hw::PIN_MIDI_TX;

    config_.omni = true;
    config_.enable_cc_next_prev = false;

    midi_.Init(midi_config);
    midi_.StartReceive();
}

bool MidiHardware::PollCommand(MidiPresetCommand& out)
{
    midi_.Listen();

    while (midi_.HasEvents())
    {
        daisy::MidiEvent event = midi_.PopEvent();
        MidiPresetCommand cmd = MapMidiPresetMessage(Convert(event), config_);
        if (cmd.type != MidiPresetCommandType::None)
        {
            out = cmd;
            return true;
        }
    }

    return false;
}

MidiPresetMessage MidiHardware::Convert(const daisy::MidiEvent& event)
{
    MidiPresetMessage msg{};
    msg.channel = static_cast<uint8_t>(event.channel);

    switch (event.type)
    {
    case daisy::ProgramChange:
        msg.type = MidiPresetMessageType::ProgramChange;
        msg.data1 = event.data[0];
        break;
    case daisy::ControlChange:
        msg.type = MidiPresetMessageType::ControlChange;
        msg.data1 = event.data[0];
        msg.data2 = event.data[1];
        break;
    case daisy::NoteOn:
        msg.type = MidiPresetMessageType::NoteOn;
        msg.data1 = event.data[0];
        msg.data2 = event.data[1];
        break;
    default:
        msg.type = MidiPresetMessageType::Other;
        msg.data1 = event.data[0];
        msg.data2 = event.data[1];
        break;
    }

    return msg;
}
#endif
```

- [ ] **Step 3: Add sources to firmware build**

In `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`, add:

```make
MidiPresetControl.cpp \
MidiHardware.cpp \
```

- [ ] **Step 4: Build after wrapper**

Run:

```sh
make
```

Expected: firmware builds. If libDaisy reports UART pin initialization failure on
hardware, re-check that D11/D12 are not already assigned in local board wiring.

---

### Task 5: Share Preset Apply Logic In `main.cpp`

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`

- [ ] **Step 1: Include MIDI hardware**

Add near the existing includes:

```cpp
#include "MidiHardware.h"
```

Add a global instance near the other modules:

```cpp
static MidiHardware midi_control;
```

- [ ] **Step 2: Add `SetPresetIndex` helper**

Add near `SaveActivePreset()`:

```cpp
static bool SetPresetIndex(uint8_t target_idx)
{
    if (presets.Count() == 0 || target_idx >= presets.Count())
        return false;

    while (presets.Current() != target_idx)
    {
        if (target_idx > presets.Current())
            presets.Next();
        else
            presets.Prev();
    }

    return true;
}
```

- [ ] **Step 3: Add `ApplyPresetIndex` helper**

Add below `SetPresetIndex()`:

```cpp
static bool ApplyPresetIndex(uint8_t target_idx, const char* source_label)
{
    if (!SetPresetIndex(target_idx))
        return false;

    presets.Apply(audio_engine, storage, models,
                  hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
    preset_dirty = false;
    PushPerformanceScreen();
    daisy_seed.PrintLine("%s preset -> %u: %s",
                         source_label ? source_label : "External",
                         (unsigned)presets.Current(),
                         presets.Name(presets.Current()));
    return true;
}
```

- [ ] **Step 4: Replace footswitch next/previous duplicate code**

Replace the FS1 tap branch:

```cpp
if (ev.fs1_tap && presets.Count() > 1)
{
    presets.Next();
    presets.Apply(audio_engine, storage, models,
                  hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
    preset_dirty = false;
    PushPerformanceScreen();
    daisy_seed.PrintLine("Preset -> %u: %s",
                 (unsigned)presets.Current(), presets.Name(presets.Current()));
}
```

with:

```cpp
if (ev.fs1_tap && presets.Count() > 1)
{
    uint8_t target = static_cast<uint8_t>((presets.Current() + 1u) % presets.Count());
    ApplyPresetIndex(target, "FS1");
}
```

Replace the FS2 tap branch:

```cpp
else if (ev.fs2_tap && presets.Count() > 1)
{
    presets.Prev();
    presets.Apply(audio_engine, storage, models,
                  hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
    preset_dirty = false;
    PushPerformanceScreen();
    daisy_seed.PrintLine("Preset -> %u: %s",
                 (unsigned)presets.Current(), presets.Name(presets.Current()));
}
```

with:

```cpp
else if (ev.fs2_tap && presets.Count() > 1)
{
    uint8_t target = static_cast<uint8_t>(
        (presets.Current() + presets.Count() - 1u) % presets.Count());
    ApplyPresetIndex(target, "FS2");
}
```

- [ ] **Step 5: Replace browse apply duplicate code**

In the browse `ev.enc1_click` branch, replace the `while (...)`, `presets.Apply`,
`preset_dirty = false`, and print block with:

```cpp
ApplyPresetIndex(browse_cursor, "Browse");
```

Keep `browsing = false;` and `PushPerformanceScreen();` after it.

- [ ] **Step 6: Build after refactor**

Run:

```sh
make
```

Expected: firmware builds and preset navigation behavior is unchanged.

---

### Task 6: Add MIDI Tuner Toggle Integration

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`

- [ ] **Step 1: Confirm tuner helpers exist**

This task assumes the tuner mode feature has already introduced helpers equivalent
to:

```cpp
static void EnterTunerMode();
static void ExitTunerMode();
static bool tuner_active = false;
```

If the tuner implementation used different names, use those existing names
instead of creating a second tuner state path.

- [ ] **Step 2: Add tuner toggle helper**

Near the tuner helpers in `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`, add:

```cpp
static bool ToggleTunerModeFromMidi()
{
    if (editing)
        return false;

    if (tuner_active)
        ExitTunerMode();
    else
    {
        browsing = false;
        EnterTunerMode();
    }

    daisy_seed.PrintLine("MIDI tuner %s", tuner_active ? "on" : "off");
    return true;
}
```

If USB CDC library sync has already been implemented, add its active/update
predicate before changing tuner state:

```cpp
if (library_sync_active)
    return false;
```

- [ ] **Step 3: Build after tuner helper**

Run:

```sh
make
```

Expected: firmware builds. If tuner mode has not been implemented yet, defer this
task until the tuner plan is complete.

---

### Task 7: Poll MIDI And Apply Commands

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`

- [ ] **Step 1: Initialize MIDI after controls**

After controls initialization in `main()`:

```cpp
daisy_seed.PrintLine("Init MIDI...");
midi_control.Init();
daisy_seed.PrintLine("MIDI ready: TRS In on UART4 RX D11.");
```

- [ ] **Step 2: Add an external-change guard**

Add near the preset helpers:

```cpp
static bool CanApplyExternalPresetChange()
{
    if (editing)
        return false;

    if (tuner_active)
        return false;

    // Future library sync/update active predicate should be added here.
    return true;
}
```

If USB CDC library sync has already been implemented, update the helper with the
sync server's active/update predicate.

- [ ] **Step 3: Add MIDI command processing in the main loop**

Immediately after `ControlEvent ev = controls.Process();`, add:

```cpp
bool midi_applied_preset = false;
bool midi_handled_mode = false;
MidiPresetCommand midi_cmd{};
if (midi_control.PollCommand(midi_cmd))
{
    if (midi_cmd.type == MidiPresetCommandType::ToggleTuner)
    {
        midi_handled_mode = ToggleTunerModeFromMidi();
    }
    else if (CanApplyExternalPresetChange() && midi_cmd.type == MidiPresetCommandType::SelectPreset)
    {
        midi_applied_preset = ApplyPresetIndex(midi_cmd.preset_index, "MIDI");
    }
    else if (CanApplyExternalPresetChange() && midi_cmd.type == MidiPresetCommandType::NextPreset && presets.Count() > 1)
    {
        uint8_t target = static_cast<uint8_t>((presets.Current() + 1u) % presets.Count());
        midi_applied_preset = ApplyPresetIndex(target, "MIDI CC");
    }
    else if (CanApplyExternalPresetChange() && midi_cmd.type == MidiPresetCommandType::PreviousPreset && presets.Count() > 1)
    {
        uint8_t target = static_cast<uint8_t>(
            (presets.Current() + presets.Count() - 1u) % presets.Count());
        midi_applied_preset = ApplyPresetIndex(target, "MIDI CC");
    }

    if (midi_applied_preset)
    {
        browsing = false;
    }
}
```

This keeps MIDI handling in the main loop and avoids applying presets or changing
tuner state from UART callbacks.

- [ ] **Step 4: Skip local mode handling after MIDI changes mode**

Wrap the existing mode handling so a successful MIDI preset change or MIDI tuner
toggle does not fall through into the performance/browse/edit branches in the
same loop iteration.
Change:

```cpp
if (!browsing && !editing)
{
    // performance mode
}
else if (browsing)
{
    // browse mode
}
else
{
    // edit mode
}
```

to:

```cpp
if (midi_applied_preset)
{
    browsing = false;
    PushPerformanceScreen();
}
else if (midi_handled_mode)
{
    // Tuner helpers already changed the screen.
}
else if (!browsing && !editing)
{
    // performance mode
}
else if (browsing)
{
    // browse mode
}
else
{
    // edit mode
}
```

Do not call `PushPerformanceScreen()` after a tuner toggle. `EnterTunerMode()`
and `ExitTunerMode()` own the tuner/performance screen transition.

Do not clear `editing` here. `CanApplyExternalPresetChange()` and
`ToggleTunerModeFromMidi()` already block MIDI while editing so an in-progress
preset edit cannot be discarded by a controller.

- [ ] **Step 5: Build firmware**

Run:

```sh
make
```

Expected: firmware builds.

---

### Task 8: Full Verification

**Files:**
- Modify as needed based on failures only.

- [ ] **Step 1: Run host tests**

Run:

```sh
make -C tests
```

Expected: all host tests pass, including `test_midi_preset_control`.

- [ ] **Step 2: Build firmware**

Run:

```sh
make
```

Expected: firmware builds with MIDI sources included.

- [ ] **Step 3: Hardware electrical smoke test**

Before connecting a controller:

- verify the TRS MIDI receiver output idles high at 3.3 V logic level on D11;
- verify the receiver output changes when MIDI data is sent;
- verify D11 is not directly connected to the raw TRS jack.

- [ ] **Step 4: MIDI controller test**

With a known TRS MIDI Type A controller:

- send Program Change 0 and confirm preset 1 on the UI;
- send Program Change 1 and confirm preset 2 on the UI;
- send the highest loaded preset's Program Change number and confirm it loads;
- send an out-of-range Program Change and confirm the preset does not change;
- send non-Program-Change messages and confirm the preset does not change.
- send CC 82 value 127 and confirm tuner mode opens;
- send CC 82 value 127 again and confirm tuner mode exits;
- send CC 82 value 0 and confirm tuner mode does not toggle.

- [ ] **Step 5: Regression test local controls**

Confirm:

- FS1 tap still advances presets;
- FS2 tap still goes to the previous preset;
- browse selection still applies presets;
- FS1 hold still saves;
- FS2 hold still reverts;
- MIDI Program Change is ignored during edit mode.
- MIDI CC 82 is ignored during edit mode.
- MIDI Program Change is ignored while tuner mode is active.
- MIDI CC 82 exits tuner mode while tuner mode is active.

- [ ] **Step 6: Optional CC enablement**

If fixed CC next/previous is desired immediately, set:

```cpp
config_.enable_cc_next_prev = true;
```

in `MidiHardware::Init()`, rebuild, and test:

- CC 80 value 127 advances one preset;
- CC 81 value 127 goes back one preset;
- CC 80 or 81 values below 64 do nothing.

Keep this disabled if Program Change covers the user's controller workflow.
