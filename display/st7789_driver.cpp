#include "st7789_driver.h"
#include "pin_map.h"

using namespace daisy;

namespace pedal {

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void St7789Driver::Init() {
    // GPIO — CS, DC, RES, BLK
    pin_cs_.Init(pins::DISP_CS,  GPIO::Mode::OUTPUT);
    pin_dc_.Init(pins::DISP_DC,  GPIO::Mode::OUTPUT);
    pin_res_.Init(pins::DISP_RES, GPIO::Mode::OUTPUT);
    pin_blk_.Init(pins::DISP_BLK, GPIO::Mode::OUTPUT);

    pin_cs_.Write(true);
    pin_blk_.Write(true);

    // SPI1 — mode 3 (CPOL=1, CPHA=1), TX-only, ~50 MHz
    SpiHandle::Config spi_cfg;
    spi_cfg.periph         = SpiHandle::Config::Peripheral::SPI_1;
    spi_cfg.mode           = SpiHandle::Config::Mode::MASTER;
    spi_cfg.direction      = SpiHandle::Config::Direction::TWO_LINES_TX_ONLY;
    spi_cfg.datasize       = 8;
    spi_cfg.clock_polarity = SpiHandle::Config::ClockPolarity::HIGH;
    spi_cfg.clock_phase    = SpiHandle::Config::ClockPhase::TWO_EDGE;
    spi_cfg.nss            = SpiHandle::Config::NSS::SOFT;
    spi_cfg.baud_prescaler = SpiHandle::Config::BaudPrescaler::PS_4;
    spi_cfg.pin_config.sclk = pins::DISP_SCK;
    spi_cfg.pin_config.mosi = pins::DISP_SDA;
    spi_cfg.pin_config.miso = Pin(PORTX, 0);
    spi_cfg.pin_config.nss  = Pin(PORTX, 0);
    spi_.Init(spi_cfg);

    HardReset();

    // ST7789 init sequence
    WriteCmd(0x01);  // SWRESET
    System::Delay(150);

    WriteCmd(0x11);  // SLPOUT
    System::Delay(500);

    {
        uint8_t d = 0x55;
        WriteCmd(0x3A);  // COLMOD — RGB565
        WriteData(&d, 1);
    }
    System::Delay(10);

    {
        uint8_t d = 0x00;
        WriteCmd(0x36);  // MADCTL — row/column order, no mirror
        WriteData(&d, 1);
    }

    WriteCmd(0x21);  // INVON — display inversion (required on most 2" modules)

    {
        uint8_t d[4] = {0x00, 0x00,
            static_cast<uint8_t>(kMaxX >> 8), static_cast<uint8_t>(kMaxX & 0xFF)};
        WriteCmd(0x2A);  // CASET — columns 0..kMaxX
        WriteData(d, 4);
    }

    {
        uint8_t d[4] = {0x00, 0x00,
            static_cast<uint8_t>(kMaxY >> 8), static_cast<uint8_t>(kMaxY & 0xFF)};
        WriteCmd(0x2B);  // RASET — rows 0..kMaxY
        WriteData(d, 4);
    }

    WriteCmd(0x13);  // NORON
    System::Delay(10);

    WriteCmd(0x29);  // DISPON
    System::Delay(100);
}

// ---------------------------------------------------------------------------
// HardReset
// ---------------------------------------------------------------------------

void St7789Driver::HardReset() {
    pin_res_.Write(true);
    System::Delay(10);
    pin_res_.Write(false);
    System::Delay(10);
    pin_res_.Write(true);
    System::Delay(120);
}

// ---------------------------------------------------------------------------
// WriteCmd / WriteData
// ---------------------------------------------------------------------------

void St7789Driver::WriteCmd(uint8_t cmd) {
    pin_cs_.Write(false);
    pin_dc_.Write(false);
    spi_.BlockingTransmit(&cmd, 1);
    pin_cs_.Write(true);
}

void St7789Driver::WriteData(const uint8_t* data, size_t len) {
    pin_cs_.Write(false);
    pin_dc_.Write(true);
    spi_.BlockingTransmit(const_cast<uint8_t*>(data), len);
    pin_cs_.Write(true);
}

// ---------------------------------------------------------------------------
// SetWindow
// ---------------------------------------------------------------------------

void St7789Driver::SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t col[4] = {
        static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1 & 0xFF),
    };
    WriteCmd(0x2A);
    WriteData(col, 4);

    uint8_t row[4] = {
        static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1 & 0xFF),
    };
    WriteCmd(0x2B);
    WriteData(row, 4);
}

// ---------------------------------------------------------------------------
// StartDmaTransfer — sets up chunked DMA and fires the first chunk
// ---------------------------------------------------------------------------

void St7789Driver::StartDmaTransfer(const uint16_t* buf, uint32_t len,
                                    DoneCallback cb, void* ctx) {
    if (!buf) return;

    done_cb_  = cb;
    done_ctx_ = ctx;
    dma_busy_ = true;

    SetWindow(0, 0, kMaxX, kMaxY);

    // RAMWR: command mode (DC low), send 0x2C, then stay in data mode (DC high)
    // CS stays asserted (low) until all chunks have been sent.
    pin_cs_.Write(false);
    pin_dc_.Write(false);
    uint8_t ramwr = 0x2C;
    spi_.BlockingTransmit(&ramwr, 1);
    pin_dc_.Write(true);

    // Send first chunk; continuation is handled in DmaChunkDoneISR.
    auto* ptr             = reinterpret_cast<uint8_t*>(const_cast<uint16_t*>(buf));
    const uint32_t chunk  = (len > kMaxChunk) ? kMaxChunk : len;
    dma_next_ptr_         = ptr + chunk;
    dma_remaining_        = len - chunk;

    spi_.DmaTransmit(ptr, chunk, nullptr, DmaChunkDoneISR, this);
}

// ---------------------------------------------------------------------------
// DmaChunkDoneISR — chains the next chunk or finalises the transfer
// ---------------------------------------------------------------------------

void St7789Driver::DmaChunkDoneISR(void* ctx, SpiHandle::Result /*result*/) {
    St7789Driver* self = static_cast<St7789Driver*>(ctx);

    if (self->dma_remaining_ > 0) {
        // More data to send — kick the next chunk from within the ISR.
        const uint32_t chunk  = (self->dma_remaining_ > kMaxChunk)
                                    ? kMaxChunk : self->dma_remaining_;
        auto* ptr             = const_cast<uint8_t*>(self->dma_next_ptr_);
        self->dma_next_ptr_  += chunk;
        self->dma_remaining_ -= chunk;

        self->spi_.DmaTransmit(ptr, chunk, nullptr, DmaChunkDoneISR, self);
    } else {
        // All chunks done — deassert CS, mark idle, then notify caller.
        self->pin_cs_.Write(true);
        self->dma_busy_ = false;
        if (self->done_cb_) {
            self->done_cb_(self->done_ctx_);
        }
    }
}

// ---------------------------------------------------------------------------
// FillScreen — blocking, startup / test use only
// ---------------------------------------------------------------------------

void St7789Driver::FillScreen(uint16_t color) {
    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);

    uint8_t line[kDisplayWidth * 2];
    for (int i = 0; i < kDisplayWidth * 2; i += 2) {
        line[i]     = hi;
        line[i + 1] = lo;
    }

    SetWindow(0, 0, kMaxX, kMaxY);
    WriteCmd(0x2C);

    pin_cs_.Write(false);
    pin_dc_.Write(true);
    for (int row = 0; row < kDisplayHeight; row++) {
        spi_.BlockingTransmit(line, static_cast<size_t>(sizeof(line)));
    }
    pin_cs_.Write(true);
}

} // namespace pedal
