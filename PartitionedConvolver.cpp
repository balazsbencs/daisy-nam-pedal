#include "PartitionedConvolver.h"

#include <algorithm>
#include <cstring>

bool PartitionedConvolver::Init(const float* ir, size_t tap_count)
{
    ready_ = false;
    if(!ir || tap_count == 0 || tap_count > kMaxTaps || !fft_.Init())
        return false;

    std::memset(ir_spectra_, 0, sizeof(ir_spectra_));
    active_partitions_ = (tap_count + kBlockSize - 1) / kBlockSize;

    for(size_t partition = 0; partition < active_partitions_; ++partition)
    {
        std::fill_n(fft_input_, kFftSize, 0.0f);
        const size_t offset = partition * kBlockSize;
        const size_t count  = std::min(kBlockSize, tap_count - offset);
        std::copy_n(ir + offset, count, fft_input_);
        fft_.Forward(fft_input_, ir_spectra_[partition]);
    }

    ready_ = true;
    Reset();
    return true;
}

void PartitionedConvolver::Reset()
{
    std::memset(input_spectra_, 0, sizeof(input_spectra_));
    std::fill_n(fft_input_, kFftSize, 0.0f);
    std::fill_n(spectrum_sum_, kFftSize, 0.0f);
    std::fill_n(ifft_output_, kFftSize, 0.0f);
    std::fill_n(overlap_, kOverlapSize, 0.0f);
    std::fill_n(delayed_output_, kBlockSize, 0.0f);
    history_write_ = 0;
}

void PartitionedConvolver::MultiplyAccumulate(const float* x,
                                              const float* h,
                                              float*       sum)
{
    sum[0] += x[0] * h[0];
    sum[1] += x[1] * h[1];
    for(size_t k = 1; k < kFftSize / 2; ++k)
    {
        const size_t i  = 2 * k;
        const float  xr = x[i];
        const float  xi = x[i + 1];
        const float  hr = h[i];
        const float  hi = h[i + 1];
        sum[i] += xr * hr - xi * hi;
        sum[i + 1] += xr * hi + xi * hr;
    }
}

void PartitionedConvolver::Process(const float* input,
                                   float*       output,
                                   size_t       frames)
{
    if(!ready_ || frames != kBlockSize)
    {
        if(input != output)
            std::copy_n(input, frames, output);
        return;
    }

    std::fill_n(fft_input_, kFftSize, 0.0f);
    std::copy_n(input, kBlockSize, fft_input_);
    fft_.Forward(fft_input_, input_spectra_[history_write_]);

    std::fill_n(spectrum_sum_, kFftSize, 0.0f);
    for(size_t partition = 0; partition < active_partitions_; ++partition)
    {
        const size_t history = (history_write_ + kMaxPartitions - partition)
                               % kMaxPartitions;
        MultiplyAccumulate(input_spectra_[history],
                           ir_spectra_[partition],
                           spectrum_sum_);
    }

    fft_.Inverse(spectrum_sum_, ifft_output_);

    for(size_t i = 0; i < kBlockSize; ++i)
    {
        const float wet = ifft_output_[i]
                          + (i < kOverlapSize ? overlap_[i] : 0.0f);
        output[i]         = delayed_output_[i];
        delayed_output_[i] = wet;
    }
    std::copy_n(ifft_output_ + kBlockSize, kOverlapSize, overlap_);

    history_write_ = (history_write_ + 1) % kMaxPartitions;
}
