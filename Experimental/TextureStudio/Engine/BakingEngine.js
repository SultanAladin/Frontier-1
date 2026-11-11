/*==========================================================================================================================================
                                                            BAKINGENGINE.JS
==========================================================================================================================================*/
/* Clean High/Low Mesh Map Baker (Tangent/World/Object/Bent/Bevel Normals, AO, Curvature, Convexity, Concavity, Thickness, Height, Dust, IDs)
   and 3D Float32 Signed Distance Field (Volume SDF) baker with spatial AABB acceleration, edge dilation padding, and Texture Paint Bridge. */

import * as THREE from "three";
import { buildPaintTargetGeometry } from "./TexturePaintEngine.js";
import { RoundedBoxGeometry } from "../Vendor/addons/RoundedBoxGeometry.js";

export const BAKE_MAP_CATALOG = [
  { id: "volumeSdf", label: "Volume SDF", short: "3D distance grid", group: "Volume & Distance", isVolume: true },
  { id: "normalTangent", label: "Tangent Normal", short: "High-to-Low normal", group: "Normals" },
  { id: "normalWorld", label: "World Normal", short: "Scene XYZ vector", group: "Normals" },
  { id: "normalObject", label: "Object Normal", short: "Local XYZ vector", group: "Normals" },
  { id: "bentNormal", label: "Bent Normal", short: "Average unoccluded ray", group: "Normals" },
  { id: "bevelNormal", label: "Bevel Normal", short: "Rounded edge filter", group: "Normals" },
  { id: "ao", label: "Ambient Occlusion", short: "Hemisphere sky AO", group: "Surface" },
  { id: "secondaryAo", label: "Secondary AO", short: "Tight contact creases", group: "Surface" },
  { id: "curvature", label: "Curvature", short: "Ridge & cavity signed", group: "Surface" },
  { id: "convexity", label: "Convexity", short: "Positive ridges", group: "Surface" },
  { id: "concavity", label: "Concavity", short: "Recessed grooves", group: "Surface" },
  { id: "thickness", label: "Thickness", short: "Interior ray depth", group: "Surface" },
  { id: "height", label: "Height / Displacement", short: "Cage ray offset", group: "Surface" },
  { id: "topDust", label: "Top-Down Dust", short: "Directional mask", group: "Masks & IDs" },
  { id: "uvIslandId", label: "UV Island ID", short: "Chart color map", group: "Masks & IDs" },
  { id: "partId", label: "Part / Material ID", short: "Sub-mesh ID color", group: "Masks & IDs" },
  { id: "position", label: "World Position", short: "Normalized XYZ bbox", group: "Masks & IDs" },
  { id: "wireframe", label: "UV Wireframe", short: "Edge triangle overlay", group: "Masks & IDs" },
];

export const BAKE_STUDY_SHAPES = [
  { id: "teapot", label: "Utah Teapot (High/Low)" },
  { id: "shaderball", label: "Shader Ball (High/Low)" },
  { id: "box", label: "Chamfer Box (High/Low)" },
  { id: "sphere", label: "UV Sphere (High/Low)" },
  { id: "torus", label: "Torus Knot Study" },
];

function clamp(v, lo = 0, hi = 1) {
  return Math.max(lo, Math.min(hi, v));
}

function tick() {
  return new Promise((r) => setTimeout(r, 0));
}

/**
 * Create paired Low/High geometries reusing TexturePaint target UVs so baked maps attach cleanly back to Texture Paint!
 */
export function createBakeStudyPair(studyId = "teapot") {
  if (studyId === "torus") {
    const low = new THREE.TorusGeometry(0.9, 0.36, 18, 36);
    const high = new THREE.TorusGeometry(0.9, 0.36, 48, 96);
    // Add crisp helical ribbing detail on high-poly so Tangent Normal / Height / Curvature bake visually pops!
    const hp = high.attributes.position;
    const hn = high.attributes.normal;
    for (let i = 0; i < hp.count; i++) {
      const x = hp.getX(i), y = hp.getY(i), z = hp.getZ(i);
      const ang = Math.atan2(y, x);
      const rib = Math.sin(ang * 16 + z * 10) * 0.028;
      hp.setXYZ(i, x + hn.getX(i) * rib, y + hn.getY(i) * rib, z + hn.getZ(i) * rib);
    }
    high.computeVertexNormals();
    high.computeBoundingBox();
    low.computeBoundingBox();
    return {
      studyId,
      low: [{ name: "Torus_low", geometry: low }],
      high: [{ name: "Torus_high", geometry: high }],
    };
  }

  if (studyId === "box") {
    const low = buildPaintTargetGeometry("box");
    const high = new RoundedBoxGeometry(1.94, 1.94, 1.94, 10, 0.22);
    // Emboss subtle recessed square panel per face on high-poly for crisp normal/AO/height contrast
    const pos = high.attributes.position;
    for (let i = 0; i < pos.count; i++) {
      const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
      const ax = Math.abs(x), ay = Math.abs(y), az = Math.abs(z);
      const rim = Math.max(ax, ay, az);
      const inner = Math.min(ax, ay, az);
      if (rim > 0.93 && inner < 0.56) {
        const s = 0.955;
        pos.setXYZ(i, ax === rim ? x * s : x, ay === rim ? y * s : y, az === rim ? z * s : z);
      }
    }
    high.computeVertexNormals();
    return {
      studyId,
      low: [{ name: "Box_low", geometry: low }],
      high: [{ name: "Box_high", geometry: high }],
    };
  }

  if (studyId === "sphere") {
    const low = buildPaintTargetGeometry("sphere");
    const high = new THREE.SphereGeometry(1.22, 72, 54);
    const hp = high.attributes.position;
    const hn = high.attributes.normal;
    for (let i = 0; i < hp.count; i++) {
      const x = hp.getX(i), y = hp.getY(i), z = hp.getZ(i);
      const d = (Math.sin(x * 7) * Math.cos(y * 7) + Math.sin(z * 9)) * 0.02;
      hp.setXYZ(i, x + hn.getX(i) * d, y + hn.getY(i) * d, z + hn.getZ(i) * d);
    }
    high.computeVertexNormals();
    return {
      studyId,
      low: [{ name: "Sphere_low", geometry: low }],
      high: [{ name: "Sphere_high", geometry: high }],
    };
  }

  // Teapot or Shader Ball
  const low = buildPaintTargetGeometry(studyId);
  const high = buildPaintTargetGeometry(studyId);
  // Subtle micro-relief detail on high mesh so High->Low normals & height show clear crisp ridges
  const hp = high.attributes.position;
  const hn = high.attributes.normal;
  for (let i = 0; i < hp.count; i++) {
    const x = hp.getX(i), y = hp.getY(i), z = hp.getZ(i);
    const band = Math.sin(y * 18) * 0.012;
    hp.setXYZ(i, x + hn.getX(i) * band, y + hn.getY(i) * band, z + hn.getZ(i) * band);
  }
  high.computeVertexNormals();
  return {
    studyId,
    low: [{ name: `${studyId}_low`, geometry: low }],
    high: [{ name: `${studyId}_high`, geometry: high }],
  };
}

/**
 * Spatial Grid acceleration structure over high-poly triangles for fast raycasting and point-distance queries.
 */
export function buildSpatialAccel(parts) {
  const tris = [];
  const bbox = new THREE.Box3();

  for (let pIdx = 0; pIdx < parts.length; pIdx++) {
    const g = parts[pIdx].geometry;
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
        partName: parts[pIdx].name,
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

  const pad = 0.02;
  bbox.min.subScalar(pad);
  bbox.max.addScalar(pad);
  const size = bbox.getSize(new THREE.Vector3());
  const gridDim = 16;
  const cells = new Array(gridDim * gridDim * gridDim);

  const cellOf = (x, y, z) => [
    Math.min(gridDim - 1, Math.max(0, Math.floor(((x - bbox.min.x) / Math.max(1e-5, size.x)) * gridDim))),
    Math.min(gridDim - 1, Math.max(0, Math.floor(((y - bbox.min.y) / Math.max(1e-5, size.y)) * gridDim))),
    Math.min(gridDim - 1, Math.max(0, Math.floor(((z - bbox.min.z) / Math.max(1e-5, size.z)) * gridDim))),
  ];

  for (let ti = 0; ti < tris.length; ti++) {
    const tr = tris[ti];
    const [x0, y0, z0] = cellOf(tr.min[0], tr.min[1], tr.min[2]);
    const [x1, y1, z1] = cellOf(tr.max[0], tr.max[1], tr.max[2]);
    for (let z = z0; z <= z1; z++) {
      for (let y = y0; y <= y1; y++) {
        for (let x = x0; x <= x1; x++) {
          const cid = (z * gridDim + y) * gridDim + x;
          if (!cells[cid]) cells[cid] = [];
          cells[cid].push(ti);
        }
      }
    }
  }

  return { tris, bbox, size, gridDim, cells };
}

function intersectTri(ox, oy, oz, dx, dy, dz, tr) {
  const e1x = tr.b[0] - tr.a[0], e1y = tr.b[1] - tr.a[1], e1z = tr.b[2] - tr.a[2];
  const e2x = tr.c[0] - tr.a[0], e2y = tr.c[1] - tr.a[1], e2z = tr.c[2] - tr.a[2];
  const px = dy * e2z - dz * e2y;
  const py = dz * e2x - dx * e2z;
  const pz = dx * e2y - dy * e2x;
  const det = e1x * px + e1y * py + e1z * pz;
  if (Math.abs(det) < 1e-8) return null;
  const inv = 1 / det;
  const tx = ox - tr.a[0], ty = oy - tr.a[1], tz = oz - tr.a[2];
  const u = (tx * px + ty * py + tz * pz) * inv;
  if (u < -0.0001 || u > 1.0001) return null;
  const qx = ty * e1z - tz * e1y;
  const qy = tz * e1x - tx * e1z;
  const qz = tx * e1y - ty * e1x;
  const v = (dx * qx + dy * qy + dz * qz) * inv;
  if (v < -0.0001 || u + v > 1.0001) return null;
  const t = (e2x * qx + e2y * qy + e2z * qz) * inv;
  return { t, u, v, w: 1 - u - v, tr };
}

function raycastAccel(accel, ox, oy, oz, dx, dy, dz, maxDist, twoSided = true) {
  let best = null;
  let bestT = maxDist;
  // Walk cell bounds overlapped by segment [origin .. origin + dir * maxDist]
  const ex = ox + dx * maxDist, ey = oy + dy * maxDist, ez = oz + dz * maxDist;
  const minX = Math.min(ox, ex), maxX = Math.max(ox, ex);
  const minY = Math.min(oy, ey), maxY = Math.max(oy, ey);
  const minZ = Math.min(oz, ez), maxZ = Math.max(oz, ez);

  const g = accel.gridDim;
  const bMin = accel.bbox.min, sz = accel.size;
  const ix0 = Math.max(0, Math.floor(((minX - bMin.x) / Math.max(1e-5, sz.x)) * g));
  const ix1 = Math.min(g - 1, Math.floor(((maxX - bMin.x) / Math.max(1e-5, sz.x)) * g));
  const iy0 = Math.max(0, Math.floor(((minY - bMin.y) / Math.max(1e-5, sz.y)) * g));
  const iy1 = Math.min(g - 1, Math.floor(((maxY - bMin.y) / Math.max(1e-5, sz.y)) * g));
  const iz0 = Math.max(0, Math.floor(((minZ - bMin.z) / Math.max(1e-5, sz.z)) * g));
  const iz1 = Math.min(g - 1, Math.floor(((maxZ - bMin.z) / Math.max(1e-5, sz.z)) * g));

  const seen = new Set();
  for (let z = iz0; z <= iz1; z++) {
    for (let y = iy0; y <= iy1; y++) {
      for (let x = ix0; x <= ix1; x++) {
        const list = accel.cells[(z * g + y) * g + x];
        if (!list) continue;
        for (let k = 0; k < list.length; k++) {
          const ti = list[k];
          if (seen.has(ti)) continue;
          seen.add(ti);
          const tr = accel.tris[ti];
          if (!twoSided && (dx * tr.fn[0] + dy * tr.fn[1] + dz * tr.fn[2]) > 0) continue;
          const hit = intersectTri(ox, oy, oz, dx, dy, dz, tr);
          if (hit && hit.t > 0.0001 && hit.t < bestT) {
            bestT = hit.t;
            best = hit;
          }
        }
      }
    }
  }
  return best;
}

function pointTriangleDistSq(px, py, pz, a, b, c) {
  const abx = b[0] - a[0], aby = b[1] - a[1], abz = b[2] - a[2];
  const acx = c[0] - a[0], acy = c[1] - a[1], acz = c[2] - a[2];
  const apx = px - a[0], apy = py - a[1], apz = pz - a[2];
  const d1 = abx * apx + aby * apy + abz * apz;
  const d2 = acx * apx + acy * apy + acz * apz;
  if (d1 <= 0 && d2 <= 0) return apx * apx + apy * apy + apz * apz;

  const bpx = px - b[0], bpy = py - b[1], bpz = pz - b[2];
  const d3 = abx * bpx + aby * bpy + abz * bpz;
  const d4 = acx * bpx + acy * bpy + acz * bpz;
  if (d3 >= 0 && d4 <= d3) return bpx * bpx + bpy * bpy + bpz * bpz;

  const vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    const v = d1 / (d1 - d3);
    const dx = a[0] + abx * v - px, dy = a[1] + aby * v - py, dz = a[2] + abz * v - pz;
    return dx * dx + dy * dy + dz * dz;
  }

  const cpx = px - c[0], cpy = py - c[1], cpz = pz - c[2];
  const d5 = abx * cpx + aby * cpy + abz * cpz;
  const d6 = acx * cpx + acy * cpy + acz * cpz;
  if (d6 >= 0 && d5 <= d6) return cpx * cpx + cpy * cpy + cpz * cpz;

  const vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    const w = d2 / (d2 - d6);
    const dx = a[0] + acx * w - px, dy = a[1] + acy * w - py, dz = a[2] + acz * w - pz;
    return dx * dx + dy * dy + dz * dz;
  }

  const va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    const dx = b[0] + (c[0] - b[0]) * w - px;
    const dy = b[1] + (c[1] - b[1]) * w - py;
    const dz = b[2] + (c[2] - b[2]) * w - pz;
    return dx * dx + dy * dy + dz * dz;
  }

  const denom = 1 / (va + vb + vc);
  const v = vb * denom, w = vc * denom;
  const dx = a[0] + abx * v + acx * w - px;
  const dy = a[1] + aby * v + acy * w - py;
  const dz = a[2] + abz * v + acz * w - pz;
  return dx * dx + dy * dy + dz * dz;
}

function closestDistanceAccel(accel, px, py, pz) {
  let bestD2 = Infinity;
  // Progressive ring search inside grid
  const g = accel.gridDim;
  const bMin = accel.bbox.min, sz = accel.size;
  const cx = Math.min(g - 1, Math.max(0, Math.floor(((px - bMin.x) / Math.max(1e-5, sz.x)) * g)));
  const cy = Math.min(g - 1, Math.max(0, Math.floor(((py - bMin.y) / Math.max(1e-5, sz.y)) * g)));
  const cz = Math.min(g - 1, Math.max(0, Math.floor(((pz - bMin.z) / Math.max(1e-5, sz.z)) * g)));
  const cellDiag = Math.hypot(sz.x / g, sz.y / g, sz.z / g);

  const visited = new Set();
  for (let rad = 0; rad <= g; rad++) {
    const x0 = Math.max(0, cx - rad), x1 = Math.min(g - 1, cx + rad);
    const y0 = Math.max(0, cy - rad), y1 = Math.min(g - 1, cy + rad);
    const z0 = Math.max(0, cz - rad), z1 = Math.min(g - 1, cz + rad);
    for (let z = z0; z <= z1; z++) {
      for (let y = y0; y <= y1; y++) {
        for (let x = x0; x <= x1; x++) {
          if (rad > 0 && Math.max(Math.abs(x - cx), Math.abs(y - cy), Math.abs(z - cz)) < rad) continue;
          const list = accel.cells[(z * g + y) * g + x];
          if (!list) continue;
          for (let k = 0; k < list.length; k++) {
            const ti = list[k];
            if (visited.has(ti)) continue;
            visited.add(ti);
            const tr = accel.tris[ti];
            const d2 = pointTriangleDistSq(px, py, pz, tr.a, tr.b, tr.c);
            if (d2 < bestD2) bestD2 = d2;
          }
        }
      }
    }
    if (bestD2 < (rad * cellDiag) ** 2) break;
  }
  return Math.sqrt(bestD2);
}

/**
 * Bake a 3D Float32 Signed Distance Field (Volume SDF) over the source mesh parts.
 */
export async function bakeVolumeSdf(parts, options = {}, onProgress = () => {}) {
  const nx = Math.max(12, Math.min(96, Math.round(options.nx || 40)));
  const ny = Math.max(12, Math.min(96, Math.round(options.ny || 40)));
  const nz = Math.max(12, Math.min(96, Math.round(options.nz || 40)));
  const padding = clamp(Number(options.padding ?? 0.16), 0.02, 0.5);
  const signed = options.signMode !== "unsigned";

  const accel = buildSpatialAccel(parts);
  const size = accel.bbox.getSize(new THREE.Vector3());
  const padVec = size.clone().multiplyScalar(padding);
  const min = accel.bbox.min.clone().sub(padVec);
  const max = accel.bbox.max.clone().add(padVec);
  const span = max.clone().sub(min);

  const values = new Float32Array(nx * ny * nz);
  let minVal = Infinity, maxVal = -Infinity;

  for (let z = 0; z < nz; z++) {
    const wz = min.z + ((z + 0.5) / nz) * span.z;
    for (let y = 0; y < ny; y++) {
      const wy = min.y + ((y + 0.5) / ny) * span.y;
      for (let x = 0; x < nx; x++) {
        const wx = min.x + ((x + 0.5) / nx) * span.x;
        let dist = closestDistanceAccel(accel, wx, wy, wz);

        if (signed) {
          // Parity raycast along +X and +Y for robust interior classification
          let hitsX = 0;
          const maxRay = span.x + 1.0;
          let ox = wx;
          for (let step = 0; step < 12; step++) {
            const h = raycastAccel(accel, ox, wy, wz, 1, 0, 0, maxRay, true);
            if (!h) break;
            hitsX++;
            ox += h.t + 0.002;
          }
          if (hitsX % 2 === 1) dist = -dist;
        }

        const idx = (z * ny + y) * nx + x;
        values[idx] = dist;
        if (dist < minVal) minVal = dist;
        if (dist > maxVal) maxVal = dist;
      }
    }
    if (z % 4 === 0) {
      onProgress(0.1 + 0.9 * (z / nz), `Baking Volume SDF slice ${z + 1}/${nz}`);
      await tick();
    }
  }

  onProgress(1, "Volume SDF complete");
  return {
    dims: [nx, ny, nz],
    min: [min.x, min.y, min.z],
    max: [max.x, max.y, max.z],
    minVal,
    maxVal,
    signed,
    values,
  };
}

/**
 * Dilate baked RGBA texels into uncovered gutter pixels to eliminate seam artifacts.
 */
function dilateBakedRgba(rgba, mask, res, paddingPx = 6) {
  const curMask = new Uint8Array(mask);
  const nextRgba = new Uint8ClampedArray(rgba);
  const dirs = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [-1, 1], [1, -1], [-1, -1]];

  for (let pass = 0; pass < paddingPx; pass++) {
    const writes = [];
    for (let y = 0; y < res; y++) {
      for (let x = 0; x < res; x++) {
        const pid = y * res + x;
        if (curMask[pid]) continue;
        let r = 0, g = 0, b = 0, a = 0, cnt = 0;
        for (const [dx, dy] of dirs) {
          const nx = x + dx, ny = y + dy;
          if (nx < 0 || nx >= res || ny < 0 || ny >= res) continue;
          const nid = ny * res + nx;
          if (!curMask[nid]) continue;
          r += nextRgba[nid * 4];
          g += nextRgba[nid * 4 + 1];
          b += nextRgba[nid * 4 + 2];
          a += nextRgba[nid * 4 + 3];
          cnt++;
        }
        if (cnt > 0) {
          writes.push([pid, Math.round(r / cnt), Math.round(g / cnt), Math.round(b / cnt), Math.round(a / cnt)]);
        }
      }
    }
    if (!writes.length) break;
    for (const [pid, r, g, b, a] of writes) {
      curMask[pid] = 1;
      nextRgba[pid * 4] = r;
      nextRgba[pid * 4 + 1] = g;
      nextRgba[pid * 4 + 2] = b;
      nextRgba[pid * 4 + 3] = a;
    }
  }
  return nextRgba;
}

function colorPaletteId(id) {
  const hue = ((id * 0.61803398875 + 0.13) % 1 + 1) % 1;
  const col = new THREE.Color().setHSL(hue, 0.58, 0.56);
  return [Math.round(col.r * 255), Math.round(col.g * 255), Math.round(col.b * 255)];
}

/**
 * Bake selected 2D UV Mesh Maps from Low/High assemblies.
 */
export async function bakeMeshMaps(lowParts, highParts, settings = {}, onProgress = () => {}) {
  const res = Math.max(128, Math.min(2048, Number(settings.resolution || 512)));
  const channels = settings.channels || ["normalTangent", "ao", "curvature", "height"];
  const samples = Math.max(4, Math.min(48, Math.round(settings.samples || 12)));
  const frontDist = Number(settings.frontDistance ?? 0.14);
  const rearDist = Number(settings.rearDistance ?? 0.14);
  const aoDist = Number(settings.aoDistance ?? 0.45);
  const thicknessRange = Number(settings.thicknessRange ?? 2.0);
  const padding = Math.max(0, Math.min(16, Math.round(settings.padding ?? 6)));
  const flipNormalY = settings.normalY === "-Y";

  const accel = buildSpatialAccel(highParts);
  const bboxSize = accel.bbox.getSize(new THREE.Vector3());
  const n = res * res;

  // Rasterize Low mesh triangles into UV space
  const coverage = new Uint8Array(n);
  const worldPos = new Float32Array(n * 3);
  const lowNorm = new Float32Array(n * 3);
  const tangentU = new Float32Array(n * 3);
  const bitangentV = new Float32Array(n * 3);
  const islandIds = new Uint32Array(n);
  const partIds = new Uint16Array(n);
  const edgeWire = new Uint8Array(n);

  let nextIsland = 1;
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
      const x0 = u0 * res, y0 = (1 - v0) * res;
      const x1 = u1 * res, y1 = (1 - v1) * res;
      const x2 = u2 * res, y2 = (1 - v2) * res;

      const denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
      if (Math.abs(denom) < 1e-6) continue;

      // Tangent frame per triangle
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
          coverage[pid] = 1;
          if (w0 < 0.045 || w1 < 0.045 || w2 < 0.045) edgeWire[pid] = 1;

          worldPos[pid * 3] = w0 * p0[0] + w1 * p1[0] + w2 * p2[0];
          worldPos[pid * 3 + 1] = w0 * p0[1] + w1 * p1[1] + w2 * p2[1];
          worldPos[pid * 3 + 2] = w0 * p0[2] + w1 * p1[2] + w2 * p2[2];

          let nx = w0 * norm.getX(i0) + w1 * norm.getX(i1) + w2 * norm.getX(i2);
          let ny = w0 * norm.getY(i0) + w1 * norm.getY(i1) + w2 * norm.getY(i2);
          let nz = w0 * norm.getZ(i0) + w1 * norm.getZ(i1) + w2 * norm.getZ(i2);
          const nl = Math.hypot(nx, ny, nz) || 1;
          lowNorm[pid * 3] = nx / nl;
          lowNorm[pid * 3 + 1] = ny / nl;
          lowNorm[pid * 3 + 2] = nz / nl;
          tangentU[pid * 3] = tx; tangentU[pid * 3 + 1] = ty; tangentU[pid * 3 + 2] = tz;
          bitangentV[pid * 3] = bx; bitangentV[pid * 3 + 1] = by; bitangentV[pid * 3 + 2] = bz;
          partIds[pid] = partAttr ? Math.round(partAttr.getX(i0)) : pIdx;
          islandIds[pid] = Math.floor(u0 * 4) + Math.floor(v0 * 4) * 4 + 1 + pIdx * 16;
        }
      }
      nextIsland++;
    }
  }

  const buffers = {};
  for (const ch of channels) {
    if (ch !== "volumeSdf") buffers[ch] = new Uint8ClampedArray(n * 4);
  }
  // Track raw height/AO/curvature floats for Paint bridge & normal bevel filtering
  const rawWorldN = new Float32Array(n * 3);
  const rawAo = new Float32Array(n).fill(1);
  const rawHeight = new Float32Array(n);

  const needRays = channels.some((c) => ["normalTangent", "normalWorld", "normalObject", "bentNormal", "bevelNormal", "ao", "secondaryAo", "thickness", "height", "curvature", "convexity", "concavity"].includes(c));
  const needAo = channels.some((c) => ["ao", "secondaryAo", "bentNormal"].includes(c));
  const needThick = channels.includes("thickness");

  // Fibonacci hemisphere sample vectors
  const hemiDirs = [];
  for (let s = 0; s < samples; s++) {
    const phi = Math.PI * (3 - Math.sqrt(5)) * s;
    const z = clamp(1 - (s + 0.5) / samples, 0.05, 1);
    const r = Math.sqrt(Math.max(0, 1 - z * z));
    hemiDirs.push([Math.cos(phi) * r, Math.sin(phi) * r, z]);
  }

  for (let y = 0; y < res; y++) {
    for (let x = 0; x < res; x++) {
      const pid = y * res + x;
      if (!coverage[pid]) continue;

      const px = worldPos[pid * 3], py = worldPos[pid * 3 + 1], pz = worldPos[pid * 3 + 2];
      const lnx = lowNorm[pid * 3], lny = lowNorm[pid * 3 + 1], lnz = lowNorm[pid * 3 + 2];

      let hnx = lnx, hny = lny, hnz = lnz;
      let hitOffset = 0;

      if (needRays) {
        const rayStartX = px + lnx * frontDist;
        const rayStartY = py + lny * frontDist;
        const rayStartZ = pz + lnz * frontDist;
        const hit = raycastAccel(accel, rayStartX, rayStartY, rayStartZ, -lnx, -lny, -lnz, frontDist + rearDist, false);
        if (hit) {
          hitOffset = frontDist - hit.t;
          hnx = hit.w * hit.tr.na[0] + hit.u * hit.tr.nb[0] + hit.v * hit.tr.nc[0];
          hny = hit.w * hit.tr.na[1] + hit.u * hit.tr.nb[1] + hit.v * hit.tr.nc[1];
          hnz = hit.w * hit.tr.na[2] + hit.u * hit.tr.nb[2] + hit.v * hit.tr.nc[2];
          const hl = Math.hypot(hnx, hny, hnz) || 1;
          hnx /= hl; hny /= hl; hnz /= hl;
        }
      }
      rawWorldN[pid * 3] = hnx;
      rawWorldN[pid * 3 + 1] = hny;
      rawWorldN[pid * 3 + 2] = hnz;
      rawHeight[pid] = hitOffset;

      let aoVal = 1.0, secAoVal = 1.0;
      let bnx = hnx, bny = hny, bnz = hnz;
      if (needAo) {
        const up = Math.abs(hnz) < 0.9 ? [0, 0, 1] : [1, 0, 0];
        let ux = up[1] * hnz - up[2] * hny;
        let uy = up[2] * hnx - up[0] * hnz;
        let uz = up[0] * hny - up[1] * hnx;
        const ul = Math.hypot(ux, uy, uz) || 1;
        ux /= ul; uy /= ul; uz /= ul;
        const vx = hny * uz - hnz * uy, vy = hnz * ux - hnx * uz, vz = hnx * uy - hny * ux;

        let occ = 0, secOcc = 0;
        let sumBx = 0, sumBy = 0, sumBz = 0;
        for (let s = 0; s < hemiDirs.length; s++) {
          const d = hemiDirs[s];
          const rdx = ux * d[0] + vx * d[1] + hnx * d[2];
          const rdy = uy * d[0] + vy * d[1] + hny * d[2];
          const rdz = uz * d[0] + vz * d[1] + hnz * d[2];
          const h = raycastAccel(accel, px + hnx * 0.008, py + hny * 0.008, pz + hnz * 0.008, rdx, rdy, rdz, aoDist, true);
          if (h) {
            occ++;
            if (h.t < aoDist * 0.32) secOcc++;
          } else {
            sumBx += rdx; sumBy += rdy; sumBz += rdz;
          }
        }
        aoVal = clamp(1 - occ / samples);
        secAoVal = clamp(1 - secOcc / samples);
        const bl = Math.hypot(sumBx, sumBy, sumBz);
        if (bl > 1e-4) {
          bnx = sumBx / bl; bny = sumBy / bl; bnz = sumBz / bl;
        }
      }
      rawAo[pid] = aoVal;

      let thickVal = 1.0;
      if (needThick) {
        const h = raycastAccel(accel, px - lnx * 0.01, py - lny * 0.01, pz - lnz * 0.01, -lnx, -lny, -lnz, thicknessRange, true);
        thickVal = h ? clamp(h.t / thicknessRange) : 1.0;
      }

      const i4 = pid * 4;
      const writeRgb = (id, r, g, b) => {
        if (!buffers[id]) return;
        buffers[id][i4] = Math.round(clamp(r, 0, 1) * 255);
        buffers[id][i4 + 1] = Math.round(clamp(g, 0, 1) * 255);
        buffers[id][i4 + 2] = Math.round(clamp(b, 0, 1) * 255);
        buffers[id][i4 + 3] = 255;
      };

      const tx = tangentU[pid * 3], ty = tangentU[pid * 3 + 1], tz = tangentU[pid * 3 + 2];
      const bx = bitangentV[pid * 3], by = bitangentV[pid * 3 + 1], bz = bitangentV[pid * 3 + 2];

      if (buffers.normalTangent) {
        const tnx = hnx * tx + hny * ty + hnz * tz;
        const tny = (hnx * bx + hny * by + hnz * bz) * (flipNormalY ? -1 : 1);
        const tnz = hnx * lnx + hny * lny + hnz * lnz;
        writeRgb("normalTangent", tnx * 0.5 + 0.5, tny * 0.5 + 0.5, tnz * 0.5 + 0.5);
      }
      if (buffers.normalWorld) writeRgb("normalWorld", hnx * 0.5 + 0.5, hny * 0.5 + 0.5, hnz * 0.5 + 0.5);
      if (buffers.normalObject) writeRgb("normalObject", hnx * 0.5 + 0.5, hny * 0.5 + 0.5, hnz * 0.5 + 0.5);
      if (buffers.bentNormal) {
        const tbx = bnx * tx + bny * ty + bnz * tz;
        const tby = (bnx * bx + bny * by + bnz * bz) * (flipNormalY ? -1 : 1);
        const tbz = bnx * lnx + bny * lny + bnz * lnz;
        writeRgb("bentNormal", tbx * 0.5 + 0.5, tby * 0.5 + 0.5, tbz * 0.5 + 0.5);
      }
      if (buffers.ao) writeRgb("ao", aoVal, aoVal, aoVal);
      if (buffers.secondaryAo) writeRgb("secondaryAo", secAoVal, secAoVal, secAoVal);
      if (buffers.thickness) writeRgb("thickness", thickVal, thickVal, thickVal);
      if (buffers.height) {
        const hv = clamp(0.5 + hitOffset / Math.max(0.01, frontDist + rearDist));
        writeRgb("height", hv, hv, hv);
      }
      if (buffers.topDust) {
        const d = clamp(hny * 0.5 + 0.5);
        writeRgb("topDust", d, d, d);
      }
      if (buffers.uvIslandId) {
        const [r, g, b] = colorPaletteId(islandIds[pid]);
        buffers.uvIslandId[i4] = r; buffers.uvIslandId[i4 + 1] = g; buffers.uvIslandId[i4 + 2] = b; buffers.uvIslandId[i4 + 3] = 255;
      }
      if (buffers.partId) {
        const [r, g, b] = colorPaletteId(partIds[pid] + 1);
        buffers.partId[i4] = r; buffers.partId[i4 + 1] = g; buffers.partId[i4 + 2] = b; buffers.partId[i4 + 3] = 255;
      }
      if (buffers.position) {
        writeRgb(
          "position",
          (px - accel.bbox.min.x) / Math.max(1e-4, bboxSize.x),
          (py - accel.bbox.min.y) / Math.max(1e-4, bboxSize.y),
          (pz - accel.bbox.min.z) / Math.max(1e-4, bboxSize.z),
        );
      }
      if (buffers.wireframe) {
        const w = edgeWire[pid] ? 1 : 0.08;
        writeRgb("wireframe", w * 0.9, w * 0.94, w * 0.8);
      }
    }

    if (y % 24 === 0) {
      onProgress(0.08 + 0.8 * (y / res), `Baking row ${y + 1}/${res}`);
      await tick();
    }
  }

  // Neighborhood curvature / convexity / concavity / bevel normal pass
  const needCurv = channels.some((c) => ["curvature", "convexity", "concavity", "bevelNormal"].includes(c));
  const rawCurvature = new Float32Array(n).fill(0.5);
  if (needCurv || channels.includes("ao")) {
    for (let y = 1; y < res - 1; y++) {
      for (let x = 1; x < res - 1; x++) {
        const pid = y * res + x;
        if (!coverage[pid]) continue;
        // Estimate curvature from normal divergence across UV neighbors
        const nxL = rawWorldN[(pid - 1) * 3], nyL = rawWorldN[(pid - 1) * 3 + 1], nzL = rawWorldN[(pid - 1) * 3 + 2];
        const nxR = rawWorldN[(pid + 1) * 3], nyR = rawWorldN[(pid + 1) * 3 + 1], nzR = rawWorldN[(pid + 1) * 3 + 2];
        const nxD = rawWorldN[(pid - res) * 3], nyD = rawWorldN[(pid - res) * 3 + 1], nzD = rawWorldN[(pid - res) * 3 + 2];
        const nxU = rawWorldN[(pid + res) * 3], nyU = rawWorldN[(pid + res) * 3 + 1], nzU = rawWorldN[(pid + res) * 3 + 2];

        const pxL = worldPos[(pid - 1) * 3], pyL = worldPos[(pid - 1) * 3 + 1], pzL = worldPos[(pid - 1) * 3 + 2];
        const pxR = worldPos[(pid + 1) * 3], pyR = worldPos[(pid + 1) * 3 + 1], pzR = worldPos[(pid + 1) * 3 + 2];
        const pxD = worldPos[(pid - res) * 3], pyD = worldPos[(pid - res) * 3 + 1], pzD = worldPos[(pid - res) * 3 + 2];
        const pxU = worldPos[(pid + res) * 3], pyU = worldPos[(pid + res) * 3 + 1], pzU = worldPos[(pid + res) * 3 + 2];

        const dpx = [pxR - pxL, pyR - pyL, pzR - pzL];
        const dpy = [pxU - pxD, pyU - pyD, pzU - pzD];
        const dnx = [nxR - nxL, nyR - nyL, nzR - nzL];
        const dny = [nxU - nxD, nyU - nyD, nzU - nzD];

        const lenX = Math.hypot(...dpx) || 1;
        const lenY = Math.hypot(...dpy) || 1;
        const k1 = (dnx[0] * dpx[0] + dnx[1] * dpx[1] + dnx[2] * dpx[2]) / (lenX * lenX);
        const k2 = (dny[0] * dpy[0] + dny[1] * dpy[1] + dny[2] * dpy[2]) / (lenY * lenY);
        const curv = clamp(0.5 + (k1 + k2) * 0.08);
        rawCurvature[pid] = curv;
        const i4 = pid * 4;

        if (buffers.curvature) {
          const c = Math.round(curv * 255);
          buffers.curvature[i4] = c; buffers.curvature[i4 + 1] = c; buffers.curvature[i4 + 2] = c; buffers.curvature[i4 + 3] = 255;
        }
        if (buffers.convexity) {
          const v = Math.round(clamp((curv - 0.5) * 2) * 255);
          buffers.convexity[i4] = v; buffers.convexity[i4 + 1] = v; buffers.convexity[i4 + 2] = v; buffers.convexity[i4 + 3] = 255;
        }
        if (buffers.concavity) {
          const v = Math.round(clamp((0.5 - curv) * 2) * 255);
          buffers.concavity[i4] = v; buffers.concavity[i4 + 1] = v; buffers.concavity[i4 + 2] = v; buffers.concavity[i4 + 3] = 255;
        }
        if (buffers.bevelNormal) {
          let bx = (nxL + nxR + nxD + nxU + rawWorldN[pid * 3] * 2) / 6;
          let by = (nyL + nyR + nyD + nyU + rawWorldN[pid * 3 + 1] * 2) / 6;
          let bz = (nzL + nzR + nzD + nzU + rawWorldN[pid * 3 + 2] * 2) / 6;
          const bl = Math.hypot(bx, by, bz) || 1;
          bx /= bl; by /= bl; bz /= bl;
          const tx = tangentU[pid * 3], ty = tangentU[pid * 3 + 1], tz = tangentU[pid * 3 + 2];
          const btx = bitangentV[pid * 3], bty = bitangentV[pid * 3 + 1], btz = bitangentV[pid * 3 + 2];
          const lnx = lowNorm[pid * 3], lny = lowNorm[pid * 3 + 1], lnz = lowNorm[pid * 3 + 2];
          const tnx = bx * tx + by * ty + bz * tz;
          const tny = (bx * btx + by * bty + bz * btz) * (flipNormalY ? -1 : 1);
          const tnz = bx * lnx + by * lny + bz * lnz;
          buffers.bevelNormal[i4] = Math.round((tnx * 0.5 + 0.5) * 255);
          buffers.bevelNormal[i4 + 1] = Math.round((tny * 0.5 + 0.5) * 255);
          buffers.bevelNormal[i4 + 2] = Math.round((tnz * 0.5 + 0.5) * 255);
          buffers.bevelNormal[i4 + 3] = 255;
        }
      }
    }
  }

  // Dilate every baked 2D map across UV borders
  if (padding > 0) {
    for (const key of Object.keys(buffers)) {
      buffers[key] = dilateBakedRgba(buffers[key], coverage, res, padding);
    }
  }

  onProgress(1, "Mesh maps baked");
  return {
    res,
    maps: buffers,
    bridgeData: {
      res,
      ao: rawAo,
      curvature: rawCurvature,
      worldNormal: rawWorldN,
      normalRgba: buffers.normalTangent || null,
    },
  };
}
