// HardwareConfig.h — single source of truth for all Daisy Seed pin assignments.
//
// All pin assignments confirmed — see docs/HARDWARE.md for the full table.
//
// Daisy Seed GPIO naming: seed::D0–D30. Full pinout diagram:
//   https://electro-smith.com/daisy/daisy
//
// When HOST_BUILD is defined (tests/Makefile) the Daisy SDK types are excluded so
// this header compiles with clang++ on macOS/Linux without any Daisy toolchain.

#pragma once

#ifndef HOST_BUILD
#include "daisy_seed.h"
#endif

#include <stdint.h>
#include <stddef.h>

namespace hw
{

// ---------------------------------------------------------------------------
// Audio (fixed — driven by the Daisy Seed's audio codec, not configurable)
// Block size and sample rate that set the 1 ms audio deadline.
// ---------------------------------------------------------------------------
constexpr size_t AUDIO_BLOCK_SIZE  = 48;       // samples per callback
constexpr float  AUDIO_SAMPLE_RATE = 48000.0f;

// ---------------------------------------------------------------------------
// QSPI data partition (must match data_format.h and tools/flash_data.sh)
// ---------------------------------------------------------------------------
constexpr uint32_t QSPI_DATA_PARTITION_OFFSET = 0x00200000u; // 0x90200000 absolute

#ifndef HOST_BUILD

using namespace daisy;

// ---------------------------------------------------------------------------
// Footswitches (momentary, active-low, internal pull-up)
// These are the two playing-mode switches: FS1 = next preset, FS2 = prev preset.
// ---------------------------------------------------------------------------
constexpr Pin PIN_FS1 = seed::D15;
constexpr Pin PIN_FS2 = seed::D16;

// ---------------------------------------------------------------------------
// Encoder 1 (primary navigation/edit encoder)
// ---------------------------------------------------------------------------
constexpr Pin PIN_ENC1_A     = seed::D0;
constexpr Pin PIN_ENC1_B     = seed::D1;
constexpr Pin PIN_ENC1_CLICK = seed::D2;

// Encoder 2 (optional — set ENC2_PRESENT = false to disable)
constexpr Pin PIN_ENC2_A     = seed::D7;
constexpr Pin PIN_ENC2_B     = seed::D8;
constexpr Pin PIN_ENC2_CLICK = seed::D9;
constexpr bool ENC2_PRESENT = false; // flip to true when hardware is wired

// ---------------------------------------------------------------------------
// ST7789 display (240×320, SPI1) — confirmed pinout, see docs/HARDWARE.md
// ---------------------------------------------------------------------------
constexpr Pin PIN_DISP_SCK   = seed::D22; // SPI1 SCK  (PA5)
constexpr Pin PIN_DISP_MOSI  = seed::D18; // SPI1 MOSI (PA7)
constexpr Pin PIN_DISP_CS    = seed::D13; // Chip select, active-low (PB6)
constexpr Pin PIN_DISP_DC    = seed::D14; // Data/command (PB7)
constexpr Pin PIN_DISP_RST   = seed::D26; // Reset, active-low (PD11)
constexpr Pin PIN_DISP_BLK   = seed::D24; // Backlight, drive HIGH for ON (PA1)

#endif // HOST_BUILD

} // namespace hw
