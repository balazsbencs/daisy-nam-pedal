#include "Ui.h"
#include "sys/system.h"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace pedal;
using namespace daisy;

// ---------------------------------------------------------------------------
// Layout constants (portrait 240×320)
// ---------------------------------------------------------------------------
// Row Y positions
static constexpr uint16_t kRowHeader    =   4;   // preset index + bypass pill
static constexpr uint16_t kRowPresetBox =  30;   // outer rect top
static constexpr uint16_t kRowPresetTxt =  32;   // text inside box (Font_16x26)
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

void Ui::ShowTuner(const TunerState& s)
{
    // Only redraw when something visible changed (note, octave, lock state, or
    // the rounded cents the bar shows) — avoids needless pushes at refresh rate.
    bool changed = screen_ != Screen::Tuner
                || tuner_.stable != s.stable
                || tuner_.signal_present != s.signal_present
                || tuner_.octave != s.octave
                || std::strncmp(tuner_.note, s.note, sizeof(s.note)) != 0
                || std::lroundf(tuner_.cents) != std::lroundf(s.cents);
    tuner_  = s;
    screen_ = Screen::Tuner;
    if (changed)
        dirty_ = true;
}

// ---------------------------------------------------------------------------
// Update — call every main-loop iteration
// ---------------------------------------------------------------------------

void Ui::Update()
{
    // Push a batch of rows per call instead of one. A frame is 320 rows; at one
    // row per main-loop iteration the per-iteration overhead dominated the push.
    // This is safe now that the EQ/Vol encoders are sampled in the audio ISR —
    // blocking the main loop ~1 ms per batch no longer drops detents. Footswitch
    // / nav polling still happens between batches.
    static constexpr int kRowsPerUpdate = 16;
    for (int i = 0; i < kRowsPerUpdate && driver_.IsBusy(); ++i)
        driver_.Service();
    if (driver_.IsBusy())
        return;
    if (!dirty_)
        return;

    uint32_t now = System::GetNow();
    if (now - last_push_ms_ < kFrameMs)
        return;

    if (screen_ == Screen::Performance)
        RenderPerformance();
    else if (screen_ == Screen::Browse)
        RenderBrowse();
    else if (screen_ == Screen::Edit)
        RenderEdit();
    else
        RenderTuner();

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

    constexpr uint16_t kEqMax = 12;

    // Header: preset index, larger preset name, bypass pill.
    char idx_buf[8];
    snprintf(idx_buf, sizeof(idx_buf), "%02u/%02u",
             (unsigned)(perf_.preset_idx + 1), (unsigned)perf_.preset_count);
    DR::DrawText(kMargin, kRowHeader, idx_buf, kColorDim, kColorBlack, Font_7x10);

    const uint16_t pill_x = 240 - 56;
    const uint16_t pill_c = perf_.bypass ? kColorRed : kColorGreen;
    DR::FillRect(pill_x, kRowHeader, 50, 12, pill_c);
    DR::DrawText(pill_x + 4, kRowHeader + 1,
                 perf_.bypass ? "BYPASS" : "ACTIVE",
                 kColorBlack, pill_c, Font_7x10);

    const char* pname = perf_.preset_name ? perf_.preset_name : "---";
    size_t pname_len = 0; while (pname[pname_len]) pname_len++;
    uint16_t pname_w = static_cast<uint16_t>(pname_len * Font_7x10.FontWidth * 2u);
    uint16_t pname_x = (pname_w < 124u) ? static_cast<uint16_t>(58u + (124u - pname_w) / 2u) : 56u;
    DR::DrawTextScaled(pname_x, 16, pname, kColorWhite, kColorBlack, Font_7x10, 2);

    if (perf_.dirty)
        DR::DrawText(76, 38, "* EDITED", kColorYellow, kColorBlack, Font_7x10);

    // Large AMP block.
    DR::FillRoundRect(10, 40, 220, 82, 5, 0x1082u, kColorBlack);
    DR::HLine(10, 40, 220, kColorOrange);
    DR::HLine(10, 115, 220, kColorOrange);
    DR::VLine(10, 35, 80, kColorOrange);
    DR::VLine(229, 35, 80, kColorOrange);
    DR::DrawText(20, 53, "AMP", kColorDim, 0x1082u, Font_7x10);
    DR::DrawTextScaled(20, 80, perf_.model_name ? perf_.model_name : "---",
                       kColorWhite, 0x1082u, Font_7x10, 2);

    // Large CAB/IR block.
    DR::FillRoundRect(10, 126, 220, 82, 5, 0x1082u, kColorBlack);
    DR::HLine(10, 126, 220, kColorCyan);
    DR::HLine(10, 207, 220, kColorCyan);
    DR::VLine(10, 127, 80, kColorCyan);
    DR::VLine(229, 127, 80, kColorCyan);
    DR::DrawText(20, 145, "CAB", kColorDim, 0x1082u, Font_7x10);
    DR::DrawTextScaled(20, 172, perf_.ir_name ? perf_.ir_name : "Off",
                       kColorWhite, 0x1082u, Font_7x10, 2);

    // Compact meters, about 70% shorter than the old 182px channel strip.
    struct MeterDef { float val; bool bipolar; uint16_t color; const char* lbl; };
    MeterDef meters[5] = {
        { perf_.input_gain / 2.0f,              false, kColorCyan,  "GAIN" },
        { perf_.eq_bass    / (float)kEqMax,     true,  kColorGreen, "BASS" },
        { perf_.eq_mid     / (float)kEqMax,     true,  kColorGreen, "MID"  },
        { perf_.eq_treble  / (float)kEqMax,     true,  kColorGreen, "TRE"  },
        { perf_.output_vol,                     false, kColorCyan,  "VOL"  },
    };

    constexpr uint16_t kMW   = 26;
    constexpr uint16_t kMH   = 52;
    constexpr uint16_t kMY   = 225;
    constexpr uint16_t kMX0  = 25;
    constexpr uint16_t kMGap = 16;

    for (int i = 0; i < 5; ++i) {
        uint16_t mx = static_cast<uint16_t>(kMX0 + i * (kMW + kMGap));
        DR::VMeter(mx, kMY, kMW, kMH, meters[i].val, meters[i].bipolar, meters[i].color);

        uint16_t lbl_len = 0; while (meters[i].lbl[lbl_len]) lbl_len++;
        uint16_t lbl_w   = static_cast<uint16_t>(lbl_len * Font_7x10.FontWidth);
        uint16_t lbl_x   = static_cast<uint16_t>(mx + (kMW > lbl_w ? (kMW - lbl_w) / 2u : 0u));
        DR::DrawText(lbl_x, kMY + kMH + 6u, meters[i].lbl,
                     meters[i].color, kColorBlack, Font_7x10);
    }

    if (perf_.overload)
        DR::DrawText(kMargin, kRowHint, "! AUDIO OVERLOAD", kColorRed, kColorBlack, Font_7x10);
    else
        DR::DrawText(kMargin, kRowHint, "FS1:next  FS2:prev  SAVE:DFU",
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
                              "FS1:apply RAM  FS2:cancel",
                              kColorDim, kColorBlack, Font_7x10);
}

// ---------------------------------------------------------------------------
// RenderTuner — muted chromatic tuner
// ---------------------------------------------------------------------------

void Ui::RenderTuner()
{
    using DR = DisplayRenderer;
    DR::Clear(kColorBlack);

    auto centered_x = [](const char* s, const FontDef& f, uint8_t scale) -> uint16_t {
        size_t n = 0; while (s[n]) ++n;
        uint16_t w = static_cast<uint16_t>(n * f.FontWidth * scale);
        return (w < 240u) ? static_cast<uint16_t>((240u - w) / 2u) : 0u;
    };

    // Title + muted badge.
    DR::DrawTextScaled(centered_x("TUNER", Font_7x10, 2), 10, "TUNER",
                       kColorCyan, kColorBlack, Font_7x10, 2);
    DR::DrawText(centered_x("MUTED", Font_7x10, 1), 44, "MUTED",
                 kColorRed, kColorBlack, Font_7x10);

    // Large note (or "--" when no stable pitch).
    char big[6];
    if (tuner_.stable)
        snprintf(big, sizeof(big), "%s%d", tuner_.note, tuner_.octave);
    else
        snprintf(big, sizeof(big), "--");
    uint16_t note_fg = tuner_.stable ? kColorWhite : kColorDim;
    DR::DrawTextScaled(centered_x(big, Font_16x26, 2), 120, big,
                       note_fg, kColorBlack, Font_16x26, 2);

    // Cents bar from -50 .. +50.
    constexpr uint16_t kBarX = 20, kBarW = 200, kBarY = 235, kBarH = 18;
    const uint16_t     cx = kBarX + kBarW / 2;
    DR::FillRect(kBarX, kBarY, kBarW, kBarH, 0x1082u);            // track
    DR::VLine(cx, kBarY - 6, kBarH + 12, kColorWhite);           // center marker

    if (tuner_.stable)
    {
        float c = tuner_.cents;                                  // clamp to bar range
        if (c >  50.0f) c =  50.0f;
        if (c < -50.0f) c = -50.0f;
        int      off = static_cast<int>(c / 50.0f * (kBarW / 2));
        uint16_t ix  = static_cast<uint16_t>(static_cast<int>(cx) + off);
        uint16_t col = (std::fabs(c) < 5.0f) ? kColorGreen : kColorYellow;
        DR::FillRect(static_cast<uint16_t>(ix - 2), kBarY, 5, kBarH, col);
    }

    DR::DrawText(kBarX, kBarY + kBarH + 6, "-50", kColorDim, kColorBlack, Font_7x10);
    DR::DrawText(kBarX + kBarW - 21, kBarY + kBarH + 6, "+50", kColorDim, kColorBlack, Font_7x10);

    // Numeric readout.
    char info[24];
    if (tuner_.stable)
        snprintf(info, sizeof(info), "%.1f Hz  %+d c",
                 (double)tuner_.frequency_hz, (int)std::lroundf(tuner_.cents));
    else
        snprintf(info, sizeof(info), "no signal");
    DR::DrawText(centered_x(info, Font_7x10, 1), 285, info,
                 kColorDim, kColorBlack, Font_7x10);
}

// ---------------------------------------------------------------------------
// PushFrame — starts a cooperative row-by-row panel transfer
// ---------------------------------------------------------------------------

void Ui::PushFrame()
{
    driver_.PushFrame(DisplayRenderer::FrameBuffer(),
                      DisplayRenderer::FrameBufferBytes());
}
