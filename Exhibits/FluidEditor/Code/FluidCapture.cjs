// Fluid Editor capture: load a preset, run N solver steps directly, hide overlays, then save the viewport.
// Usage: node FluidCapture.cjs <url> <out.png> <preset-id> <steps> [max cells on the longest axis]
const { chromium } = require('playwright-core');
const L = require('@sparticuz/chromium'), sp = L.default ?? L;
(async () => {
  const [Url, Out, Preset, Steps, MaxCells] = process.argv.slice(2);
  const B = await chromium.launch({ executablePath: await sp.executablePath(), args: [...sp.args, '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist'], headless: true });
  const P = await B.newPage({ viewport: { width: 1280, height: 800 } });
  const Err = []; P.on('pageerror', (E) => Err.push(E.message)); P.on('console', (M) => { if (M.type() === 'error') Err.push('console: ' + M.text().slice(0, 200)); });
  await P.goto(Url, { waitUntil: 'load' }); await P.waitForTimeout(4000);
  const Info = await P.evaluate(async ([Preset, Steps, MaxCells]) => {
    const App = window.__FLUID_STUDIO__;
    App.loadPreset(Preset, false);
    App.playing = false;
    if (MaxCells > 0) { App.scene.Domain.MaxCells = MaxCells; App.applyDomainResolution(); }
    for (const Key of Object.keys(App.scene.Visualizers)) if (Key !== 'Volume') App.scene.Visualizers[Key].Visible = false;
    for (const F of App.scene.Forces) F.Gizmo = false;
    const Dt = App.fixedDeltaTime * App.scene.Domain.TimeScale;
    for (let I = 0; I < Steps; I++) {
      App.solver.step(App.scene, Dt); App.solver.updateDebris(Dt, App.scene.Visualizers.Debris);
      if (I % 20 === 19) { App.solver.gl.finish(); await new Promise((R) => setTimeout(R, 0)); }
    }
    App.selection = null;
    return { Time: App.solver.time.toFixed(2), Expansion: App.scene.Domain.Expansion, Pressure: App.scene.Domain.PressureIterations, Sparse: !!App.solver.sparse, Grid: App.solver.layout.width + 'x' + App.solver.layout.height + 'x' + App.solver.layout.depth };
  }, [Preset, +Steps, +(MaxCells || 0)]);
  await P.waitForTimeout(3000);
  const View = await P.evaluate(() => { const C = [...document.querySelectorAll('canvas')].sort((a, b) => b.width * b.height - a.width * a.height)[0]; const R = C.getBoundingClientRect(); return { x: R.x, y: R.y, width: R.width, height: R.height }; });
  await P.screenshot({ path: Out, clip: View, timeout: 120000 });
  console.log(JSON.stringify(Info), 'errors', JSON.stringify(Err.slice(0, 5)));
  await B.close();
})();
