#include "test_harness.h"

#include "../BootloaderCommand.h"

int main()
{
    {
        BootloaderCommandParser parser;
        CHECK(parser.Feed("NAM_DFU_") == BootloaderCommand::None);
        CHECK(parser.Feed("BOOT 7E57A11E\n") == BootloaderCommand::EnterBootloader);
    }

    {
        BootloaderCommandParser parser;
        CHECK(parser.Feed("hello\nNAM_DFU_BOOT nope\n") == BootloaderCommand::None);
        CHECK(parser.Feed("NAM_DFU_BOOT 7E57A11E\n") == BootloaderCommand::EnterBootloader);
    }

    {
        BootloaderCommandParser parser;
        CHECK(parser.Feed("NAM_DFU_BOOT 7E57A11E") == BootloaderCommand::None);
        CHECK(parser.Feed("\r") == BootloaderCommand::EnterBootloader);
    }

    {
        BootloaderCommandParser parser;
        CHECK(parser.Feed("NAM_DFU_BOOT 7E57A11E extra\n") == BootloaderCommand::None);
    }

    return 0;
}
