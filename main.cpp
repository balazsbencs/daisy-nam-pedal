// NamPlatform — Neural Amp Modeler pedal platform for Daisy Seed
//
// Signal chain:  in[L] → input_gain → NAM model → IR (FIR) → EQ → output_vol → out L+R
// Storage:       onboard QSPI flash (no SD card), data partition @ 0x90200000
// Controls:      ENC1=Gain  ENC2=Bass  ENC3=Mid  ENC4=Treble  ENC5=Vol
//                FS1 tap=next preset  FS1 hold=save
//                FS2 tap=prev preset  FS2 hold=revert

#include "daisy_seed.h"
#include "HardwareConfig.h"
#include "AudioEngine.h"
#include "Controls.h"
#include "Eq3.h"
#include "IRLoader.h"
#include "ModelManager.h"
#include "PresetManager.h"
#include "BootloaderCommand.h"
#include "QspiStorage.h"
#include "Ui.h"
#include "ui_mode.h"
#include "hid/usb.h"
#include "sys/system.h"
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
static UsbHandle     usb_cdc;

static BootloaderCommandParser bootloader_command_parser;
static volatile bool            bootloader_requested = false;

// ---------------------------------------------------------------------------
// UI mode flags (mutually exclusive)
// ---------------------------------------------------------------------------
static bool    browsing      = false;
static bool    editing       = false;
static uint8_t browse_cursor = 0;
static bool    preset_dirty  = false; // unsaved live-edit changes

// CPU diagnostics
static volatile uint32_t cb_count   = 0;
static volatile uint32_t cb_max_cyc = 0;
static volatile float    diag_input_peak = 0.0f;
static volatile float    diag_output_peak = 0.0f;
static volatile float    diag_diff_peak = 0.0f;

// Audio diagnostics:
// 0 = full pedal DSP, 1 = input passthrough, 2 = generated beep pattern,
// 3 = process one full DSP block, then silence (measures an over-budget block).
static constexpr uint8_t kAudioDiagMode = 0;
static constexpr bool    kDisplayEnabled = true;
static constexpr bool    kDisableIrForTiming = false;

static void UsbReceiveCallback(uint8_t* buffer, uint32_t* length)
{
    if (buffer == nullptr || length == nullptr)
        return;

    if (bootloader_command_parser.Feed(buffer, *length) == BootloaderCommand::EnterBootloader)
        bootloader_requested = true;
}

static constexpr uint32_t kCbBudgetCyc   = 480000;
static constexpr uint32_t kCbOverloadCyc = (kCbBudgetCyc * 9) / 10;
static bool audio_overload = false;

// ---------------------------------------------------------------------------
// Encoder step sizes (performance-mode live edit)
// ---------------------------------------------------------------------------
static constexpr float kGainStep          = 0.10f;   // input gain per click
static constexpr float kVolStep           = 0.10f;   // output vol per click
static constexpr float kEqDbStep          = 1.0f;    // EQ gain dB per click
static constexpr float kBassFreqStepHz    = 20.0f;
static constexpr float kMidFreqStepHz     = 50.0f;
static constexpr float kTrebleFreqStepHz  = 100.0f;
static constexpr float kEqDbMin           = -12.0f;
static constexpr float kEqDbMax           =  12.0f;

// ---------------------------------------------------------------------------
// Name pointer arrays built at boot
// ---------------------------------------------------------------------------
static const char* model_name_ptrs[ModelManager::kMaxModels]    = {};
static const char* preset_name_ptrs[PresetManager::kMaxPresets] = {};

static constexpr uint8_t    kMaxIrs = 32;
static const NamDataEntry*  ir_entries[kMaxIrs]      = {};
static const char*          ir_name_ptrs[kMaxIrs + 1] = {}; // [0] = "Off"
static uint8_t              ir_count = 0;

// ---------------------------------------------------------------------------
// Edit screen working state
// ---------------------------------------------------------------------------
static Ui::EditState edit_state     = {};
static uint8_t       edit_preset_idx = 0;

// ---------------------------------------------------------------------------
// Audio callback — runs every 1 ms, must be deterministic
// ---------------------------------------------------------------------------
static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    frames)
{
    cb_count++;
    uint32_t t0 = DWT->CYCCNT;
    static float tone_phase = 0.0f;
    static uint32_t diag_frame = 0;

    float mono_in[hw::AUDIO_BLOCK_SIZE];
    float mono_out[hw::AUDIO_BLOCK_SIZE];
    for (size_t i = 0; i < frames; ++i)
    {
        mono_in[i] = in[0][i];
        float magnitude = mono_in[i] < 0.0f ? -mono_in[i] : mono_in[i];
        if(magnitude > diag_input_peak)
            diag_input_peak = magnitude;
    }

    if constexpr (kAudioDiagMode == 2)
    {
        for (size_t i = 0; i < frames; ++i)
        {
            // Repeating 3-second signature: 440 Hz, silence, 880 Hz, silence.
            const bool  first_beep  = diag_frame < 48000u;
            const bool  second_beep = diag_frame >= 72000u && diag_frame < 120000u;
            const float frequency   = second_beep ? 880.0f : 440.0f;
            const float phase_inc   = frequency / hw::AUDIO_SAMPLE_RATE;
            tone_phase += phase_inc;
            if (tone_phase >= 1.0f)
                tone_phase -= 1.0f;
            mono_out[i] = (first_beep || second_beep)
                              ? ((tone_phase < 0.5f) ? 0.75f : -0.75f)
                              : 0.0f;
            if(++diag_frame >= 144000u)
                diag_frame = 0;
        }
    }
    else if constexpr (kAudioDiagMode == 1)
    {
        for (size_t i = 0; i < frames; ++i)
            mono_out[i] = mono_in[i];
    }
    else if constexpr (kAudioDiagMode == 3)
    {
        static bool probe_complete = false;
        if(!probe_complete)
        {
            audio_engine.Process(mono_in, mono_out, frames);
            probe_complete = true;
        }
        else
        {
            for (size_t i = 0; i < frames; ++i)
                mono_out[i] = 0.0f;
        }
    }
    else
    {
        audio_engine.Process(mono_in, mono_out, frames);
    }

    for (size_t i = 0; i < frames; ++i)
    {
        float output_magnitude = mono_out[i] < 0.0f ? -mono_out[i] : mono_out[i];
        float diff = mono_out[i] - mono_in[i];
        float diff_magnitude = diff < 0.0f ? -diff : diff;
        if(output_magnitude > diag_output_peak)
            diag_output_peak = output_magnitude;
        if(diff_magnitude > diag_diff_peak)
            diag_diff_peak = diff_magnitude;
        out[0][i] = out[1][i] = mono_out[i];
    }

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
    if constexpr (!kDisplayEnabled)
        return;

    const NamPreset& p = presets.ActivePreset();
    Ui::PerformanceState s;
    s.preset_name  = presets.Name(presets.Current());
    s.model_name   = p.model_name[0] ? p.model_name : "---";
    s.ir_name      = p.ir_name[0]    ? p.ir_name    : "Off";
    s.input_gain   = audio_engine.GetInputGain();
    s.output_vol   = audio_engine.GetOutputVol();
    s.eq_bass      = audio_engine.GetEqGain(Eq3::Band::Bass);
    s.eq_mid       = audio_engine.GetEqGain(Eq3::Band::Mid);
    s.eq_treble    = audio_engine.GetEqGain(Eq3::Band::Treble);
    s.bypass       = audio_engine.GetBypass();
    s.dirty        = preset_dirty;
    s.overload     = audio_overload;
    s.preset_idx   = presets.Current();
    s.preset_count = presets.Count();
    ui.ShowPerformance(s);
}

static void PushBrowseScreen()
{
    if constexpr (!kDisplayEnabled)
        return;

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
    edit_state.input_gain      = p.input_gain;
    edit_state.output_vol      = p.output_volume;
    edit_state.bypass          = p.bypass != 0;
    edit_state.eq_bass_freq    = p.eq_bass_freq   > 0.0f ? p.eq_bass_freq   : 100.0f;
    edit_state.eq_mid_freq     = p.eq_mid_freq    > 0.0f ? p.eq_mid_freq    : 750.0f;
    edit_state.eq_treble_freq  = p.eq_treble_freq > 0.0f ? p.eq_treble_freq : 4000.0f;

    edit_state.preset_name = presets.Name(preset_idx);

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

    edit_state.ir_names = ir_name_ptrs;
    edit_state.ir_total = ir_count + 1;
    edit_state.ir_idx   = 0;
    if (p.ir_name[0] != '\0')
    {
        for (uint8_t r = 0; r < ir_count; ++r)
        {
            if (ir_entries[r] &&
                strncmp(ir_entries[r]->name, p.ir_name, NAM_DATA_NAME_LEN) == 0)
            {
                edit_state.ir_idx = r + 1;
                break;
            }
        }
    }

    edit_state.field   = 0;
    edit_state.editing = false;
    if constexpr (kDisplayEnabled)
        ui.ShowEdit(edit_state);
}

// Write active preset to QSPI flash (XIP-safe: stops audio around erase/program).
static void SaveActivePreset()
{
    uint8_t              cur   = presets.Current();
    const NamDataEntry*  entry = presets.Entry(cur);
    if (!entry) return; // synthesised preset — no flash slot

    NamPreset& p     = presets.EditablePreset(cur);
    p.input_gain     = audio_engine.GetInputGain();
    p.output_volume  = audio_engine.GetOutputVol();
    p.bypass         = audio_engine.GetBypass() ? 1u : 0u;
    p.eq_bass_gain   = audio_engine.GetEqGain(Eq3::Band::Bass);
    p.eq_mid_gain    = audio_engine.GetEqGain(Eq3::Band::Mid);
    p.eq_treble_gain = audio_engine.GetEqGain(Eq3::Band::Treble);

    daisy_seed.StopAudio();
    __disable_irq();
    storage.WritePreset(entry, p);
    __enable_irq();
    daisy_seed.StartAudio(AudioCallback);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    // --- Hardware init -------------------------------------------------------
    daisy_seed.Init(true); // 480 MHz boost; full NAM DSP does not fit at 400 MHz.

    uint32_t fpscr = __get_FPSCR();
    fpscr |= (1U << 24) | (1U << 25);
    __set_FPSCR(fpscr);
    *reinterpret_cast<volatile uint32_t*>(0xE000EF3Cu) |= (1U << 24) | (1U << 25);

    daisy_seed.StartLog(false);
    usb_cdc.SetReceiveCallback(UsbReceiveCallback, UsbHandle::FS_INTERNAL);
    daisy_seed.PrintLine("NamPlatform booting...");
    // DIAGNOSTIC: 0=DAISY_SEED(AK4556) 1=DAISY_SEED_1_1(WM8731) 2=SEED_2_DFM
    daisy_seed.PrintLine("Board version: %d", (int)daisy_seed.CheckBoardVersion());

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    // --- QSPI storage --------------------------------------------------------
    daisy_seed.PrintLine("Init storage...");
    if (storage.Init() != QspiStorage::Status::OK)
        daisy_seed.PrintLine("WARNING: data partition missing — flash a data image first.");
    daisy_seed.PrintLine("Storage ready: %u entries.",
                         (unsigned)storage.EntryCount());
    System::Delay(200);

    // --- Audio engine --------------------------------------------------------
    audio_engine.Init(hw::AUDIO_BLOCK_SIZE, hw::AUDIO_SAMPLE_RATE);

    // --- Managers ------------------------------------------------------------
    daisy_seed.PrintLine("Init models...");
    System::Delay(200);
    models.Init(storage);
    daisy_seed.PrintLine("  %u model(s) found", (unsigned)models.Count());
    System::Delay(200);

    daisy_seed.PrintLine("Init presets...");
    System::Delay(200);
    presets.Init(storage, models);
    daisy_seed.PrintLine("  %u preset(s)", (unsigned)presets.Count());
    System::Delay(200);
    BuildNamePointers();
    daisy_seed.PrintLine("Name tables ready.");
    System::Delay(200);

    if (presets.Count() > 0)
    {
        daisy_seed.PrintLine("Loading preset 0...");
        System::Delay(200);
        presets.Apply(audio_engine, storage, models,
                      hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
        if constexpr (kDisableIrForTiming)
            delete audio_engine.SwapIR(nullptr);
        daisy_seed.PrintLine("Preset 0 loaded.");
        if constexpr (kDisableIrForTiming)
            daisy_seed.PrintLine("IR disabled for NAM+EQ timing probe.");
        System::Delay(200);
    }

    // --- Controls ------------------------------------------------------------
    daisy_seed.PrintLine("Init controls...");
    System::Delay(200);
    controls.Init();
    daisy_seed.PrintLine("Controls ready.");
    System::Delay(200);

    // --- Display -------------------------------------------------------------
    if constexpr (kDisplayEnabled)
    {
        daisy_seed.PrintLine("Init display...");
        ui.Init();
        PushPerformanceScreen();
        ui.Update(); // first frame (blocking)
        daisy_seed.PrintLine("First frame pushed.");
    }
    else
    {
        daisy_seed.PrintLine("Display disabled for audio isolation test.");
    }

    // --- Audio ---------------------------------------------------------------
    // Without display initialization the firmware reaches StartAudio before
    // USB CDC has finished enumerating. Give the host time to create the tty
    // so overload diagnostics remain observable if DSP later starves main().
    daisy_seed.PrintLine("Waiting 3 seconds for USB before audio start...");
    System::Delay(3000);
    daisy_seed.SetAudioBlockSize(hw::AUDIO_BLOCK_SIZE);
    daisy_seed.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    daisy_seed.StartAudio(AudioCallback);
    if constexpr (kAudioDiagMode == 2)
        daisy_seed.PrintLine("Audio started: 440/880 Hz diagnostic beep pattern.");
    else if constexpr (kAudioDiagMode == 1)
        daisy_seed.PrintLine("Audio started: input passthrough diagnostic.");
    else if constexpr (kAudioDiagMode == 3)
        daisy_seed.PrintLine("Audio started: one-shot full DSP timing probe.");
    else
        daisy_seed.PrintLine("Audio started: full NAM DSP.");

    // --- Main loop -----------------------------------------------------------
    uint32_t now = System::GetNow();
    uint32_t last_diag_ms = now;
    uint32_t last_perf_refresh_ms = now;

    for (;;)
    {
        if (bootloader_requested)
        {
            bootloader_requested = false;
            daisy_seed.PrintLine("NAM_DFU_BOOT ACK");
            daisy_seed.PrintLine("Entering DaisyBoot DFU update mode...");
            daisy_seed.StopAudio();
            System::Delay(50);
            System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
        }

        ControlEvent ev = controls.Process();

        if (!browsing && !editing)
        {
            // --- Performance mode -------------------------------------------

            // Live EQ / gain / vol edit via encoders.
            if (ev.enc_delta[0] != 0) // ENC1 = Gain
            {
                float v = audio_engine.GetInputGain() + ev.enc_delta[0] * kGainStep;
                if (v < 0.0f) v = 0.0f;
                if (v > 2.0f) v = 2.0f;
                audio_engine.SetInputGain(v);
                preset_dirty = true;
                PushPerformanceScreen();
            }
            if (ev.enc_delta[1] != 0) // ENC2 = Bass EQ
            {
                float g = audio_engine.GetEqGain(Eq3::Band::Bass) + ev.enc_delta[1] * kEqDbStep;
                if (g < kEqDbMin) g = kEqDbMin;
                if (g > kEqDbMax) g = kEqDbMax;
                audio_engine.SetEqBand(Eq3::Band::Bass, g, audio_engine.GetEqFreq(Eq3::Band::Bass));
                preset_dirty = true;
                PushPerformanceScreen();
            }
            if (ev.enc_delta[2] != 0) // ENC3 = Mid EQ
            {
                float g = audio_engine.GetEqGain(Eq3::Band::Mid) + ev.enc_delta[2] * kEqDbStep;
                if (g < kEqDbMin) g = kEqDbMin;
                if (g > kEqDbMax) g = kEqDbMax;
                audio_engine.SetEqBand(Eq3::Band::Mid, g, audio_engine.GetEqFreq(Eq3::Band::Mid));
                preset_dirty = true;
                PushPerformanceScreen();
            }
            if (ev.enc_delta[3] != 0) // ENC4 = Treble EQ
            {
                float g = audio_engine.GetEqGain(Eq3::Band::Treble) + ev.enc_delta[3] * kEqDbStep;
                if (g < kEqDbMin) g = kEqDbMin;
                if (g > kEqDbMax) g = kEqDbMax;
                audio_engine.SetEqBand(Eq3::Band::Treble, g, audio_engine.GetEqFreq(Eq3::Band::Treble));
                preset_dirty = true;
                PushPerformanceScreen();
            }
            if (ev.enc_delta[4] != 0) // ENC5 = Vol
            {
                float v = audio_engine.GetOutputVol() + ev.enc_delta[4] * kVolStep;
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
                audio_engine.SetOutputVol(v);
                preset_dirty = true;
                PushPerformanceScreen();
            }

            // Footswitches.
            if (ev.fs1_tap && presets.Count() > 1)
            {
                presets.Next();
                presets.Apply(audio_engine, storage, models,
                              hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                preset_dirty = false;
                PushPerformanceScreen();
                daisy_seed.PrintLine("Preset -> %u: %s",
                             (unsigned)presets.Current(), presets.Name(presets.Current()));
            }
            else if (ev.fs2_tap && presets.Count() > 1)
            {
                presets.Prev();
                presets.Apply(audio_engine, storage, models,
                              hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                preset_dirty = false;
                PushPerformanceScreen();
                daisy_seed.PrintLine("Preset -> %u: %s",
                             (unsigned)presets.Current(), presets.Name(presets.Current()));
            }

            if (ev.fs1_hold)
            {
                SaveActivePreset();
                preset_dirty = false;
                PushPerformanceScreen();
                daisy_seed.PrintLine("Preset %u saved.", (unsigned)presets.Current());
            }
            if (ev.fs2_hold)
            {
                // Revert: re-apply the unmodified in-RAM preset (original QSPI data).
                presets.Apply(audio_engine, storage, models,
                              hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                preset_dirty = false;
                PushPerformanceScreen();
                daisy_seed.PrintLine("Preset %u reverted.", (unsigned)presets.Current());
            }

            // ENC1 click → enter Browse.
            if (ev.enc1_click)
            {
                browse_cursor = presets.Current();
                browsing      = true;
                PushBrowseScreen();
            }
        }
        else if (browsing)
        {
            // --- Browse mode ------------------------------------------------
            if (ev.enc_delta[0] != 0)
            {
                int16_t next = static_cast<int16_t>(browse_cursor) + ev.enc_delta[0];
                if (next < 0) next = 0;
                if (next >= presets.Count()) next = presets.Count() - 1;
                browse_cursor = static_cast<uint8_t>(next);
                PushBrowseScreen();
            }

            if (ev.enc1_click)
            {
                if (browse_cursor != presets.Current())
                {
                    while (presets.Current() != browse_cursor)
                    {
                        if (browse_cursor > presets.Current()) presets.Next();
                        else                                    presets.Prev();
                    }
                    presets.Apply(audio_engine, storage, models,
                                  hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);
                    preset_dirty = false;
                    daisy_seed.PrintLine("Preset -> %u: %s",
                                 (unsigned)presets.Current(), presets.Name(presets.Current()));
                }
                browsing = false;
                PushPerformanceScreen();
            }

            if (ev.enc1_long)
            {
                browsing = false;
                editing  = true;
                PushEditScreen(browse_cursor);
            }

            if (ev.fs1_tap || ev.fs2_tap)
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
                if (ev.enc_delta[0] != 0)
                {
                    int8_t f = static_cast<int8_t>(edit_state.field) + ev.enc_delta[0];
                    if (f < 0) f = 0;
                    if (f > 7) f = 7;
                    edit_state.field = static_cast<uint8_t>(f);
                    if constexpr (kDisplayEnabled)
                        ui.ShowEdit(edit_state);
                }
                if (ev.enc1_click)
                {
                    edit_state.editing = true;
                    if constexpr (kDisplayEnabled)
                        ui.ShowEdit(edit_state);
                }
            }
            else
            {
                if (ev.enc_delta[0] != 0)
                {
                    switch (edit_state.field)
                    {
                    case 0: // MODEL
                    {
                        int16_t idx = static_cast<int16_t>(edit_state.model_idx) + ev.enc_delta[0];
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
                        int16_t idx = static_cast<int16_t>(edit_state.ir_idx) + ev.enc_delta[0];
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
                    case 2: // IN GAIN  [0.0, 2.0]
                    {
                        float v = edit_state.input_gain + ev.enc_delta[0] * kGainStep;
                        edit_state.input_gain = (v < 0.0f) ? 0.0f : (v > 2.0f) ? 2.0f : v;
                        break;
                    }
                    case 3: // OUT VOL  [0.0, 1.0]
                    {
                        float v = edit_state.output_vol + ev.enc_delta[0] * kVolStep;
                        edit_state.output_vol = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
                        break;
                    }
                    case 4: // BYPASS — toggle
                        edit_state.bypass = !edit_state.bypass;
                        break;
                    case 5: // BASS FREQ  [20, 500] Hz
                    {
                        float f = edit_state.eq_bass_freq + ev.enc_delta[0] * kBassFreqStepHz;
                        edit_state.eq_bass_freq = (f < 20.0f) ? 20.0f : (f > 500.0f) ? 500.0f : f;
                        break;
                    }
                    case 6: // MID FREQ  [200, 2000] Hz
                    {
                        float f = edit_state.eq_mid_freq + ev.enc_delta[0] * kMidFreqStepHz;
                        edit_state.eq_mid_freq = (f < 200.0f) ? 200.0f : (f > 2000.0f) ? 2000.0f : f;
                        break;
                    }
                    case 7: // TRE FREQ  [1000, 8000] Hz
                    {
                        float f = edit_state.eq_treble_freq + ev.enc_delta[0] * kTrebleFreqStepHz;
                        edit_state.eq_treble_freq = (f < 1000.0f) ? 1000.0f : (f > 8000.0f) ? 8000.0f : f;
                        break;
                    }
                    }
                    if constexpr (kDisplayEnabled)
                        ui.ShowEdit(edit_state);
                }
                if (ev.enc1_click)
                {
                    edit_state.editing = false;
                    if constexpr (kDisplayEnabled)
                        ui.ShowEdit(edit_state);
                }
            }

            // FS1 tap: apply edits + save to flash.
            if (ev.fs1_tap)
            {
                NamPreset& p = presets.EditablePreset(edit_preset_idx);
                strncpy(p.model_name, edit_state.model_name, NAM_DATA_NAME_LEN - 1);
                p.model_name[NAM_DATA_NAME_LEN - 1] = '\0';
                strncpy(p.ir_name, edit_state.ir_name, NAM_DATA_NAME_LEN - 1);
                p.ir_name[NAM_DATA_NAME_LEN - 1] = '\0';
                p.input_gain      = edit_state.input_gain;
                p.output_volume   = edit_state.output_vol;
                p.bypass          = edit_state.bypass ? 1u : 0u;
                p.eq_bass_freq    = edit_state.eq_bass_freq;
                p.eq_mid_freq     = edit_state.eq_mid_freq;
                p.eq_treble_freq  = edit_state.eq_treble_freq;
                // Preserve live EQ gains (not edited in this screen).
                p.eq_bass_gain    = audio_engine.GetEqGain(Eq3::Band::Bass);
                p.eq_mid_gain     = audio_engine.GetEqGain(Eq3::Band::Mid);
                p.eq_treble_gain  = audio_engine.GetEqGain(Eq3::Band::Treble);

                while (presets.Current() != edit_preset_idx)
                {
                    if (edit_preset_idx > presets.Current()) presets.Next();
                    else                                      presets.Prev();
                }
                presets.ApplyPreset(p, audio_engine, storage, models,
                                    hw::AUDIO_SAMPLE_RATE, hw::AUDIO_BLOCK_SIZE);

                // Write to QSPI if this preset has a flash entry.
                const NamDataEntry* entry = presets.Entry(edit_preset_idx);
                if (entry) {
                    daisy_seed.StopAudio();
                    __disable_irq();
                    storage.WritePreset(entry, p);
                    __enable_irq();
                    daisy_seed.StartAudio(AudioCallback);
                }
                daisy_seed.PrintLine("Edit saved -> preset %u: %s",
                             (unsigned)edit_preset_idx,
                             presets.Name(edit_preset_idx));
                preset_dirty = false;
                editing = false;
                PushPerformanceScreen();
            }

            // FS2 tap: discard edits.
            if (ev.fs2_tap)
            {
                editing = false;
                PushPerformanceScreen();
            }
        }

        now = System::GetNow();
        if (ShouldRefreshPerformanceScreen(browsing, editing, last_perf_refresh_ms, now))
        {
            PushPerformanceScreen();
            last_perf_refresh_ms = now;
        }

        // Display update (fps-capped, non-blocking DMA).
        if constexpr (kDisplayEnabled)
            ui.Update();

        // Once-per-second diagnostics.
        if (now - last_diag_ms >= 1000)
        {
            float cb_ms = static_cast<float>(cb_max_cyc) / 480000.0f;
            audio_overload = (cb_max_cyc > kCbOverloadCyc);
            float input_peak = diag_input_peak;
            float output_peak = diag_output_peak;
            float diff_peak = diag_diff_peak;
            diag_input_peak = 0.0f;
            diag_output_peak = 0.0f;
            diag_diff_peak = 0.0f;
            daisy_seed.PrintLine("cb=%lu  cpu_peak=%.3fms%s  in=%.4f  out=%.4f  diff=%.4f  gain=%.2f  vol=%.2f  bypass=%s",
                (unsigned long)cb_count, cb_ms,
                audio_overload ? "  !OVERLOAD" : "",
                (double)input_peak,
                (double)output_peak,
                (double)diff_peak,
                (double)audio_engine.GetInputGain(),
                (double)audio_engine.GetOutputVol(),
                audio_engine.GetBypass() ? "Y" : "N");
            cb_max_cyc   = 0;
            last_diag_ms = now;
        }
    }
}
