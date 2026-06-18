#include "AudioEngine.h"
#include "IRLoader.h"

void AudioEngine::Init(size_t block_size, float sample_rate)
{
    block_size_  = block_size;
    sample_rate_ = sample_rate;
}

void AudioEngine::Process(const float* in, float* out, size_t frames)
{
    float gain = input_gain_.load();
    float vol  = output_vol_.load();
    bool  bp   = bypass_.load();

    if (bp)
    {
        // Clean passthrough.
        for (size_t i = 0; i < frames; ++i)
            out[i] = in[i];
        return;
    }

    // Apply input gain into scratch buffer.
    for (size_t i = 0; i < frames; ++i)
        scratch_in_[i] = in[i] * gain;

    // NAM model stage.
    nam::DSP* model = active_model_.load();
    if (model)
    {
        float* inp = scratch_in_;
        float* outp = scratch_out_;
        model->process(&inp, &outp, static_cast<int>(frames));
    }
    else
    {
        // No model loaded — clean copy through.
        for (size_t i = 0; i < frames; ++i)
            scratch_out_[i] = scratch_in_[i];
    }

    // IR convolution stage.
    IIRConvolver* ir = active_ir_.load();
    if (ir)
        ir->Process(scratch_out_, scratch_out_, frames);

    // Apply output volume and write result.
    for (size_t i = 0; i < frames; ++i)
        out[i] = scratch_out_[i] * vol;
}

std::unique_ptr<nam::DSP> AudioEngine::SwapModel(std::unique_ptr<nam::DSP> next)
{
    nam::DSP* raw_next = next.get();

    // Transfer ownership to model_owner_ so we hold the new object's lifetime.
    // Swap the old raw pointer out of active_model_ and return its owner.
    std::unique_ptr<nam::DSP> old = std::move(model_owner_);
    model_owner_ = std::move(next);
    active_model_.store(raw_next);  // atomic — callback sees new model immediately

    return old;  // caller disposes the old model safely from the main loop
}

IIRConvolver* AudioEngine::SwapIR(IIRConvolver* next)
{
    IIRConvolver* old = ir_owner_;
    ir_owner_         = next;
    active_ir_.store(next);  // atomic
    return old;
}
