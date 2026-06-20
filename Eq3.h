#pragma once
#include <atomic>
#include <stddef.h>

class Eq3
{
public:
    enum class Band { Bass = 0, Mid = 1, Treble = 2 };

    void Reset(float sample_rate);                       // main loop, pre-audio
    void SetBand(Band b, float gain_db, float freq_hz);  // main loop
    void Process(float* buf, size_t n);                  // audio ISR, in-place

    float GetGainDb(Band b) const { return gain_db_[static_cast<int>(b)]; }
    float GetFreq(Band b)   const { return freq_[static_cast<int>(b)]; }

private:
    struct BiquadCoeffs { float b0, b1, b2, a1, a2; };
    struct BiquadState  { float z1, z2; };

    static BiquadCoeffs MakeLowShelf (float fs, float f0, float gain_db);
    static BiquadCoeffs MakePeaking  (float fs, float f0, float gain_db, float q);
    static BiquadCoeffs MakeHighShelf(float fs, float f0, float gain_db);

    void Publish();

    float sample_rate_ = 48000.0f;
    static constexpr float kMidQ = 0.7f;

    BiquadCoeffs staged_[3] = {};
    BiquadCoeffs active_[2][3] = {};
    std::atomic<int> idx_{0};
    BiquadState state_[3] = {};

    float gain_db_[3] = {0.0f, 0.0f, 0.0f};
    float freq_[3]    = {100.0f, 750.0f, 4000.0f};
};
