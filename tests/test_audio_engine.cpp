#include "AudioEngine.h"
#include "test_harness.h"
#include <cmath>
#include <vector>

int main()
{
    AudioEngine eng;
    eng.Init(48, 48000.0f);
    eng.SetBypass(false);
    eng.SetInputGain(1.0f);
    eng.SetOutputVol(1.0f);
    // EQ controls remain writable, but EQ is omitted from the realtime path.
    eng.SetEqBand(Eq3::Band::Mid, 12.0f, 750.0f);

    const float fs = 48000.0f; const int N = 48; const int blocks = 200;
    double in_sq = 0, out_sq = 0; int idx = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i, ++idx) in[i] = std::sin(2.0*M_PI*750.0*idx/fs);
        eng.Process(in.data(), out.data(), N);
        if (blk >= 100) for (int i=0;i<N;++i){ in_sq+=in[i]*in[i]; out_sq+=out[i]*out[i]; }
    }
    float g = 10.0f * std::log10(out_sq / in_sq);
    CHECK(std::fabs(g) < 0.1f);
    return test_summary("test_audio_engine");
}
