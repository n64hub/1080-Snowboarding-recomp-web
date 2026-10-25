// Evaluated in the page by N64Recomp-browser's tools/play_page.mjs (tools/play_plan.json) and tools/record_shaders.mjs
// (tools/shader_plan.json), with n64r, RDRAM words at N64 addresses. The game has no symbols, so what it is showing is
// read from the kernel's module slot table (src/main/register_overlays.cpp): a slot a ROM file, by its index
// (build-web-gen/recomp-input/modules.json), holding each section's address once the module is loaded.
//
//   loaded(name)  whether a module is loaded: "titproc" (the title), "mgrproc" (the menus), "charsel" (rider and board
//       select), "game" (a race or run), "mapN" (course N's data, 1 to 10), "trnproc" (training), ...
//   modules()  the loaded ones of those, for the console.
//   untilLoaded(name, ms) / untilGone(name, ms)  resolves once the module is loaded or gone, or after ms.
// A slot keeps its addresses once its module is put away: the title's demo on Crystal Lake leaves map7 in after another
// course loads, so the plans wait for the line the game prints as it loads a course ("map3_data.uso loaded").
(() => {
  const SLOTS = 0x800130A0, STRIDE = 0x9C, TEXT = 0x4C, DATA = 0x50;
  const INDEX = {
    gui: 18, n64proc: 19, titproc: 20, eddproc: 21, mgrproc: 22, arcproc: 23, h2hproc: 24, timproc: 25, trkproc: 26,
    trnproc: 27, genproc: 28, charsel: 30, viewrec: 32, nameent: 33, charmap: 35, game: 46, snoweffects: 60,
  };
  for (let n = 1; n <= 10; n++) INDEX['map' + n] = 35 + n;
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const until = async (test, ms) => {
    const end = Date.now() + ms;
    while (!test() && Date.now() < end) await sleep(50);
    return test();
  };

  window.loaded = (name) => {
    const slot = SLOTS + INDEX[name] * STRIDE;
    return n64r(slot + TEXT) !== 0 || n64r(slot + DATA) !== 0;
  };
  window.modules = () => Object.keys(INDEX).filter((name) => window.loaded(name)).join(' ');
  window.untilLoaded = async (name, ms) =>
    ((await until(() => window.loaded(name), ms)) ? 'loaded: ' : 'not loaded: ') + window.modules();
  window.untilGone = async (name, ms) =>
    ((await until(() => !window.loaded(name), ms)) ? 'gone: ' : 'still loaded: ') + window.modules();
})();
