// ModelManager.h — enumerate NAM model entries from QSPI, load on demand.
//
// Stage-then-swap pattern: Load() reads the .namb blob from memory-mapped QSPI
// (zero-copy), constructs and resets the NAM DSP object, then atomically
// swaps it into AudioEngine. The old model is disposed in the main loop.
// Audio keeps running on the previous model during the ~10-15 ms load.

#pragma once
#include "QspiStorage.h"
#include "AudioEngine.h"
#include "data_format.h"
#include <stdint.h>

class ModelManager
{
public:
    static constexpr uint8_t kMaxModels = 32;

    // Scan QspiStorage for NAM_ENTRY_MODEL entries.
    void Init(QspiStorage& storage);

    uint8_t Count()   const { return count_; }
    uint8_t Current() const { return current_; }

    // Name of entry i (nullptr if out of range).
    const char* Name(uint8_t i) const;

    // Load model at index i into AudioEngine without synchronous prewarming.
    // Returns true on success.
    bool Load(uint8_t i, AudioEngine& engine, float sample_rate, size_t block_size);

private:
    QspiStorage*       storage_ = nullptr;
    const NamDataEntry* entries_[kMaxModels] = {};
    uint8_t            count_   = 0;
    uint8_t            current_ = 0;
};
