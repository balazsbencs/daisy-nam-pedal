// Host shim: satisfies #include "per/qspi.h" without Daisy SDK.
// GetData() ignores the hardware offset and returns g_fake_base — tests set this
// before constructing QspiStorage so the partition logic runs against an in-memory buf.
#pragma once
#include <cstdint>

namespace daisy {

struct QSPIHandle {
    struct Config {
        enum class Device { IS25LP064A };
        enum class Mode   { MEMORY_MAPPED };
        Device device = Device::IS25LP064A;
        Mode   mode   = Mode::MEMORY_MAPPED;
    };
    enum class Result { OK, ERR };

    // Tests point this at their FakeStorage buffer before calling QspiStorage::Init().
    static const uint8_t* g_fake_base;

    Result Init(const Config&) { return g_fake_base ? Result::OK : Result::ERR; }

    // Ignores offset: in tests, g_fake_base already points to the partition start.
    const uint8_t* GetData(uint32_t /*offset*/) const { return g_fake_base; }
};

// Definition lives in the test translation unit (defined once in test_harness_impl.h).
// Each test binary must define it.
inline const uint8_t* QSPIHandle::g_fake_base = nullptr;

} // namespace daisy
