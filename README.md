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
- [x] Recompile the whole game to C with N64Recomp (3,435 functions; 38 low-level libultra routines left to the runtime)
- [ ] Runtime: N64ModernRuntime + RT64, hooks into the game's module loader
- [ ] First boot on Windows
- [ ] Enhancements: widescreen, 60 and 120 fps modes, draw distance, FOV, settings menu
- [ ] HD texture pack, CRT filters
- [ ] Bonus: 3D board models on board select, audio investigation

## Recompiling

```bash
tools/recompile.sh path/to/1080.z64
```

This builds a patched N64Recomp, generates its input from your ROM into
`build/recomp-input/`, and writes the recompiled C code to
`build/RecompiledFuncs/`.

## Credits

- [N64Recomp](https://github.com/N64Recomp/N64Recomp) by Mr-Wiseguy (MIT)
- [djsaunde/1080-decomp](https://github.com/djsaunde/1080-decomp), used as a
  reference while working out the module format
