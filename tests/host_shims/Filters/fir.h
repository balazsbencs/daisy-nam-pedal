// Host stub: minimal daisysp::FIR<MaxTaps, BlockSize> template.
// All methods are no-ops — IRLoader compiles but produces silence in tests.
#pragma once
#include <cstddef>

namespace daisysp {
template <size_t MaxTaps, size_t MaxBlock>
class FIR {
public:
    void SetIR(const float* /*ir*/, size_t /*len*/, bool /*reverse*/) {}
    void Reset() {}
    void ProcessBlock(float* /*in*/, float* out, size_t frames)
    {
        for (size_t i = 0; i < frames; ++i) out[i] = 0.0f;
    }
};
} // namespace daisysp
