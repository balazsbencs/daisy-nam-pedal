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

    // Free the active model BEFORE allocating the new one so the new model's
    // conv-history arena (~150 KB) is reused from the just-freed SRAM hole and
    // keeps running from fast SRAM. Holding both live forces the new arena to
    // spill into SDRAM (heap is ~342 KB, two arenas don't fit) and running the
    // model from SDRAM is ~40% slower — it blew the 1.0 ms block deadline and
    // tripped !OVERLOAD on hardware. The brief dry passthrough during the load
    // (active_model_ -> null) is imperceptible on a manual preset switch. The
    // SDRAM-spill heap (SdramHeap.cpp) remains as a no-fault safety net.
    engine.SwapModel(nullptr);

    std::unique_ptr<nam::DSP> model;
    try
    {
        model = nam::get_dsp_namb(blob, e->length);
    }
    catch (...) { return false; }

    if (!model) return false;

    // A2 requests 6,346 prewarm samples. Running 133 neural blocks
    // synchronously stalls the embedded UI/USB during every model load.
    // Zero-initialized state is valid; let it settle during live processing.
    model->SetPrewarmOnReset(false);
    model->Reset(static_cast<double>(sample_rate), static_cast<int>(block_size));

    // Swap the new (SRAM-resident) model in. The old one was already freed
    // above, so this returns an empty owner.
    engine.SwapModel(std::move(model));

    current_ = i;
    return true;
}
