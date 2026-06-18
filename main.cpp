// NamPlatform — Neural Amp Modeler pedal platform for Daisy Seed
//
// Signal chain:  in[L] → input_gain → NAM model → IR (FIR) → output_vol → out L+R
// Storage:       onboard QSPI flash (no SD card), data partition @ 0x90200000
// Controls:      FS1=next/apply  FS2=prev/cancel  ENC1=browse/edit

#include "daisy_seed.h"
#include "HardwareConfig.h"
#include "AudioEngine.h"
#include "Controls.h"
#include "IRLoader.h"
#include "ModelManager.h"
#include "PresetManager.h"
#include "QspiStorage.h"
#include "Ui.h"
#include <cstring>

using namespace daisy;

// ---------------------------------------------------------------------------
// Global module instances
// ---------------------------------------------------------------------------
static DaisySeed     daisy_seed;
static AudioEngine   audio_engine;
static QspiStorage   storage;
static ModelManager  models;
static PresetManager presets;
static Controls      controls;
static Ui            ui;

// ---------------------------------------------------------------------------
// UI mode flags (mutually exclusive)
// ---------------------------------------------------------------------------
static bool    browsing      = false;
static bool    editing       = false;
static uint8_t browse_cursor = 0;

// CPU diagnostics
static volatile uint32_t cb_count   = 0;
static volatile uint32_t cb_max_cyc = 0;

// ---------------------------------------------------------------------------
// Name pointer arrays built at boot
// ---------------------------------------------------------------------------
static const char* model_name_ptrs[ModelManager::kMaxModels]   = {};
static const char* preset_name_ptrs[PresetManager::kMaxPresets] = {};

// IR name list: [0] = "Off", [1..ir_count] = actual IR entry names
static constexpr uint8_t    kMaxIrs = 32;
static const NamDataEntry*  ir_entries[kMaxIrs]  = {};
static const char*          ir_name_ptrs[kMaxIrs + 1] = {}; // +1 for "Off"
static uint8_t              ir_count = 0;

// ---------------------------------------------------------------------------
// Edit screen working state
// ---------------------------------------------------------------------------
static Ui::EditState edit_state = {};
static uint8_t       edit_preset_idx = 0; // which preset is being edited

// ---------------------------------------------------------------------------
// Audio callback — runs every 1 ms, must be deterministic
// ---------------------------------------------------------------------------
static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t size)
{
    __set_FPSCR(__get_FPSCR() | (1U << 24) | (1U << 25)); // FZ | DN

    cb_count++;
    uint32_t t0 = DWT->CYCCNT;

    size_t frames = size / 2;

    float mono_in[hw::AUDIO_BLOCK_SIZE];
    float mono_out[hw::AUDIO_BLOCK_SIZE];
    for (size_t i = 0; i < frames; ++i)
        mono_in[i] = in[i * 2];

    audio_engine.Process(mono_in, mono_out, frames);

    for (size_t i = 0; i < frames; ++i)
        out[i * 2] = out[i * 2 + 1] = mono_out[i];

    uint32_t cyc = DWT->CYCCNT - t0;
    if (cyc > cb_max_cyc) cb_max_cyc = cyc;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void BuildNamePointers()
{
    for (uint8_t i = 0; i < models.Count();  ++i) model_name_ptrs[i]  = models.Name(i);
    for (uint8_t i = 0; i < presets.Count(); ++i) preset_name_ptrs[i] = presets.Name(i);

    // IR list: index 0 = "Off", 1..n = QSPI IR entries.
    ir_name_ptrs[0] = "Off";
    ir_count = 0;
    uint16_t total = storage.EntryCount();
    for (uint16_t i = 0; i < total && ir_count < kMaxIrs; ++i)
    {
        const NamDataEntry* e = storage.GetEntry(i);
        if (e && static_cast<NamEntryType>(e->type) == NAM_ENTRY_IR)
        {
            ir_entries[ir_count]       = e;
            ir_name_ptrs[ir_count + 1] = e->name;
            ir_count++;
        }
    }
}

static void PushPerformanceScreen()
{
    const NamPreset& p = presets.ActivePreset();
    Ui::PerformanceState s;
    s.preset_name  = presets.Name(presets.Current());
    s.model_name   = p.model_name[0] ? p.model_name : "---";
    s.ir_name      = p.ir_name[0]    ? p.ir_name    : "Off";
    s.input_gain   = audio_engine.GetInputGain();
    s.output_vol   = audio_engine.GetOutputVol();
    s.bypass       = audio_engine.GetBypass();
    s.preset_idx   = presets.Current();
    s.preset_count = presets.Count();
    ui.ShowPerformance(s);
}

static void PushBrowseScreen()
{
    Ui::BrowseState s;
    s.title      = "PRESETS";
    s.names      = preset_name_ptrs;
    s.count      = presets.Count();
    s.cursor     = browse_cursor;
    s.scroll_top = (browse_cursor >= 7u) ? (browse_cursor - 6u) : 0u;
    ui.ShowBrowse(s);
}

static void PushEditScreen(uint8_t preset_idx)
{
    edit_preset_idx = preset_idx;
    const NamPreset& p = presets.EditablePreset(preset_idx);

    strncpy(edit_state.model_name, p.model_name, NAM_DATA_NAME_LEN - 1);
    edit_state.model_name[NAM_DATA_NAME_LEN - 1] = '\0';
    strncpy(edit_state.ir_name,    p.ir_name,    NAM_DATA_NAME_LEN - 1);
    edit_state.ir_name[NAM_DATA_NAME_LEN - 1] = '\0';
    edit_state.input_gain  = p.input_gain;
    edit_state.output_vol  = p.output_volume;
    edit_state.bypass      = p.bypass != 0;

    edit_state.preset_name = presets.Name(preset_idx);

    // Resolve model_idx from model_name.
    edit_state.model_names = model_name_ptrs;
    edit_state.model_count = models.Count();
    edit_state.model_idx   = 0;
    for (uint8_t m = 0; m < models.Count(); ++m)
    {
        if (models.Name(m) &&
            strncmp(models.Name(m), p.model_name, NAM_DATA_NAME_LEN) == 0)
        {
            edit_state.model_idx = m;
            break;
        }
    }

    // Resolve ir_idx from ir_name (0 = "Off").
    edit_state.ir_names = ir_name_ptrs;
    edit_state.ir_total = ir_count + 1; // includes "Off" at [0]
    edit_state.ir_idx   = 0;
    if (p.ir_name[0] != '\0')
    {
        for (uint8_t r = 0; r < ir_count; ++r)
        {
            if (ir_entries[r] &&
                strncmp(ir_entries[r]->name, p.ir_name, NAM_DATA_NAME_LEN) == 0)
            {
                edit_state.ir_idx = r + 1; // +1 because "Off" is at 0
                break;
            }
        }
    }

    edit_state.field   = 0;
    edit_state.editing = false;

    ui.ShowEdit(edit_state);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    // --- Hardware init -------------------------------------------------------
    daisy_seed.Init();

    // FPU Flush-to-Zero / Default-NaN for all contexts (including ISRs).
    uint32_t fpscr = __get_FPSCR();
    fpscr |= (1U << 24) | (1U << 25);
    __set_FPSCR(fpscr);
    *reinterpret_cast<volatile uint32_t*>(0xE000EF3Cu) |= (1U << 24) | (1U << 25);

    // Non-blocking serial — board boots standalone without a PC attached.
    daisy_seed.StartLog(false);
    daisy_seed.PrintLine("NamPlatform booting...");

    // DWT cycle counter for CPU profiling.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    // --- QSPI storage --------------------------------------------------------
    daisy_seed.PrintLine("Init storage...");
    if (storage.Init() != QspiStorage::Status::OK)
        daisy_seed.PrintLine("WARNING: data partition missing — flash a data image first.");
    storage.PrintDirectory(daisy_seed);

    // --- Audio engine --------------------------------------------------------
    audio_engine.Init(hw::AUDIO_BLOCK_SIZE, hw::AUDIO_SAMPLE_RATE);

    // --- Managers ------------------------------------------------------------
    daisy_seed.PrintLine("Init models...");
    models.Init(storage);
    daisy_seed.PrintLine("  %u model(s) found", (unsigned)models.Count());

    daisy_seed.PrintLine("Init presets...");
    presets.Init(storage, models);
    daisy_seed.PrintLine("  %u preset(s)", (unsigned)presets.Count());
    BuildNamePointers();

    // Load the first preset on boot.
    if (presets.Count() > 0)
    {
        daisy_seed.PrintLine("Loading preset 0...");
        presets.Apply(audio_engine, storage, models,
                      hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
    }

    // --- Controls ------------------------------------------------------------
    controls.Init();

    // --- Display -------------------------------------------------------------
    daisy_seed.PrintLine("Init display...");
    ui.Init();
    PushPerformanceScreen();

    // --- Audio ---------------------------------------------------------------
    daisy_seed.SetAudioBlockSize(hw::AUDIO_BLOCK_SIZE);
    daisy_seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    daisy_seed.StartAudio(AudioCallback);
    daisy_seed.PrintLine("Audio started.");

    // --- Main loop -----------------------------------------------------------
    uint32_t last_diag_ms = System::GetNow();

    for (;;)
    {
        ControlEvent ev = controls.Process();

        if (!browsing && !editing)
        {
            // --- Performance mode -------------------------------------------
            if (ev.next_preset && presets.Count() > 1)
            {
                presets.Next();
                presets.Apply(audio_engine, storage, models,
                              hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                PushPerformanceScreen();
                daisy_seed.PrintLine("Preset -> %u: %s", (unsigned)presets.Current(),
                             presets.Name(presets.Current()));
            }
            else if (ev.prev_preset && presets.Count() > 1)
            {
                presets.Prev();
                presets.Apply(audio_engine, storage, models,
                              hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                PushPerformanceScreen();
                daisy_seed.PrintLine("Preset -> %u: %s", (unsigned)presets.Current(),
                             presets.Name(presets.Current()));
            }

            // Encoder click → enter Browse screen.
            if (ev.enc1_click)
            {
                browse_cursor = presets.Current();
                browsing      = true;
                PushBrowseScreen();
            }

            // Encoder turn → adjust output volume in 5 % steps.
            if (ev.enc1_delta != 0)
            {
                float v = audio_engine.GetOutputVol() + ev.enc1_delta * 0.05f;
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                audio_engine.SetOutputVol(v);
                PushPerformanceScreen();
            }
        }
        else if (browsing)
        {
            // --- Browse mode ------------------------------------------------
            if (ev.enc1_delta != 0)
            {
                int16_t next = static_cast<int16_t>(browse_cursor) + ev.enc1_delta;
                if (next < 0) next = 0;
                if (next >= presets.Count()) next = presets.Count() - 1;
                browse_cursor = static_cast<uint8_t>(next);
                PushBrowseScreen();
            }

            // Click: confirm selection and exit Browse.
            if (ev.enc1_click)
            {
                if (browse_cursor != presets.Current())
                {
                    while (presets.Current() != browse_cursor)
                    {
                        if (browse_cursor > presets.Current()) presets.Next();
                        else                                   presets.Prev();
                    }
                    presets.Apply(audio_engine, storage, models,
                                  hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                    daisy_seed.PrintLine("Preset -> %u: %s", (unsigned)presets.Current(),
                                 presets.Name(presets.Current()));
                }
                browsing = false;
                PushPerformanceScreen();
            }

            // Long-press: enter Edit screen for the highlighted preset.
            if (ev.enc1_long)
            {
                browsing = false;
                editing  = true;
                PushEditScreen(browse_cursor);
            }

            // FS1/FS2: cancel browse without applying.
            if (ev.next_preset || ev.prev_preset)
            {
                browsing = false;
                PushPerformanceScreen();
            }
        }
        else // editing
        {
            // --- Edit mode --------------------------------------------------
            if (!edit_state.editing)
            {
                // Level 1: navigate between fields.
                if (ev.enc1_delta != 0)
                {
                    int8_t f = static_cast<int8_t>(edit_state.field) + ev.enc1_delta;
                    if (f < 0) f = 0;
                    if (f > 4) f = 4;
                    edit_state.field = static_cast<uint8_t>(f);
                    ui.ShowEdit(edit_state);
                }
                // Click: enter value-edit mode for the active field.
                if (ev.enc1_click)
                {
                    edit_state.editing = true;
                    ui.ShowEdit(edit_state);
                }
            }
            else
            {
                // Level 2: change the active field's value.
                if (ev.enc1_delta != 0)
                {
                    switch (edit_state.field)
                    {
                    case 0: // MODEL
                    {
                        int16_t idx = static_cast<int16_t>(edit_state.model_idx) + ev.enc1_delta;
                        if (idx < 0) idx = 0;
                        if (idx >= edit_state.model_count) idx = edit_state.model_count - 1;
                        edit_state.model_idx = static_cast<uint8_t>(idx);
                        if (edit_state.model_names && edit_state.model_names[edit_state.model_idx])
                            strncpy(edit_state.model_name,
                                    edit_state.model_names[edit_state.model_idx],
                                    NAM_DATA_NAME_LEN - 1);
                        break;
                    }
                    case 1: // CAB
                    {
                        int16_t idx = static_cast<int16_t>(edit_state.ir_idx) + ev.enc1_delta;
                        if (idx < 0) idx = 0;
                        if (idx >= edit_state.ir_total) idx = edit_state.ir_total - 1;
                        edit_state.ir_idx = static_cast<uint8_t>(idx);
                        if (edit_state.ir_idx == 0)
                            edit_state.ir_name[0] = '\0';
                        else if (edit_state.ir_names && edit_state.ir_names[edit_state.ir_idx])
                            strncpy(edit_state.ir_name,
                                    edit_state.ir_names[edit_state.ir_idx],
                                    NAM_DATA_NAME_LEN - 1);
                        break;
                    }
                    case 2: // IN GAIN  [0.0, 2.0] step 0.05
                    {
                        float v = edit_state.input_gain + ev.enc1_delta * 0.05f;
                        edit_state.input_gain = (v < 0.0f) ? 0.0f : (v > 2.0f) ? 2.0f : v;
                        break;
                    }
                    case 3: // OUT VOL  [0.0, 1.0] step 0.05
                    {
                        float v = edit_state.output_vol + ev.enc1_delta * 0.05f;
                        edit_state.output_vol = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
                        break;
                    }
                    case 4: // BYPASS — toggle on any turn
                        edit_state.bypass = !edit_state.bypass;
                        break;
                    }
                    ui.ShowEdit(edit_state);
                }
                // Click: confirm field, return to field navigation.
                if (ev.enc1_click)
                {
                    edit_state.editing = false;
                    ui.ShowEdit(edit_state);
                }
            }

            // FS1: apply edits → write back to in-RAM preset, load into engine.
            if (ev.next_preset)
            {
                NamPreset& p = presets.EditablePreset(edit_preset_idx);
                strncpy(p.model_name,  edit_state.model_name, NAM_DATA_NAME_LEN - 1);
                p.model_name[NAM_DATA_NAME_LEN - 1] = '\0';
                strncpy(p.ir_name,     edit_state.ir_name,    NAM_DATA_NAME_LEN - 1);
                p.ir_name[NAM_DATA_NAME_LEN - 1] = '\0';
                p.input_gain    = edit_state.input_gain;
                p.output_volume = edit_state.output_vol;
                p.bypass        = edit_state.bypass ? 1 : 0;

                // Navigate current preset index to the edited preset.
                while (presets.Current() != edit_preset_idx)
                {
                    if (edit_preset_idx > presets.Current()) presets.Next();
                    else                                      presets.Prev();
                }
                presets.ApplyPreset(p, audio_engine, storage, models,
                                    hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                daisy_seed.PrintLine("Edit applied -> preset %u: %s",
                             (unsigned)edit_preset_idx,
                             presets.Name(edit_preset_idx));
                editing = false;
                PushPerformanceScreen();
            }

            // FS2: cancel, discard all edits.
            if (ev.prev_preset)
            {
                editing = false;
                PushPerformanceScreen();
            }
        }

        // Display update (fps-capped, non-blocking DMA).
        ui.Update();

        // Once-per-second diagnostics.
        uint32_t now = System::GetNow();
        if (now - last_diag_ms >= 1000)
        {
            float cb_ms = static_cast<float>(cb_max_cyc) / 480000.0f;
            daisy_seed.PrintLine("cb=%lu  peak=%.3fms (budget=1.00ms)  preset=%u  bypass=%s",
                (unsigned long)cb_count, cb_ms,
                (unsigned)presets.Current(),
                audio_engine.GetBypass() ? "Y" : "N");
            cb_max_cyc   = 0;
            last_diag_ms = now;
        }
    }
}
