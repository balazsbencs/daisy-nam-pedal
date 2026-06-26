#include "PedalEffects.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Delay backing store, placed in external SDRAM (.sdram_bss is NOLOAD, so it is
// NOT auto-zeroed — DelayLine::Init/Reset memset it before use). Keeping this
// 144 KB out of SRAM leaves room for the NAM model arena to stay in fast SRAM
// when a preset reloads the model (otherwise it spills to SDRAM and the model
// runs ~40% slower, blowing the audio deadline). One global instance — there is
// exactly one DelayLine in the engine.
#ifdef HOST_BUILD
static float s_delay_buffer[DelayLine::kMaxDelaySamples];
#else
static float s_delay_buffer[DelayLine::kMaxDelaySamples] __attribute__((section(".sdram_bss")));
#endif

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float db_to_lin(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

void NoiseGate::Init(float sample_rate)
{
    sample_rate_ = sample_rate > 1000.0f ? sample_rate : 48000.0f;
    SetThresholdDb(threshold_db_);
    Reset();
}

void NoiseGate::SetEnabled(bool enabled) { enabled_ = enabled; }

void NoiseGate::SetThresholdDb(float threshold_db)
{
    threshold_db_ = clampf(threshold_db, -90.0f, -20.0f);
    threshold_lin_ = db_to_lin(threshold_db_);
}

void NoiseGate::Reset()
{
    env_ = 0.0f;
    gain_ = 0.0f;
}

void NoiseGate::Process(float* buf, size_t n)
{
    if (!enabled_) return;

    const float env_attack = 0.05f;
    const float env_release = 0.002f;
    const float gain_attack = 0.02f;
    const float gain_release = 0.0008f;
    const float open_threshold = threshold_lin_ * 1.41254f;

    for (size_t i = 0; i < n; ++i)
    {
        float x = buf[i];
        float ax = std::fabs(x);
        float ec = ax > env_ ? env_attack : env_release;
        env_ += ec * (ax - env_);

        float target = env_ >= open_threshold ? 1.0f : (env_ <= threshold_lin_ ? 0.0f : gain_);
        float gc = target > gain_ ? gain_attack : gain_release;
        gain_ += gc * (target - gain_);
        buf[i] = x * gain_;
    }
}

void Compressor::Init(float sample_rate)
{
    sample_rate_ = sample_rate > 1000.0f ? sample_rate : 48000.0f;
    SetParams(threshold_db_, ratio_, attack_ms_, release_ms_);
    Reset();
}

float Compressor::Coeff(float ms) const
{
    ms = clampf(ms, 0.1f, 1000.0f);
    return std::exp(-1.0f / (0.001f * ms * sample_rate_));
}

void Compressor::SetEnabled(bool enabled) { enabled_ = enabled; }

void Compressor::SetParams(float threshold_db, float ratio, float attack_ms, float release_ms)
{
    threshold_db_ = clampf(threshold_db, -60.0f, 0.0f);
    threshold_lin_ = db_to_lin(threshold_db_);
    ratio_ = clampf(ratio, 1.0f, 20.0f);
    attack_ms_ = clampf(attack_ms, 0.1f, 200.0f);
    release_ms_ = clampf(release_ms, 5.0f, 1000.0f);
    attack_coeff_ = Coeff(attack_ms_);
    release_coeff_ = Coeff(release_ms_);
}

void Compressor::Reset()
{
    env_ = 0.0f;
    gain_ = 1.0f;
}

void Compressor::Process(float* buf, size_t n)
{
    if (!enabled_) return;

    for (size_t i = 0; i < n; ++i)
    {
        float x = buf[i];
        float ax = std::fabs(x);
        float ec = ax > env_ ? attack_coeff_ : release_coeff_;
        env_ = (ec * env_) + ((1.0f - ec) * ax);

        float target_gain = 1.0f;
        if (env_ > threshold_lin_ && env_ > 1e-9f)
        {
            float compressed = threshold_lin_ + (env_ - threshold_lin_) / ratio_;
            target_gain = compressed / env_;
        }

        float gc = target_gain < gain_ ? attack_coeff_ : release_coeff_;
        gain_ = (gc * gain_) + ((1.0f - gc) * target_gain);
        buf[i] = x * gain_;
    }
}

void DelayLine::Init(float sample_rate)
{
    buffer_ = s_delay_buffer;   // SDRAM backing store
    sample_rate_ = sample_rate > 1000.0f ? sample_rate : 48000.0f;
    SetParams(time_ms_, repeats_, mix_, tone_);
    Reset();
}

void DelayLine::SetEnabled(bool enabled)
{
    if (enabled_ != enabled && !enabled) Reset();
    enabled_ = enabled;
}

void DelayLine::SetParams(float time_ms, float repeats, float mix, float tone)
{
    time_ms_ = clampf(time_ms, 1.0f, 750.0f);
    repeats_ = clampf(repeats, 0.0f, 0.95f);
    mix_ = clampf(mix, 0.0f, 1.0f);
    tone_ = clampf(tone, 0.0f, 1.0f);
    tone_coeff_ = 0.05f + tone_ * 0.90f;

    size_t samples = static_cast<size_t>((time_ms_ * sample_rate_ * 0.001f) + 0.5f);
    delay_samples_ = std::max<size_t>(1, std::min(samples, kMaxDelaySamples - 1));
}

void DelayLine::Reset()
{
    if (buffer_)
        std::memset(buffer_, 0, kMaxDelaySamples * sizeof(float));
    write_idx_ = 0;
    tone_state_ = 0.0f;
}

void DelayLine::Process(float* buf, size_t n)
{
    if (!enabled_ || !buffer_) return;

    for (size_t i = 0; i < n; ++i)
    {
        size_t read_idx = write_idx_ >= delay_samples_
            ? write_idx_ - delay_samples_
            : kMaxDelaySamples + write_idx_ - delay_samples_;

        float dry = buf[i];
        float delayed = buffer_[read_idx];
        tone_state_ += tone_coeff_ * (delayed - tone_state_);
        float feedback = tone_state_ * repeats_;
        buffer_[write_idx_] = dry + feedback;

        buf[i] = dry * (1.0f - mix_) + delayed * mix_;

        write_idx_++;
        if (write_idx_ >= kMaxDelaySamples) write_idx_ = 0;
    }
}
