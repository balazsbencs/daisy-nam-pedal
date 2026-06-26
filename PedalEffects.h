#pragma once
#include <stddef.h>

class NoiseGate
{
public:
    void Init(float sample_rate);
    void SetEnabled(bool enabled);
    void SetThresholdDb(float threshold_db);
    void Reset();
    void Process(float* buf, size_t n);

    bool  Enabled() const { return enabled_; }
    float ThresholdDb() const { return threshold_db_; }

private:
    float sample_rate_ = 48000.0f;
    float threshold_db_ = -70.0f;
    float threshold_lin_ = 0.000316f;
    float env_ = 0.0f;
    float gain_ = 1.0f;
    bool  enabled_ = false;
};

class Compressor
{
public:
    void Init(float sample_rate);
    void SetEnabled(bool enabled);
    void SetParams(float threshold_db, float ratio, float attack_ms, float release_ms);
    void Reset();
    void Process(float* buf, size_t n);

    bool  Enabled() const { return enabled_; }
    float ThresholdDb() const { return threshold_db_; }
    float Ratio() const { return ratio_; }
    float AttackMs() const { return attack_ms_; }
    float ReleaseMs() const { return release_ms_; }

private:
    float Coeff(float ms) const;

    float sample_rate_ = 48000.0f;
    float threshold_db_ = -18.0f;
    float threshold_lin_ = 0.125893f;
    float ratio_ = 2.0f;
    float attack_ms_ = 10.0f;
    float release_ms_ = 100.0f;
    float attack_coeff_ = 0.99791884f;
    float release_coeff_ = 0.99979168f;
    float env_ = 0.0f;
    float gain_ = 1.0f;
    bool  enabled_ = false;
};

class DelayLine
{
public:
    static constexpr size_t kMaxDelaySamples = 36000;

    void Init(float sample_rate);
    void SetEnabled(bool enabled);
    void SetParams(float time_ms, float repeats, float mix, float tone);
    void Reset();
    void Process(float* buf, size_t n);

    bool  Enabled() const { return enabled_; }
    float TimeMs() const { return time_ms_; }
    float Repeats() const { return repeats_; }
    float Mix() const { return mix_; }
    float Tone() const { return tone_; }

private:
    float sample_rate_ = 48000.0f;
    float time_ms_ = 350.0f;
    float repeats_ = 0.25f;
    float mix_ = 0.18f;
    float tone_ = 0.5f;
    float tone_coeff_ = 0.5f;
    float tone_state_ = 0.0f;
    size_t delay_samples_ = 16800;
    size_t write_idx_ = 0;
    bool enabled_ = false;
    // Backing store lives in SDRAM (see PedalEffects.cpp) to keep this 144 KB
    // out of SRAM — that space is needed so the NAM model arena stays in fast
    // SRAM across preset reloads. Set in Init().
    float* buffer_ = nullptr;
};
