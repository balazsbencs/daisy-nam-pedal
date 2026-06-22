# Full-IR Realtime Convolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the overloaded 512-tap direct FIR with deterministic partitioned FFT convolution while preserving every IR tap and adding exactly one 48-sample block of latency.

**Architecture:** A fixed-size `RealFft128` backend hides CMSIS-DSP from host tests. `PartitionedConvolver` owns precomputed IR spectra, input-spectrum history, overlap, and output-delay buffers; the existing `FirConvolver` delegates to it, leaving preset and QSPI ownership unchanged. EQ processing is bypassed to reserve callback time for NAM and the complete IR.

**Tech Stack:** C++17, CMSIS-DSP `arm_rfft_fast_f32`, Daisy Seed STM32H750, existing sanitizer-enabled host test harness, GNU Arm firmware build.

---

## File Map

- Create `RealFft128.h`: fixed 128-point real-FFT backend contract.
- Create `RealFft128.cpp`: CMSIS implementation on firmware and deterministic DFT implementation under `HOST_BUILD`.
- Create `PartitionedConvolver.h`: fixed memory layout and public init/reset/process API.
- Create `PartitionedConvolver.cpp`: packed-spectrum multiplication and overlap-add processing.
- Modify `IRLoader.h`: replace DaisySP FIR storage with `PartitionedConvolver`.
- Modify `IRLoader.cpp`: initialize and delegate to the complete-IR convolver.
- Modify `AudioEngine.cpp`: bypass post-IR EQ processing.
- Modify `Makefile`: compile the new sources and narrowly configured CMSIS FFT sources/tables.
- Modify `tests/Makefile`: compile the new host sources.
- Modify `tests/test_ir_loader.cpp`: add scalar-reference full-IR behavioral tests.
- Modify `tests/test_audio_engine.cpp`: assert EQ controls no longer alter the realtime signal path.
- Modify `main.cpp`: use the existing one-shot probe first, then restore continuous audio and display after timing passes.

### Task 1: Fixed Real FFT Backend

**Files:**
- Create: `RealFft128.h`
- Create: `RealFft128.cpp`
- Create: `tests/test_real_fft_128.cpp`
- Modify: `tests/Makefile`

- [ ] **Step 1: Write the failing round-trip test**

Create a deterministic 128-sample input containing an impulse, DC offset, and two sine components. Call `Forward`, then `Inverse`, and require maximum absolute reconstruction error below `1e-5`.

```cpp
#include "../RealFft128.h"
#include "test_harness.h"
#include <cmath>

int main()
{
    RealFft128 fft;
    CHECK(fft.Init());
    float input[RealFft128::kSize] = {};
    float work[RealFft128::kSize] = {};
    float spectrum[RealFft128::kSize] = {};
    float output[RealFft128::kSize] = {};
    for(size_t i = 0; i < RealFft128::kSize; ++i)
        input[i] = 0.125f
                   + 0.4f * std::sin(2.0 * M_PI * 7.0 * i / RealFft128::kSize)
                   - 0.2f * std::cos(2.0 * M_PI * 19.0 * i / RealFft128::kSize);
    input[0] += 0.75f;
    for(size_t i = 0; i < RealFft128::kSize; ++i) work[i] = input[i];
    fft.Forward(work, spectrum);
    fft.Inverse(spectrum, output);
    for(size_t i = 0; i < RealFft128::kSize; ++i)
        CHECK(std::fabs(output[i] - input[i]) < 1e-5f);
    return test_summary("real_fft_128");
}
```

Add `test_real_fft_128` to `BINARIES`, compile it with `../RealFft128.cpp`, and invoke it from `run`.

- [ ] **Step 2: Run the new test and verify RED**

Run: `make -C tests test_real_fft_128`

Expected: compilation fails because `RealFft128.h` does not exist.

- [ ] **Step 3: Implement the backend contract and host DFT**

Define this exact API:

```cpp
class RealFft128
{
public:
    static constexpr size_t kSize = 128;
    bool Init();
    void Forward(float* time_data, float* packed_spectrum);
    void Inverse(float* packed_spectrum, float* time_data);
private:
#ifndef HOST_BUILD
    arm_rfft_fast_instance_f32 instance_{};
#endif
    bool ready_ = false;
};
```

Use the CMSIS packed format: index 0 is real DC, index 1 is real Nyquist, and indices `2*k`, `2*k+1` are real/imaginary parts for bins 1 through 63. Under `HOST_BUILD`, calculate the forward DFT directly and reconstruct the inverse with DC, Nyquist, and conjugate bins, dividing by 128. On firmware, `Init` calls `arm_rfft_fast_init_f32(&instance_, 128)` and the transforms call `arm_rfft_fast_f32` with inverse flags 0 and 1. The backend contract returns normalized time samples from `Inverse`; CMSIS already applies its inverse scaling, so do not scale it again.

- [ ] **Step 4: Run the test and verify GREEN**

Run: `make -C tests test_real_fft_128 && ./tests/test_real_fft_128`

Expected: `real_fft_128: all tests passed` with no sanitizer diagnostics.

- [ ] **Step 5: Commit the backend**

```bash
git add RealFft128.h RealFft128.cpp tests/test_real_fft_128.cpp tests/Makefile
git commit -m "feat: add fixed 128-point real FFT backend"
```

### Task 2: Full 512-Tap Partitioned Convolution

**Files:**
- Create: `PartitionedConvolver.h`
- Create: `PartitionedConvolver.cpp`
- Modify: `tests/test_ir_loader.cpp`
- Modify: `tests/Makefile`

- [ ] **Step 1: Replace the no-crash test with failing reference tests**

Add a scalar reference helper that calculates causal convolution and prepends the required 48-sample delay:

```cpp
static std::vector<float> delayed_reference(const std::vector<float>& input,
                                            const std::vector<float>& ir)
{
    std::vector<float> output(input.size(), 0.0f);
    for(size_t n = FirConvolver::kMaxBlock; n < output.size(); ++n)
        for(size_t k = 0; k < ir.size() && k <= n - FirConvolver::kMaxBlock; ++k)
            output[n] += input[n - FirConvolver::kMaxBlock - k] * ir[k];
    return output;
}
```

Test these behaviors with complete 48-frame calls: a 512-tap impulse response, a deterministic 512-tap arbitrary signal/IR pair, a 77-tap non-aligned IR, reset clearing all history, and in-place output. Process at least `input_length + 512 + 48` samples and require every sample to differ from the scalar reference by less than `1e-4`.

- [ ] **Step 2: Run the IR test and verify RED**

Run: `make -C tests test_ir_loader && ./tests/test_ir_loader`

Expected: the 512-tap and numerical-output checks fail because the current host FIR shim emits zero and production code truncates at 128 taps.

- [ ] **Step 3: Define fixed convolver state**

Use these bounds and methods in `PartitionedConvolver.h`:

```cpp
class PartitionedConvolver
{
public:
    static constexpr size_t kBlockSize = 48;
    static constexpr size_t kFftSize = 128;
    static constexpr size_t kMaxTaps = 512;
    static constexpr size_t kMaxPartitions = 11;
    static constexpr size_t kOverlapSize = 47;
    bool Init(const float* ir, size_t tap_count);
    void Reset();
    void Process(const float* input, float* output, size_t frames);
private:
    static void MultiplyAccumulate(const float* x, const float* h, float* sum);
    RealFft128 fft_;
    alignas(16) float ir_spectra_[kMaxPartitions][kFftSize]{};
    alignas(16) float input_spectra_[kMaxPartitions][kFftSize]{};
    alignas(16) float fft_input_[kFftSize]{};
    alignas(16) float spectrum_sum_[kFftSize]{};
    alignas(16) float ifft_output_[kFftSize]{};
    float overlap_[kOverlapSize]{};
    float delayed_output_[kBlockSize]{};
    size_t active_partitions_ = 0;
    size_t history_write_ = 0;
    bool ready_ = false;
};
```

`Init` must reject null, zero, over-512, or failed FFT initialization. For each 48-tap IR partition, zero `fft_input_`, copy all remaining taps including partition 11's final 32 taps, transform it, and store the packed spectrum. Then call `Reset` without clearing `ir_spectra_`.

- [ ] **Step 4: Implement packed multiplication and processing**

Handle the two real-only bins separately, then bins 1 through 63 as complex values:

```cpp
sum[0] += x[0] * h[0];
sum[1] += x[1] * h[1];
for(size_t k = 1; k < kFftSize / 2; ++k)
{
    const size_t i = 2 * k;
    const float xr = x[i], xi = x[i + 1];
    const float hr = h[i], hi = h[i + 1];
    sum[i]     += xr * hr - xi * hi;
    sum[i + 1] += xr * hi + xi * hr;
}
```

`Process` requires exactly 48 frames; for any other frame count, copy input to output to avoid corrupting fixed block state. For a valid block, zero-pad and transform input, write its spectrum to history, clear `spectrum_sum_`, and accumulate `H[p] * X[n-p]` using `(history_write_ + kMaxPartitions - p) % kMaxPartitions`. Inverse transform, calculate the current wet block from samples 0–47 plus prior overlap, replace overlap with inverse samples 48–94, emit `delayed_output_`, store the current wet block into `delayed_output_`, and advance `history_write_` modulo 11. Copy input before writing output so in-place calls remain valid.

- [ ] **Step 5: Run focused and full host tests**

Run: `make -C tests test_ir_loader && ./tests/test_ir_loader`

Expected: all impulse, arbitrary-signal, short-IR, reset, and in-place checks pass within `1e-4`.

Run: `make -C tests run`

Expected: every host test passes without AddressSanitizer or UndefinedBehaviorSanitizer output.

- [ ] **Step 6: Commit the core**

```bash
git add PartitionedConvolver.h PartitionedConvolver.cpp tests/test_ir_loader.cpp tests/Makefile
git commit -m "feat: add full-length partitioned IR convolution"
```

### Task 3: Integrate Behind FirConvolver

**Files:**
- Modify: `IRLoader.h`
- Modify: `IRLoader.cpp`
- Modify: `tests/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Make the full-IR tests compile against production ownership**

Add `../PartitionedConvolver.cpp ../RealFft128.cpp` to `IR_SRCS`. Remove the DaisySP FIR host shim dependency from `IRLoader.h` by including `PartitionedConvolver.h` instead. The new tests should still fail until `FirConvolver` delegates to the new core.

- [ ] **Step 2: Replace direct FIR storage and remove truncation**

Delete `kRealtimeTaps`, the `daisysp::FIR` member, and all `SetIR` calls. Keep the existing public constants and API, with storage reduced to:

```cpp
PartitionedConvolver convolver_;
char name_[32] = {};
bool ready_ = false;
```

`FirConvolver::Init` validates through `convolver_.Init(ir, tap_count)`, copies the name only after success, and sets `ready_`. `Process` retains uninitialized passthrough and otherwise delegates to `convolver_.Process`. Add a public `Reset` method that calls `convolver_.Reset` for the reset test.

- [ ] **Step 3: Add narrowly configured CMSIS firmware sources**

Add `RealFft128.cpp` and `PartitionedConvolver.cpp` to `CPP_SOURCES`. Remove `arm_fir_f32.c` and `arm_fir_init_f32.c`, then add:

```make
C_DEFS += \
  -DARM_DSP_CONFIG_TABLES \
  -DARM_TABLE_TWIDDLECOEF_F32_64 \
  -DARM_TABLE_BITREVIDX_FXT_64 \
  -DARM_TABLE_TWIDDLECOEF_RFFT_F32_128

C_SOURCES += \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/CommonTables/arm_common_tables.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_f32.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_cfft_init_f32.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_f32.c \
  $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c
```

If link errors name `arm_bitreversal_32`, add only `TransformFunctions/arm_bitreversal.c`; do not enable all CMSIS tables.

- [ ] **Step 4: Verify host tests and firmware link**

Run: `make -C tests clean && make -C tests run`

Expected: all host tests pass.

Run: `make clean && make -j4`

Expected: firmware links successfully, reports no undefined CMSIS symbols, and SRAM/flash usage remains within linker limits.

- [ ] **Step 5: Commit integration**

```bash
git add IRLoader.h IRLoader.cpp RealFft128.h RealFft128.cpp PartitionedConvolver.h PartitionedConvolver.cpp Makefile tests/Makefile tests/test_ir_loader.cpp
git commit -m "feat: use partitioned convolution for complete IRs"
```

### Task 4: Remove EQ From the Realtime Path

**Files:**
- Modify: `tests/test_audio_engine.cpp`
- Modify: `AudioEngine.cpp`

- [ ] **Step 1: Change the audio-engine expectation and verify RED**

Keep the 750 Hz input and +12 dB EQ setting, but require output gain to stay within 0.1 dB of unity:

```cpp
float gain_db = 10.0f * std::log10(out_sq / in_sq);
CHECK(std::fabs(gain_db) < 0.1f);
```

Run: `make -C tests test_audio_engine && ./tests/test_audio_engine`

Expected: FAIL because the current engine applies approximately +12 dB.

- [ ] **Step 2: Bypass EQ processing**

Remove only this callback operation from `AudioEngine::Process`:

```cpp
eq_.Process(scratch_out_, frames);
```

Keep setters and preset EQ data intact so this optimization does not change serialization or UI structures.

- [ ] **Step 3: Verify focused and full tests**

Run: `make -C tests test_audio_engine && ./tests/test_audio_engine && make -C tests run`

Expected: unity-gain audio-engine test and all remaining tests pass.

- [ ] **Step 4: Commit the realtime-path change**

```bash
git add AudioEngine.cpp tests/test_audio_engine.cpp
git commit -m "perf: bypass EQ in realtime audio path"
```

### Task 5: Firmware Timing Probe

**Files:**
- Modify: `main.cpp`

- [ ] **Step 1: Configure the existing one-shot probe**

Set these diagnostics without changing any other boot behavior:

```cpp
static constexpr uint8_t kAudioDiagMode = 3;
static constexpr bool kDisplayEnabled = false;
static constexpr bool kDisableIrForTiming = false;
```

Confirm `daisy_seed.Init(true)` remains enabled and `data/ir.wav` still packs to 512 float taps.

- [ ] **Step 2: Build and flash the probe**

Run: `make clean && make -j4`

Expected: successful firmware link.

Flash with the project's established QSPI application command, reconnect `tio`, and capture the first `cpu_peak` line. Acceptance is `cpu_peak < 0.900ms`, no `!OVERLOAD`, and a complete 512-tap IR active.

- [ ] **Step 3: Stop if the timing target fails**

If peak is 0.900 ms or higher, retain one-shot mode and profile FFT, spectral accumulation, and NAM as separate cycle counters in one diagnostic build. Do not truncate the IR. Use the measurements to choose between moving NAM hot code to ITCM and increasing audio block size.

- [ ] **Step 4: Commit the passing probe configuration**

```bash
git add main.cpp
git commit -m "test: validate full IR callback timing"
```

### Task 6: Restore Continuous Pedal Operation

**Files:**
- Modify: `main.cpp`

- [ ] **Step 1: Enable continuous audio with display still isolated**

Set `kAudioDiagMode = 0`, keep `kDisplayEnabled = false`, rebuild, flash, and run for at least 60 seconds. Require continuous audio, changing input peaks, responsive controls, USB logs once per second, and no overload indication.

- [ ] **Step 2: Restore the display**

Set `kDisplayEnabled = true`, rebuild, and flash. Exercise all encoders and preset navigation while passing audio for at least 60 seconds. Require uninterrupted audio, visible control updates, and no overload indication.

- [ ] **Step 3: Run final verification**

Run: `make -C tests clean && make -C tests run && make clean && make -j4`

Expected: all host tests pass and firmware links successfully. Record the final worst-case `cpu_peak`, active 512-tap IR name, and 60-second interaction result.

- [ ] **Step 4: Commit production configuration**

```bash
git add main.cpp
git commit -m "feat: enable continuous NAM and full IR processing"
```
