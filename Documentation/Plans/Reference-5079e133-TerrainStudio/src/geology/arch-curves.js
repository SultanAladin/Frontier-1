/**
 * Boulder-Built 3D Arch Architecture Engine (`desert-arch`, `double-arch`, `sea-arch`).
 *
 * Design Principles:
 * 1. ZERO visible smooth mathematical Bézier tubes: `compileCurvedArchRib` serves strictly as a
 *    slender internal hidden structural spine (`rCrown ~ 0.019*S`) buried deep inside the boulders
 *    to guarantee 100% single-manifold connectivity (`cuts: []`, 0 floating islands).
 * 2. Continuous Boulders from Start (`t = 0.0`, ground) to Finish (`t = 1.0`, ground):
 *    Every meter of piers, haunches, shoulders, soffit underside, and crown bridge is built from
 *    overlapping, interlocking large & medium 3D boulders with crisp rock seams (`smoothUnionR` kept
 *    tight so individual boulders remain distinctly readable!).
 * 3. Rock-Type-Defining Large Boulders:
 *    - `'sandstone'` (`desert-arch`, `double-arch`): Massive weathered cross-bedded Entrada/Navajo
 *      sandstone megaliths, overhanging tabular cap-boulders, bulbous slickrock shoulder boulders,
 *      and stepped corbel blocks.
 *    - `'coastal'` (`sea-arch`): Huge angular, tilted, wave-quarried coastal cliff boulders, sharp
 *      dihedral fracture prisms, wedged polygonal keystone boulders, and jagged sea-stack crags.
 */

import {
  DEG2RAD,
  buildStrikeDipFrame,
  perturbNormal,
  vnorm,
  vcross,
  vdot,
  clamp,
  smax,
} from '../core/vec3.js';
import { scalarField3D } from '../core/spatial-noise.js';
import {
  createBoxPoly,
  clipPolyByHalfSpace,
  sculptGeologicalParentPoly,
  compilePolyBlock,
} from './polyhedron.js';

export function evalCubicBezier3D(p0, p1, p2, p3, t) {
  const u = 1 - t;
  const b0 = u * u * u;
  const b1 = 3 * u * u * t;
  const b2 = 3 * u * t * t;
  const b3 = t * t * t;
  return [
    b0 * p0[0] + b1 * p1[0] + b2 * p2[0] + b3 * p3[0],
    b0 * p0[1] + b1 * p1[1] + b2 * p2[1] + b3 * p3[1],
    b0 * p0[2] + b1 * p1[2] + b2 * p2[2] + b3 * p3[2],
  ];
}

export function evalCubicBezierDeriv3D(p0, p1, p2, p3, t) {
  const u = 1 - t;
  const d0 = 3 * u * u;
  const d1 = 6 * u * t;
  const d2 = 3 * t * t;
  return [
    d0 * (p1[0] - p0[0]) + d1 * (p2[0] - p1[0]) + d2 * (p3[0] - p2[0]),
    d0 * (p1[1] - p0[1]) + d1 * (p2[1] - p1[1]) + d2 * (p3[1] - p2[1]),
    d0 * (p1[2] - p0[2]) + d1 * (p2[2] - p1[2]) + d2 * (p3[2] - p2[2]),
  ];
}

function buildCurveBoulderFrame(tan, extN, depB, beddingFrame, blendRegional, rollRad, pitchRad) {
  const cr = Math.cos(rollRad), sr = Math.sin(rollRad);
  const nRolled = vnorm([
    extN[0] * cr + depB[0] * sr,
    extN[1] * cr + depB[1] * sr,
    extN[2] * cr + depB[2] * sr,
  ]);
  const cp = Math.cos(pitchRad), sp = Math.sin(pitchRad);
  const curveNormal = vnorm([
    nRolled[0] * cp - tan[0] * sp,
    nRolled[1] * cp - tan[1] * sp,
    nRolled[2] * cp - tan[2] * sp,
  ]);
  const curveStrike = vnorm([
    tan[0] * cp + nRolled[0] * sp,
    tan[1] * cp + nRolled[1] * sp,
    tan[2] * cp + nRolled[2] * sp,
  ]);

  // Blend curve tangent orientation with regional rock bedding so sandstone boulders keep realistic
  // sub-horizontal/dipping strata shelves while coastal boulders follow tilted joint planes!
  const b = clamp(blendRegional, 0, 1);
  const normal = vnorm([
    curveNormal[0] * (1 - b) + beddingFrame.normal[0] * b,
    curveNormal[1] * (1 - b) + beddingFrame.normal[1] * b,
    curveNormal[2] * (1 - b) + beddingFrame.normal[2] * b,
  ]);
  let strikeVec = [
    curveStrike[0] * (1 - b * 0.5) + beddingFrame.strikeVec[0] * (b * 0.5),
    curveStrike[1] * (1 - b * 0.5) + beddingFrame.strikeVec[1] * (b * 0.5),
    curveStrike[2] * (1 - b * 0.5) + beddingFrame.strikeVec[2] * (b * 0.5),
  ];
  const dotSN = vdot(strikeVec, normal);
  strikeVec = vnorm([
    strikeVec[0] - normal[0] * dotSN,
    strikeVec[1] - normal[1] * dotSN,
    strikeVec[2] - normal[2] * dotSN,
  ]);
  const dipVec = vnorm(vcross(strikeVec, normal));
  return { strikeVec, normal, dipVec };
}

/**
 * Compiles an internal hidden 3D structural spine along a crooked, organically jogged trajectory.
 * Kept slender (`rCrownIn ~ 0.019*S`) so it is 100% engulfed by real 3D boulders from t=0 to t=1!
 */
export function compileCurvedArchRib(p0, p1, p2, p3, beddingFrame, rng, opts = {}) {
  const {
    stations = 56,
    rPierIn = 0.72,
    rPierOut = 0.78,
    rCrownIn = 0.34,
    rCrownOut = 0.40,
    waistT = 0.62,
    waistPinch = 0.82,
    edgeBevel = 0.03,
    smoothUnionR = 0.06,
    finTaper = 0.24,
    isEolian = true,
    detailScale = 1.0,
    leftPierAsym = 1.15,
    rightPierAsym = 0.95,
    hardness = 0.86,
    crookedAmp = 1.0,
  } = opts;

  const M = stations;
  const px = new Float32Array(M);
  const py = new Float32Array(M);
  const pz = new Float32Array(M);
  const nx = new Float32Array(M);
  const ny = new Float32Array(M);
  const nz = new Float32Array(M);
  const bx = new Float32Array(M);
  const by = new Float32Array(M);
  const bz = new Float32Array(M);
  const rIn = new Float32Array(M);
  const rOut = new Float32Array(M);

  const tx = new Float32Array(M - 1);
  const ty = new Float32Array(M - 1);
  const tz = new Float32Array(M - 1);
  const segLen = new Float32Array(M - 1);

  const chordLen = Math.hypot(p3[0] - p0[0], p3[2] - p0[2]) || 1;
  const chordDx = p3[0] - p0[0];
  const chordDz = p3[2] - p0[2];
  const archNormal = vnorm(vcross([chordDx, 0, chordDz], [0, 1, 0]));
  const waveSalt = rng.int(1, 99999);

  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;

  for (let k = 0; k < M; k++) {
    const t = k / (M - 1);
    const pt = evalCubicBezier3D(p0, p1, p2, p3, t);
    const dv = evalCubicBezierDeriv3D(p0, p1, p2, p3, t);
    const tan = vnorm(dv);

    const rawExtN = vnorm(vcross(archNormal, tan));
    const rawDepB = vnorm(vcross(tan, rawExtN));
    const midMask = Math.sin(Math.PI * t);
    const pierMask = Math.pow(1 - midMask, 1.5);
    const sideBias = (1 - t) * leftPierAsym + t * rightPierAsym;

    // Non-parabolic crooked 3D geological centerline jogs so the arch never traces a clean curve!
    const jogN = (
      scalarField3D(t * 2.3, 0.7, 1.4, waveSalt + 13) * 0.048 +
      scalarField3D(t * 4.9, 1.8, 0.5, waveSalt + 37) * 0.022
    ) * chordLen * midMask * crookedAmp;
    const jogB = (
      scalarField3D(t * 2.0, 2.4, 0.6, waveSalt + 71) * 0.065 +
      scalarField3D(t * 4.3, 0.9, 2.1, waveSalt + 97) * 0.024
    ) * chordLen * midMask * crookedAmp;

    const waistDist = (t - waistT) / 0.17;
    const waistFactor = 1.0 - (1.0 - waistPinch) * Math.exp(-waistDist * waistDist);

    const ri = (rCrownIn + (rPierIn * sideBias - rCrownIn) * pierMask) * waistFactor;
    const ro = (rCrownOut + (rPierOut * sideBias - rCrownOut) * pierMask) * waistFactor;

    const wx = pt[0] + rawExtN[0] * jogN + rawDepB[0] * jogB;
    const wy = pt[1] + rawExtN[1] * jogN + rawDepB[1] * jogB;
    const wz = pt[2] + rawExtN[2] * jogN + rawDepB[2] * jogB;

    px[k] = wx; py[k] = wy; pz[k] = wz;
    nx[k] = rawExtN[0]; ny[k] = rawExtN[1]; nz[k] = rawExtN[2];
    bx[k] = rawDepB[0]; by[k] = rawDepB[1]; bz[k] = rawDepB[2];
    rIn[k] = ri;
    rOut[k] = ro;

    const pad = Math.max(ri, ro) * 1.35 + edgeBevel;
    if (wx - pad < minX) minX = wx - pad;
    if (wy - pad < minY) minY = wy - pad;
    if (wz - pad < minZ) minZ = wz - pad;
    if (wx + pad > maxX) maxX = wx + pad;
    if (wy + pad > maxY) maxY = wy + pad;
    if (wz + pad > maxZ) maxZ = wz + pad;
  }

  for (let k = 0; k < M - 1; k++) {
    const dx = px[k + 1] - px[k];
    const dy = py[k + 1] - py[k];
    const dz = pz[k + 1] - pz[k];
    const len = Math.hypot(dx, dy, dz) || 1e-4;
    tx[k] = dx / len;
    ty[k] = dy / len;
    tz[k] = dz / len;
    segLen[k] = len;
  }

  const cx = 0.5 * (minX + maxX);
  const cy = 0.5 * (minY + maxY);
  const cz = 0.5 * (minZ + maxZ);
  const radius = 0.5 * Math.hypot(maxX - minX, maxY - minY, maxZ - minZ);

  return {
    isCurveRib: true,
    M,
    px, py, pz,
    tx, ty, tz, segLen,
    nx, ny, nz,
    bx, by, bz,
    rIn, rOut,
    finTaper,
    edgeBevel,
    smoothUnionR,
    detailScale,
    hardness,
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

export function evalCurvedArchRib(rib, wx, wy, wz) {
  const M = rib.M;
  const px = rib.px, py = rib.py, pz = rib.pz;
  const tx = rib.tx, ty = rib.ty, tz = rib.tz, segLen = rib.segLen;

  let bestK = 0;
  let bestAlpha = 0;
  let bestDistSq = 1e18;

  for (let k = 0; k < M - 1; k++) {
    const vx = wx - px[k];
    const vy = wy - py[k];
    const vz = wz - pz[k];
    const len = segLen[k];
    let proj = vx * tx[k] + vy * ty[k] + vz * tz[k];
    if (proj < 0) proj = 0;
    else if (proj > len) proj = len;

    const dx = vx - tx[k] * proj;
    const dy = vy - ty[k] * proj;
    const dz = vz - tz[k] * proj;
    const dSq = dx * dx + dy * dy + dz * dz;
    if (dSq < bestDistSq) {
      bestDistSq = dSq;
      bestK = k;
      bestAlpha = proj / len;
    }
  }

  const k = bestK;
  const a = bestAlpha;
  const ia = 1 - a;

  const cx = px[k] * ia + px[k + 1] * a;
  const cy = py[k] * ia + py[k + 1] * a;
  const cz = pz[k] * ia + pz[k + 1] * a;

  const enx = rib.nx[k] * ia + rib.nx[k + 1] * a;
  const eny = rib.ny[k] * ia + rib.ny[k + 1] * a;
  const enz = rib.nz[k] * ia + rib.nz[k + 1] * a;

  const dbx = rib.bx[k] * ia + rib.bx[k + 1] * a;
  const dby = rib.by[k] * ia + rib.by[k + 1] * a;
  const dbz = rib.bz[k] * ia + rib.bz[k + 1] * a;

  const ri = rib.rIn[k] * ia + rib.rIn[k + 1] * a;
  const ro = rib.rOut[k] * ia + rib.rOut[k + 1] * a;

  const rx = wx - cx;
  const ry = wy - cy;
  const rz = wz - cz;

  const u = rx * enx + ry * eny + rz * enz;
  const v = rx * dbx + ry * dby + rz * dbz;

  let wEnd = 0;
  if (k === 0 && a <= 1e-4) {
    const p0Proj = (wx - px[0]) * tx[0] + (wy - py[0]) * ty[0] + (wz - pz[0]) * tz[0];
    if (p0Proj < 0) wEnd = -p0Proj;
  } else if (k === M - 2 && a >= 1 - 1e-4) {
    const pmProj = (wx - px[M - 1]) * tx[M - 2] + (wy - py[M - 1]) * ty[M - 2] + (wz - pz[M - 1]) * tz[M - 2];
    if (pmProj > 0) wEnd = pmProj;
  }

  const dTop = u - ri;
  const dBot = -u - ri * 0.90;
  const dFront = v - ro;
  const dBack = -v - ro;

  const bev = rib.edgeBevel;
  let dCross = smax(smax(dTop, dBot, bev), smax(dFront, dBack, bev), bev);

  if (wEnd > 0) {
    if (dCross > 0) return Math.hypot(dCross, wEnd);
    return Math.max(dCross, wEnd);
  }
  return dCross;
}

/**
 * Helper to sample position & local frame from the compiled crooked rib at parameter `t in [0, 1]`.
 */
function sampleRibStation(rib, t) {
  const tc = clamp(t, 0, 1);
  const kFloat = tc * (rib.M - 1);
  const k0 = Math.min(rib.M - 2, Math.floor(kFloat));
  const ka = kFloat - k0;
  const ia = 1 - ka;

  const cx = rib.px[k0] * ia + rib.px[k0 + 1] * ka;
  const cy = rib.py[k0] * ia + rib.py[k0 + 1] * ka;
  const cz = rib.pz[k0] * ia + rib.pz[k0 + 1] * ka;
  const extN = vnorm([
    rib.nx[k0] * ia + rib.nx[k0 + 1] * ka,
    rib.ny[k0] * ia + rib.ny[k0 + 1] * ka,
    rib.nz[k0] * ia + rib.nz[k0 + 1] * ka,
  ]);
  const depB = vnorm([
    rib.bx[k0] * ia + rib.bx[k0 + 1] * ka,
    rib.by[k0] * ia + rib.by[k0 + 1] * ka,
    rib.bz[k0] * ia + rib.bz[k0 + 1] * ka,
  ]);
  const tan = vnorm([rib.tx[k0], rib.ty[k0], rib.tz[k0]]);
  return { cx, cy, cz, tan, extN, depB };
}

/**
 * Sculpts a rock-type-authentic 3D boulder:
 * - `'sandstone'`: Weathered cross-bedded Entrada/Navajo boulder with tabular/bulbous ledges,
 *   stepped cap bevels, and undercut eolian belly facets.
 * - `'coastal'`: Sharp, angular, wave-quarried coastal cliff megalith with tilted dihedral fracture
 *   faces and crisp polygonal ridge crests.
 */
export function sculptRockTypeBoulder(center, radii, frame, rng, rockStyle, groundY, portalCenter = null, minPortalClear = 0) {
  const isCoastal = rockStyle === 'coastal';
  const [bx, by, bz] = center;
  const [rx, ry, rz] = radii;
  const touchesGround = (by - Math.max(rx, ry, rz) * 1.05) <= groundY + 0.15;

  let poly = sculptGeologicalParentPoly(
    [bx, touchesGround ? Math.max(groundY + ry * 0.48, by) : by, bz],
    [rx, ry, rz],
    frame,
    rng,
    {
      jointDispersion: isCoastal ? 0.24 : 0.17,
      cornerBlunt: isCoastal ? 0.67 : 0.76,
      flatBaseY: touchesGround ? groundY : undefined,
      extraBevelCuts: isCoastal ? 5 : 7,
    }
  );
  if (!poly) return null;

  if (isCoastal) {
    // Crisp asymmetric coastal dihedral ridge & wave-plucked angular undercut!
    const az = rng.float() * Math.PI * 2;
    const ridgeN = vnorm([Math.cos(az) * 0.68, 0.73, Math.sin(az) * 0.68]);
    poly = clipPolyByHalfSpace(poly, ridgeN, vdot(ridgeN, [bx, by + ry * 0.72, bz]), 'coastalCrest') || poly;
  } else {
    // Weathered sandstone boulder shelf / hoodoo-cap bevel (avoids boxy cubes while keeping chunky sandstone character!)
    const az = rng.float() * Math.PI * 2;
    const domeN1 = vnorm([Math.cos(az) * 0.45, 0.89, Math.sin(az) * 0.45]);
    const domeN2 = vnorm([-Math.cos(az + 0.7) * 0.52, 0.85, -Math.sin(az + 0.7) * 0.52]);
    poly = clipPolyByHalfSpace(poly, domeN1, vdot(domeN1, [bx, by + ry * 0.80, bz]), 'sandstoneCap') || poly;
    poly = clipPolyByHalfSpace(poly, domeN2, vdot(domeN2, [bx, by + ry * 0.78, bz]) + rx * 0.18, 'sandstoneCap') || poly;
  }

  // Keep the open arch portal airway unobstructed while letting individual boulder undersides step naturally!
  if (portalCenter && minPortalClear > 0) {
    const dx = bx - portalCenter[0];
    const dy = by - portalCenter[1];
    const dz = bz - portalCenter[2];
    // Radial outward direction from portal center in arch plane
    const rLen = Math.hypot(dx, dy, dz) || 1;
    if (rLen < minPortalClear + Math.max(rx, ry)) {
      const inDir = [-dx / rLen, -dy / rLen, -dz / rLen];
      const pGuard = [
        portalCenter[0] + (dx / rLen) * minPortalClear,
        portalCenter[1] + (dy / rLen) * minPortalClear,
        portalCenter[2] + (dz / rLen) * minPortalClear,
      ];
      // Perturb guard plane per-boulder so the soffit ceiling is stepped & rocky, NEVER a smooth circle!
      const pNormal = perturbNormal(inDir, rng, 0.18);
      poly = clipPolyByHalfSpace(poly, pNormal, vdot(pNormal, pGuard), 'boulderSoffit') || poly;
    }
  }

  if (by - Math.max(rx, ry, rz) * 1.4 < groundY) {
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
  }
  return poly;
}

/**
 * Builds an entire arch OUT OF LARGE & MEDIUM BOULDERS FROM START (`t = 0.0`) TO FINISH (`t = 1.0`)!
 * Completely engulfs the internal structural rib on top, front, back, AND underside so 0% smooth
 * mathematical curve surface is ever visible!
 */
export function buildBoulderArchSpanFromStartToFinish(blocks, rib, beddingFrame, rng, opts = {}) {
  const {
    S = 18.0,
    groundY = 0,
    rockStyle = 'sandstone', // 'sandstone' | 'coastal'
    bevel = 0.04,
    smoothR = S * 0.0028, // Tight seam radius so individual boulders read clearly!
    spanScale = 1.0,
    leftMassScale = 1.22,
    rightMassScale = 1.02,
    portalCenter = [0, S * 0.18, 0],
    minPortalClear = S * 0.18,
  } = opts;

  const isCoastal = rockStyle === 'coastal';
  const isEolian = !isCoastal;
  const regionalBlend = isCoastal ? 0.30 : 0.58;
  const numPrimary = Math.round(28 * spanScale);
  const heroSalt = rng.int(1, 99999);

  // 1. Continuous Primary Structural Boulder Chain from t = 0.00 (ground start) to t = 1.00 (ground finish)!
  for (let i = 0; i < numPrimary; i++) {
    const tBase = i / (numPrimary - 1);
    const t = clamp(tBase + rng.signed() * (0.22 / numPrimary), 0.0, 1.0);
    const st = sampleRibStation(rib, t);

    const midMask = Math.sin(Math.PI * t); // 0 at ends, 1 at crown
    const pierEnv = Math.pow(1 - midMask, 1.35);
    const sideWeight = (1 - t) * leftMassScale + t * rightMassScale;

    // Dramatic boulder size modulation along the arch: huge megaliths at piers & shoulders,
    // alternating large keystone/cap boulders and pinched saddle boulders across the bridge!
    const isHeroBoulder = (i % 4 === 1) || (t > 0.20 && t < 0.36 && i % 2 === 0) || (t > 0.44 && t < 0.54 && i % 3 === 0);
    const sizeWave = 1.0 + 0.34 * scalarField3D(t * 5.2, 0.7, 1.3, heroSalt);
    const baseThick = S * (0.056 + 0.115 * pierEnv * sideWeight) * spanScale * sizeWave * (isHeroBoulder ? 1.28 : 0.94);

    const halfTan = isCoastal
      ? baseThick * rng.range(0.95, 1.42)
      : baseThick * rng.range(1.10, 1.62); // Sandstone forms broader slabby boulders
    const halfRad = isCoastal
      ? baseThick * rng.range(0.82, 1.25)
      : baseThick * rng.range(0.72, 1.12);
    const halfDep = baseThick * rng.range(0.95, 1.45);

    // Stagger boulders radially and laterally so they step & jut organically instead of lining up like beads!
    const offN = baseThick * rng.range(-0.18, 0.34);
    const offB = baseThick * rng.signed() * 0.28;

    const center = [
      st.cx + st.extN[0] * offN + st.depB[0] * offB,
      st.cy + st.extN[1] * offN + st.depB[1] * offB,
      st.cz + st.extN[2] * offN + st.depB[2] * offB,
    ];

    const bFrame = buildCurveBoulderFrame(
      st.tan, st.extN, st.depB, beddingFrame,
      regionalBlend,
      rng.signed() * (isCoastal ? 0.38 : 0.22),
      rng.signed() * (isCoastal ? 0.30 : 0.16)
    );

    const poly = sculptRockTypeBoulder(
      center,
      [halfTan, halfRad, halfDep],
      bFrame,
      rng,
      rockStyle,
      groundY,
      midMask > 0.25 ? portalCenter : null,
      minPortalClear
    );
    if (!poly) continue;

    const comp = compilePolyBlock(poly, isCoastal ? bFrame : beddingFrame, {
      edgeBevel: bevel * rng.range(0.80, 1.20),
      smoothUnionR: smoothR,
      detailScale: rng.range(0.92, 1.14),
      isEolian,
      seedSalt: rng.int(1, 999999),
    });
    if (comp) blocks.push(comp);
  }

  // 2. Large Rock-Type-Defining Feature Boulders from Start to Finish (Massive Pier Clusters,
  //    Cantilevered Shoulder Cap-Boulders, Underside Corbel Boulders & Crown Keystone Megaliths!)
  const numFeatureBoulders = Math.round(30 * spanScale);
  for (let j = 0; j < numFeatureBoulders; j++) {
    const t = clamp((j + 0.5) / numFeatureBoulders + rng.signed() * 0.025, 0.01, 0.99);
    const st = sampleRibStation(rib, t);
    const midMask = Math.sin(Math.PI * t);
    const pierEnv = Math.pow(1 - midMask, 1.20);
    const sideWeight = (1 - t) * leftMassScale + t * rightMassScale;

    // Distribute feature boulders all around the cross-section (outer shoulder, front/back cheeks, soffit corbels!)
    const role = j % 4;
    const baseR = S * (0.048 + 0.115 * pierEnv * sideWeight) * spanScale;
    let offN = 0, offB = 0;
    let rT = baseR, rN = baseR, rB = baseR;

    if (role === 0) {
      // Large upper/extrados cap-boulder (gives sandstone its Metate/Turret caprock boulder silhouette, or coastal crag crest!)
      offN = baseR * rng.range(0.42, 0.86);
      offB = rng.signed() * baseR * 0.35;
      rT = baseR * (isCoastal ? rng.range(0.85, 1.30) : rng.range(1.15, 1.75));
      rN = baseR * (isCoastal ? rng.range(0.75, 1.18) : rng.range(0.55, 0.88));
      rB = baseR * rng.range(0.95, 1.45);
    } else if (role === 1) {
      // Bold front-face cheek boulder breaking up the arch side profile!
      offN = rng.signed() * baseR * 0.30;
      offB = baseR * rng.range(0.45, 0.88);
      rT = baseR * rng.range(0.95, 1.45);
      rN = baseR * rng.range(0.75, 1.15);
      rB = baseR * rng.range(0.68, 1.05);
    } else if (role === 2) {
      // Bold rear-face cheek boulder
      offN = rng.signed() * baseR * 0.30;
      offB = -baseR * rng.range(0.45, 0.88);
      rT = baseR * rng.range(0.95, 1.45);
      rN = baseR * rng.range(0.75, 1.15);
      rB = baseR * rng.range(0.68, 1.05);
    } else {
      // Stepped underside corbel / wedged soffit boulder (eliminates smooth curved arch ceiling!)
      offN = -baseR * rng.range(0.28, 0.58);
      offB = rng.signed() * baseR * 0.32;
      rT = baseR * rng.range(0.88, 1.35);
      rN = baseR * rng.range(0.58, 0.90);
      rB = baseR * rng.range(0.82, 1.25);
    }

    const center = [
      st.cx + st.extN[0] * offN + st.depB[0] * offB,
      st.cy + st.extN[1] * offN + st.depB[1] * offB,
      st.cz + st.extN[2] * offN + st.depB[2] * offB,
    ];
    if (center[1] + rN < groundY + S * 0.02) continue;

    const bFrame = buildCurveBoulderFrame(
      st.tan, st.extN, st.depB, beddingFrame,
      regionalBlend,
      rng.signed() * (isCoastal ? 0.44 : 0.25),
      rng.signed() * (isCoastal ? 0.32 : 0.18)
    );

    const poly = sculptRockTypeBoulder(
      center,
      [rT, rN, rB],
      bFrame,
      rng,
      rockStyle,
      groundY,
      midMask > 0.20 ? portalCenter : null,
      minPortalClear
    );
    if (!poly) continue;

    const comp = compilePolyBlock(poly, isCoastal ? bFrame : beddingFrame, {
      edgeBevel: bevel * rng.range(0.75, 1.15),
      smoothUnionR: smoothR * 0.9,
      detailScale: rng.range(0.95, 1.15),
      isEolian,
      seedSalt: rng.int(1, 999999),
    });
    if (comp) blocks.push(comp);
  }
}

/**
 * Builds a massive, organic rock-type-specific boulder headland / pier cluster from groundY upward
 * (large weathered sandstone megaliths & cap-slabs OR jagged angular coastal cliff boulders).
 */
export function addBoulderMassifPier(blocks, cx, cz, width, height, depth, beddingFrame, rng, opts = {}) {
  const {
    groundY = 0,
    bevel = 0.04,
    smoothR = 0.05,
    rockStyle = 'sandstone',
    tiers = 5,
  } = opts;

  const isCoastal = rockStyle === 'coastal';
  const isEolian = !isCoastal;

  for (let t = 0; t < tiers; t++) {
    const f = t / Math.max(1, tiers - 1);
    const y0 = groundY + f * height * 0.78;
    const tierH = height * (0.36 - 0.11 * f) * rng.range(0.88, 1.18);
    // Sandstone piers can flare near caprock (mushroom/Turret Arch style!); coastal piers taper craggily!
    const capFlare = (!isCoastal && t === tiers - 1) ? 1.14 : 1.0;
    const taper = (1.08 - 0.36 * f) * capFlare;
    const count = t < 2 ? 3 : 2;

    for (let c = 0; c < count; c++) {
      const ang = (c / count) * Math.PI * 2 + t * 1.15 + rng.signed() * 0.45;
      const dist = (t === tiers - 1 && c === 0) ? width * 0.08 : width * rng.range(0.14, 0.34) * taper;
      const bx = cx + Math.cos(ang) * dist;
      const bz = cz + Math.sin(ang) * dist * (depth / Math.max(0.1, width));
      const by = y0 + tierH * 0.48;

      const strikeBase = Math.atan2(beddingFrame.strikeVec[2], beddingFrame.strikeVec[0]);
      const localF = buildStrikeDipFrame(
        strikeBase + rng.signed() * (isCoastal ? 0.42 : 0.20),
        (isCoastal ? rng.range(22, 48) : rng.range(5, 18)) * DEG2RAD
      );

      const rx = width * (isCoastal ? rng.range(0.34, 0.54) : rng.range(0.40, 0.62)) * taper;
      const ry = tierH * (isCoastal ? rng.range(0.58, 0.82) : rng.range(0.48, 0.68));
      const rz = depth * rng.range(0.36, 0.58) * taper;

      const poly = sculptRockTypeBoulder(
        [bx, by, bz],
        [rx, ry, rz],
        localF,
        rng,
        rockStyle,
        groundY
      );
      if (!poly) continue;

      const comp = compilePolyBlock(poly, isCoastal ? localF : beddingFrame, {
        edgeBevel: bevel * rng.range(0.80, 1.15),
        smoothUnionR: smoothR,
        detailScale: 1.02,
        isEolian,
        seedSalt: rng.int(1, 999999),
      });
      if (comp) blocks.push(comp);
    }
  }
}

/**
 * 1. UTAH FREESTANDING SANDSTONE BOULDER ARCH (`desert-arch`)
 * Metate Arch / Turret Arch / Broken Arch inspired Utah sandstone arch:
 * - Built 100% from start to finish out of massive weathered Entrada/Navajo sandstone boulders,
 *   overhanging tabular cap-boulders, crooked cantilevered bridge boulders, and tumbled desert
 *   sandstone megaliths (ZERO naked smooth mathematical curve pipe & ZERO flat sidewalk slab!).
 */
export function buildDesertArch(params, tectonics, rng, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const curves = [];

  const dipRad = clamp((params.dipAngle ?? 9), 3, 22) * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? -10) * DEG2RAD;
  const beddingFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const overhang = params.overhangStrength ?? 0.78;
  const bevel = S * (0.0018 + 0.0028 * (params.edgeWear ?? 0.50));
  const smoothR = S * 0.0030; // Crisp boulder-to-boulder crevices!

  // 1. Massive Asymmetric Sandstone Boulder Buttress Towers at Both Ends (Metate / Turret Arch style!)
  addBoulderMassifPier(
    blocks,
    -0.43 * S, -0.02 * S,
    0.34 * S, 0.56 * S, 0.32 * S,
    beddingFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'sandstone', tiers: 5 }
  );

  addBoulderMassifPier(
    blocks,
     0.42 * S, 0.03 * S,
    0.29 * S, 0.40 * S, 0.29 * S,
    beddingFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'sandstone', tiers: 4 }
  );

  // Asymmetric crooked arch trajectory (higher shoulder on left tower, lower landing on right pier!)
  const archSpanH = (0.57 + 0.06 * overhang) * S;
  const ctrlY = archSpanH / 0.75;
  const p0 = [-0.41 * S, groundY + 0.02 * S, -0.02 * S];
  const p1 = [-0.37 * S, groundY + ctrlY * 1.12,  0.06 * S];
  const p2 = [ 0.27 * S, groundY + ctrlY * 0.86, -0.05 * S];
  const p3 = [ 0.41 * S, groundY + 0.02 * S,  0.03 * S];

  // Hidden internal spine (strictly buried inside the boulders!)
  const mainRib = compileCurvedArchRib(p0, p1, p2, p3, beddingFrame, rng, {
    stations: 58,
    rPierIn: S * 0.042,
    rPierOut: S * 0.046,
    rCrownIn: S * 0.020,
    rCrownOut: S * 0.024,
    waistT: 0.64,
    waistPinch: 0.82,
    edgeBevel: S * 0.0025,
    smoothUnionR: smoothR,
    isEolian: true,
    crookedAmp: 1.15,
  });
  curves.push(mainRib);

  // Continuous large & medium sandstone boulders from t = 0.00 (start) all the way to t = 1.00 (finish)!
  buildBoulderArchSpanFromStartToFinish(blocks, mainRib, beddingFrame, rng, {
    S,
    groundY,
    rockStyle: 'sandstone',
    bevel,
    smoothR,
    spanScale: 1.02,
    leftMassScale: 1.24,
    rightMassScale: 1.02,
    portalCenter: [0.00 * S, groundY + 0.22 * S, 0],
    minPortalClear: S * 0.21,
  });

  // Clustered basal sandstone outcrop boulders framing the two piers (no artificial flat bridge strip!)
  const apronBoulders = [
    { x: -0.26, z:  0.14, rx: 0.11, ry: 0.075, rz: 0.10 },
    { x: -0.30, z: -0.16, rx: 0.12, ry: 0.085, rz: 0.11 },
    { x:  0.27, z:  0.15, rx: 0.11, ry: 0.070, rz: 0.10 },
    { x:  0.28, z: -0.14, rx: 0.10, ry: 0.065, rz: 0.10 },
    { x: -0.52, z:  0.08, rx: 0.12, ry: 0.080, rz: 0.11 },
    { x:  0.51, z: -0.05, rx: 0.11, ry: 0.072, rz: 0.10 },
  ];
  for (const ab of apronBoulders) {
    const bf = buildStrikeDipFrame(strikeRad + rng.signed() * 0.35, rng.range(6, 22) * DEG2RAD);
    const bPoly = sculptRockTypeBoulder(
      [ab.x * S, groundY + ab.ry * 0.72 * S, ab.z * S],
      [ab.rx * S, ab.ry * S, ab.rz * S],
      bf, rng, 'sandstone', groundY
    );
    if (bPoly) {
      const comp = compilePolyBlock(bPoly, beddingFrame, {
        edgeBevel: bevel,
        smoothUnionR: smoothR,
        detailScale: 1.0,
        isEolian: true,
        seedSalt: rng.int(1, 999999),
      });
      if (comp) blocks.push(comp);
    }
  }

  if (addTalusFn) {
    addTalusFn(blocks, beddingFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.40) * 0.90,
      scale: S * 0.86,
      spanX: S * 0.88,
      groundY,
      edgeBevel: bevel * 0.85,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, curves, groundY };
}

/**
 * 2. TWIN SPAN DOUBLE SANDSTONE BOULDER ARCH (`double-arch`)
 * Two staggered, crooked Entrada sandstone boulder arches sharing a towering central-left boulder
 * massif — BOTH arches built 100% out of large sandstone boulders from start to finish!
 */
export function buildDoubleArch(params, tectonics, rng, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const curves = [];

  const dipRad = clamp((params.dipAngle ?? 8), 3, 22) * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? -14) * DEG2RAD;
  const beddingFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const overhang = params.overhangStrength ?? 0.82;
  const bevel = S * (0.0018 + 0.0028 * (params.edgeWear ?? 0.48));
  const smoothR = S * 0.0030;

  // 1. Three Massive Sandstone Boulder Massif Piers in Iconic V-Buttress Layout
  // (Left Outer Buttress + Towering Central Shared Buttress + Right Outer Buttress — BOTH spans clearly open!)
  addBoulderMassifPier(
    blocks,
    -0.50 * S, -0.05 * S,
    0.30 * S, 0.50 * S, 0.29 * S,
    beddingFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'sandstone', tiers: 5 }
  );

  addBoulderMassifPier(
    blocks,
     0.02 * S, 0.09 * S,
    0.31 * S, 0.56 * S, 0.30 * S,
    beddingFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'sandstone', tiers: 5 }
  );

  addBoulderMassifPier(
    blocks,
     0.50 * S, -0.09 * S,
    0.28 * S, 0.40 * S, 0.27 * S,
    beddingFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'sandstone', tiers: 4 }
  );

  // 2. Left High Sandstone Boulder Arch Span (100% boulder-clad from t=0 to t=1!)
  const highCrownY = (0.56 + 0.06 * overhang) * S;
  const highCtrlY = highCrownY / 0.75;
  const hp0 = [-0.49 * S, groundY + 0.02 * S, -0.04 * S];
  const hp1 = [-0.44 * S, groundY + highCtrlY * 1.06,  0.02 * S];
  const hp2 = [-0.05 * S, groundY + highCtrlY * 0.94,  0.09 * S];
  const hp3 = [ 0.01 * S, groundY + 0.02 * S,  0.08 * S];

  const highRib = compileCurvedArchRib(hp0, hp1, hp2, hp3, beddingFrame, rng, {
    stations: 54,
    rPierIn: S * 0.038,
    rPierOut: S * 0.042,
    rCrownIn: S * 0.019,
    rCrownOut: S * 0.023,
    waistT: 0.54,
    waistPinch: 0.84,
    edgeBevel: S * 0.0025,
    smoothUnionR: smoothR,
    isEolian: true,
    crookedAmp: 1.12,
  });
  curves.push(highRib);

  buildBoulderArchSpanFromStartToFinish(blocks, highRib, beddingFrame, rng, {
    S,
    groundY,
    rockStyle: 'sandstone',
    bevel,
    smoothR,
    spanScale: 0.90,
    leftMassScale: 1.16,
    rightMassScale: 1.18,
    portalCenter: [-0.24 * S, groundY + 0.21 * S, 0.02 * S],
    minPortalClear: S * 0.165,
  });

  // 3. Right Angled Sandstone Boulder Arch Span (100% boulder-clad from t=0 to t=1!)
  const lowCrownY = (0.45 + 0.05 * overhang) * S;
  const lowCtrlY = lowCrownY / 0.75;
  const lp0 = [ 0.03 * S, groundY + 0.02 * S,  0.08 * S];
  const lp1 = [ 0.11 * S, groundY + lowCtrlY * 1.08,  0.03 * S];
  const lp2 = [ 0.43 * S, groundY + lowCtrlY * 0.90, -0.05 * S];
  const lp3 = [ 0.49 * S, groundY + 0.02 * S, -0.08 * S];

  const lowRib = compileCurvedArchRib(lp0, lp1, lp2, lp3, beddingFrame, rng, {
    stations: 50,
    rPierIn: S * 0.035,
    rPierOut: S * 0.039,
    rCrownIn: S * 0.018,
    rCrownOut: S * 0.022,
    waistT: 0.56,
    waistPinch: 0.84,
    edgeBevel: S * 0.0024,
    smoothUnionR: smoothR,
    isEolian: true,
    crookedAmp: 1.10,
  });
  curves.push(lowRib);

  buildBoulderArchSpanFromStartToFinish(blocks, lowRib, beddingFrame, rng, {
    S,
    groundY,
    rockStyle: 'sandstone',
    bevel,
    smoothR,
    spanScale: 0.84,
    leftMassScale: 1.18,
    rightMassScale: 1.04,
    portalCenter: [0.26 * S, groundY + 0.17 * S, 0.00 * S],
    minPortalClear: S * 0.145,
  });

  if (addTalusFn) {
    addTalusFn(blocks, beddingFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.42) * 0.90,
      scale: S * 0.86,
      spanX: S * 0.88,
      groundY,
      edgeBevel: bevel * 0.85,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, curves, groundY };
}

/**
 * 3. COASTAL SEA ARCH & SEA STACK (`sea-arch`)
 * Rugged maritime cliff headland & sea-stack arch built from start to finish out of massive,
 * angular, wave-quarried coastal cliff boulders, tilted fracture prisms, and wedged lintel blocks!
 */
export function buildSeaArch(params, tectonics, rng, recursiveFractureFn, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const curves = [];
  const bevel = S * (0.0012 + 0.0026 * (params.edgeWear ?? 0.36));
  const smoothR = S * 0.0024; // Razor-crisp angular coastal boulder joints!

  const dipRad = clamp((params.dipAngle ?? 28), 20, 46) * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? -12) * DEG2RAD;
  const bedFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const overhang = params.overhangStrength ?? 0.76;

  // 1. Towering Jagged Mainland Coastal Boulder Headland (Left)
  addBoulderMassifPier(
    blocks,
    -0.41 * S, -0.01 * S,
    0.36 * S, 0.62 * S, 0.35 * S,
    bedFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'coastal', tiers: 5 }
  );

  // Additional steeply-dipping angular cliff hogback boulders backing the mainland headland!
  for (let b = 0; b < 5; b++) {
    const u = (b / 4) - 0.5;
    const hx = (-0.50 + u * 0.10) * S;
    const hz = (u * 0.24) * S;
    const hTop = (0.42 + 0.24 * Math.exp(-u * u * 3.0)) * S * rng.range(0.90, 1.12);
    const lf = buildStrikeDipFrame(strikeRad + rng.signed() * 0.14, dipRad + rng.signed() * 0.10);
    const poly = sculptRockTypeBoulder(
      [hx, groundY + hTop * 0.48, hz],
      [S * rng.range(0.09, 0.14), hTop * 0.54, S * rng.range(0.14, 0.20)],
      lf, rng, 'coastal', groundY
    );
    if (poly) {
      const comp = compilePolyBlock(poly, lf, {
        edgeBevel: bevel,
        smoothUnionR: smoothR,
        detailScale: 1.04,
        seedSalt: rng.int(1, 999999),
      });
      if (comp) blocks.push(comp);
    }
  }

  // 2. Angular Wave-Battered Sea-Stack Boulder Tower (Right)
  addBoulderMassifPier(
    blocks,
    0.27 * S, 0.01 * S,
    0.30 * S, 0.48 * S, 0.30 * S,
    bedFrame, rng,
    { groundY, bevel, smoothR, rockStyle: 'coastal', tiers: 5 }
  );

  // 3. Crooked Coastal Boulder Arch Bridge — 100% angular boulders from headland to sea-stack!
  const archH = (0.45 + 0.06 * overhang) * S;
  const ctrlY = archH / 0.75;
  const sp0 = [-0.36 * S, groundY + 0.02 * S, -0.01 * S];
  const sp1 = [-0.29 * S, groundY + ctrlY * 1.12,  0.05 * S];
  const sp2 = [ 0.16 * S, groundY + ctrlY * 0.88, -0.04 * S];
  const sp3 = [ 0.26 * S, groundY + 0.02 * S,  0.01 * S];

  const seaRib = compileCurvedArchRib(sp0, sp1, sp2, sp3, bedFrame, rng, {
    stations: 52,
    rPierIn: S * 0.040,
    rPierOut: S * 0.044,
    rCrownIn: S * 0.019,
    rCrownOut: S * 0.024,
    waistT: 0.58,
    waistPinch: 0.82,
    edgeBevel: S * 0.0022,
    smoothUnionR: smoothR,
    isEolian: false,
    crookedAmp: 1.20,
  });
  curves.push(seaRib);

  buildBoulderArchSpanFromStartToFinish(blocks, seaRib, bedFrame, rng, {
    S,
    groundY,
    rockStyle: 'coastal',
    bevel,
    smoothR,
    spanScale: 0.98,
    leftMassScale: 1.24,
    rightMassScale: 1.06,
    portalCenter: [-0.05 * S, groundY + 0.17 * S, 0],
    minPortalClear: S * 0.145,
  });

  // 4. Detached Jagged Offshore Sentinel Sea-Stack Boulders & Reef Blocks on the Right!
  const reefSpecs = [
    { x: 0.47, z:  0.06, rx: 0.095, ry: 0.19, rz: 0.105 },
    { x: 0.44, z:  0.14, rx: 0.080, ry: 0.11, rz: 0.085 },
    { x: 0.50, z: -0.04, rx: 0.075, ry: 0.09, rz: 0.080 },
  ];
  for (const rs of reefSpecs) {
    const rf = buildStrikeDipFrame(strikeRad + rng.signed() * 0.35, dipRad + rng.signed() * 0.15);
    const nPoly = sculptRockTypeBoulder(
      [rs.x * S, groundY + rs.ry * 0.82 * S, rs.z * S],
      [rs.rx * S, rs.ry * S, rs.rz * S],
      rf, rng, 'coastal', groundY
    );
    if (nPoly) {
      const compN = compilePolyBlock(nPoly, rf, {
        edgeBevel: bevel,
        smoothUnionR: smoothR,
        detailScale: 1.02,
        seedSalt: rng.int(1, 999999),
      });
      if (compN) blocks.push(compN);
    }
  }

  if (addTalusFn) {
    addTalusFn(blocks, bedFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.50) * 1.05,
      scale: S * 0.92,
      spanX: S * 0.92,
      groundY,
      edgeBevel: bevel,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, curves, groundY };
}
