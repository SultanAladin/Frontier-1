// Flipbook check: drive the Flipbook drawer like a user (open, choose settings, Bake), then save the baked sheet and
// screenshots of the drawer and the scene preset browser. Usage: node FlipbookCheck.cjs <url> <out folder> <preset-id> [max cells]
const { chromium } = require('playwright-core');
const L = require('@sparticuz/chromium'), sp = L.default ?? L;
const fs = require('fs');
(async () => {
  const [Url, Out, Preset, MaxCells] = process.argv.slice(2);
  const B = await chromium.launch({ executablePath: await sp.executablePath(), args: [...sp.args, '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist'], headless: true });
  const P = await B.newPage({ viewport: { width: 1280, height: 760 } });
  const Err = []; P.on('pageerror', (E) => Err.push(E.message)); P.on('console', (M) => { if (M.type() === 'error') Err.push('console: ' + M.text().slice(0, 200)); });
  await P.goto(Url, { waitUntil: 'load' }); await P.waitForTimeout(4000);
  await P.evaluate(([Preset, MaxCells]) => {
    const App = window.__FLUID_STUDIO__;
    App.loadPreset(Preset);
    if (MaxCells > 0) { App.scene.Domain.MaxCells = MaxCells; App.applyDomainResolution(); App.notify(); }
  }, [Preset, +(MaxCells || 0)]);
  // Scene preset browser.
  await P.click('.scenes-button');
  await P.waitForTimeout(800);
  await P.screenshot({ path: `${Out}/Presets.png`, timeout: 120000 });
  const Groups = await P.evaluate(() => [...document.querySelectorAll('.drawer .asset-group h4')].map((H) => H.textContent));
  // Flipbook drawer, driven through its controls.
  await P.click('button.scenes-button:has-text("Flipbook")');
  await P.waitForTimeout(500);
  const Selects = await P.$$('.flipbook-card select');
  await Selects[0].selectOption('16'); await P.waitForTimeout(200);
  await (await P.$$('.flipbook-card select'))[1].selectOption('128'); await P.waitForTimeout(200);
  await (await P.$$('.flipbook-card select'))[2].selectOption('12'); await P.waitForTimeout(200);
  await P.fill('.flipbook-card input[type=number]', '1'); await P.press('.flipbook-card input[type=number]', 'Tab');
  await P.waitForTimeout(300);
  if (process.env.TRANSPARENT) await P.evaluate(() => [...document.querySelectorAll('.flipbook-toggle')].find((L) => L.textContent.includes('Transparent')).querySelector('input').click());
  await P.evaluate(() => document.querySelector('.flipbook-card button.pill.primary').click());
  await P.waitForFunction(() => /complete|cancel|interrupt|Choose/i.test(document.getElementById('flipbook-label')?.textContent || ''), null, { timeout: 900000, polling: 2000 });
  const Label = await P.textContent('#flipbook-label');
  await P.waitForTimeout(1500);
  await P.screenshot({ path: `${Out}/Drawer.png`, timeout: 120000 });
  const Sheet = await P.evaluate(async () => {
    const Canvas = document.createElement('canvas');
    // The baked sheet is held by the UI; reach it through a download click intercepted as a data URL.
    return new Promise((Resolve) => {
      const Original = URL.createObjectURL;
      URL.createObjectURL = (Blob) => { const R = new FileReader(); R.onload = () => Resolve(R.result); R.readAsDataURL(Blob); URL.createObjectURL = Original; return 'about:blank'; };
      document.querySelector('.flipbook-side button.pill').click();
    });
  });
  fs.writeFileSync(`${Out}/Sheet.png`, Buffer.from(Sheet.split(',')[1], 'base64'));
  const Live = await P.evaluate(() => ({ time: window.__FLUID_STUDIO__.solver.time.toFixed(2), playing: window.__FLUID_STUDIO__.playing, baking: !!window.__FLUID_STUDIO__.baking }));
  console.log(JSON.stringify({ Label, Groups, Live }), 'errors', JSON.stringify(Err.slice(0, 5)));
  await B.close();
})();
