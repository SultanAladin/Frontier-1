// Phase 4 captures: Demolition Blast with keyed Buoyancy and Vorticity weight; dope sheet, curves view and the inspector ◆ controls.
// Run from a folder with playwright-core and @sparticuz/chromium while Serve.py serves the editor on port 8100.
const { chromium } = require('playwright-core');
const L = require('@sparticuz/chromium'), sp = L.default ?? L;
(async () => {
  const B = await chromium.launch({ executablePath: await sp.executablePath(), args: [...sp.args, '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist'], headless: true });
  const P = await B.newPage({ viewport: { width: 1600, height: 1000 } });
  const errors = []; P.on('pageerror', e => errors.push(e.message)); P.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await P.goto('http://127.0.0.1:8100/?v=' + Date.now(), { waitUntil: 'load' });
  await P.waitForFunction(() => window.__FLUID_STUDIO__ && window.__FLUID_STUDIO__.scene, null, { timeout: 60000 });
  await P.evaluate(() => {
    const a = window.__FLUID_STUDIO__;
    a.loadPreset('preset-demolition'); a.playing = false;
    a.scene.Domain.MaxCells = 24; a.applyDomainResolution(); a.reset();
    const stack = a.scene.GasStack, b = stack.Layers.find(l => l.Category === 103), v = stack.Layers.find(l => l.Category === 105);
    stack.Tracks.push({ Path: `${b.Token}/Buoyancy`, Extrapolation: 0, Keys: [{ Time: 0, Amount: b.Parameters.Buoyancy, Ease: 2 }, { Time: 1.2, Amount: 3.2, Ease: 2 }, { Time: 2.6, Amount: 0.6, Ease: 2 }, { Time: 4, Amount: 2, Ease: 3, In: 0, Out: 0 }] });
    if (v) stack.Tracks.push({ Path: `${v.Token}/Weight`, Extrapolation: 2, Keys: [{ Time: 0.5, Amount: 1, Ease: 1 }, { Time: 3, Amount: 0.2, Ease: 1 }] });
    a.evaluateStack(); a.invalidateCheckpoints(0);
    a.select('domain'); a.selectLayer(b.Token);
    a.seekNow(0.6); a.notify();
  });
  await P.waitForTimeout(1500);
  // select two keys
  await P.evaluate(() => { const keys = document.querySelectorAll('.tl-track .tl-key'); const k = keys[1]; if (k) { const r = k.getBoundingClientRect(); k.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, clientX: r.x + 4, clientY: r.y + 4 })); window.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, clientX: r.x + 4, clientY: r.y + 4 })); } });
  await P.waitForTimeout(800);
  const box = await P.locator('#pz-timeline').boundingBox();
  await P.screenshot({ path: '/home/user/Frontier/Exhibits/FluidEditor/Images/TimelineDopeSheet.png', timeout: 120000 });
  const dom = await P.evaluate(() => { const bar = document.querySelector('.tl-bar'); return { overflow: bar.scrollWidth - bar.clientWidth, selected: document.querySelectorAll('.tl-key.selected').length, diamonds: [...document.querySelectorAll('.key-toggle')].map(d => d.className + ':' + d.getAttribute('data-key-target')).slice(0, 8), keyedShown: document.querySelector('.control.keyed .control-head b, .control.keyed input')?.value ?? null }; });
  console.log(JSON.stringify(dom));
  // Inspector layer card with diamonds
  await P.evaluate(() => { const el = document.querySelector('[data-layer-inspector]') || document.querySelector('.key-toggle'); el && el.scrollIntoView({ block: 'start' }); });
  await P.waitForTimeout(600);
  const ib = await P.locator('#pz-inspector').boundingBox();
  await P.evaluate(() => { const b = [...document.querySelectorAll('.tl-views button')].find(x => /Curves/.test(x.textContent)); b && b.click(); }); await P.waitForTimeout(800); const tb = await P.locator('#pz-timeline').boundingBox(); await P.screenshot({ path: '/home/user/Frontier/Exhibits/FluidEditor/Images/TimelineCurves.png', clip: { x: tb.x, y: tb.y, width: tb.width, height: tb.height }, timeout: 120000 });
  await P.screenshot({ path: '/home/user/Frontier/Exhibits/FluidEditor/Images/TimelineInspectorKeys.png', clip: { x: ib.x, y: ib.y, width: ib.width, height: Math.min(ib.height, 1000 - ib.y) }, timeout: 120000 });
  console.log('errors', JSON.stringify(errors));
  await B.close();
})();
