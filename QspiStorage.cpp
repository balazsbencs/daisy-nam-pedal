#include "QspiStorage.h"
#include "HardwareConfig.h"
#include "daisy_seed.h"
#include <string.h>

using namespace daisy;

QspiStorage::Status QspiStorage::Init()
{
    QSPIHandle::Config cfg;
    cfg.device = QSPIHandle::Config::Device::IS25LP064A;
    cfg.mode   = QSPIHandle::Config::Mode::MEMORY_MAPPED;
    // Pin config is set to Daisy Seed defaults by the struct initialiser;
    // the hardware pins are fixed on the Daisy Seed PCB.

    // When APP_TYPE=BOOT_QSPI, libDaisy's Init() always returns ERR (its
    // CheckProgramMemory guard refuses to re-init while executing from QSPI).
    // DaisyBoot already left QSPI in memory-mapped mode, so Init() is not
    // needed — GetData() is pure pointer arithmetic and works regardless.
    qspi_.Init(cfg);

    header_ = reinterpret_cast<const NamDataHeader*>(
        reinterpret_cast<uintptr_t>(qspi_.GetData(hw::QSPI_DATA_PARTITION_OFFSET)));

    if(!header_)
    {
        status_ = Status::BAD_MAGIC;
        return status_;
    }

    if(header_->magic != NAM_DATA_MAGIC)
    {
        status_ = Status::BAD_MAGIC;
        return status_;
    }
    if(header_->version != NAM_DATA_VERSION)
    {
        status_ = Status::VERSION_ERROR;
        return status_;
    }

    status_ = Status::OK;
    return status_;
}

uint16_t QspiStorage::EntryCount() const
{
    return (status_ == Status::OK) ? header_->count : 0;
}

const NamDataEntry* QspiStorage::GetEntry(uint16_t idx) const
{
    if(status_ != Status::OK || idx >= header_->count)
        return nullptr;

    // Directory immediately follows the header.
    auto* dir = reinterpret_cast<const NamDataEntry*>(header_ + 1);
    return &dir[idx];
}

const NamDataEntry* QspiStorage::FindEntry(NamEntryType type, const char* name) const
{
    uint16_t count = EntryCount();
    for(uint16_t i = 0; i < count; i++)
    {
        const NamDataEntry* e = GetEntry(i);
        if(e && static_cast<NamEntryType>(e->type) == type &&
           strncmp(e->name, name, NAM_DATA_NAME_LEN) == 0)
            return e;
    }
    return nullptr;
}

const uint8_t* QspiStorage::BlobPtr(const NamDataEntry* entry) const
{
    if(!entry || status_ != Status::OK)
        return nullptr;
    // Partition base in the memory-mapped address space.
    auto base = reinterpret_cast<uintptr_t>(
        const_cast<daisy::QSPIHandle&>(qspi_).GetData(hw::QSPI_DATA_PARTITION_OFFSET));
    return reinterpret_cast<const uint8_t*>(base + entry->offset);
}

const uint8_t* QspiStorage::BlobPtr(uint16_t idx) const
{
    return BlobPtr(GetEntry(idx));
}

void QspiStorage::PrintDirectory(DaisySeed& seed) const
{
    if(status_ != Status::OK)
    {
        const char* reason =
            (status_ == Status::BAD_MAGIC)    ? "bad magic / peripheral error" :
            (status_ == Status::VERSION_ERROR) ? "unsupported format version"   :
                                                 "not initialised";
        seed.PrintLine("[storage] partition error: %s", reason);
        return;
    }

    seed.PrintLine("[storage] partition OK: %u entries", (unsigned)header_->count);

    static const char* type_names[] = {"model", "IR   ", "preset"};
    for(uint16_t i = 0; i < header_->count; i++)
    {
        const NamDataEntry* e = GetEntry(i);
        const char* tname = (e->type <= NAM_ENTRY_PRESET)
                                ? type_names[e->type]
                                : "?    ";
        if(e->samplerate)
            seed.PrintLine("[storage]  [%u] %s  %-24s %6lu bytes @ 0x%05lx  %lu Hz",
                (unsigned)i, tname, e->name,
                (unsigned long)e->length, (unsigned long)e->offset,
                (unsigned long)e->samplerate);
        else
            seed.PrintLine("[storage]  [%u] %s  %-24s %6lu bytes @ 0x%05lx",
                (unsigned)i, tname, e->name,
                (unsigned long)e->length, (unsigned long)e->offset);
    }
}
