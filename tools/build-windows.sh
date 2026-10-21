#!/bin/bash
# Cross-compile the Windows build on Linux with llvm-mingw.
#
#   tools/build-windows.sh            (after tools/recompile.sh ROM)
#
# Needs: llvm-mingw (https://github.com/mstorsjo/llvm-mingw) in $LLVM_MINGW,
# DirectX-Headers (https://github.com/microsoft/DirectX-Headers) in
# $DIRECTX_HEADERS, wine64 (runs build-time tools), cmake and ninja.
# Output: build/win/package/1080SnowboardingRecompiled (zip it to share).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${LLVM_MINGW:?set LLVM_MINGW to the llvm-mingw directory}"
: "${DIRECTX_HEADERS:?set DIRECTX_HEADERS to a DirectX-Headers checkout}"

# Small MinGW fixes to the renderer submodules (no-ops on a native Windows build).
apply_patch() {
    local dir=$1 patch=$2
    if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
        return  # already applied
    fi
    git -C "$dir" apply "$patch"
}
apply_patch "$ROOT/lib/rt64" "$ROOT/patches/windows-cross/rt64.patch"
apply_patch "$ROOT/lib/rt64/src/contrib/plume" "$ROOT/patches/windows-cross/plume.patch"

cmake -S "$ROOT" -B "$ROOT/build/win" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-clang.cmake" \
    -DLLVM_MINGW="$LLVM_MINGW" -DDIRECTX_HEADERS="$DIRECTX_HEADERS"
cmake --build "$ROOT/build/win"

PKG="$ROOT/build/win/package/1080SnowboardingRecompiled"
rm -rf "$PKG"
mkdir -p "$PKG"
cd "$ROOT/build/win"
cp 1080SnowboardingRecompiled.exe SDL2.dll dxcompiler.dll dxil.dll recompcontrollerdb.txt "$PKG/"
cp -r assets "$PKG/"
VERSION=$(sed -n 's/^GAME_VERSION:STRING=//p' CMakeCache.txt)
sed "1s/(early)/(v$VERSION, early)/" "$ROOT/tools/windows-readme.txt" > "$PKG/README.txt"
"$LLVM_MINGW/bin/llvm-strip" "$PKG/1080SnowboardingRecompiled.exe"
# Same program with a console window that shows the log, for troubleshooting.
python3 - "$PKG/1080SnowboardingRecompiled.exe" "$PKG/1080SnowboardingRecompiled-log.exe" <<'PY'
import struct, sys
data = bytearray(open(sys.argv[1], "rb").read())
pe = struct.unpack_from("<I", data, 0x3C)[0]
subsystem = pe + 24 + 68  # PE signature + COFF header, then optional header field
assert struct.unpack_from("<H", data, subsystem)[0] == 2  # GUI
struct.pack_into("<H", data, subsystem, 3)  # console
open(sys.argv[2], "wb").write(data)
PY
echo "Packaged to $PKG"
