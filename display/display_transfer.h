#pragma once

#include <cstdint>

namespace pedal {

struct DisplayTransferChunk
{
    const uint8_t* data = nullptr;
    uint32_t       size = 0;
};

class DisplayTransferState
{
  public:
    bool Start(const uint16_t* frame, uint32_t len_bytes)
    {
        if(IsBusy() || !frame || len_bytes == 0)
            return false;
        next_      = reinterpret_cast<const uint8_t*>(frame);
        remaining_ = len_bytes;
        return true;
    }

    bool IsBusy() const { return remaining_ != 0; }

    DisplayTransferChunk CurrentChunk(uint32_t max_bytes) const
    {
        if(!IsBusy() || max_bytes == 0)
            return {};
        return {next_, remaining_ < max_bytes ? remaining_ : max_bytes};
    }

    void Advance(uint32_t sent_bytes)
    {
        if(!IsBusy())
            return;
        const uint32_t advance = sent_bytes < remaining_ ? sent_bytes : remaining_;
        next_ += advance;
        remaining_ -= advance;
        if(remaining_ == 0)
            next_ = nullptr;
    }

  private:
    const uint8_t* next_      = nullptr;
    uint32_t       remaining_ = 0;
};

} // namespace pedal
