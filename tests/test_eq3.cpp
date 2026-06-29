#include "Eq3.h"
#include "test_harness.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Process a steady sine of `freq` through `eq`, return output/input RMS in dB
// (after discarding a settling transient).
static float band_gain_db(Eq3& eq, float fs, float freq)
{
    const int total = 8192, skip = 4096;
    double in_sq = 0, out_sq = 0;
    std::vector<float> buf(total);
    for (int i = 0; i < total; ++i)
        buf[i] = std::sin(2.0 * M_PI * freq * i / fs);
    std::vector<float> dry = buf;
    eq.Process(buf.data(), buf.size());
    for (int i = skip; i < total; ++i) { in_sq += dry[i]*dry[i]; out_sq += buf[i]*buf[i]; }
    return 10.0f * std::log10(out_sq / in_sq);
}

int main()
{
    const float fs = 48000.0f;

    // Flat EQ is transparent.
    {
        Eq3 eq; eq.Reset(fs);
        std::vector<float> x(256); for (size_t i=0;i<x.size();++i) x[i]=std::sin(0.1f*i);
        std::vector<float> y = x; eq.Process(y.data(), y.size());
        float err = 0; for (size_t i=0;i<x.size();++i) err = std::max(err, std::fabs(x[i]-y[i]));
        CHECK(err < 1e-3f);
    }
    // Mid peak +6 dB at its center frequency.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Mid, 6.0f, 750.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 750.0f) - 6.0f) < 1.0f);
    }
    // Mid cut −6 dB at center.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Mid, -6.0f, 750.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 750.0f) - (-6.0f)) < 1.0f);
    }
    // Bass low-shelf +6 dB: well below corner ≈ +6, well above ≈ 0.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Bass, 6.0f, 200.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 40.0f) - 6.0f) < 1.5f);
        CHECK(std::fabs(band_gain_db(eq, fs, 6000.0f)) < 1.0f);
    }
    // Treble high-shelf +6 dB: well above corner ≈ +6, well below ≈ 0.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Treble, 6.0f, 3000.0f);
        CHECK(std::fabs(band_gain_db(eq, fs, 12000.0f) - 6.0f) < 1.5f);
        CHECK(std::fabs(band_gain_db(eq, fs, 200.0f)) < 1.0f);
    }
    // Going flat clears active filter history before it can reappear later.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Mid, 12.0f, 750.0f);
        float impulse[2] = {1.0f, 0.0f};
        eq.Process(impulse, 2);
        eq.SetBand(Eq3::Band::Mid, 0.0f, 750.0f);
        float flat = 0.0f;
        eq.Process(&flat, 1);
        eq.SetBand(Eq3::Band::Mid, 12.0f, 750.0f);
        float resumed = 0.0f;
        eq.Process(&resumed, 1);
        CHECK(std::fabs(resumed) < 1e-7f);
    }
    // A flat-to-active transition cannot resurrect history even when both
    // control updates happen between audio blocks.
    {
        Eq3 eq; eq.Reset(fs);
        eq.SetBand(Eq3::Band::Mid, 12.0f, 750.0f);
        float impulse[2] = {1.0f, 0.0f};
        eq.Process(impulse, 2);
        eq.SetBand(Eq3::Band::Mid, 0.0f, 750.0f);
        eq.SetBand(Eq3::Band::Mid, 12.0f, 750.0f);
        float resumed = 0.0f;
        eq.Process(&resumed, 1);
        CHECK(std::fabs(resumed) < 1e-7f);
    }
    return test_summary("test_eq3");
}
