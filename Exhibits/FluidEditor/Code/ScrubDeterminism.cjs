// Phase 4 scrub determinism: a checkpoint restore + re-step must reproduce the live run bit for bit.
// For each preset: hash every checkpointed texture (grid, tracers, debris, particle records) after a straight run to T, then after
// scrubbing forward past T and back, then after scrubbing to 0 and forward again, then after a cold run with the ring cleared.
// Usage: node ScrubDeterminism.cjs <url> <preset,...> [T seconds, default 1.3] [max cells, default 40]
const { chromium } = require('playwright-core');
const L = require('@sparticuz/chromium'), sp = L.default ?? L;
(async () => {
  const [Url, Presets, Mark, Cells] = process.argv.slice(2);
  const B = await chromium.launch({ executablePath: await sp.executablePath(), args: [...sp.args, '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist'], headless: true });
  const P = await B.newPage({ viewport: { width: 900, height: 600 } });
  const Err = []; P.on('pageerror', (E) => Err.push(E.message));
  await P.goto(Url, { waitUntil: 'load' }); await P.waitForTimeout(3000);
  for (const Preset of Presets.split(',')) {
    const Row = await P.evaluate(([Preset, Mark, Cells, Keyed]) => {
      const App = window.__FLUID_STUDIO__;
      App.loadPreset(Preset, false);
      App.playing = false;
      if (Cells > 0) { App.scene.Domain.MaxCells = Cells; App.applyDomainResolution(); }      // 0 = the preset's own grid (infinite presets)
      App.reset();
      if (Keyed) {
        // A keyed gas layer and a keyed force, so the run exercises key sampling during the re-step.
        const Layer = App.scene.GasStack && App.scene.GasStack.Layers.find((L) => L.Category === 103);
        if (Layer) { App.seekNow(0); App.toggleKey({ Kind: 'Layer', Token: Layer.Token, Name: 'Buoyancy' }); App.seekNow(0.9); App.updateLayer(Layer.Token, 'Buoyancy', Layer.Parameters.Buoyancy * 3 + 1, true); App.continueSeek(Infinity); }
        App.seekNow(0); App.reset();
      }
      const Gl = App.solver.gl;
      const Hash = () => {
        Gl.finish();
        let H = 0x811c9dc5 >>> 0;
        const Names = [];
        for (const [Name, Target] of App.checkpoints.sources(App.solver, App.particles)) {
          const Out = new Float32Array(Target.width * Target.height * 4);
          Gl.bindFramebuffer(Gl.FRAMEBUFFER, Target.framebuffer);
          Gl.readPixels(0, 0, Target.width, Target.height, Gl.RGBA, Gl.FLOAT, Out);
          const Words = new Uint32Array(Out.buffer);
          for (let I = 0; I < Words.length; I++) { H ^= Words[I]; H = Math.imul(H, 0x01000193) >>> 0; }
          Names.push(Name.replace(/^particles-[0-9a-z]+/, 'cloud'));
        }
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        return { Hash: H.toString(16).padStart(8, '0'), Time: App.solver.time.toFixed(4), Frame: App.solver.frame, Sources: Names.length, Names };
      };
      const Runs = {};
      App.checkpoints.clear(); App.reset();
      App.seekNow(Mark); Runs.Straight = Hash();
      App.seekNow(Mark + 0.8); App.seekNow(Mark); Runs.ForwardThenBack = Hash();
      App.seekNow(0.1); App.seekNow(Mark); Runs.BackThenForward = Hash();
      App.seekNow(Mark - 0.04); App.seekNow(Mark); Runs.FiveStepsBack = Hash();
      const Kept = App.checkpoints.entries.map((E) => E.Time.toFixed(2)).join(',');
      App.checkpoints.clear(); App.reset(); App.seekNow(Mark); Runs.Cold = Hash();
      return { Preset, Grid: `${App.solver.layout.width}x${App.solver.layout.height}x${App.solver.layout.depth}${App.solver.sparse ? ' sparse' : ''}`, Kept, Restores: App.statistics.seekRestores, Sources: Runs.Straight.Names.join(' '), Runs };
    }, [Preset.replace(/\+keyed$/, ''), +(Mark || 1.3), Cells === undefined ? 40 : +Cells, /\+keyed$/.test(Preset)]);
    const Hashes = Object.entries(Row.Runs).map(([K, V]) => `${K} ${V.Hash} t${V.Time} #${V.Frame}`);
    const Same = new Set(Object.values(Row.Runs).map((V) => V.Hash + V.Frame)).size === 1;
    console.log(`${Same ? 'IDENTICAL' : 'DIFFERENT'} ${Preset} · ${Row.Grid} · checkpoints ${Row.Kept} · ${Row.Runs.Straight.Sources} textures (${Row.Sources})`);
    console.log('   ' + Hashes.join(' | '));
  }
  console.log('errors', JSON.stringify(Err.slice(0, 4)));
  await B.close();
})();
