#include "Ui.h"
#include "sys/system.h"
#include <cstdio>

using namespace pedal;
using namespace daisy;

// ---------------------------------------------------------------------------
// Layout constants (portrait 240×320)
// ---------------------------------------------------------------------------
// Row Y positions
static constexpr uint16_t kRowHeader    =   4;   // preset index + bypass pill
static constexpr uint16_t kRowPresetBox =  20;   // outer rect top
static constexpr uint16_t kRowPresetTxt =  28;   // text inside box (Font_16x26)
static constexpr uint16_t kRowAmpLbl    =  76;   // "AMP" label (Font_7x10)
static constexpr uint16_t kRowAmpName   =  88;   // model name
static constexpr uint16_t kRowIrLbl     = 112;   // "CAB" label
static constexpr uint16_t kRowIrName    = 124;   // IR name
static constexpr uint16_t kRowSep1      = 148;   // separator
static constexpr uint16_t kRowInLbl     = 156;   // "IN"
static constexpr uint16_t kRowInBar     = 154;   // input bar Y
static constexpr uint16_t kRowOutLbl    = 178;   // "OUT"
static constexpr uint16_t kRowOutBar    = 176;   // output bar Y
static constexpr uint16_t kRowHint      = 306;   // footer hint text

// Column X positions
static constexpr uint16_t kMargin       =   6;
static constexpr uint16_t kBarX         =  44;   // bars start after "OUT " label
static constexpr uint16_t kBarW         = 186;
static constexpr uint16_t kBarH         =  14;

// Browse screen
static constexpr uint16_t kBrowseTitleY =   4;
static constexpr uint16_t kBrowseSepY   =  18;
static constexpr uint16_t kBrowseRow0Y  =  24;   // first list row
static constexpr uint16_t kBrowseRowH   =  22;   // row pitch
static constexpr uint16_t kBrowseAccentW=   3;   // left accent bar width
static constexpr uint16_t kBrowseHintY  = 306;

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void Ui::Init()
{
    driver_.Init();
    DisplayRenderer::Clear(kColorBlack);
    driver_.FillScreen(kColorBlack);
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// State setters — mark dirty so Update() pushes a new frame
// ---------------------------------------------------------------------------

void Ui::ShowPerformance(const PerformanceState& s)
{
    perf_   = s;
    screen_ = Screen::Performance;
    dirty_  = true;
}

void Ui::ShowBrowse(const BrowseState& s)
{
    browse_ = s;
    screen_ = Screen::Browse;
    dirty_  = true;
}

void Ui::ShowEdit(const EditState& s)
{
    edit_   = s;
    screen_ = Screen::Edit;
    dirty_  = true;
}

// ---------------------------------------------------------------------------
// Update — call every main-loop iteration
// ---------------------------------------------------------------------------

void Ui::Update()
{
    if (!dirty_)
        return;
    if (driver_.IsBusy())
        return;

    uint32_t now = System::GetNow();
    if (now - last_push_ms_ < kFrameMs)
        return;

    if (screen_ == Screen::Performance)
        RenderPerformance();
    else if (screen_ == Screen::Browse)
        RenderBrowse();
    else
        RenderEdit();

    PushFrame();
    dirty_        = false;
    last_push_ms_ = now;
}

// ---------------------------------------------------------------------------
// RenderPerformance
// ---------------------------------------------------------------------------

void Ui::RenderPerformance()
{
    DisplayRenderer::Clear(kColorBlack);

    // --- Header: preset index + bypass pill ---------------------------------
    char idx_buf[16];
    snprintf(idx_buf, sizeof(idx_buf), "%02u / %02u",
             (unsigned)(perf_.preset_idx + 1), (unsigned)perf_.preset_count);
    DisplayRenderer::DrawText(kMargin, kRowHeader, idx_buf,
                              kColorDim, kColorBlack, Font_7x10);

    // Bypass pill (right-aligned, 50px wide)
    const uint16_t pill_x = 240 - 56;
    const uint16_t pill_c = perf_.bypass ? kColorRed : kColorGreen;
    DisplayRenderer::FillRect(pill_x, kRowHeader, 50, 12, pill_c);
    DisplayRenderer::DrawText(pill_x + 4, kRowHeader + 1,
                              perf_.bypass ? "BYPASS" : "ACTIVE",
                              kColorBlack, pill_c, Font_7x10);

    // --- Preset name box (Font_16x26) ---------------------------------------
    DisplayRenderer::FillRect(kMargin, kRowPresetBox,
                              240 - 2 * kMargin, 36, 0x1082 /* dark grey */);

    // Centre the name text (16px wide chars)
    const char* pname = perf_.preset_name ? perf_.preset_name : "---";
    size_t len = 0;
    while (pname[len]) len++;
    uint16_t txt_w = static_cast<uint16_t>(len * Font_16x26.FontWidth);
    uint16_t txt_x = (txt_w < 228) ? static_cast<uint16_t>((228 - txt_w) / 2 + kMargin) : kMargin;
    DisplayRenderer::DrawText(txt_x, kRowPresetTxt, pname,
                              kColorWhite, 0x1082, Font_16x26);

    // --- AMP section --------------------------------------------------------
    DisplayRenderer::DrawText(kMargin, kRowAmpLbl, "AMP",
                              kColorDim, kColorBlack, Font_7x10);
    DisplayRenderer::DrawText(kMargin, kRowAmpName,
                              perf_.model_name ? perf_.model_name : "---",
                              kColorWhite, kColorBlack, Font_7x10);

    // --- CAB section --------------------------------------------------------
    DisplayRenderer::DrawText(kMargin, kRowIrLbl, "CAB",
                              kColorDim, kColorBlack, Font_7x10);
    DisplayRenderer::DrawText(kMargin, kRowIrName,
                              perf_.ir_name ? perf_.ir_name : "Off",
                              kColorWhite, kColorBlack, Font_7x10);

    // --- Separator ----------------------------------------------------------
    DisplayRenderer::HLine(kMargin, kRowSep1, 240 - 2 * kMargin, kColorDim);

    // --- Level bars ---------------------------------------------------------
    DisplayRenderer::DrawText(kMargin, kRowInLbl + 2, "IN ",
                              kColorDim, kColorBlack, Font_7x10);
    DisplayRenderer::DrawBar(kBarX, kRowInBar, kBarW, kBarH,
                             perf_.input_gain, kColorCyan);

    DisplayRenderer::DrawText(kMargin, kRowOutLbl + 2, "OUT",
                              kColorDim, kColorBlack, Font_7x10);
    DisplayRenderer::DrawBar(kBarX, kRowOutBar, kBarW, kBarH,
                             perf_.output_vol, kColorCyan);

    // --- Footer hint --------------------------------------------------------
    DisplayRenderer::DrawText(kMargin, kRowHint,
                              "FS1: next   FS2: prev",
                              kColorDim, kColorBlack, Font_7x10);
}

// ---------------------------------------------------------------------------
// RenderBrowse
// ---------------------------------------------------------------------------

void Ui::RenderBrowse()
{
    DisplayRenderer::Clear(kColorBlack);

    // Header
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "%s  %u/%u",
             browse_.title ? browse_.title : "LIST",
             (unsigned)(browse_.cursor + 1),
             (unsigned)browse_.count);
    DisplayRenderer::DrawText(kMargin, kBrowseTitleY, hdr,
                              kColorWhite, kColorBlack, Font_7x10);
    DisplayRenderer::HLine(0, kBrowseSepY, 240, kColorDim);

    // List rows
    for (uint8_t row = 0; row < kBrowseRows; ++row)
    {
        uint8_t entry_idx = static_cast<uint8_t>(browse_.scroll_top + row);
        if (entry_idx >= browse_.count)
            break;

        uint16_t y    = static_cast<uint16_t>(kBrowseRow0Y + row * kBrowseRowH);
        bool selected = (entry_idx == browse_.cursor);

        if (selected)
        {
            // Accent bar on the left, full-row highlight
            DisplayRenderer::FillRect(0, y, 240, kBrowseRowH - 2, 0x0841 /* v.dark */);
            DisplayRenderer::FillRect(0, y, kBrowseAccentW, kBrowseRowH - 2, kColorGreen);
        }

        const char* name = (browse_.names && browse_.names[entry_idx])
                               ? browse_.names[entry_idx] : "---";
        uint16_t fg = selected ? kColorGreen : kColorWhite;
        DisplayRenderer::DrawText(kBrowseAccentW + 4, static_cast<uint16_t>(y + 5),
                                  name, fg, 0x0000, Font_7x10);
    }

    // Scrollbar indicator (thin right strip)
    if (browse_.count > kBrowseRows)
    {
        constexpr uint16_t sb_x     = 237;
        constexpr uint16_t sb_h_max = kBrowseRows * kBrowseRowH;
        uint16_t thumb_h = static_cast<uint16_t>(
            sb_h_max * kBrowseRows / browse_.count);
        uint16_t thumb_y = static_cast<uint16_t>(
            kBrowseRow0Y +
            sb_h_max * browse_.scroll_top / browse_.count);
        DisplayRenderer::VLine(sb_x, kBrowseRow0Y,
                               static_cast<uint16_t>(sb_h_max), kColorDim);
        DisplayRenderer::VLine(sb_x, thumb_y, thumb_h, kColorGreen);
    }

    // Footer
    DisplayRenderer::DrawText(kMargin, kBrowseHintY,
                              "Encoder: scroll   Click: select",
                              kColorDim, kColorBlack, Font_7x10);
}

// ---------------------------------------------------------------------------
// RenderEdit
// ---------------------------------------------------------------------------

void Ui::RenderEdit()
{
    DisplayRenderer::Clear(kColorBlack);

    // Header
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "EDIT  %s",
             edit_.preset_name ? edit_.preset_name : "---");
    DisplayRenderer::DrawText(kMargin, kBrowseTitleY, hdr,
                              kColorWhite, kColorBlack, Font_7x10);
    DisplayRenderer::HLine(0, kBrowseSepY, 240, kColorDim);

    // Field definitions: label text, value, Y for label row (value row = label + 12)
    struct FieldRow { const char* label; uint16_t y; };
    static constexpr FieldRow kFields[5] = {
        {"MODEL",   28},
        {"CAB",     68},
        {"IN GAIN", 108},
        {"OUT VOL", 148},
        {"BYPASS",  188},
    };
    static constexpr uint16_t kLabelH  = 10;
    static constexpr uint16_t kValOff  = 12; // value text Y offset below label
    static constexpr uint16_t kRowSpan = 26; // total height of one field block
    static constexpr uint16_t kAccentW = 3;

    for (uint8_t f = 0; f < 5; ++f)
    {
        uint16_t y      = kFields[f].y;
        bool     active = (f == edit_.field);
        bool     edmode = active && edit_.editing;

        // Determine accent color.
        uint16_t accent = active ? (edmode ? kColorCyan : kColorYellow) : 0;

        if (active)
        {
            // Row background highlight.
            DisplayRenderer::FillRect(0, y, 240, kRowSpan, 0x0841 /* v.dark */);
            // Left accent bar.
            DisplayRenderer::FillRect(0, y, kAccentW, kRowSpan, accent);
        }

        uint16_t label_fg = active ? accent : kColorDim;
        uint16_t val_fg   = active ? accent : kColorWhite;

        // Label
        DisplayRenderer::DrawText(kAccentW + 4, y, kFields[f].label,
                                  label_fg, 0x0000, Font_7x10);

        // Value text (built per field)
        char val[32] = {};
        switch (f)
        {
        case 0: // MODEL
        {
            const char* name = (edit_.model_count > 0 && edit_.model_names)
                ? edit_.model_names[edit_.model_idx] : "---";
            strncpy(val, name ? name : "---", sizeof(val) - 1);
            break;
        }
        case 1: // CAB
        {
            if (edit_.ir_idx == 0 || !edit_.ir_names)
                strncpy(val, "Off", sizeof(val) - 1);
            else
            {
                const char* name = edit_.ir_names[edit_.ir_idx];
                strncpy(val, name ? name : "Off", sizeof(val) - 1);
            }
            break;
        }
        case 2: // IN GAIN
            snprintf(val, sizeof(val), "%.2f", (double)edit_.input_gain);
            break;
        case 3: // OUT VOL
            snprintf(val, sizeof(val), "%.2f", (double)edit_.output_vol);
            break;
        case 4: // BYPASS
            strncpy(val, edit_.bypass ? "ON" : "OFF", sizeof(val) - 1);
            break;
        }

        uint16_t val_y  = static_cast<uint16_t>(y + kValOff);
        uint16_t val_bg = 0x0000;

        if (edmode)
        {
            // Filled background behind value while editing.
            DisplayRenderer::FillRect(kAccentW + 4, val_y, 230, kLabelH, 0x1082);
            val_bg = 0x1082;
        }
        DisplayRenderer::DrawText(kAccentW + 8, val_y, val, val_fg, val_bg, Font_7x10);
    }

    // Footer
    DisplayRenderer::DrawText(kMargin, kBrowseHintY,
                              "FS1:apply  FS2:cancel",
                              kColorDim, kColorBlack, Font_7x10);
}

// ---------------------------------------------------------------------------
// PushFrame — non-blocking DMA to the panel
// ---------------------------------------------------------------------------

void Ui::PushFrame()
{
    // Flush D-cache lines covering the framebuffer before SPI DMA reads them.
    // SDRAM is configured write-back cacheable (MPU Region 1); without this,
    // the DMA controller sees stale cache lines instead of the rendered pixels.
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(DisplayRenderer::FrameBuffer()),
                          static_cast<int32_t>(DisplayRenderer::FrameBufferBytes()));

    driver_.StartDmaTransfer(DisplayRenderer::FrameBuffer(),
                             DisplayRenderer::FrameBufferBytes(),
                             nullptr, nullptr);
}
