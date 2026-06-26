#pragma once

#include "RealFft128.h"

#include <cstddef>

class PartitionedConvolver
{
  public:
    static constexpr size_t kBlockSize     = 48;
    static constexpr size_t kFftSize       = 128;
    static constexpr size_t kMaxTaps       = 256;
    static constexpr size_t kMaxPartitions = 6;   // ceil(kMaxTaps / kBlockSize)
    static constexpr size_t kOverlapSize   = 47;

    bool Init(const float* ir, size_t tap_count);
    void Reset();
    void Process(const float* input, float* output, size_t frames);

  private:
    static void MultiplyAccumulate(const float* x,
                                   const float* h,
                                   float*       sum);

    RealFft128 fft_;
    alignas(16) float ir_spectra_[kMaxPartitions][kFftSize]{};
    alignas(16) float input_spectra_[kMaxPartitions][kFftSize]{};
    alignas(16) float fft_input_[kFftSize]{};
    alignas(16) float spectrum_sum_[kFftSize]{};
    alignas(16) float ifft_output_[kFftSize]{};
    float overlap_[kOverlapSize]{};
    float delayed_output_[kBlockSize]{};
    size_t active_partitions_ = 0;
    size_t history_write_     = 0;
    bool ready_               = false;
};
