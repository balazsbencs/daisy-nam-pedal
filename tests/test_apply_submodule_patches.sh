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
