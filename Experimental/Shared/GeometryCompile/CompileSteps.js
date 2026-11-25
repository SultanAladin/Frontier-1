// Geometry Compile pipeline: the steps every imported object goes through before it becomes a compiled asset that
// the Component Viewer can place. Steps depend on each other; changing an upstream setting marks downstream steps
// stale (amber) until they run again.
//
//   source ─► geometry ─┬─► uv
//                       ├─► sdf        (worker)
//                       ├─► lod        (worker)
//                       ├─► collision  (sdf type waits for the SDF step)
//                       └─► icon       (offscreen WebGL, injected by the UI)
//   meta (independent)

import { convertSource, computeBounds, analyseTopology, surfaceArea, cornerNormals, fitCollision, collisionVolume } from './Engine/MeshAnalysis.js';
import { transformGeometry, identityTransform } from './Engine/GeometryTransform.js';
import { unwrapCharts, packCharts, cornerUvs, layoutStatistics } from './Engine/UvCharts.js';

export const STEPS = [
  { id: 'source', label: 'Source & Units', note: 'Read, units, up axis, weld', icon: 'file', deps: [] },
  { id: 'geometry', label: 'Geometry', note: 'Transform, pivot, normals', icon: 'cube', deps: ['source'] },
  { id: 'uv', label: 'UV Charts', note: 'Planar unwrap + pack', icon: 'uv', deps: ['geometry'], optional: true },
  { id: 'sdf', label: 'SDF Bake', note: 'Signed distance volume', icon: 'sdf', deps: ['geometry'], optional: true },
  { id: 'lod', label: 'Cluster LOD', note: 'Nanite-style hierarchy', icon: 'layers', deps: ['geometry'], optional: true },
  { id: 'collision', label: 'Collision', note: 'Simple physics shape', icon: 'shield', deps: ['geometry'], optional: true },
  { id: 'icon', label: 'Icon Bake', note: 'Thumbnail for the content tray', icon: 'camera', deps: ['geometry'], optional: true },
  { id: 'meta', label: 'Metadata', note: 'Name, category, tags', icon: 'tag', deps: [] },
];

export const STEP = Object.fromEntries(STEPS.map((s) => [s.id, s]));

export function defaultSettings(name = 'Untitled', overrides = {}) {
  const base = {
    source: { unit: 'm', up: 'y', weld: 0 },
    geometry: { ...identityTransform(), centre: 'floor', normals: 'angle', angle: 40 },
    uv: { enabled: true, normalLimitDeg: 60, mergeBelow: 24, mergeLimitDeg: 75 },
    sdf: { enabled: true, resolution: 40, band: 2 },
    lod: { enabled: true, clusterTriangles: 128, groupClusters: 4, simplifyRatio: 0.5, preserveMaterials: true, seams: 'slide' },
    collision: { enabled: true, type: 'box', margin: 0 },
    icon: { enabled: true, size: 256, yaw: 35, pitch: 22, shading: 'lit', background: 'transparent' },
    meta: { name, category: 'Prop', tags: '', notes: '' },
  };
  for (const [k, v] of Object.entries(overrides)) base[k] = { ...base[k], ...v };
  return base;
}

let nextAssetId = 1;

// raw: the reader output ({ positions, indices, families, format, notes, slotNames? }).
export function createAsset({ name, fileName = '', fileSize = 0, raw, origin = 'import', settings = {}, palette = null }) {
  const asset = {
    id: `A${String(nextAssetId++).padStart(3, '0')}`,
    name, fileName, fileSize, origin, raw, palette,
    format: raw.format, notes: raw.notes || [], slotNames: raw.slotNames || null,
    settings: defaultSettings(name, settings),
    steps: Object.fromEntries(STEPS.map((s) => [s.id, { state: 'stale', ms: 0, summary: '', error: '' }])),
    out: {},
    compiledAt: 0,
    revision: 0,
  };
  for (const s of STEPS) if (s.optional && !asset.settings[s.id].enabled) asset.steps[s.id].state = 'off';
  return asset;
}

// Marks a step and everything downstream stale.
export function invalidate(asset, stepId) {
  const visit = (id) => {
    const st = asset.steps[id];
    const optional = STEP[id].optional && !asset.settings[id].enabled;
    st.state = optional ? 'off' : 'stale';
    for (const s of STEPS) if (s.deps.includes(id)) visit(s.id);
  };
  visit(stepId);
  if (stepId === 'sdf' && asset.settings.collision.type === 'sdf') visit('collision');
}

export function isCompiled(asset) {
  return STEPS.every((s) => ['done', 'off'].includes(asset.steps[s.id].state));
}

export function staleSteps(asset) {
  return STEPS.filter((s) => ['stale', 'error'].includes(asset.steps[s.id].state)).map((s) => s.id);
}

// ----------------------------------------------------------------------------------------------------------- runners
// ctx: { sdfWorker(request, onProgress) → Promise, lodWorker(request, onProgress) → Promise, renderIcon(asset) → Promise<dataUrl>, log(text) }

export async function runStep(asset, id, ctx, onProgress = () => {}) {
  const st = asset.steps[id];
  if (STEP[id].optional && !asset.settings[id].enabled) { st.state = 'off'; return; }
  for (const dep of STEP[id].deps) if (asset.steps[dep].state !== 'done') await runStep(asset, dep, ctx, onProgress);
  if (id === 'collision' && asset.settings.collision.type === 'sdf' && asset.steps.sdf.state !== 'done') {
    if (!asset.settings.sdf.enabled) throw new Error('SDF collision needs the SDF Bake step enabled');
    await runStep(asset, 'sdf', ctx, onProgress);
  }
  st.state = 'running'; st.error = '';
  const started = performance.now();
  try {
    await RUNNERS[id](asset, ctx, (f, label) => onProgress(id, f, label));
    st.ms = performance.now() - started;
    st.state = 'done';
    ctx.log?.(`${STEP[id].label}: ${st.summary} (${formatMs(st.ms)})`, 'ok');
  } catch (error) {
    st.state = 'error';
    st.error = String((error && error.message) || error);
    ctx.log?.(`${STEP[id].label} failed: ${st.error}`, 'error');
    throw error;
  }
  asset.revision++;
}

export async function compileAll(asset, ctx, onProgress = () => {}) {
  for (const s of STEPS) {
    const st = asset.steps[s.id];
    if (st.state === 'done' || st.state === 'off') continue;
    await runStep(asset, s.id, ctx, onProgress);
  }
  asset.compiledAt = Date.now();
}

const RUNNERS = {
  async source(asset) {
    const set = asset.settings.source;
    const src = convertSource(asset.raw, set);
    const bounds = computeBounds(src.positions);
    const topology = analyseTopology(src.indices, src.positions.length / 3);
    asset.out.source = { ...src, bounds, topology, area: surfaceArea(src.positions, src.indices) };
    const tris = src.indices.length / 3;
    asset.steps.source.summary = `${fmt(tris)} tris · ${topology.closed ? 'closed' : `${fmt(topology.boundary)} open edges`}`;
  },

  async geometry(asset) {
    const src = asset.out.source;
    const set = asset.settings.geometry;
    const moved = transformGeometry(src.positions, src.indices, set);
    const normals = cornerNormals(moved.positions, moved.indices, set.normals, set.angle);
    const bounds = computeBounds(moved.positions);
    asset.out.geometry = { positions: moved.positions, indices: moved.indices, normals, families: src.families, bounds, closed: src.topology.closed };
    const d = bounds.size.map((v) => v.toFixed(2)).join(' × ');
    asset.steps.geometry.summary = `${d} m · ${set.normals} normals`;
  },

  async uv(asset) {
    const g = asset.out.geometry;
    const set = asset.settings.uv;
    const layout = packCharts(unwrapCharts(g.positions, g.indices, set));
    const uvs = cornerUvs(layout);
    const stats = layoutStatistics(layout, uvs);
    const seams = layout.seams.length;
    asset.out.uv = { layout, uvs, stats, seams };
    asset.steps.uv.summary = `${fmt(layout.charts.length)} charts · ${(stats.uvArea * 100).toFixed(0)}% filled`;
  },

  async sdf(asset, ctx, progress) {
    const g = asset.out.geometry;
    const set = asset.settings.sdf;
    const result = await ctx.sdfWorker({ positions: new Float32Array(g.positions), indices: new Uint32Array(g.indices), resolution: set.resolution, band: set.band }, progress);
    asset.out.sdf = result;
    const kb = (result.values.byteLength / 1024).toFixed(0);
    asset.steps.sdf.summary = `${result.dims}³ · ${kb} KB${g.closed ? '' : ' · open, sign guessed'}`;
  },

  async lod(asset, ctx, progress) {
    const g = asset.out.geometry;
    const set = asset.settings.lod;
    const build = { clusterTriangles: set.clusterTriangles, groupClusters: set.groupClusters, simplifyRatio: set.simplifyRatio, preserveMaterials: set.preserveMaterials !== false, pinSeams: set.seams === 'pin' };
    const families = g.families ? new Uint32Array(g.families) : null;
    const h = await ctx.lodWorker({ positions: new Float32Array(g.positions), indices: new Uint32Array(g.indices), families, build }, progress);
    asset.out.lod = h;
    const mats = h.materialFamilies > 1 ? ` · ${h.materialFamilies} materials${h.preserveMaterials ? (h.config.pinSeams ? ' pinned' : ' held') : ''}` : '';
    asset.steps.lod.summary = `${h.levels.length} levels · ${fmt(h.clusterCount)} clusters${mats}`;
  },

  async collision(asset) {
    const g = asset.out.geometry;
    const set = asset.settings.collision;
    const shape = fitCollision(g.positions, set.type, set.margin);
    if (set.type === 'sdf') shape.resolution = asset.out.sdf?.dims || 0;
    asset.out.collision = shape;
    const vol = collisionVolume(shape);
    asset.steps.collision.summary = set.type === 'none' ? 'no collision' : `${set.type}${vol ? ` · ${vol.toFixed(2)} m³` : ''}`;
  },

  async icon(asset, ctx) {
    const url = await ctx.renderIcon(asset);
    asset.out.icon = url;
    asset.steps.icon.summary = `${asset.settings.icon.size} px · ${asset.settings.icon.background}`;
  },

  async meta(asset) {
    const m = asset.settings.meta;
    if (!m.name.trim()) throw new Error('Name is required');
    asset.name = m.name.trim();
    const tags = m.tags.split(',').map((t) => t.trim()).filter(Boolean);
    asset.steps.meta.summary = `${m.category}${tags.length ? ` · ${tags.length} tags` : ''}`;
  },
};

export function fmt(n) {
  if (n >= 1e6) return (n / 1e6).toFixed(2) + 'M';
  if (n >= 1e4) return (n / 1e3).toFixed(1) + 'k';
  return Math.round(n).toLocaleString('en-US');
}

export function formatMs(ms) {
  return ms >= 1000 ? (ms / 1000).toFixed(2) + ' s' : ms.toFixed(0) + ' ms';
}

// Worker wrapper with tickets so several bakes can be in flight.
export function workerChannel(url) {
  let worker = null, ticket = 0;
  const pending = new Map();
  const ensure = () => {
    if (worker) return worker;
    worker = new Worker(url, { type: 'module' });
    worker.onmessage = (e) => {
      const p = pending.get(e.data.ticket);
      if (!p) return;
      if (e.data.type === 'progress') p.progress(e.data.fraction, e.data.label);
      else if (e.data.type === 'done') { pending.delete(e.data.ticket); p.resolve(e.data.result); }
      else { pending.delete(e.data.ticket); p.reject(new Error(e.data.message)); }
    };
    worker.onerror = (e) => { for (const p of pending.values()) p.reject(new Error(e.message || 'Worker failed')); pending.clear(); worker = null; };
    return worker;
  };
  return (request, progress = () => {}) => new Promise((resolve, reject) => {
    const id = ++ticket;
    pending.set(id, { resolve, reject, progress });
    const transfer = Object.values(request).filter((v) => ArrayBuffer.isView(v)).map((v) => v.buffer);
    ensure().postMessage({ ticket: id, request }, transfer);
  });
}
