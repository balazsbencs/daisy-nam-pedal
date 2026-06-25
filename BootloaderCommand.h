#pragma once

#include <cstddef>
#include <cstdint>

enum class BootloaderCommand
{
    None,
    EnterBootloader,
};

class BootloaderCommandParser
{
  public:
    BootloaderCommand Feed(const uint8_t* bytes, size_t length);
    BootloaderCommand Feed(const char* text);

  private:
    static constexpr size_t kMaxLine = 64;
    char                    line_[kMaxLine] {};
    size_t                  length_ = 0;

    BootloaderCommand FinishLine();
    void              Reset();
};
