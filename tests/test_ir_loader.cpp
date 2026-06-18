// TEST-09: FirConvolver::Init() rejects invalid inputs.
// TEST-10: FirConvolver::Init() succeeds with valid IR; Process() doesn't crash.
#include "test_harness.h"
#include "fake_storage.h"
#include "../IRLoader.h"

// ---- TEST-09 ---------------------------------------------------------------

static void test_firconvolver_rejects_null_ir()
{
    FirConvolver conv;
    CHECK(!conv.Init(nullptr, 64, "test"));
}

static void test_firconvolver_rejects_zero_taps()
{
    float ir[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    FirConvolver conv;
    CHECK(!conv.Init(ir, 0, "test"));
}

static void test_firconvolver_rejects_too_many_taps()
{
    FirConvolver conv;
    CHECK(!conv.Init(nullptr, FirConvolver::kMaxTaps + 1, "test"));
}

// ---- TEST-10 ---------------------------------------------------------------

static void test_firconvolver_init_succeeds()
{
    float ir[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    FirConvolver conv;
    CHECK(conv.Init(ir, 8, "Unit"));
    CHECK_STR(conv.Name(), "Unit");
}

static void test_firconvolver_process_no_crash()
{
    float ir[4] = {0.5f, 0.5f, 0.0f, 0.0f};
    FirConvolver conv;
    CHECK(conv.Init(ir, 4, "Half"));

    float in[48]  = {};
    float out[48] = {};
    for (size_t i = 0; i < 48; ++i) in[i] = 1.0f;
    // Should not crash or assert.
    conv.Process(in, out, 48);
    // Stub DaisySP FIR outputs zeros — just verify no undefined behavior.
    (void)out;
}

static void test_firconvolver_process_passthrough_when_not_init()
{
    FirConvolver conv; // not Init()'d
    float in[4]  = {0.1f, 0.2f, 0.3f, 0.4f};
    float out[4] = {};
    conv.Process(in, out, 4);
    for (int i = 0; i < 4; ++i)
        CHECK(out[i] > in[i] - 0.001f && out[i] < in[i] + 0.001f);
}

static void test_load_ir_from_qspi_type_check()
{
    FakeStorage fs;
    const float taps[] = {1.0f, 0.0f, 0.0f, 0.0f};
    fs.AddEntry(NAM_ENTRY_MODEL, "NotAnIR",
                reinterpret_cast<const uint8_t*>(taps), sizeof(taps));
    fs.Commit();
    daisy::QSPIHandle::g_fake_base = fs.Ptr();

    QspiStorage storage; storage.Init();
    const NamDataEntry* e = storage.GetEntry(0);
    // Wrong type → should return nullptr.
    auto result = LoadIrFromQspi(storage, e);
    CHECK(result == nullptr);
}

static void test_load_ir_from_qspi_valid()
{
    FakeStorage fs;
    const float taps[] = {1.0f, 0.5f, 0.25f, 0.0f};
    fs.AddEntry(NAM_ENTRY_IR, "TestIR",
                reinterpret_cast<const uint8_t*>(taps), sizeof(taps));
    fs.Commit();
    daisy::QSPIHandle::g_fake_base = fs.Ptr();

    QspiStorage storage; storage.Init();
    const NamDataEntry* e = storage.FindEntry(NAM_ENTRY_IR, "TestIR");
    CHECK(e != nullptr);
    auto result = LoadIrFromQspi(storage, e);
    CHECK(result != nullptr);
    CHECK_STR(result->Name(), "TestIR");
}

int main()
{
    test_firconvolver_rejects_null_ir();
    test_firconvolver_rejects_zero_taps();
    test_firconvolver_rejects_too_many_taps();
    test_firconvolver_init_succeeds();
    test_firconvolver_process_no_crash();
    test_firconvolver_process_passthrough_when_not_init();
    test_load_ir_from_qspi_type_check();
    test_load_ir_from_qspi_valid();
    return test_summary("ir_loader");
}
