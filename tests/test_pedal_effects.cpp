#include "PedalEffects.h"
#include "test_harness.h"
#include <algorithm>
#include <cmath>
#include <vector>

static float max_abs(const std::vector<float>& v)
{
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

static void test_noise_gate_bypass_is_transparent()
{
    NoiseGate gate;
    gate.Init(48000.0f);
    gate.SetEnabled(false);
    gate.SetThresholdDb(-30.0f);

    std::vector<float> x = {0.0f, 0.1f, -0.2f, 0.3f};
    std::vector<float> y = x;
    gate.Process(y.data(), y.size());

    for (size_t i = 0; i < x.size(); ++i)
        CHECK(std::fabs(x[i] - y[i]) < 1e-7f);
}

static void test_noise_gate_reduces_signal_below_threshold()
{
    NoiseGate gate;
    gate.Init(48000.0f);
    gate.SetEnabled(true);
    gate.SetThresholdDb(-30.0f);

    std::vector<float> y(512, 0.001f);
    gate.Process(y.data(), y.size());

    CHECK(max_abs(y) < 0.0005f);
}

static void test_noise_gate_opens_above_threshold()
{
    NoiseGate gate;
    gate.Init(48000.0f);
    gate.SetEnabled(true);
    gate.SetThresholdDb(-40.0f);

    std::vector<float> y(512, 0.25f);
    gate.Process(y.data(), y.size());

    CHECK(y.back() > 0.20f);
}

static void test_compressor_bypass_is_transparent()
{
    Compressor comp;
    comp.Init(48000.0f);
    comp.SetEnabled(false);
    comp.SetParams(-18.0f, 4.0f, 5.0f, 50.0f);

    std::vector<float> x = {0.0f, 0.1f, -0.2f, 0.7f};
    std::vector<float> y = x;
    comp.Process(y.data(), y.size());

    for (size_t i = 0; i < x.size(); ++i)
        CHECK(std::fabs(x[i] - y[i]) < 1e-7f);
}

static void test_compressor_reduces_loud_signal()
{
    Compressor comp;
    comp.Init(48000.0f);
    comp.SetEnabled(true);
    comp.SetParams(-18.0f, 4.0f, 1.0f, 100.0f);

    std::vector<float> y(2048, 0.9f);
    comp.Process(y.data(), y.size());

    CHECK(y.back() < 0.65f);
    CHECK(y.back() > 0.10f);
}

static void test_delay_bypass_is_transparent()
{
    DelayLine delay;
    delay.Init(48000.0f);
    delay.SetEnabled(false);
    delay.SetParams(10.0f, 0.5f, 0.5f, 0.5f);

    std::vector<float> x = {1.0f, 0.0f, 0.0f, 0.25f};
    std::vector<float> y = x;
    delay.Process(y.data(), y.size());

    for (size_t i = 0; i < x.size(); ++i)
        CHECK(std::fabs(x[i] - y[i]) < 1e-7f);
}

static void test_delay_outputs_delayed_impulse()
{
    DelayLine delay;
    delay.Init(48000.0f);
    delay.SetEnabled(true);
    delay.SetParams(1.0f, 0.0f, 0.5f, 1.0f);

    std::vector<float> y(96, 0.0f);
    y[0] = 1.0f;
    delay.Process(y.data(), y.size());

    CHECK(std::fabs(y[0] - 0.5f) < 1e-5f);
    CHECK(std::fabs(y[48] - 0.5f) < 1e-5f);
}

int main()
{
    test_noise_gate_bypass_is_transparent();
    test_noise_gate_reduces_signal_below_threshold();
    test_noise_gate_opens_above_threshold();
    test_compressor_bypass_is_transparent();
    test_compressor_reduces_loud_signal();
    test_delay_bypass_is_transparent();
    test_delay_outputs_delayed_impulse();
    return test_summary("test_pedal_effects");
}
