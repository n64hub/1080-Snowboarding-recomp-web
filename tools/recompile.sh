#!/bin/bash
# Turn your ROM into recompiled C code.
#   tools/recompile.sh path/to/1080.z64
# Needs: python3, git, cmake, a C++ compiler, ninja.
set -euo pipefail
ROM=${1:?usage: tools/recompile.sh path/to/1080.z64}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
N64RECOMP_REV=ffb39cdad1da5de07eaaa48bd1db4a89a7986771

mkdir -p "$ROOT/build"
if [ ! -x "$ROOT/build/N64Recomp/build/N64Recomp" ]; then
    rm -rf "$ROOT/build/N64Recomp"
    git clone https://github.com/N64Recomp/N64Recomp "$ROOT/build/N64Recomp"
    git -C "$ROOT/build/N64Recomp" checkout "$N64RECOMP_REV"
    git -C "$ROOT/build/N64Recomp" submodule update --init --recursive
    git -C "$ROOT/build/N64Recomp" apply "$ROOT/patches/N64Recomp-cross-section-relocs.patch"
    cmake -S "$ROOT/build/N64Recomp" -B "$ROOT/build/N64Recomp/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/build/N64Recomp/build" --target N64Recomp
fi

python3 "$ROOT/tools/gen_recomp.py" "$ROM" "$ROOT/build/recomp-input"
(cd "$ROOT/recomp" && "$ROOT/build/N64Recomp/build/N64Recomp" us.toml)
echo "Recompiled code written to build/RecompiledFuncs"
