#include "display_renderer.h"
#include <cstring>

namespace pedal {

// ---------------------------------------------------------------------------
// SDRAM frame buffer — must be file-scope static for DSY_SDRAM_BSS.
// SDRAM is uncached, so DMA reads are coherent without a manual cache clean.
// ---------------------------------------------------------------------------
DSY_SDRAM_BSS static uint16_t s_frame_buf[kFbWidth * kFbHeight];

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline uint16_t FB(uint16_t color) {
    return __builtin_bswap16(color);
}

uint16_t* DisplayRenderer::FrameBuffer() {
    return s_frame_buf;
}

void DisplayRenderer::PutPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x < kFbWidth && y < kFbHeight) {
        s_frame_buf[static_cast<uint32_t>(y) * kFbWidth + x] = FB(color);
    }
}

// ---------------------------------------------------------------------------
// Public drawing functions
// ---------------------------------------------------------------------------

void DisplayRenderer::Clear(uint16_t color) {
    if (color == 0x0000 || color == 0xFFFF) {
        const uint8_t byte_val = (color == 0xFFFF) ? 0xFF : 0x00;
        memset(s_frame_buf, byte_val, kFbWidth * kFbHeight * sizeof(uint16_t));
        return;
    }
    const uint16_t fb_color = FB(color);
    const uint32_t total    = static_cast<uint32_t>(kFbWidth) * kFbHeight;
    for (uint32_t i = 0; i < total; ++i) {
        s_frame_buf[i] = fb_color;
    }
}

void DisplayRenderer::FillRect(uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h, uint16_t color) {
    const uint16_t fb_color = FB(color);
    const uint16_t y_end = static_cast<uint16_t>(y + h);
    for (uint16_t row = y; row < y_end; ++row) {
        uint16_t* line = &s_frame_buf[static_cast<uint32_t>(row) * kFbWidth + x];
        for (uint16_t col = 0; col < w; ++col) {
            line[col] = fb_color;
        }
    }
}

void DisplayRenderer::HLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color) {
    FillRect(x, y, len, 1, color);
}

void DisplayRenderer::VLine(uint16_t x, uint16_t y, uint16_t len, uint16_t color) {
    FillRect(x, y, 1, len, color);
}

void DisplayRenderer::DrawChar(uint16_t x, uint16_t y, char ch,
                                uint16_t fg, uint16_t bg, const FontDef& font) {
    if (ch < 0x20 || ch > 0x7E) {
        ch = '?';
    }
    const uint16_t* char_data = &font.data[static_cast<uint16_t>(ch - 0x20) * font.FontHeight];
    for (uint8_t row = 0; row < font.FontHeight; ++row) {
        const uint16_t bits = char_data[row];
        for (uint8_t col = 0; col < font.FontWidth; ++col) {
            const uint16_t color = ((bits << col) & 0x8000u) ? fg : bg;
            PutPixel(static_cast<uint16_t>(x + col),
                     static_cast<uint16_t>(y + row), color);
        }
    }
}

void DisplayRenderer::DrawText(uint16_t x, uint16_t y, const char* str,
                                uint16_t fg, uint16_t bg, const FontDef& font) {
    if (!str) { return; }
    uint16_t cx = x;
    while (*str) {
        DrawChar(cx, y, *str++, fg, bg, font);
        cx = static_cast<uint16_t>(cx + font.FontWidth);
    }
}

void DisplayRenderer::DrawBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               float val, uint16_t color) {
    if (val < 0.0f) { val = 0.0f; }
    if (val > 1.0f) { val = 1.0f; }

    HLine(x,         y,         w,     color);
    HLine(x,         y + h - 1, w,     color);
    VLine(x,         y + 1,     h - 2, color);
    VLine(x + w - 1, y + 1,     h - 2, color);

    const uint16_t inner_w = static_cast<uint16_t>(w > 2u ? w - 2u : 0u);
    const uint16_t inner_h = static_cast<uint16_t>(h > 2u ? h - 2u : 0u);
    if (inner_w == 0 || inner_h == 0) { return; }

    const uint16_t fill = static_cast<uint16_t>(val * static_cast<float>(inner_w));
    if (fill > 0) {
        FillRect(x + 1, y + 1, fill,           inner_h, color);
    }
    if (fill < inner_w) {
        FillRect(x + 1 + fill, y + 1, inner_w - fill, inner_h, 0x0000);
    }
}

} // namespace pedal
