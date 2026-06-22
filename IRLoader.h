// IRLoader.h — IR convolution abstraction for the NAM pedal platform.
//
// IIRConvolver is the interface AudioEngine uses — it never knows whether it's
// talking to a time-domain FIR or a future FFT/partitioned convolver.
//
// FirConvolver: complete-IR partitioned FFT convolution with one block latency.
//
// LoadIrFromQspi: reads float32-LE taps directly from a QSPI blob pointer
//   (zero-copy from flash) and builds a FirConvolver. Returns nullptr on error.

#pragma once
#include "PartitionedConvolver.h"
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
// FirConvolver — partitioned FFT convolution, <= kMaxTaps taps
// ---------------------------------------------------------------------------
class FirConvolver : public IIRConvolver
{
public:
    static constexpr size_t kMaxTaps  = 512;
    static constexpr size_t kMaxBlock = 48;

    // ir: float32 taps, tap_count ≤ kMaxTaps
    bool Init(const float* ir, size_t tap_count, const char* name);

    void Process(const float* buf_in, float* buf_out, size_t frames) override;
    void Reset() { convolver_.Reset(); }

    const char* Name() const override { return name_; }

private:
    PartitionedConvolver convolver_;
    char name_[32] = {};
    bool ready_    = false;
};

// ---------------------------------------------------------------------------
// Factory: build a FirConvolver from a QspiStorage IR entry
// ---------------------------------------------------------------------------
// Returns nullptr if the entry is null, not an IR, or tap count > kMaxTaps.
std::unique_ptr<FirConvolver> LoadIrFromQspi(const QspiStorage& storage,
                                             const NamDataEntry* entry);
