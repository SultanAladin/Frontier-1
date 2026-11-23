// Frontier Geometry Compiler — Cluster LOD scene + StudioIconSet + Inspector SDF Slice + SDF Preview & Baker Workspace.
import { SOURCE_PRESETS } from './Engine/SourceGeometry.js';
import { ViewportRasterization, VIEW_MODES } from './Engine/ViewportRasterization.js';
import { projectedError } from './Engine/ClusterSelection.js';
import { createIconCapture } from './IconCapture.js';
import { createUvWorkspace } from './UvWorkspace.js';
import { createGeometryEditor } from './GeometryEditor.js';
import { createSdfWorkspace } from './SdfWorkspace.js';
import { identityTransform } from './Engine/GeometryTransform.js';
import { renderIcon, hydrateIcons } from './IconRegistry.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const number = (n) => Math.round(n).toLocaleString('en-US');
const compact = (n) => (n >= 1e6 ? (n / 1e6).toFixed(n >= 1e7 ? 1 : 2) + 'M' : n >= 1e4 ? (n / 1e3).toFixed(n >= 1e5 ? 0 : 1) + 'k' : number(n));

hydrateIcons(document);

// Same ramp as the shader, for swatches and charts.
function ramp(t) {
  const stops = [[48, 107, 242], [41, 199, 194], [89, 209, 92], [247, 204, 64], [240, 77, 61]];
  t = Math.min(1, Math.max(0, t)) * 4;
  const i = Math.min(3, Math.floor(t)), f = t - i;
  return `rgb(${stops[i].map((c, k) => Math.round(c + (stops[i + 1][k] - c) * f)).join(',')})`;
}

// Graceful mapping for TextureStudio ?specimen= handoff links
const SPECIMEN_TO_PRESET = {
  GlassVessel: 'vase',
  UtahTeapot: 'torus',
  ChamferBox: 'braid',
  FoliageLeaf: 'knot',
  Asteroid: 'asteroid',
  Planet: 'planet',
  Terrain: 'terrain',
};
const requestedPreset = query.get('preset') || SPECIMEN_TO_PRESET[query.get('specimen') || ''];

// --- State -------------------------------------------------------------------------------------------------
const settings = {
  threshold: +query.get('error') || 1,
  forcedLevel: -1,
  frustumCull: true,
  backfaceCull: true,
  cullBackTriangles: true,
  viewMode: VIEW_MODES.some((m) => m.id === query.get('mode')) ? query.get('mode') : 'levels',
  wireframe: query.has('wire'),
  bounds: true,
  grid: true,
  flatShading: false,
  selected: null,
};
const build = {
  preset: SOURCE_PRESETS.some((p) => p.id === requestedPreset) ? requestedPreset : 'vase',
  triangleBudget: +query.get('budget') || 200000,
  clusterTriangles: 128, groupClusters: 4, simplifyRatio: 0.5,
  objText: null, objName: '',
  transform: identityTransform(),
};
const layout = { side: Math.min(15, Math.max(1, +query.get('side') || 5)), spacing: 3 };
const camera = { target: [0, 0, 0], yaw: 0.7, pitch: 0.38, distance: 6, fov: (50 * Math.PI) / 180, eye: [0, 0, 0] };
let geometry = null, built = null, building = false;

let viewport;
try {
  viewport = new ViewportRasterization($('Viewport'));
} catch (error) {
  $('BuildOverlay').classList.add('open', 'failed');
  $('BuildLabel').textContent = 'Cannot start the viewport';
  $('BuildNote').textContent = error.message;
  throw error;
}

function updateEye() {
  const cp = Math.cos(camera.pitch);
  camera.eye = [
    camera.target[0] + Math.sin(camera.yaw) * cp * camera.distance,
    camera.target[1] + Math.sin(camera.pitch) * camera.distance,
    camera.target[2] + Math.cos(camera.yaw) * cp * camera.distance,
  ];
}
function fitAll() {
  camera.target = [0, 0, 0];
  camera.distance = Math.max(2.6, (viewport.extent || 1.5) * 1.9);
  camera.yaw = 0.7; camera.pitch = 0.38;
}

let toastTimer = 0;
function toast(text) {
  const node = $('Toast');
  node.textContent = text;
  node.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove('show'), 2200);
}

// --- Sliders -----------------------------------------------------------------------------------------------
const SLIDERS = {
  threshold: { label: 'Pixel error', target: settings, min: 0, max: 1, step: 0.001, toValue: (s) => 0.1 * Math.pow(320, s), fromValue: (v) => Math.log(v / 0.1) / Math.log(320), format: (v) => v.toFixed(v < 10 ? 2 : 1) + ' px', note: 'A cluster is swapped for its coarser parent once the parent’s projected error is below this.' },
  side: { label: 'Grid', target: layout, min: 1, max: 15, step: 1, format: (v) => `${v} × ${v} = ${v * v}`, apply: applyLayout },
  spacing: { label: 'Spacing', target: layout, min: 2.2, max: 12, step: 0.1, format: (v) => v.toFixed(1) + ' u', apply: applyLayout },
  clusterTriangles: { label: 'Triangles per cluster', target: build, min: 32, max: 256, step: 16, format: (v) => '≤ ' + v, apply: markStale },
  groupClusters: { label: 'Clusters per group', target: build, min: 2, max: 8, step: 1, format: (v) => '~' + v, apply: markStale },
  simplifyRatio: { label: 'Keep per level', target: build, min: 0.3, max: 0.7, step: 0.05, format: (v) => Math.round(v * 100) + ' %', apply: markStale },
};
const sliderNodes = {};
for (const host of document.querySelectorAll('[data-slider]')) {
  const key = host.dataset.slider, spec = SLIDERS[key];
  host.innerHTML = `<label for="Slider_${key}">${spec.label}</label><output></output><input id="Slider_${key}" type="range" min="${spec.min}" max="${spec.max}" step="${spec.step}">${spec.note ? `<small>${spec.note}</small>` : ''}`;
  const input = host.querySelector('input'), output = host.querySelector('output');
  const refresh = () => {
    const value = spec.target[key];
    const raw = spec.fromValue ? spec.fromValue(value) : value;
    input.value = raw;
    input.style.setProperty('--fill', ((raw - spec.min) / (spec.max - spec.min)) * 100 + '%');
    output.textContent = spec.format(value);
  };
  input.addEventListener('input', () => {
    spec.target[key] = spec.toValue ? spec.toValue(+input.value) : +input.value;
    refresh();
    if (spec.apply) spec.apply();
  });
  sliderNodes[key] = refresh;
  refresh();
}

function applyLayout() {
  viewport.setInstanceGrid(layout.side, layout.spacing);
  if (settings.selected && settings.selected.instance >= viewport.instances.length) settings.selected = null;
  if (viewport.frozen) setFrozen(false);
}
function markStale() { $('Rebuild').classList.add('stale'); }

// --- Toggles -----------------------------------------------------------------------------------------------
const TOGGLES = [
  ['ToolWire', 'wireframe'], ['ToolBounds', 'bounds'], ['ToolGrid', 'grid'],
  ['TileFrustum', 'frustumCull'], ['TileCone', 'backfaceCull'], ['TileFlat', 'flatShading'], ['TileBackTriangles', 'cullBackTriangles'],
];
function refreshToggles() {
  for (const [id, key] of TOGGLES) {
    const node = $(id), on = settings[key];
    node.classList.toggle(node.classList.contains('tile') ? 'on' : 'active', on);
    node.setAttribute('aria-pressed', on);
    const tag = node.querySelector('small');
    if (tag) tag.textContent = id === 'TileBackTriangles' ? (on ? 'CULLED' : 'SHOWN') : on ? 'ON' : 'OFF';
  }
  $('ToolFreeze').classList.toggle('active', !!viewport.frozen);
  $('ToolFreeze').classList.toggle('warn', !!viewport.frozen);
  $('ToolFreeze').setAttribute('aria-pressed', !!viewport.frozen);
  const state = $('SceneState');
  state.textContent = building ? 'BUILDING' : viewport.frozen ? 'LOD FROZEN' : 'LIVE';
  state.classList.toggle('live', !building && !viewport.frozen);
}
for (const [id, key] of TOGGLES) $(id).addEventListener('click', () => { settings[key] = !settings[key]; refreshToggles(); });
function setFrozen(on) {
  if (on) { updateEye(); viewport.freeze(camera, viewport.canvas.width, viewport.canvas.height); } else viewport.unfreeze();
  refreshToggles();
}
$('ToolFreeze').addEventListener('click', () => setFrozen(!viewport.frozen));
$('ToolFit').addEventListener('click', fitAll);

// --- View modes --------------------------------------------------------------------------------------------
const modeHost = $('ViewModes');
VIEW_MODES.forEach((mode, index) => {
  const button = document.createElement('button');
  button.textContent = mode.label;
  button.title = `${mode.hint} (${index + 1})`;
  button.setAttribute('role', 'radio');
  button.addEventListener('click', () => setViewMode(mode.id));
  modeHost.append(button);
});
function setViewMode(id) {
  settings.viewMode = id;
  [...modeHost.children].forEach((button, index) => {
    const on = VIEW_MODES[index].id === id;
    button.classList.toggle('active', on);
    button.setAttribute('aria-checked', on);
  });
  const mode = VIEW_MODES.find((m) => m.id === id);
  const legend = $('Legend');
  if (id === 'levels') legend.innerHTML = `LOD level<div class="ramp"></div><div class="ends"><span>0 · source</span><span>${built ? built.levels.length - 1 : ''} · coarsest</span></div>`;
  else if (id === 'budget') legend.innerHTML = `Projected error ÷ threshold<div class="ramp"></div><div class="ends"><span>0 %</span><span>100 %</span></div>`;
  else legend.textContent = mode.hint;
}

// --- Outliner: sources + levels ----------------------------------------------------------------------------
function sourceKeyOf(id) {
  return id === 'obj' ? 'obj:' + build.objName : id;
}
function currentSource() {
  const preset = SOURCE_PRESETS.find((p) => p.id === build.preset);
  return build.objText ? { key: sourceKeyOf('obj'), name: build.objName } : { key: preset.id, name: preset.label };
}
function renderSources() {
  const host = $('SourceRows');
  host.innerHTML = '';
  const entries = SOURCE_PRESETS.map((p) => ({ id: p.id, label: p.label, note: p.note }));
  if (build.objText) entries.push({ id: 'obj', label: build.objName, note: 'Imported OBJ' });
  for (const entry of entries) {
    const active = build.objText ? entry.id === 'obj' : entry.id === build.preset;
    const row = document.createElement('button');
    row.className = 'entity' + (active ? ' active' : '');
    const saved = iconCapture ? iconCapture.iconFor(sourceKeyOf(entry.id)) : null;
    row.innerHTML = `<span class="entity-icon">${saved ? `<img class="source-thumb" alt="" src="${saved}">` : renderIcon('cube', { size: 18 })}</span><div><b></b><small></small></div><span class="${active ? 'status-disc' : 'muted-disc'}">${active ? renderIcon('check', { size: 10, mono: true }) : ''}</span>`;
    row.querySelector('b').textContent = entry.label;
    row.querySelector('small').textContent = entry.note;
    row.addEventListener('click', () => {
      if (entry.id === 'obj') return;
      build.preset = entry.id; build.objText = null; build.transform = identityTransform();
      requestBuild();
    });
    host.append(row);
  }
}
$('ObjInput').addEventListener('change', async (event) => {
  const file = event.target.files[0];
  event.target.value = '';
  if (!file) return;
  build.objText = await file.text();
  build.objName = file.name;
  build.transform = identityTransform();
  requestBuild();
});
document.querySelector('.entity.import').addEventListener('keydown', (event) => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); $('ObjInput').click(); } });

let levelNodes = [];
function renderLevels() {
  const host = $('LevelRows');
  host.innerHTML = '';
  levelNodes = [];
  const select = $('ForcedLevel');
  select.innerHTML = '<option value="-1">Automatic</option>';
  const add = (level, title, note, color) => {
    const row = document.createElement('button');
    row.className = 'entity level';
    row.title = level < 0 ? 'Pick clusters per view from the whole hierarchy' : 'Construct exactly this level everywhere';
    row.innerHTML = `<span class="swatch" style="background:${color}"></span><div><b></b><small></small></div><div><div class="share"></div><div class="meter"><i></i></div></div>`;
    row.querySelector('b').textContent = title;
    row.querySelector('small').textContent = note;
    row.addEventListener('click', () => setForcedLevel(settings.forcedLevel === level ? -1 : level));
    host.append(row);
    levelNodes.push({ level, row, share: row.querySelector('.share'), meter: row.querySelector('.meter i') });
  };
  add(-1, 'Automatic', 'View-dependent cut through all levels', 'linear-gradient(135deg,#306bf2,#59d15c,#f04d3d)');
  built.levels.forEach((info, level) => {
    add(level, `LOD ${level}`, `${number(info.clusters)} clusters · ${compact(info.triangles)} tris${level ? ' · err ' + info.error.toPrecision(2) : ''}`, ramp(level / Math.max(1, built.levels.length - 1)));
    select.insertAdjacentHTML('beforeend', `<option value="${level}">LOD ${level} · ${compact(info.triangles)} tris</option>`);
  });
  setForcedLevel(-1);
}
function setForcedLevel(level) {
  settings.forcedLevel = level;
  $('ForcedLevel').value = String(level);
  for (const node of levelNodes) node.row.classList.toggle('active', node.level === level);
}
$('ForcedLevel').addEventListener('change', (event) => setForcedLevel(+event.target.value));

// --- Build -------------------------------------------------------------------------------------------------
const BUDGETS = [20000, 60000, 200000, 500000, 1000000];
if (!BUDGETS.includes(build.triangleBudget)) BUDGETS.push(build.triangleBudget);
$('TriangleBudget').innerHTML = BUDGETS.sort((a, b) => a - b).map((b) => `<option value="${b}">${compact(b)}</option>`).join('');
$('TriangleBudget').value = String(build.triangleBudget);
$('TriangleBudget').addEventListener('change', (event) => { build.triangleBudget = +event.target.value; if (build.objText) markStale(); else requestBuild(); });
$('Rebuild').addEventListener('click', requestBuild);

let worker = null, ticket = 0;
let autoOpenSdf = query.has('sdf');
function requestBuild() {
  if (worker) worker.terminate();
  worker = new Worker(new URL('./Engine/HierarchyWorker.js', import.meta.url), { type: 'module' });
  const mine = ++ticket;
  building = true;
  const overlay = $('BuildOverlay');
  overlay.classList.remove('failed');
  overlay.classList.add('open');
  $('BuildLabel').textContent = 'Building cluster hierarchy';
  $('BuildNote').textContent = '';
  $('BuildBar').style.width = '0%';
  renderSources();
  refreshToggles();
  worker.onmessage = (event) => {
    const message = event.data;
    if (message.ticket !== mine) return;
    if (message.type === 'progress') { $('BuildBar').style.width = message.fraction * 100 + '%'; $('BuildNote').textContent = message.label; }
    else if (message.type === 'error') fail(message.message);
    else if (message.type === 'done') adopt(message.result);
  };
  worker.onerror = (event) => fail(event.message || 'Worker failed to start');
  worker.postMessage({ ticket: mine, request: { preset: build.preset, triangleBudget: build.triangleBudget, objText: build.objText, transform: build.transform, build: { clusterTriangles: build.clusterTriangles, groupClusters: build.groupClusters, simplifyRatio: build.simplifyRatio } } });
}
function fail(message) {
  building = false;
  $('BuildOverlay').classList.add('failed');
  $('BuildLabel').textContent = 'Build failed';
  $('BuildNote').textContent = message;
  if (geometry) setTimeout(() => $('BuildOverlay').classList.remove('open'), 3500);
  if (build.objText) { build.objText = null; renderSources(); }
  refreshToggles();
}
function adopt(result) {
  building = false;
  geometry = result; built = result.hierarchy;
  try { viewport.setGeometry(result); } catch (error) { fail(error.message); return; }
  settings.selected = null;
  viewport.unfreeze();
  viewport.setInstanceGrid(layout.side, layout.spacing);
  $('BuildOverlay').classList.remove('open');
  $('Rebuild').classList.remove('stale');
  const preset = SOURCE_PRESETS.find((p) => p.id === build.preset);
  const title = build.objText ? build.objName : preset.label;
  $('GeometryTitle').textContent = title;
  $('GeometryNote').textContent = `${result.closed ? 'Closed surface' : 'Open or imported surface'} · ${number(built.sourceTriangles)} triangles · ${number(result.vertexCount)} vertices`;
  $('SceneTitle').textContent = title;
  $('OutlinerSummary').textContent = `${title} · ${built.levels.length} levels`;
  $('HierarchySummary').textContent = `${number(built.clusterCount)} clusters · ${number(built.groupCount)} groups`;
  const top = built.levels[built.levels.length - 1];
  $('BuildReport').textContent = `Built in ${(built.buildMs / 1000).toFixed(2)} s: ${built.levels.length} levels, coarsest ${number(top.triangles)} triangles in ${top.clusters} cluster${top.clusters > 1 ? 's' : ''}.` +
    (built.stalled ? ' Stopped early: locked borders left too little to simplify — try larger clusters or groups.' : '');
  $('FootBuild').textContent = (built.buildMs / 1000).toFixed(2) + ' s';
  $('FootGpu').textContent = (viewport.geometry.gpuBytes / 1048576).toFixed(1) + ' MB';
  renderSources();
  renderLevels();
  setViewMode(settings.viewMode);
  fitAll();
  if (iconCapture.active) iconCapture.setActive(true);
  uvWorkspace.prepare();
  sdfWorkspace.onGeometryChanged();
  refreshToggles();
  renderPicked();
  if (autoOpenSdf) { autoOpenSdf = false; sdfWorkspace.show(); }
}

// --- Picked cluster ----------------------------------------------------------------------------------------
function lodViewFor(instanceIndex) {
  const lod = viewport.lodView, instance = viewport.instances[instanceIndex];
  if (!lod || !instance) return null;
  return { eye: viewport.toLocal(instance, lod.eye), focal: lod.focal, near: lod.near };
}
function renderPicked() {
  const host = $('PickedPanel'), pick = settings.selected;
  if (!pick || !built) { host.innerHTML = '<p>Click a cluster in the viewport to see its level, error, bounds and why it was chosen. Double-click to orbit around it.</p>'; return; }
  const c = pick.cluster, self = built.clusterGroupSelf[c], parent = built.clusterGroupParent[c];
  const view = lodViewFor(pick.instance);
  const own = view ? projectedError(built, self, view) : 0, up = parent < 0 || !view ? Infinity : projectedError(built, parent, view);
  const px = (v) => (Number.isFinite(v) ? v.toFixed(v < 10 ? 2 : 1) + ' px' : '∞');
  const units = (g) => (g < 0 ? '0' : built.groupError[g].toExponential(2));
  const children = self < 0 ? 0 : built.groupChildStart[self + 1] - built.groupChildStart[self];
  const siblings = parent < 0 ? 0 : built.groupChildStart[parent + 1] - built.groupChildStart[parent];
  let verdict;
  if (settings.forcedLevel >= 0) verdict = `Level is forced to LOD ${settings.forcedLevel}; the error test is bypassed.`;
  else verdict = `Chosen because its own error <b>${px(own)}</b> ≤ ${px(settings.threshold)} while its parent would be <b>${px(up)}</b> &gt; ${px(settings.threshold)}.` + (parent < 0 ? ' It is a root: nothing coarser exists.' : '');
  host.innerHTML = `<div class="rows">
    <span>Cluster</span><b>#${c} · instance ${pick.instance}</b>
    <span>Level</span><b><i class="dot" style="background:${ramp(built.clusterLevel[c] / Math.max(1, built.levels.length - 1))}"></i>LOD ${built.clusterLevel[c]} of ${built.levels.length - 1}</b>
    <span>Triangles</span><b>${built.clusterSize[c] / 3}</b>
    <span>Own error</span><b>${units(self)} u → ${px(own)}</b>
    <span>Parent error</span><b>${parent < 0 ? 'root' : units(parent) + ' u → ' + px(up)}</b>
    <span>Tight radius</span><b>${built.clusterSphere[c * 4 + 3].toFixed(4)} u</b>
    <span>Built from</span><b>${children ? children + ' finer clusters' : 'source triangles'}</b>
    <span>Simplifies with</span><b>${siblings ? siblings - 1 + ' neighbours' : '—'}</b>
    <span>Cone cull</span><b>${built.clusterCone[c * 4 + 3] < 1.5 ? 'eligible' : 'too curved'}</b>
  </div>
  <div class="verdict">${verdict}</div>
  <small>Bounds: <b style="color:#fff">white</b> tight · <b style="color:#ffb238">amber</b> own LOD sphere · <b style="color:#59a6ff">blue</b> parent LOD sphere</small>
  <div class="hop"><button id="PickLevel">Force LOD ${built.clusterLevel[c]}</button><button id="PickClear">Clear</button></div>`;
  $('PickLevel').addEventListener('click', () => setForcedLevel(built.clusterLevel[c]));
  $('PickClear').addEventListener('click', () => { settings.selected = null; renderPicked(); });
}

// --- Camera input ------------------------------------------------------------------------------------------
const canvas = $('Viewport');
const pointers = new Map();
let dragMode = null, moved = 0, pinch = 0;
canvas.addEventListener('contextmenu', (event) => event.preventDefault());
canvas.addEventListener('pointerdown', (event) => {
  canvas.focus({ preventScroll: true });
  canvas.setPointerCapture(event.pointerId);
  pointers.set(event.pointerId, [event.clientX, event.clientY]);
  moved = 0;
  dragMode = event.button === 2 || event.button === 1 || event.shiftKey ? 'pan' : 'orbit';
  if (pointers.size === 2) { const [a, b] = [...pointers.values()]; pinch = Math.hypot(a[0] - b[0], a[1] - b[1]); dragMode = 'pinch'; moved = 99; }
  canvas.classList.add('dragging');
});
canvas.addEventListener('pointermove', (event) => {
  const previous = pointers.get(event.pointerId);
  if (!previous) return;
  const dx = event.clientX - previous[0], dy = event.clientY - previous[1];
  pointers.set(event.pointerId, [event.clientX, event.clientY]);
  moved += Math.abs(dx) + Math.abs(dy);
  if (dragMode === 'pinch' && pointers.size === 2) {
    const [a, b] = [...pointers.values()], d = Math.hypot(a[0] - b[0], a[1] - b[1]);
    if (pinch > 0 && d > 0) camera.distance = clampDistance(camera.distance * (pinch / d));
    pinch = d;
  } else if (dragMode === 'orbit') {
    camera.yaw -= dx * 0.006;
    camera.pitch = Math.min(1.5, Math.max(-1.5, camera.pitch + dy * 0.006));
  } else if (dragMode === 'pan') {
    const scale = (camera.distance * Math.tan(camera.fov / 2) * 2) / canvas.clientHeight;
    const rx = Math.cos(camera.yaw), rz = -Math.sin(camera.yaw);
    const ux = -Math.sin(camera.pitch) * Math.sin(camera.yaw), uy = Math.cos(camera.pitch), uz = -Math.sin(camera.pitch) * Math.cos(camera.yaw);
    camera.target[0] += (-dx * rx + dy * ux) * scale; camera.target[1] += dy * uy * scale; camera.target[2] += (-dx * rz + dy * uz) * scale;
  }
});
function pickAt(event) {
  updateEye();
  return viewport.pick(camera, settings, event.clientX, event.clientY);
}
const release = (event) => {
  if (!pointers.delete(event.pointerId)) return;
  if (pointers.size === 0) {
    canvas.classList.remove('dragging');
    if (moved < 5 && event.type === 'pointerup' && event.button === 0 && built) { settings.selected = pickAt(event); renderPicked(); }
    dragMode = null;
  }
};
canvas.addEventListener('pointerup', release);
canvas.addEventListener('pointercancel', release);
canvas.addEventListener('dblclick', (event) => {
  if (!built) return;
  const pick = pickAt(event);
  if (!pick) return;
  const instance = viewport.instances[pick.instance], s = built.clusterSphere, o = pick.cluster * 4;
  camera.target = [s[o] * instance.cos + s[o + 2] * instance.sin + instance.x, s[o + 1], -s[o] * instance.sin + s[o + 2] * instance.cos + instance.z];
  updateEye();
});
const clampDistance = (d) => Math.min(4000, Math.max(0.02, d));
canvas.addEventListener('wheel', (event) => {
  event.preventDefault();
  camera.distance = clampDistance(camera.distance * Math.exp(event.deltaY * (event.deltaMode ? 0.05 : 0.0012)));
}, { passive: false });

const held = new Set();
window.addEventListener('keydown', (event) => {
  if (event.target.matches('input, select, textarea') || event.ctrlKey || event.metaKey || event.altKey) return;
  if (sdfWorkspace.open) { sdfWorkspace.keydown(event); return; }
  if (uvWorkspace.open) { uvWorkspace.keydown(event); return; }
  if (geometryEditor.open) { geometryEditor.keydown(event); return; }
  const key = event.key.toLowerCase();
  if (key === 'i') { iconCapture.setActive(!iconCapture.active); return; }
  if (key === 'p' && iconCapture.active) { iconCapture.capture(); return; }
  if (key === 'u') { uvWorkspace.show(); return; }
  if (key === 'v') { sdfWorkspace.show(); return; }
  if ('wasdqe'.includes(key) && key.length === 1) { held.add(key); event.preventDefault(); return; }
  if (key >= '1' && key <= String(VIEW_MODES.length)) setViewMode(VIEW_MODES[+key - 1].id);
  else if (key === 'z') { settings.wireframe = !settings.wireframe; refreshToggles(); }
  else if (key === 'b') { settings.bounds = !settings.bounds; refreshToggles(); }
  else if (key === 'g') { settings.grid = !settings.grid; refreshToggles(); }
  else if (key === 'c') { settings.backfaceCull = !settings.backfaceCull; refreshToggles(); }
  else if (key === 'l') setFrozen(!viewport.frozen);
  else if (key === 'f') fitAll();
  else if (key === '[' || key === ']') { settings.threshold = Math.min(32, Math.max(0.1, settings.threshold * (key === ']' ? 1.25 : 0.8))); sliderNodes.threshold(); }
  else if (key === 'escape') {
    if ($('Notch').classList.contains('open')) toggleNotch(false);
    else if (iconCapture.active) iconCapture.setActive(false);
    else { settings.selected = null; renderPicked(); }
  }
});
window.addEventListener('keyup', (event) => held.delete(event.key.toLowerCase()));
window.addEventListener('blur', () => held.clear());
function fly(dt, fast) {
  if (!held.size) return;
  const step = Math.max(0.3, camera.distance) * dt * (fast ? 3 : 1);
  const cp = Math.cos(camera.pitch);
  const forward = [-Math.sin(camera.yaw) * cp, -Math.sin(camera.pitch), -Math.cos(camera.yaw) * cp], right = [Math.cos(camera.yaw), 0, -Math.sin(camera.yaw)];
  const move = [0, 0, 0];
  const add = (v, s) => { move[0] += v[0] * s; move[1] += v[1] * s; move[2] += v[2] * s; };
  if (held.has('w')) add(forward, 1); if (held.has('s')) add(forward, -1);
  if (held.has('d')) add(right, 1); if (held.has('a')) add(right, -1);
  if (held.has('e')) add([0, 1, 0], 1); if (held.has('q')) add([0, 1, 0], -1);
  for (let i = 0; i < 3; i++) camera.target[i] += move[i] * step;
}
let shiftHeld = false;
window.addEventListener('keydown', (event) => { shiftHeld = event.shiftKey; });
window.addEventListener('keyup', (event) => { shiftHeld = event.shiftKey; });

// --- Notch + drawer ----------------------------------------------------------------------------------------
function toggleNotch(open) {
  const shell = $('Notch');
  const state = open === undefined ? !shell.classList.contains('open') : open;
  shell.classList.toggle('open', state);
  $('NotchHandle').setAttribute('aria-expanded', state);
}
$('NotchHandle').addEventListener('click', () => toggleNotch());
$('NotchClose').addEventListener('click', () => toggleNotch(false));
$('ToolHelp').addEventListener('click', () => toggleNotch(true));
$('DrawerHandle').addEventListener('click', () => {
  const open = $('Drawer').classList.toggle('open');
  $('DrawerHandle').setAttribute('aria-expanded', open);
});
if (query.has('drawer')) $('Drawer').classList.add('open');

// --- Charts ------------------------------------------------------------------------------------------------
function fitCanvas(node) {
  const ratio = Math.min(window.devicePixelRatio || 1, 2), w = Math.round(node.clientWidth * ratio), h = Math.round(node.clientHeight * ratio);
  if (node.width !== w || node.height !== h) { node.width = w; node.height = h; }
  const g = node.getContext('2d');
  g.setTransform(ratio, 0, 0, ratio, 0, 0);
  g.clearRect(0, 0, node.clientWidth, node.clientHeight);
  return g;
}
function renderLevelChart(stats) {
  const node = $('LevelChart');
  if (!node.clientWidth || !built) return;
  const g = fitCanvas(node), w = node.clientWidth, h = node.clientHeight, count = built.levels.length;
  const slot = w / count, base = h - 16, instances = viewport.instances.length;
  const ceiling = Math.log10(Math.max(10, built.levels[0].triangles * instances));
  const height = (n) => (n > 0 ? Math.max(2, (Math.log10(Math.max(1, n)) / ceiling) * (base - 14)) : 0);
  g.font = '10px "DM Sans", system-ui, sans-serif';
  g.textAlign = 'center';
  built.levels.forEach((info, level) => {
    const x = level * slot + slot * 0.18, bw = slot * 0.64;
    const stored = height(info.triangles * instances), drawn = height(stats.levelTriangles[level]);
    g.fillStyle = '#2a2a2a';
    g.fillRect(x, base - stored, bw, stored);
    g.fillStyle = ramp(level / Math.max(1, count - 1));
    g.fillRect(x, base - drawn, bw, drawn);
    g.fillStyle = '#8a8a8a';
    g.fillText('LOD ' + level, x + bw / 2, h - 3);
    if (stats.levelTriangles[level]) { g.fillStyle = '#e6e6e6'; g.fillText(compact(stats.levelTriangles[level]), x + bw / 2, base - Math.max(stored, drawn) - 4); }
  });
}
const timeHistory = new Float32Array(240);
let timeCursor = 0;
function renderTimeChart() {
  const node = $('TimeChart');
  if (!node.clientWidth) return;
  const g = fitCanvas(node), w = node.clientWidth, h = node.clientHeight;
  let peak = 20;
  for (const v of timeHistory) peak = Math.max(peak, v);
  g.strokeStyle = '#333'; g.lineWidth = 1;
  const guide = h - (16.7 / peak) * h;
  g.beginPath(); g.moveTo(0, guide); g.lineTo(w, guide); g.stroke();
  g.strokeStyle = '#d8d8d8'; g.beginPath();
  for (let i = 0; i < timeHistory.length; i++) {
    const v = timeHistory[(timeCursor + i) % timeHistory.length], x = (i / (timeHistory.length - 1)) * w, y = h - Math.min(1, v / peak) * (h - 2) - 1;
    if (i) g.lineTo(x, y); else g.moveTo(x, y);
  }
  g.stroke();
}

// --- Sub-Workspaces: Icon camera + UV Workspace + SDF Workspace + Geometry Editor --------------------------
const iconCapture = createIconCapture({
  viewport, camera, settings, updateEye, toast, currentSource,
  onLibraryChange: () => renderSources(),
});
const uvWorkspace = createUvWorkspace({
  getGeometry: () => geometry,
  getName: () => currentSource().name,
  toast,
});
const sdfWorkspace = createSdfWorkspace({
  getGeometry: () => geometry,
  getName: () => currentSource().name,
  toast,
});
const geometryEditor = createGeometryEditor({
  getBase: () => (geometry ? geometry.base : null),
  getTransform: () => build.transform,
  applyTransform: (transform) => { build.transform = transform; requestBuild(); },
  getName: () => currentSource().name,
  toast,
});

// --- Tick loop --------------------------------------------------------------------------------------------
let last = performance.now(), smoothed = 16.7, nextReadout = 0;
function tick(now) {
  const dt = Math.min(0.1, (now - last) / 1000);
  last = now;
  smoothed += (dt * 1000 - smoothed) * 0.1;
  timeHistory[timeCursor] = dt * 1000; timeCursor = (timeCursor + 1) % timeHistory.length;
  fly(dt, shiftHeld);
  updateEye();
  // Pause WebGL2 main redraw while the modal SDF Workspace is active so WebGPU gets 100% of the GPU
  const stats = sdfWorkspace.open ? null : viewport.render(camera, settings);
  if (stats && now >= nextReadout && built) {
    nextReadout = now + 200;
    const instances = viewport.instances.length, sourceTotal = built.sourceTriangles * instances, storedClusters = built.clusterCount * instances;
    const share = sourceTotal ? stats.triangles / sourceTotal : 0;
    $('StatDrawn').textContent = compact(stats.clusters);
    $('StatCulled').textContent = compact(stats.culledFrustum + stats.culledBackface);
    $('MetricTriangles').textContent = compact(stats.triangles);
    $('MetricSource').textContent = compact(sourceTotal);
    $('MetricClusters').textContent = `${compact(stats.clusters)} / ${compact(storedClusters)}`;
    $('MetricTime').textContent = `${smoothed.toFixed(1)} ms`;
    $('HeroPercent').textContent = (share * 100).toFixed(share < 0.1 ? 2 : 1) + ' %';
    $('HeroFill').style.width = Math.min(100, share * 100) + '%';
    $('FootSubmit').textContent = `${stats.drawCalls} call${stats.drawCalls === 1 ? '' : 's'}`;
    $('SceneNote').textContent = `${instances} instance${instances > 1 ? 's' : ''} · ${stats.instancesVisible} in view`;
    $('Readout').textContent =
      `${(1000 / smoothed).toFixed(0).padStart(4)} fps   ${smoothed.toFixed(1)} ms\n` +
      `tris     ${number(stats.triangles)}\n` +
      `clusters ${number(stats.clusters)}\n` +
      `tests    ${number(stats.tests)}  (of ${compact(storedClusters)})\n` +
      `culled   ${number(stats.culledFrustum)} frustum · ${number(stats.culledBackface)} cone\n` +
      `select   ${stats.selectMs.toFixed(2)} ms cpu`;
    $('StripText').textContent = `WEBGL2 PREVIEW · ${viewport.multiDraw ? 'MULTI-DRAW' : 'RANGE LOOP'} · ${stats.ranges} RANGES · PIXEL ERROR ${settings.threshold.toFixed(2)} · ${settings.forcedLevel < 0 ? 'AUTOMATIC CUT' : 'FORCED LOD ' + settings.forcedLevel}`;
    $('TimeNote').textContent = `${smoothed.toFixed(1)} ms average · guide at 16.7 ms`;
    for (const node of levelNodes) {
      if (node.level < 0) { node.share.textContent = compact(stats.triangles); node.meter.style.width = Math.min(100, share * 100) + '%'; continue; }
      const drawn = stats.levelTriangles[node.level];
      node.share.textContent = drawn ? compact(drawn) : '—';
      node.meter.style.width = (stats.triangles ? (drawn / stats.triangles) * 100 : 0) + '%';
      node.meter.style.background = ramp(node.level / Math.max(1, built.levels.length - 1));
    }
    if ($('Drawer').classList.contains('open')) { renderLevelChart(stats); renderTimeChart(); }
    if (settings.selected) renderPickedLive();
  }
  requestAnimationFrame(tick);
}

let pickedStamp = '';
function renderPickedLive() {
  const stamp = JSON.stringify([settings.selected, settings.threshold, settings.forcedLevel, viewport.lodView && viewport.lodView.eye.map((v) => v.toFixed(3))]);
  if (stamp !== pickedStamp && !$('PickedPanel').matches(':hover')) { pickedStamp = stamp; renderPicked(); }
}

setViewMode(settings.viewMode);
refreshToggles();
renderPicked();
requestBuild();
requestAnimationFrame(tick);

window.GeometryCompiler = {
  settings, build, layout, camera, viewport, iconCapture, uvWorkspace, sdfWorkspace, geometryEditor,
  get geometry() { return geometry; },
  get hierarchy() { return built; },
  get building() { return building; },
  setViewMode, setForcedLevel, setFrozen, fitAll, requestBuild,
};
window.ClusterLodStudio = window.GeometryCompiler;
