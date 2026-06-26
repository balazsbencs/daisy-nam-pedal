# USB CDC Library Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Decision update, 2026-06-25:** This plan failed the hardware feasibility gate. The pedal hard-faulted or failed to boot when executing the RAM-resident probe from AXI SRAM, from AXI SRAM with cache maintenance, and from ITCM. Do not continue this live CDC/QSPI programming plan. Replace it with an app-triggered bootloader/DFU flow that keeps the existing app-built data image and automates entering bootloader mode where possible.

## Superseded Direction: App-Triggered Bootloader/DFU

**New goal:** Let the desktop app initiate a library update without asking the user to manually hold BOOT/RESET in the common case.

**Architecture:** Firmware exposes a tiny safe command over the existing USB serial/log channel: validate a magic reboot request, persist a bootloader-intent marker if needed, stop audio, disconnect USB cleanly, and jump/reset into the STM32 system bootloader or Daisy bootloader path. The desktop app then runs the existing DFU/data-image flashing flow. Manual BOOT/RESET remains the recovery fallback.

**Why:** The original plan required programming the QSPI data partition while executing the app from QSPI. The hardware probe showed that even a 12-byte RAM-resident function call is not reliable enough in this boot context. The bootloader path is less elegant, but it is operationally safer and fits the current flashing model.

**Immediate next steps:**
- Remove RAM-resident updater work from the implementation path.
- Keep the app-built data image as the single source of truth.
- Add a firmware `enter_bootloader` command only; do not write QSPI from the running app.
- Add desktop app flow: request bootloader, wait for DFU device, flash firmware/data using the existing DFU backend, then reconnect.
- Keep manual BOOT/RESET instructions as the fallback when software bootloader entry fails.

**Goal:** Let the desktop app sync models, IRs, and presets to the running pedal over USB CDC, without requiring the user to manually enter DFU mode.

**Architecture:** Reuse the existing QSPI data image as the storage format and add a second transfer path beside DFU. Firmware receives the app-built image over USB CDC into SDRAM, validates it, enters maintenance mode, writes the data partition from a RAM-resident QSPI updater, verifies flash, then reboots.

**Tech Stack:** C++17 firmware with libDaisy USB CDC and QSPI, STM32H750 SDRAM/QSPI linker sections, host tests with `make`, Rust/Tauri backend with `serialport`, React/TypeScript frontend with Vite.

---

## Scope And Ordering

- Do not use worktrees.
- Implement this after, or rebased on top of, the pre/post effects preset-format feature.
- Do not hard-code preset blob size in CDC sync code. Use the data image produced by the current app image builder.
- Keep DFU flashing as an advanced fallback.
- The first milestone is a hardware feasibility proof for BOOT_QSPI-safe data-partition programming. If that proof fails, stop and switch to a one-click bootloader/DFU fallback plan.

## File Structure

Firmware repo:

- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/third_party/libDaisy/core/STM32H750IB_qspi.lds`
  - Add `.ramfunc` linker section for updater code copied to SRAM.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/RamFunc.h`
  - Defines `NAM_RAMFUNC` and linker symbols.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/RamFunc.cpp`
  - Copies `.ramfunc` from QSPI load address to SRAM at boot.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/CdcFrame.h`
  - Shared firmware frame constants and header struct.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/CdcFrame.cpp`
  - Host-testable CDC frame parser and encoder.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibraryImageValidator.h`
  - Validates `NamDataHeader` and `NamDataEntry` bounds.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibraryImageValidator.cpp`
  - Implements image validation.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.h`
  - Interface for erasing/programming/verifying the data partition.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.cpp`
  - Host fake implementation plus hardware entry points.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/UsbCdcLink.h`
  - Owns `UsbHandle`, RX ring, and TX helpers.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/UsbCdcLink.cpp`
  - Implements USB receive callback and non-callback polling.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibrarySyncServer.h`
  - Firmware sync state machine API.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibrarySyncServer.cpp`
  - Implements hello, receive, validate, program, verify, and reboot flow.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`
  - Initializes RAM functions, starts CDC sync, polls sync server, and enters maintenance mode.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`
  - Adds new firmware sources.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_cdc_frame.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_library_image_validator.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_library_sync_server.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

Desktop app repo:

- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/Cargo.toml`
  - Adds serial-port dependency.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/cdc_protocol.rs`
  - Rust frame codec.
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/sync.rs`
  - CDC discovery and sync commands.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/mod.rs`
  - Exposes `sync`.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/lib.rs`
  - Registers sync commands.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`
  - Adds sync status types.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/api.ts`
  - Adds sync commands.
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/FlashPage.tsx`
  - Makes CDC sync primary and DFU fallback secondary.

---

### Task 1: RAM-Resident QSPI Feasibility Gate

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/third_party/libDaisy/core/STM32H750IB_qspi.lds`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/RamFunc.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/RamFunc.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`

- [ ] **Step 1: Add `.ramfunc` linker section**

In `/Users/bbalazs/daisy/daisy-nam-pedal/third_party/libDaisy/core/STM32H750IB_qspi.lds`,
add this section after `.data` and before `.bss`:

```ld
	.ramfunc :
	{
		. = ALIGN(4);
		_sramfunc = .;
		PROVIDE(__ramfunc_start__ = _sramfunc);
		*(.ramfunc)
		*(.ramfunc*)
		. = ALIGN(4);
		_eramfunc = .;
		PROVIDE(__ramfunc_end__ = _eramfunc);
	} > SRAM AT > QSPIFLASH

	_siramfunc = LOADADDR(.ramfunc);
```

Expected: linker now has VMA symbols in SRAM and a load address in QSPI.

- [ ] **Step 2: Create RAM function copy helper**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/RamFunc.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#define NAM_RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))

extern "C" {
extern uint8_t _sramfunc;
extern uint8_t _eramfunc;
extern uint8_t _siramfunc;
}

void CopyRamFuncs();
bool RamFuncAddressLooksValid(const void* fn);
```

Create `/Users/bbalazs/daisy/daisy-nam-pedal/RamFunc.cpp`:

```cpp
#include "RamFunc.h"
#include <cstring>

void CopyRamFuncs()
{
    const size_t n = static_cast<size_t>(&_eramfunc - &_sramfunc);
    if (n > 0)
        std::memcpy(&_sramfunc, &_siramfunc, n);
}

bool RamFuncAddressLooksValid(const void* fn)
{
    uintptr_t p = reinterpret_cast<uintptr_t>(fn);
    return (p >= 0x24000000u && p < 0x24080000u) ||
           (p >= 0x20000000u && p < 0x20020000u) ||
           (p < 0x00010000u);
}
```

- [ ] **Step 3: Add a minimal RAM function probe**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.h`:

```cpp
#pragma once
#include <stdint.h>

bool QspiProgrammerRamProbeAddressOk();
uint32_t QspiProgrammerRamProbeAdd(uint32_t a, uint32_t b);
```

Create `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.cpp`:

```cpp
#include "QspiDataProgrammer.h"
#include "RamFunc.h"

extern "C" NAM_RAMFUNC uint32_t QspiProgrammerRamProbeAddImpl(uint32_t a, uint32_t b)
{
    return a + b + 0x12340000u;
}

bool QspiProgrammerRamProbeAddressOk()
{
    return RamFuncAddressLooksValid(reinterpret_cast<const void*>(&QspiProgrammerRamProbeAddImpl));
}

uint32_t QspiProgrammerRamProbeAdd(uint32_t a, uint32_t b)
{
    return QspiProgrammerRamProbeAddImpl(a, b);
}
```

- [ ] **Step 4: Initialize RAM functions at boot**

In `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`, include:

```cpp
#include "RamFunc.h"
#include "QspiDataProgrammer.h"
```

After `daisy_seed.Init(true);`, add:

```cpp
    CopyRamFuncs();
    daisy_seed.PrintLine("ramfunc probe addr=%s value=0x%08lx",
                         QspiProgrammerRamProbeAddressOk() ? "OK" : "BAD",
                         (unsigned long)QspiProgrammerRamProbeAdd(1, 2));
```

- [ ] **Step 5: Add sources to firmware build**

In `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`, add:

```make
  RamFunc.cpp \
  QspiDataProgrammer.cpp \
```

- [ ] **Step 6: Build and inspect the ELF**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
make
arm-none-eabi-nm -n build/NamPlatform.elf | rg "QspiProgrammerRamProbeAddImpl|_sramfunc|_eramfunc|_siramfunc"
```

Expected:

- `_sramfunc`, `_eramfunc`, and `QspiProgrammerRamProbeAddImpl` have SRAM-like addresses such as `0x240xxxxx`.
- `_siramfunc` has a QSPI load address such as `0x900xxxxx`.

- [ ] **Step 7: Hardware probe**

Flash the firmware and read the USB log.

Expected serial log:

```text
ramfunc probe addr=OK value=0x12340003
```

- [ ] **Step 8: Decide whether to continue**

If the symbol and hardware probe fail, stop this feature and write a replacement
plan for app-triggered bootloader DFU. If they pass, continue.

- [ ] **Step 9: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add third_party/libDaisy/core/STM32H750IB_qspi.lds RamFunc.h RamFunc.cpp QspiDataProgrammer.h QspiDataProgrammer.cpp main.cpp Makefile
git commit -m "feat: add RAM-resident updater section"
```

---

### Task 2: CDC Frame Codec

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/CdcFrame.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/CdcFrame.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_cdc_frame.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Write failing frame codec tests**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_cdc_frame.cpp`:

```cpp
#include "test_harness.h"
#include "../CdcFrame.h"
#include <cstring>

static void test_round_trip_frame()
{
    CdcFrame frame{};
    frame.type = CdcFrameType::HelloReq;
    frame.seq = 7;
    const uint8_t payload[] = {1, 2, 3, 4};
    uint8_t encoded[kCdcHeaderSize + kCdcMaxPayload] = {};
    size_t encoded_len = 0;
    CHECK(CdcEncodeFrame(frame.type, frame.seq, payload, sizeof(payload),
                         encoded, sizeof(encoded), encoded_len));

    CdcFrameParser parser;
    CdcFrame out{};
    bool got = false;
    for (size_t i = 0; i < encoded_len; ++i)
        got = parser.PushByte(encoded[i], out) || got;

    CHECK(got);
    CHECK_EQ((uint8_t)out.type, (uint8_t)CdcFrameType::HelloReq);
    CHECK_EQ(out.seq, 7u);
    CHECK_EQ(out.length, 4u);
    CHECK_EQ(out.payload[0], 1u);
    CHECK_EQ(out.payload[3], 4u);
}

static void test_parser_ignores_log_noise_before_magic()
{
    uint8_t encoded[kCdcHeaderSize + kCdcMaxPayload] = {};
    size_t encoded_len = 0;
    const uint8_t payload[] = {9};
    CHECK(CdcEncodeFrame(CdcFrameType::HelloResp, 2, payload, sizeof(payload),
                         encoded, sizeof(encoded), encoded_len));

    CdcFrameParser parser;
    CdcFrame out{};
    const char* noise = "cb=100 cpu_peak=0.82ms\r\n";
    for (const char* p = noise; *p; ++p)
        CHECK(!parser.PushByte(static_cast<uint8_t>(*p), out));

    bool got = false;
    for (size_t i = 0; i < encoded_len; ++i)
        got = parser.PushByte(encoded[i], out) || got;

    CHECK(got);
    CHECK_EQ((uint8_t)out.type, (uint8_t)CdcFrameType::HelloResp);
}

static void test_bad_crc_rejected()
{
    uint8_t encoded[kCdcHeaderSize + kCdcMaxPayload] = {};
    size_t encoded_len = 0;
    const uint8_t payload[] = {1, 2, 3};
    CHECK(CdcEncodeFrame(CdcFrameType::DataChunk, 1, payload, sizeof(payload),
                         encoded, sizeof(encoded), encoded_len));
    encoded[encoded_len - 1] ^= 0x55;

    CdcFrameParser parser;
    CdcFrame out{};
    bool got = false;
    for (size_t i = 0; i < encoded_len; ++i)
        got = parser.PushByte(encoded[i], out) || got;

    CHECK(!got);
}

int main()
{
    test_round_trip_frame();
    test_parser_ignores_log_noise_before_magic();
    test_bad_crc_rejected();
    return test_summary("cdc_frame");
}
```

- [ ] **Step 2: Add test target**

In `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`, add:

```make
CDC_FRAME_SRCS = ../CdcFrame.cpp
BINARIES = test_data_format test_qspi_storage test_preset_manager test_ir_loader test_eq3 test_audio_engine test_cdc_frame test_quad_encoder test_meter_fill test_real_fft_128 test_partitioned_convolver test_display_transfer test_ui_mode

test_cdc_frame: test_cdc_frame.cpp $(CDC_FRAME_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

Add to `run`:

```make
	@echo "=== test_cdc_frame ==="
	./test_cdc_frame
```

- [ ] **Step 3: Run and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_cdc_frame
```

Expected: fail because `CdcFrame.h` and `CdcFrame.cpp` do not exist.

- [ ] **Step 4: Implement frame codec**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/CdcFrame.h`:

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

static constexpr uint32_t kCdcFrameMagic = 0x3144434Eu; // "NCD1" little-endian
static constexpr uint32_t kCdcMaxPayload = 1024;
static constexpr uint32_t kCdcHeaderSize = 16;

enum class CdcFrameType : uint8_t
{
    HelloReq = 1,
    HelloResp = 2,
    BeginUpdate = 3,
    BeginAck = 4,
    DataChunk = 5,
    DataAck = 6,
    CommitUpdate = 7,
    Progress = 8,
    Done = 9,
    Error = 10,
    Reboot = 11,
};

struct CdcFrame
{
    CdcFrameType type = CdcFrameType::Error;
    uint8_t flags = 0;
    uint16_t seq = 0;
    uint32_t length = 0;
    uint8_t payload[kCdcMaxPayload] = {};
};

uint32_t CdcCrc32(const uint8_t* data, size_t len);
bool CdcEncodeFrame(CdcFrameType type, uint16_t seq, const uint8_t* payload,
                    uint32_t length, uint8_t* out, size_t out_capacity,
                    size_t& written);

class CdcFrameParser
{
public:
    bool PushByte(uint8_t b, CdcFrame& out);
    void Reset();

private:
    uint8_t header_[kCdcHeaderSize] = {};
    uint8_t payload_[kCdcMaxPayload] = {};
    uint32_t header_pos_ = 0;
    uint32_t payload_pos_ = 0;
    uint32_t expected_len_ = 0;
    uint32_t expected_crc_ = 0;
    bool reading_payload_ = false;
};
```

Create `/Users/bbalazs/daisy/daisy-nam-pedal/CdcFrame.cpp`:

```cpp
#include "CdcFrame.h"
#include <cstring>

static uint16_t read_u16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write_u16(uint8_t* out, size_t& pos, uint16_t v)
{
    out[pos++] = (uint8_t)(v & 0xff);
    out[pos++] = (uint8_t)(v >> 8);
}
static void write_u32(uint8_t* out, size_t& pos, uint32_t v)
{
    out[pos++] = (uint8_t)(v & 0xff);
    out[pos++] = (uint8_t)((v >> 8) & 0xff);
    out[pos++] = (uint8_t)((v >> 16) & 0xff);
    out[pos++] = (uint8_t)((v >> 24) & 0xff);
}

uint32_t CdcCrc32(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool CdcEncodeFrame(CdcFrameType type, uint16_t seq, const uint8_t* payload,
                    uint32_t length, uint8_t* out, size_t out_capacity,
                    size_t& written)
{
    written = 0;
    if (!out || length > kCdcMaxPayload || out_capacity < kCdcHeaderSize + length)
        return false;
    write_u32(out, written, kCdcFrameMagic);
    out[written++] = (uint8_t)type;
    out[written++] = 0;
    write_u16(out, written, seq);
    write_u32(out, written, length);
    write_u32(out, written, CdcCrc32(payload, length));
    if (length > 0)
        std::memcpy(out + written, payload, length);
    written += length;
    return true;
}

void CdcFrameParser::Reset()
{
    header_pos_ = 0;
    payload_pos_ = 0;
    expected_len_ = 0;
    expected_crc_ = 0;
    reading_payload_ = false;
}

bool CdcFrameParser::PushByte(uint8_t b, CdcFrame& out)
{
    if (!reading_payload_)
    {
        header_[header_pos_++] = b;
        if (header_pos_ < 4)
            return false;

        if (read_u32(header_) != kCdcFrameMagic)
        {
            std::memmove(header_, header_ + 1, 3);
            header_pos_ = 3;
            return false;
        }

        if (header_pos_ < kCdcHeaderSize)
            return false;

        expected_len_ = read_u32(header_ + 8);
        expected_crc_ = read_u32(header_ + 12);
        if (expected_len_ > kCdcMaxPayload)
        {
            Reset();
            return false;
        }
        payload_pos_ = 0;
        reading_payload_ = true;
        if (expected_len_ == 0)
        {
            out.type = (CdcFrameType)header_[4];
            out.flags = header_[5];
            out.seq = read_u16(header_ + 6);
            out.length = 0;
            Reset();
            return true;
        }
        return false;
    }

    payload_[payload_pos_++] = b;
    if (payload_pos_ < expected_len_)
        return false;

    bool ok = CdcCrc32(payload_, expected_len_) == expected_crc_;
    if (ok)
    {
        out.type = (CdcFrameType)header_[4];
        out.flags = header_[5];
        out.seq = read_u16(header_ + 6);
        out.length = expected_len_;
        std::memcpy(out.payload, payload_, expected_len_);
    }
    Reset();
    return ok;
}
```

- [ ] **Step 5: Run tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_cdc_frame && ./test_cdc_frame
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add CdcFrame.h CdcFrame.cpp tests/test_cdc_frame.cpp tests/Makefile
git commit -m "feat: add CDC frame codec"
```

---

### Task 3: Library Image Validator

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibraryImageValidator.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibraryImageValidator.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_library_image_validator.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Write failing validator tests**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_library_image_validator.cpp`:

```cpp
#include "test_harness.h"
#include "../LibraryImageValidator.h"
#include "../data_format.h"
#include <cstring>
#include <vector>

static void put_name(char* dst, const char* src)
{
    std::memset(dst, 0, NAM_DATA_NAME_LEN);
    std::strncpy(dst, src, NAM_DATA_NAME_LEN - 1);
}

static std::vector<uint8_t> make_valid_image()
{
    std::vector<uint8_t> img(8192, 0xff);
    auto* hdr = reinterpret_cast<NamDataHeader*>(img.data());
    hdr->magic = NAM_DATA_MAGIC;
    hdr->version = NAM_DATA_VERSION;
    hdr->count = 1;

    auto* e = reinterpret_cast<NamDataEntry*>(img.data() + sizeof(NamDataHeader));
    e->type = NAM_ENTRY_PRESET;
    put_name(e->name, "Preset");
    e->offset = 4096;
    e->length = sizeof(NamPreset);
    e->samplerate = 0;
    e->reserved = 0;
    return img;
}

static void test_valid_image_accepts()
{
    auto img = make_valid_image();
    LibraryImageValidationResult r = ValidateLibraryImage(img.data(), img.size());
    CHECK(r.ok);
}

static void test_bad_magic_rejects()
{
    auto img = make_valid_image();
    img[0] = 0;
    LibraryImageValidationResult r = ValidateLibraryImage(img.data(), img.size());
    CHECK(!r.ok);
    CHECK_EQ((uint8_t)r.error, (uint8_t)LibraryImageError::BadMagic);
}

static void test_blob_past_image_rejects()
{
    auto img = make_valid_image();
    auto* e = reinterpret_cast<NamDataEntry*>(img.data() + sizeof(NamDataHeader));
    e->offset = 4096;
    e->length = 999999;
    LibraryImageValidationResult r = ValidateLibraryImage(img.data(), img.size());
    CHECK(!r.ok);
    CHECK_EQ((uint8_t)r.error, (uint8_t)LibraryImageError::EntryOutOfBounds);
}

static void test_unaligned_blob_rejects()
{
    auto img = make_valid_image();
    auto* e = reinterpret_cast<NamDataEntry*>(img.data() + sizeof(NamDataHeader));
    e->offset = 4100;
    LibraryImageValidationResult r = ValidateLibraryImage(img.data(), img.size());
    CHECK(!r.ok);
    CHECK_EQ((uint8_t)r.error, (uint8_t)LibraryImageError::UnalignedEntry);
}

int main()
{
    test_valid_image_accepts();
    test_bad_magic_rejects();
    test_blob_past_image_rejects();
    test_unaligned_blob_rejects();
    return test_summary("library_image_validator");
}
```

- [ ] **Step 2: Add test target**

In `tests/Makefile`, add:

```make
VALIDATOR_SRCS = ../LibraryImageValidator.cpp

test_library_image_validator: test_library_image_validator.cpp $(VALIDATOR_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@
```

Add `test_library_image_validator` to `BINARIES` and `run`.

- [ ] **Step 3: Run and confirm failure**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_library_image_validator
```

Expected: fail because validator files do not exist.

- [ ] **Step 4: Implement validator**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/LibraryImageValidator.h`:

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

enum class LibraryImageError : uint8_t
{
    None = 0,
    TooSmall,
    TooLarge,
    BadMagic,
    UnsupportedVersion,
    DirectoryOutOfBounds,
    EntryOutOfBounds,
    UnalignedEntry,
    BadEntryType,
};

struct LibraryImageValidationResult
{
    bool ok = false;
    LibraryImageError error = LibraryImageError::None;
    uint16_t entry_count = 0;
};

LibraryImageValidationResult ValidateLibraryImage(const uint8_t* image, size_t length);
```

Create `/Users/bbalazs/daisy/daisy-nam-pedal/LibraryImageValidator.cpp`:

```cpp
#include "LibraryImageValidator.h"
#include "data_format.h"

static LibraryImageValidationResult fail(LibraryImageError e)
{
    LibraryImageValidationResult r;
    r.ok = false;
    r.error = e;
    return r;
}

LibraryImageValidationResult ValidateLibraryImage(const uint8_t* image, size_t length)
{
    if (!image || length < sizeof(NamDataHeader))
        return fail(LibraryImageError::TooSmall);
    if (length > NAM_DATA_PARTITION_SIZE)
        return fail(LibraryImageError::TooLarge);

    const auto* hdr = reinterpret_cast<const NamDataHeader*>(image);
    if (hdr->magic != NAM_DATA_MAGIC)
        return fail(LibraryImageError::BadMagic);
    if (hdr->version != NAM_DATA_VERSION)
        return fail(LibraryImageError::UnsupportedVersion);

    size_t dir_end = sizeof(NamDataHeader) + (size_t)hdr->count * sizeof(NamDataEntry);
    if (dir_end > length)
        return fail(LibraryImageError::DirectoryOutOfBounds);

    const auto* entries = reinterpret_cast<const NamDataEntry*>(image + sizeof(NamDataHeader));
    for (uint16_t i = 0; i < hdr->count; ++i)
    {
        const NamDataEntry& e = entries[i];
        if (e.type > NAM_ENTRY_PRESET)
            return fail(LibraryImageError::BadEntryType);
        if ((e.offset % NAM_DATA_SECTOR_SIZE) != 0)
            return fail(LibraryImageError::UnalignedEntry);
        if ((size_t)e.offset + (size_t)e.length > length)
            return fail(LibraryImageError::EntryOutOfBounds);
    }

    LibraryImageValidationResult r;
    r.ok = true;
    r.error = LibraryImageError::None;
    r.entry_count = hdr->count;
    return r;
}
```

- [ ] **Step 5: Run validator tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_library_image_validator && ./test_library_image_validator
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add LibraryImageValidator.h LibraryImageValidator.cpp tests/test_library_image_validator.cpp tests/Makefile
git commit -m "feat: validate library images"
```

---

### Task 4: Firmware Sync State Machine With Fake Programmer

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibrarySyncServer.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/LibrarySyncServer.cpp`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_library_sync_server.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`

- [ ] **Step 1: Write sync server tests**

Create `/Users/bbalazs/daisy/daisy-nam-pedal/tests/test_library_sync_server.cpp`:

```cpp
#include "test_harness.h"
#include "../LibrarySyncServer.h"
#include "../data_format.h"
#include <cstring>
#include <vector>

static std::vector<uint8_t> make_image()
{
    std::vector<uint8_t> img(8192, 0xff);
    auto* hdr = reinterpret_cast<NamDataHeader*>(img.data());
    hdr->magic = NAM_DATA_MAGIC;
    hdr->version = NAM_DATA_VERSION;
    hdr->count = 0;
    return img;
}

static void test_rejects_commit_before_complete_image()
{
    uint8_t staging[8192] = {};
    FakeQspiDataProgrammer flash;
    LibrarySyncServer server(staging, sizeof(staging), flash);

    auto image = make_image();
    CHECK(server.Begin(image.size(), CdcCrc32(image.data(), image.size())));
    CHECK(server.ReceiveChunk(0, image.data(), 100));
    CHECK(!server.Commit());
    CHECK_EQ(flash.erase_count, 0u);
}

static void test_valid_image_programs_header_last()
{
    uint8_t staging[8192] = {};
    FakeQspiDataProgrammer flash;
    LibrarySyncServer server(staging, sizeof(staging), flash);

    auto image = make_image();
    CHECK(server.Begin(image.size(), CdcCrc32(image.data(), image.size())));
    CHECK(server.ReceiveChunk(0, image.data(), image.size()));
    CHECK(server.Commit());
    CHECK_EQ(flash.erase_count, 2u);
    CHECK(flash.header_written_last);
}

static void test_bad_crc_never_erases()
{
    uint8_t staging[8192] = {};
    FakeQspiDataProgrammer flash;
    LibrarySyncServer server(staging, sizeof(staging), flash);

    auto image = make_image();
    CHECK(server.Begin(image.size(), 0xdeadbeefu));
    CHECK(server.ReceiveChunk(0, image.data(), image.size()));
    CHECK(!server.Commit());
    CHECK_EQ(flash.erase_count, 0u);
}

int main()
{
    test_rejects_commit_before_complete_image();
    test_valid_image_programs_header_last();
    test_bad_crc_never_erases();
    return test_summary("library_sync_server");
}
```

- [ ] **Step 2: Add programmer interface and fake**

In `QspiDataProgrammer.h`, append:

```cpp
class IQspiDataProgrammer
{
public:
    virtual ~IQspiDataProgrammer() = default;
    virtual bool EraseSector(uint32_t partition_offset) = 0;
    virtual bool Program(uint32_t partition_offset, const uint8_t* data, uint32_t length) = 0;
    virtual bool Verify(uint32_t partition_offset, const uint8_t* data, uint32_t length) = 0;
};

#ifdef HOST_BUILD
class FakeQspiDataProgrammer : public IQspiDataProgrammer
{
public:
    uint32_t erase_count = 0;
    bool header_written_last = false;
    uint32_t last_program_offset = 0xffffffffu;

    bool EraseSector(uint32_t partition_offset) override;
    bool Program(uint32_t partition_offset, const uint8_t* data, uint32_t length) override;
    bool Verify(uint32_t partition_offset, const uint8_t* data, uint32_t length) override;
};
#endif
```

In `QspiDataProgrammer.cpp`, append host fake methods under `#ifdef HOST_BUILD`.
They should track `erase_count`, set `last_program_offset`, and set
`header_written_last = true` when `partition_offset == 0` and at least one
non-header sector was already programmed.

- [ ] **Step 3: Implement server**

Create `LibrarySyncServer.h`:

```cpp
#pragma once
#include "CdcFrame.h"
#include "QspiDataProgrammer.h"
#include <stdint.h>
#include <stddef.h>

class LibrarySyncServer
{
public:
    LibrarySyncServer(uint8_t* staging, size_t staging_size, IQspiDataProgrammer& programmer);

    bool Begin(uint32_t image_size, uint32_t expected_crc);
    bool ReceiveChunk(uint32_t offset, const uint8_t* data, uint32_t length);
    bool Commit();

    uint32_t ImageSize() const { return image_size_; }
    uint32_t ReceivedBytes() const { return received_bytes_; }

private:
    uint8_t* staging_;
    size_t staging_size_;
    IQspiDataProgrammer& programmer_;
    uint32_t image_size_ = 0;
    uint32_t expected_crc_ = 0;
    uint32_t received_bytes_ = 0;
    bool begun_ = false;
};
```

Create `LibrarySyncServer.cpp`:

```cpp
#include "LibrarySyncServer.h"
#include "LibraryImageValidator.h"
#include "data_format.h"
#include <cstring>

LibrarySyncServer::LibrarySyncServer(uint8_t* staging, size_t staging_size, IQspiDataProgrammer& programmer)
    : staging_(staging), staging_size_(staging_size), programmer_(programmer)
{
}

bool LibrarySyncServer::Begin(uint32_t image_size, uint32_t expected_crc)
{
    if (image_size == 0 || image_size > staging_size_ || image_size > NAM_DATA_PARTITION_SIZE)
        return false;
    image_size_ = image_size;
    expected_crc_ = expected_crc;
    received_bytes_ = 0;
    begun_ = true;
    return true;
}

bool LibrarySyncServer::ReceiveChunk(uint32_t offset, const uint8_t* data, uint32_t length)
{
    if (!begun_ || !data || offset + length > image_size_)
        return false;
    std::memcpy(staging_ + offset, data, length);
    if (offset == received_bytes_)
        received_bytes_ += length;
    return true;
}

bool LibrarySyncServer::Commit()
{
    if (!begun_ || received_bytes_ != image_size_)
        return false;
    if (CdcCrc32(staging_, image_size_) != expected_crc_)
        return false;
    if (!ValidateLibraryImage(staging_, image_size_).ok)
        return false;

    const uint32_t sector = NAM_DATA_SECTOR_SIZE;
    const uint32_t sectors = (image_size_ + sector - 1u) / sector;

    for (uint32_t s = 1; s < sectors; ++s)
        if (!programmer_.EraseSector(s * sector))
            return false;
    if (!programmer_.EraseSector(0))
        return false;

    for (uint32_t s = 1; s < sectors; ++s)
    {
        uint32_t off = s * sector;
        uint32_t len = (off + sector <= image_size_) ? sector : (image_size_ - off);
        if (!programmer_.Program(off, staging_ + off, len))
            return false;
    }
    uint32_t header_len = image_size_ < sector ? image_size_ : sector;
    if (!programmer_.Program(0, staging_, header_len))
        return false;

    return programmer_.Verify(0, staging_, image_size_);
}
```

- [ ] **Step 4: Add test target**

Add `test_library_sync_server` to `/Users/bbalazs/daisy/daisy-nam-pedal/tests/Makefile`
with sources:

```make
SYNC_SERVER_SRCS = ../LibrarySyncServer.cpp ../LibraryImageValidator.cpp ../CdcFrame.cpp ../QspiDataProgrammer.cpp
```

- [ ] **Step 5: Run tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make test_library_sync_server && ./test_library_sync_server
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add QspiDataProgrammer.h QspiDataProgrammer.cpp LibrarySyncServer.h LibrarySyncServer.cpp tests/test_library_sync_server.cpp tests/Makefile
git commit -m "feat: add library sync state machine"
```

---

### Task 5: Firmware USB CDC Integration

**Files:**
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/UsbCdcLink.h`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal/UsbCdcLink.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/LibrarySyncServer.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/LibrarySyncServer.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/main.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/Makefile`

- [ ] **Step 1: Add USB link**

Create `UsbCdcLink.h`:

```cpp
#pragma once
#include "CdcFrame.h"
#include "daisy_seed.h"
#include <stdint.h>

class UsbCdcLink
{
public:
    void Init();
    bool PollFrame(CdcFrame& frame);
    bool SendFrame(CdcFrameType type, uint16_t seq, const uint8_t* payload, uint32_t length);

private:
    static void RxCallback(uint8_t* data, uint32_t* len);
    static constexpr uint32_t kRxSize = 4096;
    static volatile uint32_t write_pos_;
    static volatile uint32_t read_pos_;
    static uint8_t rx_[kRxSize];
    daisy::UsbHandle usb_;
    CdcFrameParser parser_;
};
```

Create `UsbCdcLink.cpp` with a power-of-two ring buffer, `UsbHandle::FS_INTERNAL`,
`SetReceiveCallback()`, and `TransmitInternal()` for encoded frames. The callback
must only copy bytes into the ring.

- [ ] **Step 2: Add sync protocol handling**

Extend `LibrarySyncServer` with:

```cpp
bool HandleFrame(const CdcFrame& in, UsbCdcLink& link);
```

Implement handling for:

- `HelloReq`: send `HelloResp` with protocol version, `NAM_DATA_VERSION`,
  `NAM_DATA_PARTITION_SIZE`, and `kCdcMaxPayload`.
- `BeginUpdate`: parse image size and CRC, call `Begin()`, send `BeginAck` or
  `Error`.
- `DataChunk`: parse offset, call `ReceiveChunk()`, send `DataAck`.
- `CommitUpdate`: call `Commit()`, send `Done` or `Error`.
- `Reboot`: call `System::Reset()`.

- [ ] **Step 3: Add SDRAM staging and sync polling in `main.cpp`**

Add globals:

```cpp
static uint8_t library_image_staging[NAM_DATA_PARTITION_SIZE]
    __attribute__((section(".sdram_bss")));
static UsbCdcLink usb_sync_link;
static HardwareQspiDataProgrammer qspi_programmer;
static LibrarySyncServer library_sync(library_image_staging, sizeof(library_image_staging), qspi_programmer);
```

After USB logging initialization:

```cpp
    usb_sync_link.Init();
```

In the main loop, before control processing:

```cpp
        CdcFrame sync_frame;
        while (usb_sync_link.PollFrame(sync_frame))
        {
            bool entering_update = sync_frame.type == CdcFrameType::BeginUpdate;
            if (entering_update)
            {
                daisy_seed.StopAudio();
                delete audio_engine.SwapIR(nullptr);
                audio_engine.SwapModel(nullptr);
                browsing = false;
                editing = false;
            }
            library_sync.HandleFrame(sync_frame, usb_sync_link);
        }
```

- [ ] **Step 4: Add sources**

In `Makefile`, add:

```make
  CdcFrame.cpp \
  LibraryImageValidator.cpp \
  LibrarySyncServer.cpp \
  UsbCdcLink.cpp \
```

- [ ] **Step 5: Build firmware**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
make
```

Expected: firmware builds.

- [ ] **Step 6: Hardware handshake smoke test**

Use a serial terminal or a tiny host script to send a `HELLO_REQ` frame and confirm
`HELLO_RESP` bytes are returned. Logs may still appear before binary response; the
host parser must scan for `NCD1`.

- [ ] **Step 7: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add UsbCdcLink.h UsbCdcLink.cpp LibrarySyncServer.h LibrarySyncServer.cpp main.cpp Makefile
git commit -m "feat: serve library sync over USB CDC"
```

---

### Task 6: Hardware QSPI Data Programmer

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.h`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/QspiDataProgrammer.cpp`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal/LibrarySyncServer.cpp`

- [ ] **Step 1: Implement hardware programmer API**

Add to `QspiDataProgrammer.h`:

```cpp
#ifndef HOST_BUILD
class HardwareQspiDataProgrammer : public IQspiDataProgrammer
{
public:
    bool EraseSector(uint32_t partition_offset) override;
    bool Program(uint32_t partition_offset, const uint8_t* data, uint32_t length) override;
    bool Verify(uint32_t partition_offset, const uint8_t* data, uint32_t length) override;
};
#endif
```

In `QspiDataProgrammer.cpp`, implement methods so public methods call
RAM-resident erase/program helpers. The critical helpers must:

- run from `.ramfunc`
- not call `daisy::QSPIHandle::Write()` or `Erase()` because those reject
  `BOOT_QSPI`
- not call any QSPI-resident code while the QSPI peripheral is in indirect mode
- use only internal-RAM code/data during erase/program commands
- restore memory-mapped mode before returning

- [ ] **Step 2: Add symbol inspection gate**

After implementation, run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
make
arm-none-eabi-nm -n build/NamPlatform.elf | rg "Qspi.*Ram|Program.*Ram|Erase.*Ram"
```

Expected: all critical erase/program helper symbols are in SRAM/ITCM, not QSPI.

- [ ] **Step 3: Run 64 KiB hardware update**

Use desktop or a temporary host sender to transfer a 64 KiB valid image. Confirm:

- audio stops
- transfer completes
- programming completes
- verify completes
- firmware reboots
- `QspiStorage::Init()` reports valid storage after reboot

- [ ] **Step 4: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git add QspiDataProgrammer.h QspiDataProgrammer.cpp LibrarySyncServer.cpp
git commit -m "feat: program data partition from CDC sync"
```

---

### Task 7: Desktop CDC Protocol And Backend

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/Cargo.toml`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/cdc_protocol.rs`
- Create: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/sync.rs`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/commands/mod.rs`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri/src/lib.rs`

- [ ] **Step 1: Add serial dependency**

In `src-tauri/Cargo.toml`, add:

```toml
serialport = "4"
```

- [ ] **Step 2: Add Rust frame codec tests**

Create `src-tauri/src/cdc_protocol.rs` with tests equivalent to firmware:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frame_round_trips() {
        let encoded = encode_frame(FrameType::HelloReq, 7, &[1, 2, 3, 4]);
        let mut parser = FrameParser::default();
        let mut out = None;
        for b in encoded {
            if let Some(frame) = parser.push(b).unwrap() {
                out = Some(frame);
            }
        }
        let frame = out.unwrap();
        assert_eq!(frame.frame_type, FrameType::HelloReq);
        assert_eq!(frame.seq, 7);
        assert_eq!(frame.payload, vec![1, 2, 3, 4]);
    }

    #[test]
    fn parser_ignores_log_noise() {
        let encoded = encode_frame(FrameType::HelloResp, 2, &[9]);
        let mut parser = FrameParser::default();
        for b in b"cb=100 cpu_peak=0.8ms\r\n" {
            assert!(parser.push(*b).unwrap().is_none());
        }
        let mut out = None;
        for b in encoded {
            out = parser.push(b).unwrap().or(out);
        }
        assert_eq!(out.unwrap().frame_type, FrameType::HelloResp);
    }
}
```

Implement `FrameType`, `Frame`, `encode_frame()`, `FrameParser`, and CRC32 using
the same header layout as firmware.

- [ ] **Step 3: Add sync command**

Create `src-tauri/src/commands/sync.rs`:

```rust
use crate::cdc_protocol::{encode_frame, FrameParser, FrameType};
use serialport::SerialPort;
use std::fs;
use std::io::{Read, Write};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter};

#[tauri::command]
pub fn detect_sync_device() -> Result<Option<String>, String> {
    for port in serialport::available_ports().map_err(|e| e.to_string())? {
        if let Ok(mut p) = serialport::new(&port.port_name, 115_200)
            .timeout(Duration::from_millis(250))
            .open()
        {
            if handshake(&mut *p).is_ok() {
                return Ok(Some(port.port_name));
            }
        }
    }
    Ok(None)
}

#[tauri::command]
pub fn sync_image(app: AppHandle, image_path: String) -> Result<(), String> {
    let image = fs::read(&image_path).map_err(|e| format!("read image: {e}"))?;
    let port_name = detect_sync_device()?.ok_or("No running pedal found over USB CDC")?;
    let mut port = serialport::new(&port_name, 115_200)
        .timeout(Duration::from_millis(1000))
        .open()
        .map_err(|e| format!("open {port_name}: {e}"))?;

    send_update(&app, &mut *port, &image)
}
```

Add these helper behaviors in the same file:

- `read_frame(port, timeout)` loops until timeout, feeds bytes into `FrameParser`,
  and returns the first decoded frame.
- `handshake(port)` writes `HELLO_REQ`, waits for `HELLO_RESP`, and verifies the
  response advertises protocol version `1` and a max chunk size of at least `1024`.
- `send_update(app, port, image)` computes CRC32, sends `BEGIN_UPDATE`, waits for
  `BEGIN_ACK`, sends 1024-byte `DATA_CHUNK` frames in offset order, waits for
  matching `DATA_ACK` after each chunk, sends `COMMIT_UPDATE`, then waits for
  `DONE`.
- If any received frame is `ERROR`, return `Err` with the device message.
- Emit `sync-progress` events with `{ percent, message }` during connect,
  transfer, validate/program, verify, and reboot stages.

- [ ] **Step 4: Register commands**

In `commands/mod.rs`:

```rust
pub mod sync;
```

In `lib.rs`, import and register:

```rust
use commands::{library, presets, flash, discover, sync};
```

```rust
sync::detect_sync_device,
sync::sync_image,
```

- [ ] **Step 5: Run Rust tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test cdc_protocol
cargo test
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src-tauri/Cargo.toml src-tauri/Cargo.lock src-tauri/src/cdc_protocol.rs src-tauri/src/commands/sync.rs src-tauri/src/commands/mod.rs src-tauri/src/lib.rs
git commit -m "feat: add USB CDC sync backend"
```

---

### Task 8: Desktop Sync UI

**Files:**
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/types.ts`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/lib/api.ts`
- Modify: `/Users/bbalazs/daisy/daisy-nam-pedal-application/src/pages/FlashPage.tsx`

- [ ] **Step 1: Add API methods**

In `src/lib/api.ts`, add:

```ts
export const detectSyncDevice = () => invoke<string | null>("detect_sync_device");
export const syncImage = (imagePath: string) => invoke<void>("sync_image", { imagePath });
```

- [ ] **Step 2: Update FlashPage state names**

In `FlashPage.tsx`, keep `buildImage()` and existing summary table. Add:

```ts
const [syncDevice, setSyncDevice] = useState<string | null>(null);
const [syncing, setSyncing] = useState(false);
const [showDfuFallback, setShowDfuFallback] = useState(false);
```

Listen for `sync-progress` in addition to `flash-progress`.

- [ ] **Step 3: Add sync detect and sync handlers**

Add:

```ts
async function detectSyncDevice() {
  setDetecting(true);
  try {
    const port = await api.detectSyncDevice();
    setSyncDevice(port);
    setDeviceFound(port !== null);
  } catch (e) {
    toast.error(String(e));
  } finally {
    setDetecting(false);
  }
}

async function handleSync() {
  setSyncing(true);
  setProgress(0);
  setProgressMsg("Starting sync...");
  try {
    const s = summary ?? await api.buildImage();
    setSummary(s);
    await api.syncImage(s.image_path);
    toast.success("Sync complete. Pedal rebooting.");
  } catch (e) {
    toast.error(String(e));
  } finally {
    setSyncing(false);
    setProgress(0);
    setProgressMsg("");
  }
}
```

- [ ] **Step 4: Make CDC sync primary**

Change visible copy:

- Page title: `Sync`
- Description: `Build and sync your library to the running pedal`
- Device good state: `Pedal connected`
- Missing state: `Plug in the pedal while it is running, then click Detect`
- Primary button: `Sync to Pedal`

Keep the existing DFU button and BOOT/RESET instructions inside a collapsed or
secondary `Advanced DFU recovery` section controlled by `showDfuFallback`.

- [ ] **Step 5: Run frontend build**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
npm run build
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git add src/lib/api.ts src/lib/types.ts src/pages/FlashPage.tsx
git commit -m "feat: make USB sync the primary library update flow"
```

---

### Task 9: Full Verification

**Files:**
- No new source files unless a previous task needs a small test fixture.

- [ ] **Step 1: Run firmware host tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal/tests
make clean
make run
```

Expected: all host tests pass.

- [ ] **Step 2: Run desktop tests**

Run:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
npm run build
```

Expected: Rust tests and frontend build pass.

- [ ] **Step 3: Hardware handshake**

Flash firmware by existing app/DFU flow once. With the pedal running normally:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application/src-tauri
cargo test -- --ignored cdc_hardware_handshake
```

Expected: test or temporary tool finds a CDC port and receives `HELLO_RESP`.

- [ ] **Step 4: Hardware 64 KiB image sync**

Build a small valid data image and sync it from the app. Expected:

- app shows transfer, programming, verify, reboot
- pedal reboots
- serial log shows valid data partition
- no manual BOOT/RESET step

- [ ] **Step 5: Hardware full image sync**

Use the real current library with models, IRs, and presets. Expected:

- full image sync completes
- pedal reboots into the new library
- model/IR/preset names match app summary
- pre/post effect preset fields from feature one still round-trip if present

- [ ] **Step 6: Failure recovery tests**

Run three failure checks:

- Send image with bad CRC. Expected: firmware rejects before erase.
- Send oversized image. Expected: firmware rejects before erase.
- Power-cycle during programming. Expected: firmware still boots, reports bad or
  missing data partition, and CDC sync can retry.

- [ ] **Step 7: Final commits**

Commit any remaining firmware changes:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal
git status --short
git add .
git commit -m "feat: sync library over USB CDC"
```

Commit any remaining desktop app changes:

```bash
cd /Users/bbalazs/daisy/daisy-nam-pedal-application
git status --short
git add .
git commit -m "feat: sync pedal library over USB CDC"
```

## Final Review Checklist

- [ ] RAM-resident QSPI updater proof passed on hardware.
- [ ] CDC parser ignores normal serial log text before frame magic.
- [ ] Firmware validates a complete staged image before erasing QSPI.
- [ ] Firmware writes header sector last.
- [ ] Firmware can boot and accept CDC sync with a corrupt/missing data partition.
- [ ] Desktop app uses CDC sync as primary UX.
- [ ] Existing DFU flow remains available as fallback.
- [ ] Sync uses the same app-built image as DFU.
- [ ] Firmware host tests, desktop Rust tests, and frontend build pass.
