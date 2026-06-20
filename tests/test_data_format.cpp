// TEST-01: struct sizes/offsets match the Python packer.
// Run on the host with the same -O0 or -O2 that clang uses.
// The static_asserts in data_format.h already catch mismatches at compile time;
// these runtime checks additionally verify field offsets.
#include "test_harness.h"
#include "../data_format.h"
#include <cstddef>

int main()
{
    // --- NamDataHeader (8 bytes) -------------------------------------------
    CHECK_EQ(sizeof(NamDataHeader), 8u);
    CHECK_EQ(offsetof(NamDataHeader, magic),   0u);
    CHECK_EQ(offsetof(NamDataHeader, version), 4u);
    CHECK_EQ(offsetof(NamDataHeader, count),   6u);

    // --- NamDataEntry (48 bytes) --------------------------------------------
    CHECK_EQ(sizeof(NamDataEntry), 48u);
    CHECK_EQ(offsetof(NamDataEntry, type),       0u);
    CHECK_EQ(offsetof(NamDataEntry, name),       1u);
    CHECK_EQ(offsetof(NamDataEntry, offset),    32u);
    CHECK_EQ(offsetof(NamDataEntry, length),    36u);
    CHECK_EQ(offsetof(NamDataEntry, samplerate),40u);
    CHECK_EQ(offsetof(NamDataEntry, reserved),  44u);

    // --- NamPreset (98 bytes, packed) ---------------------------------------
    // Python: struct.calcsize("<31s31sffB3x6f") == 98
    CHECK_EQ(sizeof(NamPreset), 98u);
    CHECK_EQ(offsetof(NamPreset, model_name),   0u);
    CHECK_EQ(offsetof(NamPreset, ir_name),      31u);
    CHECK_EQ(offsetof(NamPreset, input_gain),   62u);
    CHECK_EQ(offsetof(NamPreset, output_volume),66u);
    CHECK_EQ(offsetof(NamPreset, bypass),       70u);

    // EQ fields appended; struct grows by 6 floats (24 bytes) to 98.
    CHECK_EQ(sizeof(NamPreset), 98u);
    {
        NamPreset p{};
        p.eq_bass_gain = -3.0f; p.eq_mid_gain = 2.5f; p.eq_treble_gain = 4.0f;
        p.eq_bass_freq = 120.0f; p.eq_mid_freq = 800.0f; p.eq_treble_freq = 3500.0f;
        // Offsets of legacy fields must be unchanged.
        CHECK_EQ(offsetof(NamPreset, input_gain), 62u);
        CHECK_EQ(offsetof(NamPreset, bypass), 70u);
        // EQ block starts right after the 74-byte legacy record.
        CHECK_EQ(offsetof(NamPreset, eq_bass_gain), 74u);
    }

    return test_summary("data_format");
}
