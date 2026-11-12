/*==========================================================================================================================================
                                                            GPUBAKEENGINE.JS
==========================================================================================================================================*/
/* Real GPU Mesh Map Baker & 3D Volume SDF Baker:
   - Tier 1: WebGPU Compute (WGSL @compute pipelines over GPU storage buffers for mesh maps + 3D SDF)
   - Tier 2: WebGL2 GPU Fragment Raytracer & Edge Dilator (RGBA32F spatial-grid DataTextures -> RGBA8 RenderTargets,
             with IEEE-754 floatBitsToUint bit packing for exact Float32 Volume SDF without requiring float FBO extensions)
   - Clean telemetry reporting active GPU device & bake duration in ms. */

import * as THREE from "three";

const GRID_DIM = 16;

// --------------------------------------------------------------------------- SHARED GEOMETRY PACKING (O(T))
export function packBakeGeometryForGpu(lowParts, highParts) {
  const tris = [];
  const bbox = new THREE.Box3();

  for (let pIdx = 0; pIdx < highParts.length; pIdx++) {
    const g = highParts[pIdx].geometry;
    if (!g.attributes.normal) g.computeVertexNormals();
    const pos = g.attributes.position;
    const norm = g.attributes.normal;
    const idx = g.index ? g.index.array : null;
    const count = idx ? idx.length / 3 : pos.count / 3;

    for (let t = 0; t < count; t++) {
      const i0 = idx ? idx[t * 3] : t * 3;
      const i1 = idx ? idx[t * 3 + 1] : t * 3 + 1;
      const i2 = idx ? idx[t * 3 + 2] : t * 3 + 2;

      const ax = pos.getX(i0), ay = pos.getY(i0), az = pos.getZ(i0);
      const bx = pos.getX(i1), by = pos.getY(i1), bz = pos.getZ(i1);
      const cx = pos.getX(i2), cy = pos.getY(i2), cz = pos.getZ(i2);

      const minX = Math.min(ax, bx, cx), maxX = Math.max(ax, bx, cx);
      const minY = Math.min(ay, by, cy), maxY = Math.max(ay, by, cy);
      const minZ = Math.min(az, bz, cz), maxZ = Math.max(az, bz, cz);
      bbox.min.min(new THREE.Vector3(minX, minY, minZ));
      bbox.max.max(new THREE.Vector3(maxX, maxY, maxZ));

      const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
      const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
      let fnx = e1y * e2z - e1z * e2y;
      let fny = e1z * e2x - e1x * e2z;
      let fnz = e1x * e2y - e1y * e2x;
      const fl = Math.hypot(fnx, fny, fnz) || 1;
      fnx /= fl; fny /= fl; fnz /= fl;

      tris.push({
        partIdx: pIdx,
        a: [ax, ay, az],
        b: [bx, by, bz],
        c: [cx, cy, cz],
        na: [norm.getX(i0), norm.getY(i0), norm.getZ(i0)],
        nb: [norm.getX(i1), norm.getY(i1), norm.getZ(i1)],
        nc: [norm.getX(i2), norm.getY(i2), norm.getZ(i2)],
        fn: [fnx, fny, fnz],
        min: [minX, minY, minZ],
        max: [maxX, maxY, maxZ],
      });
    }
  }

  bbox.min.subScalar(0.02);
  bbox.max.addScalar(0.02);
  const size = bbox.getSize(new THREE.Vector3());
  const numCells = GRID_DIM * GRID_DIM * GRID_DIM;
  const cells = Array.from({ length: numCells }, () => []);

  const cellOf = (x, y, z) => [
    Math.min(GRID_DIM - 1, Math.max(0, Math.floor(((x - bbox.min.x) / Math.max(1e-5, size.x)) * GRID_DIM))),
    Math.min(GRID_DIM - 1, Math.max(0, Math.floor(((y - bbox.min.y) / Math.max(1e-5, size.y)) * GRID_DIM))),
    Math.min(GRID_DIM - 1, Math.max(0, Math.floor(((z - bbox.min.z) / Math.max(1e-5, size.z)) * GRID_DIM))),
  ];

  let totalRefs = 0;
  for (let ti = 0; ti < tris.length; ti++) {
    const tr = tris[ti];
    const [x0, y0, z0] = cellOf(tr.min[0], tr.min[1], tr.min[2]);
    const [x1, y1, z1] = cellOf(tr.max[0], tr.max[1], tr.max[2]);
    for (let z = z0; z <= z1; z++) {
      for (let y = y0; y <= y1; y++) {
        for (let x = x0; x <= x1; x++) {
          const cid = (z * GRID_DIM + y) * GRID_DIM + x;
          cells[cid].push(ti);
          totalRefs++;
        }
      }
    }
  }

  // Pack triangle data: 7 vec4s per triangle = 28 floats per triangle
  // [0]: a.xyz, partIdx
  // [1]: b.xyz, 0
  // [2]: c.xyz, 0
  // [3]: na.xyz, 0
  // [4]: nb.xyz, 0
  // [5]: nc.xyz, 0
  // [6]: fn.xyz, 0
  const triData = new Float32Array(Math.max(1, tris.length) * 28);
  for (let i = 0; i < tris.length; i++) {
    const t = tris[i];
    const o = i * 28;
    triData[o + 0] = t.a[0]; triData[o + 1] = t.a[1]; triData[o + 2] = t.a[2]; triData[o + 3] = t.partIdx;
    triData[o + 4] = t.b[0]; triData[o + 5] = t.b[1]; triData[o + 6] = t.b[2]; triData[o + 7] = 0;
    triData[o + 8] = t.c[0]; triData[o + 9] = t.c[1]; triData[o + 10] = t.c[2]; triData[o + 11] = 0;
    triData[o + 12] = t.na[0]; triData[o + 13] = t.na[1]; triData[o + 14] = t.na[2]; triData[o + 15] = 0;
    triData[o + 16] = t.nb[0]; triData[o + 17] = t.nb[1]; triData[o + 18] = t.nb[2]; triData[o + 19] = 0;
    triData[o + 20] = t.nc[0]; triData[o + 21] = t.nc[1]; triData[o + 22] = t.nc[2]; triData[o + 23] = 0;
    triData[o + 24] = t.fn[0]; triData[o + 25] = t.fn[1]; triData[o + 26] = t.fn[2]; triData[o + 27] = 0;
  }

  // Pack spatial cell offsets & counts + flat triangle index list
  const gridMeta = new Uint32Array(numCells * 2);
  const cellList = new Uint32Array(Math.max(1, totalRefs));
  let cursor = 0;
  for (let c = 0; c < numCells; c++) {
    const arr = cells[c];
    gridMeta[c * 2] = cursor;
    gridMeta[c * 2 + 1] = arr.length;
    for (let k = 0; k < arr.length; k++) {
      cellList[cursor++] = arr[k];
    }
  }

  return {
    triCount: tris.length,
    triData,
    gridMeta,
    cellList,
    bboxMin: [bbox.min.x, bbox.min.y, bbox.min.z],
    bboxSize: [Math.max(1e-5, size.x), Math.max(1e-5, size.y), Math.max(1e-5, size.z)],
  };
}

export function rasterizeLowMeshGBuffer(lowParts, res) {
  const n = res * res;
  const posCov = new Float32Array(n * 4);
  const normPart = new Float32Array(n * 4);
  const tangIsland = new Float32Array(n * 4);
  const bitanWire = new Float32Array(n * 4);

  for (let pIdx = 0; pIdx < lowParts.length; pIdx++) {
    const g = lowParts[pIdx].geometry;
    if (!g.attributes.normal) g.computeVertexNormals();
    const pos = g.attributes.position;
    const norm = g.attributes.normal;
    const uv = g.attributes.uv;
    if (!uv) throw new Error("Low poly target requires UV coordinates.");
    const partAttr = g.attributes.surfacePart;
    const idx = g.index ? g.index.array : null;
    const tris = idx ? idx.length / 3 : pos.count / 3;

    for (let t = 0; t < tris; t++) {
      const i0 = idx ? idx[t * 3] : t * 3;
      const i1 = idx ? idx[t * 3 + 1] : t * 3 + 1;
      const i2 = idx ? idx[t * 3 + 2] : t * 3 + 2;

      const u0 = uv.getX(i0), v0 = uv.getY(i0);
      const u1 = uv.getX(i1), v1 = uv.getY(i1);
      const u2 = uv.getX(i2), v2 = uv.getY(i2);
      const x0 = u0 * res, y0 = v0 * res;
      const x1 = u1 * res, y1 = v1 * res;
      const x2 = u2 * res, y2 = v2 * res;

      const denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
      if (Math.abs(denom) < 1e-6) continue;

      const p0 = [pos.getX(i0), pos.getY(i0), pos.getZ(i0)];
      const p1 = [pos.getX(i1), pos.getY(i1), pos.getZ(i1)];
      const p2 = [pos.getX(i2), pos.getY(i2), pos.getZ(i2)];
      const du1 = u1 - u0, dv1 = v1 - v0;
      const du2 = u2 - u0, dv2 = v2 - v0;
      const uvDet = du1 * dv2 - du2 * dv1;
      let tx = 1, ty = 0, tz = 0, bx = 0, by = 0, bz = 1;
      if (Math.abs(uvDet) > 1e-8) {
        const rDet = 1 / uvDet;
        tx = ((p1[0] - p0[0]) * dv2 - (p2[0] - p0[0]) * dv1) * rDet;
        ty = ((p1[1] - p0[1]) * dv2 - (p2[1] - p0[1]) * dv1) * rDet;
        tz = ((p1[2] - p0[2]) * dv2 - (p2[2] - p0[2]) * dv1) * rDet;
        const tl = Math.hypot(tx, ty, tz) || 1;
        tx /= tl; ty /= tl; tz /= tl;

        bx = ((p2[0] - p0[0]) * du1 - (p1[0] - p0[0]) * du2) * rDet;
        by = ((p2[1] - p0[1]) * du1 - (p1[1] - p0[1]) * du2) * rDet;
        bz = ((p2[2] - p0[2]) * du1 - (p1[2] - p0[2]) * du2) * rDet;
        const bl = Math.hypot(bx, by, bz) || 1;
        bx /= bl; by /= bl; bz /= bl;
      }

      const partId = partAttr ? Math.round(partAttr.getX(i0)) : pIdx;
      const islandId = Math.floor(u0 * 4) + Math.floor(v0 * 4) * 4 + 1 + pIdx * 16;

      const minX = Math.max(0, Math.floor(Math.min(x0, x1, x2)));
      const maxX = Math.min(res - 1, Math.ceil(Math.max(x0, x1, x2)));
      const minY = Math.max(0, Math.floor(Math.min(y0, y1, y2)));
      const maxY = Math.min(res - 1, Math.ceil(Math.max(y0, y1, y2)));

      for (let py = minY; py <= maxY; py++) {
        for (let px = minX; px <= maxX; px++) {
          const sx = px + 0.5, sy = py + 0.5;
          const w0 = ((y1 - y2) * (sx - x2) + (x2 - x1) * (sy - y2)) / denom;
          const w1 = ((y2 - y0) * (sx - x2) + (x0 - x2) * (sy - y2)) / denom;
          const w2 = 1 - w0 - w1;
          if (w0 < -0.01 || w1 < -0.01 || w2 < -0.01) continue;
          const pid = py * res + px;
          const o = pid * 4;

          posCov[o] = w0 * p0[0] + w1 * p1[0] + w2 * p2[0];
          posCov[o + 1] = w0 * p0[1] + w1 * p1[1] + w2 * p2[1];
          posCov[o + 2] = w0 * p0[2] + w1 * p1[2] + w2 * p2[2];
          posCov[o + 3] = 1.0; // covered

          let nx = w0 * norm.getX(i0) + w1 * norm.getX(i1) + w2 * norm.getX(i2);
          let ny = w0 * norm.getY(i0) + w1 * norm.getY(i1) + w2 * norm.getY(i2);
          let nz = w0 * norm.getZ(i0) + w1 * norm.getZ(i1) + w2 * norm.getZ(i2);
          const nl = Math.hypot(nx, ny, nz) || 1;
          normPart[o] = nx / nl;
          normPart[o + 1] = ny / nl;
          normPart[o + 2] = nz / nl;
          normPart[o + 3] = partId;

          tangIsland[o] = tx;
          tangIsland[o + 1] = ty;
          tangIsland[o + 2] = tz;
          tangIsland[o + 3] = islandId;

          bitanWire[o] = bx;
          bitanWire[o + 1] = by;
          bitanWire[o + 2] = bz;
          bitanWire[o + 3] = (w0 < 0.045 || w1 < 0.045 || w2 < 0.045) ? 1.0 : 0.0;
        }
      }
    }
  }
  return { posCov, normPart, tangIsland, bitanWire };
}

// --------------------------------------------------------------------------- WEBGPU COMPUTE SHADERS (WGSL)
export const BAKE_WGSL_SHADERS = {
  MESH_RAY_WGSL: /* wgsl */ `
struct BakeParams {
  res: u32,
  samples: u32,
  triCount: u32,
  flipY: u32,
  frontDist: f32,
  rearDist: f32,
  aoDist: f32,
  thicknessRange: f32,
  bboxMinX: f32,
  bboxMinY: f32,
  bboxMinZ: f32,
  pad0: f32,
  bboxSizeX: f32,
  bboxSizeY: f32,
  bboxSizeZ: f32,
  pad1: f32,
};

struct Tri {
  a: vec4<f32>,
  b: vec4<f32>,
  c: vec4<f32>,
  na: vec4<f32>,
  nb: vec4<f32>,
  nc: vec4<f32>,
  fn: vec4<f32>,
};

struct RayHit {
  hit: u32,
  t: f32,
  u: f32,
  v: f32,
  w: f32,
  triIdx: u32,
};

@group(0) @binding(0) var<uniform> P: BakeParams;
@group(0) @binding(1) var<storage, read> tris: array<Tri>;
@group(0) @binding(2) var<storage, read> gridMeta: array<vec2<u32>>;
@group(0) @binding(3) var<storage, read> cellList: array<u32>;
@group(0) @binding(4) var<storage, read> lowPosCov: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read> lowNormPart: array<vec4<f32>>;
@group(0) @binding(6) var<storage, read> lowTangIsland: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> lowBitanWire: array<vec4<f32>>;

// Output storage buffers per core pass (Packed RGBA8 as u32 per pixel!)
@group(0) @binding(8) var<storage, read_write> outHighNorm: array<vec4<f32>>;
@group(0) @binding(9) var<storage, read_write> outSurfaceA: array<vec4<f32>>; // (ao, secAo, height, thickness)
@group(0) @binding(10) var<storage, read_write> outBentNorm: array<vec4<f32>>;

fn intersectTri(ro: vec3<f32>, rd: vec3<f32>, ti: u32) -> RayHit {
  var rh: RayHit;
  rh.hit = 0u;
  let tr = tris[ti];
  let e1 = tr.b.xyz - tr.a.xyz;
  let e2 = tr.c.xyz - tr.a.xyz;
  let pv = cross(rd, e2);
  let det = dot(e1, pv);
  if (abs(det) < 1e-8) { return rh; }
  let inv = 1.0 / det;
  let tv = ro - tr.a.xyz;
  let u = dot(tv, pv) * inv;
  if (u < -0.0001 || u > 1.0001) { return rh; }
  let qv = cross(tv, e1);
  let v = dot(rd, qv) * inv;
  if (v < -0.0001 || (u + v) > 1.0001) { return rh; }
  let t = dot(e2, qv) * inv;
  if (t > 0.0001) {
    rh.hit = 1u;
    rh.t = t;
    rh.u = u;
    rh.v = v;
    rh.w = 1.0 - u - v;
    rh.triIdx = ti;
  }
  return rh;
}

fn raycastGrid(ro: vec3<f32>, rd: vec3<f32>, maxDist: f32) -> RayHit {
  var best: RayHit;
  best.hit = 0u;
  best.t = maxDist;

  let bMin = vec3<f32>(P.bboxMinX, P.bboxMinY, P.bboxMinZ);
  let bSize = vec3<f32>(P.bboxSizeX, P.bboxSizeY, P.bboxSizeZ);
  let endPos = ro + rd * maxDist;
  let segMin = min(ro, endPos);
  let segMax = max(ro, endPos);

  let i0 = clamp(vec3<i32>(floor(((segMin - bMin) / bSize) * 16.0)), vec3<i32>(0), vec3<i32>(15));
  let i1 = clamp(vec3<i32>(floor(((segMax - bMin) / bSize) * 16.0)), vec3<i32>(0), vec3<i32>(15));

  for (var z = i0.z; z <= i1.z; z = z + 1) {
    for (var y = i0.y; y <= i1.y; y = y + 1) {
      for (var x = i0.x; x <= i1.x; x = x + 1) {
        let cid = u32((z * 16 + y) * 16 + x);
        let meta = gridMeta[cid];
        for (var k = 0u; k < meta.y; k = k + 1u) {
          let ti = cellList[meta.x + k];
          let h = intersectTri(ro, rd, ti);
          if (h.hit != 0u && h.t < best.t) {
            best = h;
          }
        }
      }
    }
  }
  return best;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= P.res || gid.y >= P.res) { return; }
  // Flip Y to match canvas top-down UV storage
  let row = P.res - 1u - gid.y;
  let pid = row * P.res + gid.x;
  let cov = lowPosCov[pid];
  if (cov.w < 0.5) {
    outHighNorm[pid] = vec4<f32>(0.0);
    outSurfaceA[pid] = vec4<f32>(1.0, 1.0, 0.5, 1.0);
    outBentNorm[pid] = vec4<f32>(0.5, 0.5, 1.0, 0.0);
    return;
  }

  let wp = cov.xyz;
  let ln = normalize(lowNormPart[pid].xyz);
  let tu = lowTangIsland[pid].xyz;
  let bv = lowBitanWire[pid].xyz;

  // High-to-Low projection ray
  let hFwd = raycastGrid(wp + ln * P.frontDist, -ln, P.frontDist + P.rearDist);
  var hn = ln;
  var relH = 0.0;
  if (hFwd.hit != 0u) {
    let tr = tris[hFwd.triIdx];
    hn = normalize(tr.na.xyz * hFwd.w + tr.nb.xyz * hFwd.u + tr.nc.xyz * hFwd.v);
    relH = P.frontDist - hFwd.t;
  } else {
    let hBack = raycastGrid(wp - ln * P.rearDist, ln, P.frontDist + P.rearDist);
    if (hBack.hit != 0u) {
      let tr = tris[hBack.triIdx];
      hn = normalize(tr.na.xyz * hBack.w + tr.nb.xyz * hBack.u + tr.nc.xyz * hBack.v);
      relH = hBack.t - P.rearDist;
    }
  }
  outHighNorm[pid] = vec4<f32>(hn, relH);

  // Tangent basis around low normal for hemisphere AO + Bent Normal
  let helper = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(ln.y) > 0.9);
  let hT = normalize(cross(helper, ln));
  let hB = cross(ln, hT);
  let ro = wp + ln * 0.004;
  var occ = 0.0;
  var secOcc = 0.0;
  var bent = vec3<f32>(0.0);
  let nSamp = max(4u, P.samples);
  let invN = 1.0 / f32(nSamp);

  for (var s = 0u; s < nSamp; s = s + 1u) {
    let fi = (f32(s) + 0.5) * invN;
    let r = sqrt(fi);
    let phi = 2.39996323 * f32(s) + f32(gid.x + gid.y * 7u) * 0.13;
    let lx = cos(phi) * r;
    let ly = sin(phi) * r;
    let lz = sqrt(max(0.0, 1.0 - r * r));
    let rd = normalize(hT * lx + hB * ly + ln * lz);
    let h = raycastGrid(ro, rd, P.aoDist);
    if (h.hit != 0u) {
      occ += 1.0 - (h.t / P.aoDist) * 0.45;
      if (h.t < P.aoDist * 0.3) { secOcc += 1.0; }
    } else {
      bent += rd;
    }
  }
  let aoVal = clamp(1.0 - occ * invN, 0.0, 1.0);
  let secAoVal = clamp(1.0 - secOcc * invN, 0.0, 1.0);
  let heightNorm = clamp(0.5 + relH / max(0.01, P.frontDist + P.rearDist), 0.0, 1.0);

  // Thickness inward raycast
  let hThick = raycastGrid(wp - ln * 0.012, -ln, P.thicknessRange);
  let thickVal = select(1.0, clamp(hThick.t / max(0.05, P.thicknessRange), 0.0, 1.0), hThick.hit != 0u);

  outSurfaceA[pid] = vec4<f32>(aoVal, secAoVal, heightNorm, thickVal);
  let bl = length(bent);
  let bDir = select(ln, bent / bl, bl > 1e-4);
  outBentNorm[pid] = vec4<f32>(bDir, 1.0);
}
`,

  SDF_WGSL: /* wgsl */ `
struct SdfParams {
  nx: u32,
  ny: u32,
  nz: u32,
  signedFlag: u32,
  minX: f32, minY: f32, minZ: f32, pad0: f32,
  spanX: f32, spanY: f32, spanZ: f32, pad1: f32,
  bboxMinX: f32, bboxMinY: f32, bboxMinZ: f32, pad2: f32,
  bboxSizeX: f32, bboxSizeY: f32, bboxSizeZ: f32, pad3: f32,
};

struct Tri {
  a: vec4<f32>,
  b: vec4<f32>,
  c: vec4<f32>,
  na: vec4<f32>,
  nb: vec4<f32>,
  nc: vec4<f32>,
  fn: vec4<f32>,
};

@group(0) @binding(0) var<uniform> P: SdfParams;
@group(0) @binding(1) var<storage, read> tris: array<Tri>;
@group(0) @binding(2) var<storage, read> gridMeta: array<vec2<u32>>;
@group(0) @binding(3) var<storage, read> cellList: array<u32>;
@group(0) @binding(4) var<storage, read_write> outSdf: array<f32>;

fn pointTriDistSq(p: vec3<f32>, a: vec3<f32>, b: vec3<f32>, c: vec3<f32>) -> f32 {
  let ab = b - a;
  let ac = c - a;
  let ap = p - a;
  let d1 = dot(ab, ap);
  let d2 = dot(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) { return dot(ap, ap); }

  let bp = p - b;
  let d3 = dot(ab, bp);
  let d4 = dot(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) { return dot(bp, bp); }

  let vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    let v = d1 / (d1 - d3);
    let q = a + ab * v - p;
    return dot(q, q);
  }

  let cp = p - c;
  let d5 = dot(ab, cp);
  let d6 = dot(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) { return dot(cp, cp); }

  let vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    let w = d2 / (d2 - d6);
    let q = a + ac * w - p;
    return dot(q, q);
  }

  let va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
    let w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    let q = b + (c - b) * w - p;
    return dot(q, q);
  }

  let denom = 1.0 / (va + vb + vc);
  let v = vb * denom;
  let w = vc * denom;
  let q = a + ab * v + ac * w - p;
  return dot(q, q);
}

fn intersectX(ro: vec3<f32>, ti: u32) -> f32 {
  let tr = tris[ti];
  let e1 = tr.b.xyz - tr.a.xyz;
  let e2 = tr.c.xyz - tr.a.xyz;
  let rd = vec3<f32>(1.0, 0.0, 0.0);
  let pv = cross(rd, e2);
  let det = dot(e1, pv);
  if (abs(det) < 1e-8) { return -1.0; }
  let inv = 1.0 / det;
  let tv = ro - tr.a.xyz;
  let u = dot(tv, pv) * inv;
  if (u < -0.0001 || u > 1.0001) { return -1.0; }
  let qv = cross(tv, e1);
  let v = dot(rd, qv) * inv;
  if (v < -0.0001 || (u + v) > 1.0001) { return -1.0; }
  return dot(e2, qv) * inv;
}

@compute @workgroup_size(4, 4, 4)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= P.nx || gid.y >= P.ny || gid.z >= P.nz) { return; }
  let p = vec3<f32>(
    P.minX + ((f32(gid.x) + 0.5) / f32(P.nx)) * P.spanX,
    P.minY + ((f32(gid.y) + 0.5) / f32(P.ny)) * P.spanY,
    P.minZ + ((f32(gid.z) + 0.5) / f32(P.nz)) * P.spanZ,
  );
  let bMin = vec3<f32>(P.bboxMinX, P.bboxMinY, P.bboxMinZ);
  let bSize = vec3<f32>(P.bboxSizeX, P.bboxSizeY, P.bboxSizeZ);
  let cell = clamp(vec3<i32>(floor(((p - bMin) / bSize) * 16.0)), vec3<i32>(0), vec3<i32>(15));
  let cellDiag = length(bSize / 16.0);

  var bestD2 = 1e20;
  for (var rad = 0; rad <= 16; rad = rad + 1) {
    let x0 = max(0, cell.x - rad); let x1 = min(15, cell.x + rad);
    let y0 = max(0, cell.y - rad); let y1 = min(15, cell.y + rad);
    let z0 = max(0, cell.z - rad); let z1 = min(15, cell.z + rad);
    for (var z = z0; z <= z1; z = z + 1) {
      for (var y = y0; y <= y1; y = y + 1) {
        for (var x = x0; x <= x1; x = x + 1) {
          if (rad > 0 && max(abs(x - cell.x), max(abs(y - cell.y), abs(z - cell.z))) < rad) { continue; }
          let cid = u32((z * 16 + y) * 16 + x);
          let meta = gridMeta[cid];
          for (var k = 0u; k < meta.y; k = k + 1u) {
            let ti = cellList[meta.x + k];
            let tr = tris[ti];
            let d2 = pointTriDistSq(p, tr.a.xyz, tr.b.xyz, tr.c.xyz);
            if (d2 < bestD2) { bestD2 = d2; }
          }
        }
      }
    }
    if (bestD2 < f32(rad * rad) * cellDiag * cellDiag) { break; }
  }
  var dist = sqrt(bestD2);
  if (P.signedFlag != 0u) {
    var hits = 0u;
    var ox = p.x;
    let maxRay = P.spanX + 1.0;
    for (var s = 0u; s < 10u; s = s + 1u) {
      let cx0 = clamp(i32(floor(((ox - bMin.x) / bSize.x) * 16.0)), 0, 15);
      var bestT = maxRay;
      for (var cx = cx0; cx <= 15; cx = cx + 1) {
        let cid = u32((cell.z * 16 + cell.y) * 16 + cx);
        let meta = gridMeta[cid];
        for (var k = 0u; k < meta.y; k = k + 1u) {
          let ti = cellList[meta.x + k];
          let t = intersectX(vec3<f32>(ox, p.y, p.z), ti);
          if (t > 0.0001 && t < bestT) { bestT = t; }
        }
      }
      if (bestT >= maxRay) { break; }
      hits = hits + 1u;
      ox = ox + bestT + 0.002;
    }
    if ((hits & 1u) == 1u) { dist = -dist; }
  }
  let idx = (gid.z * P.ny + gid.y) * P.nx + gid.x;
  outSdf[idx] = dist;
}
`,
};

// --------------------------------------------------------------------------- WEBGL2 GPU SHADERS (GLSL ES 3.00)
const FULLSCREEN_GLSL3_VERT = /* glsl */ `
out vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = vec4(position.xy, 0.0, 1.0);
}`;

const WEBGL2_BAKE_COMMON = /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uTriDataTex; // width 1024 RGBA32F, 7 texels per tri
uniform sampler2D uGridMetaTex; // 64x64 RGBA32F -> (offset, count, 0, 0)
uniform sampler2D uCellListTex; // width 1024 RGBA32F -> scalar index in .r
uniform sampler2D uLowPosCov;
uniform sampler2D uLowNormPart;
uniform sampler2D uLowTangIsland;
uniform sampler2D uLowBitanWire;
uniform vec3 uBboxMin;
uniform vec3 uBboxSize;
uniform float uRes;
uniform int uSamples;
uniform float uFrontDist;
uniform float uRearDist;
uniform float uAoDist;
uniform float uThickRange;
in vec2 vUv;

vec4 fetchTriVec(int ti, int slot) {
  int lin = ti * 7 + slot;
  return texelFetch(uTriDataTex, ivec2(lin & 1023, lin >> 10), 0);
}

ivec2 fetchGridMeta(int cid) {
  vec4 m = texelFetch(uGridMetaTex, ivec2(cid & 63, cid >> 6), 0);
  return ivec2(int(m.x + 0.5), int(m.y + 0.5));
}

int fetchCellTri(int idx) {
  vec4 v = texelFetch(uCellListTex, ivec2(idx & 1023, idx >> 10), 0);
  return int(v.x + 0.5);
}

bool intersectTri(vec3 ro, vec3 rd, int ti, out float outT, out float outU, out float outV) {
  vec3 a = fetchTriVec(ti, 0).xyz;
  vec3 b = fetchTriVec(ti, 1).xyz;
  vec3 c = fetchTriVec(ti, 2).xyz;
  vec3 e1 = b - a;
  vec3 e2 = c - a;
  vec3 pv = cross(rd, e2);
  float det = dot(e1, pv);
  if (abs(det) < 1e-8) return false;
  float inv = 1.0 / det;
  vec3 tv = ro - a;
  float u = dot(tv, pv) * inv;
  if (u < -0.0001 || u > 1.0001) return false;
  vec3 qv = cross(tv, e1);
  float v = dot(rd, qv) * inv;
  if (v < -0.0001 || u + v > 1.0001) return false;
  float t = dot(e2, qv) * inv;
  if (t <= 0.0001) return false;
  outT = t; outU = u; outV = v;
  return true;
}

bool raycastGrid(vec3 ro, vec3 rd, float maxDist, out float bestT, out float bestU, out float bestV, out int bestTi) {
  bestT = maxDist;
  bestTi = -1;
  vec3 ep = ro + rd * maxDist;
  vec3 sMin = min(ro, ep);
  vec3 sMax = max(ro, ep);
  ivec3 i0 = clamp(ivec3(floor(((sMin - uBboxMin) / uBboxSize) * 16.0)), ivec3(0), ivec3(15));
  ivec3 i1 = clamp(ivec3(floor(((sMax - uBboxMin) / uBboxSize) * 16.0)), ivec3(0), ivec3(15));

  for (int z = i0.z; z <= i1.z; z++) {
    for (int y = i0.y; y <= i1.y; y++) {
      for (int x = i0.x; x <= i1.x; x++) {
        int cid = (z * 16 + y) * 16 + x;
        ivec2 meta = fetchGridMeta(cid);
        for (int k = 0; k < 256; k++) {
          if (k >= meta.y) break;
          int ti = fetchCellTri(meta.x + k);
          float t, u, v;
          if (intersectTri(ro, rd, ti, t, u, v) && t < bestT) {
            bestT = t; bestU = u; bestV = v; bestTi = ti;
          }
        }
      }
    }
  }
  return bestTi >= 0;
}
`;

export const BAKE_GLSL3_SHADERS = {
  CORE_RAY_FRAG: /* glsl */ `${WEBGL2_BAKE_COMMON}
uniform int uPassMode; // 0 = highNormRGB+height, 1 = surface(ao,secAo,thick,cov), 2 = bentNormRGB+cov
out vec4 fragColor;

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 cov = texelFetch(uLowPosCov, px, 0);
  if (cov.w < 0.5) {
    fragColor = uPassMode == 1 ? vec4(1.0, 1.0, 1.0, 0.0) : vec4(0.5, 0.5, 1.0, 0.0);
    return;
  }
  vec3 wp = cov.xyz;
  vec3 ln = normalize(texelFetch(uLowNormPart, px, 0).xyz);

  if (uPassMode == 0) {
    float bt, bu, bv; int bti;
    vec3 hn = ln;
    float relH = 0.0;
    if (raycastGrid(wp + ln * uFrontDist, -ln, uFrontDist + uRearDist, bt, bu, bv, bti)) {
      float bw = 1.0 - bu - bv;
      hn = normalize(fetchTriVec(bti, 3).xyz * bw + fetchTriVec(bti, 4).xyz * bu + fetchTriVec(bti, 5).xyz * bv);
      relH = uFrontDist - bt;
    } else if (raycastGrid(wp - ln * uRearDist, ln, uFrontDist + uRearDist, bt, bu, bv, bti)) {
      float bw = 1.0 - bu - bv;
      hn = normalize(fetchTriVec(bti, 3).xyz * bw + fetchTriVec(bti, 4).xyz * bu + fetchTriVec(bti, 5).xyz * bv);
      relH = bt - uRearDist;
    }
    float hNorm = clamp(0.5 + relH / max(0.01, uFrontDist + uRearDist), 0.0, 1.0);
    fragColor = vec4(hn * 0.5 + 0.5, hNorm);
    return;
  }

  vec3 helper = abs(ln.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 hT = normalize(cross(helper, ln));
  vec3 hB = cross(ln, hT);
  vec3 ro = wp + ln * 0.004;
  float occ = 0.0;
  float secOcc = 0.0;
  vec3 bent = vec3(0.0);
  int nS = max(4, uSamples);
  float invN = 1.0 / float(nS);
  for (int s = 0; s < 48; s++) {
    if (s >= nS) break;
    float fi = (float(s) + 0.5) * invN;
    float r = sqrt(fi);
    float phi = 2.39996323 * float(s) + float(px.x + px.y * 7) * 0.13;
    vec3 rd = normalize(hT * (cos(phi) * r) + hB * (sin(phi) * r) + ln * sqrt(max(0.0, 1.0 - r * r)));
    float bt, bu, bv; int bti;
    if (raycastGrid(ro, rd, uAoDist, bt, bu, bv, bti)) {
      occ += 1.0 - (bt / uAoDist) * 0.45;
      if (bt < uAoDist * 0.3) secOcc += 1.0;
    } else {
      bent += rd;
    }
  }
  if (uPassMode == 2) {
    float bl = length(bent);
    vec3 bDir = bl > 1e-4 ? (bent / bl) : ln;
    fragColor = vec4(bDir * 0.5 + 0.5, 1.0);
    return;
  }
  float aoVal = clamp(1.0 - occ * invN, 0.0, 1.0);
  float secAo = clamp(1.0 - secOcc * invN, 0.0, 1.0);
  float tt, tu, tv; int tti;
  float thick = raycastGrid(wp - ln * 0.012, -ln, uThickRange, tt, tu, tv, tti)
    ? clamp(tt / max(0.05, uThickRange), 0.0, 1.0)
    : 1.0;
  fragColor = vec4(aoVal, secAo, thick, 1.0);
}
`,

  DERIVE_CHANNEL_FRAG: /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uHighNormHeight;
uniform sampler2D uSurfaceStats; // ao, secAo, thick, cov
uniform sampler2D uBentNorm;
uniform sampler2D uLowPosCov;
uniform sampler2D uLowNormPart;
uniform sampler2D uLowTangIsland;
uniform sampler2D uLowBitanWire;
uniform vec3 uBboxMin;
uniform vec3 uBboxSize;
uniform int uChannelIdx;
uniform float uFlipY;
in vec2 vUv;
out vec4 fragColor;

vec3 paletteId(float id) {
  float hue = fract(id * 0.61803398875 + 0.13);
  vec3 rgb = clamp(abs(mod(hue * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
  return 0.56 + (rgb - 0.5) * 0.52;
}

float calcCurvature(ivec2 px, vec3 hn) {
  vec4 pL = texelFetch(uLowPosCov, px + ivec2(-1, 0), 0);
  vec4 pR = texelFetch(uLowPosCov, px + ivec2(1, 0), 0);
  vec4 pU = texelFetch(uLowPosCov, px + ivec2(0, 1), 0);
  vec4 pD = texelFetch(uLowPosCov, px + ivec2(0, -1), 0);
  if (pL.w < 0.5 || pR.w < 0.5 || pU.w < 0.5 || pD.w < 0.5) return 0.5;
  vec3 nL = normalize(texelFetch(uHighNormHeight, px + ivec2(-1, 0), 0).xyz * 2.0 - 1.0);
  vec3 nR = normalize(texelFetch(uHighNormHeight, px + ivec2(1, 0), 0).xyz * 2.0 - 1.0);
  vec3 nU = normalize(texelFetch(uHighNormHeight, px + ivec2(0, 1), 0).xyz * 2.0 - 1.0);
  vec3 nD = normalize(texelFetch(uHighNormHeight, px + ivec2(0, -1), 0).xyz * 2.0 - 1.0);
  vec3 dpx = pR.xyz - pL.xyz;
  vec3 dpy = pU.xyz - pD.xyz;
  vec3 dnx = nR - nL;
  vec3 dny = nU - nD;
  float lx2 = max(1e-6, dot(dpx, dpx));
  float ly2 = max(1e-6, dot(dpy, dpy));
  float k1 = dot(dnx, dpx) / lx2;
  float k2 = dot(dny, dpy) / ly2;
  return clamp(0.5 + (k1 + k2) * 0.08, 0.0, 1.0);
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 lp = texelFetch(uLowPosCov, px, 0);
  if (lp.w < 0.5) { fragColor = vec4(0.0); return; }

  vec4 hnH = texelFetch(uHighNormHeight, px, 0);
  vec3 hn = normalize(hnH.xyz * 2.0 - 1.0);
  vec4 stats = texelFetch(uSurfaceStats, px, 0);
  vec3 lnPart = texelFetch(uLowNormPart, px, 0).xyz;
  float partId = texelFetch(uLowNormPart, px, 0).w;
  vec3 tu = texelFetch(uLowTangIsland, px, 0).xyz;
  float islandId = texelFetch(uLowTangIsland, px, 0).w;
  vec3 bv = texelFetch(uLowBitanWire, px, 0).xyz;
  float wire = texelFetch(uLowBitanWire, px, 0).w;

  if (uChannelIdx == 0) { // normalTangent
    vec3 tn = vec3(dot(hn, tu), dot(hn, bv) * (uFlipY > 0.5 ? -1.0 : 1.0), dot(hn, normalize(lnPart)));
    tn = normalize(tn) * 0.5 + 0.5;
    fragColor = vec4(tn, 1.0);
  } else if (uChannelIdx == 1) { // normalWorld
    fragColor = vec4(hn * 0.5 + 0.5, 1.0);
  } else if (uChannelIdx == 2) { // normalObject
    fragColor = vec4(normalize(lnPart) * 0.5 + 0.5, 1.0);
  } else if (uChannelIdx == 3) { // bentNormal
    vec3 bn = normalize(texelFetch(uBentNorm, px, 0).xyz * 2.0 - 1.0);
    vec3 btn = normalize(vec3(dot(bn, tu), dot(bn, bv) * (uFlipY > 0.5 ? -1.0 : 1.0), dot(bn, normalize(lnPart)))) * 0.5 + 0.5;
    fragColor = vec4(btn, 1.0);
  } else if (uChannelIdx == 4) { // bevelNormal
    vec3 avg = hn;
    float cnt = 1.0;
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        if (dx == 0 && dy == 0) continue;
        if (texelFetch(uLowPosCov, px + ivec2(dx, dy), 0).w > 0.5) {
          avg += normalize(texelFetch(uHighNormHeight, px + ivec2(dx, dy), 0).xyz * 2.0 - 1.0);
          cnt += 1.0;
        }
      }
    }
    avg = normalize(avg / cnt);
    vec3 bvt = normalize(vec3(dot(avg, tu), dot(avg, bv) * (uFlipY > 0.5 ? -1.0 : 1.0), dot(avg, normalize(lnPart)))) * 0.5 + 0.5;
    fragColor = vec4(bvt, 1.0);
  } else if (uChannelIdx == 5) { // ao
    fragColor = vec4(vec3(stats.r), 1.0);
  } else if (uChannelIdx == 6) { // secondaryAo
    fragColor = vec4(vec3(stats.g), 1.0);
  } else if (uChannelIdx == 7) { // curvature
    fragColor = vec4(vec3(calcCurvature(px, hn)), 1.0);
  } else if (uChannelIdx == 8) { // convexity
    float cv = calcCurvature(px, hn);
    fragColor = vec4(vec3(clamp((cv - 0.5) * 2.0, 0.0, 1.0)), 1.0);
  } else if (uChannelIdx == 9) { // concavity
    float cv = calcCurvature(px, hn);
    fragColor = vec4(vec3(clamp((0.5 - cv) * 2.0, 0.0, 1.0)), 1.0);
  } else if (uChannelIdx == 10) { // thickness
    fragColor = vec4(vec3(stats.b), 1.0);
  } else if (uChannelIdx == 11) { // height
    fragColor = vec4(vec3(hnH.a), 1.0);
  } else if (uChannelIdx == 12) { // topDust
    float dust = clamp(max(0.0, hn.y) * 0.72 + stats.r * 0.28, 0.0, 1.0);
    fragColor = vec4(vec3(dust), 1.0);
  } else if (uChannelIdx == 13) { // uvIslandId
    fragColor = vec4(paletteId(islandId), 1.0);
  } else if (uChannelIdx == 14) { // partId
    fragColor = vec4(paletteId(partId + 1.0), 1.0);
  } else if (uChannelIdx == 15) { // position
    vec3 np = clamp((lp.xyz - uBboxMin) / uBboxSize, 0.0, 1.0);
    fragColor = vec4(np, 1.0);
  } else { // wireframe
    vec3 wCol = wire > 0.5 ? vec3(0.937, 0.957, 0.812) : vec3(0.118, 0.125, 0.141);
    fragColor = vec4(wCol, 1.0);
  }
}
`,

  DILATE_FRAG: /* glsl */ `
precision highp float;
uniform sampler2D uSource;
in vec2 vUv;
out vec4 fragColor;
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec4 c = texelFetch(uSource, px, 0);
  if (c.a > 0.5) { fragColor = c; return; }
  vec4 sum = vec4(0.0);
  float cnt = 0.0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) continue;
      vec4 n = texelFetch(uSource, px + ivec2(dx, dy), 0);
      if (n.a > 0.5) { sum += n; cnt += 1.0; }
    }
  }
  fragColor = cnt > 0.0 ? vec4(sum.rgb / cnt, 1.0) : vec4(0.0);
}
`,

  SDF_SLICE_FRAG: /* glsl */ `
precision highp float;
precision highp int;
uniform sampler2D uTriDataTex;
uniform sampler2D uGridMetaTex;
uniform sampler2D uCellListTex;
uniform vec3 uMin;
uniform vec3 uSpan;
uniform vec3 uBboxMin;
uniform vec3 uBboxSize;
uniform vec3 uDims;
uniform int uSliceZ;
uniform int uSigned;
in vec2 vUv;
out vec4 fragColor;

vec4 fetchTriVec(int ti, int slot) {
  int lin = ti * 7 + slot;
  return texelFetch(uTriDataTex, ivec2(lin & 1023, lin >> 10), 0);
}
ivec2 fetchGridMeta(int cid) {
  vec4 m = texelFetch(uGridMetaTex, ivec2(cid & 63, cid >> 6), 0);
  return ivec2(int(m.x + 0.5), int(m.y + 0.5));
}
int fetchCellTri(int idx) {
  vec4 v = texelFetch(uCellListTex, ivec2(idx & 1023, idx >> 10), 0);
  return int(v.x + 0.5);
}

float pointTriDistSq(vec3 p, vec3 a, vec3 b, vec3 c) {
  vec3 ab = b - a, ac = c - a, ap = p - a;
  float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) return dot(ap, ap);
  vec3 bp = p - b;
  float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) return dot(bp, bp);
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
    float v = d1 / (d1 - d3);
    vec3 q = a + ab * v - p;
    return dot(q, q);
  }
  vec3 cp = p - c;
  float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) return dot(cp, cp);
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
    float w = d2 / (d2 - d6);
    vec3 q = a + ac * w - p;
    return dot(q, q);
  }
  float va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
    float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    vec3 q = b + (c - b) * w - p;
    return dot(q, q);
  }
  float denom = 1.0 / (va + vb + vc);
  float v = vb * denom, w = vc * denom;
  vec3 q = a + ab * v + ac * w - p;
  return dot(q, q);
}

float intersectX(vec3 ro, int ti) {
  vec3 a = fetchTriVec(ti, 0).xyz;
  vec3 b = fetchTriVec(ti, 1).xyz;
  vec3 c = fetchTriVec(ti, 2).xyz;
  vec3 e1 = b - a, e2 = c - a;
  vec3 rd = vec3(1.0, 0.0, 0.0);
  vec3 pv = cross(rd, e2);
  float det = dot(e1, pv);
  if (abs(det) < 1e-8) return -1.0;
  float inv = 1.0 / det;
  vec3 tv = ro - a;
  float u = dot(tv, pv) * inv;
  if (u < -0.0001 || u > 1.0001) return -1.0;
  vec3 qv = cross(tv, e1);
  float v = dot(rd, qv) * inv;
  if (v < -0.0001 || u + v > 1.0001) return -1.0;
  return dot(e2, qv) * inv;
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  ivec3 dims = ivec3(uDims + 0.5);
  if (px.x >= dims.x || px.y >= dims.y) { fragColor = vec4(0.0); return; }
  vec3 p = vec3(
    uMin.x + ((float(px.x) + 0.5) / float(dims.x)) * uSpan.x,
    uMin.y + ((float(px.y) + 0.5) / float(dims.y)) * uSpan.y,
    uMin.z + ((float(uSliceZ) + 0.5) / float(dims.z)) * uSpan.z
  );
  ivec3 cell = clamp(ivec3(floor(((p - uBboxMin) / uBboxSize) * 16.0)), ivec3(0), ivec3(15));
  float cellDiag = length(uBboxSize / 16.0);
  float bestD2 = 1e20;
  for (int rad = 0; rad <= 16; rad++) {
    int x0 = max(0, cell.x - rad), x1 = min(15, cell.x + rad);
    int y0 = max(0, cell.y - rad), y1 = min(15, cell.y + rad);
    int z0 = max(0, cell.z - rad), z1 = min(15, cell.z + rad);
    for (int z = z0; z <= z1; z++) {
      for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
          if (rad > 0 && max(abs(x - cell.x), max(abs(y - cell.y), abs(z - cell.z))) < rad) continue;
          int cid = (z * 16 + y) * 16 + x;
          ivec2 meta = fetchGridMeta(cid);
          for (int k = 0; k < 256; k++) {
            if (k >= meta.y) break;
            int ti = fetchCellTri(meta.x + k);
            vec3 a = fetchTriVec(ti, 0).xyz;
            vec3 b = fetchTriVec(ti, 1).xyz;
            vec3 c = fetchTriVec(ti, 2).xyz;
            bestD2 = min(bestD2, pointTriDistSq(p, a, b, c));
          }
        }
      }
    }
    if (bestD2 < float(rad * rad) * cellDiag * cellDiag) break;
  }
  float dist = sqrt(bestD2);
  if (uSigned != 0) {
    int hits = 0;
    float ox = p.x;
    float maxRay = uSpan.x + 1.0;
    for (int s = 0; s < 10; s++) {
      int cx0 = clamp(int(floor(((ox - uBboxMin.x) / uBboxSize.x) * 16.0)), 0, 15);
      float bestT = maxRay;
      for (int cx = cx0; cx <= 15; cx++) {
        int cid = (cell.z * 16 + cell.y) * 16 + cx;
        ivec2 meta = fetchGridMeta(cid);
        for (int k = 0; k < 256; k++) {
          if (k >= meta.y) break;
          int ti = fetchCellTri(meta.x + k);
          float t = intersectX(vec3(ox, p.y, p.z), ti);
          if (t > 0.0001 && t < bestT) bestT = t;
        }
      }
      if (bestT >= maxRay) break;
      hits++;
      ox += bestT + 0.002;
    }
    if ((hits & 1) == 1) dist = -dist;
  }
  uint bits = floatBitsToUint(dist);
  fragColor = vec4(
    float(bits & 255u),
    float((bits >> 8u) & 255u),
    float((bits >> 16u) & 255u),
    float((bits >> 24u) & 255u)
  ) / 255.0;
}
`,
};

const CHANNEL_INDEX_MAP = {
  normalTangent: 0,
  normalWorld: 1,
  normalObject: 2,
  bentNormal: 3,
  bevelNormal: 4,
  ao: 5,
  secondaryAo: 6,
  curvature: 7,
  convexity: 8,
  concavity: 9,
  thickness: 10,
  height: 11,
  topDust: 12,
  uvIslandId: 13,
  partId: 14,
  position: 15,
  wireframe: 16,
};

function makeFloatDataTex(arr, w, h) {
  const padded = new Float32Array(w * h * 4);
  padded.set(arr.subarray(0, padded.length));
  const tex = new THREE.DataTexture(padded, w, h, THREE.RGBAFormat, THREE.FloatType);
  tex.minFilter = THREE.NearestFilter;
  tex.magFilter = THREE.NearestFilter;
  tex.generateMipmaps = false;
  tex.flipY = false;
  tex.needsUpdate = true;
  return tex;
}

function makeScalarDataTex(typedArr, w, h) {
  const padded = new Float32Array(w * h * 4);
  for (let i = 0; i < typedArr.length; i++) padded[i * 4] = typedArr[i];
  const tex = new THREE.DataTexture(padded, w, h, THREE.RGBAFormat, THREE.FloatType);
  tex.minFilter = THREE.NearestFilter;
  tex.magFilter = THREE.NearestFilter;
  tex.generateMipmaps = false;
  tex.flipY = false;
  tex.needsUpdate = true;
  return tex;
}

function makePairDataTex(uint32Arr, w, h) {
  const padded = new Float32Array(w * h * 4);
  for (let i = 0; i < uint32Arr.length / 2; i++) {
    padded[i * 4] = uint32Arr[i * 2];
    padded[i * 4 + 1] = uint32Arr[i * 2 + 1];
  }
  const tex = new THREE.DataTexture(padded, w, h, THREE.RGBAFormat, THREE.FloatType);
  tex.minFilter = THREE.NearestFilter;
  tex.magFilter = THREE.NearestFilter;
  tex.generateMipmaps = false;
  tex.flipY = false;
  tex.needsUpdate = true;
  return tex;
}

function flipRowsRgba(buf, w, h) {
  const out = new Uint8ClampedArray(buf.length);
  const stride = w * 4;
  for (let y = 0; y < h; y++) {
    const srcOff = (h - 1 - y) * stride;
    const dstOff = y * stride;
    out.set(buf.subarray(srcOff, srcOff + stride), dstOff);
  }
  return out;
}

function flipRowsScalar(buf, w, h) {
  const out = new Uint8ClampedArray(w * h);
  for (let y = 0; y < h; y++) {
    const srcOff = (h - 1 - y) * w;
    const dstOff = y * w;
    out.set(buf.subarray(srcOff, srcOff + w), dstOff);
  }
  return out;
}

export class GpuBakeSession {
  constructor(renderer) {
    this.renderer = renderer;
    this.isWebGL2 = !!renderer?.capabilities?.isWebGL2;
    this.webgpuDevice = null;
    this.webgpuAdapterName = "";
    this.webgpuChecked = false;
    this.quadGeo = new THREE.PlaneGeometry(2, 2);
    this.quadScene = new THREE.Scene();
    this.quadCam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    this.quadMesh = new THREE.Mesh(this.quadGeo, new THREE.MeshBasicMaterial());
    this.quadScene.add(this.quadMesh);
  }

  async ensureWebGPU() {
    if (this.webgpuChecked) return this.webgpuDevice;
    this.webgpuChecked = true;
    if (typeof navigator === "undefined" || !navigator.gpu) return null;
    try {
      const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
      if (!adapter) return null;
      const device = await adapter.requestDevice();
      this.webgpuDevice = device;
      const info = adapter.info || {};
      this.webgpuAdapterName = [info.vendor, info.architecture, info.device, info.description].filter(Boolean).join(" ") || "WebGPU Compute";
    } catch {
      this.webgpuDevice = null;
    }
    return this.webgpuDevice;
  }

  _renderPass(material, target) {
    const prev = this.renderer.getRenderTarget();
    const prevAuto = this.renderer.autoClear;
    const prevScissor = this.renderer.getScissorTest?.() ?? false;
    this.renderer.autoClear = true;
    this.renderer.setScissorTest(false);
    this.quadMesh.material = material;
    this.renderer.setRenderTarget(target);
    this.renderer.clear(true, true, false);
    this.renderer.render(this.quadScene, this.quadCam);
    this.renderer.setRenderTarget(prev);
    this.renderer.setScissorTest(prevScissor);
    this.renderer.autoClear = prevAuto;
  }

  async bakeMeshMapsWebGL2(lowParts, highParts, settings = {}, onProgress = () => {}) {
    if (!this.isWebGL2) return null;
    const t0 = performance.now();
    const res = Math.max(128, Math.min(2048, Number(settings.resolution || 512)));
    const channels = (settings.channels || ["normalTangent", "ao", "curvature", "height"]).filter((c) => c !== "volumeSdf");
    const samples = Math.max(4, Math.min(48, Math.round(settings.samples || 12)));
    const frontDist = Number(settings.frontDistance ?? 0.14);
    const rearDist = Number(settings.rearDistance ?? 0.14);
    const aoDist = Number(settings.aoDistance ?? 0.45);
    const thicknessRange = Number(settings.thicknessRange ?? 2.0);
    const padding = Math.max(0, Math.min(16, Math.round(settings.padding ?? 6)));
    const flipNormalY = settings.normalY === "-Y";

    onProgress(0.08, "Packing High/Low BVH grid on GPU…");
    const packed = packBakeGeometryForGpu(lowParts, highParts);
    const gbuf = rasterizeLowMeshGBuffer(lowParts, res);

    const triH = Math.max(1, Math.ceil((Math.max(1, packed.triCount) * 7) / 1024));
    const listH = Math.max(1, Math.ceil(packed.cellList.length / 1024));

    const triTex = makeFloatDataTex(packed.triData, 1024, triH);
    const gridTex = makePairDataTex(packed.gridMeta, 64, 64);
    const listTex = makeScalarDataTex(packed.cellList, 1024, listH);

    const lowPosTex = makeFloatDataTex(gbuf.posCov, res, res);
    const lowNormTex = makeFloatDataTex(gbuf.normPart, res, res);
    const lowTangTex = makeFloatDataTex(gbuf.tangIsland, res, res);
    const lowBitanTex = makeFloatDataTex(gbuf.bitanWire, res, res);

    const rtOpts = {
      minFilter: THREE.NearestFilter,
      magFilter: THREE.NearestFilter,
      format: THREE.RGBAFormat,
      type: THREE.UnsignedByteType,
      depthBuffer: false,
      stencilBuffer: false,
    };

    const rtHighNorm = new THREE.WebGLRenderTarget(res, res, rtOpts);
    const rtStats = new THREE.WebGLRenderTarget(res, res, rtOpts);
    const rtBent = new THREE.WebGLRenderTarget(res, res, rtOpts);
    const rtOutA = new THREE.WebGLRenderTarget(res, res, rtOpts);
    const rtOutB = new THREE.WebGLRenderTarget(res, res, rtOpts);

    const coreUniforms = {
      uTriDataTex: { value: triTex },
      uGridMetaTex: { value: gridTex },
      uCellListTex: { value: listTex },
      uLowPosCov: { value: lowPosTex },
      uLowNormPart: { value: lowNormTex },
      uLowTangIsland: { value: lowTangTex },
      uLowBitanWire: { value: lowBitanTex },
      uBboxMin: { value: new THREE.Vector3(...packed.bboxMin) },
      uBboxSize: { value: new THREE.Vector3(...packed.bboxSize) },
      uRes: { value: res },
      uSamples: { value: samples },
      uFrontDist: { value: frontDist },
      uRearDist: { value: rearDist },
      uAoDist: { value: aoDist },
      uThickRange: { value: thicknessRange },
      uPassMode: { value: 0 },
    };

    const coreMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_GLSL3_VERT,
      fragmentShader: BAKE_GLSL3_SHADERS.CORE_RAY_FRAG,
      uniforms: coreUniforms,
      depthTest: false,
      depthWrite: false,
    });

    onProgress(0.25, "WebGL2 GPU · Raytracing high-poly projection…");
    coreUniforms.uPassMode.value = 0;
    this._renderPass(coreMat, rtHighNorm);

    onProgress(0.45, "WebGL2 GPU · Computing AO & thickness…");
    coreUniforms.uPassMode.value = 1;
    this._renderPass(coreMat, rtStats);

    if (channels.includes("bentNormal")) {
      coreUniforms.uPassMode.value = 2;
      this._renderPass(coreMat, rtBent);
    }

    const deriveMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_GLSL3_VERT,
      fragmentShader: BAKE_GLSL3_SHADERS.DERIVE_CHANNEL_FRAG,
      uniforms: {
        uHighNormHeight: { value: rtHighNorm.texture },
        uSurfaceStats: { value: rtStats.texture },
        uBentNorm: { value: rtBent.texture },
        uLowPosCov: { value: lowPosTex },
        uLowNormPart: { value: lowNormTex },
        uLowTangIsland: { value: lowTangTex },
        uLowBitanWire: { value: lowBitanTex },
        uBboxMin: { value: new THREE.Vector3(...packed.bboxMin) },
        uBboxSize: { value: new THREE.Vector3(...packed.bboxSize) },
        uChannelIdx: { value: 0 },
        uFlipY: { value: flipNormalY ? 1.0 : 0.0 },
      },
      depthTest: false,
      depthWrite: false,
    });

    const dilateMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_GLSL3_VERT,
      fragmentShader: BAKE_GLSL3_SHADERS.DILATE_FRAG,
      uniforms: { uSource: { value: rtOutA.texture } },
      depthTest: false,
      depthWrite: false,
    });

    const maps = {};
    const rawRead = new Uint8Array(res * res * 4);
    for (let ci = 0; ci < channels.length; ci++) {
      const ch = channels[ci];
      deriveMat.uniforms.uChannelIdx.value = CHANNEL_INDEX_MAP[ch] ?? 0;
      this._renderPass(deriveMat, rtOutA);

      let cur = rtOutA;
      let nxt = rtOutB;
      if (ch !== "wireframe") {
        for (let p = 0; p < padding; p++) {
          dilateMat.uniforms.uSource.value = cur.texture;
          this._renderPass(dilateMat, nxt);
          const tmp = cur; cur = nxt; nxt = tmp;
        }
      }

      this.renderer.readRenderTargetPixels(cur, 0, 0, res, res, rawRead);
      maps[ch] = new Uint8ClampedArray(rawRead);
      onProgress(0.55 + 0.42 * ((ci + 1) / Math.max(1, channels.length)), `WebGL2 GPU · Baked ${ch}`);
    }

    // Bridge AO + Curvature scalar arrays cleanly for Texture Paint
    const bridgeData = {
      res,
      ao: maps.ao ? new Uint8ClampedArray(res * res) : null,
      curvature: maps.curvature ? new Uint8ClampedArray(res * res) : null,
      normalRgba: maps.normalTangent || null,
    };
    if (maps.ao) for (let i = 0; i < res * res; i++) bridgeData.ao[i] = maps.ao[i * 4];
    if (maps.curvature) for (let i = 0; i < res * res; i++) bridgeData.curvature[i] = maps.curvature[i * 4];
    if (!bridgeData.ao) {
      this.renderer.readRenderTargetPixels(rtStats, 0, 0, res, res, rawRead);
      bridgeData.ao = new Uint8ClampedArray(res * res);
      for (let i = 0; i < res * res; i++) bridgeData.ao[i] = rawRead[i * 4];
    }
    if (!bridgeData.curvature) {
      bridgeData.curvature = new Uint8ClampedArray(res * res).fill(128);
    }

    triTex.dispose(); gridTex.dispose(); listTex.dispose();
    lowPosTex.dispose(); lowNormTex.dispose(); lowTangTex.dispose(); lowBitanTex.dispose();
    rtHighNorm.dispose(); rtStats.dispose(); rtBent.dispose(); rtOutA.dispose(); rtOutB.dispose();
    coreMat.dispose(); deriveMat.dispose(); dilateMat.dispose();

    const elapsedMs = Math.round(performance.now() - t0);
    onProgress(1, `WebGL2 GPU bake finished in ${elapsedMs} ms`);
    return { res, maps, bridgeData, deviceUsed: "WebGL2 GPU", elapsedMs };
  }

  async bakeVolumeSdfWebGL2(parts, options = {}, onProgress = () => {}) {
    if (!this.isWebGL2) return null;
    const t0 = performance.now();
    const nx = Math.max(12, Math.min(96, Math.round(options.nx || 40)));
    const ny = Math.max(12, Math.min(96, Math.round(options.ny || 40)));
    const nz = Math.max(12, Math.min(96, Math.round(options.nz || 40)));
    const padding = Math.max(0.02, Math.min(0.5, Number(options.padding ?? 0.16)));
    const signed = options.signMode !== "unsigned";

    const packed = packBakeGeometryForGpu(parts, parts);
    const padVec = [packed.bboxSize[0] * padding, packed.bboxSize[1] * padding, packed.bboxSize[2] * padding];
    const min = [packed.bboxMin[0] - padVec[0], packed.bboxMin[1] - padVec[1], packed.bboxMin[2] - padVec[2]];
    const span = [
      packed.bboxSize[0] + padVec[0] * 2,
      packed.bboxSize[1] + padVec[1] * 2,
      packed.bboxSize[2] + padVec[2] * 2,
    ];
    const max = [min[0] + span[0], min[1] + span[1], min[2] + span[2]];

    const triH = Math.max(1, Math.ceil((Math.max(1, packed.triCount) * 7) / 1024));
    const listH = Math.max(1, Math.ceil(packed.cellList.length / 1024));
    const triTex = makeFloatDataTex(packed.triData, 1024, triH);
    const gridTex = makePairDataTex(packed.gridMeta, 64, 64);
    const listTex = makeScalarDataTex(packed.cellList, 1024, listH);

    const rtSlice = new THREE.WebGLRenderTarget(nx, ny, {
      minFilter: THREE.NearestFilter,
      magFilter: THREE.NearestFilter,
      format: THREE.RGBAFormat,
      type: THREE.UnsignedByteType,
      depthBuffer: false,
      stencilBuffer: false,
    });

    const sdfMat = new THREE.ShaderMaterial({
      glslVersion: THREE.GLSL3,
      vertexShader: FULLSCREEN_GLSL3_VERT,
      fragmentShader: BAKE_GLSL3_SHADERS.SDF_SLICE_FRAG,
      uniforms: {
        uTriDataTex: { value: triTex },
        uGridMetaTex: { value: gridTex },
        uCellListTex: { value: listTex },
        uMin: { value: new THREE.Vector3(...min) },
        uSpan: { value: new THREE.Vector3(...span) },
        uBboxMin: { value: new THREE.Vector3(...packed.bboxMin) },
        uBboxSize: { value: new THREE.Vector3(...packed.bboxSize) },
        uDims: { value: new THREE.Vector3(nx, ny, nz) },
        uSliceZ: { value: 0 },
        uSigned: { value: signed ? 1 : 0 },
      },
      depthTest: false,
      depthWrite: false,
    });

    const values = new Float32Array(nx * ny * nz);
    const sliceBytes = new Uint8Array(nx * ny * 4);
    let minVal = Infinity, maxVal = -Infinity;

    for (let z = 0; z < nz; z++) {
      sdfMat.uniforms.uSliceZ.value = z;
      this._renderPass(sdfMat, rtSlice);
      this.renderer.readRenderTargetPixels(rtSlice, 0, 0, nx, ny, sliceBytes);
      const sliceFloats = new Float32Array(sliceBytes.buffer, sliceBytes.byteOffset, nx * ny);
      const zOff = z * ny * nx;
      for (let i = 0; i < nx * ny; i++) {
        const d = sliceFloats[i];
        values[zOff + i] = d;
        if (d < minVal) minVal = d;
        if (d > maxVal) maxVal = d;
      }
      if (z % 6 === 0) onProgress(0.1 + 0.85 * (z / nz), `WebGL2 GPU · Volume SDF slice ${z + 1}/${nz}`);
    }

    triTex.dispose(); gridTex.dispose(); listTex.dispose();
    rtSlice.dispose(); sdfMat.dispose();

    const elapsedMs = Math.round(performance.now() - t0);
    onProgress(1, `WebGL2 GPU SDF complete in ${elapsedMs} ms`);
    return {
      dims: [nx, ny, nz],
      min,
      max,
      minVal,
      maxVal,
      signed,
      values,
      deviceUsed: "WebGL2 GPU",
      elapsedMs,
    };
  }

  async bakeVolumeSdfWebGPU(parts, options = {}, onProgress = () => {}) {
    const dev = await this.ensureWebGPU();
    if (!dev) return null;
    try {
      const t0 = performance.now();
      const nx = Math.max(12, Math.min(96, Math.round(options.nx || 40)));
      const ny = Math.max(12, Math.min(96, Math.round(options.ny || 40)));
      const nz = Math.max(12, Math.min(96, Math.round(options.nz || 40)));
      const padding = Math.max(0.02, Math.min(0.5, Number(options.padding ?? 0.16)));
      const signed = options.signMode !== "unsigned";

      const packed = packBakeGeometryForGpu(parts, parts);
      const padVec = [packed.bboxSize[0] * padding, packed.bboxSize[1] * padding, packed.bboxSize[2] * padding];
      const min = [packed.bboxMin[0] - padVec[0], packed.bboxMin[1] - padVec[1], packed.bboxMin[2] - padVec[2]];
      const span = [
        packed.bboxSize[0] + padVec[0] * 2,
        packed.bboxSize[1] + padVec[1] * 2,
        packed.bboxSize[2] + padVec[2] * 2,
      ];
      const max = [min[0] + span[0], min[1] + span[1], min[2] + span[2]];

      const paramData = new ArrayBuffer(80);
      const u = new Uint32Array(paramData);
      const f = new Float32Array(paramData);
      u[0] = nx; u[1] = ny; u[2] = nz; u[3] = signed ? 1 : 0;
      f[4] = min[0]; f[5] = min[1]; f[6] = min[2];
      f[8] = span[0]; f[9] = span[1]; f[10] = span[2];
      f[12] = packed.bboxMin[0]; f[13] = packed.bboxMin[1]; f[14] = packed.bboxMin[2];
      f[16] = packed.bboxSize[0]; f[17] = packed.bboxSize[1]; f[18] = packed.bboxSize[2];

      const outSize = nx * ny * nz * 4;
      const makeBuf = (arr, usage) => {
        const byteLen = Math.max(16, (arr.byteLength + 15) & ~15);
        const b = dev.createBuffer({ size: byteLen, usage });
        dev.queue.writeBuffer(b, 0, arr);
        return b;
      };

      const pBuf = makeBuf(new Uint8Array(paramData), GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST);
      const triBuf = makeBuf(packed.triData, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const gmBuf = makeBuf(packed.gridMeta, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const clBuf = makeBuf(packed.cellList, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const outBuf = dev.createBuffer({ size: outSize, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
      const readBuf = dev.createBuffer({ size: outSize, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });

      if (!this._sdfPipeline) {
        this._sdfPipeline = dev.createComputePipeline({
          layout: "auto",
          compute: {
            module: dev.createShaderModule({ code: BAKE_WGSL_SHADERS.SDF_WGSL }),
            entryPoint: "main",
          },
        });
      }

      const bg = dev.createBindGroup({
        layout: this._sdfPipeline.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: pBuf } },
          { binding: 1, resource: { buffer: triBuf } },
          { binding: 2, resource: { buffer: gmBuf } },
          { binding: 3, resource: { buffer: clBuf } },
          { binding: 4, resource: { buffer: outBuf } },
        ],
      });

      onProgress(0.35, "WebGPU Compute · Dispatching 3D SDF kernel…");
      const enc = dev.createCommandEncoder();
      const pass = enc.beginComputePass();
      pass.setPipeline(this._sdfPipeline);
      pass.setBindGroup(0, bg);
      pass.dispatchWorkgroups(Math.ceil(nx / 4), Math.ceil(ny / 4), Math.ceil(nz / 4));
      pass.end();
      enc.copyBufferToBuffer(outBuf, 0, readBuf, 0, outSize);
      dev.queue.submit([enc.finish()]);
      await readBuf.mapAsync(GPUMapMode.READ);
      const values = new Float32Array(readBuf.getMappedRange()).slice();
      readBuf.unmap();
      pBuf.destroy(); triBuf.destroy(); gmBuf.destroy(); clBuf.destroy(); outBuf.destroy(); readBuf.destroy();

      let minVal = Infinity, maxVal = -Infinity;
      for (let i = 0; i < values.length; i++) {
        if (values[i] < minVal) minVal = values[i];
        if (values[i] > maxVal) maxVal = values[i];
      }
      const elapsedMs = Math.round(performance.now() - t0);
      onProgress(1, `WebGPU Compute SDF complete in ${elapsedMs} ms`);
      return {
        dims: [nx, ny, nz],
        min,
        max,
        minVal,
        maxVal,
        signed,
        values,
        deviceUsed: `WebGPU (${this.webgpuAdapterName})`,
        elapsedMs,
      };
    } catch {
      return null;
    }
  }

  async bakeMeshMapsWebGPU(lowParts, highParts, settings = {}, onProgress = () => {}) {
    const dev = await this.ensureWebGPU();
    if (!dev) return null;
    try {
      const t0 = performance.now();
      const res = Math.max(128, Math.min(2048, Number(settings.resolution || 512)));
      const channels = (settings.channels || ["normalTangent", "ao", "curvature", "height"]).filter((c) => c !== "volumeSdf");
      const samples = Math.max(4, Math.min(48, Math.round(settings.samples || 12)));
      const frontDist = Number(settings.frontDistance ?? 0.14);
      const rearDist = Number(settings.rearDistance ?? 0.14);
      const aoDist = Number(settings.aoDistance ?? 0.45);
      const thicknessRange = Number(settings.thicknessRange ?? 2.0);
      const padding = Math.max(0, Math.min(16, Math.round(settings.padding ?? 6)));
      const flipNormalY = settings.normalY === "-Y";

      onProgress(0.08, "WebGPU Compute · Packing High/Low spatial grid…");
      const packed = packBakeGeometryForGpu(lowParts, highParts);
      const gbuf = rasterizeLowMeshGBuffer(lowParts, res);

      // G-buffer is already in top-down canvas UV order; pass directly to WebGPU compute
      const posTop = gbuf.posCov;
      const normTop = gbuf.normPart;
      const tangTop = gbuf.tangIsland;
      const bitanTop = gbuf.bitanWire;

      const paramData = new ArrayBuffer(64);
      const pu = new Uint32Array(paramData);
      const pf = new Float32Array(paramData);
      pu[0] = res; pu[1] = samples; pu[2] = packed.triCount; pu[3] = flipNormalY ? 1 : 0;
      pf[4] = frontDist; pf[5] = rearDist; pf[6] = aoDist; pf[7] = thicknessRange;
      pf[8] = packed.bboxMin[0]; pf[9] = packed.bboxMin[1]; pf[10] = packed.bboxMin[2];
      pf[12] = packed.bboxSize[0]; pf[13] = packed.bboxSize[1]; pf[14] = packed.bboxSize[2];

      const makeBuf = (arr, usage) => {
        const byteLen = Math.max(16, (arr.byteLength + 15) & ~15);
        const b = dev.createBuffer({ size: byteLen, usage });
        dev.queue.writeBuffer(b, 0, arr);
        return b;
      };

      const nPix = res * res;
      const vec4Bytes = nPix * 16;
      const pBuf = makeBuf(new Uint8Array(paramData), GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST);
      const triBuf = makeBuf(packed.triData, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const gmBuf = makeBuf(packed.gridMeta, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const clBuf = makeBuf(packed.cellList, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const lpBuf = makeBuf(posTop, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const lnBuf = makeBuf(normTop, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const ltBuf = makeBuf(tangTop, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
      const lbBuf = makeBuf(bitanTop, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);

      const outHN = dev.createBuffer({ size: vec4Bytes, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
      const outSA = dev.createBuffer({ size: vec4Bytes, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
      const outBN = dev.createBuffer({ size: vec4Bytes, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });

      const rHN = dev.createBuffer({ size: vec4Bytes, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const rSA = dev.createBuffer({ size: vec4Bytes, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
      const rBN = dev.createBuffer({ size: vec4Bytes, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });

      if (!this._meshPipeline) {
        this._meshPipeline = dev.createComputePipeline({
          layout: "auto",
          compute: {
            module: dev.createShaderModule({ code: BAKE_WGSL_SHADERS.MESH_RAY_WGSL }),
            entryPoint: "main",
          },
        });
      }

      const bg = dev.createBindGroup({
        layout: this._meshPipeline.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: pBuf } },
          { binding: 1, resource: { buffer: triBuf } },
          { binding: 2, resource: { buffer: gmBuf } },
          { binding: 3, resource: { buffer: clBuf } },
          { binding: 4, resource: { buffer: lpBuf } },
          { binding: 5, resource: { buffer: lnBuf } },
          { binding: 6, resource: { buffer: ltBuf } },
          { binding: 7, resource: { buffer: lbBuf } },
          { binding: 8, resource: { buffer: outHN } },
          { binding: 9, resource: { buffer: outSA } },
          { binding: 10, resource: { buffer: outBN } },
        ],
      });

      onProgress(0.32, "WebGPU Compute · Dispatching parallel mesh raytracer…");
      const enc = dev.createCommandEncoder();
      const pass = enc.beginComputePass();
      pass.setPipeline(this._meshPipeline);
      pass.setBindGroup(0, bg);
      pass.dispatchWorkgroups(Math.ceil(res / 8), Math.ceil(res / 8), 1);
      pass.end();
      enc.copyBufferToBuffer(outHN, 0, rHN, 0, vec4Bytes);
      enc.copyBufferToBuffer(outSA, 0, rSA, 0, vec4Bytes);
      enc.copyBufferToBuffer(outBN, 0, rBN, 0, vec4Bytes);
      dev.queue.submit([enc.finish()]);

      await Promise.all([rHN.mapAsync(GPUMapMode.READ), rSA.mapAsync(GPUMapMode.READ), rBN.mapAsync(GPUMapMode.READ)]);
      const hnArr = new Float32Array(rHN.getMappedRange()).slice();
      const saArr = new Float32Array(rSA.getMappedRange()).slice();
      const bnArr = new Float32Array(rBN.getMappedRange()).slice();
      rHN.unmap(); rSA.unmap(); rBN.unmap();
      pBuf.destroy(); triBuf.destroy(); gmBuf.destroy(); clBuf.destroy();
      lpBuf.destroy(); lnBuf.destroy(); ltBuf.destroy(); lbBuf.destroy();
      outHN.destroy(); outSA.destroy(); outBN.destroy();
      rHN.destroy(); rSA.destroy(); rBN.destroy();

      // Reconstruct requested RGBA buffers & curvature/dilation cleanly from GPU ray results
      const cov = new Uint8Array(nPix);
      const maps = {};
      for (const ch of channels) maps[ch] = new Uint8ClampedArray(nPix * 4);
      const rawAo = new Uint8ClampedArray(nPix);
      const rawCurv = new Uint8ClampedArray(nPix).fill(128);
      const signY = flipNormalY ? -1 : 1;

      const writeRgb = (ch, i4, r, g, b) => {
        const buf = maps[ch];
        if (!buf) return;
        buf[i4] = Math.max(0, Math.min(255, Math.round(r * 255)));
        buf[i4 + 1] = Math.max(0, Math.min(255, Math.round(g * 255)));
        buf[i4 + 2] = Math.max(0, Math.min(255, Math.round(b * 255)));
        buf[i4 + 3] = 255;
      };

      const hueRgb = (id) => {
        const c = new THREE.Color().setHSL(((id * 0.61803398875 + 0.13) % 1 + 1) % 1, 0.58, 0.56);
        return [c.r, c.g, c.b];
      };

      for (let py = 0; py < res; py++) {
        for (let px = 0; px < res; px++) {
          const pid = py * res + px;
          const o = pid * 4;
          // MESH_RAY_WGSL flipped Y when writing out buffers (row = res - 1 - gid.y)
          // while posTop/normTop were top-down directly: index consistently by pid!
          if (posTop[o + 3] < 0.5) continue;
          cov[pid] = 1;
          const hn = [hnArr[o], hnArr[o + 1], hnArr[o + 2]];
          const ln = [normTop[o], normTop[o + 1], normTop[o + 2]];
          const tu = [tangTop[o], tangTop[o + 1], tangTop[o + 2]];
          const bv = [bitanTop[o], bitanTop[o + 1], bitanTop[o + 2]];
          const ao = saArr[o], secAo = saArr[o + 1], hNorm = saArr[o + 2], thick = saArr[o + 3];
          rawAo[pid] = Math.round(ao * 255);

          writeRgb("normalTangent", o, (hn[0]*tu[0]+hn[1]*tu[1]+hn[2]*tu[2])*0.5+0.5, (hn[0]*bv[0]+hn[1]*bv[1]+hn[2]*bv[2])*signY*0.5+0.5, (hn[0]*ln[0]+hn[1]*ln[1]+hn[2]*ln[2])*0.5+0.5);
          writeRgb("normalWorld", o, hn[0]*0.5+0.5, hn[1]*0.5+0.5, hn[2]*0.5+0.5);
          writeRgb("normalObject", o, ln[0]*0.5+0.5, ln[1]*0.5+0.5, ln[2]*0.5+0.5);
          const bn = [bnArr[o], bnArr[o + 1], bnArr[o + 2]];
          writeRgb("bentNormal", o, (bn[0]*tu[0]+bn[1]*tu[1]+bn[2]*tu[2])*0.5+0.5, (bn[0]*bv[0]+bn[1]*bv[1]+bn[2]*bv[2])*signY*0.5+0.5, (bn[0]*ln[0]+bn[1]*ln[1]+bn[2]*ln[2])*0.5+0.5);
          writeRgb("ao", o, ao, ao, ao);
          writeRgb("secondaryAo", o, secAo, secAo, secAo);
          writeRgb("thickness", o, thick, thick, thick);
          writeRgb("height", o, hNorm, hNorm, hNorm);
          const dust = Math.max(0, Math.min(1, Math.max(0, hn[1]) * 0.72 + ao * 0.28));
          writeRgb("topDust", o, dust, dust, dust);
          if (maps.uvIslandId) { const [r, g, b] = hueRgb(tangTop[o + 3]); writeRgb("uvIslandId", o, r, g, b); }
          if (maps.partId) { const [r, g, b] = hueRgb(normTop[o + 3] + 1); writeRgb("partId", o, r, g, b); }
          if (maps.position) writeRgb("position", o, (posTop[o]-packed.bboxMin[0])/packed.bboxSize[0], (posTop[o+1]-packed.bboxMin[1])/packed.bboxSize[1], (posTop[o+2]-packed.bboxMin[2])/packed.bboxSize[2]);
          if (maps.wireframe) {
            const w = bitanTop[o + 3] > 0.5;
            writeRgb("wireframe", w ? 0.937 : 0.118, w ? 0.957 : 0.125, w ? 0.812 : 0.141);
          }
        }
      }

      // Curvature + Bevel normal neighbor pass
      for (let py = 1; py < res - 1; py++) {
        for (let px = 1; px < res - 1; px++) {
          const pid = py * res + px;
          if (!cov[pid] || !cov[pid - 1] || !cov[pid + 1] || !cov[pid - res] || !cov[pid + res]) continue;
          const oL = (pid - 1) * 4, oR = (pid + 1) * 4, oU = (pid - res) * 4, oD = (pid + res) * 4, o = pid * 4;
          const dpx = [posTop[oR]-posTop[oL], posTop[oR+1]-posTop[oL+1], posTop[oR+2]-posTop[oL+2]];
          const dpy = [posTop[oD]-posTop[oU], posTop[oD+1]-posTop[oU+1], posTop[oD+2]-posTop[oU+2]];
          const dnx = [hnArr[oR]-hnArr[oL], hnArr[oR+1]-hnArr[oL+1], hnArr[oR+2]-hnArr[oL+2]];
          const dny = [hnArr[oD]-hnArr[oU], hnArr[oD+1]-hnArr[oU+1], hnArr[oD+2]-hnArr[oU+2]];
          const lx2 = Math.max(1e-6, dpx[0]*dpx[0]+dpx[1]*dpx[1]+dpx[2]*dpx[2]);
          const ly2 = Math.max(1e-6, dpy[0]*dpy[0]+dpy[1]*dpy[1]+dpy[2]*dpy[2]);
          const cv = Math.max(0, Math.min(1, 0.5 + ((dnx[0]*dpx[0]+dnx[1]*dpx[1]+dnx[2]*dpx[2])/lx2 + (dny[0]*dpy[0]+dny[1]*dpy[1]+dny[2]*dpy[2])/ly2) * 0.08));
          rawCurv[pid] = Math.round(cv * 255);
          writeRgb("curvature", o, cv, cv, cv);
          writeRgb("convexity", o, Math.max(0, Math.min(1, (cv - 0.5) * 2)), Math.max(0, Math.min(1, (cv - 0.5) * 2)), Math.max(0, Math.min(1, (cv - 0.5) * 2)));
          writeRgb("concavity", o, Math.max(0, Math.min(1, (0.5 - cv) * 2)), Math.max(0, Math.min(1, (0.5 - cv) * 2)), Math.max(0, Math.min(1, (0.5 - cv) * 2)));
          if (maps.bevelNormal) {
            let bx = (hnArr[oL]+hnArr[oR]+hnArr[oU]+hnArr[oD]+hnArr[o]*2)/6;
            let by = (hnArr[oL+1]+hnArr[oR+1]+hnArr[oU+1]+hnArr[oD+1]+hnArr[o+1]*2)/6;
            let bz = (hnArr[oL+2]+hnArr[oR+2]+hnArr[oU+2]+hnArr[oD+2]+hnArr[o+2]*2)/6;
            const bl = Math.hypot(bx, by, bz) || 1; bx /= bl; by /= bl; bz /= bl;
            const ln = [normTop[o], normTop[o + 1], normTop[o + 2]];
            const tu = [tangTop[o], tangTop[o + 1], tangTop[o + 2]];
            const bv = [bitanTop[o], bitanTop[o + 1], bitanTop[o + 2]];
            writeRgb("bevelNormal", o, (bx*tu[0]+by*tu[1]+bz*tu[2])*0.5+0.5, (bx*bv[0]+by*bv[1]+bz*bv[2])*signY*0.5+0.5, (bx*ln[0]+by*ln[1]+bz*ln[2])*0.5+0.5);
          }
        }
      }

      // Edge padding dilation pass
      if (padding > 0) {
        const dirs = [[1,0],[-1,0],[0,1],[0,-1],[1,1],[-1,1],[1,-1],[-1,-1]];
        for (const key of Object.keys(maps)) {
          if (key === "wireframe") continue;
          const msk = new Uint8Array(cov);
          const buf = maps[key];
          for (let passIdx = 0; passIdx < padding; passIdx++) {
            const writes = [];
            for (let y = 0; y < res; y++) {
              for (let x = 0; x < res; x++) {
                const pid = y * res + x;
                if (msk[pid]) continue;
                let r = 0, g = 0, b = 0, c = 0;
                for (const [dx, dy] of dirs) {
                  const nx = x + dx, ny = y + dy;
                  if (nx < 0 || nx >= res || ny < 0 || ny >= res) continue;
                  const nid = ny * res + nx;
                  if (!msk[nid]) continue;
                  r += buf[nid*4]; g += buf[nid*4+1]; b += buf[nid*4+2]; c++;
                }
                if (c > 0) writes.push([pid, Math.round(r/c), Math.round(g/c), Math.round(b/c)]);
              }
            }
            for (const [pid, r, g, b] of writes) {
              msk[pid] = 1; buf[pid*4] = r; buf[pid*4+1] = g; buf[pid*4+2] = b; buf[pid*4+3] = 255;
            }
          }
        }
      }

      const elapsedMs = Math.round(performance.now() - t0);
      onProgress(1, `WebGPU Compute bake complete in ${elapsedMs} ms`);
      return {
        res,
        maps,
        bridgeData: { res, ao: rawAo, curvature: rawCurv, normalRgba: maps.normalTangent || null },
        deviceUsed: `WebGPU (${this.webgpuAdapterName})`,
        elapsedMs,
      };
    } catch {
      return null;
    }
  }

  dispose() {
    this.quadGeo.dispose();
    this.quadMesh.material.dispose();
  }
}
