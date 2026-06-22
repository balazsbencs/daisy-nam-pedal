# Cooperative Display Transfer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split each ST7789 frame transfer into one-row service calls so ALPS EC11 encoders are polled throughout redraws.

**Architecture:** A pure `DisplayTransferState` tracks the framebuffer pointer and remaining bytes. `St7789Driver` starts RAMWR once, sends one 480-byte row per `Service()` call, and closes chip select after the final row. `Ui::Update()` services active transfers before rendering and never modifies the single framebuffer while it is in use.

**Tech Stack:** C++17, libDaisy SPI, ST7789 RGB565 framebuffer, sanitizer-enabled host tests.

---

### Task 1: Host-Tested Transfer Cursor

**Files:**
- Create: `display/display_transfer.h`
- Create: `tests/test_display_transfer.cpp`
- Modify: `tests/Makefile`

- [ ] **Step 1: Write the failing test**

```cpp
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
```

Add `test_display_transfer` to `BINARIES`, compile it from the test source, and invoke it from `run`.

- [ ] **Step 2: Verify RED**

Run: `make -C tests test_display_transfer`

Expected: compilation fails because `display/display_transfer.h` is absent.

- [ ] **Step 3: Implement the cursor**

```cpp
namespace pedal {
struct DisplayTransferChunk {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};
class DisplayTransferState {
public:
    bool Start(const uint16_t* frame, uint32_t len_bytes);
    bool IsBusy() const;
    DisplayTransferChunk CurrentChunk(uint32_t max_bytes) const;
    void Advance(uint32_t sent_bytes);
private:
    const uint8_t* next_ = nullptr;
    uint32_t remaining_ = 0;
};
}
```

`Start` rejects null, zero, and busy state. `CurrentChunk` returns at most `max_bytes`. `Advance` clamps to the remaining count and clears the pointer at completion.

- [ ] **Step 4: Verify GREEN**

Run: `make -C tests test_display_transfer && ./tests/test_display_transfer && make -C tests run`

Expected: the new test and all existing host tests pass without sanitizer diagnostics.

### Task 2: Cooperative ST7789 Driver

**Files:**
- Modify: `display/st7789_driver.h`
- Modify: `display/st7789_driver.cpp`
- Modify: `Ui.cpp`
- Modify: `Ui.h`

- [ ] **Step 1: Add the service API**

Include `display_transfer.h`, add `void Service()`, make `IsBusy()` return `transfer_.IsBusy()`, and add `DisplayTransferState transfer_`. Update comments from DMA/blocking-full-frame language to cooperative transfer language.

- [ ] **Step 2: Make PushFrame start a transfer**

Return unless `transfer_.Start(buf, len_bytes)` succeeds. Then set the full-screen window, transmit RAMWR, switch DC to data, and leave chip select asserted. Remove the frame row loop.

- [ ] **Step 3: Send one row per service call**

```cpp
void St7789Driver::Service()
{
    if(!transfer_.IsBusy()) return;
    constexpr uint32_t kRowBytes = kDisplayWidth * 2u;
    const auto chunk = transfer_.CurrentChunk(kRowBytes);
    spi_.BlockingTransmit(const_cast<uint8_t*>(chunk.data), chunk.size);
    transfer_.Advance(chunk.size);
    if(!transfer_.IsBusy()) pin_cs_.Write(true);
}
```

- [ ] **Step 4: Service before rendering**

At the start of `Ui::Update`, call `driver_.Service()`, then return if it remains busy. Only afterward check `dirty_` and the 30 FPS interval. State changes during transfer remain dirty and render after completion.

- [ ] **Step 5: Verify software**

Run: `make -C tests run && make -j4`

Expected: all host tests pass and display-enabled firmware links.

### Task 3: Hardware Validation

- [ ] **Step 1:** Flash with `./tools/flash_app.sh`.
- [ ] **Step 2:** For 60 seconds, rapidly rotate every encoder and enter browse/edit screens. Require prompt detent response and coherent frames.
- [ ] **Step 3:** Require clean audio, no `!OVERLOAD`, and `cpu_peak < 0.900 ms` with NAM and the complete 512-tap IR active.
