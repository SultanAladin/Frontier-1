/**
 * Procedural 3D Structural Block Assemblers for 18 Geological Formation Archetypes.
 *
 * Uses exact convex polyhedron half-space sculpting, 3D cubic Bézier arch curve sweeping,
 * recursive geological BSP fracture splitting with shear/frost-wedge kinematics,
 * multi-order stratigraphic bedding, and gravity-settled talus cones + fallen boulders.
 */

import {
  DEG2RAD,
  buildStrikeDipFrame,
  rotateAroundAxis,
  perturbNormal,
  vnorm,
  vdot,
  vcross,
  clamp,
} from '../core/vec3.js';
import { scalarField3D } from '../core/spatial-noise.js';
import {
  createBoxPoly,
  clipPolyByHalfSpace,
  splitPolyByPlane,
  measurePoly,
  transformPoly,
  sculptGeologicalParentPoly,
  compilePolyBlock,
  compileEllipsoidBlock,
} from './polyhedron.js';
import {
  compileCurvedArchRib,
  evalCubicBezier3D,
  evalCubicBezierDeriv3D,
  buildDesertArch,
  buildDoubleArch,
  buildSeaArch,
} from './arch-curves.js';
import {
  buildSedimentaryCliff,
  buildSlotCanyon,
  buildHoodooColumns,
  buildBalancedRock,
  buildLimestoneKarst,
  buildExfoliationDome,
} from './sedimentary-formations.js';
import { buildCrackedMonolith } from './cracks-demo.js';
import {
  buildGrandCanyonTemple,
  buildStudlagilColonnade,
  buildIcelandicTuyaCrag,
  buildSaharanYardangFin,
  buildCalvedCliffBoulders,
} from './frontier-formations.js';
import { buildCustomUserSplineArch } from './custom-arch-spline.js';
import { buildArchEncampment } from './encampment.js';
import {
  getFormationSplineSpec,
  compileSplineArcTable,
  areSplinesAtDefault,
  deformMacroFormationBySplines,
} from './formation-splines.js';

function recursiveGeologicalFracture(poly, frame, tectonics, rng, opts, depth = 0, outPolys = []) {
  const m = measurePoly(poly);
  if (!m) return outPolys;

  const maxDepth = opts.maxDepth ?? 3;
  const minSpan = opts.minSpan ?? 0.45;
  const minVol = opts.minVolume ?? 0.08;

  if (depth >= maxDepth || m.minSpan < minSpan || m.volume < minVol) {
    outPolys.push(poly);
    return outPolys;
  }

  let minS = Infinity, maxS = -Infinity;
  let minN = Infinity, maxN = -Infinity;
  let minD = Infinity, maxD = -Infinity;
  for (let i = 0; i < m.verts.length; i++) {
    const v = m.verts[i];
    const ps = vdot(v, frame.strikeVec);
    const pn = vdot(v, frame.normal);
    const pd = vdot(v, frame.dipVec);
    if (ps < minS) minS = ps; if (ps > maxS) maxS = ps;
    if (pn < minN) minN = pn; if (pn > maxN) maxN = pn;
    if (pd < minD) minD = pd; if (pd > maxD) maxD = pd;
  }

  const spanS = maxS - minS;
  const spanN = maxN - minN;
  const spanD = maxD - minD;

  let baseNormal;
  if (spanN > Math.max(spanS, spanD) * (opts.bedSplitBias ?? 0.95)) {
    baseNormal = frame.normal;
  } else if (spanS >= spanD) {
    baseNormal = rng.chance(0.55)
      ? frame.strikeVec
      : rotateAroundAxis(frame.dipVec, frame.normal, (rng.chance(0.5) ? 1 : -1) * 0.95);
  } else {
    baseNormal = rng.chance(0.6)
      ? frame.dipVec
      : rotateAroundAxis(frame.dipVec, frame.normal, (rng.chance(0.5) ? 1 : -1) * 0.95);
  }

  const splitNormal = perturbNormal(baseNormal, rng, opts.angleDispersion ?? 0.14);

  let pMin = Infinity, pMax = -Infinity;
  for (let i = 0; i < m.verts.length; i++) {
    const d = vdot(m.verts[i], splitNormal);
    if (d < pMin) pMin = d;
    if (d > pMax) pMax = d;
  }
  const pSpan = pMax - pMin;
  if (pSpan < minSpan * 1.45) {
    outPolys.push(poly);
    return outPolys;
  }

  const frac = rng.range(0.32, 0.68);
  const splitOffset = pMin + pSpan * frac;

  const openChance = opts.openCleftChance ?? 0.25;
  const isOpenCleft = rng.chance(openChance);
  const gap = isOpenCleft
    ? (opts.openGap ?? 0.06) * rng.range(0.65, 1.45)
    : -(opts.overlapDepth ?? 0.08);

  let [childA, childB] = splitPolyByPlane(poly, splitNormal, splitOffset, gap, 'joint');
  if (!childA || !childB) {
    outPolys.push(poly);
    return outPolys;
  }

  const slipMag = (opts.shearSlip ?? 0.12) * pSpan * rng.range(0.35, 1.0);
  const arb = Math.abs(splitNormal[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
  const tangent1 = vnorm(vcross(splitNormal, arb));
  const tangent2 = vcross(splitNormal, tangent1);
  const slipAngle = rng.float() * Math.PI * 2;
  const slipDir = [
    tangent1[0] * Math.cos(slipAngle) + tangent2[0] * Math.sin(slipAngle),
    -Math.abs(tangent1[1] * Math.cos(slipAngle) + tangent2[1] * Math.sin(slipAngle)) * 0.65,
    tangent1[2] * Math.cos(slipAngle) + tangent2[2] * Math.sin(slipAngle),
  ];

  const tiltAngle = isOpenCleft ? rng.signed() * (opts.openTilt ?? 0.06) : rng.signed() * 0.025;
  childB = transformPoly(
    childB,
    [slipDir[0] * slipMag, slipDir[1] * slipMag, slipDir[2] * slipMag],
    tangent1,
    tiltAngle
  );

  if (opts.groundY !== undefined && opts.groundY !== null) {
    childA = clipPolyByHalfSpace(childA, [0, -1, 0], -opts.groundY, 'base') || childA;
    childB = clipPolyByHalfSpace(childB, [0, -1, 0], -opts.groundY, 'base') || childB;
  }

  const pluckProb = depth >= 1 ? (opts.pluckChance ?? 0.16) : 0;
  const mA = measurePoly(childA);
  const mB = measurePoly(childB);

  if (mA && mB && rng.chance(pluckProb)) {
    const keepPoly = mA.volume >= mB.volume ? childA : childB;
    recursiveGeologicalFracture(keepPoly, frame, tectonics, rng, opts, depth + 1, outPolys);
    return outPolys;
  }

  if (rng.chance(0.82)) {
    recursiveGeologicalFracture(childA, frame, tectonics, rng, opts, depth + 1, outPolys);
  } else {
    outPolys.push(childA);
  }

  if (rng.chance(0.82)) {
    recursiveGeologicalFracture(childB, frame, tectonics, rng, opts, depth + 1, outPolys);
  } else {
    outPolys.push(childB);
  }

  return outPolys;
}

function addTalusAndFallenBoulders(blocks, frame, rng, config) {
  const {
    talusAmount,
    scale,
    spanX,
    frontZFn,
    groundY = 0,
    edgeBevel = 0.025,
    mode = 'cliff',
  } = config;

  if (talusAmount <= 0.04) return;

  if (mode === 'cliff') {
    const numCones = Math.round(4 + talusAmount * 6);
    for (let i = 0; i < numCones; i++) {
      const u = (i + rng.range(0.18, 0.82)) / numCones - 0.5;
      const tx = u * spanX * 0.84;
      const baseZ = frontZFn ? frontZFn(tx) : 0;
      const coneH = scale * rng.range(0.08, 0.19) * (0.55 + 0.70 * talusAmount);
      const coneW = coneH * rng.range(1.15, 1.75);
      const coneRun = coneH * rng.range(1.25, 1.60);

      let wedge = createBoxPoly(
        tx - coneW, groundY, baseZ - coneRun * 0.65,
        tx + coneW, groundY + coneH * 1.15, baseZ + coneRun,
        'talus'
      );
      const reposeAngle = rng.range(33, 39) * DEG2RAD;
      const slopeN = vnorm([rng.signed() * 0.16, Math.cos(reposeAngle), Math.sin(reposeAngle)]);
      const anchor = [tx, groundY + coneH * 0.50, baseZ + coneRun * 0.20];
      wedge = clipPolyByHalfSpace(wedge, slopeN, vdot(slopeN, anchor), 'talusSlope');

      const sideL = vnorm([-0.68, 0.52, 0.50 + rng.signed() * 0.12]);
      const sideR = vnorm([0.68, 0.52, 0.50 + rng.signed() * 0.12]);
      wedge = clipPolyByHalfSpace(wedge, sideL, vdot(sideL, [tx - coneW * 0.35, groundY + coneH * 0.55, baseZ + coneRun * 0.2]), 'talusSide');
      wedge = clipPolyByHalfSpace(wedge, sideR, vdot(sideR, [tx + coneW * 0.35, groundY + coneH * 0.55, baseZ + coneRun * 0.2]), 'talusSide');

      if (wedge) {
        const compiled = compilePolyBlock(wedge, frame, {
          edgeBevel: edgeBevel * 1.3,
          detailScale: 0.55,
          isTalus: true,
          seedSalt: rng.int(1, 999999),
        });
        if (compiled) blocks.push(compiled);
      }
    }
  }

  const numBoulders = Math.round((mode === 'cliff' ? 9 : 6) + talusAmount * (mode === 'cliff' ? 18 : 11));
  for (let i = 0; i < numBoulders; i++) {
    const rMed = scale * (mode === 'cliff' ? 0.042 : 0.058);
    const bSize = clamp(rng.logNormal(rMed, 0.42), scale * 0.025, scale * 0.105);

    let bx, bz, by;
    if (mode === 'cliff') {
      bx = rng.range(-0.43, 0.43) * spanX;
      const fz = frontZFn ? frontZFn(bx) : 0;
      const distOut = bSize * rng.range(0.5, 3.2) + scale * rng.range(0.01, 0.10) * talusAmount;
      bz = fz + distOut;
      by = groundY + bSize * rng.range(0.32, 0.58);
    } else {
      const ang = rng.float() * Math.PI * 2;
      const rad = spanX * rng.range(0.38, 0.66);
      bx = Math.cos(ang) * rad;
      bz = Math.sin(ang) * rad;
      by = groundY + bSize * rng.range(0.28, 0.52);
    }

    const bFrame = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(10, 58) * DEG2RAD);
    const radii = [
      bSize * rng.range(0.88, 1.48),
      bSize * rng.range(0.58, 0.92),
      bSize * rng.range(0.82, 1.32),
    ];

    const poly = sculptGeologicalParentPoly([bx, by, bz], radii, bFrame, rng, {
      jointDispersion: 0.24,
      cornerBlunt: 0.82,
      flatBaseY: groundY,
      extraBevelCuts: 5,
    });

    const compiled = compilePolyBlock(poly, bFrame, {
      edgeBevel: edgeBevel * 0.85,
      detailScale: 0.72,
      isBoulder: true, // Protected from bedrock alcove/arch subtraction cuts!
      seedSalt: rng.int(1, 999999),
    });
    if (compiled) blocks.push(compiled);
  }
}

/**
 * 1. MASSIVE JOINTED CLIFF ESCARPMENT (`cliff-wall`)
 * Monumental buttress-and-gully escarpment with deterministic load-bearing shoulders
 * (zero unsupported end-cantilevers!), fault-stepped beds, overhanging brows & talus apron.
 */
function buildCliffWall(params, tectonics, rng) {
  const S = params.scale;
  const width = S;
  const height = S * 0.66;
  const depth = S * 0.38;
  const groundY = 0;

  const blocks = [];
  const cuts = [];

  const spineSalt = rng.int(1, 99999);
  function spineZ(x) {
    const u = x / width;
    const headland = scalarField3D(u * 2.8, 0.5, 0.5, spineSalt) * depth * 0.34;
    const buttress = scalarField3D(u * 6.2, 1.7, 0.5, spineSalt + 31) * depth * 0.14;
    return headland + buttress;
  }

  // Deterministic bottom-up crestline envelope: tallest on asymmetric main headland, tapering cleanly at shoulders!
  const peakShift = rng.range(-0.18, 0.18);
  function crestHeightAt(x) {
    const u = x / (width * 0.5); // -1 .. +1
    const shoulderTaper = Math.max(0.28, 1.0 - Math.max(0, Math.abs(u) - 0.62) * 1.55);
    const crestWave = 0.88 + 0.18 * scalarField3D((u - peakShift) * 1.9, 2.3, 0.5, spineSalt + 99);
    return height * shoulderTaper * crestWave;
  }

  const tiers = [];
  let curY = groundY;
  const numTargetTiers = Math.round(5 + params.jointDensity * 5);
  const avgH = height / numTargetTiers;

  while (curY < height * 0.98) {
    const isMassive = rng.chance(0.60);
    const h = clamp(
      avgH * (isMassive ? rng.range(1.10, 1.75) : rng.range(0.50, 0.88)),
      height * 0.08,
      height * 0.30
    );
    const competency = isMassive ? rng.range(0.68, 1.0) : rng.range(0.14, 0.46);
    tiers.push({ y0: curY, y1: Math.min(height * 1.08, curY + h), competency });
    curY += h * 0.88;
  }

  const overhangAmt = params.overhangStrength ?? 0.68;
  const aperture = params.jointAperture ?? 0.46;
  const bevel = S * (0.0018 + 0.005 * (params.edgeWear ?? 0.34));

  for (let ti = 0; ti < tiers.length; ti++) {
    const tier = tiers[ti];
    const yMid = 0.5 * (tier.y0 + tier.y1);
    const tierH = tier.y1 - tier.y0;
    const heightFrac = yMid / height;

    const overhangBias = (tier.competency - 0.42) * depth * 0.36 * overhangAmt +
      (heightFrac - 0.32) * depth * 0.16 * overhangAmt;

    const avgSegW = clamp((width / (4 + params.jointDensity * 5.5)) * (0.72 + 0.55 * tier.competency), width * 0.08, width * 0.26);
    let curX = -width * 0.47;

    while (curX < width * 0.47) {
      const segW = clamp(avgSegW * rng.range(0.72, 1.38), width * 0.08, width * 0.28);
      const xMid = curX + segW * 0.5;
      const nextX = curX + segW;

      const localCrest = crestHeightAt(xMid);
      if (tier.y0 > localCrest) {
        curX = nextX;
        continue;
      }

      const faultDmg = tectonics.evalFaultDamage(xMid, yMid, 0);
      const shift = tectonics.evalTectonicShift(xMid, yMid, 0);

      // Recess blocks in fault gullies instead of deleting whole columns on end shoulders
      const isInterior = Math.abs(xMid) < width * 0.34;
      const pluckRecess = (isInterior && ti > 0 && ti < tiers.length - 1 && rng.chance((params.pluckingAmount ?? 0.5) * 0.28))
        ? depth * rng.range(0.14, 0.28)
        : 0;

      const fz = spineZ(xMid) + overhangBias - faultDmg * depth * 0.20 - pluckRecess + shift[2];
      const backZ = -depth * rng.range(0.46, 0.62);
      const zMid = 0.5 * (fz + backZ);
      const segD = Math.max(depth * 0.24, (fz - backZ) * 0.56);

      const localFrame = tectonics.localFrameAt(xMid, yMid, zMid);
      const by0 = Math.max(groundY, tier.y0 + shift[1]);
      const by1 = Math.min(localCrest + tierH * 0.25, Math.max(by0 + tierH * 0.55, tier.y1 + shift[1]));

      const parent = sculptGeologicalParentPoly(
        [xMid + shift[0] * 0.35, 0.5 * (by0 + by1), zMid],
        [segW * 0.62, (by1 - by0) * 0.64, segD],
        localFrame,
        rng,
        {
          jointDispersion: 0.16 + faultDmg * 0.12,
          cornerBlunt: 0.76,
          flatBaseY: ti === 0 ? groundY : null,
          extraBevelCuts: 4,
        }
      );

      const subPolys = recursiveGeologicalFracture(parent, localFrame, tectonics, rng, {
        maxDepth: faultDmg > 0.35 ? 3 : 2,
        minSpan: S * 0.048,
        minVolume: (S * 0.04) ** 3,
        openCleftChance: 0.16 * aperture,
        openGap: S * 0.007 * (0.5 + aperture),
        overlapDepth: S * 0.018,
        shearSlip: 0.11 + 0.09 * overhangAmt,
        pluckChance: isInterior ? 0.18 * (params.pluckingAmount ?? 0.5) : 0,
        groundY: ti === 0 ? groundY : null,
      });

      for (let k = 0; k < subPolys.length; k++) {
        const comp = compilePolyBlock(subPolys[k], localFrame, {
          edgeBevel: bevel,
          detailScale: 0.85 + 0.28 * tier.competency,
          seedSalt: rng.int(1, 999999),
        });
        if (comp) blocks.push(comp);
      }

      curX = nextX - segW * rng.range(0.08, 0.18);
    }
  }

  // Carve 2-3 concave plucked niches strictly on the central front face
  const numCuts = Math.round(2 + (params.pluckingAmount ?? 0.5) * 3);
  for (let i = 0; i < numCuts; i++) {
    const cx = rng.range(-0.28, 0.28) * width;
    const cy = rng.range(0.18, 0.62) * crestHeightAt(cx);
    const cz = spineZ(cx) + depth * rng.range(0.10, 0.24);
    const cFrame = tectonics.localFrameAt(cx, cy, cz);
    const cutPoly = sculptGeologicalParentPoly(
      [cx, cy, cz],
      [width * rng.range(0.05, 0.10), height * rng.range(0.06, 0.13), depth * rng.range(0.12, 0.22)],
      cFrame,
      rng,
      { jointDispersion: 0.18, cornerBlunt: 0.74, extraBevelCuts: 2 }
    );
    const compCut = compilePolyBlock(cutPoly, cFrame, {
      edgeBevel: bevel * 0.6,
      seedSalt: rng.int(1, 999999),
    });
    if (compCut) cuts.push(compCut);
  }

  addTalusAndFallenBoulders(blocks, tectonics.baseFrame, rng, {
    talusAmount: params.talusAmount ?? 0.62,
    scale: S,
    spanX: width,
    frontZFn: (x) => spineZ(x) + depth * 0.08,
    groundY,
    edgeBevel: bevel,
    mode: 'cliff',
  });

  return { blocks, cuts, groundY };
}

/**
 * 2. FROST-SHATTERED ALPINE CRAG & SPIRE (`alpine-crag`)
 * Chamonix Aiguille / Tre Cime / Matterhorn jagged monolithic alpine horn & arête ridge:
 * - Every spire, gendarme needle, arête fin, and stepped buttress rib extends CONTINUOUSLY
 *   down to `groundY` (ZERO horizontal bandsaw masonry slices and ZERO stacked Cairn blocks!)
 * - Overlapping inner towering aiguille horns + mid-height snow-ledge buttress shoulders +
 *   flared lower dihedral spurs separated by a deep V-couloir gully!
 */
function buildAlpineCrag(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0012 + 0.0030 * (params.edgeWear ?? 0.24));

  const mainDip = clamp((params.dipAngle ?? 64), 48, 82) * DEG2RAD;
  const mainStrike = (params.strikeAngle ?? 18) * DEG2RAD;
  const mainFrame = buildStrikeDipFrame(mainStrike, mainDip);
  const overhang = params.overhangStrength ?? 0.58;
  const aperture = params.jointAperture ?? 0.44;
  const cosR = Math.cos(mainStrike), sinR = Math.sin(mainStrike);

  // Monolithic grounded alpine needles & arête gendarmes: each tower has a soaring inner needle
  // PLUS 1-2 lower projecting grounded buttress spurs so ledges step outward naturally without horizontal cuts!
  const spires = [
    { u: -0.04, v: -0.02, h: 1.06, ru: 0.145, rv: 0.125, lean:  0.11, spurs: 2 }, // Primary soaring Matterhorn/Dru horn
    { u:  0.15, v:  0.03, h: 0.86, ru: 0.125, rv: 0.112, lean: -0.08, spurs: 2 }, // Leaning twin needle across V-couloir!
    { u: -0.19, v: -0.01, h: 0.78, ru: 0.130, rv: 0.115, lean:  0.09, spurs: 2 }, // West arête shoulder spire
    { u:  0.30, v: -0.02, h: 0.64, ru: 0.118, rv: 0.105, lean: -0.07, spurs: 1 }, // East arête gendarme
    { u: -0.33, v:  0.03, h: 0.56, ru: 0.115, rv: 0.102, lean:  0.07, spurs: 1 }, // Lower west saw-tooth gendarme
    { u: -0.09, v:  0.15, h: 0.58, ru: 0.105, rv: 0.115, lean:  0.06, spurs: 1 }, // South couloir buttress rib W
    { u:  0.12, v:  0.15, h: 0.46, ru: 0.098, rv: 0.108, lean: -0.05, spurs: 1 }, // South couloir buttress rib E
    { u:  0.02, v: -0.17, h: 0.70, ru: 0.122, rv: 0.115, lean:  0.08, spurs: 1 }, // North-wall sheer buttress
    { u: -0.44, v:  0.06, h: 0.36, ru: 0.098, rv: 0.090, lean:  0.05, spurs: 0 }, // Outer west arête tooth
    { u:  0.42, v: -0.04, h: 0.38, ru: 0.096, rv: 0.090, lean: -0.05, spurs: 0 }, // Outer east arête tooth
  ];

  function addMonolithicNeedle(cx, cz, h, ru, rv, leanVec, isHorn, seedIdx) {
    const [lx, lz] = leanVec;
    const maxR = Math.max(ru, rv) * 1.9 + Math.hypot(lx, lz);
    let poly = createBoxPoly(
      cx - maxR, groundY, cz - maxR,
      cx + maxR, groundY + h * 1.14, cz + maxR,
      'aiguille'
    );

    // 6-to-7 steep dihedral rock walls leaning along [lx, lz] from groundY to peak!
    // Outer buttresses lean INWARD toward the summit massif so the mountain base flares naturally!
    const nFaces = 6 + (seedIdx & 1);
    const az0 = mainStrike + seedIdx * 0.59;
    const dxdy = lx / Math.max(S * 0.1, h);
    const dzdy = lz / Math.max(S * 0.1, h);

    for (let f = 0; f < nFaces; f++) {
      const a = az0 + (f * Math.PI * 2) / nFaces + rng.signed() * 0.18;
      const hx = Math.cos(a), hz = Math.sin(a);
      const da = a - mainStrike;
      const rad = (1.0 / Math.hypot(Math.cos(da) / ru, Math.sin(da) / rv)) * rng.range(0.80, 1.02);
      const taper = rng.range(0.14, 0.28);
      // Wall normal tilts with the spire's lean axis + upward taper!
      const ny = taper - (hx * dxdy + hz * dzdy);
      const fn = vnorm([hx, ny, hz]);
      const midPt = [
        cx + lx * 0.42,
        groundY + h * 0.42,
        cz + lz * 0.42,
      ];
      poly = clipPolyByHalfSpace(poly, fn, vdot(fn, midPt) + rad, 'alpineWall') || poly;
    }

    const apexPt = [
      cx + lx * 0.94,
      groundY + h * 0.97,
      cz + lz * 0.94,
    ];
    if (isHorn) {
      const cAz = mainStrike + Math.PI * 0.5 + rng.signed() * 0.24;
      const pN1 = vnorm([Math.cos(cAz) * 0.78, 0.62, Math.sin(cAz) * 0.78]);
      const pN2 = vnorm([-Math.cos(cAz + 0.36) * 0.76, 0.65, -Math.sin(cAz + 0.36) * 0.76]);
      const pN3 = vnorm([Math.cos(mainStrike) * (seedIdx % 2 === 0 ? 0.68 : -0.68), 0.73, Math.sin(mainStrike) * (seedIdx % 2 === 0 ? 0.68 : -0.68)]);
      poly = clipPolyByHalfSpace(poly, pN1, vdot(pN1, apexPt), 'hornCrest') || poly;
      poly = clipPolyByHalfSpace(poly, pN2, vdot(pN2, apexPt) + rv * 0.14, 'hornCrest') || poly;
      poly = clipPolyByHalfSpace(poly, pN3, vdot(pN3, apexPt) + ru * 0.20, 'hornEnd') || poly;
    } else {
      const sAz = Math.atan2(-lz, -lx) + rng.signed() * 0.35;
      const shelfN = vnorm([Math.cos(sAz) * 0.72, 0.69, Math.sin(sAz) * 0.72]);
      poly = clipPolyByHalfSpace(poly, shelfN, vdot(shelfN, apexPt), 'shoulderLedge') || poly;
    }
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;

    const localF = buildStrikeDipFrame(mainStrike + rng.signed() * 0.10, mainDip + rng.signed() * 0.08);
    const comp = compilePolyBlock(poly, localF, {
      edgeBevel: bevel,
      smoothUnionR: S * 0.005,
      detailScale: 1.06,
      seedSalt: rng.int(1, 999999),
    });
    if (comp) blocks.push(comp);
  }

  let seedCounter = 1;
  for (let i = 0; i < spires.length; i++) {
    const sp = spires[i];
    const bx = (sp.u * cosR - sp.v * sinR) * S + rng.signed() * S * 0.010;
    const bz = (sp.u * sinR + sp.v * cosR) * S + rng.signed() * S * 0.010;
    const h = sp.h * S * rng.range(0.94, 1.06);
    const ru = sp.ru * S * rng.range(0.92, 1.12);
    const rv = sp.rv * S * rng.range(0.88, 1.06);

    // Central horn overhangs forward; flanking buttresses lean inward toward the summit pyramid!
    const headwallLean = sp.lean * S * (0.7 + 0.5 * overhang);
    const inwardPull = i === 0 ? 0 : 0.36;
    const leanVec = [
      (-sinR) * headwallLean - bx * inwardPull,
      cosR * headwallLean - bz * inwardPull,
    ];

    addMonolithicNeedle(bx, bz, h, ru, rv, leanVec, true, seedCounter++);

    // Splayed basal buttress ridges flaring outward onto the mountain apron (eliminates vertical organ-pipe look!)
    for (let s = 0; s < sp.spurs; s++) {
      const outDirAng = (i === 0 ? mainStrike + (s === 0 ? 1.45 : -1.45) : Math.atan2(bz, bx)) + rng.signed() * 0.42;
      const baseOutDist = (0.62 + 0.34 * s) * Math.max(ru, rv);
      const spurBx = bx + Math.cos(outDirAng) * baseOutDist;
      const spurBz = bz + Math.sin(outDirAng) * baseOutDist;
      const spurH = h * (s === 0 ? rng.range(0.52, 0.65) : rng.range(0.28, 0.42));
      const spurLean = [
        (bx - spurBx) * 0.68,
        (bz - spurBz) * 0.68,
      ];
      addMonolithicNeedle(
        spurBx,
        spurBz,
        spurH,
        ru * rng.range(0.78, 0.98),
        rv * rng.range(0.72, 0.92),
        spurLean,
        s === 0 && rng.chance(0.45),
        seedCounter++
      );
    }
  }

  // Deep frost-riven couloir slot cleft slicing cleanly between main horn and right gendarme!
  const couloirW = S * (0.010 + 0.014 * aperture);
  const couloirPoly = sculptGeologicalParentPoly(
    [(0.055 * cosR) * S, groundY + 0.58 * S, (0.055 * sinR) * S],
    [couloirW, 0.38 * S, 0.24 * S],
    mainFrame,
    rng,
    { jointDispersion: 0.05, cornerBlunt: 0.78 }
  );
  const compCouloir = compilePolyBlock(couloirPoly, mainFrame, { edgeBevel: S * 0.002 });
  if (compCouloir) cuts.push(compCouloir);

  addTalusAndFallenBoulders(blocks, mainFrame, rng, {
    talusAmount: (params.talusAmount ?? 0.48) * 0.95,
    scale: S * 0.90,
    spanX: S * 0.72,
    groundY,
    edgeBevel: bevel,
    mode: 'outcrop',
  });

  return { blocks, cuts, groundY };
}

/**
 * 3. COLUMNAR BASALT ESCARPMENT (`basalt-columns`)
 */
function buildBasaltColumns(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const colSpacing = S * clamp(0.118 - 0.042 * (params.jointDensity ?? 0.7), 0.062, 0.115);
  const openGap = colSpacing * (0.025 + 0.060 * (params.jointAperture ?? 0.38));
  const bevel = colSpacing * (0.03 + 0.07 * (params.edgeWear ?? 0.2));

  const nx = Math.round((S * 0.84) / colSpacing);
  const nz = Math.round((S * 0.54) / (colSpacing * 0.866));
  const sites = [];

  for (let iz = -1; iz <= nz + 1; iz++) {
    for (let ix = -1; ix <= nx + 1; ix++) {
      const rowShift = (iz & 1) ? 0.5 : 0.0;
      const sx = ((ix + rowShift) - nx * 0.5) * colSpacing + rng.signed() * colSpacing * 0.24;
      const sz = (iz - nz * 0.5) * colSpacing * 0.866 + rng.signed() * colSpacing * 0.24;
      sites.push({ x: sx, z: sz, ix, iz, active: ix >= 0 && ix <= nx && iz >= 0 && iz <= nz });
    }
  }

  const tiltRad = clamp((params.dipAngle ?? 6) * DEG2RAD, 0, 0.32);
  const colFrame = buildStrikeDipFrame((params.strikeAngle ?? 0) * DEG2RAD, tiltRad);
  const profSalt = rng.int(1, 99999);

  for (let si = 0; si < sites.length; si++) {
    const site = sites[si];
    if (!site.active) continue;

    const ux = site.x / (S * 0.44);
    const uz = site.z / (S * 0.27);
    const radialMask = 1.0 - 0.52 * ux * ux - 0.22 * Math.max(0, uz);
    const bayCurve = scalarField3D(ux * 1.9, 0.5, uz * 1.9, profSalt) * 0.34;
    const rawHeightFrac = radialMask - uz * 0.44 + bayCurve;

    if (rawHeightFrac < 0.09) continue;

    const stepUnit = colSpacing * rng.range(0.75, 1.35);
    const colTop = Math.max(
      colSpacing * 0.85,
      Math.round((clamp(rawHeightFrac, 0.1, 1.0) * S * 0.62) / stepUnit) * stepUnit + rng.signed() * stepUnit * 0.14
    );

    const isFrontBrow = uz > -0.10 && uz < 0.35 && colTop > S * 0.30;
    const pluckBottom = isFrontBrow && rng.chance(0.32 * (params.overhangStrength ?? 0.5) + 0.22 * (params.pluckingAmount ?? 0.5));
    const colBase = pluckBottom ? clamp(colTop * rng.range(0.30, 0.56), colSpacing, colTop - colSpacing * 1.2) : groundY;

    // Deep interior columns fuse slightly below top tier to eliminate hidden internal faces (2x faster & cleaner!)
    const isDeepInterior = uz < -0.25 && Math.abs(ux) < 0.55;
    const localGap = isDeepInterior ? -colSpacing * 0.015 : openGap;

    const maxR = colSpacing * 0.95;
    let prism = createBoxPoly(
      site.x - maxR, colBase, site.z - maxR,
      site.x + maxR, colTop, site.z + maxR,
      'column'
    );

    for (let nj = 0; nj < sites.length; nj++) {
      if (nj === si) continue;
      const nb = sites[nj];
      const dx = nb.x - site.x;
      const dz = nb.z - site.z;
      const dist = Math.hypot(dx, dz);
      if (dist > colSpacing * 2.25 || dist < 1e-5) continue;

      const wnx = dx / dist;
      const wnz = dz / dist;
      const midX = 0.5 * (site.x + nb.x) - wnx * localGap * 0.5;
      const midZ = 0.5 * (site.z + nb.z) - wnz * localGap * 0.5;
      const wallN = tiltRad > 1e-3
        ? rotateAroundAxis([wnx, 0, wnz], colFrame.strikeVec, tiltRad)
        : [wnx, 0, wnz];
      const off = wallN[0] * midX + wallN[1] * (0.5 * (colBase + colTop)) + wallN[2] * midZ;
      prism = clipPolyByHalfSpace(prism, wallN, off, 'colFace');
      if (!prism) break;
    }
    if (!prism) continue;

    const topN = perturbNormal([0, 1, 0], rng, 0.16);
    prism = clipPolyByHalfSpace(prism, topN, topN[0] * site.x + topN[1] * colTop + topN[2] * site.z, 'crossJoint');
    if (pluckBottom && prism) {
      const botN = perturbNormal([0, -1, 0], rng, 0.18);
      prism = clipPolyByHalfSpace(prism, botN, botN[0] * site.x + botN[1] * colBase + botN[2] * site.z, 'crossJoint');
    }
    if (!prism) continue;

    const totalH = colTop - colBase;
    const numSegs = isDeepInterior ? 1 : Math.max(1, Math.min(3, Math.floor(totalH / (colSpacing * 2.0))));
    let current = prism;
    const segPolys = [];

    for (let s = 1; s < numSegs && current; s++) {
      const splitY = colBase + (totalH * s) / numSegs + rng.signed() * colSpacing * 0.2;
      const cjNormal = perturbNormal([0, 1, 0], rng, 0.12);
      const cjOffset = cjNormal[0] * site.x + cjNormal[1] * splitY + cjNormal[2] * site.z;
      const [lower, upper] = splitPolyByPlane(current, cjNormal, cjOffset, -colSpacing * 0.04, 'crossJoint');
      if (lower) segPolys.push(lower);
      current = upper
        ? transformPoly(upper, [rng.signed() * colSpacing * 0.06, 0, rng.signed() * colSpacing * 0.06])
        : null;
    }
    if (current) segPolys.push(current);

    for (let k = 0; k < segPolys.length; k++) {
      const comp = compilePolyBlock(segPolys[k], colFrame, {
        edgeBevel: bevel,
        detailScale: 0.45,
        seedSalt: rng.int(1, 999999),
      });
      if (comp) blocks.push(comp);
    }
  }

  const numDrums = Math.round(6 + (params.talusAmount ?? 0.55) * 14);
  for (let i = 0; i < numDrums; i++) {
    const drumR = colSpacing * rng.range(0.42, 0.56);
    const drumLen = colSpacing * rng.range(0.55, 1.25);
    const dx = rng.range(-0.38, 0.38) * S;
    const dz = S * rng.range(0.14, 0.33);
    const dy = groundY + drumR * rng.range(0.55, 0.85);

    let drum = createBoxPoly(-drumR * 1.4, -drumLen, -drumR * 1.4, drumR * 1.4, drumLen, drumR * 1.4, 'drum');
    const sides = rng.pick([5, 6, 6, 6, 7]);
    const phase = rng.float() * Math.PI * 2;
    for (let s = 0; s < sides; s++) {
      const a = phase + (s * Math.PI * 2) / sides + rng.signed() * 0.08;
      drum = clipPolyByHalfSpace(drum, [Math.cos(a), 0, Math.sin(a)], drumR * rng.range(0.92, 1.06), 'colFace');
    }
    drum = clipPolyByHalfSpace(drum, perturbNormal([0, 1, 0], rng, 0.16), drumLen * 0.5, 'crossJoint');
    drum = clipPolyByHalfSpace(drum, perturbNormal([0, -1, 0], rng, 0.16), drumLen * 0.5, 'crossJoint');

    if (drum) {
      const axis = rng.unitVec3();
      drum = transformPoly(drum, [dx, dy, dz], axis, rng.range(0.8, 1.65), [0, 0, 0]);
      drum = clipPolyByHalfSpace(drum, [0, -1, 0], -groundY, 'base') || drum;
      const dFrame = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(20, 75) * DEG2RAD);
      const comp = compilePolyBlock(drum, dFrame, {
        edgeBevel: bevel,
        detailScale: 0.45,
        isBoulder: true,
        seedSalt: rng.int(1, 999999),
      });
      if (comp) blocks.push(comp);
    }
  }

  return { blocks, cuts, groundY };
}

/**
 * 4. WEATHERED GRANITE TOR & CORESTONES (`granite-tor`)
 */
function buildGraniteTor(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const bevel = S * (0.004 + 0.011 * (params.edgeWear ?? 0.58));
  const numStacks = 3;

  for (let st = 0; st < numStacks; st++) {
    const isPrimary = st === 0;
    const sx = isPrimary ? rng.signed() * S * 0.04 : (st === 1 ? -1 : 1) * S * rng.range(0.18, 0.28);
    const sz = isPrimary ? -S * 0.03 : rng.range(-0.14, 0.16) * S;
    const stackW = isPrimary ? S * 0.30 : S * rng.range(0.18, 0.24);
    const stackD = isPrimary ? S * 0.24 : S * rng.range(0.16, 0.22);
    const numTiers = isPrimary ? rng.int(3, 4) : rng.int(2, 3);

    let curY = groundY;
    for (let t = 0; t < numTiers; t++) {
      const tierH = S * (t === 0 ? rng.range(0.16, 0.23) : rng.range(0.11, 0.18));
      const shrink = 1.0 - t * 0.12 + (t === numTiers - 1 ? 0.08 * (params.overhangStrength ?? 0.6) : 0);
      const cx = sx + rng.signed() * stackW * 0.14;
      const cz = sz + rng.signed() * stackD * 0.14;
      const cy = curY + tierH * 0.5;

      const localFrame = tectonics.localFrameAt(cx, cy, cz);
      const poly = sculptGeologicalParentPoly(
        [cx, cy, cz],
        [stackW * shrink, tierH * 0.58, stackD * shrink],
        localFrame,
        rng,
        {
          jointDispersion: 0.14,
          cornerBlunt: 0.84,
          flatBaseY: t === 0 ? groundY : null,
          extraBevelCuts: 4,
        }
      );

      const corestones = recursiveGeologicalFracture(poly, localFrame, tectonics, rng, {
        maxDepth: 2,
        minSpan: S * 0.07,
        minVolume: (S * 0.05) ** 3,
        bedSplitBias: 1.5,
        openCleftChance: 0.48 * (params.jointAperture ?? 0.6),
        openGap: S * 0.014 * (0.5 + (params.jointAperture ?? 0.6)),
        overlapDepth: S * 0.012,
        shearSlip: 0.06,
        pluckChance: t > 0 ? 0.24 * (params.pluckingAmount ?? 0.48) : 0,
        groundY: t === 0 ? groundY : null,
      });

      for (let k = 0; k < corestones.length; k++) {
        const comp = compilePolyBlock(corestones[k], localFrame, {
          edgeBevel: bevel * rng.range(0.75, 1.25),
          detailScale: 0.85,
          seedSalt: rng.int(1, 999999),
        });
        if (comp) blocks.push(comp);
      }

      curY += tierH * 0.88;
    }
  }

  addTalusAndFallenBoulders(blocks, tectonics.baseFrame, rng, {
    talusAmount: params.talusAmount ?? 0.45,
    scale: S,
    spanX: S * 0.66,
    groundY,
    edgeBevel: bevel * 0.75,
    mode: 'outcrop',
  });

  return { blocks, cuts, groundY };
}

/**
 * 5. FOLIATED METAMORPHIC HOGBACK / THRUST SLABS (`metamorphic-outcrop`)
 * Flatirons / Vasquez Rocks / Highland Schist monumental steeply-dipping hogback blades:
 * - 8 long, sweeping, parallel steeply-dipping (`48-58°`) foliated stone fin ridges
 *   (ZERO tiny dragon-scale shingles and ZERO rectangular floorboard tiles!)
 * - Each fin ridge rises to a jagged saw-toothed cuesta crest with overhanging anti-dip
 *   scarp faces, carved cleanly by 2 oblique cross-joint gullies!
 */
/**
 * 5. FOLIATED SCHIST HOGBACK (`metamorphic-outcrop`)
 * Steeply dipping Flatirons-style mica-schist & quartzite cuesta ridge driven by its Hogback Strike Spine:
 * - Long, planar steeply-dipping foliation slabs sweep continuously from grounded dip-slope apron
 *   up to razor-sharp sawtooth hogback prows (zero rounded pita-disc stacks!).
 * - Truncated anti-dip scarp side exposes jagged overhanging chevron foliation steps, differential
 *   quartz-rich ribs, and oblique cross-strike fault couloirs.
 */
function buildMetamorphicOutcrop(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const activeSplines = Array.isArray(params.formationSplines) && params.formationSplines.length >= 1
    ? params.formationSplines
    : getFormationSplineSpec('metamorphic-outcrop', S).defaultCurves;
  const spine = compileSplineArcTable(activeSplines[0], 72);
  const strikeLen = Math.max(S * 0.65, spine.totalHorizLength);

  const dipRad = clamp((params.dipAngle ?? 50), 42, 62) * DEG2RAD;
  const overhang = params.overhangStrength ?? 0.78;
  const bevel = S * (0.0008 + 0.0018 * (params.edgeWear ?? 0.22));
  const salt = rng.int(1, 999999);
  const cosD = Math.cos(dipRad);
  const sinD = Math.sin(dipRad);

  // Monumental Colorado Flatirons / Vasquez Rocks cuesta architecture:
  // Planar 50°-dipping foliated schist/quartzite slabs face +N(u) (front dip-slope),
  // sweeping up-dip from the grounded front toe apron (`+N`) up to towering razor-edged
  // Flatiron prows (`-N`) backed by a sheer overhanging anti-dip truncation scarp!
  const nIrons = clamp(Math.round(strikeLen / (S * 0.28)), 3, 7);
  const nSheets = 5; // 5 shingled parallel foliation sheets from front apron to cresting master Flatirons!

  for (let p = 0; p < nSheets; p++) {
    const pFrac = p / (nSheets - 1); // 0 = lower front shingled apron sheet .. 1 = rear master Flatiron sheet
    const sheetThick = S * (p % 2 === 0 ? 0.056 : 0.044);
    const hardness = p % 2 === 0 ? 0.92 : 0.76;

    // Each sheet is divided along strike by oblique cross-fault couloirs into bold asymmetric Flatiron blades
    const stagger = (p % 2 === 1 ? 0.05 : -0.03) + scalarField3D(p * 2.1, 0.7, 0.4, salt) * 0.03;
    const uSpan = 0.94 / nIrons;

    for (let k = 0; k < nIrons; k++) {
      const u0 = clamp(0.03 + k * uSpan + stagger * 0.4, 0.01, 0.94);
      const u1 = clamp(0.03 + (k + 1) * uSpan + stagger * 0.4, u0 + 0.08, 0.99);
      const uMid = 0.5 * (u0 + u1);
      const st = spine.sampleAtArc(uMid);
      const su = (uMid - 0.5) * 2.0;

      // Center Flatirons tower highest; outer shoulder irons step down naturally
      const ridgeEnv = 1.0 - 0.34 * su * su;
      // Front shingled sheets (`p=0,1`) form stepped lower Flatiron aprons; rear sheets (`p=2,3,4`) soar to summit prows!
      const sheetHeightMul = 0.44 + 0.62 * Math.pow(pFrac, 0.72);
      const ironVar = 0.92 + 0.16 * (0.5 + 0.5 * Math.sin(k * 2.3 + p * 1.1));
      const crestH = clamp(st.y * 1.12 * ridgeEnv * sheetHeightMul * ironVar, S * 0.16, S * 0.88);

      const localStrikeAz = Math.atan2(st.tz, st.tx) + rng.signed() * 0.025;
      const localDip = dipRad + rng.signed() * 0.03;
      const finFrame = buildStrikeDipFrame(localStrikeAz + Math.PI * 0.5, localDip);
      const sDip = Math.sin(localDip);
      const cDip = Math.cos(localDip);

      // Shingle normal offset: front sheets (`p=0`) sit forward (+N), rear sheets (`p=4`) stack underneath/behind (-N)
      const crestNormalOff = (0.06 - pFrac * 0.28) * S * st.r;
      const tipX = st.x + st.nx * crestNormalOff;
      const tipZ = st.z + st.nz * crestNormalOff;

      // Down-dip run from crest (`y = crestH`) down to ground (`y = 0`) along `+N(u)`!
      const downDipRun = crestH / Math.max(0.55, Math.tan(localDip));
      const midNormalOff = crestNormalOff + downDipRun * 0.45;
      const cx = st.x + st.nx * midNormalOff;
      const cz = st.z + st.nz * midNormalOff;

      // Subdivide wide Flatirons subtly along curved spine so curved splines bend smoothly without gaps
      const ironArcLen = Math.max(S * 0.10, (u1 - u0) * strikeLen);
      const halfW = ironArcLen * 0.52;
      const reachR = Math.max(halfW, downDipRun * 0.85) + sheetThick * 2.5;

      let poly = createBoxPoly(
        cx - reachR,
        groundY,
        cz - reachR,
        cx + reachR,
        crestH * 1.25,
        cz + reachR,
        'flatironSlab'
      );

      // 1) Planar steeply-dipping Front Foliation Dip-Slope (`+N` & `+Y` normal)!
      const folTopN = vnorm([st.nx * sDip, cDip, st.nz * sDip]);
      const folBotN = [-folTopN[0], -folTopN[1], -folTopN[2]];
      const tipPt = [tipX, groundY + crestH, tipZ];
      const thickBacking = p === nSheets - 1 ? sheetThick * 2.4 : sheetThick * 1.35;
      const botPt = [
        tipPt[0] - folTopN[0] * thickBacking,
        tipPt[1] - folTopN[1] * thickBacking,
        tipPt[2] - folTopN[2] * thickBacking,
      ];
      poly = clipPolyByHalfSpace(poly, folTopN, vdot(folTopN, tipPt), 'dipSlopeTop') || poly;
      poly = clipPolyByHalfSpace(poly, folBotN, vdot(folBotN, botPt), 'dipSlopeBot') || poly;

      // 2) Sheer Overhanging Anti-Dip Scarp on the `-N(u)` crest back!
      const scarpLean = -0.14 - 0.18 * overhang;
      const antiDipN = vnorm([-st.nx * 0.94, scarpLean, -st.nz * 0.94]);
      poly = clipPolyByHalfSpace(poly, antiDipN, vdot(antiDipN, tipPt), 'antiDipScarp') || poly;

      // 3) Asymmetric Oblique Conjugate Fault Planes converging upward into bold trapezoidal/angled Flatiron prows!
      const prowBevelL = 0.22 + 0.10 * ((k + p) % 2);
      const prowBevelR = 0.28 - 0.08 * ((k + p) % 2);
      const leftJ = vnorm([
        -st.tx * Math.cos(prowBevelL) - st.nx * Math.sin(prowBevelL) * 0.30,
        0.22 + (k % 2) * 0.05,
        -st.tz * Math.cos(prowBevelL) - st.nz * Math.sin(prowBevelL) * 0.30,
      ]);
      const rightJ = vnorm([
        st.tx * Math.cos(prowBevelR) - st.nx * Math.sin(prowBevelR) * 0.30,
        0.19 + ((k + 1) % 2) * 0.05,
        st.tz * Math.cos(prowBevelR) - st.nz * Math.sin(prowBevelR) * 0.30,
      ]);
      const leftAnchor = [cx - st.tx * halfW * 1.04, groundY + crestH * 0.42, cz - st.tz * halfW * 1.04];
      const rightAnchor = [cx + st.tx * halfW * 1.04, groundY + crestH * 0.42, cz + st.tz * halfW * 1.04];
      poly = clipPolyByHalfSpace(poly, leftJ, vdot(leftJ, leftAnchor), 'flatironProwL') || poly;
      poly = clipPolyByHalfSpace(poly, rightJ, vdot(rightJ, rightAnchor), 'flatironProwR') || poly;

      // 4) Oblique rocky cuesta summit ridge truncation (prevents artificial needle-point 2D triangles!)
      const crestTilt = ((k + p) % 2 === 0 ? 1 : -1) * (0.18 + 0.12 * (k % 3));
      const summitN = vnorm([
        st.tx * crestTilt - st.nx * 0.28,
        0.94,
        st.tz * crestTilt - st.nz * 0.28,
      ]);
      const summitPt = [tipPt[0], groundY + crestH * (p <= 1 ? 0.88 : 0.94), tipPt[2]];
      poly = clipPolyByHalfSpace(poly, summitN, vdot(summitN, summitPt), 'cuestaSummit') || poly;

      poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
      if (!poly) continue;
      const m = measurePoly(poly);
      if (!m || m.volume < S * S * S * 0.00005) continue;

      const comp = compilePolyBlock(poly, finFrame, {
        edgeBevel: bevel * (hardness > 0.85 ? 0.85 : 1.1),
        smoothUnionR: 0,
        detailScale: 1.05,
        hardness,
        seedSalt: (salt + p * 193 + k * 37) & 0x7fffffff,
      });
      if (comp) blocks.push(comp);
    }
  }

  // Grounded Metamorphic Pedestal Core tying the rear anti-dip hogback spine cleanly into the ground
  const nCore = clamp(Math.round(strikeLen / (S * 0.10)), 10, 22);
  for (let i = 0; i < nCore; i++) {
    const u0 = i / nCore;
    const u1 = (i + 1) / nCore;
    const uMid = 0.5 * (u0 + u1);
    const st = spine.sampleAtArc(uMid);
    const su = (uMid - 0.5) * 2.0;
    const hCore = st.y * 0.56 * Math.max(0.25, 1.0 - 0.45 * su * su);
    const segW = (strikeLen / nCore) * 0.85;
    const cx = st.x - st.nx * S * 0.04 * st.r;
    const cz = st.z - st.nz * S * 0.04 * st.r;
    const localF = buildStrikeDipFrame(Math.atan2(st.tz, st.tx) + Math.PI * 0.5, dipRad);

    let cPoly = createBoxPoly(cx - segW - S * 0.22, groundY, cz - segW - S * 0.22, cx + segW + S * 0.22, hCore * 1.2, cz + segW + S * 0.22, 'hogbackCore');
    const frontDipN = vnorm([st.nx * sinD, cosD, st.nz * sinD]);
    const backScarpN = vnorm([-st.nx * 0.86, 0.50, -st.nz * 0.86]);
    cPoly = clipPolyByHalfSpace(cPoly, frontDipN, vdot(frontDipN, [cx + st.nx * S * 0.06, groundY + hCore, cz + st.nz * S * 0.06]), 'coreDip') || cPoly;
    cPoly = clipPolyByHalfSpace(cPoly, backScarpN, vdot(backScarpN, [cx - st.nx * S * 0.18 * st.r, groundY + hCore * 0.55, cz - st.nz * S * 0.18 * st.r]), 'coreBack') || cPoly;
    cPoly = clipPolyByHalfSpace(cPoly, [-st.tx, 0, -st.tz], vdot([-st.tx, 0, -st.tz], [cx - st.tx * segW, hCore * 0.4, cz - st.tz * segW]), 'cL') || cPoly;
    cPoly = clipPolyByHalfSpace(cPoly, [st.tx, 0, st.tz], vdot([st.tx, 0, st.tz], [cx + st.tx * segW, hCore * 0.4, cz + st.tz * segW]), 'cR') || cPoly;
    cPoly = clipPolyByHalfSpace(cPoly, [0, -1, 0], -groundY, 'base') || cPoly;
    if (!cPoly) continue;
    const compC = compilePolyBlock(cPoly, localF, {
      edgeBevel: bevel * 1.1,
      smoothUnionR: S * 0.005,
      detailScale: 0.90,
      hardness: 0.84,
      seedSalt: (salt + 501) & 0x7fffffff,
    });
    if (compC) blocks.push(compC);
  }

  // Angular Fallen Foliated Schist Slabs along the Dip-Slope & Scarp Toe
  const numTalusSlabs = Math.round(7 + (params.talusAmount ?? 0.48) * 9);
  for (let b = 0; b < numTalusSlabs; b++) {
    const u = rng.range(0.08, 0.92);
    const st = spine.sampleAtArc(u);
    const rS = S * rng.range(0.026, 0.054);
    const sideSign = b % 3 === 0 ? -1 : 1;
    const outOff = sideSign * S * (sideSign > 0 ? rng.range(0.24, 0.38) : rng.range(0.18, 0.28)) * st.r;
    const bx = st.x + st.nx * outOff;
    const bz = st.z + st.nz * outOff;
    const bf = buildStrikeDipFrame(Math.atan2(st.tz, st.tx) + rng.signed() * 0.6, rng.range(14, 42) * DEG2RAD);

    let bPoly = sculptGeologicalParentPoly(
      [bx, groundY + rS * 0.36, bz],
      [rS * 1.45, rS * 0.40, rS * 1.08],
      bf,
      rng,
      { jointDispersion: 0.12, cornerBlunt: 0.44, flatBaseY: groundY, extraBevelCuts: 3 }
    );
    if (!bPoly) continue;
    bPoly = clipPolyByHalfSpace(bPoly, [0, -1, 0], -groundY, 'base') || bPoly;
    const compB = compilePolyBlock(bPoly, bf, {
      edgeBevel: bevel,
      detailScale: 0.96,
      hardness: 0.88,
      isBoulder: true,
      seedSalt: (salt + 7001 + b * 43) & 0x7fffffff,
    });
    if (compB) blocks.push(compB);
  }

  return { blocks, cuts, groundY };
}

/**
 * 6. CAPROCK SANDSTONE BUTTE (`sandstone-butte`)
 * Sweeping monumental mesa caprock slabs over a naturally recessed soft
 * bed. The former broad boolean alcove cut has been removed: it read as an
 * artificial cave/hole, while differential bedding already gives the
 * undercut profile a butte needs.
 */
function buildSandstoneButte(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0025 + 0.007 * (params.edgeWear ?? 0.46));

  const height = S * 0.66;
  const radX = S * 0.44;
  const radZ = S * 0.30;
  const overhang = params.overhangStrength ?? 0.82;

  // Broad horizontal mechanical rock units with irregular headland buttress prows (no regular comb slots!)
  const unitSpecs = [
    { y0: 0.00, y1: 0.24, zFront: 0.88, wScale: 1.06, comp: 0.55 },
    { y0: 0.20, y1: 0.50, zFront: 0.84, wScale: 0.94, comp: 0.42 }, // Recessed soft-bed horizon (not a cave)
    { y0: 0.45, y1: 0.78, zFront: 1.04, wScale: 1.02, comp: 0.88 }, // Massive cliff-former
    { y0: 0.73, y1: 0.94, zFront: 1.15, wScale: 1.05, comp: 1.00 }, // Overhanging caprock!
    { y0: 0.88, y1: 1.06, zFront: 0.82, wScale: 0.65, comp: 0.90 }, // Asymmetric summit mesa crown
  ];

  const crownOffset = rng.range(-0.14, 0.14) * S;

  for (let ui = 0; ui < unitSpecs.length; ui++) {
    const u = unitSpecs[ui];
    const y0 = u.y0 * height;
    const y1 = u.y1 * height;
    const yMid = 0.5 * (y0 + y1);
    const zProj = radZ * (1.0 + (u.zFront - 1.0) * (0.5 + 0.65 * overhang));
    const totalW = radX * 2.0 * u.wScale;

    // 3-4 irregular broad interlocked slabs per horizon
    const numSlabs = ui === 4 ? 2 : 3 + (rng.chance(0.5) ? 1 : 0);
    let curX = -totalW * 0.5 + (ui === 4 ? crownOffset : 0);

    for (let si = 0; si < numSlabs; si++) {
      const rem = (totalW * 0.5 + (ui === 4 ? crownOffset : 0)) - curX;
      const slabW = si === numSlabs - 1 ? rem : (totalW / numSlabs) * rng.range(0.78, 1.25);
      const xMid = curX + slabW * 0.5;
      const prowCurve = (1.0 - 0.55 * (xMid / (radX + 1e-4)) ** 2);
      const frontZ = zProj * prowCurve;
      const backZ = -radZ * 0.85;

      const shift = tectonics.evalTectonicShift(xMid, yMid, 0);
      const frame = tectonics.localFrameAt(xMid, yMid, 0);

      const poly = sculptGeologicalParentPoly(
        [xMid + shift[0] * 0.25, yMid + shift[1] * 0.45, 0.5 * (frontZ + backZ)],
        [slabW * 0.60, (y1 - y0) * 0.64, (frontZ - backZ) * 0.56],
        frame,
        rng,
        {
          jointDispersion: 0.12,
          cornerBlunt: 0.82,
          flatBaseY: ui === 0 ? groundY : null,
          extraBevelCuts: 4,
        }
      );

      const parts = recursiveGeologicalFracture(poly, frame, tectonics, rng, {
        maxDepth: 2,
        minSpan: S * 0.065,
        minVolume: (S * 0.05) ** 3,
        bedSplitBias: 1.3,
        openCleftChance: 0.18 * (params.jointAperture ?? 0.5),
        openGap: S * 0.008,
        overlapDepth: S * 0.022, // Strong overlap prevents isolated hanging pillars!
        shearSlip: 0.08,
        pluckChance: ui < 4 ? 0.14 * (params.pluckingAmount ?? 0.6) : 0,
        groundY: ui === 0 ? groundY : null,
      });

      for (let k = 0; k < parts.length; k++) {
        const comp = compilePolyBlock(parts[k], frame, {
          edgeBevel: bevel,
          detailScale: 0.72 + 0.32 * u.comp,
          seedSalt: rng.int(1, 999999),
        });
        if (comp) blocks.push(comp);
      }

      curX += slabW * 0.92;
    }
  }

  // No broad alcove/hole cut. The recessed soft-bed unit above supplies the
  // shadowed under-caprock band while keeping the butte a solid landform.
  addTalusAndFallenBoulders(blocks, tectonics.baseFrame, rng, {
    talusAmount: params.talusAmount ?? 0.66,
    scale: S,
    spanX: radX * 2.0,
    frontZFn: (x) => (1 - 0.6 * (x / (radX + 1e-4)) ** 2) * radZ * 0.78,
    groundY,
    edgeBevel: bevel,
    mode: 'cliff',
  });

  return { blocks, cuts, groundY };
}

/**
 * 8. FROST-SPLIT HERO BOULDER & ERRATIC CLUSTER (`hero-boulder`)
 * AAA multi-lobed glacial erratic boulder cluster (God of War / Horizon / Witcher 3 style):
 * - Assembles the primary erratic from 5 interlocking stepped rock slabs & craggy boulder lobes
 *   so the silhouette has authentic re-entrant rock steps, overhanging ledges & layered ridges
 *   (ZERO smooth single-convex-hull facets and ZERO flat cube tops!)
 * - Sliced cleanly by a deep open frost-wedged master fissure + rotationally calved flank slab
 *   + angular frost-shattered satellite boulders!
 */
function buildHeroBoulder(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0018 + 0.0045 * (params.edgeWear ?? 0.38));

  const bDip = clamp((params.dipAngle ?? 32), 24, 52) * DEG2RAD;
  const bStrike = ((params.strikeAngle ?? 26) * DEG2RAD) + rng.signed() * 0.16;
  const aperture = params.jointAperture ?? 0.56;
  const overhang = params.overhangStrength ?? 0.58;

  // Frost-wedge master fracture plane slicing diagonally through the multi-lobed erratic massif!
  const cleftGap = S * (0.015 + 0.025 * aperture);
  const frostN = perturbNormal(vnorm([0.82, 0.16, -0.55]), rng, 0.08);
  const frostOff = vdot(frostN, [S * 0.02, groundY + S * 0.25, 0]);

  // 5 Interlocking Stepped Glacial Erratic Lobes (creates rich re-entrant shelves & overhanging prow!)
  const lobeSpecs = [
    { x: -0.14, y: 0.28, z:  0.03, rx: 0.30, ry: 0.31, rz: 0.27, prow: true,  crestAng: 0.2 },
    { x: -0.06, y: 0.42, z: -0.04, rx: 0.23, ry: 0.23, rz: 0.22, prow: true,  crestAng: 0.9 },
    { x:  0.14, y: 0.24, z:  0.02, rx: 0.28, ry: 0.26, rz: 0.26, prow: false, crestAng: -0.5 },
    { x:  0.05, y: 0.16, z:  0.17, rx: 0.22, ry: 0.17, rz: 0.19, prow: true,  crestAng: 1.4 },
    { x: -0.22, y: 0.15, z: -0.12, rx: 0.20, ry: 0.16, rz: 0.20, prow: false, crestAng: 2.2 },
  ];

  for (let i = 0; i < lobeSpecs.length; i++) {
    const lb = lobeSpecs[i];
    const localF = buildStrikeDipFrame(bStrike + rng.signed() * 0.14, bDip + rng.signed() * 0.10);
    let poly = sculptGeologicalParentPoly(
      [lb.x * S, groundY + lb.y * S, lb.z * S],
      [lb.rx * S, lb.ry * S, lb.rz * S],
      localF,
      rng,
      {
        jointDispersion: 0.20,
        cornerBlunt: 0.73,
        flatBaseY: lb.y - lb.ry <= 0.04 ? groundY : undefined,
        extraBevelCuts: 7,
      }
    );

    const cAz = bStrike + lb.crestAng;
    const cN1 = vnorm([Math.cos(cAz) * 0.66, 0.75, Math.sin(cAz) * 0.66]);
    const cN2 = vnorm([-Math.cos(cAz + 0.5) * 0.62, 0.78, -Math.sin(cAz + 0.5) * 0.62]);
    const topRef = [lb.x * S, groundY + (lb.y + lb.ry * 0.72) * S, lb.z * S];
    poly = clipPolyByHalfSpace(poly, cN1, vdot(cN1, topRef), 'erraticCrest') || poly;
    poly = clipPolyByHalfSpace(poly, cN2, vdot(cN2, topRef) + lb.rx * 0.22 * S, 'erraticCrest') || poly;

    if (lb.prow) {
      const underN = vnorm([-0.38, -0.58 * (0.6 + 0.5 * overhang), 0.72]);
      poly = clipPolyByHalfSpace(poly, underN, vdot(underN, [lb.x * S, groundY + S * 0.10, (lb.z + lb.rz * 0.72) * S]), 'undercut') || poly;
    }

    const [leftHalf, rightHalf] = splitPolyByPlane(poly, frostN, frostOff, -cleftGap, 'frostCleft');
    if (leftHalf) {
      const gl = clipPolyByHalfSpace(leftHalf, [0, -1, 0], -groundY, 'base') || leftHalf;
      const compL = compilePolyBlock(gl, localF, {
        edgeBevel: bevel,
        smoothUnionR: S * 0.0055,
        detailScale: 1.08,
        seedSalt: rng.int(1, 999999),
      });
      if (compL) blocks.push(compL);
    }
    if (rightHalf) {
      let heaved = transformPoly(
        rightHalf,
        [frostN[0] * S * (0.022 + 0.025 * aperture), -S * 0.014, frostN[2] * S * (0.022 + 0.025 * aperture)],
        [0.35, 0, 0.94],
        0.14 + 0.10 * aperture,
        [S * 0.14, groundY, 0]
      );
      heaved = clipPolyByHalfSpace(heaved, [0, -1, 0], -groundY, 'base') || heaved;
      const compR = compilePolyBlock(heaved, buildStrikeDipFrame(bStrike - 0.18, bDip + 0.14), {
        edgeBevel: bevel,
        smoothUnionR: S * 0.0055,
        detailScale: 1.08,
        seedSalt: rng.int(1, 999999),
      });
      if (compR) blocks.push(compR);
    }
  }

  // Rotationally Calved Flank Slab resting at ~28° against the right shoulder!
  const slabFrame = buildStrikeDipFrame(bStrike - 0.42, 54 * DEG2RAD);
  let flankSlab = sculptGeologicalParentPoly(
    [0.36 * S, groundY + 0.16 * S, 0.08 * S],
    [0.18 * S, 0.095 * S, 0.24 * S],
    slabFrame,
    rng,
    { jointDispersion: 0.18, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 6 }
  );
  flankSlab = clipPolyByHalfSpace(flankSlab, [0, -1, 0], -groundY, 'base') || flankSlab;
  const compSlab = compilePolyBlock(flankSlab, slabFrame, {
    edgeBevel: bevel * 0.9,
    smoothUnionR: S * 0.003,
    detailScale: 1.06,
    seedSalt: rng.int(1, 999999),
  });
  if (compSlab) blocks.push(compSlab);

  const numSats = Math.round(6 + (params.talusAmount ?? 0.36) * 8);
  for (let s = 0; s < numSats; s++) {
    const sa = (s / numSats) * Math.PI * 2 + rng.signed() * 0.30;
    const dist = S * rng.range(0.38, 0.56);
    const rSat = S * rng.range(0.045, 0.105);
    const satF = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(22, 58) * DEG2RAD);
    let satP = sculptGeologicalParentPoly(
      [Math.cos(sa) * dist, groundY + rSat * 0.56, Math.sin(sa) * dist * 0.85],
      [rSat * rng.range(0.95, 1.35), rSat * rng.range(0.62, 0.88), rSat * rng.range(0.85, 1.25)],
      satF,
      rng,
      { jointDispersion: 0.22, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 6 }
    );
    satP = clipPolyByHalfSpace(satP, [0, -1, 0], -groundY, 'base') || satP;
    const compS = compilePolyBlock(satP, satF, {
      edgeBevel: bevel * 0.85,
      detailScale: 0.98,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compS) blocks.push(compS);
  }

  return { blocks, cuts, groundY };
}

/**
 * 10. MONUMENT SPIRES & PINNACLES (`desert-spires`)
 * Fisher Towers (The Titan & Kingfisher) / Courthouse Organ / Monument Valley Totem Complex:
 * - Asymmetric flared Cutler/Moenkopi badlands ridge & radiating buttress spurs
 * - Soaring cathedral sandstone fin-walls built from bundled vertical organ-pipe ribs,
 *   staggered flanking spirelets/needles, and pitched jagged dihedral turret crowns
 *   (ZERO stacked marshmallow cubes or flat floating square caps!)
 * - Correlated thin horizontal Cutler/Wingate sandstone bedding shelves wrapping across the ribs
 * - Deep sky-lit vertical joint clefts separating the main Titan tower from flanking needles
 */
function buildDesertSpires(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const frame = tectonics.baseFrame;
  const overhang = params.overhangStrength ?? 0.72;
  const bevel = S * (0.0014 + 0.0026 * (params.edgeWear ?? 0.42));
  const finStrike = (params.strikeAngle ?? 8) * DEG2RAD;

  const ridgeZ = (xFrac) =>
    S * (0.024 * Math.sin(xFrac * 5.2 + 0.4) - 0.016 * Math.cos(xFrac * 3.1));

  // 1. Stepped Flared Badlands Pedestal Ridge + Radiating Transverse Buttress Spurs
  const pedNodes = 9;
  for (let i = 0; i < pedNodes; i++) {
    const u = i / (pedNodes - 1) - 0.5; // -0.5 .. +0.5
    const env = Math.max(0.28, 1.0 - 2.1 * u * u);
    const px = u * 0.90 * S + rng.signed() * S * 0.012;
    const pz = ridgeZ(u);
    const pH = S * (0.09 + 0.13 * env) * rng.range(0.92, 1.08);
    const pRx = S * 0.095 * rng.range(0.92, 1.15);
    const pRz = S * (0.12 + 0.13 * env) * rng.range(0.90, 1.12);

    const pf = buildStrikeDipFrame(finStrike + rng.signed() * 0.14, rng.range(2, 7) * DEG2RAD);
    let pPoly = sculptGeologicalParentPoly(
      [px, groundY + pH * 0.46, pz],
      [pRx, pH * 0.54, pRz],
      pf,
      rng,
      { jointDispersion: 0.14, cornerBlunt: 0.74, flatBaseY: groundY, extraBevelCuts: 6 }
    );
    pPoly = clipPolyByHalfSpace(pPoly, [0, -1, 0], -groundY, 'base') || pPoly;
    const compP = compilePolyBlock(pPoly, pf, {
      edgeBevel: bevel,
      smoothUnionR: S * 0.0085,
      detailScale: 0.95,
      isEolian: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compP) blocks.push(compP);

    // Radiating transverse sandstone buttress spurs jutting out from the pedestal ridge!
    if (i >= 1 && i <= pedNodes - 2 && i % 2 === 1) {
      for (const side of [-1, 1]) {
        const spurLen = pRz * rng.range(0.75, 1.15);
        const spurH = pH * rng.range(0.55, 0.82);
        const sf = buildStrikeDipFrame(finStrike + Math.PI * 0.5 + rng.signed() * 0.18, rng.range(8, 22) * DEG2RAD);
        let spurP = sculptGeologicalParentPoly(
          [px + rng.signed() * S * 0.015, groundY + spurH * 0.44, pz + side * spurLen * 0.72],
          [S * rng.range(0.032, 0.052), spurH * 0.52, spurLen * 0.62],
          sf,
          rng,
          { jointDispersion: 0.16, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 5 }
        );
        const pitchN = vnorm([rng.signed() * 0.15, 0.80, side * 0.58]);
        spurP = clipPolyByHalfSpace(spurP, pitchN, vdot(pitchN, [px, groundY + spurH * 0.82, pz + side * spurLen * 0.6]), 'spurSlope') || spurP;
        spurP = clipPolyByHalfSpace(spurP, [0, -1, 0], -groundY, 'base') || spurP;
        const compSpur = compilePolyBlock(spurP, sf, {
          edgeBevel: bevel,
          smoothUnionR: S * 0.0075,
          detailScale: 0.94,
          isEolian: true,
          seedSalt: rng.int(1, 999999),
        });
        if (compSpur) blocks.push(compSpur);
      }
    }
  }

  // 2. Nine Cathedral Fin-Towers & Soaring Organ-Pipe Pinnacles
  // Each tower consists of a tall blade-fin core + 4 bundled vertical organ-pipe buttress ribs
  // terminating in staggered jagged turret needles + fine correlated sandstone strata shelves!
  const towers = [
    { x: -0.055, zOff:  0.000, h: 0.98, rx: 0.068, rz: 0.098, leanX: -0.010, ribs: 5, turret: 1.06 }, // Central Titan Spire
    { x:  0.095, zOff:  0.012, h: 0.86, rx: 0.056, rz: 0.086, leanX:  0.014, ribs: 4, turret: 1.10 }, // Ancient-Art Needle across sky-slot!
    { x: -0.195, zOff: -0.010, h: 0.78, rx: 0.064, rz: 0.090, leanX: -0.008, ribs: 5, turret: 0.96 }, // Kingfisher Cathedral Buttress
    { x:  0.225, zOff: -0.008, h: 0.68, rx: 0.058, rz: 0.082, leanX:  0.008, ribs: 4, turret: 1.08 }, // Right Organ-Pipe Tower
    { x: -0.320, zOff:  0.008, h: 0.56, rx: 0.056, rz: 0.076, leanX: -0.012, ribs: 4, turret: 0.92 }, // Left Wing Pinnacle
    { x:  0.345, zOff:  0.010, h: 0.48, rx: 0.050, rz: 0.070, leanX:  0.010, ribs: 3, turret: 1.04 }, // Right Outer Sentinel Needle
    { x: -0.415, zOff: -0.006, h: 0.36, rx: 0.046, rz: 0.062, leanX: -0.008, ribs: 3, turret: 0.90 }, // Far-left badlands gendarme
    { x:  0.018, zOff:  0.092, h: 0.52, rx: 0.044, rz: 0.058, leanX:  0.006, ribs: 3, turret: 1.02 }, // Foreground cathedral flying buttress spire!
    { x: -0.130, zOff: -0.082, h: 0.49, rx: 0.044, rz: 0.056, leanX: -0.006, ribs: 3, turret: 0.95 }, // Rear flanking needle
  ];

  // Shared master Cutler/Wingate horizontal sandstone shelf elevations across all spires!
  const ledgeLevels = [0.20, 0.27, 0.34, 0.41, 0.48, 0.55, 0.62, 0.69, 0.76, 0.83, 0.90];

  for (let t = 0; t < towers.length; t++) {
    const tw = towers[t];
    const bx = tw.x * S + rng.signed() * S * 0.006;
    const bz = ridgeZ(tw.x) + tw.zOff * S + rng.signed() * S * 0.006;
    const topY = groundY + tw.h * S * rng.range(0.96, 1.03);
    const towerH = topY - groundY;
    const tf = buildStrikeDipFrame(finStrike + rng.signed() * 0.08, rng.range(1.5, 4.5) * DEG2RAD);

    // 2A. Three-Stage Gothic Tapered Sandstone Spire Core (Wide Fluted Base -> Mid Shaft -> Sharp Pinnacle Needle!)
    const stageSpecs = [
      { y0f: 0.00, y1f: 0.48, radScale: 1.18, leanF: 0.20, peakClip: false },
      { y0f: 0.36, y1f: 0.78, radScale: 0.80, leanF: 0.58, peakClip: false },
      { y0f: 0.66, y1f: 1.00, radScale: 0.52, leanF: 0.95, peakClip: true  },
    ];
    for (let st = 0; st < stageSpecs.length; st++) {
      const ss = stageSpecs[st];
      const sy0 = groundY + towerH * ss.y0f;
      const sy1 = groundY + towerH * ss.y1f;
      const sh = sy1 - sy0;
      const scx = bx + tw.leanX * ss.leanF * S;
      let stPoly = sculptGeologicalParentPoly(
        [scx, sy0 + sh * 0.50, bz],
        [tw.rx * ss.radScale * S, sh * 0.53, tw.rz * ss.radScale * S],
        tf,
        rng,
        {
          jointDispersion: 0.09,
          cornerBlunt: 0.74,
          flatBaseY: st === 0 ? groundY : undefined,
          extraBevelCuts: 6,
        }
      );
      if (st === 0) {
        stPoly = clipPolyByHalfSpace(stPoly, [0, -1, 0], -groundY, 'base') || stPoly;
      }
      if (ss.peakClip) {
        // Four steep converging dihedral facets around the summit -> authentic craggy pinnacle needle!
        const baseAz = finStrike + rng.signed() * 0.5;
        for (let k = 0; k < 4; k++) {
          const az = baseAz + k * Math.PI * 0.5 + rng.signed() * 0.22;
          const pN = vnorm([Math.cos(az) * 0.82, 0.57, Math.sin(az) * 0.82]);
          stPoly = clipPolyByHalfSpace(
            stPoly,
            pN,
            vdot(pN, [scx, topY - S * 0.008, bz]) + tw.rx * 0.14 * S,
            'pinnacleTip'
          ) || stPoly;
        }
      }
      const compSt = compilePolyBlock(stPoly, tf, {
        edgeBevel: bevel * (ss.peakClip ? 0.72 : 0.92),
        smoothUnionR: S * 0.0065,
        detailScale: 0.98,
        isEolian: true,
        seedSalt: rng.int(1, 999999),
      });
      if (compSt) blocks.push(compSt);
    }

    // 2B. Bundled Vertical Organ-Pipe Buttress Ribs & Staggered Flanking Needles!
    // Each rib tapers upward into a sharp secondary spirelet clinging to the main tower (NO bracket-fungus ledges!)
    for (let r = 0; r < tw.ribs + 1; r++) {
      const ang = (r / (tw.ribs + 1)) * Math.PI * 2 + t * 0.73 + rng.signed() * 0.20;
      const ribHFrac = clamp(0.36 + (r / (tw.ribs + 1)) * 0.52 + rng.signed() * 0.06, 0.32, 0.90);
      const ribTopY = groundY + towerH * ribHFrac;
      const ribStartY = groundY + S * 0.04;
      const ribH = ribTopY - ribStartY;
      if (ribH < S * 0.12) continue;

      const radPlace = 1.02 - 0.30 * ribHFrac;
      const offX = Math.cos(ang) * tw.rx * 0.76 * radPlace * S + tw.leanX * ribHFrac * 0.45 * S;
      const offZ = Math.sin(ang) * tw.rz * 0.82 * radPlace * S;
      const ribRx = tw.rx * rng.range(0.34, 0.48) * S;
      const ribRz = tw.rz * rng.range(0.36, 0.52) * S;

      const rf = buildStrikeDipFrame(ang, rng.range(2, 6) * DEG2RAD);
      let ribPoly = sculptGeologicalParentPoly(
        [bx + offX, ribStartY + ribH * 0.48, bz + offZ],
        [ribRx, ribH * 0.51, ribRz],
        rf,
        rng,
        { jointDispersion: 0.08, cornerBlunt: 0.72, extraBevelCuts: 6 }
      );
      // Converging 3-plane Gothic needle tip on every flanking organ-pipe spirelet!
      for (let k = -1; k <= 1; k++) {
        const tipAz = ang + k * 0.95;
        const tipN = vnorm([Math.cos(tipAz) * 0.78, 0.62, Math.sin(tipAz) * 0.78]);
        ribPoly = clipPolyByHalfSpace(
          ribPoly,
          tipN,
          vdot(tipN, [bx + offX * 0.78, ribTopY - S * 0.006, bz + offZ * 0.78]) + ribRx * 0.22,
          'ribNeedle'
        ) || ribPoly;
      }

      const compRib = compilePolyBlock(ribPoly, rf, {
        edgeBevel: bevel * 0.78,
        smoothUnionR: S * 0.0055,
        detailScale: 0.96,
        isEolian: true,
        seedSalt: rng.int(1, 999999),
      });
      if (compRib) blocks.push(compRib);
    }
  }

  // 3. Narrow Vertical Joint Clefts Between Cathedral Spire Groups so Desert Sky Shines Cleanly Through!
  const cleftXs = [-0.128, 0.028, 0.164, -0.262, 0.290];
  for (let i = 0; i < cleftXs.length; i++) {
    const cx = cleftXs[i] * S;
    const cleftCut = sculptGeologicalParentPoly(
      [cx, groundY + 0.56 * S, ridgeZ(cleftXs[i])],
      [S * rng.range(0.011, 0.018), S * 0.36, S * 0.18],
      frame,
      rng,
      { jointDispersion: 0.05, cornerBlunt: 0.74 }
    );
    const compSlot = compilePolyBlock(cleftCut, frame, { edgeBevel: S * 0.0018 });
    if (compSlot) cuts.push(compSlot);
  }

  // 4. Angular Calved Sandstone Slab Talus Around the Badlands Skirt
  const numTalus = Math.round(10 + (params.talusAmount ?? 0.46) * 14);
  for (let i = 0; i < numTalus; i++) {
    const u = rng.range(-0.46, 0.46);
    const side = rng.chance(0.55) ? 1 : -1;
    const bx = u * S;
    const bz = ridgeZ(u) + side * S * rng.range(0.16, 0.34);
    const rB = S * rng.range(0.018, 0.045);
    const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(10, 42) * DEG2RAD);
    let bPoly = sculptGeologicalParentPoly(
      [bx, groundY + rB * 0.48, bz],
      [rB * rng.range(1.0, 1.55), rB * rng.range(0.48, 0.78), rB * rng.range(0.90, 1.35)],
      bf,
      rng,
      { jointDispersion: 0.22, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 5 }
    );
    bPoly = clipPolyByHalfSpace(bPoly, [0, -1, 0], -groundY, 'base') || bPoly;
    const compB = compilePolyBlock(bPoly, bf, {
      edgeBevel: bevel * 0.85,
      detailScale: 0.90,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compB) blocks.push(compB);
  }

  return { blocks, cuts, groundY };
}

/**
 * 11. UTAH CANYON WALL & SLICKROCK ALCOVE (`utah-canyon`)
 * Zion / Escalante slickrock amphitheater wall:
 * - Sweeping crescent Navajo sandstone wall built from continuous stepped
 *   cliff units and tapering cross-bedded wings.
 * - The wall is deliberately solid: no boolean cave/vault and no localized
 *   alcove recess punched into the cliff, because that read as a fake hole.
 *   Only broad plan curvature, bedding ledges and narrow joints remain.
 */
function buildUtahCanyon(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const activeSplines = Array.isArray(params.formationSplines) && params.formationSplines.length >= 1
    ? params.formationSplines
    : getFormationSplineSpec('utah-canyon', S).defaultCurves;
  const spine = compileSplineArcTable(activeSplines[0], 96);
  const spanLen = Math.max(S * 0.65, spine.totalHorizLength);

  const dipRad = clamp((params.dipAngle ?? 8), 2, 18) * 0.42 * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? -12) * DEG2RAD;
  const bedFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const overhang = params.overhangStrength ?? 0.84;
  const aperture = params.jointAperture ?? 0.48;
  const bevel = S * (0.0009 + 0.0018 * (params.edgeWear ?? 0.36));

  const baseHeight = S * 0.74;
  const baseDepth = S * 0.44;
  const baySalt = rng.int(1, 99999);

  // Broad amphitheater front offset along local normal +N(u)
  // Keeps sideMax - centre < 0.75m across central [-4.5m, +4.5m] so there is zero cave-hole artifact,
  // while vertical Wingate buttresses give majestic Utah canyon relief!
  function frontOffsetAt(u, rMul) {
    const su = (u - 0.5) * 2.0; // -1 .. +1
    const leftButtress = Math.exp(-((su + 0.54) * (su + 0.54)) / 0.16) * 0.085;
    const rightButtress = Math.exp(-((su - 0.52) * (su - 0.52)) / 0.16) * 0.080;
    const midRib = Math.exp(-((su + 0.06) * (su + 0.06)) / 0.10) * 0.032;
    const wingSweep = -0.18 * Math.pow(Math.abs(su), 3.0);
    const flute = scalarField3D(su * 5.4, 0.6, 0.8, baySalt) * 0.028;
    return (0.28 + leftButtress + rightButtress + midRib + wingSweep + flute) * baseDepth * rMul;
  }

  function backOffsetAt(u, rMul) {
    const su = (u - 0.5) * 2.0;
    const endTrim = 0.18 * Math.pow(Math.abs(su), 2.6);
    const backJag = scalarField3D(su * 3.1, 1.9, 0.5, baySalt + 41) * 0.035;
    return (-0.48 + endTrim + backJag) * baseDepth * rMul;
  }

  // Broad monumental plateau rim across the canyon wall (no narrow triangular wedding-cake chop!)
  function rimCrestAt(u, splineY) {
    const su = (u - 0.5) * 2.0;
    const shoulderDrop = Math.abs(su) > 0.78
      ? 1.0 - 0.42 * Math.pow((Math.abs(su) - 0.78) / 0.22, 1.65)
      : 1.0;
    const slickrockDome = 0.96 + 0.055 * Math.cos(su * 3.8 - 0.25) +
      0.038 * scalarField3D(su * 4.2, 1.4, 0.6, baySalt + 31);
    return clamp(splineY * shoulderDrop * slickrockDome, baseHeight * 0.36, baseHeight * 1.45);
  }

  // 1. Unbroken Recessed Canyon Wall Core along the Spine Spline (C1 tangent rim -> zero stair-steps!)
  const nCore = clamp(Math.round(spanLen / (S * 0.08)), 16, 38);
  for (let i = 0; i < nCore; i++) {
    const u0 = i / nCore;
    const u1 = (i + 1) / nCore;
    const uMid = 0.5 * (u0 + u1);
    const st0 = spine.sampleAtArc(u0);
    const st1 = spine.sampleAtArc(u1);
    const st = spine.sampleAtArc(uMid);
    const h0 = rimCrestAt(u0, st0.y) * 0.96;
    const h1 = rimCrestAt(u1, st1.y) * 0.96;
    const hCore = rimCrestAt(uMid, st.y) * 0.96;
    const dCoreRim = clamp((h1 - h0) / Math.max(S * 0.04, spanLen / nCore), -0.9, 0.9);
    const fCore = frontOffsetAt(uMid, st.r) - S * 0.024;
    const bCore = Math.min(fCore - S * 0.09, backOffsetAt(uMid, st.r) + S * 0.015);
    const midOff = 0.5 * (fCore + bCore);
    const halfThick = Math.max(S * 0.052, 0.5 * (fCore - bCore) * 1.14);
    const segHalfW = (spanLen / nCore) * 0.88;
    const cx = st.x + st.nx * midOff;
    const cz = st.z + st.nz * midOff;
    const localF = buildStrikeDipFrame(Math.atan2(-st.nx, st.nz), dipRad);

    let cPoly = sculptGeologicalParentPoly(
      [cx, groundY + hCore * 0.50, cz],
      [segHalfW, hCore * 0.56, halfThick],
      localF,
      rng,
      { jointDispersion: 0.04, cornerBlunt: 0.66, flatBaseY: groundY, extraBevelCuts: 3 }
    );
    if (!cPoly) continue;
    const coreRimN = vnorm([-st.tx * dCoreRim + st.nx * 0.14, 0.98, -st.tz * dCoreRim + st.nz * 0.14]);
    cPoly = clipPolyByHalfSpace(cPoly, coreRimN, vdot(coreRimN, [cx, groundY + hCore, cz]), 'coreRim') || cPoly;
    cPoly = clipPolyByHalfSpace(cPoly, [0, -1, 0], -groundY, 'base') || cPoly;
    const compC = compilePolyBlock(cPoly, bedFrame, {
      edgeBevel: bevel * 1.15,
      smoothUnionR: S * 0.0060,
      detailScale: 0.88,
      hardness: 0.84,
      isSedimentary: true,
      seedSalt: (baySalt + 700) & 0x7fffffff,
    });
    if (compC) blocks.push(compC);
  }

  // 5 irregular master dihedral vertical fracture zones across the Wingate/Navajo cliff (NOT 22 keyboard slots!)
  const masterWingateJoints = [0.15, 0.32, 0.52, 0.69, 0.85];

  // 2. Chinle / Moenkopi Stepped Basal Plinth & Wingate / Kayenta / Navajo Cliff Architecture
  const canyonHorizons = [
    // Basal Chinle / Moenkopi stepped plinth ledges
    { y0: 0.00, y1: 0.09, proj:  0.13, hard: 0.78, blkM: 0.082, batter: 0.28, grp: 1 },
    { y0: 0.08, y1: 0.17, proj:  0.07, hard: 0.72, blkM: 0.080, batter: 0.22, grp: 1 },
    // Basal spring-line shale recess under Wingate cliff-former
    { y0: 0.16, y1: 0.21, proj: -0.045, hard: 0.30, blkM: 0.095, batter: 0.14, grp: 2 },
    // Monumental sheer Wingate Sandstone vertical buttress monoliths (continuous across lower+upper Wingate!)
    { y0: 0.20, y1: 0.43, proj:  0.055, hard: 0.95, blkM: 0.074, batter: 0.015, grp: 3 },
    { y0: 0.41, y1: 0.62, proj:  0.052, hard: 0.94, blkM: 0.074, batter: 0.025, grp: 3 },
    // Kayenta Formation stepped ledge & recessed parting
    { y0: 0.60, y1: 0.66, proj: -0.030, hard: 0.38, blkM: 0.090, batter: 0.16, grp: 4 },
    { y0: 0.65, y1: 0.73, proj:  0.042, hard: 0.88, blkM: 0.078, batter: 0.04, grp: 4 },
    // Upper Navajo Sandstone cliff & rounded cross-bedded slickrock rim buttresses
    { y0: 0.72, y1: 0.87, proj:  0.024, hard: 0.92, blkM: 0.082, batter: 0.06, grp: 5 },
    { y0: 0.85, y1: 1.02, proj: -0.012, hard: 0.90, blkM: 0.088, batter: 0.16, grp: 5 },
  ];

  for (let hIdx = 0; hIdx < canyonHorizons.length; hIdx++) {
    const hz = canyonHorizons[hIdx];
    const refY0 = groundY + hz.y0 * baseHeight;
    const refY1 = groundY + hz.y1 * baseHeight;
    const unitH = refY1 - refY0;

    const nBlocks = clamp(Math.round(spanLen / (S * hz.blkM)), 16, 34);
    const du = 0.97 / nBlocks;
    const jointHairline = S * (0.0016 + 0.0028 * aperture);
    const horizonJoints = masterWingateJoints.map((ju, idx) =>
      ju + scalarField3D(idx * 2.3, hz.grp * 2.1, 0.5, baySalt + 219) * (hz.grp === 3 ? 0.006 : 0.026)
    );

    for (let i = 0; i < nBlocks; i++) {
      const u0 = 0.015 + i * du;
      const u1 = 0.015 + (i + 1) * du;
      const uMid = 0.5 * (u0 + u1);

      const st0 = spine.sampleAtArc(u0);
      const st1 = spine.sampleAtArc(u1);
      const stM = spine.sampleAtArc(uMid);

      let panelIdx = 0;
      let jointAtLeft = false;
      let jointAtRight = false;
      for (let j = 0; j < horizonJoints.length; j++) {
        if (uMid > horizonJoints[j]) panelIdx = j + 1;
        if (Math.abs(u0 - horizonJoints[j]) < du * 0.55) jointAtLeft = true;
        if (Math.abs(u1 - horizonJoints[j]) < du * 0.55) jointAtRight = true;
      }

      const crest0 = rimCrestAt(u0, st0.y);
      const crest1 = rimCrestAt(u1, st1.y);
      const localCrest = rimCrestAt(uMid, stM.y);
      const crestMax = Math.max(crest0, crest1, localCrest);
      if (refY0 > crestMax - unitH * 0.16) continue;
      const cappedByRim = refY1 >= crestMax - unitH * 0.18;

      // Coherent Wingate panel step + gentle desert-varnish vertical fluting
      const panelRelief = scalarField3D(panelIdx * 2.1, hz.grp * 1.4, 0.6, baySalt + 307) * 0.022;
      const fluteWave = scalarField3D(uMid * 4.2, hz.grp * 0.7, 0.4, baySalt + 409) * 0.015;
      const stepProj = (hz.proj * (0.72 + 0.34 * overhang) + panelRelief + fluteWave) * baseDepth * stM.r;

      const fOff0 = frontOffsetAt(u0, st0.r) + stepProj;
      const fOff1 = frontOffsetAt(u1, st1.r) + stepProj;
      const fOffM = 0.5 * (fOff0 + fOff1);
      const bOffM = Math.min(fOffM - S * 0.09, backOffsetAt(uMid, stM.r));

      const segArcW = Math.max(S * 0.034, (u1 - u0) * spanLen);
      const leftExt = jointAtLeft ? Math.max(S * 0.014, segArcW * 0.50 - jointHairline) : segArcW * 0.74;
      const rightExt = jointAtRight ? Math.max(S * 0.014, segArcW * 0.50 - jointHairline) : segArcW * 0.74;

      const botY = hIdx === 0 ? groundY : Math.max(groundY, refY0 - unitH * 0.06);
      const topY = (cappedByRim ? localCrest : refY1) + (hz.hard < 0.45 ? unitH * 0.14 : unitH * 0.03);
      const midY = 0.5 * (botY + topY);

      const frontPt = [stM.x + stM.nx * fOffM, midY, stM.z + stM.nz * fOffM];
      const backPt = [stM.x + stM.nx * bOffM, midY, stM.z + stM.nz * bOffM];
      const centerPt = [0.5 * (frontPt[0] + backPt[0]), midY, 0.5 * (frontPt[2] + backPt[2])];
      const reach = Math.max(segArcW, Math.abs(fOffM - bOffM)) + S * 0.12;

      let poly = createBoxPoly(
        centerPt[0] - reach,
        groundY,
        centerPt[2] - reach,
        centerPt[0] + reach,
        baseHeight * 1.75,
        centerPt[2] + reach,
        'utahHorizon'
      );

      // C1 tangent-sloped slickrock rim plane wherever a horizon reaches the plateau rim (ZERO stair-steps!)
      const dRimDs = clamp((crest1 - crest0) / Math.max(S * 0.03, segArcW), -0.95, 0.95);
      if (refY1 > Math.min(crest0, crest1) - unitH * 0.12) {
        const rimN = vnorm([-stM.tx * dRimDs + stM.nx * 0.22, 0.96, -stM.tz * dRimDs + stM.nz * 0.22]);
        poly = clipPolyByHalfSpace(poly, rimN, vdot(rimN, [centerPt[0], groundY + localCrest, centerPt[2]]), 'slickrockRim') || poly;
      }
      if (!cappedByRim) {
        poly = clipPolyByHalfSpace(poly, bedFrame.normal, vdot(bedFrame.normal, [centerPt[0], refY1 + (hz.hard < 0.45 ? unitH * 0.14 : unitH * 0.02), centerPt[2]]), 'unitTop') || poly;
      }
      const botN = hIdx === 0 ? [0, -1, 0] : [-bedFrame.normal[0], -bedFrame.normal[1], -bedFrame.normal[2]];
      poly = clipPolyByHalfSpace(poly, botN, vdot(botN, [centerPt[0], botY, centerPt[2]]), 'unitBot') || poly;
      if (!poly) continue;

      const dfds = clamp((fOff1 - fOff0) / Math.max(S * 0.03, segArcW), -0.85, 0.85);
      const frontN = vnorm([
        stM.nx - stM.tx * dfds,
        hz.batter,
        stM.nz - stM.tz * dfds,
      ]);
      poly = clipPolyByHalfSpace(poly, frontN, vdot(frontN, frontPt), 'canyonFace') || poly;

      const yawL = jointAtLeft ? scalarField3D(panelIdx * 2.9, hz.grp, 0.3, baySalt + 521) * 0.16 : 0;
      const yawR = jointAtRight ? scalarField3D((panelIdx + 1) * 2.9, hz.grp, 0.3, baySalt + 521) * 0.16 : 0;
      const jL = vnorm([
        -stM.tx * Math.cos(yawL) + stM.nx * Math.sin(yawL),
        jointAtLeft ? 0.02 : 0,
        -stM.tz * Math.cos(yawL) + stM.nz * Math.sin(yawL),
      ]);
      const jR = vnorm([
        stM.tx * Math.cos(yawR) + stM.nx * Math.sin(yawR),
        jointAtRight ? 0.02 : 0,
        stM.tz * Math.cos(yawR) + stM.nz * Math.sin(yawR),
      ]);
      poly = clipPolyByHalfSpace(poly, jL, vdot(jL, [frontPt[0] - stM.tx * leftExt, midY, frontPt[2] - stM.tz * leftExt]), 'jointL') || poly;
      poly = clipPolyByHalfSpace(poly, jR, vdot(jR, [frontPt[0] + stM.tx * rightExt, midY, frontPt[2] + stM.tz * rightExt]), 'jointR') || poly;

      const backN = vnorm([-stM.nx, 0.20, -stM.nz]);
      poly = clipPolyByHalfSpace(poly, backN, vdot(backN, backPt), 'canyonBack') || poly;
      poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
      if (!poly) continue;

      const hasMasterJoint = jointAtLeft || jointAtRight;
      const comp = compilePolyBlock(poly, bedFrame, {
        edgeBevel: hz.hard > 0.8 ? bevel * 0.9 : bevel * 1.25,
        smoothUnionR: hasMasterJoint ? S * 0.0016 : S * 0.0056,
        detailScale: hz.hard > 0.85 ? 0.86 : 1.02,
        hardness: hz.hard,
        isSedimentary: true,
        isEolian: hIdx >= 7,
        // Shared seedSalt across each massive Wingate/Navajo panel -> monumental desert varnish faces!
        seedSalt: (baySalt + hz.grp * 101 + panelIdx * 29) & 0x7fffffff,
      });
      if (comp) blocks.push(comp);
    }
  }

  // 3. Two Natural Oblique Rim Drainage Joint Notches on Upper Flanks
  //    Replaces the artificial mid-wall coin-slot slits with organic weathered V-couloirs
  //    at the upper plateau rim shoulders (keeps macro.cuts.length === 2, width < 1.5m, vol < 16m³)
  for (const [idx, uNotch] of [0.19, 0.81].entries()) {
    const st = spine.sampleAtArc(uNotch);
    const localRim = rimCrestAt(uNotch, st.y);
    const fOff = frontOffsetAt(uNotch, st.r);
    const nx = st.x + st.nx * (fOff - S * 0.02);
    const nz = st.z + st.nz * (fOff - S * 0.02);
    const notchFrame = buildStrikeDipFrame(
      Math.atan2(st.tz, st.tx) + (idx === 0 ? 0.28 : -0.28),
      (68 + (idx === 0 ? 5 : -4)) * DEG2RAD
    );
    const halfW = Math.min(0.42, S * (0.016 + 0.010 * aperture));
    const halfH = Math.min(1.65, S * 0.095);
    const halfD = Math.min(1.15, S * 0.068);
    const couloir = sculptGeologicalParentPoly(
      [nx, groundY + localRim * 0.90, nz],
      [halfW, halfH, halfD],
      notchFrame,
      rng,
      { jointDispersion: 0.08, cornerBlunt: 0.55, extraBevelCuts: 3 }
    );
    const compCouloir = compilePolyBlock(couloir, notchFrame, { edgeBevel: S * 0.0025 });
    if (compCouloir) cuts.push(compCouloir);
  }

  // 4. Angular Fallen Wingate Sandstone Blocks along the Basal Plinth Apron
  const numTalus = Math.round(7 + (params.talusAmount ?? 0.48) * 9);
  for (let b = 0; b < numTalus; b++) {
    const u = rng.range(0.08, 0.92);
    const st = spine.sampleAtArc(u);
    const rB = S * rng.range(0.024, 0.052);
    const fOff = frontOffsetAt(u, st.r) + baseDepth * rng.range(0.10, 0.28);
    const bx = st.x + st.nx * fOff;
    const bz = st.z + st.nz * fOff;
    const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(10, 34) * DEG2RAD);
    let bPoly = sculptGeologicalParentPoly(
      [bx, groundY + rB * 0.46, bz],
      [rB * 1.28, rB * 0.66, rB * 1.04],
      bf,
      rng,
      { jointDispersion: 0.14, cornerBlunt: 0.62, flatBaseY: groundY, extraBevelCuts: 4 }
    );
    if (!bPoly) continue;
    bPoly = clipPolyByHalfSpace(bPoly, [0, -1, 0], -groundY, 'base') || bPoly;
    const compB = compilePolyBlock(bPoly, bf, {
      edgeBevel: bevel * 0.9,
      detailScale: 0.95,
      hardness: 0.90,
      isSedimentary: true,
      isBoulder: true,
      seedSalt: (baySalt + 9101 + b * 31) & 0x7fffffff,
    });
    if (compB) blocks.push(compB);
  }

  return { blocks, cuts, groundY };
}

const NATIVE_SPLINE_ARCHETYPES = new Set([
  'sedimentary-cliff',
  'slot-canyon',
  'exfoliation-dome',
  'studlagil-colonnade',
  'saharan-yardang-fin',
  'metamorphic-outcrop',
  'utah-canyon',
]);

const ARCH_SPLINE_ARCHETYPES = new Set([
  'desert-arch',
  'double-arch',
  'sea-arch',
  'arch-encampment',
]);

export function buildMacroFormation(params, tectonics, rng) {
  if (Array.isArray(params.customArchCurves) && params.customArchCurves.length > 0) {
    return buildCustomUserSplineArch(params, tectonics, rng, addTalusAndFallenBoulders);
  }
  const hasEditedSplines = Array.isArray(params.formationSplines) &&
    params.formationSplines.length > 0 &&
    !areSplinesAtDefault(params.archetype, params.scale, params.formationSplines);

  if (hasEditedSplines && ARCH_SPLINE_ARCHETYPES.has(params.archetype)) {
    return buildCustomUserSplineArch(
      { ...params, customArchCurves: params.formationSplines },
      tectonics,
      rng,
      addTalusAndFallenBoulders
    );
  }

  let macro;
  switch (params.archetype) {
    case "arch-encampment":
      macro = buildArchEncampment(params, tectonics, rng, addTalusAndFallenBoulders);
      break;
    case "desert-arch":
      macro = buildDesertArch(params, tectonics, rng, addTalusAndFallenBoulders);
      break;
    case "double-arch":
      macro = buildDoubleArch(params, tectonics, rng, addTalusAndFallenBoulders);
      break;
    case "sea-arch":
      macro = buildSeaArch(params, tectonics, rng, recursiveGeologicalFracture, addTalusAndFallenBoulders);
      break;
    case "sedimentary-cliff":
      macro = buildSedimentaryCliff(params, tectonics, rng, recursiveGeologicalFracture, addTalusAndFallenBoulders);
      break;
    case "slot-canyon":
      macro = buildSlotCanyon(params, tectonics, rng);
      break;
    case "hoodoo-columns":
      macro = buildHoodooColumns(params, tectonics, rng, addTalusAndFallenBoulders);
      break;
    case "balanced-rock":
      macro = buildBalancedRock(params, tectonics, rng, recursiveGeologicalFracture, addTalusAndFallenBoulders);
      break;
    case "limestone-karst":
      macro = buildLimestoneKarst(params, tectonics, rng, addTalusAndFallenBoulders);
      break;
    case "exfoliation-dome":
      macro = buildExfoliationDome(params, tectonics, rng, addTalusAndFallenBoulders);
      break;
    case "desert-spires":
      macro = buildDesertSpires(params, tectonics, rng);
      break;
    case "utah-canyon":
      macro = buildUtahCanyon(params, tectonics, rng);
      break;
    case "alpine-crag":
      macro = buildAlpineCrag(params, tectonics, rng);
      break;
    case "basalt-columns":
      macro = buildBasaltColumns(params, tectonics, rng);
      break;
    case "granite-tor":
      macro = buildGraniteTor(params, tectonics, rng);
      break;
    case "metamorphic-outcrop":
      macro = buildMetamorphicOutcrop(params, tectonics, rng);
      break;
    case "sandstone-butte":
      macro = buildSandstoneButte(params, tectonics, rng);
      break;
    case "hero-boulder":
      macro = buildHeroBoulder(params, tectonics, rng);
      break;
    case "cracked-monolith":
      macro = buildCrackedMonolith(params, tectonics, rng);
      break;
    case "grand-canyon-temple":
      macro = buildGrandCanyonTemple(params, tectonics, rng, addTalusAndFallenBoulders);
      break;
    case "studlagil-colonnade":
      macro = buildStudlagilColonnade(params, tectonics, rng);
      break;
    case "icelandic-tuya-crag":
      macro = buildIcelandicTuyaCrag(params, tectonics, rng);
      break;
    case "saharan-yardang-fin":
      macro = buildSaharanYardangFin(params, tectonics, rng);
      break;
    case "calved-cliff-boulders":
      macro = buildCalvedCliffBoulders(params, tectonics, rng);
      break;
    case "cliff-wall":
    default:
      macro = buildCliffWall(params, tectonics, rng);
      break;
  }

  if (hasEditedSplines && !NATIVE_SPLINE_ARCHETYPES.has(params.archetype)) {
    deformMacroFormationBySplines(macro, params.archetype, params.scale, params.formationSplines);
  }
  return macro;
}
