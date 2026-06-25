#include "BootloaderCommand.h"

#include <cstring>

namespace
{
constexpr const char* kBootloaderCommand = "NAM_DFU_BOOT 7E57A11E";
}

BootloaderCommand BootloaderCommandParser::Feed(const char* text)
{
    if (text == nullptr)
        return BootloaderCommand::None;

    return Feed(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
}

BootloaderCommand BootloaderCommandParser::Feed(const uint8_t* bytes, size_t length)
{
    if (bytes == nullptr)
        return BootloaderCommand::None;

    for (size_t i = 0; i < length; ++i)
    {
        const char ch = static_cast<char>(bytes[i]);
        if (ch == '\n' || ch == '\r')
        {
            const BootloaderCommand result = FinishLine();
            if (result != BootloaderCommand::None)
                return result;
            continue;
        }

        if (length_ + 1 >= kMaxLine)
        {
            Reset();
            continue;
        }

        line_[length_++] = ch;
        line_[length_]   = '\0';
    }

    return BootloaderCommand::None;
}

BootloaderCommand BootloaderCommandParser::FinishLine()
{
    const bool matches = std::strcmp(line_, kBootloaderCommand) == 0;
    Reset();
    return matches ? BootloaderCommand::EnterBootloader : BootloaderCommand::None;
}

void BootloaderCommandParser::Reset()
{
    length_  = 0;
    line_[0] = '\0';
}
