# Submodule Changes Maintainer Guide Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish a contributor-facing explanation of every maintained submodule modification, its embedded-system rationale, validation requirements, and retirement condition.

**Architecture:** `docs/SUBMODULE_CHANGES.md` is the durable technical reference, organized first by target constraints and then by submodule. README links to it from the embedded-build notes; existing patch documentation remains focused on installation.

**Tech Stack:** Markdown, Git submodules, POSIX shell verification, existing host test suite.

---

## File Structure

- Create `docs/SUBMODULE_CHANGES.md`: rationale and maintenance contract for all nine patched submodule files.
- Modify `README.md`: link contributors to the guide.

### Task 1: Write the Maintainer Guide

**Files:**
- Create: `docs/SUBMODULE_CHANGES.md`

- [ ] **Step 1: Run the documentation coverage check and verify RED**

Run: `test -f docs/SUBMODULE_CHANGES.md`

Expected: exit 1 because the guide does not exist.

- [ ] **Step 2: Create the guide**

Create `docs/SUBMODULE_CHANGES.md` with this complete structure and content:

```markdown
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
JSON configuration parsing, Slimmable support, and desktop parser registration
behind `HOST_BUILD`. These paths depend on desktop facilities that firmware does
not need.

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
```

- [ ] **Step 3: Verify all patched files and key rationale are covered**

```sh
for path in NAM/dsp.cpp NAM/get_dsp.h NAM/wavenet/a2_fast.cpp \
  NAM/wavenet/a2_fast.h NAM/wavenet/model.cpp namb/get_dsp_namb.cpp \
  namb/get_dsp_namb.h tools/loadmodel.cpp Source/Filters/fir.h
do
  rg -F "$path" docs/SUBMODULE_CHANGES.md
done
rg -n 'HOST_BUILD|memory-mapped QSPI|generic WaveNet path|partitioned FFT convolver|Retire' docs/SUBMODULE_CHANGES.md
git diff --check -- docs/SUBMODULE_CHANGES.md
```

Expected: every command exits 0 and all nine patched paths are printed.

### Task 2: Link and Commit the Guide

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Verify the README discovery check is RED**

Run: `rg -F 'docs/SUBMODULE_CHANGES.md' README.md`

Expected: exit 1 because README does not yet link the guide.

- [ ] **Step 2: Add the README link**

Append to `## Notes on the embedded build`:

```markdown
For the rationale, validation requirements, and retirement criteria for every
maintained submodule adaptation, see [Maintained Submodule Changes](docs/SUBMODULE_CHANGES.md).
```

- [ ] **Step 3: Verify discovery and whitespace**

```sh
rg -F 'docs/SUBMODULE_CHANGES.md' README.md
git diff --check -- README.md docs/SUBMODULE_CHANGES.md
```

Expected: both commands exit 0.

- [ ] **Step 4: Commit contributor documentation**

```sh
git add README.md docs/SUBMODULE_CHANGES.md
git commit -m "docs: explain maintained submodule adaptations"
```

### Task 3: Verify and Publish

**Files:**
- Verify: `.gitmodules`
- Verify: `patches/submodules/*.patch`
- Verify: `tools/apply_submodule_patches.sh`
- Verify: `tests/test_apply_submodule_patches.sh`

- [ ] **Step 1: Verify patch workflow and host behavior**

```sh
tests/test_apply_submodule_patches.sh
tools/apply_submodule_patches.sh
make -C tests run
git diff --check
```

Expected: installer integration passes, production reports all patches already
applied, every host test reports zero failures, and whitespace check exits 0.

- [ ] **Step 2: Verify documentation changed no patch metadata**

```sh
test -z "$(git diff c2213d8..HEAD -- .gitmodules patches/submodules tools/apply_submodule_patches.sh tests/test_apply_submodule_patches.sh)"
git submodule status NeuralAmpModelerCore nam-binary-loader third_party/DaisySP
```

Expected: the first command exits 0. Status shows pinned commits
`a0d82a063b0b16720cbafe82a31c639559c2f7bd`,
`92c9e85bb7903ccbfd3264d3f7bcb5fee6c0892d`, and
`599511b740f8f3a9b8db72a0642aa45b8a23c3a3` without leading `+` markers.

- [ ] **Step 3: Inspect and push**

```sh
git log --oneline origin/main..HEAD
git status --short
git push origin main
git status -sb
test "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)"
```

Expected: the push advances `origin/main` without force. HEAD then matches
`origin/main`; the user's existing dirty worktree remains present.
