// Frontier MakeHuman CC0 Parametric Atelier Mannequin Engine
// Loads packed FMN1 mesh + 28 sparse morph targets, maintains watertight flat-face cap,
// builds uniform 3D spatial triangle hash grid for CPU & WebGPU cloth collision,
// and computes real-time tailor measurements & torso silhouette queries.

export const MANNEQUIN_ASSET_URL = './Assets/FrontierMannequin.bin';

export const BODY_SLIDER_SPECS = [
  { key: 'gender',    label: 'Form Block (F ↔ M)', min: 0,    max: 1,   step: 0.01, def: 0,    group: 'Silhouette' },
  { key: 'height',    label: 'Stature / Height',   min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Silhouette' },
  { key: 'weight',    label: 'Body Mass',          min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Silhouette' },
  { key: 'muscle',    label: 'Muscle Tone',        min: 0,    max: 1,   step: 0.01, def: 0.15, group: 'Silhouette' },
  { key: 'shoulders', label: 'Shoulder Width',     min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Upper Body' },
  { key: 'chest',     label: 'Across Chest',       min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Upper Body' },
  { key: 'bust',      label: 'Bust Circumference', min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Upper Body' },
  { key: 'cup',       label: 'Bust Projection',    min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Upper Body' },
  { key: 'underbust', label: 'Underbust',          min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Upper Body' },
  { key: 'torso',     label: 'Torso Length',       min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Core & Lower' },
  { key: 'waist',     label: 'Waist',              min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Core & Lower' },
  { key: 'hips',      label: 'Hips',               min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Core & Lower' },
  { key: 'neck',      label: 'Neck',               min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Details' },
  { key: 'upperarm',  label: 'Upper Arm',          min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Details' },
  { key: 'thigh',     label: 'Thigh',              min: -1,   max: 1,   step: 0.01, def: 0,    group: 'Details' },
];

export const DEFAULT_BODY_PARAMS = Object.fromEntries(
  BODY_SLIDER_SPECS.map((s) => [s.key, s.def])
);

export const BODY_PRESETS = {
  atelier36: {
    name: 'Atelier FR 36',
    subtitle: 'Classic French dress form (167.5 cm)',
    params: { ...DEFAULT_BODY_PARAMS },
  },
  runway: {
    name: 'Runway Tall',
    subtitle: 'Slender couture runway block (~182 cm)',
    params: { ...DEFAULT_BODY_PARAMS, height: 0.68, weight: -0.35, waist: -0.32, shoulders: 0.28, hips: -0.15, muscle: 0.2 },
  },
  hourglass: {
    name: 'Hourglass Curve',
    subtitle: 'Full bust & hip with sculpted waist',
    params: { ...DEFAULT_BODY_PARAMS, bust: 0.65, cup: 0.45, underbust: -0.15, waist: -0.58, hips: 0.65, thigh: 0.25 },
  },
  athletic: {
    name: 'Athletic Fit',
    subtitle: 'Broad shoulders & toned musculature',
    params: { ...DEFAULT_BODY_PARAMS, height: 0.2, muscle: 0.78, shoulders: 0.48, chest: 0.3, waist: -0.12, thigh: 0.38 },
  },
  tailorMale: {
    name: 'Menswear EU 50',
    subtitle: 'Tailored menswear jacket & trouser form',
    params: { ...DEFAULT_BODY_PARAMS, gender: 1.0, height: 0.35, muscle: 0.38, shoulders: 0.32, chest: 0.22, bust: -0.1, cup: -0.25 },
  },
};

const SIGNED_MORPH_PAIRS = [
  ['height',    'height_incr',    'height_decr',    0.32],
  ['weight',    'weight_incr',    'weight_decr',    0.85],
  ['bust',      'bust_incr',      'bust_decr',      1.0],
  ['underbust', 'underbust_incr', 'underbust_decr', 1.0],
  ['waist',     'waist_incr',     'waist_decr',     1.0],
  ['hips',      'hips_incr',      'hips_decr',      1.0],
  ['shoulders', 'shoulders_incr', 'shoulders_decr', 1.0],
  ['chest',     'chest_incr',     'chest_decr',     1.0],
  ['torso',     'torso_incr',     'torso_decr',     1.0],
  ['neck',      'neck_incr',      'neck_decr',      1.0],
  ['upperarm',  'upperarm_incr',  'upperarm_decr',  1.0],
  ['thigh',     'thigh_incr',     'thigh_decr',     1.0],
  ['cup',       'cup_incr',       'cup_decr',       1.0],
];

export class MannequinEngine {
  static async load(url = MANNEQUIN_ASSET_URL) {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`Failed to fetch mannequin asset ${url}: HTTP ${res.status}`);
    const buf = await res.arrayBuffer();
    return MannequinEngine.fromArrayBuffer(buf);
  }

  static fromArrayBuffer(arrayBuffer) {
    const dv = new DataView(arrayBuffer);
    let off = 0;
    const magic = String.fromCharCode(dv.getUint8(0), dv.getUint8(1), dv.getUint8(2), dv.getUint8(3));
    if (magic !== 'FMN1') throw new Error(`Invalid mannequin binary magic: ${magic}`);
    off += 4;

    const vCount = dv.getUint32(off, true); off += 4;
    const tCount = dv.getUint32(off, true); off += 4;
    const loopLen = dv.getUint32(off, true); off += 4;
    const capStart = dv.getUint32(off, true); off += 4;
    const capCount = dv.getUint32(off, true); off += 4;
    const morphCount = dv.getUint32(off, true); off += 4;

    const basePositions = new Float32Array(vCount * 3);
    for (let i = 0; i < vCount * 3; i++) {
      basePositions[i] = dv.getFloat32(off, true); off += 4;
    }

    const triIndices = new Uint32Array(tCount * 3);
    for (let i = 0; i < tCount * 3; i++) {
      triIndices[i] = dv.getUint16(off, true); off += 2;
    }

    const loopIds = new Uint16Array(loopLen);
    for (let i = 0; i < loopLen; i++) {
      loopIds[i] = dv.getUint16(off, true); off += 2;
    }

    const capLoopIdx = new Int32Array(capCount);
    const capRadialW = new Float32Array(capCount);
    const capZBlend = new Float32Array(capCount);
    for (let i = 0; i < capCount; i++) {
      capLoopIdx[i] = dv.getInt32(off, true); off += 4;
      capRadialW[i] = dv.getFloat32(off, true); off += 4;
      capZBlend[i] = dv.getFloat32(off, true); off += 4;
    }

    const morphs = new Map();
    const decoder = new TextDecoder();
    for (let m = 0; m < morphCount; m++) {
      const nameLen = dv.getUint16(off, true); off += 2;
      const name = decoder.decode(new Uint8Array(arrayBuffer, off, nameLen)); off += nameLen;
      const entryCount = dv.getUint32(off, true); off += 4;
      const scale = dv.getFloat32(off, true); off += 4;
      const indices = new Uint16Array(entryCount);
      const deltas = new Int16Array(entryCount * 3);
      for (let k = 0; k < entryCount; k++) {
        indices[k] = dv.getUint16(off, true); off += 2;
        deltas[k * 3] = dv.getInt16(off, true); off += 2;
        deltas[k * 3 + 1] = dv.getInt16(off, true); off += 2;
        deltas[k * 3 + 2] = dv.getInt16(off, true); off += 2;
      }
      morphs.set(name, { name, count: entryCount, scale, indices, deltas });
    }

    return new MannequinEngine({
      vCount, tCount, loopLen, capStart, capCount,
      basePositions, triIndices, loopIds, capLoopIdx, capRadialW, capZBlend, morphs,
    });
  }

  constructor(data) {
    this.vCount = data.vCount;
    this.tCount = data.tCount;
    this.loopLen = data.loopLen;
    this.capStart = data.capStart;
    this.capCount = data.capCount;
    this.basePositions = data.basePositions;
    this.triIndices = data.triIndices;
    this.loopIds = data.loopIds;
    this.capLoopIdx = data.capLoopIdx;
    this.capRadialW = data.capRadialW;
    this.capZBlend = data.capZBlend;
    this.morphs = data.morphs;

    this.positions = new Float32Array(this.vCount * 3);
    this.prevPositions = new Float32Array(this.vCount * 3);
    this.normals = new Float32Array(this.vCount * 3);
    this.triNormals = new Float32Array(this.tCount * 3);
    this.vertexVelocities = new Float32Array(this.vCount * 3);

    this.params = { ...DEFAULT_BODY_PARAMS };
    this.geometryVersion = 0;
    this.motionVersion = 0;
    this._hasMotionDelta = false;
    this.heightMeters = 1.6752;
    this.bounds = { minX: -0.5, maxX: 0.5, minY: 0, maxY: 1.7, minZ: -0.2, maxZ: 0.25 };

    // Uniform 3D Spatial Triangle Grid (CSR) for O(1) collision queries on CPU & GPU
    this.cellSize = 0.055;
    this.gridDim = [1, 1, 1];
    this.gridOrigin = [0, 0, 0];
    this.gridCellOffsets = new Int32Array(2);
    this.gridTriIndices = new Int32Array(0);

    // Pre-allocated scratch for spatial grid building & deduplication
    this._cellCounts = new Int32Array(1);
    this._cellCursor = new Int32Array(1);
    this._triStamp = new Int32Array(this.tCount);
    this._querySeq = 0;

    // Live anthropometric measurements (cm) + slice polylines for tailor tape overlay
    this.measurements = {
      height: 167.5,
      neck: 38.9,
      shoulderWidth: 41.8,
      bust: 83.0,
      underbust: 71.4,
      waist: 68.7,
      hips: 96.2,
      chest: 80.0,
    };
    this.measureRings = [];

    this.applyParams(this.params, { resetVelocity: true });
  }

  _applySparseMorph(name, weight) {
    if (Math.abs(weight) < 1e-4) return;
    const m = this.morphs.get(name);
    if (!m || m.count === 0) return;
    const ws = weight * m.scale;
    const pos = this.positions;
    const idx = m.indices;
    const del = m.deltas;
    for (let k = 0; k < m.count; k++) {
      const i3 = idx[k] * 3;
      const k3 = k * 3;
      pos[i3]     += del[k3]     * ws;
      pos[i3 + 1] += del[k3 + 1] * ws;
      pos[i3 + 2] += del[k3 + 2] * ws;
    }
  }

  applyParams(nextParams, opts = {}) {
    const resetVelocity = !!opts.resetVelocity;
    const p = { ...this.params, ...nextParams };
    this.params = p;

    if (!resetVelocity) {
      this.prevPositions.set(this.positions);
    }

    const pos = this.positions;
    pos.set(this.basePositions);

    // 1. Primary gender, muscle, and signed measurement morphs
    this._applySparseMorph('gender_male', p.gender);
    this._applySparseMorph('muscle_incr', p.muscle);
    for (const [key, incrName, decrName, mult] of SIGNED_MORPH_PAIRS) {
      const v = (p[key] || 0) * mult;
      if (v > 0) this._applySparseMorph(incrName, v);
      else if (v < 0) this._applySparseMorph(decrName, -v);
    }

    // 2. Recompute seamless watertight flat-face cap from current deformed boundary loop
    const L = this.loopLen;
    const loop = this.loopIds;
    let cx = 0, cy = 0, czMax = -1e9;
    for (let k = 0; k < L; k++) {
      const i3 = loop[k] * 3;
      cx += pos[i3];
      cy += pos[i3 + 1];
      if (pos[i3 + 2] > czMax) czMax = pos[i3 + 2];
    }
    cx /= L;
    cy /= L;
    const czFront = czMax + 0.002;

    for (let c = 0; c < this.capCount; c++) {
      const vid3 = (this.capStart + c) * 3;
      const k = this.capLoopIdx[c];
      if (k < 0) {
        pos[vid3] = cx;
        pos[vid3 + 1] = cy;
        pos[vid3 + 2] = czFront;
        continue;
      }
      const w = this.capRadialW[c];
      const zb = this.capZBlend[c];
      const kp3 = loop[(k - 1 + L) % L] * 3;
      const kc3 = loop[k] * 3;
      const kn3 = loop[(k + 1) % L] * 3;
      const lx = 0.25 * pos[kp3]     + 0.50 * pos[kc3]     + 0.25 * pos[kn3];
      const ly = 0.25 * pos[kp3 + 1] + 0.50 * pos[kc3 + 1] + 0.25 * pos[kn3 + 1];
      const lz = 0.25 * pos[kp3 + 2] + 0.50 * pos[kc3 + 2] + 0.25 * pos[kn3 + 2];
      pos[vid3]     = w * lx + (1 - w) * cx;
      pos[vid3 + 1] = w * ly + (1 - w) * cy;
      pos[vid3 + 2] = (w * lz + (1 - w) * czFront) * (1 - zb) + czFront * zb;
    }

    // 3. Anchor soles cleanly at y = 0.0 and measure bounding box
    let minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9, minZ = 1e9, maxZ = -1e9;
    for (let i = 0; i < this.vCount; i++) {
      const y = pos[i * 3 + 1];
      if (y < minY) minY = y;
    }
    for (let i = 0; i < this.vCount; i++) {
      const i3 = i * 3;
      const x = pos[i3];
      const y = pos[i3 + 1] - minY;
      const z = pos[i3 + 2];
      pos[i3 + 1] = y;
      if (x < minX) minX = x; if (x > maxX) maxX = x;
      if (y > maxY) maxY = y;
      if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }
    this.heightMeters = maxY;
    this.bounds = { minX, maxX, minY: 0, maxY, minZ, maxZ };

    if (resetVelocity) {
      this.prevPositions.set(pos);
      this.vertexVelocities.fill(0);
      this._hasMotionDelta = false;
    } else {
      for (let i = 0; i < this.vCount * 3; i++) {
        this.vertexVelocities[i] = pos[i] - this.prevPositions[i];
      }
      this._hasMotionDelta = true;
    }

    // 4. Compute triangle normals & area-weighted smooth vertex normals
    this._computeNormals();

    // 5. Rebuild uniform 3D spatial triangle hash grid
    this._rebuildSpatialGrid();

    // 6. Update anthropometric tailor measurements & tape rings
    this._computeMeasurements();

    this.geometryVersion++;
    this.motionVersion++;
  }

  clearMotionDelta() {
    if (!this._hasMotionDelta) return;
    this.prevPositions.set(this.positions);
    this.vertexVelocities.fill(0);
    this._hasMotionDelta = false;
    this.motionVersion++;
  }

  _computeNormals() {
    const pos = this.positions;
    const tris = this.triIndices;
    const vn = this.normals;
    const tn = this.triNormals;
    vn.fill(0);

    for (let t = 0; t < this.tCount; t++) {
      const t3 = t * 3;
      const a = tris[t3] * 3;
      const b = tris[t3 + 1] * 3;
      const c = tris[t3 + 2] * 3;

      const abx = pos[b] - pos[a], aby = pos[b + 1] - pos[a + 1], abz = pos[b + 2] - pos[a + 2];
      const acx = pos[c] - pos[a], acy = pos[c + 1] - pos[a + 1], acz = pos[c + 2] - pos[a + 2];

      const nx = aby * acz - abz * acy;
      const ny = abz * acx - abx * acz;
      const nz = abx * acy - aby * acx;
      const len = Math.hypot(nx, ny, nz);
      if (len > 1e-10) {
        const inv = 1 / len;
        tn[t3] = nx * inv; tn[t3 + 1] = ny * inv; tn[t3 + 2] = nz * inv;
      } else {
        tn[t3] = 0; tn[t3 + 1] = 1; tn[t3 + 2] = 0;
      }
      // Area-weighted accumulation into vertex normals
      vn[a] += nx; vn[a + 1] += ny; vn[a + 2] += nz;
      vn[b] += nx; vn[b + 1] += ny; vn[b + 2] += nz;
      vn[c] += nx; vn[c + 1] += ny; vn[c + 2] += nz;
    }

    for (let i = 0; i < this.vCount; i++) {
      const i3 = i * 3;
      const len = Math.hypot(vn[i3], vn[i3 + 1], vn[i3 + 2]);
      if (len > 1e-10) {
        const inv = 1 / len;
        vn[i3] *= inv; vn[i3 + 1] *= inv; vn[i3 + 2] *= inv;
      } else {
        vn[i3] = 0; vn[i3 + 1] = 1; vn[i3 + 2] = 0;
      }
    }
  }

  _rebuildSpatialGrid() {
    const pad = 0.06;
    const cs = this.cellSize;
    const invCs = 1 / cs;
    const ox = this.bounds.minX - pad;
    const oy = this.bounds.minY - pad;
    const oz = this.bounds.minZ - pad;
    const dx = Math.max(4, Math.ceil((this.bounds.maxX + pad - ox) * invCs));
    const dy = Math.max(4, Math.ceil((this.bounds.maxY + pad - oy) * invCs));
    const dz = Math.max(4, Math.ceil((this.bounds.maxZ + pad - oz) * invCs));
    this.gridOrigin = [ox, oy, oz];
    this.gridDim = [dx, dy, dz];
    const numCells = dx * dy * dz;

    if (this._cellCounts.length < numCells) {
      this._cellCounts = new Int32Array(numCells);
      this._cellCursor = new Int32Array(numCells);
    } else {
      this._cellCounts.fill(0, 0, numCells);
    }
    if (this.gridCellOffsets.length !== numCells + 1) {
      this.gridCellOffsets = new Int32Array(numCells + 1);
    }

    const pos = this.positions;
    const tris = this.triIndices;
    let totalRefs = 0;

    // Pass 1: count triangle references per cell
    for (let t = 0; t < this.tCount; t++) {
      const t3 = t * 3;
      const a = tris[t3] * 3, b = tris[t3 + 1] * 3, c = tris[t3 + 2] * 3;
      const minX = Math.min(pos[a], pos[b], pos[c]);
      const maxX = Math.max(pos[a], pos[b], pos[c]);
      const minY = Math.min(pos[a + 1], pos[b + 1], pos[c + 1]);
      const maxY = Math.max(pos[a + 1], pos[b + 1], pos[c + 1]);
      const minZ = Math.min(pos[a + 2], pos[b + 2], pos[c + 2]);
      const maxZ = Math.max(pos[a + 2], pos[b + 2], pos[c + 2]);

      const cx0 = Math.max(0, Math.min(dx - 1, Math.floor((minX - ox) * invCs)));
      const cx1 = Math.max(0, Math.min(dx - 1, Math.floor((maxX - ox) * invCs)));
      const cy0 = Math.max(0, Math.min(dy - 1, Math.floor((minY - oy) * invCs)));
      const cy1 = Math.max(0, Math.min(dy - 1, Math.floor((maxY - oy) * invCs)));
      const cz0 = Math.max(0, Math.min(dz - 1, Math.floor((minZ - oz) * invCs)));
      const cz1 = Math.max(0, Math.min(dz - 1, Math.floor((maxZ - oz) * invCs)));

      for (let gz = cz0; gz <= cz1; gz++) {
        const zOff = gz * dy * dx;
        for (let gy = cy0; gy <= cy1; gy++) {
          const yOff = zOff + gy * dx;
          for (let gx = cx0; gx <= cx1; gx++) {
            this._cellCounts[yOff + gx]++;
            totalRefs++;
          }
        }
      }
    }

    // Prefix sum
    let acc = 0;
    for (let c = 0; c < numCells; c++) {
      this.gridCellOffsets[c] = acc;
      this._cellCursor[c] = acc;
      acc += this._cellCounts[c];
    }
    this.gridCellOffsets[numCells] = acc;

    if (this.gridTriIndices.length < totalRefs) {
      this.gridTriIndices = new Int32Array(Math.ceil(totalRefs * 1.15));
    }

    // Pass 2: scatter triangle indices into CSR buckets
    for (let t = 0; t < this.tCount; t++) {
      const t3 = t * 3;
      const a = tris[t3] * 3, b = tris[t3 + 1] * 3, c = tris[t3 + 2] * 3;
      const minX = Math.min(pos[a], pos[b], pos[c]);
      const maxX = Math.max(pos[a], pos[b], pos[c]);
      const minY = Math.min(pos[a + 1], pos[b + 1], pos[c + 1]);
      const maxY = Math.max(pos[a + 1], pos[b + 1], pos[c + 1]);
      const minZ = Math.min(pos[a + 2], pos[b + 2], pos[c + 2]);
      const maxZ = Math.max(pos[a + 2], pos[b + 2], pos[c + 2]);

      const cx0 = Math.max(0, Math.min(dx - 1, Math.floor((minX - ox) * invCs)));
      const cx1 = Math.max(0, Math.min(dx - 1, Math.floor((maxX - ox) * invCs)));
      const cy0 = Math.max(0, Math.min(dy - 1, Math.floor((minY - oy) * invCs)));
      const cy1 = Math.max(0, Math.min(dy - 1, Math.floor((maxY - oy) * invCs)));
      const cz0 = Math.max(0, Math.min(dz - 1, Math.floor((minZ - oz) * invCs)));
      const cz1 = Math.max(0, Math.min(dz - 1, Math.floor((maxZ - oz) * invCs)));

      for (let gz = cz0; gz <= cz1; gz++) {
        const zOff = gz * dy * dx;
        for (let gy = cy0; gy <= cy1; gy++) {
          const yOff = zOff + gy * dx;
          for (let gx = cx0; gx <= cx1; gx++) {
            const cell = yOff + gx;
            this.gridTriIndices[this._cellCursor[cell]++] = t;
          }
        }
      }
    }
    this.gridTriActiveLength = totalRefs;
  }

  _computeMeasurements() {
    const H = this.heightMeters;
    const armScale = 1 + (this.params.weight || 0) * 0.14 + (this.params.shoulders || 0) * 0.08;
    const slices = [
      { key: 'neck',      label: 'Neck',      y: H * 0.840, maxX: 0.110 * armScale, segs: [], circ: 0, minX: 1e9, maxXHit: -1e9 },
      { key: 'shoulders', label: 'Shoulders', y: H * 0.815, maxX: 0.250 * armScale, segs: [], circ: 0, minX: 1e9, maxXHit: -1e9 },
      { key: 'bust',      label: 'Bust',      y: H * 0.725, maxX: 0.185 * armScale, segs: [], circ: 0, minX: 1e9, maxXHit: -1e9 },
      { key: 'underbust', label: 'Underbust', y: H * 0.688, maxX: 0.175 * armScale, segs: [], circ: 0, minX: 1e9, maxXHit: -1e9 },
      { key: 'waist',     label: 'Waist',     y: H * 0.620, maxX: 0.175 * armScale, segs: [], circ: 0, minX: 1e9, maxXHit: -1e9 },
      { key: 'hips',      label: 'Hips',      y: H * 0.515, maxX: 0.225 * armScale, segs: [], circ: 0, minX: 1e9, maxXHit: -1e9 },
    ];

    const pos = this.positions;
    const tris = this.triIndices;
    for (let t = 0; t < this.tCount; t++) {
      const t3 = t * 3;
      const a3 = tris[t3] * 3, b3 = tris[t3 + 1] * 3, c3 = tris[t3 + 2] * 3;
      const ay = pos[a3 + 1], by = pos[b3 + 1], cy = pos[c3 + 1];
      const yMin = Math.min(ay, by, cy), yMax = Math.max(ay, by, cy);
      for (let sIdx = 0; sIdx < slices.length; sIdx++) {
        const sl = slices[sIdx];
        const ym = sl.y;
        if (ym < yMin || ym >= yMax) continue;
        const pairs = [[a3, b3, ay, by], [b3, c3, by, cy], [c3, a3, cy, ay]];
        let p0x = 0, p0z = 0, p1x = 0, p1z = 0, cnt = 0;
        for (let k = 0; k < 3; k++) {
          const [u3, w3, yu, yw] = pairs[k];
          if ((yu <= ym && ym < yw) || (yw <= ym && ym < yu)) {
            const alpha = (ym - yu) / (yw - yu);
            const px = pos[u3] + alpha * (pos[w3] - pos[u3]);
            if (Math.abs(px) <= sl.maxX) {
              const pz = pos[u3 + 2] + alpha * (pos[w3 + 2] - pos[u3 + 2]);
              if (cnt === 0) { p0x = px; p0z = pz; cnt = 1; }
              else { p1x = px; p1z = pz; cnt = 2; }
            }
          }
        }
        if (cnt === 2) {
          sl.circ += Math.hypot(p1x - p0x, p1z - p0z);
          if (p0x < sl.minX) sl.minX = p0x; if (p0x > sl.maxXHit) sl.maxXHit = p0x;
          if (p1x < sl.minX) sl.minX = p1x; if (p1x > sl.maxXHit) sl.maxXHit = p1x;
          if (sIdx >= 2) sl.segs.push([p0x, ym, p0z, p1x, ym, p1z]);
        }
      }
    }

    this.measurements = {
      height: H * 100.0,
      neck: slices[0].circ * 100.0,
      shoulderWidth: slices[1].segs !== null && slices[1].maxXHit > slices[1].minX ? (slices[1].maxXHit - slices[1].minX) * 100.0 : 0,
      bust: slices[2].circ * 100.0,
      underbust: slices[3].circ * 100.0,
      waist: slices[4].circ * 100.0,
      hips: slices[5].circ * 100.0,
    };
    this.measureRings = slices.slice(2).map((sl) => ({
      key: sl.key,
      label: sl.label,
      yMeters: sl.y,
      circCm: sl.circ * 100.0,
      segs: sl.segs,
    }));
  }

  // Samples radial torso contour (centerX/centerZ and outer radius) at height yMeters & azimuth theta
  sampleTorsoPoint(yMeters, theta, easeMeters = 0.022) {
    const pos = this.positions;
    const hRatio = yMeters / Math.max(0.5, this.heightMeters);
    const armScale = 1 + Math.max(0, this.params.weight || 0) * 0.18 + Math.max(0, this.params.shoulders || 0) * 0.08;
    // Narrower torso corridor near armpit/bust (0.68..0.80H) so arms never taint bodice contours
    const baseMaxX = (hRatio > 0.67 && hRatio < 0.80) ? 0.158 : 0.215;
    const maxArmGapX = baseMaxX * armScale;
    let czAcc = 0, czCnt = 0;
    for (let i = 0; i < this.vCount; i++) {
      const i3 = i * 3;
      const y = pos[i3 + 1];
      if (Math.abs(y - yMeters) < 0.045 && Math.abs(pos[i3]) < maxArmGapX) {
        czAcc += pos[i3 + 2];
        czCnt++;
      }
    }
    const centerZ = czCnt > 0 ? czAcc / czCnt : 0.03;
    const dirX = Math.cos(theta);
    const dirZ = Math.sin(theta);

    let maxProj = 0.08;
    for (let i = 0; i < this.vCount; i++) {
      const i3 = i * 3;
      const y = pos[i3 + 1];
      if (Math.abs(y - yMeters) > 0.05) continue;
      const x = pos[i3];
      if (Math.abs(x) >= maxArmGapX) continue;
      const dz = pos[i3 + 2] - centerZ;
      const len = Math.hypot(x, dz);
      if (len < 1e-4) continue;
      const cosA = (x * dirX + dz * dirZ) / len;
      if (cosA > 0.86) {
        const proj = x * dirX + dz * dirZ;
        if (proj > maxProj) maxProj = proj;
      }
    }
    const r = maxProj + easeMeters;
    return [dirX * r, yMeters, centerZ + dirZ * r];
  }

  // Finds nearest mannequin vertex to target world position (used for dynamic landmark-tracked garment pins)
  findNearestVertex(tx, ty, tz, maxAbsX = 10.0) {
    const pos = this.positions;
    let bestId = 0;
    let bestD2 = 1e9;
    for (let i = 0; i < this.vCount; i++) {
      const i3 = i * 3;
      if (Math.abs(pos[i3]) > maxAbsX) continue;
      const dx = pos[i3] - tx;
      const dy = pos[i3 + 1] - ty;
      const dz = pos[i3 + 2] - tz;
      const d2 = dx * dx + dy * dy + dz * dz;
      if (d2 < bestD2) { bestD2 = d2; bestId = i; }
    }
    return bestId;
  }

  // Exact closest point & smooth oriented pseudo-normal on mannequin surface around (px, py, pz)
  // Returns true if within capture range (`phi < thickness + margin`), writing to `out`
  queryClosestSurface(px, py, pz, thickness, maxCaptureDist, out) {
    const [dx, dy, dz] = this.gridDim;
    const [ox, oy, oz] = this.gridOrigin;
    const invCs = 1 / this.cellSize;

    const fx = (px - ox) * invCs;
    const fy = (py - oy) * invCs;
    const fz = (pz - oz) * invCs;
    if (fx < -1 || fx > dx || fy < -1 || fy > dy || fz < -1 || fz > dz) return false;

    let x0, x1, y0, y1, z0, z1;
    if (maxCaptureDist <= 0.5 * this.cellSize) {
      const bx = Math.floor(fx - 0.5);
      const by = Math.floor(fy - 0.5);
      const bz = Math.floor(fz - 0.5);
      x0 = Math.max(0, bx); x1 = Math.min(dx - 1, bx + 1);
      y0 = Math.max(0, by); y1 = Math.min(dy - 1, by + 1);
      z0 = Math.max(0, bz); z1 = Math.min(dz - 1, bz + 1);
    } else {
      const gx = Math.floor(fx), gy = Math.floor(fy), gz = Math.floor(fz);
      x0 = Math.max(0, gx - 1); x1 = Math.min(dx - 1, gx + 1);
      y0 = Math.max(0, gy - 1); y1 = Math.min(dy - 1, gy + 1);
      z0 = Math.max(0, gz - 1); z1 = Math.min(dz - 1, gz + 1);
    }

    const seq = ++this._querySeq;
    const stamp = this._triStamp;
    const pos = this.positions;
    const prev = this.prevPositions;
    const vn = this.normals;
    const tn = this.triNormals;
    const tris = this.triIndices;
    const offsets = this.gridCellOffsets;
    const triList = this.gridTriIndices;

    let bestD2 = maxCaptureDist * maxCaptureDist;
    let bestSigned = 1e9;
    let bestQx = 0, bestQy = 0, bestQz = 0;
    let bestNx = 0, bestNy = 1, bestNz = 0;
    let bestVx = 0, bestVy = 0, bestVz = 0;
    let hit = false;

    for (let cz = z0; cz <= z1; cz++) {
      const zBase = cz * dy * dx;
      for (let cy = y0; cy <= y1; cy++) {
        const yBase = zBase + cy * dx;
        for (let cx = x0; cx <= x1; cx++) {
          const cell = yBase + cx;
          const start = offsets[cell];
          const end = offsets[cell + 1];
          for (let idx = start; idx < end; idx++) {
            const t = triList[idx];
            if (stamp[t] === seq) continue;
            stamp[t] = seq;
            const t3 = t * 3;
            const ia = tris[t3] * 3, ib = tris[t3 + 1] * 3, ic = tris[t3 + 2] * 3;

            const ax = pos[ia], ay = pos[ia + 1], az = pos[ia + 2];
            const bx = pos[ib], by = pos[ib + 1], bz = pos[ib + 2];
            const cx_ = pos[ic], cy_ = pos[ic + 1], cz_ = pos[ic + 2];

            // Closest point on triangle ABC (Ericson barycentric projection)
            const abx = bx - ax, aby = by - ay, abz = bz - az;
            const acx = cx_ - ax, acy = cy_ - ay, acz = cz_ - az;
            const apx = px - ax, apy = py - ay, apz = pz - az;

            const d1 = abx * apx + aby * apy + abz * apz;
            const d2 = acx * apx + acy * apy + acz * apz;
            let u = 0, v = 0, w = 0;

            if (d1 <= 0 && d2 <= 0) {
              w = 1;
            } else {
              const bpx = px - bx, bpy = py - by, bpz = pz - bz;
              const d3 = abx * bpx + aby * bpy + abz * bpz;
              const d4 = acx * bpx + acy * bpy + acz * bpz;
              if (d3 >= 0 && d4 <= d3) {
                u = 1;
              } else {
                const vc = d1 * d4 - d3 * d2;
                if (vc <= 0 && d1 >= 0 && d3 <= 0) {
                  u = d1 / (d1 - d3); w = 1 - u;
                } else {
                  const cpx = px - cx_, cpy = py - cy_, cpz = pz - cz_;
                  const d5 = abx * cpx + aby * cpy + abz * cpz;
                  const d6 = acx * cpx + acy * cpy + acz * cpz;
                  if (d6 >= 0 && d5 <= d6) {
                    v = 1;
                  } else {
                    const vb = d5 * d2 - d1 * d6;
                    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
                      v = d2 / (d2 - d6); w = 1 - v;
                    } else {
                      const va = d3 * d6 - d5 * d4;
                      if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
                        const tEdge = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                        u = 1 - tEdge; v = tEdge;
                      } else {
                        const denom = 1 / (va + vb + vc);
                        u = vb * denom;
                        v = vc * denom;
                        w = 1 - u - v;
                      }
                    }
                  }
                }
              }
            }

            const qx = w * ax + u * bx + v * cx_;
            const qy = w * ay + u * by + v * cy_;
            const qz = w * az + u * bz + v * cz_;

            const rx = px - qx, ry = py - qy, rz = pz - qz;
            const dist2 = rx * rx + ry * ry + rz * rz;
            if (dist2 >= bestD2) continue;

            // Smooth interpolated pseudo-normal blended with face normal prevents edge catching
            let nx = (w * vn[ia]     + u * vn[ib]     + v * vn[ic])     * 0.65 + tn[t3]     * 0.35;
            let ny = (w * vn[ia + 1] + u * vn[ib + 1] + v * vn[ic + 1]) * 0.65 + tn[t3 + 1] * 0.35;
            let nz = (w * vn[ia + 2] + u * vn[ib + 2] + v * vn[ic + 2]) * 0.65 + tn[t3 + 2] * 0.35;
            const nLen = Math.hypot(nx, ny, nz);
            if (nLen > 1e-8) {
              const invN = 1 / nLen;
              nx *= invN; ny *= invN; nz *= invN;
            } else {
              nx = tn[t3]; ny = tn[t3 + 1]; nz = tn[t3 + 2];
            }

            const signed = rx * nx + ry * ny + rz * nz;
            // Reject deep back-side faces so front chest never snaps to spine
            if (signed < -maxCaptureDist * 0.85) continue;

            bestD2 = dist2;
            bestSigned = signed;
            bestQx = qx; bestQy = qy; bestQz = qz;
            bestNx = nx; bestNy = ny; bestNz = nz;

            const pqx = w * prev[ia]     + u * prev[ib]     + v * prev[ic];
            const pqy = w * prev[ia + 1] + u * prev[ib + 1] + v * prev[ic + 1];
            const pqz = w * prev[ia + 2] + u * prev[ib + 2] + v * prev[ic + 2];
            bestVx = qx - pqx; bestVy = qy - pqy; bestVz = qz - pqz;
            hit = true;
          }
        }
      }
    }

    if (!hit) return false;
    out.phi = bestSigned - thickness;
    out.dist = Math.sqrt(bestD2);
    out.qx = bestQx; out.qy = bestQy; out.qz = bestQz;
    out.nx = bestNx; out.ny = bestNy; out.nz = bestNz;
    out.dqx = bestVx; out.dqy = bestVy; out.dqz = bestVz;
    return true;
  }
}
