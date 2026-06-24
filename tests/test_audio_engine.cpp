#include "AudioEngine.h"
#include "test_harness.h"
#include <cmath>
#include <vector>

static void test_eq_is_in_audio_path()
{
    AudioEngine eng;
    eng.Init(48, 48000.0f);
    eng.SetBypass(false);
    eng.SetInputGain(1.0f);
    eng.SetOutputVol(1.0f);
    eng.SetEqBand(Eq3::Band::Mid, 12.0f, 750.0f);

    const float fs = 48000.0f;
    const int N = 48;
    const int blocks = 200;
    double in_sq = 0.0, out_sq = 0.0;
    int idx = 0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i, ++idx) in[i] = std::sin(2.0 * M_PI * 750.0 * idx / fs);
        eng.Process(in.data(), out.data(), N);
        if (blk >= 100)
            for (int i = 0; i < N; ++i) { in_sq += in[i] * in[i]; out_sq += out[i] * out[i]; }
    }
    float gain_db = 10.0f * std::log10(out_sq / in_sq);
    CHECK(gain_db > 8.0f);
}

static void test_delay_is_after_eq()
{
    AudioEngine eng;
    eng.Init(48, 48000.0f);
    eng.SetBypass(false);
    eng.SetInputGain(1.0f);
    eng.SetOutputVol(1.0f);
    eng.SetEqBand(Eq3::Band::Mid, 0.0f, 750.0f);
    eng.SetDelay(true, 1.0f, 0.0f, 0.5f, 1.0f);

    std::vector<float> in(96, 0.0f), out(96, 0.0f);
    in[0] = 1.0f;
    eng.Process(in.data(), out.data(), 48);
    eng.Process(in.data() + 48, out.data() + 48, 48);

    CHECK(std::fabs(out[0] - 0.5f) < 1e-5f);
    CHECK(std::fabs(out[48] - 0.5f) < 1e-5f);
}

int main()
{
    test_eq_is_in_audio_path();
    test_delay_is_after_eq();
    return test_summary("test_audio_engine");
}
