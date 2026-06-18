#include "ModelManager.h"
#include "namb/get_dsp_namb.h"
#include "NAM/activations.h"

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

    nam::activations::Activation::enable_fast_tanh();
}

const char* ModelManager::Name(uint8_t i) const
{
    if (i >= count_) return nullptr;
    return entries_[i]->name;
}

bool ModelManager::Load(uint8_t i, AudioEngine& engine,
                        float sample_rate, size_t block_size)
{
    if (i >= count_ || !storage_) return false;

    const NamDataEntry* e = entries_[i];
    const uint8_t* blob   = storage_->BlobPtr(e);
    if (!blob) return false;

    std::unique_ptr<nam::DSP> model;
    try
    {
        model = nam::get_dsp_namb(blob, e->length);
    }
    catch (...) { return false; }

    if (!model) return false;

    model->ResetAndPrewarm(static_cast<double>(sample_rate),
                           static_cast<int>(block_size));

    // Swap into engine — old model returned for disposal here (main loop safe).
    auto old = engine.SwapModel(std::move(model));
    // old destroyed here (never in ISR).

    current_ = i;
    return true;
}
