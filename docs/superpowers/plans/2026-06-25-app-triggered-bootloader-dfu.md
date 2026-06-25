# App-Triggered Bootloader DFU Implementation Plan

> **Pivot from 2026-06-24 USB CDC Library Sync:** Live QSPI programming from the
> running BOOT_QSPI app failed the hardware feasibility gate. This plan keeps the
> safe, existing DFU write path and removes the manual BOOT/RESET step in the
> common case by asking the running firmware to reboot into DaisyBoot.

## Goal

Let the desktop app update the pedal library without live QSPI writes from the
running firmware. The app builds the same data image as today, sends a guarded
USB CDC command to the running pedal, waits for DaisyBoot DFU, then flashes the
image to `0x90200000` with the existing `dfu-util` backend. Manual BOOT/RESET
remains the fallback.

## Architecture

- Firmware: add a tiny byte-stream parser for a single guarded command,
  `NAM_DFU_BOOT 7E57A11E`. On match, acknowledge over CDC, stop audio, and call
  `daisy::System::ResetToBootloader(daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT)`.
- Firmware USB: keep `DaisySeed::StartLog(false)` for logs and register a
  `daisy::UsbHandle::SetReceiveCallback()` on `FS_INTERNAL` after logging starts.
  libDaisy's USB handle is static-backed, so this should attach RX to the same
  CDC device.
- Desktop app: add a Tauri command that scans serial ports, writes the guarded
  command, waits for the ACK or CDC disconnect, then waits for DFU via the
  existing `detect_device`/`dfu-util -l` logic.
- UI: make the main flow "Build image -> Send pedal to update mode -> Flash",
  with the current manual DFU instructions retained as recovery.

## Constraints And Guardrails

- Do not write QSPI from the running firmware.
- Do not carry the failed RAM-resident QSPI programmer into the final branch.
- Use DaisyBoot DFU, not STM system bootloader, because DaisyBoot maps the
  external QSPI range used by the data partition.
- Keep the serial command exact and magic-token guarded so random terminal input
  cannot trigger bootloader mode.
- Keep desktop flash output and errors grounded in the existing `dfu-util` path.

## Task 1: Remove Failed RAM/QSPI Probe Path

**Files:**
- `main.cpp`
- `Makefile`
- `RamFunc.cpp`
- `RamFunc.h`
- `QspiDataProgrammer.cpp`
- `QspiDataProgrammer.h`
- `third_party/libDaisy/core/STM32H750IB_qspi.lds`

**Steps:**

1. Remove `NAM_ENABLE_RAMFUNC_BOOT_PROBE`, `CopyRamFuncs()`, and
   `QspiProgrammerRamProbe*` usage from `main.cpp`.
2. Remove `RamFunc.cpp` and `QspiDataProgrammer.cpp` from `CPP_SOURCES`.
3. Remove the special `RAMFUNC_CPPFLAGS`, `RamFunc.o`, and
   `QspiDataProgrammer.o` Makefile rules.
4. Delete `RamFunc.*` and `QspiDataProgrammer.*`.
5. Revert the `.ramfunc` linker-script additions that were only needed for the
   failed probe.
6. Build once with `make clean && make -j8`.

**Acceptance:**

- Firmware builds without `RamFunc` or `QspiDataProgrammer` symbols.
- Boot log no longer prints `ramfunc probe skipped at boot`.

## Task 2: Add Host-Tested Firmware Command Parser

**Files:**
- `BootloaderCommand.h`
- `BootloaderCommand.cpp`
- `tests/test_bootloader_command.cpp`
- `tests/Makefile`

**Red Test:**

Add tests that feed the parser fragmented byte streams:

```cpp
BootloaderCommandParser parser;
CHECK(parser.Feed("NAM_DFU_") == BootloaderCommand::None);
CHECK(parser.Feed("BOOT 7E57A11E\n") == BootloaderCommand::EnterBootloader);
```

Also test noise rejection:

```cpp
BootloaderCommandParser parser;
CHECK(parser.Feed("hello\nNAM_DFU_BOOT nope\n") == BootloaderCommand::None);
CHECK(parser.Feed("NAM_DFU_BOOT 7E57A11E\n") == BootloaderCommand::EnterBootloader);
```

**Implementation:**

- Implement a fixed-size line buffer, max 64 bytes.
- Reset on `\n`, `\r`, or overflow.
- Match exactly `NAM_DFU_BOOT 7E57A11E`.

**Acceptance:**

- `cd tests && make test_bootloader_command && ./test_bootloader_command` passes.
- Existing `cd tests && make run` passes.

## Task 3: Wire Firmware CDC RX To Bootloader Request

**Files:**
- `main.cpp`
- `BootloaderCommand.h`
- `BootloaderCommand.cpp`
- `Makefile`

**Steps:**

1. Add `BootloaderCommand.cpp` to `CPP_SOURCES`.
2. Include `hid/usb.h` and `sys/system.h` where needed.
3. Create a static parser and volatile flag:

```cpp
static BootloaderCommandParser bootloader_command_parser;
static volatile bool bootloader_requested = false;
```

4. Register a CDC receive callback after `StartLog(false)`:

```cpp
static void UsbReceiveCallback(uint8_t* buffer, uint32_t* length)
{
    if (buffer == nullptr || length == nullptr)
        return;
    if (bootloader_command_parser.Feed(buffer, *length) == BootloaderCommand::EnterBootloader)
        bootloader_requested = true;
}
```

5. In the main loop, before normal UI/control work, handle the request:

```cpp
if (bootloader_requested)
{
    daisy_seed.PrintLine("Entering DaisyBoot DFU update mode...");
    daisy_seed.StopAudio();
    System::Delay(50);
    System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
}
```

6. Build with `make -j8`.

**Acceptance:**

- Firmware builds.
- Serial logs still work.
- Sending the magic line over the running CDC port reboots into DaisyBoot DFU.

## Task 4: Add Desktop Serial Bootloader Command

**Files:**
- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/Cargo.toml`
- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/flash.rs`
- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/lib.rs`

**Steps:**

1. Add Rust dependency:

```toml
serialport = "4"
```

2. Add Tauri command:

```rust
#[tauri::command]
pub async fn request_bootloader(app: AppHandle) -> Result<(), String>
```

3. In the command:
   - If DFU already exists, return `Ok(())`.
   - Enumerate serial ports.
   - Prefer ports whose name or USB metadata resembles Daisy/STM/usbmodem.
   - Open at `115200`, timeout around 250 ms.
   - Write `NAM_DFU_BOOT 7E57A11E\n`.
   - Try to read an ACK briefly, but do not require it if the port disconnects.
   - Poll `detect_device` for up to roughly 10 seconds.
   - Return a clear fallback error if DFU does not appear.

4. Register `flash::request_bootloader` in `generate_handler!`.

**Tests:**

- Unit-test pure helpers for serial-port candidate ranking.
- Unit-test DFU polling timeout math if factored.

**Acceptance:**

- `cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri && cargo test` passes.
- Existing `flash_image` path remains unchanged except for shared helper reuse.

## Task 5: Update Flash Page Flow

**Files:**
- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/api.ts`
- `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/FlashPage.tsx`

**Steps:**

1. Add `api.requestBootloader()`.
2. Add UI state for bootloader request in progress.
3. Change primary flow:
   - Build image.
   - Click "Send to pedal".
   - App calls `requestBootloader`.
   - App detects DFU and enables Flash.
4. Keep the manual BOOT/RESET copy visible only when software entry fails or no
   running serial device is found.
5. Keep current progress handling for `flash-progress`.

**Acceptance:**

- `npm run build` passes.
- UI still supports manual DFU fallback.

## Task 6: Hardware Verification

**Steps:**

1. Flash the firmware once by the current manual DFU path.
2. Boot normally and confirm:
   - Display initializes.
   - Serial log reaches `Audio started: full NAM DSP.`
3. From the desktop app, build an image.
4. Click the new software update-mode action.
5. Confirm the running CDC port disconnects and DFU appears.
6. Flash the image through the existing DFU command.
7. Confirm the pedal leaves DFU and boots the app.

**Acceptance:**

- No manual BOOT/RESET needed in the common path.
- Manual BOOT/RESET remains usable if software entry fails.
- Data image flashes to `0x90200000` and presets/models/IRs load after reboot.

## Task 7: Commit And Push

**Steps:**

1. Review `git diff` in both repos.
2. Run final verification:
   - Pedal firmware: `make clean && make -j8`
   - Pedal host tests: `cd tests && make run`
   - Desktop Rust tests: `cd src-tauri && cargo test`
   - Desktop frontend build: `npm run build`
3. Commit firmware branch.
4. Push `usb-cdc-library-sync`.
5. Commit desktop app branch.
6. Push desktop app branch if credentials allow.

**Acceptance:**

- Both repos have clean, focused commits.
- Remote push succeeds where credentials allow.
