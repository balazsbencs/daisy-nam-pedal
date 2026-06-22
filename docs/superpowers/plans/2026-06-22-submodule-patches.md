# Parent-Owned Submodule Patches Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store the current tracked edits from three upstream submodules in the parent repository and provide a safe, repeatable command that applies them after cloning.

**Architecture:** Binary-safe patch files under `patches/submodules/` are the source of truth for local changes. A small POSIX shell script maps each patch to its submodule, applies forward-applicable patches, accepts reverse-applicable patches as already installed, and refuses ambiguous/conflicting state without modifying it.

**Tech Stack:** Git submodules, `git diff --binary`, `git apply`, POSIX shell, shell integration tests.

---

## File Structure

- Create `patches/submodules/NeuralAmpModelerCore.patch`: tracked source delta for `NeuralAmpModelerCore`.
- Create `patches/submodules/nam-binary-loader.patch`: tracked source delta for `nam-binary-loader`; excludes untracked `build/`.
- Create `patches/submodules/DaisySP.patch`: tracked source delta for `third_party/DaisySP`.
- Create `tools/apply_submodule_patches.sh`: safe and idempotent patch application command.
- Create `tests/test_apply_submodule_patches.sh`: isolated Git-repository integration coverage for forward, repeated, and conflicting application.
- Modify `README.md`: add the patch step to first-time setup and explain ownership.

### Task 1: Patch Application Contract

**Files:**
- Create: `tests/test_apply_submodule_patches.sh`
- Create: `tools/apply_submodule_patches.sh`

- [ ] **Step 1: Write the failing integration test**

Create an executable shell test that constructs an isolated parent-like directory, initializes three minimal Git repositories at the production submodule paths, creates one patch for each, restores the repositories, and copies the production script into `tools/`. Its assertions must cover:

```sh
#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/submodule-patches.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

mkdir -p "$TMP/tools" "$TMP/patches/submodules" "$TMP/third_party"
cp "$ROOT/tools/apply_submodule_patches.sh" "$TMP/tools/"

make_fixture() {
    path=$1
    patch=$2
    mkdir -p "$TMP/$path"
    git -C "$TMP/$path" init -q
    git -C "$TMP/$path" config user.name Test
    git -C "$TMP/$path" config user.email test@example.invalid
    printf '%s\n' original > "$TMP/$path/value.txt"
    git -C "$TMP/$path" add value.txt
    git -C "$TMP/$path" commit -qm base
    printf '%s\n' patched > "$TMP/$path/value.txt"
    git -C "$TMP/$path" diff --binary > "$TMP/patches/submodules/$patch"
    git -C "$TMP/$path" restore value.txt
}

make_fixture NeuralAmpModelerCore NeuralAmpModelerCore.patch
make_fixture nam-binary-loader nam-binary-loader.patch
make_fixture third_party/DaisySP DaisySP.patch

mv "$TMP/patches/submodules/DaisySP.patch" "$TMP/DaisySP.patch.saved"
if "$TMP/tools/apply_submodule_patches.sh" >"$TMP/out" 2>"$TMP/err"; then
    echo "expected missing patch preflight to fail" >&2
    exit 1
fi
for path in NeuralAmpModelerCore nam-binary-loader third_party/DaisySP; do
    test "$(cat "$TMP/$path/value.txt")" = original
done
mv "$TMP/DaisySP.patch.saved" "$TMP/patches/submodules/DaisySP.patch"

"$TMP/tools/apply_submodule_patches.sh"
for path in NeuralAmpModelerCore nam-binary-loader third_party/DaisySP; do
    test "$(cat "$TMP/$path/value.txt")" = patched
done

"$TMP/tools/apply_submodule_patches.sh"

printf '%s\n' conflict > "$TMP/NeuralAmpModelerCore/value.txt"
if "$TMP/tools/apply_submodule_patches.sh" >"$TMP/out" 2>"$TMP/err"; then
    echo "expected conflicting patch state to fail" >&2
    exit 1
fi
test "$(cat "$TMP/NeuralAmpModelerCore/value.txt")" = conflict
grep -q "cannot apply" "$TMP/err"
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```sh
chmod +x tests/test_apply_submodule_patches.sh
tests/test_apply_submodule_patches.sh
```

Expected: FAIL because `tools/apply_submodule_patches.sh` does not exist.

- [ ] **Step 3: Implement the minimal patch script**

Create this executable POSIX shell script:

```sh
#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

check_input() {
    submodule=$1
    patch=$2
    worktree="$ROOT/$submodule"
    patch_file="$ROOT/patches/submodules/$patch"

    if test ! -d "$worktree" || ! git -C "$worktree" rev-parse --git-dir >/dev/null 2>&1; then
        echo "error: submodule is not initialized: $submodule" >&2
        return 1
    fi
    if test ! -f "$patch_file"; then
        echo "error: patch file is missing: patches/submodules/$patch" >&2
        return 1
    fi
}

apply_patch() {
    submodule=$1
    patch=$2
    worktree="$ROOT/$submodule"
    patch_file="$ROOT/patches/submodules/$patch"

    if git -C "$worktree" apply --check "$patch_file" 2>/dev/null; then
        git -C "$worktree" apply "$patch_file"
        echo "applied: $submodule"
    elif git -C "$worktree" apply --reverse --check "$patch_file" 2>/dev/null; then
        echo "already applied: $submodule"
    else
        echo "error: cannot apply patches/submodules/$patch to $submodule" >&2
        return 1
    fi
}

check_input NeuralAmpModelerCore NeuralAmpModelerCore.patch
check_input nam-binary-loader nam-binary-loader.patch
check_input third_party/DaisySP DaisySP.patch

apply_patch NeuralAmpModelerCore NeuralAmpModelerCore.patch
apply_patch nam-binary-loader nam-binary-loader.patch
apply_patch third_party/DaisySP DaisySP.patch
```

- [ ] **Step 4: Run the integration test and verify GREEN**

Run: `tests/test_apply_submodule_patches.sh`

Expected: exit 0; missing-input preflight changes no repository, first full invocation reports three `applied` entries, second invocation reports three `already applied` entries, and the conflict assertion succeeds.

- [ ] **Step 5: Check shell syntax**

Run:

```sh
sh -n tools/apply_submodule_patches.sh
sh -n tests/test_apply_submodule_patches.sh
git diff --check -- tools/apply_submodule_patches.sh tests/test_apply_submodule_patches.sh
```

Expected: all commands exit 0 with no output.

- [ ] **Step 6: Commit the script and test**

```sh
git add tools/apply_submodule_patches.sh tests/test_apply_submodule_patches.sh
git commit -m "build: add safe submodule patch installer"
```

### Task 2: Capture Current Submodule Diffs

**Files:**
- Create: `patches/submodules/NeuralAmpModelerCore.patch`
- Create: `patches/submodules/nam-binary-loader.patch`
- Create: `patches/submodules/DaisySP.patch`

- [ ] **Step 1: Export binary-safe tracked diffs**

```sh
mkdir -p patches/submodules
git -C NeuralAmpModelerCore diff --binary > patches/submodules/NeuralAmpModelerCore.patch
git -C nam-binary-loader diff --binary > patches/submodules/nam-binary-loader.patch
git -C third_party/DaisySP diff --binary > patches/submodules/DaisySP.patch
```

- [ ] **Step 2: Verify the generated patches contain only intended tracked paths**

Run:

```sh
git apply --numstat patches/submodules/NeuralAmpModelerCore.patch
git apply --numstat patches/submodules/nam-binary-loader.patch
git apply --numstat patches/submodules/DaisySP.patch
rg -n '(^|/)build/' patches/submodules
```

Expected: numstat lists the five NAM DSP files, three NAMB loader files, and `Source/Filters/fir.h`; the final `rg` exits 1 with no matches.

- [ ] **Step 3: Verify each patch reproduces the current diff from its pinned commit**

For each submodule, create a detached temporary worktree from `HEAD`, apply its patch, and compare its binary-safe diff byte-for-byte with the current submodule diff. Use temporary files and remove the worktree afterward:

```sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nam-core-patch.XXXXXX")
git -C NeuralAmpModelerCore worktree add --detach "$tmp/worktree" HEAD
git -C "$tmp/worktree" apply "$PWD/patches/submodules/NeuralAmpModelerCore.patch"
git -C NeuralAmpModelerCore diff --binary > "$tmp/current.diff"
git -C "$tmp/worktree" diff --binary > "$tmp/applied.diff"
cmp "$tmp/current.diff" "$tmp/applied.diff"
git -C NeuralAmpModelerCore worktree remove "$tmp/worktree"
rm "$tmp/current.diff" "$tmp/applied.diff"
rmdir "$tmp"
```

Repeat with distinct temporary directories for `nam-binary-loader` and `third_party/DaisySP`, changing the patch path accordingly. Expected: each `cmp` exits 0.

- [ ] **Step 4: Verify the production script recognizes current changes**

Run: `tools/apply_submodule_patches.sh`

Expected: exit 0 and `already applied` for all three submodules; no submodule file changes.

- [ ] **Step 5: Commit the patch artifacts**

```sh
git add patches/submodules/NeuralAmpModelerCore.patch \
        patches/submodules/nam-binary-loader.patch \
        patches/submodules/DaisySP.patch
git commit -m "build: preserve local submodule changes"
```

### Task 3: Document Clone and Apply Workflow

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add the installer to first-time setup**

Change the setup block to:

```sh
git clone <repo-url>
cd daisy-nam-pedal
git submodule update --init --recursive
tools/apply_submodule_patches.sh
```

Immediately below it, document that the upstream submodule URLs and commits are unchanged, local adaptations live in `patches/submodules/`, and rerunning the script is safe.

- [ ] **Step 2: Verify documentation and repository metadata**

Run:

```sh
rg -n 'apply_submodule_patches|patches/submodules' README.md
git diff -- .gitmodules
git diff --submodule=short -- NeuralAmpModelerCore nam-binary-loader third_party/DaisySP
git status --short -- nam-binary-loader/build patches/submodules tools/apply_submodule_patches.sh tests/test_apply_submodule_patches.sh README.md
git diff --check -- README.md
```

Expected: README contains both references; `.gitmodules` has no diff; all three submodule entries report only modified content rather than new gitlink commits; `nam-binary-loader/build/` remains untracked and unstaged; whitespace check exits 0.

- [ ] **Step 3: Run final behavioral verification**

```sh
tests/test_apply_submodule_patches.sh
tools/apply_submodule_patches.sh
sh -n tools/apply_submodule_patches.sh tests/test_apply_submodule_patches.sh
git diff --check
```

Expected: every command exits 0; integration tests cover forward, repeated, and conflict behavior; production reports all patches already applied.

- [ ] **Step 4: Commit documentation**

```sh
git add README.md
git commit -m "docs: explain submodule patch setup"
```
