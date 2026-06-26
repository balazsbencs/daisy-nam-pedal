#include "PresetManager.h"
#include "Eq3.h"
#include <cstring>

// Temporary: globally disable IR load + convolution to test whether the IR
// (build cost on switch and/or per-block convolution) is what starves the CPU.
// Set via -DNAM_DISABLE_IR=1 in the Makefile. Remove once diagnosed.
#ifndef NAM_DISABLE_IR
#define NAM_DISABLE_IR 0
#endif

static constexpr uint32_t NAM_LEGACY_PRESET_SIZE = 74;

// Substitute default frequency when a stored value is absent (zero).
static inline float eq_freq_or(float stored, float dflt) { return stored > 0.0f ? stored : dflt; }
static inline float param_or(float stored, float dflt) { return stored > 0.0f ? stored : dflt; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void PresetManager::Init(QspiStorage& storage, ModelManager& models)
{
    count_ = 0;

    // Collect NAM_ENTRY_PRESET blobs from QSPI.
    uint16_t total = storage.EntryCount();
    for (uint16_t i = 0; i < total && count_ < kMaxPresets; ++i)
    {
        const NamDataEntry* e = storage.GetEntry(i);
        if (!e || static_cast<NamEntryType>(e->type) != NAM_ENTRY_PRESET)
            continue;

        const uint8_t* blob = storage.BlobPtr(e);
        if (!blob || e->length < NAM_LEGACY_PRESET_SIZE)
            continue;

        memset(&presets_[count_], 0, sizeof(NamPreset));
        size_t n = e->length < sizeof(NamPreset) ? e->length : sizeof(NamPreset);
        memcpy(&presets_[count_], blob, n);
        strncpy(names_[count_], e->name, NAM_DATA_NAME_LEN - 1);
        names_[count_][NAM_DATA_NAME_LEN - 1] = '\0';
        entries_[count_] = e;
        count_++;
    }

    // If no presets exist, synthesise one per model so the pedal is usable
    // straight after flashing models without any presets.json.
    if (count_ == 0)
    {
        for (uint8_t m = 0; m < models.Count() && count_ < kMaxPresets; ++m)
        {
            NamPreset& p = presets_[count_];
            const char* mname = models.Name(m);
            strncpy(p.model_name, mname ? mname : "", NAM_DATA_NAME_LEN - 1);
            p.model_name[NAM_DATA_NAME_LEN - 1] = '\0';
            p.ir_name[0]     = '\0'; // no IR
            p.input_gain     = 1.0f;
            p.output_volume  = 0.85f;
            p.bypass         = 0;
            p.eq_bass_gain = p.eq_mid_gain = p.eq_treble_gain = 0.0f;
            p.eq_bass_freq = 100.0f; p.eq_mid_freq = 750.0f; p.eq_treble_freq = 4000.0f;
            strncpy(names_[count_], mname ? mname : "Preset", NAM_DATA_NAME_LEN - 1);
            names_[count_][NAM_DATA_NAME_LEN - 1] = '\0';
            count_++;
        }
    }

    // Always have at least one (silent/bypass) preset.
    if (count_ == 0)
    {
        memset(&presets_[0], 0, sizeof(NamPreset));
        presets_[0].input_gain    = 1.0f;
        presets_[0].output_volume = 0.85f;
        presets_[0].bypass        = 1;
        strncpy(names_[0], "Direct", NAM_DATA_NAME_LEN - 1);
        count_ = 1;
    }
}

const char* PresetManager::Name(uint8_t i) const
{
    return (i < count_) ? names_[i] : nullptr;
}

void PresetManager::ApplyPreset(const NamPreset& p,
                                AudioEngine& engine, QspiStorage& storage,
                                ModelManager& models,
                                float sample_rate, size_t block_size)
{
    // Silence output while we rebuild the model + IR (heavy main-loop work);
    // unmuted at the end so the switch is a brief gap, not a glitch/overload.
    engine.SetMuted(true);

    engine.SetBypass(p.bypass != 0);
    engine.SetInputGain(p.input_gain);
    engine.SetOutputVol(p.output_volume);
    engine.SetEqBand(Eq3::Band::Bass,   p.eq_bass_gain,   eq_freq_or(p.eq_bass_freq,   100.0f));
    engine.SetEqBand(Eq3::Band::Mid,    p.eq_mid_gain,    eq_freq_or(p.eq_mid_freq,    750.0f));
    engine.SetEqBand(Eq3::Band::Treble, p.eq_treble_gain, eq_freq_or(p.eq_treble_freq, 4000.0f));
    engine.SetNoiseGate(
        p.noise_gate_enabled != 0,
        clampf(p.noise_gate_threshold_db == 0.0f ? -70.0f : p.noise_gate_threshold_db, -90.0f, -20.0f));
    engine.SetCompressor(
        p.compressor_enabled != 0,
        clampf(p.compressor_threshold_db == 0.0f ? -18.0f : p.compressor_threshold_db, -60.0f, 0.0f),
        clampf(param_or(p.compressor_ratio, 2.0f), 1.0f, 20.0f),
        clampf(param_or(p.compressor_attack_ms, 10.0f), 0.1f, 200.0f),
        clampf(param_or(p.compressor_release_ms, 100.0f), 5.0f, 1000.0f));
    engine.SetDelay(
        p.delay_enabled != 0,
        clampf(param_or(p.delay_time_ms, 350.0f), 1.0f, 750.0f),
        clampf(p.delay_repeats, 0.0f, 0.95f),
        clampf(p.delay_mix, 0.0f, 1.0f),
        clampf(p.delay_tone == 0.0f ? 0.5f : p.delay_tone, 0.0f, 1.0f));

    // Load model by name. If the named model is missing OR fails to load
    // (corrupt blob, parse error), engage bypass rather than leaving a stale
    // or null model running with bypass disabled.
    if (p.model_name[0] != '\0')
    {
        bool loaded = false;
        for (uint8_t m = 0; m < models.Count(); ++m)
        {
            if (strncmp(models.Name(m), p.model_name, NAM_DATA_NAME_LEN) == 0)
            {
                loaded = models.Load(m, engine, sample_rate, block_size);
                break;
            }
        }
        if (!loaded)
            engine.SetBypass(true);
    }

    // Load IR by name (or null = bypass). When NAM_DISABLE_IR is set, never
    // build or install an IR — new_ir stays null, so SwapIR frees any previous
    // one and Process skips the convolution entirely.
    IIRConvolver* new_ir = nullptr;
#if !NAM_DISABLE_IR
    if (p.ir_name[0] != '\0')
    {
        const NamDataEntry* ir_entry = storage.FindEntry(NAM_ENTRY_IR, p.ir_name);
        if (ir_entry)
        {
            auto conv = LoadIrFromQspi(storage, ir_entry);
            new_ir = conv.release();
        }
    }
#endif

    IIRConvolver* old_ir = engine.SwapIR(new_ir);
    delete old_ir;
    current_ir_ = new_ir;

    engine.SetMuted(false);
}

void PresetManager::Apply(AudioEngine& engine, QspiStorage& storage,
                          ModelManager& models,
                          float sample_rate, size_t block_size)
{
    if (count_ == 0) return;
    ApplyPreset(presets_[current_], engine, storage, models, sample_rate, block_size);
}
