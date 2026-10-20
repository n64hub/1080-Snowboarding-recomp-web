# 1080° Snowboarding PC port

A native Windows port of *1080° Snowboarding* (N64, 1998) built with static
recompilation, aiming for widescreen, high frame rates, longer draw distance,
adjustable field of view, an HD texture pack, CRT filters and other optional
enhancements, all toggleable.

**No game data is included.** You need your own ROM of the USA release
(`md5 fa27089c425dbab99f19245c5c997613`).

## Status

Early work in progress.

- [x] Decode the game's USO code modules, symbol tables and relocations ([docs/USO_FORMAT.md](docs/USO_FORMAT.md))
- [x] Recompile the whole game to C with N64Recomp (3,421 functions)
- [x] Identify the game's libultra 2.0I functions (213 named) and its RSP microcode (F3DEX 1.23 family + audio)
- [x] Recompile the audio microcode with RSPRecomp
- [x] Runtime: N64ModernRuntime + RT64 + RecompFrontend menus, synced with the game's module loader
- [x] First boot: intro, title screen and attract demo run (Linux)
- [x] Windows builds (cross-compiled from Linux with llvm-mingw, see below)
- [ ] Enhancements: widescreen, 60 and 120 fps modes, draw distance, FOV, settings menu
- [ ] HD texture pack, CRT filters
- [ ] Bonus: 3D board models on board select, audio investigation

## Building

Clone with submodules (`git clone --recursive`), then:

```bash
tools/recompile.sh path/to/1080.z64
cmake -S . -B build/pc -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/pc
```

`tools/recompile.sh` builds a patched N64Recomp, generates its input from your
ROM into `build/recomp-input/`, and writes the recompiled game to
`build/RecompiledFuncs/` and its audio microcode to `build/rsp/`. CMake then
builds the game with [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime),
[RT64](https://github.com/rt64/rt64) and RecompFrontend.

On first launch the game asks for your ROM.

### Windows (cross-compiled from Linux)

After `tools/recompile.sh`, with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw),
[DirectX-Headers](https://github.com/microsoft/DirectX-Headers) and wine64 installed:

```bash
LLVM_MINGW=/opt/llvm-mingw DIRECTX_HEADERS=/opt/DirectX-Headers tools/build-windows.sh
```

The script applies the small MinGW fixes in `patches/windows-cross/` to the renderer
submodules and leaves a ready-to-zip folder in `build/win/package/`.

## Credits

- [Snowboard Kids 2: Recompiled](https://github.com/cdlewis/snowboardkids2-recomp)
  by cdlewis (GPL-3), whose runtime setup this port started from
- [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) (GPL-3), for
  menu fonts and icons

- [N64Recomp](https://github.com/N64Recomp/N64Recomp) by Mr-Wiseguy (MIT)
- [djsaunde/1080-decomp](https://github.com/djsaunde/1080-decomp), used as a
  reference while working out the module format

## License

GPL-3.0 (see [LICENSE](LICENSE)), because the runtime code it builds on is GPL-3.
