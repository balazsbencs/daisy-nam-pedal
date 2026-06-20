#pragma once
#include "daisy_seed.h"
#include "util/oled_fonts.h"
#include "meter_fill.h"
#include <cstdint>
#include <cstddef>

namespace pedal {

// Frame buffer dimensions.
constexpr uint16_t kFbWidth  = 240;
constexpr uint16_t kFbHeight = 320;

/// Renders into a 240×320 RGB565 frame buffer stored in SDRAM.
/// All pixel values are written as big-endian (byte-swapped) so the SPI DMA
/// transfer delivers correct data to the ST7789 without any post-processing.
class DisplayRenderer {
public:
    /// Pointer to the SDRAM frame buffer (240*320 uint16_t).
    static uint16_t* FrameBuffer();

    static uint32_t FrameBufferBytes() { return kFbWidth * kFbHeight * 2u; }

    /// Fill the entire frame buffer with one color.
    static void Clear(uint16_t color);

    /// Filled rectangle.
    static void FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

    /// Horizontal line.
    static void HLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

    /// Vertical line.
    static void VLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

    /// Single character from a libDaisy 1-bit font.
    static void DrawChar(uint16_t x, uint16_t y, char ch,
                         uint16_t fg, uint16_t bg, const FontDef& font);

    /// Null-terminated string.
    static void DrawText(uint16_t x, uint16_t y, const char* str,
                         uint16_t fg, uint16_t bg, const FontDef& font);

    /// Null-terminated string drawn at pixel scale (e.g. scale=2 → 2×2 pixels per dot).
    static void DrawTextScaled(uint16_t x, uint16_t y, const char* str,
                               uint16_t fg, uint16_t bg,
                               const FontDef& font, uint8_t scale);

    /// Filled rounded rectangle. Pixels outside the corner arcs are painted bg.
    static void FillRoundRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               uint16_t r, uint16_t fg, uint16_t bg);

    /// Vertical bar meter. val is unipolar [0,1] or bipolar [-1,+1].
    static void VMeter(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       float val, bool bipolar, uint16_t color);

    /// Outlined bar with fill proportional to val [0, 1].
    static void DrawBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        float val, uint16_t color);

private:
    static void PutPixel(uint16_t x, uint16_t y, uint16_t color);
};

} // namespace pedal
