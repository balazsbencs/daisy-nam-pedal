#pragma once
#include "daisy_seed.h"
#include "per/spi.h"
#include "sys/system.h"
#include <cstdint>
#include <cstddef>

namespace pedal {

/// Low-level driver for the ST7789 240×320 IPS display over SPI1.
/// Blocking helpers are used only during Init(); the display update
/// path uses StartDmaTransfer() which automatically chains multiple
/// DMA chunks (HAL_SPI_Transmit_DMA is limited to 65535 bytes;
/// the 240×320 frame buffer is 153 600 bytes).
class St7789Driver {
public:
    using DoneCallback = void (*)(void* ctx);

    void Init();

    /// Non-blocking full-frame DMA transfer.
    /// Internally splits into ≤65534-byte chunks and chains them via the
    /// DMA completion ISR. Calls cb (if non-null) when the last chunk is done.
    void StartDmaTransfer(const uint16_t* buf, uint32_t len,
                          DoneCallback cb, void* ctx);

    bool IsBusy() const { return dma_busy_; }

    /// Blocking fill — use only during startup or test.
    void FillScreen(uint16_t color);

private:
    void WriteCmd(uint8_t cmd);
    void WriteData(const uint8_t* data, size_t len);
    void SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void HardReset();

    // Maximum bytes per DMA chunk: must be ≤ 65535 and even (2 bytes/pixel).
    static constexpr uint32_t kMaxChunk = 65534u;

    // Display dimensions.
    static constexpr uint16_t kDisplayWidth  = 240u;
    static constexpr uint16_t kDisplayHeight = 320u;
    static constexpr uint16_t kMaxX          = kDisplayWidth  - 1u;  // 239
    static constexpr uint16_t kMaxY          = kDisplayHeight - 1u;  // 319

    // Called from DMA ISR after each chunk — chains the next chunk or finishes.
    static void DmaChunkDoneISR(void* ctx, daisy::SpiHandle::Result result);

    daisy::SpiHandle spi_;
    daisy::GPIO      pin_cs_;
    daisy::GPIO      pin_dc_;
    daisy::GPIO      pin_res_;
    daisy::GPIO      pin_blk_;

    // Chunked-transfer state (written before first chunk, read in ISR)
    const uint8_t*   dma_next_ptr_  = nullptr;
    uint32_t         dma_remaining_ = 0;
    DoneCallback     done_cb_       = nullptr;
    void*            done_ctx_      = nullptr;
    volatile bool    dma_busy_      = false;
};

} // namespace pedal
