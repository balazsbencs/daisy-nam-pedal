// QspiStorage.h — read-only access to the QSPI data partition at runtime.
//
// The partition is memory-mapped at 0x90200000; this class casts that region to
// NamDataHeader / NamDataEntry (from data_format.h) and validates the magic.
// All entry reads are zero-copy — BlobPtr() returns a direct pointer into flash.
//
// The firmware only READS. Writes happen via the bootloader's DFU (flash_data.sh).

#pragma once
#include "data_format.h"
#include "daisy_seed.h"
#include "per/qspi.h"
#include <stdint.h>
#include <stddef.h>

class QspiStorage
{
public:
    enum class Status
    {
        OK,
        BAD_MAGIC,     // partition doesn't contain the expected magic bytes
        VERSION_ERROR, // format version is newer than this firmware knows
        NOT_INIT,
    };

    // Init the QSPI peripheral in memory-mapped mode and validate the partition.
    // Call once at startup before any other method.
    Status Init();

    // Returns the last status (useful for serial diagnostics).
    Status GetStatus() const { return status_; }

    // Number of entries in the directory (0 if not Init'd or bad magic).
    uint16_t EntryCount() const;

    // Access a directory entry by index (returns nullptr if out of range).
    const NamDataEntry* GetEntry(uint16_t idx) const;

    // Find the first entry with the given type and name. Returns nullptr if
    // not found. Case-sensitive match against NamDataEntry::name.
    const NamDataEntry* FindEntry(NamEntryType type, const char* name) const;

    // Return a pointer directly into QSPI flash for the given entry's blob.
    // Valid as long as QSPI stays in memory-mapped mode (i.e. forever at runtime).
    const uint8_t* BlobPtr(const NamDataEntry* entry) const;

    // Convenience: BlobPtr by index. Returns nullptr if index out of range.
    const uint8_t* BlobPtr(uint16_t idx) const;

    // Dump all entries to the Daisy serial log (call after Init() for debugging).
    void PrintDirectory(daisy::DaisySeed& seed) const;

private:
    daisy::QSPIHandle   qspi_;
    const NamDataHeader* header_ = nullptr;
    Status              status_  = Status::NOT_INIT;
};
