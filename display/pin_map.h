// Shim that maps pedal::pins::DISP_* to the values in HardwareConfig.h.
// st7789_driver.cpp includes "../config/pin_map.h" — this file satisfies that
// include without modifying the driver source.
#pragma once
#include "../HardwareConfig.h"

namespace pedal {
namespace pins {

// Mirror every display pin from hw:: so the driver compiles unchanged.
constexpr daisy::Pin DISP_CS  = hw::PIN_DISP_CS;
constexpr daisy::Pin DISP_DC  = hw::PIN_DISP_DC;
constexpr daisy::Pin DISP_RES = hw::PIN_DISP_RST;
constexpr daisy::Pin DISP_BLK = hw::PIN_DISP_BLK;
constexpr daisy::Pin DISP_SCK = hw::PIN_DISP_SCK;
constexpr daisy::Pin DISP_SDA = hw::PIN_DISP_MOSI;

} // namespace pins
} // namespace pedal
