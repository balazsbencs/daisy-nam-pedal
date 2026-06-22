#include "../display/display_transfer.h"
#include "test_harness.h"

int main()
{
    uint16_t frame[500] = {};
    pedal::DisplayTransferState state;

    CHECK(!state.IsBusy());
    CHECK(!state.Start(nullptr, sizeof(frame)));
    CHECK(!state.Start(frame, 0));
    CHECK(state.Start(frame, sizeof(frame)));
    CHECK(state.IsBusy());
    CHECK(!state.Start(frame, sizeof(frame)));

    auto first = state.CurrentChunk(480);
    CHECK(first.data == reinterpret_cast<const uint8_t*>(frame));
    CHECK_EQ(first.size, 480);
    state.Advance(first.size);

    auto second = state.CurrentChunk(480);
    CHECK_EQ(second.size, 480);
    state.Advance(second.size);

    auto last = state.CurrentChunk(480);
    CHECK_EQ(last.size, 40);
    state.Advance(last.size);
    CHECK(!state.IsBusy());

    return test_summary("display_transfer");
}
