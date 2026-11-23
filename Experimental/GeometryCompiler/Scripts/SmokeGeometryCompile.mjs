// UI smoke test for the Geometry Compiler workspace page (Compile.html, split out of the Component Viewer in M0):
// boots CompileMain.js inside jsdom (Shared/Studio/Scripts/StudioHarness.mjs) and drives the start screen, every
// step + preview mode, Compile all, the exports, Compile & keep, recompile from the session list, Esc, a file import
// and the CompileExchange guest handshake the Component Viewer relies on.
//   npm install --prefix /tmp/smoke jsdom@25
//   JSDOM_DIR=/tmp/smoke node Experimental/GeometryCompiler/Scripts/SmokeGeometryCompile.mjs
import { pathToFileURL, fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { bootStudio } from '../../Shared/Studio/Scripts/StudioHarness.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..') + '/';
const { w, $, click, key, sleep, step, errors, downloads, finish } = bootStudio({ htmlPath: ROOT + 'Compile.html', url: 'http://localhost:8095/Experimental/GeometryCompiler/Compile.html' });
const waitIdle = async (n = 200) => { for (let i = 0; i < n && /…|ing/.test($('CompileStatus').textContent); i++) await sleep(50); };

await import(pathToFileURL(ROOT + 'CompileMain.js'));
await sleep(50);

await step('start screen', async () => {
  console.log('   start visible:', !$('Start').hidden, '| shell hidden:', $('Compile').hidden, '| samples:', $('SampleList').children.length, '| formats:', $('ImportFormats').textContent, '| caption:', $('StartCaption').textContent);
  if ($('Start').hidden || !$('Compile').hidden) throw new Error('standalone page should open on the start screen');
  if ($('SampleList').children.length < 2) throw new Error('sample sources missing');
});
await step('sample → compile session', async () => {
  click($('SampleList').querySelector('button')); await sleep(50); await waitIdle();
  console.log('   shell hidden:', $('Compile').hidden, '| start hidden:', $('Start').hidden, '| status:', $('CompileStatus').textContent, '| steps:', $('StepList').children.length, '| count:', $('CompileStepCount').textContent, '| open label:', $('CompileOpenLabel').textContent);
  if ($('Compile').hidden) throw new Error('session did not open');
  if ($('CompileOpenLabel').textContent !== 'Compile & keep') throw new Error('standalone open label should be "Compile & keep"');
});
await step('visit every step + preview modes', async () => {
  for (const s of [...$('StepList').querySelectorAll('.step-row')]) {
    click(s); await sleep(10);
    const modes = [...$('PreviewModes').querySelectorAll('button')];
    for (const m of modes) { click(m); await sleep(5); }
    console.log(`   ${s.querySelector('b')?.textContent}: settings panels ${$('StepSettings').querySelectorAll('.panel').length}, modes ${modes.length}`);
  }
  key('ArrowDown'); key('ArrowUp'); key('f');
});
await step('compile all + exports', async () => {
  click($('CompileRunAll'));
  for (let i = 0; i < 200; i++) { await sleep(50); if (/compiled|ready|done/i.test($('CompileStatus').textContent)) break; }
  console.log('   status:', $('CompileStatus').textContent, '| total:', $('CompileTotal').textContent, '| mem:', $('CompileMemory').textContent);
  for (const s of $('StepList').querySelectorAll('.step-row')) {
    click(s); await sleep(5);
    for (const b of $('StepSettings').querySelectorAll('button')) if (/export|download|obj|svg|r32|png/i.test(b.textContent)) { click(b); await sleep(5); }
  }
  console.log('   downloads:', downloads.join(', '));
});
await step('compile & keep → session list', async () => {
  click($('CompileOpen')); await sleep(300);
  console.log('   shell hidden:', $('Compile').hidden, '| start visible:', !$('Start').hidden, '| kept:', $('SessionCount').textContent);
  if ($('SessionCount').textContent !== '1') throw new Error('compiled asset not kept');
});
await step('recompile from session + Esc', async () => {
  click($('SessionList').querySelector('button')); await sleep(100); await waitIdle();
  console.log('   open:', !$('Compile').hidden, '| label:', $('CompileOpenLabel').textContent);
  if ($('CompileOpenLabel').textContent !== 'Compile & update') throw new Error('recompile label');
  key('Escape'); await sleep(50);
  console.log('   after esc hidden:', $('Compile').hidden, '| kept:', $('SessionCount').textContent);
});
await step('import file (OBJ via FileInput)', async () => {
  const obj = 'v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv 0 1.5 0\nf 1 2 3 4\nf 1 5 2\nf 2 5 3\nf 3 5 4\nf 4 5 1\n';
  const file = new w.File([obj], 'Pyramid.obj');
  file.arrayBuffer = async () => new TextEncoder().encode(obj).buffer;
  Object.defineProperty($('FileInput'), 'files', { value: [file], configurable: true });
  $('FileInput').dispatchEvent(new w.Event('change', { bubbles: true }));
  await sleep(500);
  console.log('   open:', !$('Compile').hidden, '| file:', $('CompileFile').textContent, '| status:', $('CompileStatus').textContent);
  if (!/Pyramid/.test($('CompileFile').textContent)) throw new Error('import did not open');
  click($('CompileCancel')); await sleep(50);
});
await step('exchange: guest handshake (framed mode)', async () => {
  const { compilerGuest, CHANNEL } = await import(pathToFileURL(resolve(ROOT, '../Shared/GeometryCompile/CompileExchange.js')));
  const hostFrame = w.document.createElement('iframe'); w.document.body.append(hostFrame);
  const host = hostFrame.contentWindow, got = [];
  host.postMessage = (m) => got.push(m);
  Object.defineProperty(w, 'parent', { value: host, configurable: true });
  const opened = [];
  const guest = compilerGuest({ onOpen: (asset, o) => opened.push([asset, o]) });
  guest.announce();
  w.dispatchEvent(new w.MessageEvent('message', { data: { channel: CHANNEL, type: 'open', asset: { id: 'A900' }, recompile: true }, source: host }));
  w.dispatchEvent(new w.MessageEvent('message', { data: { channel: CHANNEL, type: 'open', asset: { id: 'A901' } }, source: w }));   // not from the host: ignored
  guest.finish({ id: 'A900' }, { open: true, existing: true });
  console.log('   embedded:', guest.embedded, '| to host:', got.map((m) => m.type).join(','), '| opened:', opened.map(([a, o]) => `${a.id}${o.recompile ? ' (recompile)' : ''}`).join(','));
  if (!guest.embedded || got.map((m) => m.type).join() !== 'ready,finish' || opened.length !== 1) throw new Error('exchange handshake broken');
});

await finish();
