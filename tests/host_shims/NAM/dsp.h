// Host stub: minimal nam::DSP interface. No actual NAM code needed for tests.
#pragma once
#include <cstddef>

namespace nam {
class DSP {
public:
    virtual ~DSP() = default;
    // Real NAM API: float** (multi-channel buffer pointers).
    virtual void process(float** inputs, float** outputs, int num_frames) {
        if (inputs && outputs && inputs[0] && outputs[0])
            for (int i = 0; i < num_frames; ++i) outputs[0][i] = inputs[0][i];
    }
    virtual void ResetAndPrewarm(double /*sample_rate*/, int /*block_size*/) {}
};
} // namespace nam
