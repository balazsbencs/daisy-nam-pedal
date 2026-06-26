# USB CDC Library Sync Design Spec

**Date:** 2026-06-24

## Goal

Let the desktop app update the pedal's model, IR, and preset library over the
normal USB CDC connection, without asking the user to manually enter DFU mode.

The user experience should be:

```text
plug in pedal -> app detects running firmware -> click Sync -> progress -> pedal reboots with new library
```

## Current Findings

- Firmware is built with `APP_TYPE = BOOT_QSPI`.
- Application code executes from external QSPI at `0x90040000`.
- Models, IRs, and presets live in the same QSPI chip in the data partition at
  `0x90200000`.
- The desktop app already builds a valid data-partition image in
  `src-tauri/src/image_builder.rs`.
- Current desktop transfer uses `dfu-util` to write that image at `0x90200000`.
- libDaisy USB CDC is available through `daisy::UsbHandle`.
- `DaisySeed::StartLog(false)` already initializes the internal USB CDC device
  for serial logging.
- libDaisy `QSPIHandle` refuses erase/write when program memory is QSPI. That is
  intentional: code cannot safely fetch from QSPI while the QSPI peripheral is
  being used for erase/program commands.

## Design Summary

Keep the current packed QSPI data image as the source of truth. The CDC feature is
a new transport and update path, not a new library storage format.

Use a maintenance-mode update flow:

1. Desktop app builds the same data image it would have flashed with DFU.
2. App discovers the running pedal over USB CDC by handshake.
3. Firmware receives the image over CDC into SDRAM staging memory.
4. Firmware validates size, magic/version, directory bounds, and CRC32.
5. Firmware stops audio, unloads active model/IR state, and enters update mode.
6. Firmware erases/programs the QSPI data partition from the staged SDRAM image.
7. Firmware verifies flash bytes against the staged image.
8. Firmware sends success/failure over CDC.
9. On success, firmware reboots so managers reload the updated library.

This requires one feasibility gate before the full feature: prove that the project
can run a small QSPI data-partition erase/program routine from internal RAM while
the main application is otherwise BOOT_QSPI. If that cannot be made reliable, the
fallback should be a one-click app-driven reboot-to-bootloader DFU flow, not a
manual DFU workflow.

## User Experience

The desktop Flash page should become a Sync page:

- Primary action: `Sync to Pedal`
- Device status:
  - `Pedal connected` for running firmware over CDC.
  - `DFU device found` only for fallback/recovery.
  - `No pedal found`.
- Build summary stays: models, IRs, presets, total bytes, free bytes.
- Sync progress has stages:
  - building image
  - connecting
  - transferring
  - validating
  - programming
  - verifying
  - rebooting
- Advanced fallback: keep the existing DFU flash path for recovery and early
  hardware bring-up.

During sync, the pedal may stop audio and show a maintenance/update screen. That
is acceptable for this feature.

## Protocol

Use a small binary frame protocol over USB CDC. CDC is a byte stream, so the
firmware parser must scan for frame magic and tolerate unrelated serial log text.

Frame header, little-endian:

```c
struct CdcFrameHeader {
    char     magic[4];      // "NCD1"
    uint8_t  type;
    uint8_t  flags;
    uint16_t seq;
    uint32_t length;
    uint32_t payload_crc32;
};
```

Payload length is capped at 1024 bytes for data frames. This keeps firmware RX
buffering simple and avoids depending on CDC packet boundaries.

Frame types:

- `HELLO_REQ`
- `HELLO_RESP`
- `BEGIN_UPDATE`
- `BEGIN_ACK`
- `DATA_CHUNK`
- `DATA_ACK`
- `COMMIT_UPDATE`
- `PROGRESS`
- `DONE`
- `ERROR`
- `REBOOT`

Handshake response includes:

- protocol version
- firmware data format version
- data partition size
- maximum chunk size
- whether an update is currently in progress
- current image header status if readable

`BEGIN_UPDATE` includes:

- image size
- whole-image CRC32
- expected data format version
- entry count from the app-built image

`DATA_CHUNK` includes:

- absolute image offset
- chunk bytes

The firmware acknowledges every chunk by sequence and accepted byte count. The app
does not send the next chunk until it receives the ACK. This is slower than a
streaming firehose, but robust and simple. A 6 MiB image over USB CDC is still a
reasonable one-click operation.

## Firmware Architecture

New firmware units:

- `UsbCdcLink`
  - Owns `UsbHandle` callback setup and TX helpers.
  - Does not allocate in callbacks.
  - Pushes received bytes into a fixed ring buffer.
- `CdcFrameCodec`
  - Pure parser/encoder for `NCD1` frames.
  - Host-testable without Daisy headers.
- `LibrarySyncServer`
  - State machine for hello, receive, validate, program, verify, reboot.
  - Runs from the main loop, not from the USB receive callback.
- `LibraryImageValidator`
  - Validates `NamDataHeader`, `NamDataEntry` bounds, alignment, and payload CRC.
  - Uses `data_format.h` constants, including the preset size from feature one.
- `QspiDataProgrammer`
  - Programs the data partition from staged SDRAM.
  - Has a host fake-flash implementation for tests.
  - Has a hardware implementation whose critical erase/program routines run from
    internal RAM, not QSPI.

SDRAM staging:

```cpp
static uint8_t library_image_staging[NAM_DATA_PARTITION_SIZE]
    __attribute__((section(".sdram_bss")));
```

This uses the existing linker SDRAM region and avoids touching QSPI until the
complete image is received and validated.

## QSPI Update Constraint

The hardest part is not USB CDC. It is writing to the external QSPI chip while the
application is executing from that same chip.

The plan must therefore start with a proof:

- Add a linker section for RAM-resident updater code, for example `.ramfunc`.
- Copy `.ramfunc` from QSPI to SRAM during startup.
- Mark a minimal QSPI write/erase probe function as `.ramfunc`.
- Confirm the probe symbol address is in SRAM/ITCM, not `0x900xxxxx`.
- Confirm a hardware test can erase/program/verify one unused data-partition
  sector while the app is otherwise BOOT_QSPI.

Only after that proof should the full sync feature be built. If the proof fails,
do not keep adding protocol/UI code around an unsafe flash path.

## Safety And Recovery

The update is not power-fail atomic. The app must warn the user not to unplug the
pedal during `programming` or `verifying`.

Recovery expectations:

- The app region is not modified by library sync.
- If the data partition is corrupt after a failed sync, firmware should still boot.
- Firmware should keep CDC sync available even when `QspiStorage::Init()` reports
  `BAD_MAGIC`.
- The desktop app can retry CDC sync.
- Existing DFU data flashing remains as an advanced recovery path.

To reduce false-valid images:

- Validate staged image before erasing flash.
- Erase/program the data partition from SDRAM.
- Write the first sector containing `NamDataHeader` last.
- Verify QSPI bytes against SDRAM before reboot.

Writing the header last does not preserve the old library after power loss, but it
prevents firmware from accepting a partially written new directory as valid.

## Desktop App Architecture

Add a CDC sync backend alongside the current DFU backend.

Rust backend:

- Add a serial-port dependency such as `serialport`.
- Add `src-tauri/src/cdc_protocol.rs` for frame encoding/decoding and test
  vectors shared conceptually with firmware tests.
- Add `src-tauri/src/commands/sync.rs` for device discovery and sync.
- Reuse `image_builder::build()` and `build_image()` output.
- Keep `flash.rs` for DFU fallback.

Tauri commands:

- `detect_sync_device() -> SyncDeviceStatus`
- `sync_image(image_path: String) -> Result<(), String>`
- optionally `sync_current_library()` to build and sync in one command

Frontend:

- Update `src/pages/FlashPage.tsx` into a Sync-first UI.
- Keep the existing table and storage bar.
- Replace BOOT/RESET instructions with normal plug-in instructions.
- Keep DFU instructions in an advanced recovery section.

## Relation To Pre/Post Effects

This feature sits on top of the preset-format work from the pre/post effects
feature. It must not hard-code a 98-byte preset. All validation and app image
building must follow the final `data_format.h`/Rust image-builder layout at the
time of implementation.

The CDC sync protocol transports a whole data image. It does not need to know
whether preset entries are 98 bytes, 138 bytes, or a later compatible size except
through image validation.

## Test Plan

Firmware host tests:

- Frame codec parses fragmented frames and ignores serial-log noise before magic.
- Frame codec rejects bad CRC, oversized payloads, and unsupported versions.
- Library image validator accepts a valid image and rejects:
  - bad magic
  - unsupported version
  - directory entry beyond image size
  - unaligned blob offset
  - image larger than partition
- Sync state machine handles hello, begin, chunks, commit, verify, done, and error.
- Fake flash programmer writes header sector last.
- Failed validation never erases fake flash.

Desktop Rust tests:

- Frame codec test vectors match firmware.
- Device discovery ignores non-pedal serial ports that do not answer `HELLO_REQ`.
- Sync command sends chunks in order and waits for ACK.
- Bad ACK, timeout, CRC error, and device `ERROR` become clear app errors.

Frontend checks:

- `npm run build` passes.
- UI exposes CDC sync as the primary action and keeps DFU as fallback.

Hardware tests:

- CDC handshake works with the running pedal.
- 64 KiB test image transfers, programs, verifies, and reboots.
- Full library image transfers, programs, verifies, and reboots.
- Corrupt transfer is rejected before erase.
- Power-cycle during a failed update still boots firmware and allows retry.

## Success Criteria

- User can update models, IRs, and presets from the app without manually entering
  DFU mode.
- Existing DFU flashing still works as fallback.
- Firmware remains bootable with a missing or corrupt data partition.
- App and firmware use the same data image format.
- Hardware proof confirms QSPI data partition programming is safe under BOOT_QSPI
  before the full CDC sync flow is considered complete.
