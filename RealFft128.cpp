#include "RealFft128.h"

#ifdef HOST_BUILD
#include <cmath>
#endif

bool RealFft128::Init()
{
#ifdef HOST_BUILD
    ready_ = true;
#else
    ready_ = arm_rfft_fast_init_f32(&instance_, kSize) == ARM_MATH_SUCCESS;
#endif
    return ready_;
}

void RealFft128::Forward(float* time_data, float* packed_spectrum)
{
#ifdef HOST_BUILD
    constexpr double kPi = 3.14159265358979323846;
    for(size_t k = 0; k <= kSize / 2; ++k)
    {
        double real = 0.0;
        double imag = 0.0;
        for(size_t n = 0; n < kSize; ++n)
        {
            const double phase = -2.0 * kPi * k * n / kSize;
            real += time_data[n] * std::cos(phase);
            imag += time_data[n] * std::sin(phase);
        }

        if(k == 0)
            packed_spectrum[0] = static_cast<float>(real);
        else if(k == kSize / 2)
            packed_spectrum[1] = static_cast<float>(real);
        else
        {
            packed_spectrum[2 * k]     = static_cast<float>(real);
            packed_spectrum[2 * k + 1] = static_cast<float>(imag);
        }
    }
#else
    arm_rfft_fast_f32(&instance_, time_data, packed_spectrum, 0);
#endif
}

void RealFft128::Inverse(float* packed_spectrum, float* time_data)
{
#ifdef HOST_BUILD
    constexpr double kPi = 3.14159265358979323846;
    for(size_t n = 0; n < kSize; ++n)
    {
        double value = packed_spectrum[0];
        value += (n & 1U) ? -packed_spectrum[1] : packed_spectrum[1];
        for(size_t k = 1; k < kSize / 2; ++k)
        {
            const double phase = 2.0 * kPi * k * n / kSize;
            const double real  = packed_spectrum[2 * k];
            const double imag  = packed_spectrum[2 * k + 1];
            value += 2.0 * (real * std::cos(phase) - imag * std::sin(phase));
        }
        time_data[n] = static_cast<float>(value / kSize);
    }
#else
    arm_rfft_fast_f32(&instance_, packed_spectrum, time_data, 1);
#endif
}
