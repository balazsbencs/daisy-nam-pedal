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
check_input third_party/libDaisy libDaisy.patch

apply_patch NeuralAmpModelerCore NeuralAmpModelerCore.patch
apply_patch nam-binary-loader nam-binary-loader.patch
apply_patch third_party/DaisySP DaisySP.patch
apply_patch third_party/libDaisy libDaisy.patch
