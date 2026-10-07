# Code Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the reviewed correctness defects and remove avoidable production audio-callback work.

**Architecture:** Keep the existing firmware structure. Delete the impossible live-QSPI write path, validate flash/model data at their existing boundaries, enforce the configured real-time model/block limits, and add only compile-time or fast-return CPU optimizations.

**Tech Stack:** C++17 firmware, ARM GNU toolchain, host tests with clang sanitizers, maintained Git submodule patches.

---

### Task 1: Repair the host test runner

**Files:**
- Modify: `tests/Makefile`
- Modify: `tests/test_ir_loader.cpp`

- [ ] Remove the committed conflict markers while retaining bootloader, tuner, and footswitch targets and run steps.
- [ ] Change the maximum-tap impulse test to write `FirConvolver::kMaxTaps - 1`, size its buffers from `kMaxTaps` and `kMaxBlock`, and check the corresponding delayed output sample.
- [ ] Run `make -C tests clean run`; expect all suites to build and pass under ASan/UBSan.

### Task 2: Remove false live-QSPI persistence

**Files:**
- Modify: `main.cpp`
- Modify: `QspiStorage.h`
- Modify: `QspiStorage.cpp`
- Modify: `tests/test_qspi_storage.cpp`
- Modify: `Ui.cpp`

- [ ] Update tests to remove the obsolete write-offset expectation and verify only read-side storage behavior.
- [ ] Delete `BlobFlashOffset`, `WritePreset`, and every firmware call to them.
- [ ] Make FS1 save report `Use desktop/DFU to persist` without clearing `preset_dirty`.
- [ ] Make edit commit apply the edited preset in RAM, keep `preset_dirty = true`, and report `Edit applied (RAM only)`.
- [ ] Update the edit/footer wording from save/persist language to apply/RAM language.
- [ ] Run the storage, preset, and UI-related host tests.

### Task 3: Validate the QSPI directory once at initialization

**Files:**
- Modify: `QspiStorage.cpp`
- Modify: `tests/test_qspi_storage.cpp`
- Modify: `tests/fake_storage.h` only if the existing builder cannot express malformed records.

- [ ] Add failing tests for a directory count that cannot fit in the partition, a blob range outside the partition, an invalid entry type, and a name without a null terminator.
- [ ] Confirm the new tests fail because `Init()` currently returns `OK`.
- [ ] In `Init()`, use `NAM_DATA_PARTITION_SIZE`, division-based range checks, and `memchr` to validate the directory and every entry before setting `Status::OK`.
- [ ] Re-run `test_qspi_storage` and `test_preset_manager`; expect all checks to pass.

### Task 4: Guard audio block size and bypass flat EQ

**Files:**
- Modify: `AudioEngine.h`
- Modify: `AudioEngine.cpp`
- Modify: `Eq3.h`
- Modify: `Eq3.cpp`
- Modify: `tests/test_audio_engine.cpp`
- Modify: `tests/test_eq3.cpp`

- [ ] Add a failing oversized-block test that calls `Process` with `hw::AUDIO_BLOCK_SIZE + 1` and expects silence without sanitizer errors.
- [ ] Derive `AudioEngine::kMaxBlock` from `hw::AUDIO_BLOCK_SIZE`; before touching scratch buffers, silence and return when `frames > kMaxBlock`.
- [ ] Add tests proving a flat EQ leaves samples unchanged and that disabling then re-enabling EQ does not revive stale filter state.
- [ ] Add an atomic `enabled_` flag computed from the three gains; make the audio thread clear state once when EQ becomes flat and return before the sample loop.
- [ ] Run `test_audio_engine`, `test_audio_engine_realtime_default`, and `test_eq3`.

### Task 5: Harden NAMB parsing

**Files:**
- Modify: `nam-binary-loader/namb/namb_format.h`
- Modify: `nam-binary-loader/namb/get_dsp_namb.cpp`
- Modify: `nam-binary-loader/test/test_namb.cpp`

- [ ] Add failing memory-buffer tests for null input, `weights_offset < MODEL_BLOCK_OFFSET`, an unaligned weight offset, an overflowing/excessive weight count, inconsistent `model_block_size`, and condition-DSP weights exceeding the outer weight count.
- [ ] Change `BinaryReader::check` to `n > _size - _pos`.
- [ ] Validate total size, model-block bounds, weight alignment, and weight count with subtraction/division before constructing pointers.
- [ ] Check `cdsp_weight_count <= weight_count` before copying or subtracting.
- [ ] Configure and run `nam-binary-loader`'s `test_namb`; expect all tests to pass.

### Task 6: Enforce nano-only and remove redundant float copies

**Files:**
- Modify: `nam-binary-loader/namb/get_dsp_namb.cpp`
- Modify: `NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp`
- Modify: `NeuralAmpModelerCore/tools/test/test_a2_fast.cpp` if compile-time coverage is needed.

- [ ] Under `NAM_A2_NANO_ONLY`, make A2 shape recognition and config creation accept only three channels; leave desktop/default builds supporting both three and eight.
- [ ] Under `NAM_SAMPLE_FLOAT`, point the condition input directly at `in0`, render the head directly to `out0`, and omit `_cond`/`_head_out` allocation and storage.
- [ ] Build the firmware and inspect `arm-none-eabi-nm`; expect no `A2FastModel<8>` symbols.
- [ ] Run the NAM A2 numerical tests in their default dual-model configuration.

### Task 7: Compile production diagnostics out

**Files:**
- Modify: `main.cpp`
- Modify: `Makefile`

- [ ] Define `NAM_ENABLE_AUDIO_DIAGNOSTICS` to default to `0` in source and document the opt-in build define beside the build flags.
- [ ] Compile-gate callback counters, DWT timing, peak calculations, and once-per-second diagnostic printing; leave `audio_overload` false when diagnostics are disabled.
- [ ] Build normally and inspect the callback disassembly or symbols to confirm the diagnostic globals/format string are absent.
- [ ] Build once with `-DNAM_ENABLE_AUDIO_DIAGNOSTICS=1` to confirm the profiling path still compiles.

### Task 8: Refresh maintained patches and verify everything

**Files:**
- Modify: `patches/submodules/NeuralAmpModelerCore.patch`
- Modify: `patches/submodules/nam-binary-loader.patch`

- [ ] Regenerate the two patch files mechanically from each submodule's pinned commit and current working-tree diff.
- [ ] Verify `tools/apply_submodule_patches.sh` remains idempotent with `git apply --check` against clean temporary copies or equivalent reverse/check commands.
- [ ] Run `make -C tests clean run`; expect zero failures and sanitizer errors.
- [ ] Run the NAMB and NAM core tests; expect zero failures.
- [ ] Run `make clean && make -j4`; expect a successful ARM firmware link.
- [ ] Run `git diff --check` in the root and modified submodules.
- [ ] Review `git diff --stat` and confirm no user-owned assets or unrelated changes were modified.
