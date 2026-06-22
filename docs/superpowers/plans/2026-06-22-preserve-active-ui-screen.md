# Preserve the Active UI Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent the once-per-second diagnostics refresh from replacing Browse or Edit with the Performance screen.

**Architecture:** Put the refresh decision in a tiny hardware-independent helper whose inputs are the existing `browsing` and `editing` flags. The diagnostics block calls `PushPerformanceScreen()` only when that helper reports Performance mode, preserving all current transition and rendering behavior.

**Tech Stack:** C++17, GNU Make, host-side AddressSanitizer/UndefinedBehaviorSanitizer tests, Daisy firmware build

---

### Task 1: Guard the periodic Performance refresh

**Files:**
- Create: `ui_mode.h`
- Create: `tests/test_ui_mode.cpp`
- Modify: `tests/Makefile`
- Modify: `main.cpp` near the project includes and once-per-second diagnostics block

- [ ] **Step 1: Write the failing regression test**

Create `tests/test_ui_mode.cpp`:

```cpp
#include "ui_mode.h"
#include <cassert>
#include <cstdio>

int main()
{
    assert(ShouldRefreshPerformanceScreen(false, false));
    assert(!ShouldRefreshPerformanceScreen(true, false));
    assert(!ShouldRefreshPerformanceScreen(false, true));
    assert(!ShouldRefreshPerformanceScreen(true, true));
    std::puts("test_ui_mode: PASS");
    return 0;
}
```

Add `test_ui_mode` to `BINARIES` in `tests/Makefile`, add this target:

```make
test_ui_mode: test_ui_mode.cpp ../ui_mode.h
	$(CXX) $(CXXFLAGS) $< -o $@
```

and add this invocation to `run` before the Python tools test:

```make
	@echo "=== test_ui_mode ==="
	./test_ui_mode
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C tests test_ui_mode`

Expected: compilation fails because `ui_mode.h` does not exist.

- [ ] **Step 3: Add the minimal mode helper**

Create `ui_mode.h`:

```cpp
#pragma once

constexpr bool ShouldRefreshPerformanceScreen(bool browsing, bool editing)
{
    return !browsing && !editing;
}
```

- [ ] **Step 4: Run the regression test to verify it passes**

Run: `make -C tests test_ui_mode && ./tests/test_ui_mode`

Expected: `test_ui_mode: PASS`.

- [ ] **Step 5: Use the helper in the diagnostics refresh**

Add this include to `main.cpp`:

```cpp
#include "ui_mode.h"
```

Replace the unconditional diagnostics refresh:

```cpp
PushPerformanceScreen();
```

with:

```cpp
if (ShouldRefreshPerformanceScreen(browsing, editing))
    PushPerformanceScreen();
```

- [ ] **Step 6: Run complete verification**

Run: `make -C tests run`

Expected: all host tests pass, including `test_ui_mode: PASS`.

Run: `make -j2`

Expected: firmware build exits successfully and produces the normal NamPlatform firmware artifacts.

Run: `git diff --check`

Expected: no output and exit status 0.

- [ ] **Step 7: Commit the fix**

```bash
git add ui_mode.h tests/test_ui_mode.cpp tests/Makefile main.cpp
git commit -m "fix: preserve browse and edit screens"
```
