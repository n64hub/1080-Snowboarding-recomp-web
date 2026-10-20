#!/bin/bash
# Turn your ROM into recompiled C code.
#   tools/recompile.sh path/to/1080.z64
# Needs: python3, git, cmake, a C++ compiler, ninja.
set -euo pipefail
ROM=${1:?usage: tools/recompile.sh path/to/1080.z64}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
N64RECOMP_REV=ffb39cdad1da5de07eaaa48bd1db4a89a7986771

mkdir -p "$ROOT/build"
# Our fixes to N64Recomp: relocations that point into other sections, and
# loads into $zero. Rebuild whenever the patch changes.
PATCH="$ROOT/patches/N64Recomp.patch"
STAMP="$ROOT/build/N64Recomp/.patch-applied"
if [ ! -d "$ROOT/build/N64Recomp/.git" ]; then
    git clone https://github.com/N64Recomp/N64Recomp "$ROOT/build/N64Recomp"
fi
if ! cmp -s "$PATCH" "$STAMP" 2>/dev/null; then
    git -C "$ROOT/build/N64Recomp" checkout -f "$N64RECOMP_REV"
    git -C "$ROOT/build/N64Recomp" submodule update --init --recursive
    git -C "$ROOT/build/N64Recomp" apply "$PATCH"
    cp "$PATCH" "$STAMP"
fi
cmake -S "$ROOT/build/N64Recomp" -B "$ROOT/build/N64Recomp/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$ROOT/build/N64Recomp/build" --target N64Recomp RSPRecomp

python3 "$ROOT/tools/gen_recomp.py" "$ROM" "$ROOT/build/recomp-input"
cat "$ROOT/recomp/us.toml" "$ROOT/build/recomp-input/hooks.toml" > "$ROOT/build/recomp-input/us.toml"
(cd "$ROOT/build/recomp-input" && "$ROOT/build/N64Recomp/build/N64Recomp" us.toml)
mkdir -p "$ROOT/build/rsp"
(cd "$ROOT/build/recomp-input" && "$ROOT/build/N64Recomp/build/RSPRecomp" aspMain.toml)
echo "Recompiled code written to build/RecompiledFuncs and build/rsp"
