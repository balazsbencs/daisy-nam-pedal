# Submodule Changes Maintainer Guide — Design

## Goal

Create a contributor-facing guide that explains what this repository changes in
each upstream submodule, why those changes were necessary for the Daisy Seed
firmware, how contributors can validate them, and when a patch can be retired.

This document complements the patch-installation instructions. It describes the
technical adaptations rather than repeating how the parent-owned patch mechanism
works.

## Location and Discovery

The guide will live at `docs/SUBMODULE_CHANGES.md`. The README's embedded-build
notes will link to it so contributors encounter the rationale from the existing
architecture documentation.

## Content

The guide will start with the constraints that caused the divergence:

- the STM32H750 embedded toolchain cannot depend on desktop filesystem and file
  stream APIs;
- models are loaded directly from memory-mapped QSPI as `.namb` buffers;
- audio runs in 48-sample blocks at 48 kHz, leaving a 1 ms realtime budget;
- the A2-specialized WaveNet implementation is required to make supported A2
  models viable on this target.

It will then describe each maintained patch by intent and affected files.

### NeuralAmpModelerCore

- Guard filesystem, file-stream, JSON parsing, Slimmable, and desktop parser
  registration paths with `HOST_BUILD` where they are not required by the
  embedded binary-loading path.
- Keep the typed `dspData` construction API available to embedded code.
- Expose `create_a2_fast_config(int channels)` so a validated binary WaveNet
  configuration can instantiate the optimized A2 model without reconstructing
  JSON. Only channel counts 3 and 8 are valid.

### nam-binary-loader

- Keep the memory-buffer loader available on embedded builds while restricting
  path/file-stream loading to `HOST_BUILD`.
- Strictly recognize A2 nano and A2 standard WaveNet configurations after binary
  parsing, including architecture dimensions, kernels, dilations, activations,
  gating, groups, and inactive FiLM blocks.
- Select the A2 fast configuration only after the complete signature matches;
  otherwise preserve the generic WaveNet path.
- Print the concrete loaded model type in the host `loadmodel` diagnostic so
  maintainers can confirm fast-path selection.

### DaisySP

- Include `Utility/dsp.h` directly from `Source/Filters/fir.h` because the header
  uses `DSY_MIN` and must not rely on transitive includes.
- Record that this fix originated while the firmware used DaisySP's direct FIR
  implementation. The runtime IR path now uses the repository's partitioned FFT
  convolver, so maintainers should reassess whether this patch is still required
  when updating DaisySP.

## Maintenance Guidance

For each submodule, the guide will identify:

- the behavior that must remain intact during an upstream update;
- focused source-level and build checks for the adaptation;
- likely conflict areas;
- an explicit retirement condition, such as upstream adoption, removal of the
  relevant dependency, or replacement of the optimized path.

The guide will distinguish verified behavior from historical context. It will
not claim the DaisySP FIR remains in the current realtime signal path.

## Validation

Documentation verification will check that:

- all nine patched source files are accounted for;
- the guide names `HOST_BUILD`, the memory-buffer `.namb` API, the strict A2
  fallback behavior, and the DaisySP historical-status caveat;
- README links to the guide;
- Markdown has no placeholders or whitespace errors.

The existing submodule-patch integration test and full host test suite will be
run before pushing. Patch contents, gitlink commit hashes, and `.gitmodules` URLs
will remain unchanged by this documentation task.
