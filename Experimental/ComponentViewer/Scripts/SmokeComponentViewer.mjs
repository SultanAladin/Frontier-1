// UI smoke test: boots Main.js inside jsdom (Shared/Studio/Scripts/StudioHarness.mjs mocks WebGL, canvas, layout and
// runs the workers in-process) and drives the main paths — boot, tree, channels, drawer, simulate, graph, add /
// duplicate / delete, compile, details edits, content actions, tabs, file import.
// Geometry Compile is a separate workspace (Experimental/GeometryCompiler/Compile.html, own smoke). Here the framed
// compiler is played by a stand-in that speaks CompileExchange.js: it compiles the asset headlessly with the real
// CompileSteps, structured-clones it (as postMessage would) and posts 'finish' back.
//   npm install --prefix /tmp/smoke jsdom@25
//   JSDOM_DIR=/tmp/smoke node Experimental/ComponentViewer/Scripts/SmokeComponentViewer.mjs
import { pathToFileURL, fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { bootStudio } from '../../Shared/Studio/Scripts/StudioHarness.mjs';
import { compileAll } from '../../Shared/GeometryCompile/CompileSteps.js';
import { bakeSdf } from '../../Shared/GeometryCompile/Engine/SdfBake.js';
import { buildClusterHierarchy } from '../../Shared/GeometryCompile/Engine/ClusterHierarchy.js';
import { CHANNEL } from '../../Shared/GeometryCompile/CompileExchange.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..') + '/';
const { w, $, click, key, sleep, step, errors, downloads, finish } = bootStudio({ htmlPath: ROOT + 'index.html', url: 'http://localhost:8095/Experimental/ComponentViewer/' });

// ---------------------------------------------------------------- Geometry Compiler stand-in (the framed page)
const compilerCtx = {
  sdfWorker: async (r) => bakeSdf(r.positions, r.indices, { resolution: r.resolution, band: r.band }),
  lodWorker: async (r) => buildClusterHierarchy(r.positions, r.indices, { ...r.build, families: r.families || null }),
  renderIcon: async () => 'data:image/png;base64,AAAA',
};
const compiler = { opened: [], reply: 'compile', frame: null };
function fromCompiler(data) { w.dispatchEvent(new w.MessageEvent('message', { data: { channel: CHANNEL, ...data }, source: compiler.frame.contentWindow })); }
function attachCompiler() {
  const frame = $('Compile');
  if (compiler.frame === frame && frame.contentWindow.__standIn) return;
  compiler.frame = frame;
  frame.contentWindow.__standIn = true;
  frame.contentWindow.postMessage = (m) => {
    if (!m || m.channel !== CHANNEL || m.type !== 'open') return;
    compiler.opened.push(m);
    setTimeout(async () => {
      try {
        if (compiler.reply === 'compile') {
          await compileAll(m.asset, compilerCtx);
          fromCompiler({ type: 'finish', asset: structuredClone(m.asset), open: true, existing: m.recompile });
        } else fromCompiler({ type: 'finish', asset: structuredClone(m.asset), cancelled: true, existing: m.recompile });
      } catch (e) { errors.push('compiler stand-in: ' + e.stack); }
    }, 0);
  };
  fromCompiler({ type: 'ready' });
}
async function awaitCompiler(n) { for (let i = 0; i < 200 && compiler.opened.length < n; i++) { await sleep(25); if ($('Compile').getAttribute('src')) attachCompiler(); } }
async function awaitFrameClosed() { for (let i = 0; i < 200 && !$('Compile').hidden; i++) await sleep(25); }

await import(pathToFileURL(ROOT + 'Main.js'));
for (let i = 0; i < 100 && !$('Boot').classList.contains('done') && !$('Boot').classList.contains('failed'); i++) await sleep(100);

await step('boot', async () => {
  console.log('   boot:', $('Boot').className, '|', $('BootText').textContent);
  console.log('   title:', $('BpTitle').textContent, '|', $('BpSubtitle').textContent, '| tabs:', $('BlueprintTabs').children.length, '| rows:', $('ComponentTree').querySelectorAll('.comp-row').length, '| comps:', $('StatComponents').textContent, '| tris:', $('StatTriangles').textContent);
  if (!$('Boot').classList.contains('done')) throw new Error('boot did not finish');
});
await step('select tree rows & details', async () => {
  const rows = [...$('ComponentTree').querySelectorAll('.comp-row')];
  for (const r of rows) { click(r); await sleep(5); }
  console.log('   details sections:', $('Details').querySelectorAll('.detail-section').length, '| head:', ($('Details').querySelector('.name-input') || {}).value);
});
await step('camera rig + HUD', async () => {
  const cv = $('Viewport'), hud = $('ViewportWell').querySelector('.cam-hud');
  if (!hud) throw new Error('camera HUD missing');
  const lens = hud.querySelector('.cam-chip.lens'), before = lens.textContent;
  const ev = (type, x, y, extra = {}) => cv.dispatchEvent(new w.MouseEvent(type, { bubbles: true, clientX: x, clientY: y, ...extra }));
  ev('pointerdown', 200, 200); ev('pointermove', 260, 230); ev('pointermove', 300, 240); ev('pointerup', 300, 240);
  ev('pointerdown', 200, 200, { button: 2 }); key('w'); ev('pointermove', 210, 205); ev('pointerup', 210, 205, { button: 2 });
  cv.dispatchEvent(new w.WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: -120, clientX: 200, clientY: 200 }));
  cv.dispatchEvent(new w.WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: 120, altKey: true }));
  click(lens); await sleep(1100);
  click(hud.querySelectorAll('.dial-bead')[2]); key('t'); await sleep(200); key('t');
  const camRow = [...$('ComponentTree').querySelectorAll('.comp-row')].find((r) => /Camera/i.test(r.textContent));
  let through = '';
  if (camRow) { click(camRow); await sleep(20); key('c'); await sleep(1300); through = lens.textContent; }
  key('f'); await sleep(900);
  console.log('   lens:', before, '→', lens.textContent, '| beads:', hud.querySelectorAll('.dial-bead').length, '| camera row:', !!camRow, '| look chip hidden:', hud.querySelector('.cam-chip:nth-child(3)').hidden, '| through ChaseCamera:', through);
  if (through !== '20 mm') throw new Error('look-through did not take the Camera component lens (62° → 20 mm)');
  key('Escape');
});
await step('channels 1-7', async () => { for (const k of '1234567') { key(k); await sleep(20); } key('1'); console.log('   legend:', $('Legend').textContent.slice(0, 60)); });
await step('drawer compiled + curves', async () => {
  click($('DrawerHandle')); await sleep(20);
  console.log('   drawer open:', $('Drawer').classList.contains('open'), '| cards:', $('DrawerBody').querySelectorAll('.baked-card').length);
  click(w.document.querySelector('[data-drawer=curves]')); await sleep(20);
  console.log('   curve canvas:', !!$('DrawerBody').querySelector('canvas'));
  click(w.document.querySelector('[data-drawer=compiled]'));
});
await step('simulate', async () => {
  click($('SimulateBtn')); await sleep(300);
  console.log('   hud hidden:', $('SimHud').hidden, '| speed:', $('SimSpeed').textContent, '| rpm:', $('SimRpm').textContent);
  click($('SimulateBtn'));
});
await step('graph mode', async () => {
  const modes = [...$('CentreModes').querySelectorAll('button')];
  console.log('   centre modes:', modes.map((b) => b.textContent).join(','));
  click(modes[1]); await sleep(30);
  console.log('   nodes:', $('Graph').querySelectorAll('.gnode').length, '| pane hidden:', $('GraphPane').hidden);
  click(modes[0]);
});
await step('sockets: dock release / connect, kinds, add menu, simulate pings', async () => {
  const row = (name) => [...$('ComponentTree').querySelectorAll('.comp-row')].find((r) => r.querySelector('b')?.firstChild?.textContent === name);
  const subs = [...$('ComponentTree').querySelectorAll('.comp-row small')].map((x) => x.textContent).filter((t) => / socket/.test(t));
  console.log('   socket rows:', subs.length, '|', [...new Set(subs)].join(', '));
  click(row('RearWingSocket')); await sleep(30);
  const tiles = $('Details').querySelectorAll('.sock-tile');
  const tag = $('ViewportWell').querySelector('.socket-tag');
  console.log('   tiles:', tiles.length, '| active:', $('Details').querySelector('.sock-tile.active')?.textContent, '| tag:', tag.hidden ? 'hidden' : tag.textContent);
  if (tiles.length !== 8) throw new Error('expected 8 socket kinds');
  const btn = () => [...$('Details').querySelectorAll('.sock-actions button')].find((b) => /Release|Connect/.test(b.textContent));
  click(btn()); await sleep(700);
  const afterRelease = $('Details').querySelector('.sock-live').textContent;
  click(btn()); await sleep(900);
  const afterConnect = $('Details').querySelector('.sock-live').textContent;
  console.log('   release →', afterRelease, '| connect →', afterConnect, '| pings so far:', $('ViewportWell').querySelectorAll('.socket-ping').length);
  if (!/released|returning/.test(afterRelease) || !/connected/.test(afterConnect)) throw new Error('dock did not release and reconnect');
  click(row('TowStrap')); await sleep(20);
  for (const t of [...$('Details').querySelectorAll('.sock-tile')]) { click(t); await sleep(15); click([...$('Details').querySelectorAll('.sock-tile')].find((x) => x.textContent === t.textContent)); }
  click([...$('Details').querySelectorAll('.sock-tile')].find((x) => x.textContent === 'Tether')); await sleep(20);
  console.log('   TowStrap kind after cycling:', $('Details').querySelector('.sock-tile.active')?.textContent, '| fields:', $('Details').querySelectorAll('.sock-fields .field').length);
  click($('AddComponentBtn')); await sleep(10);
  const items = [...$('AddList').querySelectorAll('.sock-item')];
  click($('AddComponentBtn'));
  for (let i = 0; i < items.length; i++) { click($('AddComponentBtn')); await sleep(5); click($('AddList').querySelectorAll('.sock-item')[i]); await sleep(10); }
  console.log('   socket menu items:', items.length, '| comps:', $('StatComponents').textContent);
  click(row('RearWingSocket')); await sleep(20);
  click($('SimulateBtn')); await sleep(3200);
  const pings = [...$('ViewportWell').querySelectorAll('.socket-ping')].map((x) => x.textContent);
  console.log('   sim pings:', pings.join(' | ') || 'none', '| live:', $('Details').querySelector('.sock-live').textContent);
  click($('SimulateBtn')); await sleep(20);
  if (!pings.some((t) => /RearWingSocket · released at 1\.2/.test(t))) throw new Error('pull test did not release the wing');
});
await step('add component menu', async () => {
  click($('AddComponentBtn')); await sleep(10);
  const items = $('AddList').querySelectorAll('.menu-item');
  console.log('   menu items:', items.length);
  const pl = [...items].find((b) => /Point Light/.test(b.textContent));
  click(pl); await sleep(20);
  console.log('   comps after add:', $('StatComponents').textContent, '| state:', $('BpState').textContent);
});
await step('duplicate / delete / frame / grid', async () => {
  key('d', { ctrlKey: true }); await sleep(10);
  console.log('   after dup:', $('StatComponents').textContent);
  key('Delete'); await sleep(10);
  console.log('   after del:', $('StatComponents').textContent);
  key('f'); key('g'); await sleep(10);
});
await step('compile blueprint', async () => { click($('CompileBpBtn')); await sleep(300); console.log('   state:', $('BpState').textContent); });
await step('details edits (vec3, range, toggle, select)', async () => {
  const rows = [...$('ComponentTree').querySelectorAll('.comp-row')];
  click(rows[1]); await sleep(10);
  const inputs = [...$('Details').querySelectorAll('input, select')];
  let n = 0;
  for (const inp of inputs) {
    if (inp.type === 'range') { inp.value = inp.max; inp.dispatchEvent(new w.Event('input', { bubbles: true })); n++; }
    else if (inp.type === 'checkbox') { inp.checked = !inp.checked; inp.dispatchEvent(new w.Event('change', { bubbles: true })); n++; }
    else if (inp.tagName === 'SELECT' && inp.options.length > 1) { inp.selectedIndex = 1; inp.dispatchEvent(new w.Event('change', { bubbles: true })); n++; }
    else if (inp.type === 'text' && inp.closest('.axis')) { inp.value = '1.5'; inp.dispatchEvent(new w.Event('change', { bubbles: true })); n++; }
  }
  console.log('   inputs touched:', n);
});
await step('root details', async () => { click($('ComponentTree').querySelector('.comp-row')); await sleep(10); console.log('   root sections:', $('Details').querySelectorAll('.panel').length); });
await step('content tab + sample → Geometry Compiler frame', async () => {
  click(w.document.querySelector('[data-left=content]')); await sleep(10);
  console.log('   content:', $('ContentCount').textContent, '| samples:', $('SampleList').children.length, '| formats:', $('ImportFormats').textContent.slice(0, 60));
  click($('SampleList').querySelector('button'));
  await awaitCompiler(1);
  const src = $('Compile').getAttribute('src') || '';
  console.log('   frame open:', !$('Compile').hidden, '| src:', src.replace(/^.*\/Experimental\//, ''), '| sent:', compiler.opened[0]?.asset?.name);
  if (!/GeometryCompiler\/Compile\.html/.test(src)) throw new Error('frame does not load the Geometry Compiler page');
  if (!compiler.opened.length) throw new Error('no open message reached the compiler');
});
await step('compiler finish → opened as Blueprint', async () => {
  await awaitFrameClosed(); await sleep(100);
  const sent = compiler.opened[0].asset;
  console.log('   frame hidden:', $('Compile').hidden, '| title:', $('BpTitle').textContent, '| tabs:', $('BlueprintTabs').children.length, '| content:', $('ContentCount').textContent);
  if (!$('Compile').hidden) throw new Error('frame stayed open after finish');
  if (!$('BpTitle').textContent.includes(sent.name.replace(/[^\w]+/g, ''))) throw new Error('compiled asset did not open as a Blueprint');
});
await step('content row actions', async () => {
  const row = $('ContentList').querySelector('.content-row');
  const acts = row ? [...row.querySelectorAll('.content-actions button')] : [];
  console.log('   actions:', acts.map((b) => b.textContent).join(','));
  if (acts[0]) { click(acts[0]); await sleep(20); }
  console.log('   comps:', $('StatComponents').textContent);
  if (acts[2]) {
    compiler.reply = 'cancel';
    const n = compiler.opened.length;
    click(acts[2]); await awaitCompiler(n + 1);
    console.log('   recompile sent:', compiler.opened.length > n, '| recompile flag:', compiler.opened.at(-1).recompile);
    await awaitFrameClosed();
    console.log('   cancelled → frame hidden:', $('Compile').hidden);
  }
});
await step('switch tabs and close', async () => {
  const tabs = [...$('BlueprintTabs').querySelectorAll('.document-tab')];
  click(tabs[0]); await sleep(30);
  console.log('   title:', $('BpTitle').textContent);
  const close = $('BlueprintTabs').querySelectorAll('.tab-close');
  if (close.length > 1) { click(close[close.length - 1]); await sleep(20); }
  console.log('   tabs:', $('BlueprintTabs').children.length);
});
await step('import file (OBJ via FileInput)', async () => {
  compiler.reply = 'cancel';
  const n = compiler.opened.length;
  const obj = 'v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv 0 1.5 0\nf 1 2 3 4\nf 1 5 2\nf 2 5 3\nf 3 5 4\nf 4 5 1\n';
  const file = new w.File([obj], 'Pyramid.obj');
  file.arrayBuffer = async () => new TextEncoder().encode(obj).buffer;
  Object.defineProperty($('FileInput'), 'files', { value: [file], configurable: true });
  $('FileInput').dispatchEvent(new w.Event('change', { bubbles: true }));
  await awaitCompiler(n + 1);
  console.log('   sent:', compiler.opened.at(-1)?.asset?.fileName, '| format:', compiler.opened.at(-1)?.asset?.format);
  await awaitFrameClosed();
  console.log('   cancelled → frame hidden:', $('Compile').hidden);
  if (compiler.opened.length !== n + 1) throw new Error('import did not reach the compiler');
});

await finish();
