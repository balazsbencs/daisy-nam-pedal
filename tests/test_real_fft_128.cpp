#include "../RealFft128.h"
#include "test_harness.h"

#include <cmath>

int main()
{
    RealFft128 fft;
    CHECK(fft.Init());

    float input[RealFft128::kSize]    = {};
    float work[RealFft128::kSize]     = {};
    float spectrum[RealFft128::kSize] = {};
    float output[RealFft128::kSize]   = {};

    constexpr double kPi = 3.14159265358979323846;
    for(size_t i = 0; i < RealFft128::kSize; ++i)
    {
        input[i] = 0.125f
                   + 0.4f * std::sin(2.0 * kPi * 7.0 * i / RealFft128::kSize)
                   - 0.2f * std::cos(2.0 * kPi * 19.0 * i / RealFft128::kSize);
        work[i] = input[i];
    }
    input[0] += 0.75f;
    work[0] = input[0];

    fft.Forward(work, spectrum);
    fft.Inverse(spectrum, output);

    for(size_t i = 0; i < RealFft128::kSize; ++i)
        CHECK(std::fabs(output[i] - input[i]) < 1e-5f);

    return test_summary("real_fft_128");
}
