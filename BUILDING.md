# Building the web page

The desktop build is in the README (Building). This is the page: the same game built with Emscripten into one web
page that draws through WebGPU, with [N64Recomp-browser](https://github.com/n64hub/N64Recomp-browser)'s web support.
The game's code is recompiled at build time from your own dump of 1080° Snowboarding (USA, big-endian `.z64`); the
page holds none of it but the recompiled code, and takes the dump from its launcher's Load ROM.

## Tools

On Linux (or WSL): [emsdk](https://emscripten.org/docs/getting_started/downloads.html) active (`emsdk install
latest`), clang, CMake 3.20+, Ninja, Python 3, and [naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga) (`cargo
install naga-cli`). N64Recomp-browser's README (Using it in a recomp, What the build needs) has the rest: spirv-opt,
and [ccache](https://ccache.dev) on `PATH`, which N64Recomp-browser's `frontend/build_speed.cmake` puts in front of
every compile.

## Build

```bash
git clone --recurse-submodules https://github.com/n64hub/1080-Snowboarding-recomp-web
cd 1080-Snowboarding-recomp-web
bash tools/build_web.sh <your USA dump>
```

`tools/build_web.sh` (its header says each step) patches the submodules, builds N64Recomp from the patched
`lib/N64ModernRuntime/N64Recomp`, makes the recompiler's input from the ROM and the recompiled game in `build-web-gen`,
as `tools/recompile.sh` does for the desktop in `build`, and builds the page in `build-web`:
`1080SnowboardingRecompiled.html`, `.js`, `.wasm` and `.data`.

The patches are the game's own (`patches/runtime/`, `patches/N64Recomp.patch`), then N64Recomp-browser's web
support (`lib/N64Recomp-browser/patches/`). The game's rt64, hlslpp, N64Recomp and RecompFrontend are newer than the
commits those patches were made on, and its own patches change some of the same lines, so for the files where the two
meet (`web/patches/files.txt`) the web's change is `web/patches/<project>.patch`, merged by hand, and what
N64Recomp-browser has changed in them since is merged in by its `apply.cmake --since`. A change there that meets the
game's own lines stops the build: merge it into `web/patches` by hand then, as `files.txt` says.
`bash tools/build_web.sh --patches` does only that step, without a ROM; N64Recomp-browser's CI runs it on every change
there.

Serve `build-web` with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`
(threads need SharedArrayBuffer), or with `coi_serviceworker.js` beside the page where headers can't be set, and open
it in a browser with WebGPU. The ROM, settings and saves are kept in the browser's storage.

## What the page leaves out

RetroAchievements: the launcher has no Achievements option on the page and `src/game/achievements.cpp` starts no
client there. MSAA: RT64 has none on WebGPU, which gives no sample positions. Everything else is the desktop
build's, and a page starts with Framerate at Display, RT64 interpolating to the display's rate, and Resolution at
Auto, the canvas's size.

## The shader list

The page specializes the shaders in `web/rt64_shaders.txt` at build time (`RT64_PREBUILT_SHADER_LIST`), so that it
never draws with RT64's slow general shader. The list is recorded by playing: N64Recomp-browser's
`tools/record_shaders.mjs` plays `tools/shader_plan.json` in the page opened with `?record-shaders`, which prints
every shader the game asks for, a page load a section:

```bash
node lib/N64Recomp-browser/tools/record_shaders.mjs tools/shader_plan.json build-web <your USA dump> web/rt64_shaders.txt
```

then build the page again. The plan reads which of the game's modules are loaded (`tools/game_state.js`) to know where
the game is. Its header says what was played and what wasn't.

`tools/play_plan.json` is the page's regular play, 2-3 minutes with every timeout counted, which N64Recomp-browser's
`tools/play_page.mjs` plays to measure the page. Its header says what it plays.
