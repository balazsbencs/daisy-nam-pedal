// AudioEngine.h — real-time audio processing core.
//
// Owns a pair of NAM model slots (active + staging) and an IR convolver slot.
// The audio callback reads only the active pointers — never touches flash or RAM
// allocation. All model/IR construction happens in the main loop, then an atomic
// pointer swap brings the new object live.
//
// Signal chain (mono, 48 kHz):
//   in → input_gain → NAM model → IR convolver → output_vol → out L+R

#pragma once
#include "Eq3.h"
#include "NAM/dsp.h"
#include "PedalEffects.h"
#include <atomic>
#include <memory>
#include <stddef.h>

// Forward-declare so IRLoader.h can stay out of this header.
class IIRConvolver;

class AudioEngine
{
public:
    void Init(size_t block_size, float sample_rate);

    // Called from the audio ISR — must be deterministic and non-blocking.
    void Process(const float* in, float* out, size_t frames);

    // ---------------------------------------------------------------------------
    // Thread-safe setters (call from main loop only)
    // ---------------------------------------------------------------------------

    // Atomically replace the active NAM model. The old model is returned so the
    // caller can destroy it safely from the main loop (never free in the ISR).
    // Passing nullptr installs bypass on the model stage.
    std::unique_ptr<nam::DSP> SwapModel(std::unique_ptr<nam::DSP> next);

    // Atomically replace the active IR convolver. nullptr = IR bypassed.
    IIRConvolver* SwapIR(IIRConvolver* next);

    void SetInputGain(float g)  { input_gain_.store(g);  }
    void SetOutputVol(float v)  { output_vol_.store(v);  }
    void SetBypass(bool b)      { bypass_.store(b);      }

    // Update one EQ band (main loop only). gain in dB, freq in Hz.
    void  SetEqBand(Eq3::Band b, float gain_db, float freq_hz) { eq_.SetBand(b, gain_db, freq_hz); }
    float GetEqGain(Eq3::Band b) const { return eq_.GetGainDb(b); }
    float GetEqFreq(Eq3::Band b) const { return eq_.GetFreq(b); }

    void SetNoiseGate(bool enabled, float threshold_db);
    void SetCompressor(bool enabled, float threshold_db, float ratio, float attack_ms, float release_ms);
    void SetDelay(bool enabled, float time_ms, float repeats, float mix, float tone);

    bool  GetNoiseGateEnabled() const { return gate_.Enabled(); }
    float GetNoiseGateThresholdDb() const { return gate_.ThresholdDb(); }
    bool  GetCompressorEnabled() const { return compressor_.Enabled(); }
    float GetCompressorThresholdDb() const { return compressor_.ThresholdDb(); }
    float GetCompressorRatio() const { return compressor_.Ratio(); }
    float GetCompressorAttackMs() const { return compressor_.AttackMs(); }
    float GetCompressorReleaseMs() const { return compressor_.ReleaseMs(); }
    bool  GetDelayEnabled() const { return delay_.Enabled(); }
    float GetDelayTimeMs() const { return delay_.TimeMs(); }
    float GetDelayRepeats() const { return delay_.Repeats(); }
    float GetDelayMix() const { return delay_.Mix(); }
    float GetDelayTone() const { return delay_.Tone(); }

    float GetInputGain()  const { return input_gain_.load(); }
    float GetOutputVol()  const { return output_vol_.load(); }
    bool  GetBypass()     const { return bypass_.load();     }

    // True if a model is currently loaded and active.
    bool HasModel() const { return active_model_.load() != nullptr; }

private:
    // Active pointers — read by the ISR. Written only under the atomic swap.
    std::atomic<nam::DSP*>     active_model_{nullptr};
    std::atomic<IIRConvolver*> active_ir_{nullptr};

    // Ownership — held in the main loop context.
    std::unique_ptr<nam::DSP> model_owner_;
    IIRConvolver*             ir_owner_ = nullptr;

    std::atomic<float> input_gain_{1.0f};
    std::atomic<float> output_vol_{1.0f};
    std::atomic<bool>  bypass_{true};

    Eq3 eq_;
    NoiseGate gate_;
    Compressor compressor_;
    DelayLine delay_;

    size_t block_size_   = 48;
    float  sample_rate_  = 48000.0f;

    // Scratch buffers for mono processing (in default .bss / AXI SRAM).
    static constexpr size_t kMaxBlock = 48;
    float scratch_in_[kMaxBlock];
    float scratch_out_[kMaxBlock];
};
