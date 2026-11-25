/** Sparse, unbounded top-down mask tiles. One tile spans one map coordinate unit. */
export const MASK_RESOLUTION = 256;
const clamp = (x, lo = 0, hi = 1) => Math.max(lo, Math.min(hi, x));
const tileKey = (x, y) => `${x},${y}`;

export function createWorld() {
  return { version: 2, layers: [], regions: [], nextLayer: 1, nextRegion: 1 };
}

/** Preserve all existing masks, documents and bounds from the original 1x1 map. */
export function migrateWorld(world) {
  if (!world || !Array.isArray(world.layers) || !Array.isArray(world.regions)) throw new Error('Invalid saved world');
  if (world.version === 2) return world;
  if (world.version !== 1) throw new Error(`Unsupported world version ${world.version}`);
  const migrated = { ...world, version: 2, layers: world.layers.map(layer => {
    const { mask, ...rest } = layer;
    if (!(mask instanceof Uint8Array) || mask.length !== MASK_RESOLUTION ** 2) throw new Error('Invalid legacy shape mask');
    return { ...rest, tiles: mask.some(Boolean) ? { '0,0': mask } : {} };
  }), regions: world.regions.map(region => ({ ...region })) };
  // A non-stale legacy region can now track its own cropped mask rather than
  // being invalidated whenever somebody paints elsewhere on the same layer.
  for (const region of migrated.regions) {
    const layer = migrated.layers.find(item => item.id === region.layerId);
    if (region.document && layer && region.maskRevision === layer.revision) {
      region.maskFingerprint = maskFingerprint(regionMask(migrated, region));
    }
  }
  return migrated;
}

export function addShapeLayer(world, name = `Shape ${world.nextLayer}`) {
  const layer = { id: `shape-${world.nextLayer++}`, name, visible: true, revision: 0, tiles: {} };
  world.layers.push(layer);
  return layer;
}

/** Paint a continuous capsule across tile boundaries, without allocating empty tiles. */
export function paintMask(layer, x0, y0, x1, y1, radius, erase = false) {
  if (![x0, y0, x1, y1, radius].every(Number.isFinite)) throw new Error('Invalid brush coordinates');
  const n = MASK_RESOLUTION, r = clamp(radius, 1 / n, 1);
  const distance = Math.hypot(x1 - x0, y1 - y0);
  const steps = Math.max(1, Math.ceil(distance / (r * .45)));
  const touched = new Set();
  for (let step = 0; step <= steps; step++) {
    const t = step / steps, cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t;
    const minX = Math.floor((cx - r) * n), maxX = Math.ceil((cx + r) * n);
    const minY = Math.floor((cy - r) * n), maxY = Math.ceil((cy + r) * n);
    for (let py = minY; py <= maxY; py++) for (let px = minX; px <= maxX; px++) {
      const d = Math.hypot((px + .5) / n - cx, (py + .5) / n - cy);
      const value = Math.round(clamp((r - d) * n + .5) * 255);
      if (!value) continue;
      const tx = Math.floor(px / n), ty = Math.floor(py / n), key = tileKey(tx, ty);
      let tile = layer.tiles[key];
      if (!tile) { if (erase) continue; tile = layer.tiles[key] = new Uint8Array(n * n); }
      const i = (py - ty * n) * n + px - tx * n;
      const next = erase ? Math.min(tile[i], 255 - value) : Math.max(tile[i], value);
      if (next !== tile[i]) { tile[i] = next; touched.add(key); }
    }
  }
  if (touched.size) {
    layer.revision = (layer.revision || 0) + 1;
    if (erase) for (const key of touched) if (!layer.tiles[key].some(Boolean)) delete layer.tiles[key];
  }
}

export function normalizeBounds(x0, y0, x1, y1) {
  if (![x0, y0, x1, y1].every(Number.isFinite)) return null;
  const bounds = { x: Math.min(x0, x1), y: Math.min(y0, y1), width: Math.abs(x1 - x0), height: Math.abs(y1 - y0) };
  return bounds.width >= 2 / MASK_RESOLUTION && bounds.height >= 2 / MASK_RESOLUTION ? bounds : null;
}

export function addRegion(world, layerId, bounds, name = `Terrain ${world.nextRegion}`) {
  if (!world.layers.some(layer => layer.id === layerId) || !bounds || ![bounds.x, bounds.y, bounds.width, bounds.height].every(Number.isFinite) || bounds.width <= 0 || bounds.height <= 0) throw new Error('Invalid shape layer or bounds');
  const region = { id: `terrain-${world.nextRegion++}`, layerId, name, bounds: { ...bounds }, bake: null };
  world.regions.push(region);
  return region;
}

export function sampleGrid(data, n, u, v) {
  const x = clamp(u) * (n - 1), y = clamp(v) * (n - 1);
  const x0 = Math.floor(x), y0 = Math.floor(y), x1 = Math.min(n - 1, x0 + 1), y1 = Math.min(n - 1, y0 + 1);
  const tx = x - x0, ty = y - y0;
  return (data[y0 * n + x0] * (1 - tx) + data[y0 * n + x1] * tx) * (1 - ty) + (data[y1 * n + x0] * (1 - tx) + data[y1 * n + x1] * tx) * ty;
}

/** Bilinear sampling in world coordinates, including negative and adjoining tiles. */
export function sampleMask(layer, x, y) {
  const n = MASK_RESOLUTION, gx = x * n - .5, gy = y * n - .5;
  const x0 = Math.floor(gx), y0 = Math.floor(gy), fx = gx - x0, fy = gy - y0;
  const at = (px, py) => {
    const tx = Math.floor(px / n), ty = Math.floor(py / n);
    return layer.tiles[tileKey(tx, ty)]?.[(py - ty * n) * n + px - tx * n] || 0;
  };
  return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

export function maskFingerprint(mask) {
  let hash = 2166136261;
  for (const value of mask) { hash ^= value; hash = Math.imul(hash, 16777619); }
  return hash >>> 0;
}

/** Fit a rectangular world region into the square simulation without stretching.
 * Legacy documents without a mapping remain in their original stretched layout. */
export function terrainImportMapping(region) {
  if (region.importMapping === 'stretch' || (region.document && !region.importMapping)) {
    return { x: 0, y: 0, width: 1, height: 1, mode: 'stretch' };
  }
  const b = region.bounds, span = Math.max(b.width, b.height);
  const width = b.width / span, height = b.height / span;
  return { x: (1 - width) / 2, y: (1 - height) / 2, width, height, mode: 'padded' };
}

/** Crop a rectangular map region into its original aspect inside the square GPU grid. */
export function terrainImportMask(world, region, size = MASK_RESOLUTION) {
  const layer = world.layers.find(item => item.id === region.layerId);
  if (!layer) throw new Error('Region shape layer no longer exists');
  const output = new Uint8Array(size * size), b = region.bounds, m = terrainImportMapping(region);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const u = x / (size - 1), v = y / (size - 1);
    if (u < m.x || u > m.x + m.width || v < m.y || v > m.y + m.height) continue;
    output[y * size + x] = Math.round(sampleMask(layer, b.x + (u - m.x) / m.width * b.width, b.y + (v - m.y) / m.height * b.height));
  }
  return output;
}

export function regionMask(world, region, size = MASK_RESOLUTION) {
  const layer = world.layers.find(item => item.id === region.layerId);
  if (!layer) throw new Error('Region shape layer no longer exists');
  const output = new Uint8Array(size * size), b = region.bounds;
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    output[y * size + x] = Math.round(sampleMask(layer, b.x + (x / (size - 1)) * b.width, b.y + (y / (size - 1)) * b.height));
  }
  return output;
}

/** Sample a bounded part of the combined world. Terrain patches are composited in list order. */
export function bakeWorld(world, size = 128, bounds = { x: 0, y: 0, width: 1, height: 1 }) {
  const heights = new Float32Array(size * size), coverage = new Float32Array(size * size), edited = new Uint8Array(size * size);
  const layers = world.layers.filter(layer => layer.visible);
  const patches = world.regions.map(region => ({ region, layer: layers.find(layer => layer.id === region.layerId) })).filter(item => item.layer && item.region.bake);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const u = bounds.x + x / (size - 1) * bounds.width, v = bounds.y + y / (size - 1) * bounds.height, i = y * size + x;
    for (const layer of layers) coverage[i] = Math.max(coverage[i], sampleMask(layer, u, v) / 255);
    for (const { region, layer } of patches) {
      const b = region.bounds;
      if (u < b.x || v < b.y || u > b.x + b.width || v > b.y + b.height) continue;
      const weight = sampleMask(layer, u, v) / 255;
      if (weight > 0) edited[i] = 1;
      heights[i] = heights[i] * (1 - weight) + sampleGrid(region.bake.heights, region.bake.size, (u - b.x) / b.width, (v - b.y) / b.height) * weight;
    }
  }
  return { size, heights, coverage, edited };
}

/** Only painted tiles consume storage or produce terrain preview meshes. */
export function visibleTileKeys(world) {
  const keys = new Set();
  for (const layer of world.layers) if (layer.visible) for (const key of Object.keys(layer.tiles)) keys.add(key);
  return [...keys];
}
