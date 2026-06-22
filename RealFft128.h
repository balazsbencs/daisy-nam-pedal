#pragma once

#include <cstddef>

#ifndef HOST_BUILD
#include "dsp/transform_functions.h"
#endif

class RealFft128
{
  public:
    static constexpr size_t kSize = 128;

    bool Init();
    void Forward(float* time_data, float* packed_spectrum);
    void Inverse(float* packed_spectrum, float* time_data);

  private:
#ifndef HOST_BUILD
    arm_rfft_fast_instance_f32 instance_{};
#endif
    bool ready_ = false;
};
