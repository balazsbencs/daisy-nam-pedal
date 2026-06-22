# Maintained Submodule Changes

This project carries source-level adaptations to three upstream submodules. The
upstream URLs and pinned commits remain unchanged; the durable adaptations live
in `patches/submodules/` and are installed by
`tools/apply_submodule_patches.sh`.

This guide explains why the changes exist and what contributors must preserve
when updating a submodule. Each section includes a retirement condition.

## Target constraints

The firmware runs on an STM32H750 Cortex-M7 at 480 MHz. Audio arrives in
48-sample blocks at 48 kHz, leaving a 1 ms callback deadline. Models and IRs are
stored in memory-mapped QSPI flash rather than a desktop filesystem.

Embedded code therefore loads `.namb` models directly from memory without
`std::filesystem`, file streams, or desktop JSON loading. Supported A2 WaveNet
models must use the specialized implementation because the generic WaveNet does
not provide enough realtime margin on this target. `HOST_BUILD` separates
desktop-only loading from the embedded binary path.

## NeuralAmpModelerCore

Patch: `patches/submodules/NeuralAmpModelerCore.patch`

### Embedded/host separation

Affected files:

- `NAM/dsp.cpp`
- `NAM/get_dsp.h`
- `NAM/wavenet/model.cpp`

The patch removes unused desktop file headers and places filesystem overloads,
JSON configuration parsing, Slimmable support, desktop parser registration, and
thread-local prewarm state behind `HOST_BUILD`. These paths depend on desktop
facilities that firmware does not need; in particular, the bare-metal ARM runtime
does not provide the thread-local storage hook used by `thread_local`.

The typed `get_dsp(dspData&, DspLoadOptions)` API remains available because the
embedded integration constructs a DSP after the binary loader produces typed
configuration and weights. `NamEmbeddedStubs.cpp` supplies the small set of
required host-library symbols and rejects unsupported JSON loading.

During an update, verify that ARM compilation needs no filesystem, file-stream,
Slimmable JSON, or desktop parser path, while host builds retain their normal
file and JSON overloads. Retire these guards when upstream provides an equivalent
embedded boundary.

### A2 construction without JSON

Affected files:

- `NAM/wavenet/a2_fast.cpp`
- `NAM/wavenet/a2_fast.h`

The `.namb` loader already has a typed `WaveNetConfig`, so converting it back to
JSON would restore an embedded dependency that the binary format removes. The
patch adds `create_a2_fast_config(int channels)` after the binary loader has
validated the complete A2 shape. Only three-channel A2 nano and eight-channel A2
standard models are accepted.

During an update, verify that this factory creates the same optimized model as
the JSON factory and accepts only the compiled channel counts. Retire it when
upstream exposes a typed A2 factory or the firmware stops using A2 specialization.

## nam-binary-loader

Patch: `patches/submodules/nam-binary-loader.patch`

### Memory-only embedded loading

Affected files:

- `namb/get_dsp_namb.cpp`
- `namb/get_dsp_namb.h`

Firmware calls `get_dsp_namb(const uint8_t*, size_t)` on a memory-mapped QSPI
blob. The patch guards the filesystem-path overload and its headers with
`HOST_BUILD`, leaving the buffer overload in both builds. Verify both build
configurations during updates. Retire the guards when upstream separates these
APIs equivalently.

### Strict A2 recognition

Affected file: `namb/get_dsp_namb.cpp`

With `NAM_ENABLE_A2_FAST`, the loader checks the parsed WaveNet configuration's
complete A2 signature: dimensions, three/eight channel count, bottleneck, head,
groups, 1x1 layers, kernels, dilations, LeakyReLU slopes, gating, and inactive
FiLM blocks. Inactive FiLM blocks are judged by `active == false`; irrelevant
serialized fields such as `shift == true` must not reject an inactive block.

A complete match selects `create_a2_fast_config(3)` or
`create_a2_fast_config(8)`. Any meaningful mismatch falls back to the generic
WaveNet path, preventing a merely similar architecture from being misclassified.

During updates, verify known A2 nano/standard models select the fast path and a
signature mismatch remains generic. Compare fast and generic output before
changing detector rules. Retire the detector when upstream binary loading gains
equivalent strict dispatch.

### Host model-type diagnostic

Affected file: `tools/loadmodel.cpp`

The host tool prints `typeid(*model).name()` as a quick check of fast-path
selection. The name is implementation-defined and is not a stable interface.
Retire this output when a stable diagnostic or dedicated automated coverage
replaces it.

## DaisySP

Patch: `patches/submodules/DaisySP.patch`

Affected file: `Source/Filters/fir.h`

`fir.h` uses `DSY_MIN`; the patch directly includes `Utility/dsp.h`, which
defines it. This makes the header self-contained instead of relying on a
transitive include.

The change originated when the IR stage directly used `daisysp::FIR`. The current
realtime IR path uses this repository's partitioned FFT convolver instead. When
updating DaisySP, check whether upstream fixed the include and whether any current
code still includes `fir.h` directly. Remove this patch if neither the current
firmware nor planned code requires it.

## Updating a patched submodule

For each adaptation:

1. Determine whether upstream already provides the required behavior.
2. Reapply only behavior that remains necessary; do not preserve old lines
   mechanically during conflict resolution.
3. Regenerate the relevant artifact under `patches/submodules/`.
4. Run `tests/test_apply_submodule_patches.sh`.
5. Run `make -C tests run` and the relevant ARM build.
6. For A2 changes, verify fast dispatch, generic fallback, and hardware timing
   against the 1 ms callback budget.

When retiring a patch, remove its installer mapping, artifact, and maintenance
instructions in the same change.
