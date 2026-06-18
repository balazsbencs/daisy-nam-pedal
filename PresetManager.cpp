#include "PresetManager.h"
#include <cstring>

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
        if (!blob || e->length < sizeof(NamPreset))
            continue;

        memcpy(&presets_[count_], blob, sizeof(NamPreset));
        strncpy(names_[count_], e->name, NAM_DATA_NAME_LEN - 1);
        names_[count_][NAM_DATA_NAME_LEN - 1] = '\0';
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
    engine.SetBypass(p.bypass != 0);
    engine.SetInputGain(p.input_gain);
    engine.SetOutputVol(p.output_volume);

    // Load model by name. If specified but not found, engage bypass.
    if (p.model_name[0] != '\0')
    {
        bool found = false;
        for (uint8_t m = 0; m < models.Count(); ++m)
        {
            if (strncmp(models.Name(m), p.model_name, NAM_DATA_NAME_LEN) == 0)
            {
                models.Load(m, engine, sample_rate, block_size);
                found = true;
                break;
            }
        }
        if (!found)
            engine.SetBypass(true);
    }

    // Load IR by name (or null = bypass).
    IIRConvolver* new_ir = nullptr;
    if (p.ir_name[0] != '\0')
    {
        const NamDataEntry* ir_entry = storage.FindEntry(NAM_ENTRY_IR, p.ir_name);
        if (ir_entry)
        {
            auto conv = LoadIrFromQspi(storage, ir_entry);
            new_ir = conv.release();
        }
    }

    IIRConvolver* old_ir = engine.SwapIR(new_ir);
    delete old_ir;
    current_ir_ = new_ir;
}

void PresetManager::Apply(AudioEngine& engine, QspiStorage& storage,
                          ModelManager& models,
                          float sample_rate, size_t block_size)
{
    if (count_ == 0) return;
    ApplyPreset(presets_[current_], engine, storage, models, sample_rate, block_size);
}
