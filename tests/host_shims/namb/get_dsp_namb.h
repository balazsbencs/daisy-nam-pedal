// Host stub: get_dsp_namb() returns nullptr — model loading not needed for tests.
#pragma once
#include "NAM/dsp.h"
#include <memory>
#include <cstdint>

namespace nam {
inline std::unique_ptr<DSP> get_dsp_namb(const uint8_t* /*data*/, size_t /*len*/)
{
    return nullptr;
}
} // namespace nam
