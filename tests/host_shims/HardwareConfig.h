// Host shim: satisfies #include "HardwareConfig.h" without Daisy SDK pin types.
#pragma once
#include <cstddef>
#include <cstdint>

namespace hw {
    constexpr size_t   AUDIO_BLOCK_SIZE    = 48;
    constexpr float    AUDIO_SAMPLE_RATE   = 48000.0f;
    // Offset ignored by the host QSPI shim (GetData always returns g_fake_base).
    constexpr uint32_t QSPI_DATA_PARTITION_OFFSET = 0u;

    // Fake pin type — tests never call Init() on real hardware.
    struct FakePin { int id = 0; };
    using Pin = FakePin;
    constexpr Pin PIN_FS1         = {};
    constexpr Pin PIN_FS2         = {};
    constexpr Pin PIN_ENC1_A      = {};
    constexpr Pin PIN_ENC1_B      = {};
    constexpr Pin PIN_ENC1_CLICK  = {};
    constexpr Pin PIN_DISP_SCK    = {};
    constexpr Pin PIN_DISP_MOSI   = {};
    constexpr Pin PIN_DISP_CS     = {};
    constexpr Pin PIN_DISP_DC     = {};
    constexpr Pin PIN_DISP_RST    = {};
    constexpr Pin PIN_DISP_BLK    = {};
}
