#include "IRLoader.h"
#include "data_format.h"
#include <cstring>

// ---------------------------------------------------------------------------
// FirConvolver
// ---------------------------------------------------------------------------

bool FirConvolver::Init(const float* ir, size_t tap_count, const char* name)
{
    if (!ir || tap_count == 0 || tap_count > kMaxTaps)
        return false;

    // DaisySP FIR: SetIR(coefs, len, reverse=true)
    // reverse=true because DaisySP expects the IR in convolution order.
    fir_.SetIR(ir, tap_count, /*reverse=*/true);
    fir_.Reset();

    strncpy(name_, name ? name : "IR", sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
    ready_ = true;
    return true;
}

void FirConvolver::Process(const float* buf_in, float* buf_out, size_t frames)
{
    if (!ready_)
    {
        if (buf_in != buf_out)
            for (size_t i = 0; i < frames; ++i)
                buf_out[i] = buf_in[i];
        return;
    }
    fir_.ProcessBlock(const_cast<float*>(buf_in), buf_out, frames);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<FirConvolver> LoadIrFromQspi(const QspiStorage& storage,
                                             const NamDataEntry* entry)
{
    if (!entry)
        return nullptr;
    if (static_cast<NamEntryType>(entry->type) != NAM_ENTRY_IR)
        return nullptr;

    size_t tap_count = entry->length / sizeof(float);
    if (tap_count == 0 || tap_count > FirConvolver::kMaxTaps)
        return nullptr;

    // Read taps directly from QSPI (memory-mapped, zero-copy).
    const uint8_t* blob = storage.BlobPtr(entry);
    if (!blob)
        return nullptr;

    const float* taps = reinterpret_cast<const float*>(blob);
    auto conv = std::make_unique<FirConvolver>();
    if (!conv->Init(taps, tap_count, entry->name))
        return nullptr;

    return conv;
}
