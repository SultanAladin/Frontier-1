/**
 * Chemical weathering stains for rock formations: iron-oxide "bleeding"
 * (rust streaks below seeps & joints) and copper oxidation patina
 * (malachite green / azurite blue), simulated as surface flow on the mesh.
 *
 * Model: rainwater percolates through joints and concave seep zones, picks
 * up dissolved Fe/Cu where the rock is mineral-bearing (deterministic
 * low-frequency veining), then runs DOWN the surface along steepest-descent
 * mesh edges. Each vertex deposits a fraction of the passing mineral flux
 * as stain and routes the rest downhill, so stain accumulates along real
 * drainage lines, streaks below sources, and pools in hollows -- the same
 * flow-accumulation structure as a tiny hydrological model on the mesh.
 *
 * Pure & deterministic for a given (mesh, seed, params): safe to unit test.
 * Output is a per-vertex vec2 attribute `(ironStain, copperStain)` in [0,1]
 * consumed by the viewport material (and the QA renderer). No geometry is
 * modified: chemical weathering here is staining/oxidation rinds, which is
 * what reads visually at formation scale.
 */

function clamp01(x) {
  return x < 0 ? 0 : x > 1 ? 1 : x;
}

function smoothstep(a, b, x) {
  const t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}

// Deterministic lattice hash -> [0,1). Seed-mixed, stable across runs.
function hash3(ix, iy, iz, seed) {
  let h = Math.imul(ix | 0, 374761393) ^ Math.imul(iy | 0, 668265263) ^
          Math.imul(iz | 0, 1274126177) ^ Math.imul(seed | 0, 974634211);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

function valueNoise3(x, y, z, seed) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
  const fx = x - ix, fy = y - iy, fz = z - iz;
  const sx = fx * fx * (3 - 2 * fx);
  const sy = fy * fy * (3 - 2 * fy);
  const sz = fz * fz * (3 - 2 * fz);
  const lerp = (a, b, t) => a + (b - a) * t;
  return lerp(
    lerp(
      lerp(hash3(ix, iy, iz, seed), hash3(ix + 1, iy, iz, seed), sx),
      lerp(hash3(ix, iy + 1, iz, seed), hash3(ix + 1, iy + 1, iz, seed), sx),
      sy
    ),
    lerp(
      lerp(hash3(ix, iy, iz + 1, seed), hash3(ix + 1, iy, iz + 1, seed), sx),
      lerp(hash3(ix, iy + 1, iz + 1, seed), hash3(ix + 1, iy + 1, iz + 1, seed), sx),
      sy
    ),
    sz
  );
}

/** Two-octave veining field in [0,1]. */
function veinNoise(x, y, z, seed, freq) {
  return 0.65 * valueNoise3(x * freq, y * freq, z * freq, seed) +
         0.35 * valueNoise3(x * freq * 2.7, y * freq * 2.7, z * freq * 2.7, seed ^ 0x5bd1e995);
}

/**
 * Computes per-vertex chemical stains for a formation mesh.
 *
 * @param {{positions:Float32Array, indices:Uint32Array|Uint16Array,
 *          normals?:Float32Array, colors?:Float32Array}} mesh
 *        colors is the vec4 mask attribute (ao, cavity, edge, .) from
 *        buildGameReadyAttributes.
 * @param {{amount?:number, iron?:number, copper?:number, seed?:number}} opts
 * @returns {{stains: Float32Array, stats: object}}
 */
export function computeChemicalStains(mesh, opts = {}) {
  const t0 = performance.now();
  const amount = opts.amount ?? 0.65;
  const iron = opts.iron ?? 0.55;
  const copper = opts.copper ?? 0.18;
  const seed = (opts.seed ?? 4029) | 0;

  const pos = mesh.positions;
  const idx = mesh.indices;
  const nrm = mesh.normals || null;
  const col = mesh.colors || null;
  const vCount = (pos.length / 3) | 0;
  const stains = new Float32Array(vCount * 2);
  const emptyStats = {
    vertices: 0, weldedVertices: 0,
    stainedFracFe: 0, stainedFracCu: 0, maxFe: 0, maxCu: 0,
    elapsedMs: Math.round(performance.now() - t0),
  };
  if (!vCount || !idx || idx.length < 3 || amount <= 0.001) {
    return { stains, stats: emptyStats };
  }

  // -----------------------------------------------------------------------
  // 1. Weld split vertices by quantized position; average masks/normal-Y.
  // -----------------------------------------------------------------------
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let i = 0; i < pos.length; i += 3) {
    const x = pos[i], y = pos[i + 1], z = pos[i + 2];
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  }
  const span = Math.max(maxX - minX, maxY - minY, maxZ - minZ, 1e-3);
  const quantum = Math.max(span / 20000, 1e-4);
  const invQ = 1 / quantum;

  const weldMap = new Map();
  const remap = new Int32Array(vCount);
  const wPos = [];    // 3 per welded vert
  const wNy = [];     // summed normal Y (normalized later via counts)
  const wAo = [], wCav = [], wEdge = [], wCnt = [];
  for (let v = 0; v < vCount; v++) {
    const x = pos[v * 3], y = pos[v * 3 + 1], z = pos[v * 3 + 2];
    const key = `${Math.round(x * invQ)},${Math.round(y * invQ)},${Math.round(z * invQ)}`;
    let wid = weldMap.get(key);
    if (wid === undefined) {
      wid = wCnt.length;
      weldMap.set(key, wid);
      wPos.push(x, y, z);
      wNy.push(0); wAo.push(0); wCav.push(0); wEdge.push(0); wCnt.push(0);
    }
    remap[v] = wid;
    wNy[wid] += nrm ? nrm[v * 3 + 1] : 1;
    wAo[wid] += col ? col[v * 4] : 1;
    wCav[wid] += col ? col[v * 4 + 1] : 0;
    wEdge[wid] += col ? col[v * 4 + 2] : 0;
    wCnt[wid]++;
  }
  const wCount = wCnt.length;
  for (let w = 0; w < wCount; w++) {
    const inv = 1 / wCnt[w];
    wNy[w] *= inv; wAo[w] *= inv; wCav[w] *= inv; wEdge[w] *= inv;
  }

  // -----------------------------------------------------------------------
  // 2. Unique edges -> CSR adjacency.
  // -----------------------------------------------------------------------
  const W = 4194304; // 2^22 > any plausible welded count; keys stay exact in f64
  const edgeSet = new Set();
  for (let t = 0; t < idx.length; t += 3) {
    const a = remap[idx[t]], b = remap[idx[t + 1]], c = remap[idx[t + 2]];
    if (a !== b) edgeSet.add(a < b ? a * W + b : b * W + a);
    if (b !== c) edgeSet.add(b < c ? b * W + c : c * W + b);
    if (c !== a) edgeSet.add(c < a ? c * W + a : a * W + c);
  }
  const degree = new Int32Array(wCount);
  for (const k of edgeSet) {
    degree[Math.floor(k / W)]++;
    degree[k % W]++;
  }
  const adjOff = new Int32Array(wCount + 1);
  for (let w = 0; w < wCount; w++) adjOff[w + 1] = adjOff[w] + degree[w];
  const adj = new Int32Array(adjOff[wCount]);
  const cursor = Int32Array.from(adjOff.subarray(0, wCount));
  for (const k of edgeSet) {
    const a = Math.floor(k / W), b = k % W;
    adj[cursor[a]++] = b;
    adj[cursor[b]++] = a;
  }

  // -----------------------------------------------------------------------
  // 3. Mineral sources: seeps at joints/concavities, iron/copper veining.
  // -----------------------------------------------------------------------
  const srcFe = new Float32Array(wCount);
  const srcCu = new Float32Array(wCount);
  const veinFreq = 2.6 / span * 10; // ~2-4 m mineral patches on a ~10 m formation
  for (let w = 0; w < wCount; w++) {
    const x = wPos[w * 3], y = wPos[w * 3 + 1], z = wPos[w * 3 + 2];
    const seep = clamp01(wCav[w] * 1.05 + wEdge[w] * 0.5 + (1 - wAo[w]) * 0.4);
    const exposure = clamp01(0.30 + wNy[w]); // rain-washed upward faces feed the flow
    const veinFe = smoothstep(0.50, 0.82, veinNoise(x, y, z, seed, veinFreq));
    const veinCu = smoothstep(0.58, 0.85, veinNoise(x, y, z, seed ^ 0x27d4eb2d, veinFreq * 0.8));
    srcFe[w] = amount * iron * (0.22 + 0.78 * veinFe) * (0.30 + 0.70 * seep) * exposure;
    srcCu[w] = amount * copper * veinCu * (0.30 + 0.70 * seep) * exposure;
  }

  // -----------------------------------------------------------------------
  // 4. Multi-direction slope-weighted descent + capillary dispersion (highest first)
  //    so mineral seeps form organic weeping fans/curtains rather than 1-edge straight stripes.
  // -----------------------------------------------------------------------
  const order = new Int32Array(wCount);
  for (let w = 0; w < wCount; w++) order[w] = w;
  order.sort((a, b) => wPos[b * 3 + 1] - wPos[a * 3 + 1]);

  const fluxFe = new Float32Array(wCount);
  const fluxCu = new Float32Array(wCount);
  const accFe = new Float32Array(wCount);
  const accCu = new Float32Array(wCount);
  const DEPOSIT = 0.38;
  const nbrIdx = [];
  const nbrW = [];

  for (let oi = 0; oi < wCount; oi++) {
    const v = order[oi];
    const vx = wPos[v * 3], vy = wPos[v * 3 + 1], vz = wPos[v * 3 + 2];
    nbrIdx.length = 0;
    nbrW.length = 0;
    let wSum = 0;
    for (let e = adjOff[v]; e < adjOff[v + 1]; e++) {
      const u = adj[e];
      const dy = vy - wPos[u * 3 + 1];
      if (dy <= 1e-6) continue;
      const dx = vx - wPos[u * 3];
      const dz = vz - wPos[u * 3 + 2];
      const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);
      if (dist <= 1e-8) continue;
      const slope = dy / dist;
      const w = Math.pow(slope, 1.55);
      nbrIdx.push(u);
      nbrW.push(w);
      wSum += w;
    }
    const fFe = fluxFe[v] + srcFe[v];
    const fCu = fluxCu[v] + srcCu[v];
    if (wSum > 1e-12) {
      accFe[v] += fFe * DEPOSIT;
      accCu[v] += fCu * DEPOSIT;
      const passFe = fFe * (1 - DEPOSIT) / wSum;
      const passCu = fCu * (1 - DEPOSIT) / wSum;
      for (let k = 0; k < nbrIdx.length; k++) {
        const u = nbrIdx[k];
        const w = nbrW[k];
        fluxFe[u] += passFe * w;
        fluxCu[u] += passCu * w;
      }
    } else {
      // Local hollow / toe: everything ponds and precipitates here.
      accFe[v] += fFe;
      accCu[v] += fCu;
    }
  }

  // -----------------------------------------------------------------------
  // 5. Soft-saturate to [0,1] stain and scatter back to split vertices.
  // -----------------------------------------------------------------------
  const GAIN = 4.0;
  const wFe = new Float32Array(wCount);
  const wCu = new Float32Array(wCount);
  let stainedFe = 0, stainedCu = 0, maxFe = 0, maxCu = 0;
  for (let w = 0; w < wCount; w++) {
    const fe = 1 - Math.exp(-GAIN * accFe[w]);
    const cu = 1 - Math.exp(-GAIN * accCu[w]);
    wFe[w] = fe; wCu[w] = cu;
    if (fe > 0.05) stainedFe++;
    if (cu > 0.05) stainedCu++;
    if (fe > maxFe) maxFe = fe;
    if (cu > maxCu) maxCu = cu;
  }
  for (let v = 0; v < vCount; v++) {
    const w = remap[v];
    stains[v * 2] = wFe[w];
    stains[v * 2 + 1] = wCu[w];
  }

  return {
    stains,
    stats: {
      vertices: vCount,
      weldedVertices: wCount,
      stainedFracFe: Number((stainedFe / Math.max(1, wCount)).toFixed(3)),
      stainedFracCu: Number((stainedCu / Math.max(1, wCount)).toFixed(3)),
      maxFe: Number(maxFe.toFixed(3)),
      maxCu: Number(maxCu.toFixed(3)),
      elapsedMs: Math.round(performance.now() - t0),
    },
  };
}
