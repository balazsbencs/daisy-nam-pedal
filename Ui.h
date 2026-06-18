// Ui.h — display coordinator for the NAM pedal platform.
//
// Owns the St7789Driver and wraps DisplayRenderer. Renders into the SDRAM
// framebuffer and DMA-pushes to the panel at most kFps times per second.
//
// Three screens:
//   Performance — active during playing (preset name, amp, IR, level bars, bypass)
//   Browse      — preset list with a cursor (entered via encoder click)
//   Edit        — field editor for the highlighted preset (entered via encoder long-press)
//
// Call Update() every main-loop iteration; it only pushes a new frame when dirty
// AND the previous DMA transfer has finished AND the fps budget allows it.

#pragma once
#include "data_format.h"
#include "display/st7789_driver.h"
#include "display/display_renderer.h"
#include "display/display_colors.h"

namespace pedal { class St7789Driver; }

class Ui
{
public:
    enum class Screen { Performance, Browse, Edit };

    struct PerformanceState
    {
        const char* preset_name;   // current preset display name
        const char* model_name;    // current amp capture name
        const char* ir_name;       // current IR name (or "Off")
        float       input_gain;    // [0,2] — drives the input bar
        float       output_vol;    // [0,1] — drives the output bar
        bool        bypass;        // true = bypass pill shown
        uint8_t     preset_idx;    // shown as "01" etc.
        uint8_t     preset_count;
    };

    struct BrowseState
    {
        const char* title;
        const char* const* names;
        uint8_t     count;
        uint8_t     cursor;
        uint8_t     scroll_top;
    };

    // Working copy of a preset under edit plus enough context to render all fields.
    struct EditState
    {
        const char* preset_name;   // display name of the preset being edited

        // Field values (working copy — edited in RAM, never written to flash).
        char    model_name[NAM_DATA_NAME_LEN];
        char    ir_name[NAM_DATA_NAME_LEN];    // empty = "Off"
        float   input_gain;   // [0.0, 2.0]
        float   output_vol;   // [0.0, 1.0]
        bool    bypass;

        // Available options for MODEL and CAB fields.
        const char* const* model_names;  // [0..model_count-1]
        uint8_t            model_count;
        uint8_t            model_idx;    // currently selected model index

        // ir_names[0] = "Off"; ir_names[1..] = actual IR names
        const char* const* ir_names;
        uint8_t            ir_total;     // includes "Off" entry
        uint8_t            ir_idx;       // 0 = Off

        uint8_t  field;    // 0=MODEL 1=CAB 2=IN_GAIN 3=OUT_VOL 4=BYPASS
        bool     editing;  // true = value-edit mode; false = field-navigation mode
    };

    void Init();

    void ShowPerformance(const PerformanceState& s);
    void ShowBrowse(const BrowseState& s);
    void ShowEdit(const EditState& s);

    // Call every main-loop iteration.
    void Update();

private:
    static constexpr uint32_t kFps        = 30;
    static constexpr uint32_t kFrameMs    = 1000u / kFps;
    static constexpr uint8_t  kBrowseRows = 7;

    void RenderPerformance();
    void RenderBrowse();
    void RenderEdit();
    void PushFrame();

    pedal::St7789Driver driver_;

    Screen           screen_       = Screen::Performance;
    bool             dirty_        = true;
    uint32_t         last_push_ms_ = 0;

    PerformanceState perf_   = {};
    BrowseState      browse_ = {};
    EditState        edit_   = {};
};
