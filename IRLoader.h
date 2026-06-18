// IRLoader.h — IR convolution abstraction for the NAM pedal platform.
//
// IIRConvolver is the interface AudioEngine uses — it never knows whether it's
// talking to a time-domain FIR or a future FFT/partitioned convolver.
//
// FirConvolver: wraps DaisySP's FIR<> (time-domain, CMSIS arm_fir_f32 on ARM).
//   Max taps: kMaxTaps (512). Good to ~0.05 ms per block alongside NAM.
//
// LoadIrFromQspi: reads float32-LE taps directly from a QSPI blob pointer
//   (zero-copy from flash) and builds a FirConvolver. Returns nullptr on error.

#pragma once
#include "Filters/fir.h"   // DaisySP — header-only, found via -I../../DaisySP/Source
#include "QspiStorage.h"
#include <stddef.h>
#include <stdint.h>
#include <memory>

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------
class IIRConvolver
{
public:
    virtual ~IIRConvolver() = default;
    // In-place or separate input/output are both valid; buf_in may equal buf_out.
    virtual void Process(const float* buf_in, float* buf_out, size_t frames) = 0;
    virtual const char* Name() const = 0;
};

// ---------------------------------------------------------------------------
// FirConvolver — time-domain FIR, ≤kMaxTaps taps
// ---------------------------------------------------------------------------
class FirConvolver : public IIRConvolver
{
public:
    static constexpr size_t kMaxTaps  = 512;
    static constexpr size_t kMaxBlock = 48;

    // ir: float32 taps, tap_count ≤ kMaxTaps
    bool Init(const float* ir, size_t tap_count, const char* name);

    void Process(const float* buf_in, float* buf_out, size_t frames) override;

    const char* Name() const override { return name_; }

private:
    // DaisySP FIR with internal storage for up to kMaxTaps × kMaxBlock.
    daisysp::FIR<kMaxTaps, kMaxBlock> fir_;
    char name_[32] = {};
    bool ready_    = false;
};

// ---------------------------------------------------------------------------
// Factory: build a FirConvolver from a QspiStorage IR entry
// ---------------------------------------------------------------------------
// Returns nullptr if the entry is null, not an IR, or tap count > kMaxTaps.
std::unique_ptr<FirConvolver> LoadIrFromQspi(const QspiStorage& storage,
                                             const NamDataEntry* entry);
