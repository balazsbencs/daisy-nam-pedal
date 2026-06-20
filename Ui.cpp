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
    using DR = DisplayRenderer;
    DR::Clear(kColorBlack);

    // --- Channel strip layout constants -------------------------------------
    constexpr uint16_t kMW    = 32;   // meter width
    constexpr uint16_t kMGap  =  6;   // gap between meters
    constexpr uint16_t kMX0   = 28;   // left edge of first meter (centres 5 bars in 240px)
    constexpr uint16_t kMY    = 70;   // meter top
    constexpr uint16_t kMH    = 182;  // meter height
    constexpr uint16_t kEqMax = 12;   // ±12 dB full-scale for EQ

    // --- Header: index + dirty + bypass pill --------------------------------
    char idx_buf[8];
    snprintf(idx_buf, sizeof(idx_buf), "%02u/%02u",
             (unsigned)(perf_.preset_idx + 1), (unsigned)perf_.preset_count);
    DR::DrawText(kMargin, kRowHeader, idx_buf, kColorDim, kColorBlack, Font_7x10);

    if (perf_.dirty)
        DR::DrawText(56, kRowHeader, "* EDITED", kColorYellow, kColorBlack, Font_7x10);

    const uint16_t pill_x = 240 - 56;
    const uint16_t pill_c = perf_.bypass ? kColorRed : kColorGreen;
    DR::FillRect(pill_x, kRowHeader, 50, 12, pill_c);
    DR::DrawText(pill_x + 4, kRowHeader + 1,
                 perf_.bypass ? "BYPASS" : "ACTIVE",
                 kColorBlack, pill_c, Font_7x10);

    // --- Preset name (2× scaled, centred) -----------------------------------
    const char* pname = perf_.preset_name ? perf_.preset_name : "---";
    size_t len = 0; while (pname[len]) len++;
    uint16_t nm_w  = static_cast<uint16_t>(len * Font_7x10.FontWidth * 2u);
    uint16_t nm_x  = (nm_w < 240u) ? static_cast<uint16_t>((240u - nm_w) / 2u) : 0u;
    DR::DrawTextScaled(nm_x, 20, pname, kColorWhite, kColorBlack, Font_7x10, 2);

    // --- AMP / CAB labels ---------------------------------------------------
    char amp_buf[NAM_DATA_NAME_LEN + 8];
    snprintf(amp_buf, sizeof(amp_buf), "AMP %s",
             perf_.model_name ? perf_.model_name : "---");
    DR::DrawText(kMargin, 42, amp_buf, kColorDim, kColorBlack, Font_7x10);

    char cab_buf[NAM_DATA_NAME_LEN + 8];
    snprintf(cab_buf, sizeof(cab_buf), "CAB %s",
             perf_.ir_name ? perf_.ir_name : "Off");
    DR::DrawText(kMargin, 54, cab_buf, kColorDim, kColorBlack, Font_7x10);

    // Separator
    DR::HLine(0, 66, 240, kColorDim);

    // --- 5 vertical meters --------------------------------------------------
    struct MeterDef { float val; bool bipolar; uint16_t color; const char* lbl; };
    MeterDef meters[5] = {
        { perf_.input_gain / 2.0f,              false, kColorCyan,   "GAIN" },
        { perf_.eq_bass    / (float)kEqMax,     true,  kColorGreen,  "BASS" },
        { perf_.eq_mid     / (float)kEqMax,     true,  kColorGreen,  "MID"  },
        { perf_.eq_treble  / (float)kEqMax,     true,  kColorGreen,  "TRE"  },
        { perf_.output_vol,                     false, kColorCyan,   "VOL"  },
    };

    for (int i = 0; i < 5; ++i) {
        uint16_t mx = static_cast<uint16_t>(kMX0 + i * (kMW + kMGap));
        DR::VMeter(mx, kMY, kMW, kMH, meters[i].val, meters[i].bipolar, meters[i].color);
        // Band label centred below the meter
        uint16_t lbl_len = 0; while (meters[i].lbl[lbl_len]) lbl_len++;
        uint16_t lbl_w   = static_cast<uint16_t>(lbl_len * Font_7x10.FontWidth);
        uint16_t lbl_x   = static_cast<uint16_t>(mx + (kMW - lbl_w) / 2u);
        DR::DrawText(lbl_x, kMY + kMH + 4u, meters[i].lbl,
                     meters[i].color, kColorBlack, Font_7x10);
    }

    // --- Footer -------------------------------------------------------------
    if (perf_.overload)
        DR::DrawText(kMargin, kRowHint, "! AUDIO OVERLOAD", kColorRed, kColorBlack, Font_7x10);
    else
        DR::DrawText(kMargin, kRowHint, "FS1:next/SAVE  FS2:prev/RVRT",
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

    // 8 fields, 34px pitch starting at Y=22.
    struct FieldRow { const char* label; };
    static constexpr FieldRow kFields[8] = {
        {"MODEL"},
        {"CAB"},
        {"IN GAIN"},
        {"OUT VOL"},
        {"BYPASS"},
        {"BASS FREQ"},
        {"MID FREQ"},
        {"TRE FREQ"},
    };
    static constexpr uint16_t kField0Y  = 22;
    static constexpr uint16_t kPitch    = 34;
    static constexpr uint16_t kValOff   = 12;
    static constexpr uint16_t kRowSpan  = kPitch - 2u;
    static constexpr uint16_t kAccentW  = 3;

    for (uint8_t f = 0; f < 8; ++f)
    {
        uint16_t y      = static_cast<uint16_t>(kField0Y + f * kPitch);
        bool     active = (f == edit_.field);
        bool     edmode = active && edit_.editing;

        uint16_t accent   = active ? (edmode ? kColorCyan : kColorYellow) : 0u;

        if (active)
        {
            DisplayRenderer::FillRect(0, y, 240, kRowSpan, 0x0841u);
            DisplayRenderer::FillRect(0, y, kAccentW, kRowSpan, accent);
        }

        uint16_t label_fg = active ? accent : kColorDim;
        uint16_t val_fg   = active ? accent : kColorWhite;

        DisplayRenderer::DrawText(kAccentW + 4, y, kFields[f].label,
                                  label_fg, 0x0000u, Font_7x10);

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
            else {
                const char* name = edit_.ir_names[edit_.ir_idx];
                strncpy(val, name ? name : "Off", sizeof(val) - 1);
            }
            break;
        }
        case 2: snprintf(val, sizeof(val), "%.2f", (double)edit_.input_gain); break;
        case 3: snprintf(val, sizeof(val), "%.2f", (double)edit_.output_vol); break;
        case 4: strncpy(val, edit_.bypass ? "ON" : "OFF", sizeof(val) - 1); break;
        case 5: snprintf(val, sizeof(val), "%.0f Hz", (double)edit_.eq_bass_freq);   break;
        case 6: snprintf(val, sizeof(val), "%.0f Hz", (double)edit_.eq_mid_freq);    break;
        case 7: snprintf(val, sizeof(val), "%.0f Hz", (double)edit_.eq_treble_freq); break;
        }

        uint16_t val_y  = static_cast<uint16_t>(y + kValOff);
        uint16_t val_bg = 0x0000u;

        if (edmode)
        {
            DisplayRenderer::FillRect(kAccentW + 4, val_y, 230, 10, 0x1082u);
            val_bg = 0x1082u;
        }
        DisplayRenderer::DrawText(kAccentW + 8, val_y, val, val_fg, val_bg, Font_7x10);
    }

    // Footer
    static constexpr uint16_t kEditHintY = 298;
    DisplayRenderer::DrawText(kMargin, kEditHintY,
                              "FS1:save  FS2:revert",
                              kColorDim, kColorBlack, Font_7x10);
}

// ---------------------------------------------------------------------------
// PushFrame — non-blocking DMA to the panel
// ---------------------------------------------------------------------------

void Ui::PushFrame()
{
    driver_.PushFrame(DisplayRenderer::FrameBuffer(),
                      DisplayRenderer::FrameBufferBytes());
}
