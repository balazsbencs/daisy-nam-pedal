#pragma once
#include "daisy_seed.h"
#include "per/spi.h"
#include "sys/system.h"
#include "display_transfer.h"
#include <cstdint>
#include <cstddef>

namespace pedal {

/// Low-level driver for the ST7789 240×320 IPS display over SPI1.
class St7789Driver {
public:
    void Init();

    /// Start a cooperative full-frame transfer.
    void PushFrame(const uint16_t* buf, uint32_t len_bytes);

    /// Send at most one display row. Call once per main-loop iteration.
    void Service();

    bool IsBusy() const { return transfer_.IsBusy(); }

    /// Blocking fill — use only during startup or test.
    void FillScreen(uint16_t color);

private:
    void WriteCmd(uint8_t cmd);
    void WriteData(const uint8_t* data, size_t len);
    void SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void HardReset();

    // Display dimensions.
    static constexpr uint16_t kDisplayWidth  = 240u;
    static constexpr uint16_t kDisplayHeight = 320u;
    static constexpr uint16_t kMaxX          = kDisplayWidth  - 1u;  // 239
    static constexpr uint16_t kMaxY          = kDisplayHeight - 1u;  // 319

    daisy::SpiHandle spi_;
    daisy::GPIO      pin_cs_;
    daisy::GPIO      pin_dc_;
    daisy::GPIO      pin_res_;
    daisy::GPIO      pin_blk_;
    DisplayTransferState transfer_;
};

} // namespace pedal
