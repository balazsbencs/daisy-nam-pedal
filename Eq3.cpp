#include "Eq3.h"
#include <cmath>
#include <cstring>

void Eq3::Reset(float sample_rate)
{
    sample_rate_ = sample_rate;
    for (int b = 0; b < 3; ++b) state_[b] = {0.0f, 0.0f};
    gain_db_[0] = gain_db_[1] = gain_db_[2] = 0.0f;
    freq_[0] = 100.0f; freq_[1] = 750.0f; freq_[2] = 4000.0f;
    staged_[0] = MakeLowShelf (sample_rate_, 100.0f,  0.0f);
    staged_[1] = MakePeaking  (sample_rate_, 750.0f,  0.0f, kMidQ);
    staged_[2] = MakeHighShelf(sample_rate_, 4000.0f, 0.0f);
    active_[0][0] = active_[1][0] = staged_[0];
    active_[0][1] = active_[1][1] = staged_[1];
    active_[0][2] = active_[1][2] = staged_[2];
    idx_.store(0, std::memory_order_release);
}

void Eq3::SetBand(Band b, float gain_db, float freq_hz)
{
    int i = static_cast<int>(b);
    gain_db_[i] = gain_db;
    freq_[i]    = freq_hz;
    switch (b)
    {
    case Band::Bass:   staged_[i] = MakeLowShelf (sample_rate_, freq_hz, gain_db); break;
    case Band::Mid:    staged_[i] = MakePeaking  (sample_rate_, freq_hz, gain_db, kMidQ); break;
    case Band::Treble: staged_[i] = MakeHighShelf(sample_rate_, freq_hz, gain_db); break;
    }
    Publish();
}

void Eq3::Publish()
{
    int next = 1 - idx_.load(std::memory_order_relaxed);
    active_[next][0] = staged_[0];
    active_[next][1] = staged_[1];
    active_[next][2] = staged_[2];
    idx_.store(next, std::memory_order_release);
}

void Eq3::Process(float* buf, size_t n)
{
    int i = idx_.load(std::memory_order_acquire);
    const BiquadCoeffs* c = active_[i];
    for (size_t s = 0; s < n; ++s)
    {
        float x = buf[s];
        for (int b = 0; b < 3; ++b)
        {
            // Transposed Direct Form II.
            float y = c[b].b0 * x + state_[b].z1;
            state_[b].z1 = c[b].b1 * x - c[b].a1 * y + state_[b].z2;
            state_[b].z2 = c[b].b2 * x - c[b].a2 * y;
            x = y;
        }
        buf[s] = x;
    }
}

Eq3::BiquadCoeffs Eq3::MakePeaking(float fs, float f0, float gain_db, float q)
{
    float A    = std::pow(10.0f, gain_db / 40.0f);
    float w0   = 2.0f * static_cast<float>(M_PI) * f0 / fs;
    float cw   = std::cos(w0), sw = std::sin(w0);
    float alpha = sw / (2.0f * q);
    float a0 = 1.0f + alpha / A;
    BiquadCoeffs c;
    c.b0 = (1.0f + alpha * A) / a0;
    c.b1 = (-2.0f * cw)       / a0;
    c.b2 = (1.0f - alpha * A) / a0;
    c.a1 = (-2.0f * cw)       / a0;
    c.a2 = (1.0f - alpha / A) / a0;
    return c;
}

Eq3::BiquadCoeffs Eq3::MakeLowShelf(float fs, float f0, float gain_db)
{
    float A    = std::pow(10.0f, gain_db / 40.0f);
    float w0   = 2.0f * static_cast<float>(M_PI) * f0 / fs;
    float cw   = std::cos(w0), sw = std::sin(w0);
    float alpha = sw / 2.0f * std::sqrt(2.0f);
    float ap1 = A + 1.0f, am1 = A - 1.0f, ta = 2.0f * std::sqrt(A) * alpha;
    float a0 =  ap1 + am1 * cw + ta;
    BiquadCoeffs c;
    c.b0 =  A * (ap1 - am1 * cw + ta) / a0;
    c.b1 =  2.0f * A * (am1 - ap1 * cw) / a0;
    c.b2 =  A * (ap1 - am1 * cw - ta) / a0;
    c.a1 = -2.0f * (am1 + ap1 * cw) / a0;
    c.a2 =  (ap1 + am1 * cw - ta) / a0;
    return c;
}

Eq3::BiquadCoeffs Eq3::MakeHighShelf(float fs, float f0, float gain_db)
{
    float A    = std::pow(10.0f, gain_db / 40.0f);
    float w0   = 2.0f * static_cast<float>(M_PI) * f0 / fs;
    float cw   = std::cos(w0), sw = std::sin(w0);
    float alpha = sw / 2.0f * std::sqrt(2.0f);
    float ap1 = A + 1.0f, am1 = A - 1.0f, ta = 2.0f * std::sqrt(A) * alpha;
    float a0 =  ap1 - am1 * cw + ta;
    BiquadCoeffs c;
    c.b0 =  A * (ap1 + am1 * cw + ta) / a0;
    c.b1 = -2.0f * A * (am1 + ap1 * cw) / a0;
    c.b2 =  A * (ap1 + am1 * cw - ta) / a0;
    c.a1 =  2.0f * (am1 - ap1 * cw) / a0;
    c.a2 =  (ap1 - am1 * cw - ta) / a0;
    return c;
}
