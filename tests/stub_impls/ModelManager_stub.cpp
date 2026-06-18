// Stub ModelManager — scans storage for NAM_ENTRY_MODEL entries (same as real),
// but Load() doesn't actually parse any NAM binary. Sufficient for PresetManager tests.
#include "ModelManager.h"

void ModelManager::Init(QspiStorage& storage)
{
    storage_ = &storage;
    count_   = 0;
    uint16_t total = storage.EntryCount();
    for (uint16_t i = 0; i < total && count_ < kMaxModels; ++i)
    {
        const NamDataEntry* e = storage.GetEntry(i);
        if (e && static_cast<NamEntryType>(e->type) == NAM_ENTRY_MODEL)
            entries_[count_++] = e;
    }
    // No enable_fast_tanh() — not needed for tests.
}

const char* ModelManager::Name(uint8_t i) const
{
    if (i >= count_) return nullptr;
    return entries_[i]->name;
}

bool ModelManager::Load(uint8_t i, AudioEngine& engine,
                        float /*sample_rate*/, size_t /*block_size*/)
{
    if (i >= count_) return false;
    // In tests: swap in nullptr (bypass) — AudioEngine handles nullptr as bypass.
    auto old = engine.SwapModel(nullptr);
    current_ = i;
    return true;
}
