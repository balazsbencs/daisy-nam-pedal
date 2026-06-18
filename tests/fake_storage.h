// FakeStorage — in-memory QSPI partition for unit tests.
// Build a partition image in RAM, hand a pointer to firmware code under test.
#pragma once
#include "../data_format.h"
#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>

class FakeStorage
{
public:
    static constexpr size_t kPartitionSize = 256 * 1024; // 256 KB for tests

    FakeStorage()
    {
        memset(buf_, 0xFF, sizeof(buf_));
        // Write header at offset 0 (will be finalised in Commit()).
    }

    // Add a blob entry. Returns the entry index. Data is copied into the buffer.
    int AddEntry(NamEntryType type, const char* name,
                 const uint8_t* data, uint32_t length,
                 uint32_t samplerate = 0)
    {
        assert(entry_count_ < 64 && "too many entries for FakeStorage");

        // Blobs start after the header + directory; 4 KB-aligned.
        uint32_t blob_offset = AlignUp(kDirEnd() + blob_cursor_, 4096);
        assert(blob_offset + length < kPartitionSize);

        memcpy(buf_ + blob_offset, data, length);

        NamDataEntry& e = entries_[entry_count_];
        e.type = static_cast<uint8_t>(type);
        strncpy(e.name, name, NAM_DATA_NAME_LEN - 1);
        e.name[NAM_DATA_NAME_LEN - 1] = '\0';
        e.offset     = blob_offset;
        e.length     = length;
        e.samplerate = samplerate;
        e.reserved   = 0;

        blob_cursor_ = blob_offset + length - kDirEnd();
        return entry_count_++;
    }

    // Write header + directory into buf_ so the firmware readers see it.
    void Commit()
    {
        NamDataHeader hdr;
        hdr.magic   = NAM_DATA_MAGIC;
        hdr.version = NAM_DATA_VERSION;
        hdr.count   = static_cast<uint16_t>(entry_count_);
        memcpy(buf_, &hdr, sizeof(hdr));
        memcpy(buf_ + sizeof(hdr), entries_, entry_count_ * sizeof(NamDataEntry));
    }

    // Raw pointer to the partition buffer — pass to code under test.
    const uint8_t* Ptr() const { return buf_; }
    uint8_t*       MutablePtr()  { return buf_; }

private:
    static constexpr size_t kMaxEntries = 64;

    static uint32_t AlignUp(uint32_t v, uint32_t align)
    {
        return (v + align - 1) & ~(align - 1);
    }
    uint32_t kDirEnd() const
    {
        return static_cast<uint32_t>(sizeof(NamDataHeader) + kMaxEntries * sizeof(NamDataEntry));
    }

    uint8_t      buf_[kPartitionSize] = {};
    NamDataEntry entries_[kMaxEntries] = {};
    int          entry_count_ = 0;
    uint32_t     blob_cursor_ = 0;
};
