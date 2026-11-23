// Fluid Editor force metrics: for each preset, step the solver and read back the velocity, state and curl volumes at
// checkpoints. Inside the smoke (density > 0.05 or fire), report mean speed, 95th-percentile speed and mean vorticity.
// A scene that "starts clean and turns messy" shows speed and vorticity that keep climbing instead of levelling off.
// Usage: node FluidMetrics.cjs <url> <max cells> <preset-id,preset-id,...> [checkpoint seconds, default 1,3,6,10] [domain overrides JSON]
const { chromium } = require('playwright-core');
const L = require('@sparticuz/chromium'), sp = L.default ?? L;
(async () => {
  const [Url, Cells, Presets, Marks, Overrides] = process.argv.slice(2);
  const B = await chromium.launch({ executablePath: await sp.executablePath(), args: [...sp.args, '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist'], headless: true });
  const P = await B.newPage({ viewport: { width: 900, height: 600 } });
  const Err = []; P.on('pageerror', (E) => Err.push(E.message));
  await P.goto(Url, { waitUntil: 'load' }); await P.waitForTimeout(3000);
  for (const Preset of Presets.split(',')) {
    const Rows = await P.evaluate(async ([Preset, Cells, Marks, Overrides]) => {
      const App = window.__FLUID_STUDIO__;
      App.loadPreset(Preset, false);
      App.playing = false;
      Object.assign(App.scene.Domain, Overrides);
      App.scene.Domain.MaxCells = Cells; App.applyDomainResolution();
      const Solver = App.solver, Gl = Solver.gl, Layout = Solver.layout;
      const Read = (Target) => {
        const Out = new Float32Array(Layout.textureWidth * Layout.textureHeight * 4);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, Target.framebuffer);
        Gl.readPixels(0, 0, Layout.textureWidth, Layout.textureHeight, Gl.RGBA, Gl.FLOAT, Out);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        return Out;
      };
      const Dt = App.fixedDeltaTime * App.scene.Domain.TimeScale;
      const Result = [];
      let Step = 0;
      for (const Mark of Marks) {
        while (Step * Dt < Mark - 1e-6) { Solver.step(App.scene, Dt); Step++; if (Step % 30 === 0) { Gl.finish(); await new Promise((R) => setTimeout(R, 0)); } }
        const Velocity = Read(Solver.fields.velocity.read), State = Read(Solver.fields.state.read), Curl = Read(Solver.fields.curl);
        const Speeds = []; let Vorticity = 0;
        for (let I = 0; I < State.length; I += 4) {
          if (State[I] < 0.05 && State[I + 1] < 1.0) continue;
          Speeds.push(Math.hypot(Velocity[I], Velocity[I + 1], Velocity[I + 2]));
          Vorticity += Curl[I + 3];
        }
        Speeds.sort((A, B) => A - B);
        const N = Speeds.length || 1;
        Result.push({ T: Mark, Cells: Speeds.length, Mean: Speeds.reduce((A, B) => A + B, 0) / N, P95: Speeds[Math.floor(0.95 * (N - 1))] || 0, Curl: Vorticity / N });
      }
      return { Grid: Layout.width + 'x' + Layout.height + 'x' + Layout.depth, Result };
    }, [Preset, +Cells, (Marks || '1,3,6,10').split(',').map(Number), JSON.parse(Overrides || '{}')]);
    console.log(Preset.padEnd(22), Rows.Grid.padEnd(9), Rows.Result.map((R) => `t${R.T}: v${R.Mean.toFixed(2)} p95 ${R.P95.toFixed(2)} ω${R.Curl.toFixed(1)}`).join(' | '));
  }
  console.log('errors', JSON.stringify(Err.slice(0, 4)));
  await B.close();
})();
