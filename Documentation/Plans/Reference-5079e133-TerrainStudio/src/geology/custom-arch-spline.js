/**
 * User-Drawn 3D Spline Arch & Custom Curve Volumetric SDF Assembler.
 *
 * Allows interactive placement and 3D XYZ-gizmo manipulation of arbitrary 3D control points
 * `[{ x, y, z, r }, ...]` (supports multiple branches/curves!) and synthesizes a true
 * QEF Dual-Contoured 3D Volumetric SDF Boulder Arch along the exact spline.
 */

import {
  DEG2RAD,
  buildStrikeDipFrame,
  vnorm,
  vcross,
  vdot,
  clamp,
} from '../core/vec3.js';
import { scalarField3D } from '../core/spatial-noise.js';
import { compilePolyBlock } from './polyhedron.js';
import {
  buildBoulderArchSpanFromStartToFinish,
  sculptRockTypeBoulder,
} from './arch-curves.js';

export const DEFAULT_CUSTOM_ARCH_CURVES = [
  [
    { x: -7.8, y: 0.0, z: -0.6, r: 1.25 },
    { x: -5.4, y: 5.2, z:  0.4, r: 1.05 },
    { x: -0.2, y: 8.6, z:  0.2, r: 0.88 },
    { x:  5.2, y: 5.4, z: -0.5, r: 1.05 },
    { x:  7.8, y: 0.0, z:  0.5, r: 1.22 },
  ],
];

/**
 * Evaluates a smooth 3D Catmull-Rom spline passing directly through all `points` (`N >= 2`)
 * at normalized parameter `t in [0, 1]`, returning `{ x, y, z, r, tx, ty, tz }`.
 */
export function evalCatmullRom3D(points, t) {
  const n = points.length;
  if (n === 0) return { x: 0, y: 0, z: 0, r: 1, tx: 1, ty: 0, tz: 0 };
  if (n === 1) {
    const p = points[0];
    return { x: p.x, y: p.y, z: p.z, r: p.r ?? 1, tx: 1, ty: 0, tz: 0 };
  }

  const tc = clamp(t, 0, 1);
  const scaled = tc * (n - 1);
  const seg = Math.min(n - 2, Math.floor(scaled));
  const u = scaled - seg;

  const getPt = (idx) => {
    if (idx < 0) {
      return {
        x: 2 * points[0].x - points[1].x,
        y: 2 * points[0].y - points[1].y,
        z: 2 * points[0].z - points[1].z,
        r: points[0].r ?? 1,
      };
    }
    if (idx >= n) {
      return {
        x: 2 * points[n - 1].x - points[n - 2].x,
        y: 2 * points[n - 1].y - points[n - 2].y,
        z: 2 * points[n - 1].z - points[n - 2].z,
        r: points[n - 1].r ?? 1,
      };
    }
    const p = points[idx];
    return { x: p.x, y: p.y, z: p.z, r: p.r ?? 1 };
  };

  const p0 = getPt(seg - 1);
  const p1 = getPt(seg);
  const p2 = getPt(seg + 1);
  const p3 = getPt(seg + 2);

  const u2 = u * u;
  const u3 = u2 * u;

  const catmull = (v0, v1, v2, v3) =>
    0.5 * (
      (2 * v1) +
      (-v0 + v2) * u +
      (2 * v0 - 5 * v1 + 4 * v2 - v3) * u2 +
      (-v0 + 3 * v1 - 3 * v2 + v3) * u3
    );

  const catmullDeriv = (v0, v1, v2, v3) =>
    0.5 * (
      (-v0 + v2) +
      2 * (2 * v0 - 5 * v1 + 4 * v2 - v3) * u +
      3 * (-v0 + 3 * v1 - 3 * v2 + v3) * u2
    );

  const x = catmull(p0.x, p1.x, p2.x, p3.x);
  const y = catmull(p0.y, p1.y, p2.y, p3.y);
  const z = catmull(p0.z, p1.z, p2.z, p3.z);
  const r = clamp(catmull(p0.r, p1.r, p2.r, p3.r), 0.35, 2.8);

  const dx = catmullDeriv(p0.x, p1.x, p2.x, p3.x);
  const dy = catmullDeriv(p0.y, p1.y, p2.y, p3.y);
  const dz = catmullDeriv(p0.z, p1.z, p2.z, p3.z);
  const dLen = Math.hypot(dx, dy, dz) || 1e-4;

  return {
    x,
    y,
    z,
    r,
    tx: dx / dLen,
    ty: dy / dLen,
    tz: dz / dLen,
  };
}

/**
 * Compiles an arbitrary 3D user Catmull-Rom spline into the internal structural `CurveRib`
 * representation consumed by `evalCurvedArchRib` and `buildBoulderArchSpanFromStartToFinish`.
 */
export function compileUserSplineRib(points, beddingFrame, rng, opts = {}) {
  const {
    M = 64,
    baseRadius = 0.42,
    edgeBevel = 0.038,
    smoothUnionR = 0.05,
    isEolian = true,
    crookedAmp = 0.35,
  } = opts;

  const px = new Float32Array(M);
  const py = new Float32Array(M);
  const pz = new Float32Array(M);
  const tx = new Float32Array(M - 1);
  const ty = new Float32Array(M - 1);
  const tz = new Float32Array(M - 1);
  const segLen = new Float32Array(M - 1);
  const nx = new Float32Array(M);
  const ny = new Float32Array(M);
  const nz = new Float32Array(M);
  const bx = new Float32Array(M);
  const by = new Float32Array(M);
  const bz = new Float32Array(M);
  const rIn = new Float32Array(M);
  const rOut = new Float32Array(M);

  const waveSalt = rng.int(1, 99999);
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;

  // Parallel-transport frame initialization so arbitrary 3D curves never twist or flip!
  let prevTan = null;
  let prevB = null;

  for (let k = 0; k < M; k++) {
    const t = k / (M - 1);
    const st = evalCatmullRom3D(points, t);
    const tan = [st.tx, st.ty, st.tz];

    let depB;
    if (!prevTan) {
      let refB = vcross([0, 1, 0], tan);
      if (Math.hypot(refB[0], refB[1], refB[2]) < 1e-3) {
        refB = vcross([0, 0, 1], tan);
      }
      depB = vnorm(refB);
    } else {
      // Project previous binormal perpendicular to new tangent (Rotation Minimizing Frame)
      const proj = vdot(prevB, tan);
      const cand = [
        prevB[0] - tan[0] * proj,
        prevB[1] - tan[1] * proj,
        prevB[2] - tan[2] * proj,
      ];
      depB = Math.hypot(cand[0], cand[1], cand[2]) > 1e-4 ? vnorm(cand) : prevB;
    }
    const extN = vnorm(vcross(tan, depB));
    prevTan = tan;
    prevB = depB;

    const midMask = Math.sin(Math.PI * t);
    const jogN = scalarField3D(t * 3.1, 0.7, 1.4, waveSalt + 13) * 0.22 * midMask * crookedAmp;
    const jogB = scalarField3D(t * 2.7, 2.4, 0.6, waveSalt + 71) * 0.26 * midMask * crookedAmp;

    const wx = st.x + extN[0] * jogN + depB[0] * jogB;
    const wy = st.y + extN[1] * jogN + depB[1] * jogB;
    const wz = st.z + extN[2] * jogN + depB[2] * jogB;

    const ri = baseRadius * st.r;
    const ro = baseRadius * 1.12 * st.r;

    px[k] = wx; py[k] = wy; pz[k] = wz;
    nx[k] = extN[0]; ny[k] = extN[1]; nz[k] = extN[2];
    bx[k] = depB[0]; by[k] = depB[1]; bz[k] = depB[2];
    rIn[k] = ri;
    rOut[k] = ro;

    const pad = Math.max(ri, ro) * 1.4 + edgeBevel;
    if (wx - pad < minX) minX = wx - pad;
    if (wy - pad < minY) minY = wy - pad;
    if (wz - pad < minZ) minZ = wz - pad;
    if (wx + pad > maxX) maxX = wx + pad;
    if (wy + pad > maxY) maxY = wy + pad;
    if (wz + pad > maxZ) maxZ = wz + pad;
  }

  let arcLength = 0;
  for (let k = 0; k < M - 1; k++) {
    const dx = px[k + 1] - px[k];
    const dy = py[k + 1] - py[k];
    const dz = pz[k + 1] - pz[k];
    const len = Math.hypot(dx, dy, dz) || 1e-4;
    tx[k] = dx / len;
    ty[k] = dy / len;
    tz[k] = dz / len;
    segLen[k] = len;
    arcLength += len;
  }

  const cx = 0.5 * (minX + maxX);
  const cy = 0.5 * (minY + maxY);
  const cz = 0.5 * (minZ + maxZ);
  const radius = 0.5 * Math.hypot(maxX - minX, maxY - minY, maxZ - minZ);

  return {
    isCurveRib: true,
    arcLength,
    M,
    px, py, pz,
    tx, ty, tz, segLen,
    nx, ny, nz,
    bx, by, bz,
    rIn, rOut,
    finTaper: 1.0,
    edgeBevel,
    smoothUnionR,
    detailScale: 1.0,
    hardness: 0.78,
    isEolian,
    spallSalt: waveSalt + 129,
    seedSalt: rng.int(1, 999999),
    fx0: beddingFrame.strikeVec[0], fx1: beddingFrame.strikeVec[1], fx2: beddingFrame.strikeVec[2],
    fy0: beddingFrame.normal[0],    fy1: beddingFrame.normal[1],    fy2: beddingFrame.normal[2],
    fz0: beddingFrame.dipVec[0],    fz1: beddingFrame.dipVec[1],    fz2: beddingFrame.dipVec[2],
    minX, minY, minZ,
    maxX, maxY, maxZ,
    cx, cy, cz,
    radius,
  };
}

/**
 * Synthesizes a complete 3D Volumetric SDF Boulder Arch formation from 1 or more user-edited
 * 3D Catmull-Rom curves (`params.customArchCurves`).
 */
export function buildCustomUserSplineArch(params, tectonics, rng, addTalusFn = null) {
  const curvesInput = Array.isArray(params.customArchCurves) && params.customArchCurves.length > 0
    ? params.customArchCurves
    : DEFAULT_CUSTOM_ARCH_CURVES;

  const blocks = [];
  const cuts = [];
  const curves = [];
  const groundY = 0;

  const isCoastal = params.archetype === 'sea-arch';
  const rockStyle = isCoastal ? 'coastal' : 'sandstone';
  const strikeRad = (params.strikeAngle ?? 12) * DEG2RAD;
  const dipRad = (params.dipAngle ?? 6) * DEG2RAD;
  const bedFrame = buildStrikeDipFrame(strikeRad, dipRad);

  let maxSpanWorld = 10;

  for (const pts of curvesInput) {
    if (!Array.isArray(pts) || pts.length < 2) continue;

    // Estimate span & crown height of this user curve for proportional boulder sizing
    let pMinX = Infinity, pMaxX = -Infinity;
    let pMinY = Infinity, pMaxY = -Infinity;
    let pMinZ = Infinity, pMaxZ = -Infinity;
    for (const p of pts) {
      if (p.x < pMinX) pMinX = p.x;
      if (p.x > pMaxX) pMaxX = p.x;
      if (p.y < pMinY) pMinY = p.y;
      if (p.y > pMaxY) pMaxY = p.y;
      if (p.z < pMinZ) pMinZ = p.z;
      if (p.z > pMaxZ) pMaxZ = p.z;
    }

    const chordSpan = Math.max(6.0, Math.hypot(pMaxX - pMinX, pMaxZ - pMinZ, (pMaxY - pMinY) * 0.7));
    if (chordSpan > maxSpanWorld) maxSpanWorld = chordSpan;

    const bevel = chordSpan * 0.0024;
    const smoothR = chordSpan * 0.0030;

    // Compute average user thickness multiplier `r`
    let avgR = 0;
    for (const p of pts) avgR += (p.r ?? 1.0);
    avgR /= pts.length;

    const rib = compileUserSplineRib(pts, bedFrame, rng, {
      M: 64,
      baseRadius: chordSpan * 0.022,
      edgeBevel: bevel,
      smoothUnionR: smoothR,
      isEolian: !isCoastal,
      crookedAmp: 0.32,
    });
    curves.push(rib);

    // Portal guard keeps airway under high crown points open while boulders step organically!
    const portalCenter = [
      0.5 * (pMinX + pMaxX),
      groundY + Math.max(1.2, (pMaxY - groundY) * 0.34),
      0.5 * (pMinZ + pMaxZ),
    ];
    const minPortalClear = Math.max(0.8, Math.min((pMaxY - groundY) * 0.38, chordSpan * 0.16));

    buildBoulderArchSpanFromStartToFinish(blocks, rib, bedFrame, rng, {
      S: chordSpan,
      groundY,
      rockStyle,
      bevel,
      smoothR,
      spanScale: clamp(0.86 * avgR, 0.48, 1.65),
      leftMassScale: clamp(pts[0].r ?? 1.15, 0.65, 1.8),
      rightMassScale: clamp(pts[pts.length - 1].r ?? 1.12, 0.65, 1.8),
      portalCenter,
      minPortalClear,
    });

    // Grounded pier footing boulders at any curve endpoint near groundY so piers anchor solidly!
    for (const endPt of [pts[0], pts[pts.length - 1]]) {
      if (endPt.y <= groundY + chordSpan * 0.18) {
        const footR = chordSpan * 0.11 * (endPt.r ?? 1.1);
        for (let k = 0; k < 3; k++) {
          const ang = (k / 3) * Math.PI * 2 + rng.float();
          const ox = Math.cos(ang) * footR * 0.55;
          const oz = Math.sin(ang) * footR * 0.55;
          const bf = buildStrikeDipFrame(strikeRad + rng.signed() * 0.22, dipRad + rng.signed() * 0.12);
          const poly = sculptRockTypeBoulder(
            [endPt.x + ox, groundY + footR * rng.range(0.45, 0.85), endPt.z + oz],
            [footR * rng.range(0.75, 1.15), footR * rng.range(0.55, 0.92), footR * rng.range(0.75, 1.15)],
            bf,
            rng,
            rockStyle,
            groundY
          );
          if (poly) {
            const comp = compilePolyBlock(poly, isCoastal ? bf : bedFrame, {
              edgeBevel: bevel,
              smoothUnionR: smoothR,
              detailScale: 1.0,
              isEolian: !isCoastal,
              seedSalt: rng.int(1, 999999),
            });
            if (comp) blocks.push(comp);
          }
        }
      }
    }
  }

  if (addTalusFn && blocks.length > 0) {
    addTalusFn(blocks, bedFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.35) * 0.85,
      scale: maxSpanWorld * 0.85,
      spanX: maxSpanWorld * 0.90,
      groundY,
      edgeBevel: maxSpanWorld * 0.0024,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, curves, groundY };
}
