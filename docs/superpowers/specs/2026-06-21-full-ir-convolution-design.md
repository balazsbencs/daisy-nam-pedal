# Full-IR Realtime Convolution Design

## Goal

Run the complete 512-tap cabinet IR after the NAM model without exceeding the
48 kHz, 48-frame audio callback deadline. Up to one additional callback block
(1 ms) of latency is acceptable. Post-IR EQ is not required.

## Evidence and Constraints

- The callback deadline is 1.0 ms.
- NAM plus EQ, with IR disabled, peaks at approximately 0.798 ms.
- NAM plus the current 512-tap direct FIR and EQ peaks at approximately
  1.217 ms.
- The direct FIR therefore costs approximately 0.419 ms and is the confirmed
  source of the realtime overload.
- Removing EQ cannot recover the missing 0.217 ms by itself.
- The solution must preserve all IR taps and perform no allocation, locking,
  file access, or coefficient preparation in the audio callback.

## Architecture

Replace the time-domain `daisysp::FIR<512, 48>` implementation inside
`FirConvolver` with uniform partitioned convolution based on CMSIS-DSP's
single-precision real FFT.

The public `IIRConvolver` and `FirConvolver` interfaces remain unchanged, so
preset loading and `AudioEngine` ownership do not change. `FirConvolver::Init`
prepares the IR partitions outside the audio callback. `Process` performs only
fixed-size transforms, spectral multiplication, accumulation, and overlap-add.

The post-IR EQ call in `AudioEngine::Process` is bypassed to maximize realtime
margin. EQ state and preset data may remain intact for compatibility, but they
do not consume callback time.

## Convolution Layout

- Input block: 48 samples.
- IR length: up to 512 samples.
- Partition length: 48 samples.
- Partition count: `ceil(512 / 48) = 11`.
- FFT length: 128 samples, sufficient for the 95-sample linear convolution of
  two 48-sample blocks.
- Spectrum history: eleven 128-point real-FFT spectra in a circular buffer.
- IR spectra: eleven precomputed 128-point real-FFT spectra.
- Output: inverse transform of the accumulated spectrum followed by
  overlap-add and an explicit one-block output delay. Total added algorithmic
  latency is exactly 48 samples (1 ms at 48 kHz).

All arrays have compile-time bounds and are owned by the convolver. The last
IR partition is zero-padded. Shorter IRs use fewer active partitions while
retaining the same fixed maximum storage.

## Initialization and Reset

`Init` validates the IR pointer and tap count, initializes the 128-point CMSIS
RFFT instance, zeroes all history and overlap state, partitions the complete
IR, and precomputes every partition spectrum. Failure returns `false` without
publishing a partially initialized convolver.

`Reset` clears spectrum history, overlap, output delay, and circular indices
without recomputing the IR spectra.

## Realtime Processing

For each 48-sample callback block:

1. Zero-pad and transform the current NAM output.
2. Store its spectrum in the circular history.
3. Multiply each active IR spectrum by its corresponding delayed input
   spectrum and sum into one accumulator spectrum.
4. Inverse-transform the accumulator.
5. Apply CMSIS inverse-transform normalization and overlap-add.
6. Emit exactly 48 finite output samples and advance the history index.

In-place processing remains supported because the input block is transformed
before output samples overwrite it.

## Build Integration

Add only the CMSIS-DSP transform and common-table sources required by the
128-point `arm_rfft_fast_f32` path. Existing CMSIS compile flags and include
paths remain authoritative. The build must not pull in unrelated DSP modules.

## Verification

Host tests compare partitioned output with a scalar reference convolution for:

- an impulse through a 512-tap IR;
- a deterministic arbitrary signal through a 512-tap IR;
- shorter and non-partition-aligned IR lengths;
- reset behavior;
- in-place processing;
- output latency alignment and numerical tolerance.

The maximum absolute sample error against the scalar reference must be below
`1e-4` after accounting for the explicit 48-sample delay.

Firmware validation proceeds with the display disabled and the existing
one-shot timing probe. The full 512-tap IR must be active and the temporary
128-tap cap removed. Acceptance requires a callback peak below 0.90 ms to
retain at least 10% timing margin. After that passes, continuous NAM plus IR is
enabled, followed by restoring the display and checking that controls and USB
diagnostics remain responsive.

If the partitioned implementation does not achieve the timing target, no IR
truncation is accepted as the final solution. The next investigation is moving
the NAM hot path to ITCM or increasing the audio block size, measured one
change at a time.
