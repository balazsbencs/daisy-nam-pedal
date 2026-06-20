// TEST-02: valid partition → EntryCount matches, GetEntry works, FindEntry works.
// TEST-03: bad magic / empty → graceful failure, no crash.
//
// Compile with host shims (see tests/Makefile); does NOT need Daisy SDK.
#include "test_harness.h"
#include "fake_storage.h"
#include "../QspiStorage.h"

// The host QSPI shim uses QSPIHandle::g_fake_base; set it before each test.
static void set_fake(const FakeStorage& fs)
{
    daisy::QSPIHandle::g_fake_base = fs.Ptr();
}

// ---- TEST-02 ---------------------------------------------------------------

static void test_valid_partition()
{
    FakeStorage fs;

    const uint8_t model_blob[] = {0x01, 0x02, 0x03, 0x04};
    const float   ir_taps[]    = {0.5f, -0.5f, 0.25f};
    fs.AddEntry(NAM_ENTRY_MODEL, "TestAmp",
                model_blob, sizeof(model_blob));
    fs.AddEntry(NAM_ENTRY_IR, "TestCab",
                reinterpret_cast<const uint8_t*>(ir_taps), sizeof(ir_taps));
    fs.Commit();

    set_fake(fs);
    QspiStorage storage;
    auto s = storage.Init();
    CHECK(s == QspiStorage::Status::OK);
    CHECK_EQ(storage.EntryCount(), 2u);

    // GetEntry by index.
    const NamDataEntry* e0 = storage.GetEntry(0);
    CHECK(e0 != nullptr);
    CHECK_STR(e0->name, "TestAmp");
    CHECK_EQ(static_cast<NamEntryType>(e0->type), NAM_ENTRY_MODEL);

    const NamDataEntry* e1 = storage.GetEntry(1);
    CHECK(e1 != nullptr);
    CHECK_STR(e1->name, "TestCab");

    // Out-of-range index.
    CHECK(storage.GetEntry(2) == nullptr);

    // FindEntry by type + name.
    const NamDataEntry* found = storage.FindEntry(NAM_ENTRY_IR, "TestCab");
    CHECK(found != nullptr);
    CHECK_EQ(found, e1);

    // FindEntry — wrong type.
    CHECK(storage.FindEntry(NAM_ENTRY_MODEL, "TestCab") == nullptr);
    // FindEntry — unknown name.
    CHECK(storage.FindEntry(NAM_ENTRY_MODEL, "NoSuch") == nullptr);

    // BlobPtr: verify first 4 bytes of model blob.
    const uint8_t* blob = storage.BlobPtr(e0);
    CHECK(blob != nullptr);
    CHECK_EQ(blob[0], 0x01u);
    CHECK_EQ(blob[3], 0x04u);

    // BlobPtr by index for IR; verify first tap.
    const uint8_t* ir_raw = storage.BlobPtr(e1);
    CHECK(ir_raw != nullptr);
    float tap0;
    memcpy(&tap0, ir_raw, sizeof(float));
    CHECK(tap0 > 0.4f && tap0 < 0.6f); // ≈ 0.5f
}

// ---- TEST-03 ---------------------------------------------------------------

static void test_bad_magic()
{
    FakeStorage fs;
    // Corrupt the magic.
    uint8_t* raw = fs.MutablePtr();
    raw[0] = 0xDE; raw[1] = 0xAD; raw[2] = 0xBE; raw[3] = 0xEF;

    set_fake(fs);
    QspiStorage storage;
    auto s = storage.Init();
    CHECK(s == QspiStorage::Status::BAD_MAGIC);
    CHECK_EQ(storage.EntryCount(), 0u);
    CHECK(storage.GetEntry(0) == nullptr);
}

static void test_null_base()
{
    daisy::QSPIHandle::g_fake_base = nullptr;
    QspiStorage storage;
    auto s = storage.Init();
    // Should report an error (not crash).
    CHECK(s != QspiStorage::Status::OK);
}

static void test_blob_flash_offset()
{
    NamDataEntry e{};
    e.type   = NAM_ENTRY_PRESET;
    e.offset = 0x5000;           // 4 KiB-aligned blob offset
    e.length = sizeof(NamPreset);
    CHECK_EQ(QspiStorage::BlobFlashOffset(&e), NAM_DATA_PARTITION_OFFSET + 0x5000u);
    // Blob sits at a sector boundary so the erase only touches its own sector.
    CHECK_EQ(e.offset % NAM_DATA_SECTOR_SIZE, 0u);
}

int main()
{
    test_valid_partition();
    test_bad_magic();
    test_null_base();
    test_blob_flash_offset();
    return test_summary("qspi_storage");
}
