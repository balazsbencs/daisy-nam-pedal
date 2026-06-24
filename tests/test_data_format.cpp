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

    // --- NamPreset (138 bytes, packed) --------------------------------------
    // Python/Rust: struct.calcsize("<31s31sffB3x6f3B1x9f") == 138
    CHECK_EQ(sizeof(NamPreset), 138u);
    CHECK_EQ(offsetof(NamPreset, model_name),   0u);
    CHECK_EQ(offsetof(NamPreset, ir_name),      31u);
    CHECK_EQ(offsetof(NamPreset, input_gain),   62u);
    CHECK_EQ(offsetof(NamPreset, output_volume),66u);
    CHECK_EQ(offsetof(NamPreset, bypass),       70u);
    CHECK_EQ(offsetof(NamPreset, eq_bass_gain), 74u);
    CHECK_EQ(offsetof(NamPreset, eq_treble_freq), 94u);
    CHECK_EQ(offsetof(NamPreset, noise_gate_enabled), 98u);
    CHECK_EQ(offsetof(NamPreset, compressor_enabled), 99u);
    CHECK_EQ(offsetof(NamPreset, delay_enabled), 100u);
    CHECK_EQ(offsetof(NamPreset, noise_gate_threshold_db), 102u);
    CHECK_EQ(offsetof(NamPreset, compressor_threshold_db), 106u);
    CHECK_EQ(offsetof(NamPreset, compressor_ratio), 110u);
    CHECK_EQ(offsetof(NamPreset, compressor_attack_ms), 114u);
    CHECK_EQ(offsetof(NamPreset, compressor_release_ms), 118u);
    CHECK_EQ(offsetof(NamPreset, delay_time_ms), 122u);
    CHECK_EQ(offsetof(NamPreset, delay_repeats), 126u);
    CHECK_EQ(offsetof(NamPreset, delay_mix), 130u);
    CHECK_EQ(offsetof(NamPreset, delay_tone), 134u);

    return test_summary("data_format");
}
