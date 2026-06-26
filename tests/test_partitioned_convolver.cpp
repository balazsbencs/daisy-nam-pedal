#include "../PartitionedConvolver.h"
#include "test_harness.h"

#include <cmath>
#include <vector>

static std::vector<float> delayed_reference(const std::vector<float>& input,
                                            const std::vector<float>& ir)
{
    std::vector<float> output(input.size(), 0.0f);
    for(size_t n = PartitionedConvolver::kBlockSize; n < output.size(); ++n)
    {
        const size_t source_n = n - PartitionedConvolver::kBlockSize;
        for(size_t k = 0; k < ir.size() && k <= source_n; ++k)
            output[n] += input[source_n - k] * ir[k];
    }
    return output;
}

static std::vector<float> process(PartitionedConvolver& conv,
                                  const std::vector<float>& input,
                                  bool in_place = false)
{
    std::vector<float> output(input.size(), 0.0f);
    for(size_t offset = 0; offset < input.size(); offset += PartitionedConvolver::kBlockSize)
    {
        if(in_place)
        {
            float block[PartitionedConvolver::kBlockSize];
            for(size_t i = 0; i < PartitionedConvolver::kBlockSize; ++i)
                block[i] = input[offset + i];
            conv.Process(block, block, PartitionedConvolver::kBlockSize);
            for(size_t i = 0; i < PartitionedConvolver::kBlockSize; ++i)
                output[offset + i] = block[i];
        }
        else
        {
            conv.Process(input.data() + offset,
                         output.data() + offset,
                         PartitionedConvolver::kBlockSize);
        }
    }
    return output;
}

static void check_close(const std::vector<float>& actual,
                        const std::vector<float>& expected)
{
    CHECK(actual.size() == expected.size());
    for(size_t i = 0; i < actual.size(); ++i)
        CHECK(std::fabs(actual[i] - expected[i]) < 1e-4f);
}

static void test_complete_max_tap_impulse_response()
{
    std::vector<float> ir(PartitionedConvolver::kMaxTaps);
    for(size_t i = 0; i < ir.size(); ++i)
        ir[i] = static_cast<float>((static_cast<int>(i % 17) - 8) * 0.002);

    std::vector<float> input(1152, 0.0f);
    input[0] = 1.0f;

    PartitionedConvolver conv;
    CHECK(conv.Init(ir.data(), ir.size()));
    check_close(process(conv, input), delayed_reference(input, ir));
}

static void test_arbitrary_signal_and_non_aligned_ir()
{
    constexpr double kPi = 3.14159265358979323846;
    std::vector<float> ir(77);
    for(size_t i = 0; i < ir.size(); ++i)
        ir[i] = 0.01f * std::cos(0.17 * i) * std::exp(-0.025 * i);

    std::vector<float> input(768);
    for(size_t i = 0; i < input.size(); ++i)
        input[i] = 0.2f * std::sin(2.0 * kPi * 13.0 * i / 257.0)
                   + 0.07f * std::cos(2.0 * kPi * 31.0 * i / 193.0);

    PartitionedConvolver conv;
    CHECK(conv.Init(ir.data(), ir.size()));
    check_close(process(conv, input), delayed_reference(input, ir));
}

static void test_reset_clears_history()
{
    const float ir[] = {0.5f, 0.25f, -0.125f};
    std::vector<float> impulse(192, 0.0f);
    impulse[0] = 1.0f;

    PartitionedConvolver conv;
    CHECK(conv.Init(ir, 3));
    const auto first = process(conv, impulse);
    conv.Reset();
    const auto second = process(conv, impulse);
    check_close(second, first);
}

static void test_in_place_matches_reference()
{
    std::vector<float> ir(PartitionedConvolver::kMaxTaps, 0.0f);
    ir[0] = 0.8f;
    ir[127] = -0.2f;
    ir[PartitionedConvolver::kMaxTaps - 1] = 0.1f;
    std::vector<float> input(1152);
    for(size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>((static_cast<int>(i % 23) - 11) * 0.01);

    PartitionedConvolver conv;
    CHECK(conv.Init(ir.data(), ir.size()));
    check_close(process(conv, input, true), delayed_reference(input, ir));
}

int main()
{
    test_complete_max_tap_impulse_response();
    test_arbitrary_signal_and_non_aligned_ir();
    test_reset_clears_history();
    test_in_place_matches_reference();
    return test_summary("partitioned_convolver");
}
