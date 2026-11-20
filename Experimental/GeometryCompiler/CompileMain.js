// Geometry Compiler workspace boot. Two modes:
//   framed by the Component Viewer (?host=component-viewer) — waits for an 'open' message, runs the session and posts
//     the compiled copy back (Shared/GeometryCompile/CompileExchange.js);
//   standalone — start screen with import, sample sources and the assets compiled in this session.
import { $, el, iconEl, toast } from '../Shared/Studio/Ui.js';
import { hydrateIcons } from '../Shared/Studio/Icons.js';
import { createCompileContext } from '../Shared/GeometryCompile/CompileContext.js';
import { COMPILE_SAMPLES, assetFromFile } from '../Shared/GeometryCompile/CompileSources.js';
import { compilerGuest } from '../Shared/GeometryCompile/CompileExchange.js';
import { IMPORT_FORMATS } from '../Shared/GeometryCompile/Engine/ImportFormats.js';
import { fmt } from '../Shared/GeometryCompile/CompileSteps.js';
import { createGeometryCompile } from './GeometryCompile.js';

const { ctx } = createCompileContext();
const kept = new Map();   // standalone: compiled assets of this session, by id

const guest = compilerGuest({
  onOpen(asset, { recompile }) { openSession(asset, { recompile }); },
});

const compile = createGeometryCompile({
  ctx,
  host: guest.embedded ? 'Component Viewer' : null,
  onFinish(asset, info) {
    if (guest.embedded) guest.finish(asset, info);
    else if (!info.cancelled) { kept.set(asset.id, asset); toast(`${asset.name} compiled · kept in this session`); }
    showStart();
  },
});

function openSession(asset, options = {}) {
  $('Start').hidden = true;
  compile.open(asset, options);
}

function showStart() {
  $('Start').hidden = false;
  $('StartCaption').textContent = guest.embedded ? 'Framed by the Component Viewer · waiting for an import' : 'Import an object to start a compile session';
  $('ViewerLink').hidden = guest.embedded;
  const list = $('SessionList');
  list.innerHTML = '';
  for (const a of kept.values()) {
    const tris = a.out.geometry ? a.out.geometry.indices.length / 3 : 0;
    const row = el('button', { type: 'button', class: 'entity import', 'data-asset': a.id },
      a.out.icon ? el('img', { class: 'entity-thumb', src: a.out.icon, alt: '' }) : iconEl('cube', 16, 'entity-icon'),
      el('div', {}, el('b', {}, a.name), el('small', {}, `${a.format} · ${fmt(tris)} tris · ${a.settings.meta.category}`)),
      iconEl('hammer', 13, 'muted'));
    row.addEventListener('click', () => openSession(a, { recompile: true }));
    list.append(row);
  }
  $('SessionCount').textContent = String(kept.size);
}

async function importFiles(files) {
  for (const file of files) {
    try { openSession(await assetFromFile(file)); return; }
    catch (error) { toast(String(error.message || error)); }
  }
}

// ------------------------------------------------------------------------------------------------- start screen
$('ImportFormats').append(...IMPORT_FORMATS.map((f) => el('span', { class: `chip ${f.reads ? '' : 'dim'}`, title: f.reads ? 'Read in this build' : 'Listed, no reader in this build' }, f.label)));
for (const s of COMPILE_SAMPLES) {
  const b = el('button', { type: 'button', class: 'entity import', 'data-sample': s.id }, iconEl(s.icon || 'sparkle', 16, 'entity-icon'), el('div', {}, el('b', {}, s.label), el('small', {}, s.note)), iconEl('upload', 13, 'muted'));
  b.addEventListener('click', async () => { try { openSession(await s.source()); } catch (error) { toast(String(error.message || error)); } });
  $('SampleList').append(b);
}
$('ImportBtn').addEventListener('click', () => $('FileInput').click());
$('FileInput').addEventListener('change', (e) => { importFiles(e.target.files); e.target.value = ''; });
addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('Files')) e.preventDefault(); });
addEventListener('drop', (e) => { if (e.dataTransfer.files.length && !e.defaultPrevented) { e.preventDefault(); importFiles(e.dataTransfer.files); } });

hydrateIcons(document);
showStart();
guest.announce();

window.FrontierGeometryCompiler = { compile, kept, openSession, embedded: guest.embedded };
