// FakeAudioEngine — records the last Apply() calls for test assertions.
#pragma once
#include <cstddef>
#include <cstring>

// Match the real AudioEngine interface PresetManager calls.
struct FakeAudioEngine
{
    bool    bypass       = false;
    float   input_gain   = 1.0f;
    float   output_vol   = 1.0f;

    void SetBypass(bool v)     { bypass = v; }
    void SetInputGain(float v) { input_gain = v; }
    void SetOutputVol(float v) { output_vol = v; }
    bool GetBypass()     const { return bypass; }
    float GetInputGain() const { return input_gain; }
    float GetOutputVol() const { return output_vol; }

    // SwapModel and SwapIR stubs — PresetManager calls these through real objects;
    // we intercept them by using FakeModelManager / FakeIRConvolver instead.
};
