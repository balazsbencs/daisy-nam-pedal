// TEST-04: No presets in storage → synthesises one preset per model.
// TEST-05: No presets, no models → falls back to single "Direct" bypass preset.
// TEST-06: Preset entries loaded correctly from storage.
// TEST-07: Next()/Prev() wrap-around; no divide-by-zero when count == 0.
// TEST-08: Apply() with unknown model name → engine forced to bypass.
#include "test_harness.h"
#include "fake_storage.h"
#include "../QspiStorage.h"
#include "../ModelManager.h"
#include "../PresetManager.h"
#include "../AudioEngine.h"
#include <cmath>
#include <cstring>

// Point the QSPI shim at our FakeStorage buffer.
static void set_fake(const FakeStorage& fs)
{
    daisy::QSPIHandle::g_fake_base = fs.Ptr();
}

// Build a NamPreset blob for AddEntry.
static std::vector<uint8_t> make_preset_blob(const char* mname, const char* irname,
                                              float gain, float vol, uint8_t bypass)
{
    NamPreset p{};
    strncpy(p.model_name, mname,  NAM_DATA_NAME_LEN - 1);
    strncpy(p.ir_name,    irname, NAM_DATA_NAME_LEN - 1);
    p.input_gain    = gain;
    p.output_volume = vol;
    p.bypass        = bypass;
    std::vector<uint8_t> buf(sizeof(NamPreset));
    memcpy(buf.data(), &p, sizeof(NamPreset));
    return buf;
}

static std::vector<uint8_t> make_legacy_preset_blob(const char* mname, const char* irname,
                                                     float gain, float vol, uint8_t bypass)
{
    std::vector<uint8_t> buf(74, 0);
    strncpy(reinterpret_cast<char*>(buf.data()), mname, NAM_DATA_NAME_LEN - 1);
    strncpy(reinterpret_cast<char*>(buf.data() + 31), irname, NAM_DATA_NAME_LEN - 1);
    memcpy(buf.data() + 62, &gain, sizeof(float));
    memcpy(buf.data() + 66, &vol, sizeof(float));
    buf[70] = bypass;
    return buf;
}

// ----- TEST-04 --------------------------------------------------------------

static void test_no_presets_synthesises_per_model()
{
    FakeStorage fs;
    const uint8_t dummy[] = {0x01};
    fs.AddEntry(NAM_ENTRY_MODEL, "ModelA", dummy, sizeof(dummy));
    fs.AddEntry(NAM_ENTRY_MODEL, "ModelB", dummy, sizeof(dummy));
    fs.Commit();

    set_fake(fs);
    QspiStorage  storage;  storage.Init();
    ModelManager models;   models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    CHECK_EQ(presets.Count(), 2u);
    CHECK_STR(presets.Name(0), "ModelA");
    CHECK_STR(presets.Name(1), "ModelB");
    CHECK_STR(presets.ActivePreset().model_name, "ModelA");
}

// ----- TEST-05 --------------------------------------------------------------

static void test_no_presets_no_models_makes_direct()
{
    FakeStorage fs;
    fs.Commit(); // empty partition

    set_fake(fs);
    QspiStorage  storage;  storage.Init();
    ModelManager models;   models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    CHECK_EQ(presets.Count(), 1u);
    CHECK_STR(presets.Name(0), "Direct");
    CHECK_EQ(presets.ActivePreset().bypass, 1u);
}

// ----- TEST-06 --------------------------------------------------------------

static void test_preset_entries_loaded()
{
    FakeStorage fs;
    const uint8_t dummy[] = {0x01};
    fs.AddEntry(NAM_ENTRY_MODEL, "Plexi", dummy, sizeof(dummy));

    auto blob = make_preset_blob("Plexi", "V30", 1.0f, 0.8f, 0);
    fs.AddEntry(NAM_ENTRY_PRESET, "Rock Lead", blob.data(), (uint32_t)blob.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage  storage;  storage.Init();
    ModelManager models;   models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    CHECK_EQ(presets.Count(), 1u);
    CHECK_STR(presets.Name(0), "Rock Lead");
    const NamPreset& p = presets.ActivePreset();
    CHECK_STR(p.model_name, "Plexi");
    CHECK_STR(p.ir_name,    "V30");
    CHECK(p.input_gain   > 0.99f && p.input_gain   < 1.01f);
    CHECK(p.output_volume > 0.79f && p.output_volume < 0.81f);
    CHECK_EQ(p.bypass, 0u);
}

// ----- TEST-07 --------------------------------------------------------------

static void test_navigation_wraps()
{
    FakeStorage fs;
    auto b0 = make_preset_blob("", "", 1.0f, 1.0f, 0);
    auto b1 = make_preset_blob("", "", 1.0f, 1.0f, 0);
    auto b2 = make_preset_blob("", "", 1.0f, 1.0f, 0);
    fs.AddEntry(NAM_ENTRY_PRESET, "A", b0.data(), (uint32_t)b0.size());
    fs.AddEntry(NAM_ENTRY_PRESET, "B", b1.data(), (uint32_t)b1.size());
    fs.AddEntry(NAM_ENTRY_PRESET, "C", b2.data(), (uint32_t)b2.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage  storage;  storage.Init();
    ModelManager models;   models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    CHECK_EQ(presets.Current(), 0u);
    presets.Next();
    CHECK_EQ(presets.Current(), 1u);
    presets.Next();
    CHECK_EQ(presets.Current(), 2u);
    presets.Next();                    // wraps
    CHECK_EQ(presets.Current(), 0u);
    presets.Prev();                    // wraps back
    CHECK_EQ(presets.Current(), 2u);
}

static void test_navigation_no_crash_when_empty()
{
    // PresetManager always has at least 1 preset ("Direct"), so count_ == 0 can't
    // happen after Init(). But guard is in place for robustness — verify no UB.
    // We test by building a 1-preset manager and confirming Next/Prev stay at 0.
    FakeStorage fs;
    fs.Commit();
    set_fake(fs);
    QspiStorage  storage;  storage.Init();
    ModelManager models;   models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    CHECK_EQ(presets.Count(), 1u);
    presets.Next();  // wraps to 0
    CHECK_EQ(presets.Current(), 0u);
    presets.Prev();  // wraps to 0
    CHECK_EQ(presets.Current(), 0u);
}

// ----- TEST-08 --------------------------------------------------------------

static void test_apply_unknown_model_forces_bypass()
{
    FakeStorage fs;
    // Preset references a model not in storage.
    auto blob = make_preset_blob("Phantom", "", 1.0f, 1.0f, 0);
    fs.AddEntry(NAM_ENTRY_PRESET, "Ghost", blob.data(), (uint32_t)blob.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage  storage;  storage.Init();
    ModelManager models;   models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    AudioEngine engine;
    engine.Init(48, 48000.0f);
    engine.SetBypass(false);

    presets.Apply(engine, storage, models, 48000.0f, 48);

    // Model not found → PresetManager must have engaged bypass.
    CHECK(engine.GetBypass());
}

static void test_apply_eq_forwarded()
{
    // ApplyPreset forwards explicit EQ values to the engine.
    AudioEngine engine; engine.Init(48, 48000.0f);
    QspiStorage storage;   // not Init'd; empty model/IR names skip those paths
    ModelManager models;
    PresetManager pm;
    NamPreset p{};
    p.input_gain = 1.0f; p.output_volume = 0.8f; p.bypass = 0;
    p.eq_bass_gain = 3.0f; p.eq_mid_gain = -2.0f; p.eq_treble_gain = 1.0f;
    p.eq_bass_freq = 120.0f; p.eq_mid_freq = 800.0f; p.eq_treble_freq = 3500.0f;
    pm.ApplyPreset(p, engine, storage, models, 48000.0f, 48);
    CHECK(std::fabs(engine.GetEqGain(Eq3::Band::Bass) - 3.0f)    < 1e-6f);
    CHECK(std::fabs(engine.GetEqGain(Eq3::Band::Mid)  - (-2.0f)) < 1e-6f);
    CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Mid)  - 800.0f)  < 1e-3f);
}

static void test_apply_eq_default_freq_fallback()
{
    // Zeroed freq (legacy/short blob → memset to 0) falls back to defaults.
    AudioEngine engine; engine.Init(48, 48000.0f);
    QspiStorage storage; ModelManager models; PresetManager pm;
    NamPreset p{};   // all-zero EQ
    pm.ApplyPreset(p, engine, storage, models, 48000.0f, 48);
    CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Bass)   - 100.0f)  < 1e-3f);
    CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Mid)    - 750.0f)  < 1e-3f);
    CHECK(std::fabs(engine.GetEqFreq(Eq3::Band::Treble) - 4000.0f) < 1e-3f);
}

static void test_preset_entries_load_full_eq_blob()
{
    FakeStorage fs;
    auto blob = make_preset_blob("", "", 1.0f, 0.8f, 0);
    NamPreset* raw = reinterpret_cast<NamPreset*>(blob.data());
    raw->eq_bass_gain = -3.0f;
    raw->eq_mid_gain = 2.5f;
    raw->eq_treble_gain = 4.0f;
    raw->eq_bass_freq = 120.0f;
    raw->eq_mid_freq = 800.0f;
    raw->eq_treble_freq = 3500.0f;

    fs.AddEntry(NAM_ENTRY_PRESET, "EQ Lead", blob.data(), (uint32_t)blob.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage storage; storage.Init();
    ModelManager models; models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    const NamPreset& p = presets.ActivePreset();
    CHECK(std::fabs(p.eq_bass_gain - (-3.0f)) < 1e-6f);
    CHECK(std::fabs(p.eq_mid_gain - 2.5f) < 1e-6f);
    CHECK(std::fabs(p.eq_treble_gain - 4.0f) < 1e-6f);
    CHECK(std::fabs(p.eq_bass_freq - 120.0f) < 1e-3f);
    CHECK(std::fabs(p.eq_mid_freq - 800.0f) < 1e-3f);
    CHECK(std::fabs(p.eq_treble_freq - 3500.0f) < 1e-3f);
}

static void test_legacy_preset_blob_zero_fills_eq_fields()
{
    FakeStorage fs;
    auto blob = make_legacy_preset_blob("LegacyAmp", "LegacyCab", 0.7f, 0.6f, 0);
    fs.AddEntry(NAM_ENTRY_PRESET, "Legacy", blob.data(), (uint32_t)blob.size());
    fs.Commit();

    set_fake(fs);
    QspiStorage storage; storage.Init();
    ModelManager models; models.Init(storage);
    PresetManager presets;
    presets.Init(storage, models);

    const NamPreset& p = presets.ActivePreset();
    CHECK_STR(p.model_name, "LegacyAmp");
    CHECK_STR(p.ir_name, "LegacyCab");
    CHECK(std::fabs(p.input_gain - 0.7f) < 1e-6f);
    CHECK(std::fabs(p.output_volume - 0.6f) < 1e-6f);
    CHECK(std::fabs(p.eq_bass_gain) < 1e-6f);
    CHECK(std::fabs(p.eq_mid_gain) < 1e-6f);
    CHECK(std::fabs(p.eq_treble_gain) < 1e-6f);
    CHECK(std::fabs(p.eq_bass_freq) < 1e-6f);
    CHECK(std::fabs(p.eq_mid_freq) < 1e-6f);
    CHECK(std::fabs(p.eq_treble_freq) < 1e-6f);
}

int main()
{
    test_no_presets_synthesises_per_model();
    test_no_presets_no_models_makes_direct();
    test_preset_entries_loaded();
    test_navigation_wraps();
    test_navigation_no_crash_when_empty();
    test_apply_unknown_model_forces_bypass();
    test_preset_entries_load_full_eq_blob();
    test_legacy_preset_blob_zero_fills_eq_fields();
    test_apply_eq_forwarded();
    test_apply_eq_default_freq_fallback();
    return test_summary("preset_manager");
}
