// Single-asset Cluster LOD workspace, using Geometry Compiler Studio's WebGL2 renderer and selection logic.
import { ViewportRasterization, VIEW_MODES } from '../GeometryCompiler/Engine/ViewportRasterization.js';
import { computeNormals } from '../GeometryCompiler/Engine/SourceGeometry.js';
import { projectedError } from '../GeometryCompiler/Engine/ClusterSelection.js';
import { invalidate, runStep } from '../Shared/GeometryCompile/CompileSteps.js';
import { el, toast } from '../Shared/Studio/Ui.js';

const fmt = (n) => Math.round(n).toLocaleString('en-US');
const colour = (i, count) => `hsl(${Math.round(220 - 220 * i / Math.max(1, count - 1))} 75% 60%)`;
const html = `
<section class="dock left" aria-label="LOD outliner">
  <div class="tab-strip"><div class="document-tab active">Outliner</div></div>
  <div class="outliner-heading"><h1>Outliner</h1><small data-id="summary">—</small></div>
  <div class="stats"><div class="stat-tile"><small>Visible</small><strong data-id="visible">0</strong></div><div class="stat-tile"><small>Culled</small><strong data-id="culled">0</strong></div></div>
  <div class="outliner-scroll"><div class="collection"><div><b>Mesh</b><small data-id="mesh-name"></small></div></div><div class="collection"><div><b>LOD hierarchy</b><small data-id="hierarchy">Not built</small></div></div><div data-id="levels"></div></div>
  <footer class="dock-footer"><span><i>BUILD</i><b data-id="build-time">—</b></span><span><i>GPU</i><b data-id="gpu">—</b></span><span><i>SUBMIT</i><b data-id="submit">—</b></span></footer>
</section>
<section class="dock centre" aria-label="Cluster viewport">
  <div class="tab-strip"><div class="document-tab active">Viewport</div><button type="button" class="tool cluster-back" data-id="back">← Component Viewer</button></div>
  <header class="viewport-heading"><div class="scene-title"><b data-id="title">Cluster / LOD</b><small>One selected mesh</small></div><span class="state-tag live" data-id="state">LIVE</span></header>
  <div class="viewport-tools"><span class="cluster-debug-label">DEBUG</span><div class="segmented" role="group" aria-label="Cluster debug modes" data-id="modes"></div></div>
  <div class="viewport-tools"><button type="button" class="tool" data-toggle="wireframe">Wire</button><button type="button" class="tool active" data-toggle="bounds">Bounds</button><button type="button" class="tool active" data-toggle="grid">Grid</button><button type="button" class="tool" data-id="freeze">Freeze LOD</button><button type="button" class="tool" data-id="fit">Fit</button></div>
  <div class="canvas-well"><canvas data-id="canvas" tabindex="0" aria-label="Cluster LOD viewport"></canvas><div class="readout" data-id="readout"></div><div class="legend" data-id="legend"></div><div class="cluster-overlay" data-id="overlay" hidden><b data-id="progress">Building hierarchy…</b><small data-id="progress-detail"></small></div></div>
  <footer class="strip" data-id="strip">WEBGL2 · ONE ASSET</footer>
</section>
<section class="dock right" aria-label="Cluster inspector">
  <div class="tab-strip"><div class="document-tab active">Inspector</div></div>
  <div class="inspector-scroll cluster-inspector">
    <div class="crumbs">Inspector / Cluster LOD</div><h1 data-id="asset-title">—</h1><div class="subtitle" data-id="asset-note">—</div>
    <div class="hero"><div class="hero-bar"><i data-id="hero-fill"></i></div><div class="hero-text"><b data-id="hero-percent">—</b><small>of source triangles constructed</small></div></div>
    <div class="metrics"><div><b data-id="triangles">—</b><small>TRIANGLES</small></div><div><b data-id="source">—</b><small>SOURCE</small></div><div><b data-id="clusters">—</b><small>CLUSTERS</small></div><div><b data-id="time">—</b><small>REDRAW</small></div></div>
    <h4>LOD SELECTION</h4><div class="panel"><label class="field"><span>Pixel error</span><output data-id="threshold-value">1.00 px</output><input data-id="threshold" type="range" min="0" max="1" step="0.001" value="0.4"><small>A coarser group is chosen when its projected error is below this.</small></label><label class="field inline"><span>Level</span><select data-id="forced"></select></label><div class="tiles"><button class="tile on" data-toggle="frustumCull">Frustum cull<small>ON</small></button><button class="tile on" data-toggle="backfaceCull">Cone cull<small>ON</small></button></div></div>
    <h4>PICKED CLUSTER</h4><div class="panel" data-id="picked"><p>Click a cluster in the viewport to inspect its level, groups, triangle count and error.</p></div>
    <h4>DISPLAY</h4><div class="panel"><div class="tiles"><button class="tile" data-toggle="flatShading">Flat shading<small>OFF</small></button><button class="tile on" data-toggle="cullBackTriangles">Back faces<small>CULLED</small></button></div></div>
    <h4>BUILD</h4><div class="panel" data-id="build-controls"></div><p class="muted-note">Experimental cluster hierarchy and view-dependent selection, not a Nanite runtime. Editing this mesh updates the shared asset.</p>
  </div>
</section>`;

export function createClusterWorkspace({ ctx, onClose, onAssetUpdated }) {
  const root = el('div', { id: 'ClusterWorkspace', class: 'workspace', hidden: '', 'aria-label': 'Cluster LOD editor' });
  root.innerHTML = html;
  document.body.append(root);
  const $ = (key) => root.querySelector(`[data-id="${key}"]`);
  const state = { threshold: 1, forcedLevel: -1, frustumCull: true, backfaceCull: true, cullBackTriangles: true, viewMode: 'clusters', wireframe: false, bounds: true, grid: true, flatShading: false, selected: null };
  const camera = { target: [0, 0, 0], yaw: 0.7, pitch: 0.38, distance: 6, fov: Math.PI * 50 / 180, eye: [0, 0, 0] };
  let viewport = null, asset = null, built = null, raf = 0, shown = false, session = 0, busy = false, last = 0, avg = 16.7, readoutAt = 0;
  const pending = new WeakSet();
  function eye() { const cp = Math.cos(camera.pitch); camera.eye = [camera.target[0] + Math.sin(camera.yaw) * cp * camera.distance, camera.target[1] + Math.sin(camera.pitch) * camera.distance, camera.target[2] + Math.cos(camera.yaw) * cp * camera.distance]; }
  function fit() { if (!viewport?.geometry) return; camera.target = [...viewport.geometry.centre]; camera.distance = Math.max(.05, viewport.geometry.fitRadius * 3.3); camera.yaw = .7; camera.pitch = .38; eye(); }
  function setMode(id) { state.viewMode = id; root.querySelectorAll('[data-mode]').forEach((b) => { const active = b.dataset.mode === id; b.classList.toggle('active', active); b.setAttribute('aria-pressed', String(active)); }); const mode = VIEW_MODES.find((m) => m.id === id); $('legend').textContent = mode?.hint || ''; }
  for (const mode of VIEW_MODES) { const b = el('button', { type: 'button', dataset: { mode: mode.id }, title: mode.hint }, mode.label); b.addEventListener('click', () => setMode(mode.id)); $('modes').append(b); }
  function setLevel(level) { state.forcedLevel = level; $('forced').value = String(level); root.querySelectorAll('[data-level]').forEach((b) => b.classList.toggle('active', +b.dataset.level === level)); }
  $('forced').addEventListener('change', (e) => setLevel(+e.target.value));
  const slider = $('threshold'); slider.value = String(Math.log(10) / Math.log(320));
  slider.addEventListener('input', () => { state.threshold = .1 * Math.pow(320, +slider.value); $('threshold-value').textContent = `${state.threshold.toFixed(2)} px`; });
  for (const b of root.querySelectorAll('[data-toggle]')) b.addEventListener('click', () => { const key = b.dataset.toggle; state[key] = !state[key]; b.classList.toggle(b.classList.contains('tile') ? 'on' : 'active', state[key]); b.setAttribute('aria-pressed', String(state[key])); const small = b.querySelector('small'); if (small) small.textContent = key === 'cullBackTriangles' ? (state[key] ? 'CULLED' : 'SHOWN') : state[key] ? 'ON' : 'OFF'; });
  $('fit').addEventListener('click', fit);
  $('freeze').addEventListener('click', () => { if (!viewport?.geometry) return; if (viewport.frozen) viewport.unfreeze(); else { eye(); viewport.freeze(camera, viewport.canvas.width, viewport.canvas.height); } $('freeze').classList.toggle('active', !!viewport.frozen); $('state').textContent = viewport.frozen ? 'LOD FROZEN' : 'LIVE'; });
  $('back').addEventListener('click', close);
  function buildControls() {
    const host = $('build-controls'); host.innerHTML = '';
    for (const [key, label, min, max, step] of [['clusterTriangles', 'Triangles per cluster', 32, 256, 16], ['groupClusters', 'Clusters per group', 2, 8, 1], ['simplifyRatio', 'Keep per level', .3, .7, .05]]) {
      const input = el('input', { type: 'range', min, max, step, value: asset.settings.lod[key], 'aria-label': label });
      const out = el('output', {}, key === 'simplifyRatio' ? `${Math.round(asset.settings.lod[key] * 100)}%` : String(asset.settings.lod[key]));
      input.disabled = busy;
      input.addEventListener('input', () => { out.textContent = key === 'simplifyRatio' ? `${Math.round(+input.value * 100)}%` : input.value; $('rebuild').classList.add('stale'); });
      host.append(el('label', { class: 'field' }, el('span', {}, label), out, input)); input.dataset.build = key;
    }
    const preserve = el('input', { type: 'checkbox', class: 'switch', 'aria-label': 'Preserve materials' }); preserve.checked = asset.settings.lod.preserveMaterials !== false; preserve.disabled = busy; preserve.dataset.build = 'preserveMaterials';
    preserve.addEventListener('change', () => $('rebuild').classList.add('stale'));
    const rebuild = el('button', { type: 'button', class: 'primary', 'data-id': 'rebuild', disabled: busy }, busy ? 'Building…' : 'Rebuild hierarchy'); rebuild.addEventListener('click', rebuildHierarchy);
    host.append(el('label', { class: 'toggle-row' }, el('span', {}, 'Preserve materials'), preserve), rebuild, el('p', { 'data-id': 'build-report' }, built ? report(built) : 'Build a hierarchy for the selected mesh.'));
  }
  function report(h) { const top = h.levels.at(-1); return `Built in ${(h.buildMs / 1000).toFixed(2)} s · ${h.levels.length} levels · ${fmt(h.clusterCount)} clusters · ${fmt(h.groupCount)} groups · coarsest ${fmt(top.triangles)} triangles${h.stalled ? ' · stopped at locked borders' : ''}.`; }
  function levels() {
    const host = $('levels'), select = $('forced'); host.innerHTML = ''; select.innerHTML = '';
    const add = (i, title, note, swatch) => {
      const b = el('button', { type: 'button', class: 'entity level', dataset: { level: String(i) } }, el('span', { class: 'swatch', style: { background: swatch } }), el('div', {}, el('b', {}, title), el('small', {}, note)), el('div', {}, el('div', { class: 'share' }, '—'), el('div', { class: 'meter' }, el('i'))));
      b.addEventListener('click', () => setLevel(i)); host.append(b); select.append(el('option', { value: i }, title));
    };
    add(-1, 'Automatic', 'View-dependent cut', 'linear-gradient(135deg,#306bf2,#59d15c,#f04d3d)');
    if (built) built.levels.forEach((L, i) => add(i, `LOD ${i}`, `${fmt(L.clusters)} clusters · ${fmt(L.triangles)} triangles · error ${Number(L.error || 0).toPrecision(2)}`, colour(i, built.levels.length)));
    setLevel(-1);
  }
  function adopt() {
    built = asset.steps.lod.state === 'done' ? asset.out.lod : null; state.selected = null;
    $('mesh-name').textContent = asset.name;
    $('summary').textContent = built ? `${built.levels.length} levels` : 'No hierarchy';
    $('hierarchy').textContent = built ? `${fmt(built.clusterCount)} clusters · ${fmt(built.groupCount)} groups` : 'Not built';
    $('asset-title').textContent = asset.name;
    const g = asset.out.geometry; $('asset-note').textContent = `${g.closed ? 'Closed' : 'Open'} surface · ${fmt(g.indices.length / 3)} triangles · ${fmt(g.positions.length / 3)} vertices`;
    $('source').textContent = fmt(g.indices.length / 3);
    if (built) {
      let lowest = Infinity; for (let i = 1; i < g.positions.length; i += 3) lowest = Math.min(lowest, g.positions[i]);
      viewport.setGeometry({ positions: g.positions, normals: computeNormals(g.positions, g.indices), indices: g.indices, vertexCount: g.positions.length / 3, hierarchy: built, lowest });
      viewport.setInstanceGrid(1, 3); viewport.unfreeze(); $('freeze').classList.remove('active'); $('state').textContent = 'LIVE';
      $('build-time').textContent = `${(built.buildMs / 1000).toFixed(2)} s`; $('gpu').textContent = `${(viewport.geometry.gpuBytes / 1048576).toFixed(1)} MB`; fit();
    } else { viewport.dispose(); $('build-time').textContent = $('gpu').textContent = '—'; }
    levels(); buildControls(); picked();
  }
  async function rebuildHierarchy() {
    if (!asset || busy || pending.has(asset)) return;
    const target = asset, geometry = target.out.geometry, token = session;
    const config = { ...target.settings.lod, enabled: true };
    for (const input of root.querySelectorAll('[data-build]')) config[input.dataset.build] = input.type === 'checkbox' ? input.checked : +input.value;
    // Build on a detached state snapshot so closing the editor or changing geometry cannot publish stale worker results.
    const draft = { ...target, settings: { ...target.settings, lod: config }, steps: Object.fromEntries(Object.entries(target.steps).map(([k, v]) => [k, { ...v }])), out: { ...target.out } };
    invalidate(draft, 'lod'); pending.add(target); busy = true; buildControls(); $('overlay').hidden = false; $('progress').textContent = 'Building cluster hierarchy'; $('progress-detail').textContent = '';
    try {
      await runStep(draft, 'lod', ctx, (_id, fraction, label) => { if (shown && asset === target && session === token) $('progress-detail').textContent = `${label || ''} ${Math.round(fraction * 100)}%`; });
      if (target.out.geometry !== geometry) { toast('Geometry changed during the build; rebuild the hierarchy'); return; }
      target.settings.lod = config; target.out.lod = draft.out.lod; target.steps.lod = draft.steps.lod; target.revision++; onAssetUpdated(target);
      if (shown && asset === target) { busy = false; adopt(); }
    } catch (error) { toast(`Cluster build: ${error.message || error}`); if (shown && asset === target && session === token) $('progress-detail').textContent = String(error.message || error); }
    finally { pending.delete(target); if (shown && asset === target) { busy = false; $('overlay').hidden = true; buildControls(); } }
  }
  function picked() {
    const box = $('picked'), p = state.selected; box.innerHTML = '';
    if (!p || !built) { box.append(el('p', {}, 'Click a cluster in the viewport to inspect its level, groups, triangle count and error.')); return; }
    const c = p.cluster, own = built.clusterGroupSelf[c], parent = built.clusterGroupParent[c], v = viewport.lodView;
    const local = v && viewport.instances[p.instance] ? { eye: viewport.toLocal(viewport.instances[p.instance], v.eye), focal: v.focal, near: v.near } : null;
    const px = (group) => group < 0 || !local ? '—' : `${projectedError(built, group, local).toFixed(2)} px`;
    const children = own < 0 ? 0 : built.groupChildStart[own + 1] - built.groupChildStart[own];
    const rows = [['Cluster', `#${c}`], ['Level', `LOD ${built.clusterLevel[c]}`], ['Triangles', fmt(built.clusterSize[c] / 3)], ['Group', own < 0 ? 'Source' : `#${own}`], ['Parent group', parent < 0 ? 'Root' : `#${parent}`], ['Own error', px(own)], ['Parent error', parent < 0 ? 'Root' : px(parent)], ['Built from', children ? `${children} finer clusters` : 'Source triangles'], ['Tight radius', `${built.clusterSphere[c * 4 + 3].toFixed(4)} m`]];
    box.append(el('div', { class: 'rows' }, ...rows.flatMap(([a, b]) => [el('span', {}, a), el('b', {}, b)])), el('p', {}, state.forcedLevel < 0 ? `Automatic selection · threshold ${state.threshold.toFixed(2)} px` : `Forced LOD ${state.forcedLevel} · error test bypassed`));
  }
  const canvas = $('canvas'); let drag = null;
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  canvas.addEventListener('pointerdown', (e) => { canvas.focus(); canvas.setPointerCapture(e.pointerId); drag = { id: e.pointerId, x: e.clientX, y: e.clientY, moved: 0, pan: e.button === 1 || e.button === 2 || e.shiftKey }; });
  canvas.addEventListener('pointermove', (e) => { if (!drag || drag.id !== e.pointerId) return; const dx = e.clientX - drag.x, dy = e.clientY - drag.y; drag.x = e.clientX; drag.y = e.clientY; drag.moved += Math.abs(dx) + Math.abs(dy); if (drag.pan) { const scale = camera.distance * Math.tan(camera.fov / 2) * 2 / Math.max(1, canvas.clientHeight); camera.target[0] += (-dx * Math.cos(camera.yaw) - dy * Math.sin(camera.pitch) * Math.sin(camera.yaw)) * scale; camera.target[1] += dy * Math.cos(camera.pitch) * scale; camera.target[2] += (dx * Math.sin(camera.yaw) - dy * Math.sin(camera.pitch) * Math.cos(camera.yaw)) * scale; } else { camera.yaw -= dx * .006; camera.pitch = Math.max(-1.5, Math.min(1.5, camera.pitch + dy * .006)); } });
  canvas.addEventListener('pointerup', (e) => { if (!drag || drag.id !== e.pointerId) return; if (drag.moved < 5 && e.button === 0 && built) { eye(); state.selected = viewport.pick(camera, state, e.clientX, e.clientY); picked(); } drag = null; });
  canvas.addEventListener('pointercancel', () => { drag = null; });
  canvas.addEventListener('wheel', (e) => { e.preventDefault(); camera.distance = Math.min(4000, Math.max(.02, camera.distance * Math.exp(e.deltaY * .0012))); }, { passive: false });
  function tick(now) {
    if (!shown) { raf = 0; return; }
    const dt = Math.min(100, now - (last || now)); last = now; avg += (dt - avg) * .1;
    eye();
    if (built) {
      const stats = viewport.render(camera, state);
      if (stats && now >= readoutAt) {
        readoutAt = now + 200; const source = built.sourceTriangles || 1, share = stats.triangles / source;
        $('visible').textContent = fmt(stats.clusters); $('culled').textContent = fmt(stats.culledFrustum + stats.culledBackface); $('triangles').textContent = fmt(stats.triangles); $('clusters').textContent = `${fmt(stats.clusters)} / ${fmt(built.clusterCount)}`; $('time').textContent = `${avg.toFixed(1)} ms`;
        $('hero-percent').textContent = `${(share * 100).toFixed(1)}%`; $('hero-fill').style.width = `${Math.min(100, share * 100)}%`; $('submit').textContent = `${stats.drawCalls} calls`;
        $('readout').textContent = `tris     ${fmt(stats.triangles)}\nclusters ${fmt(stats.clusters)}\ntests    ${fmt(stats.tests)}\nculled   ${fmt(stats.culledFrustum)} frustum · ${fmt(stats.culledBackface)} cone\nselect   ${stats.selectMs.toFixed(2)} ms cpu`;
        $('strip').textContent = `WEBGL2 · ONE ASSET · PIXEL ERROR ${state.threshold.toFixed(2)} · ${state.forcedLevel < 0 ? 'AUTOMATIC CUT' : `FORCED LOD ${state.forcedLevel}`}`;
        for (const row of root.querySelectorAll('[data-level]')) { const n = +row.dataset.level, drawn = n < 0 ? stats.triangles : stats.levelTriangles[n]; row.querySelector('.share').textContent = drawn ? fmt(drawn) : '—'; row.querySelector('.meter i').style.width = `${Math.min(100, 100 * drawn / Math.max(1, stats.triangles))}%`; }
      }
    }
    raf = requestAnimationFrame(tick);
  }
  function open(target) {
    if (!target?.out.geometry) return toast('Select an imported mesh first');
    asset = target; shown = true; session++; busy = pending.has(target); root.hidden = false; $('overlay').hidden = !busy;
    try { viewport ||= new ViewportRasterization(canvas); adopt(); setMode(state.viewMode); if (!raf) raf = requestAnimationFrame(tick); if (!built && !busy) rebuildHierarchy(); }
    catch (error) { close(); toast(`Cluster viewport: ${error.message || error}`); }
  }
  function close() { if (!shown) return; shown = false; session++; root.hidden = true; if (raf) cancelAnimationFrame(raf); raf = 0; viewport?.dispose(); built = null; onClose(); }
  function keydown(e) { if (!shown || e.target.matches('input,select,textarea')) return; if (e.key === 'Escape') { e.preventDefault(); close(); } else if (e.key.toLowerCase() === 'f') fit(); else if (e.key >= '1' && e.key <= '6') setMode(VIEW_MODES[+e.key - 1].id); }
  return { open, close, keydown, get active() { return shown; }, get asset() { return asset; } };
}
