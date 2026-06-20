// PresetManager.h — preset list, navigation, and application.
//
// Presets live as NAM_ENTRY_PRESET blobs in QSPI (read at boot).
// If no preset entries exist, a single default "Direct" preset is synthesised.
// Apply() coordinates ModelManager + IRLoader + AudioEngine to switch cleanly.

#pragma once
#include "QspiStorage.h"
#include "AudioEngine.h"
#include "ModelManager.h"
#include "IRLoader.h"
#include "data_format.h"
#include <stdint.h>

class PresetManager
{
public:
    static constexpr uint8_t kMaxPresets = 32;

    void Init(QspiStorage& storage, ModelManager& models);

    uint8_t Count()   const { return count_; }
    uint8_t Current() const { return current_; }

    const char* Name(uint8_t i) const;

    // Navigate — wraps around. No-op when count is zero.
    void Next() { if (count_ > 0) current_ = (current_ + 1) % count_; }
    void Prev() { if (count_ > 0) current_ = (current_ + count_ - 1) % count_; }

    // Apply current preset: load model + IR + levels into engine. ~10-25 ms.
    void Apply(AudioEngine& engine, QspiStorage& storage, ModelManager& models,
               float sample_rate, size_t block_size);

    // Apply an arbitrary preset (used by the Edit screen to apply edited fields).
    void ApplyPreset(const NamPreset& p,
                     AudioEngine& engine, QspiStorage& storage, ModelManager& models,
                     float sample_rate, size_t block_size);

    // Convenience: the active NamPreset record (may be a synthesised default).
    const NamPreset& ActivePreset() const { return presets_[current_]; }

    // Mutable access for in-RAM editing. Changes are session-only (not persisted).
    NamPreset& EditablePreset(uint8_t i) { return presets_[i < count_ ? i : 0]; }

    // QSPI directory entry for preset i (nullptr for synthesised defaults).
    // Use this pointer with QspiStorage::WritePreset() to persist changes.
    const NamDataEntry* Entry(uint8_t i) const { return (i < count_) ? entries_[i] : nullptr; }

private:
    NamPreset           presets_[kMaxPresets];
    const NamDataEntry* entries_[kMaxPresets] = {};
    char                names_[kMaxPresets][NAM_DATA_NAME_LEN] = {};
    uint8_t             count_   = 0;
    uint8_t             current_ = 0;

    // Currently owned IR (disposed on next Apply call).
    IIRConvolver* current_ir_ = nullptr;
};
