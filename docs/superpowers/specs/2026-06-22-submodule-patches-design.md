# Parent-Owned Submodule Patches

## Goal

Preserve the tracked source changes currently made in `NeuralAmpModelerCore`,
`nam-binary-loader`, and `third_party/DaisySP` inside the parent repository,
without changing the upstream submodule repositories, their URLs, or the pinned
submodule commits.

The untracked `nam-binary-loader/build/` directory is generated output and is
explicitly excluded.

## Repository Layout

The parent repository will contain one binary-safe Git patch per modified
submodule:

```text
patches/submodules/
├── NeuralAmpModelerCore.patch
├── nam-binary-loader.patch
└── DaisySP.patch
```

`tools/apply_submodule_patches.sh` will apply these patches after submodules are
initialized. The README first-time setup instructions will invoke this script
after `git submodule update --init --recursive`.

## Patch Generation

Each patch will be generated from the tracked diff at the submodule's pinned
commit using Git's binary-safe diff format. Untracked and ignored files will not
be included. The patch files themselves are the durable representation of the
local submodule changes and will be tracked by the parent repository.

## Apply Script Behavior

The script will:

1. Resolve paths relative to the parent repository, independent of the caller's
   current directory.
2. Check that every expected submodule worktree and patch file exists.
3. Use `git apply --check` before applying a patch.
4. Treat a patch whose reverse applies cleanly as already applied and continue
   successfully, making repeated execution safe.
5. Stop with a clear error when neither the patch nor its reverse applies.

The script will not run `git reset`, `git clean`, checkout files, initialize
submodules, or discard unrelated work. A conflict therefore remains visible and
requires manual resolution.

## Verification

Verification will use temporary detached Git worktrees created from each pinned
submodule commit. For every patch it will:

1. Confirm the patch applies to a clean worktree.
2. Apply the patch.
3. Compare the resulting tracked binary-safe diff with the corresponding current
   submodule worktree diff.
4. Run the apply script a second time to confirm the already-applied path exits
   successfully without changing the result.

The parent repository status will also be inspected to confirm that no submodule
commit pointer or `.gitmodules` URL changed and no generated `build/` content was
added.

## Documentation

The README will explain that cloning requires submodule initialization followed
by the patch script. It will also state that the parent repository owns these
patches and that rerunning the script is safe.
