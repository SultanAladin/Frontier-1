/**
 * Additive 3D Geological Formation Builders for Canyon, Icelandic, Aeolian Yardang,
 * and Calved Cliff-Boulder Outcrops.
 *
 * Strictly additive: leaves all 19 baseline non-crack presets untouched.
 */

import {
  DEG2RAD,
  buildStrikeDipFrame,
  vnorm,
  vdot,
  clamp,
} from '../core/vec3.js';
import { scalarField3D } from '../core/spatial-noise.js';
import {
  createBoxPoly,
  clipPolyByHalfSpace,
  splitPolyByPlane,
  transformPoly,
  sculptGeologicalParentPoly,
  compilePolyBlock,
} from './polyhedron.js';
import {
  getFormationSplineSpec,
  compileSplineArcTable,
  compileCanyonTripletTable,
} from './formation-splines.js';

/**
 * 1. Grand Canyon Cliff-Bench Temple (`grand-canyon-temple`):
 *    Alternating cliff-forming hard limestone/sandstone units (Redwall, Coconino, Kaibab)
 *    and recessed slope-forming shales (Bright Angel, Supai, Hermit) with talus aprons.
 */
export function buildGrandCanyonTemple(params, tectonics, rng, addTalusAndFallenBoulders) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0014 + 0.0022 * (params.edgeWear ?? 0.26));

  const strikeRad = (params.strikeAngle ?? 6) * DEG2RAD;
  const dipRad = clamp((params.dipAngle ?? 4) * 0.35, 0, 14) * DEG2RAD;
  const frame = buildStrikeDipFrame(strikeRad, dipRad);

  // 5-tier Grand Canyon cliff-slope stratigraphic temple staircase
  const tiers = [
    { y0: 0.00, y1: 0.16, rx: 0.50, rz: 0.38, hard: 0.42, nSeg: 4 }, // Bright Angel / lower Supai apron
    { y0: 0.14, y1: 0.34, rx: 0.42, rz: 0.31, hard: 0.90, nSeg: 4 }, // Redwall vertical cliff
    { y0: 0.32, y1: 0.48, rx: 0.33, rz: 0.24, hard: 0.48, nSeg: 3 }, // Hermit recessed bench
    { y0: 0.46, y1: 0.65, rx: 0.26, rz: 0.18, hard: 0.92, nSeg: 3 }, // Coconino sheer sandstone cliff
    { y0: 0.63, y1: 0.76, rx: 0.19, rz: 0.13, hard: 0.88, nSeg: 2 }, // Kaibab caprock turret
  ];

  for (let t = 0; t < tiers.length; t++) {
    const tr = tiers[t];
    const yc = groundY + 0.5 * (tr.y0 + tr.y1) * S;
    const ry = 0.5 * (tr.y1 - tr.y0) * S * 1.12;

    for (let s = 0; s < tr.nSeg; s++) {
      const frac = tr.nSeg > 1 ? (s / (tr.nSeg - 1) - 0.5) : 0;
      const cx = frac * tr.rx * S * 0.92 + rng.signed() * S * 0.02;
      const cz = Math.sin(frac * 2.4 + t * 0.7) * S * 0.035 + rng.signed() * S * 0.018;
      const subRx = (tr.rx * S * (tr.nSeg > 1 ? 0.56 : 0.92)) * rng.range(0.90, 1.12);
      const subRz = tr.rz * S * rng.range(0.88, 1.12);

      let poly = sculptGeologicalParentPoly(
        [cx, yc, cz],
        [subRx, Math.max(ry, S * 0.06), subRz],
        frame,
        rng,
        {
          jointDispersion: tr.hard > 0.75 ? 0.11 : 0.22,
          cornerBlunt: tr.hard > 0.75 ? 0.84 : 0.68,
          flatBaseY: t === 0 ? groundY : null,
          extraBevelCuts: 6,
        }
      );
      if (!poly) continue;
      if (t === 0) poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;

      const comp = compilePolyBlock(poly, frame, {
        edgeBevel: bevel * (tr.hard > 0.75 ? 0.8 : 1.25),
        smoothUnionR: tr.hard < 0.55 ? S * 0.016 : S * 0.004,
        detailScale: 1.0,
        hardness: tr.hard,
        isSedimentary: true,
        seedSalt: rng.int(1, 999999),
      });
      if (comp) blocks.push(comp);
    }
  }

  if (addTalusAndFallenBoulders) {
    addTalusAndFallenBoulders(blocks, frame, rng, {
      scale: S,
      groundY,
      talusAmount: Math.max(0.42, params.talusAmount ?? 0.50),
      bevel,
      footprintX: S * 0.52,
      footprintZ: S * 0.40,
    });
  }

  return { blocks, cuts, groundY };
}

/**
 * 2. Stuðlagil Icelandic Columnar Basalt Gorge (`studlagil-colonnade`):
 *    True bilateral basalt river gorge driven by 3 splines (`Left Wall`, `Centre River`,
 *    `Right Wall`), with opposing stepped hexagonal prismatic basalt colonnade terraces,
 *    hackly overhanging entablature rims, and a sculpted riverside basalt channel bed.
 */
export function buildStudlagilColonnade(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0008 + 0.0012 * (params.edgeWear ?? 0.16));
  const overhang = params.overhangStrength ?? 0.78;
  const frame = buildStrikeDipFrame((params.strikeAngle ?? 4) * DEG2RAD, (params.dipAngle ?? 6) * DEG2RAD);
  const gorgeSalt = rng.int(1, 999999);

  const activeSplines = Array.isArray(params.formationSplines) && params.formationSplines.length >= 3
    ? params.formationSplines
    : getFormationSplineSpec('studlagil-colonnade', S).defaultCurves;
  const gorge = compileCanyonTripletTable(activeSplines, S, 96);

  const colRadius = S * 0.031;
  const colSpacingAlong = colRadius * 1.56;
  const nColsAlong = clamp(Math.round(gorge.totalLength / colSpacingAlong), 18, 44);
  const du = 1 / nColsAlong;

  // Helper: constructs an exact 6-sided hexagonal basalt prism (with optional slight columnar splay)
  function addHexBasaltColumn(cx, cz, baseY, topY, rad, leanVec, hardness = 0.88, salt = 1) {
    const colH = topY - baseY;
    if (colH < S * 0.030) return;
    const midY = baseY + colH * 0.5;
    let poly = createBoxPoly(
      cx - rad * 1.45,
      baseY,
      cz - rad * 1.45,
      cx + rad * 1.45,
      topY + rad * 0.65,
      cz + rad * 1.45,
      'hexBasalt'
    );
    const az0 = 0.16 + scalarField3D(cx * 0.7, 0.3, cz * 0.7, gorgeSalt + salt) * 0.08;
    for (let k = 0; k < 6; k++) {
      const a = az0 + (k * Math.PI) / 3;
      const nx = Math.cos(a) - leanVec[0] * 0.85;
      const nz = Math.sin(a) - leanVec[2] * 0.85;
      const hn = vnorm([nx, leanVec[1], nz]);
      poly = clipPolyByHalfSpace(poly, hn, vdot(hn, [cx, midY, cz]) + rad * 0.94, 'hexFace') || poly;
    }
    // Crisp sub-horizontal chisel/cup-and-ball basalt cross-joint cap
    const capN = vnorm([
      leanVec[0] * 0.4 + rng.signed() * 0.09,
      1.0,
      leanVec[2] * 0.4 + rng.signed() * 0.09,
    ]);
    poly = clipPolyByHalfSpace(poly, capN, vdot(capN, [cx, topY, cz]), 'colCap') || poly;
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
    if (!poly) return;

    const comp = compilePolyBlock(poly, frame, {
      edgeBevel: bevel * 0.68,
      smoothUnionR: 0,
      detailScale: 0.74,
      hardness,
      seedSalt: (gorgeSalt + salt * 37 + rng.int(1, 9999)) & 0x7fffffff,
    });
    if (comp) blocks.push(comp);
  }

  // 1. Sculpted Riverbed Channel Floor confined cleanly between the colonnade walls (no rectangular tray sticking out!)
  const nFloorSegs = clamp(Math.round(nColsAlong * 0.72), 14, 28);
  for (let i = 1; i < nFloorSegs - 1; i++) {
    const u0 = i / nFloorSegs;
    const u1 = (i + 1) / nFloorSegs;
    const uMid = 0.5 * (u0 + u1);
    const st0 = gorge.stationAt(u0);
    const st1 = gorge.stationAt(u1);
    const stM = gorge.stationAt(uMid);

    const halfW = 0.5 * (stM.halfGapL + stM.halfGapR) + colRadius * 1.6;
    const segLen = Math.max(S * 0.045, gorge.totalLength / nFloorSegs) * 0.66;
    let fPoly = sculptGeologicalParentPoly(
      [stM.center.x, groundY + stM.floorY * 0.48, stM.center.z],
      [halfW, Math.max(S * 0.020, stM.floorY * 0.58), segLen],
      frame,
      rng,
      { jointDispersion: 0.05, cornerBlunt: 0.88, flatBaseY: groundY, extraBevelCuts: 3 }
    );
    if (!fPoly) continue;
    const dFloor = (st1.floorY - st0.floorY) / Math.max(S * 0.04, segLen * 1.5);
    const floorN = vnorm([-stM.tx * dFloor, 1.0, -stM.tz * dFloor]);
    fPoly = clipPolyByHalfSpace(fPoly, floorN, vdot(floorN, [stM.center.x, groundY + stM.floorY, stM.center.z]), 'gorgeBed') || fPoly;
    fPoly = clipPolyByHalfSpace(fPoly, [0, -1, 0], -groundY, 'base') || fPoly;
    const compF = compilePolyBlock(fPoly, frame, {
      edgeBevel: bevel,
      smoothUnionR: S * 0.003,
      detailScale: 0.72,
      hardness: 0.84,
      seedSalt: (gorgeSalt + 8100 + i * 29) & 0x7fffffff,
    });
    if (compF) blocks.push(compF);
  }

  // 2. Bilateral Opposing Stuðlagil Hexagonal Basalt Organ-Pipe Colonnades (4 staggered rows per canyon wall!)
  //    Climbs naturally from water-worn hexagonal stepping-stone pedestals at river level up to towering
  //    full-height hexagonal columns at the canyon rim — ZERO chunky potato boulders perched on top!
  for (const side of [-1, 1]) {
    for (let ic = 0; ic < nColsAlong; ic++) {
      const u = (ic + 0.5) * du;
      const st = gorge.stationAt(u);
      const wallCrest = side < 0 ? st.crestL : st.crestR;
      const wallThickMul = side < 0 ? st.thickL : st.thickR;
      const portalTaper = clamp(1.04 - 0.30 * Math.pow(Math.abs(u - 0.5) * 2.0, 2.5), 0.54, 1.04);
      const localCrest = wallCrest * portalTaper;

      // 4 staggered hexagonal colonnade tiers from riverside stepping stones up to towering canyon rim
      for (let row = 0; row < 4; row++) {
        if (row === 0 && (ic === 0 || ic === nColsAlong - 1 || rng.chance(0.18))) continue;
        const staggerU = clamp(u + (row % 2 === 1 ? 0.5 * du : 0), 0.01, 0.99);
        const stR = gorge.stationAt(staggerU);
        const outX = side * stR.acrossX;
        const outZ = side * stR.acrossZ;
        const rGap = side < 0 ? stR.halfGapL : stR.halfGapR;
        const radialDist = rGap + (row * 1.38 + 0.25) * colRadius * wallThickMul;
        const jitterAlong = rng.signed() * colRadius * 0.10;
        const cx = stR.center.x + outX * radialDist + stR.tx * jitterAlong;
        const cz = stR.center.z + outZ * radialDist + stR.tz * jitterAlong;

        // Coherent organ-pipe wave along canyon wall so adjacent columns step like real basalt cross-joints!
        const waveA = 0.5 + 0.5 * Math.sin(staggerU * Math.PI * 3.6 + side * 0.8 + row * 0.7);
        const waveB = scalarField3D(staggerU * 7.2, row * 1.9, side * 2.1, gorgeSalt + 303) * 0.09;
        const tierFrac =
          row === 0 ? 0.14 + 0.13 * waveA + waveB
          : row === 1 ? 0.38 + 0.18 * waveA + waveB
          : row === 2 ? 0.72 + 0.18 * waveA + waveB
          : 0.92 + 0.10 * waveA + waveB * 0.6;
        const topY = groundY + clamp(localCrest * tierFrac, S * 0.065, localCrest * 1.04);

        // Classic Stuðlagil curving cooling-isotherm fan on upper columns!
        const splayMag = row >= 2 ? -0.048 * overhang * (row === 3 ? 1.15 : 0.85) : (row === 1 ? -0.018 : 0.012);
        const leanVec = [outX * splayMag, 0, outZ * splayMag];

        addHexBasaltColumn(
          cx,
          cz,
          groundY,
          topY,
          colRadius * (row >= 2 ? 1.04 : 0.96),
          leanVec,
          0.91,
          (side > 0 ? 2000 : 1000) + row * 200 + ic
        );
      }
    }

    // 3. Recessed Rear Basalt Plateau Slope behind the 4th Colonnade Row (smooth tangent rim, no top boulders!)
    const nBack = clamp(Math.round(nColsAlong * 0.55), 10, 20);
    for (let ie = 0; ie < nBack; ie++) {
      const u0 = ie / nBack;
      const u1 = (ie + 1) / nBack;
      const uMid = 0.5 * (u0 + u1);
      const st0 = gorge.stationAt(u0);
      const st1 = gorge.stationAt(u1);
      const st = gorge.stationAt(uMid);

      const crestAt = (s, uu) => (side < 0 ? s.crestL : s.crestR) *
        clamp(1.04 - 0.30 * Math.pow(Math.abs(uu - 0.5) * 2.0, 2.5), 0.54, 1.04) * 0.86;
      const h0 = crestAt(st0, u0);
      const h1 = crestAt(st1, u1);
      const hMid = crestAt(st, uMid);

      const wallGap = side < 0 ? st.halfGapL : st.halfGapR;
      const wallThick = S * 0.11 * (side < 0 ? st.thickL : st.thickR);
      const outX = side * st.acrossX;
      const outZ = side * st.acrossZ;
      const segLen = gorge.totalLength / nBack;
      const dhds = clamp((h1 - h0) / Math.max(S * 0.04, segLen), -0.85, 0.85);

      const backRadial = wallGap + colRadius * 4.75 + wallThick * 0.35;
      const bx = st.center.x + outX * backRadial;
      const bz = st.center.z + outZ * backRadial;
      const localF = buildStrikeDipFrame(Math.atan2(st.tz, st.tx), (6 + side * 4) * DEG2RAD);

      let corePoly = sculptGeologicalParentPoly(
        [bx, groundY + hMid * 0.46, bz],
        [segLen * 0.68, hMid * 0.52, wallThick * 0.64],
        localF,
        rng,
        { jointDispersion: 0.08, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 4 }
      );
      if (corePoly) {
        const topSlopeN = vnorm([-st.tx * dhds + outX * 0.28, 0.94, -st.tz * dhds + outZ * 0.28]);
        corePoly = clipPolyByHalfSpace(corePoly, topSlopeN, vdot(topSlopeN, [bx, groundY + hMid, bz]), 'rearRim') || corePoly;
        const extN = vnorm([outX * 0.88, 0.44, outZ * 0.88]);
        corePoly = clipPolyByHalfSpace(
          corePoly,
          extN,
          vdot(extN, [bx + outX * wallThick * 0.46, groundY + hMid * 0.48, bz + outZ * wallThick * 0.46]),
          'outerFlank'
        ) || corePoly;
        corePoly = clipPolyByHalfSpace(corePoly, [0, -1, 0], -groundY, 'base') || corePoly;
        const compCore = compilePolyBlock(corePoly, localF, {
          edgeBevel: bevel * 1.1,
          smoothUnionR: S * 0.0035,
          detailScale: 0.88,
          hardness: 0.86,
          seedSalt: (gorgeSalt + (side > 0 ? 5200 : 4200) + ie * 17) & 0x7fffffff,
        });
        if (compCore) blocks.push(compCore);
      }
    }
  }

  return { blocks, cuts, groundY };
}

/**
 * 3. Icelandic Subglacial Palagonite Tuya Crag (`icelandic-tuya-crag`):
 *    Steep hyaloclastite/palagonite ridge intruded by a resistant feeder-dike spine
 *    and flanked by frost-riven scree blocks.
 */
export function buildIcelandicTuyaCrag(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0014 + 0.0020 * (params.edgeWear ?? 0.30));
  const strikeRad = (params.strikeAngle ?? 22) * DEG2RAD;
  const dipRad = clamp(params.dipAngle ?? 42, 18, 68) * DEG2RAD;
  const frame = buildStrikeDipFrame(strikeRad, dipRad);

  const cragMasses = [
    { x:  0.00, y: 0.36, z:  0.00, rx: 0.26, ry: 0.42, rz: 0.22, dike: true },
    { x: -0.19, y: 0.26, z:  0.05, rx: 0.22, ry: 0.30, rz: 0.19, dike: false },
    { x:  0.20, y: 0.24, z: -0.04, rx: 0.21, ry: 0.28, rz: 0.18, dike: false },
    { x:  0.03, y: 0.18, z:  0.17, rx: 0.20, ry: 0.20, rz: 0.16, dike: false },
  ];

  for (const cm of cragMasses) {
    let poly = sculptGeologicalParentPoly(
      [cm.x * S, groundY + cm.y * S, cm.z * S],
      [cm.rx * S, cm.ry * S, cm.rz * S],
      frame,
      rng,
      { jointDispersion: 0.22, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 7 }
    );
    if (!poly) continue;
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
    const comp = compilePolyBlock(poly, frame, {
      edgeBevel: bevel * (cm.dike ? 0.75 : 1.15),
      smoothUnionR: cm.dike ? S * 0.004 : S * 0.012,
      detailScale: cm.dike ? 1.12 : 0.96,
      seedSalt: rng.int(1, 999999),
    });
    if (comp) blocks.push(comp);
  }

  // Frost-riven basaltic spall boulders settled around crag skirt
  for (let i = 0; i < 10; i++) {
    const a = (i / 10) * Math.PI * 2 + rng.signed() * 0.22;
    const rad = S * rng.range(0.24, 0.40);
    const rB = S * rng.range(0.042, 0.072);
    const bf = buildStrikeDipFrame(a, rng.range(18, 56) * DEG2RAD);
    let bPoly = sculptGeologicalParentPoly(
      [Math.cos(a) * rad, groundY + rB * 0.48, Math.sin(a) * rad * 0.85],
      [rB * 1.25, rB * 0.65, rB * 0.95],
      bf,
      rng,
      { jointDispersion: 0.24, cornerBlunt: 0.68, flatBaseY: groundY, extraBevelCuts: 5 }
    );
    if (!bPoly) continue;
    bPoly = clipPolyByHalfSpace(bPoly, [0, -1, 0], -groundY, 'base') || bPoly;
    const compB = compilePolyBlock(bPoly, bf, {
      edgeBevel: bevel * 0.85,
      detailScale: 0.95,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compB) blocks.push(compB);
  }

  return { blocks, cuts, groundY };
}

/**
 * 4. Saharan Wind-Carved Yardang Fin (`saharan-yardang-fin`):
 *    True aerodynamic Lut / Borkou / White Desert yardang fin driven by its Spine Spline:
 *    - Towering overhanging sphinx/ship-prow upwind bluff nose (`u = 0`),
 *    - Deeply sandblasted concave saltation undercut waist near the desert floor (`0.04..0.28 H`),
 *    - Wind-etched differential horizontal sandstone flutes/ledges along streamlined flanks,
 *    - Razor-backed dorsal fin crest tapering smoothly downwind into a low leeward tail (`u = 1`),
 *    - Grounded desert plinth + subordinate streamlined satellite whaleback ridge.
 */
export function buildSaharanYardangFin(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0013 + 0.0020 * (params.edgeWear ?? 0.46));
  const overhang = params.overhangStrength ?? 0.82;
  const dipRad = clamp((params.dipAngle ?? 5) * 0.4, 0, 12) * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? -8) * DEG2RAD;
  const baseFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const yardSalt = rng.int(1, 999999);

  const activeSplines = Array.isArray(params.formationSplines) && params.formationSplines.length >= 1
    ? params.formationSplines
    : getFormationSplineSpec('saharan-yardang-fin', S).defaultCurves;
  const spine = compileSplineArcTable(activeSplines[0], 96);

  // 12 overlapping hull stations with buried end-caps (`halfAlong = 0.85 * segLen` + recessed guard planes at +/-0.28):
  // Every internal slice end-cap terminates 1.5 cm underground inside its neighbor -> 100% seamless aerodynamic hull!
  const nStations = clamp(Math.round(spine.totalHorizLength / (S * 0.11)), 10, 18);
  const du = 1 / nStations;
  const peakRefH = S * 0.68;

  // Fixed horizontal lacustrine / aeolian sandstone strata cut diagonally by the streamlined aerodynamic keel!
  //   tier 0: grounded desert bedrock plinth (`0.00..0.11 H`)
  //   tier 1: deeply sandblasted concave saltation undercut waist (`0.09..0.32 H` — narrowed by 42%!)
  //   tier 2: resistant lower-middle wind-fluted sandstone ledge (`0.28..0.52 H`)
  //   tier 3: recessed soft siltstone flute groove (`0.49..0.64 H`)
  //   tier 4: overhanging Sphinx prow brow & razor-streamlined dorsal fin (`0.60..1.05 H`)
  const horizStrata = [
    { y0: 0.00, y1: 0.11, wMul: 1.08, prowAdv: -0.020, hard: 0.80, tiltY:  0.40 },
    { y0: 0.09, y1: 0.32, wMul: 0.56 - 0.10 * overhang, prowAdv: -0.065 * overhang, hard: 0.40, tiltY: -0.22 },
    { y0: 0.28, y1: 0.52, wMul: 0.94, prowAdv:  0.025 * overhang, hard: 0.88, tiltY:  0.14 },
    { y0: 0.49, y1: 0.64, wMul: 0.82, prowAdv:  0.015 * overhang, hard: 0.58, tiltY:  0.08 },
    { y0: 0.60, y1: 1.08, wMul: 0.88, prowAdv:  0.085 * overhang, hard: 0.94, tiltY:  0.58 },
  ];

  function yardangHalfWidthAt(u, rMul) {
    // Smooth aerodynamic teardrop hull: rounded bluff prow at u~0.14, sleek unbroken taper to u=1
    const airfoil = Math.pow(clamp(u + 0.05, 0.03, 1.0), 0.38) * Math.pow(clamp(1.04 - u, 0.08, 1.04), 1.32) * 1.98;
    const gentleWave = 1.0 + 0.035 * Math.sin(u * Math.PI * 2.4 + 0.4);
    return S * 0.142 * clamp(airfoil, 0.18, 1.06) * rMul * gentleWave;
  }

  function yardangCrestAt(u, splineY) {
    // Towering upwind Sphinx prow rising to peak at u~0.11, then sweeping unbroken dorsal keel down to leeward tail
    const prowRise = 0.80 + 0.22 * smoothstep01(u / 0.11);
    const smoothSpine = splineY * prowRise * (1.0 + 0.022 * Math.sin(u * Math.PI * 3.0));
    return Math.max(S * 0.085, smoothSpine);
  }

  for (let i = 0; i < nStations; i++) {
    const u0 = i * du;
    const u1 = (i + 1) * du;
    const uMid = 0.5 * (u0 + u1);
    const st0 = spine.sampleAtArc(u0);
    const st1 = spine.sampleAtArc(u1);
    const stM = spine.sampleAtArc(uMid);

    const localF = buildStrikeDipFrame(Math.atan2(stM.tz, stM.tx), dipRad);
    const hCrest = yardangCrestAt(uMid, stM.y);
    const h0 = yardangCrestAt(u0, st0.y);
    const h1 = yardangCrestAt(u1, st1.y);
    const baseHalfW = yardangHalfWidthAt(uMid, stM.r);
    const w0 = yardangHalfWidthAt(u0, st0.r);
    const w1 = yardangHalfWidthAt(u1, st1.r);
    const segLen = spine.totalHorizLength / nStations;
    const dWds = clamp((w1 - w0) / Math.max(S * 0.03, segLen), -0.80, 0.80);
    const dHds = clamp((h1 - h0) / Math.max(S * 0.03, segLen), -0.95, 0.95);

    for (let tIdx = 0; tIdx < horizStrata.length; tIdx++) {
      const tr = horizStrata[tIdx];
      const yBot = groundY + tr.y0 * peakRefH;
      const rawTop = groundY + tr.y1 * peakRefH;
      if (yBot > groundY + hCrest - S * 0.018) continue;
      const yTop = Math.min(rawTop, groundY + hCrest);
      if (yTop - yBot < S * 0.016) continue;

      const isCappedByCrest = rawTop >= groundY + hCrest - S * 0.015;
      const prowFactor = Math.exp(-Math.pow(uMid / 0.24, 2.0));
      const shiftAlong = -tr.prowAdv * S * prowFactor;
      const cx = stM.x + stM.tx * shiftAlong;
      const cz = stM.z + stM.tz * shiftAlong;
      const tierHalfW = baseHalfW * tr.wMul;
      const halfAlong = segLen * 0.86;

      const reach = Math.max(tierHalfW, halfAlong) * 1.7 + S * 0.08;
      let poly = createBoxPoly(
        cx - reach,
        tIdx === 0 ? groundY : yBot,
        cz - reach,
        cx + reach,
        yTop + S * 0.06,
        cz + reach,
        'yardangHull'
      );

      const midY = 0.5 * (yBot + yTop);
      if (i === 0) {
        const halfNose = segLen * 0.50;
        const prowLean = tIdx >= 2 ? -0.36 * (0.55 + 0.55 * overhang) : (tIdx === 1 ? 0.42 : 0.16);
        const nosePt = [cx - stM.tx * halfNose * 0.68, midY, cz - stM.tz * halfNose * 0.68];
        const prowN = vnorm([-stM.tx, prowLean, -stM.tz]);
        const bowR = vnorm([-stM.tx * 0.80 + stM.nx * 0.58, prowLean * 0.85 + 0.08, -stM.tz * 0.80 + stM.nz * 0.58]);
        const bowL = vnorm([-stM.tx * 0.80 - stM.nx * 0.58, prowLean * 0.85 + 0.08, -stM.tz * 0.80 - stM.nz * 0.58]);
        poly = clipPolyByHalfSpace(poly, prowN, vdot(prowN, nosePt), 'sphinxProw') || poly;
        poly = clipPolyByHalfSpace(poly, bowR, vdot(bowR, nosePt) + tierHalfW * 0.24, 'bowFacetR') || poly;
        poly = clipPolyByHalfSpace(poly, bowL, vdot(bowL, nosePt) + tierHalfW * 0.24, 'bowFacetL') || poly;
      } else {
        const backN = [-stM.tx, 0, -stM.tz];
        poly = clipPolyByHalfSpace(poly, backN, vdot(backN, [cx - stM.tx * halfAlong, midY, cz - stM.tz * halfAlong]), 'stBack') || poly;
      }

      if (i === nStations - 1) {
        const halfTail = segLen * 0.50;
        const tailN = vnorm([stM.tx * 0.86, 0.50, stM.tz * 0.86]);
        poly = clipPolyByHalfSpace(poly, tailN, vdot(tailN, [cx + stM.tx * halfTail * 0.75, midY, cz + stM.tz * halfTail * 0.75]), 'leeTail') || poly;
      } else {
        const fwdN = [stM.tx, 0, stM.tz];
        poly = clipPolyByHalfSpace(poly, fwdN, vdot(fwdN, [cx + stM.tx * halfAlong, midY, cz + stM.tz * halfAlong]), 'stFwd') || poly;
      }

      // 5-point buried-overlap tangent stencil (`subF = 0, 0.5, 1` exact + `-0.30, +1.30` recessed guard planes):
      // Buries `stBack` and `stFwd` 1.5 cm underground inside neighboring stations so zero end-caps reach the skin!
      const flankTiltY = isCappedByCrest ? Math.max(tr.tiltY, 0.54) : tr.tiltY;
      for (const subF of [-0.30, 0.0, 0.50, 1.0, 1.30]) {
        if ((subF < 0 && i === 0) || (subF > 1 && i === nStations - 1)) continue;
      const isGuard = subF < -0.01 || subF > 1.01;
      const buryIn = isGuard ? S * 0.026 : 0.0;
      const uS = clamp(u0 + (u1 - u0) * subF, 0.005, 0.995);
      const uSm = Math.max(0, uS - 0.018);
      const uSp = Math.min(1, uS + 0.018);
      const stS = spine.sampleAtArc(uS);
      const stSm = spine.sampleAtArc(uSm);
      const stSp = spine.sampleAtArc(uSp);
      const dsLocal = Math.max(S * 0.02, (uSp - uSm) * spine.totalHorizLength);
      const wS = Math.max(S * 0.02, yardangHalfWidthAt(uS, stS.r) * tr.wMul - buryIn);
      const dwSign = 0.0;
        const dwLocal = clamp(
          (yardangHalfWidthAt(uSp, stSp.r) - yardangHalfWidthAt(uSm, stSm.r)) * tr.wMul / dsLocal + dwSign,
          -0.85,
          0.85
        );
        const pS = Math.exp(-Math.pow(uS / 0.24, 2.0));
        const sx = stS.x - stS.tx * tr.prowAdv * S * pS;
        const sz = stS.z - stS.tz * tr.prowAdv * S * pS;

        const nRight = vnorm([stS.nx - stS.tx * dwLocal, flankTiltY, stS.nz - stS.tz * dwLocal]);
        const nLeft = vnorm([-stS.nx - stS.tx * dwLocal, flankTiltY, -stS.nz - stS.tz * dwLocal]);
        poly = clipPolyByHalfSpace(poly, nRight, vdot(nRight, [sx + stS.nx * wS, midY, sz + stS.nz * wS]), 'flankR') || poly;
        poly = clipPolyByHalfSpace(poly, nLeft, vdot(nLeft, [sx - stS.nx * wS, midY, sz - stS.nz * wS]), 'flankL') || poly;

        if (isCappedByCrest) {
          const hS = yardangCrestAt(uS, stS.y) - buryIn * 0.85;
          const dhLocal = clamp(
            (yardangCrestAt(uSp, stSp.y) - yardangCrestAt(uSm, stSm.y)) / dsLocal + dwSign,
            -0.95,
            0.95
          );
          const crestN = vnorm([-stS.tx * dhLocal, 1.0, -stS.tz * dhLocal]);
          poly = clipPolyByHalfSpace(poly, crestN, vdot(crestN, [sx, groundY + hS, sz]), 'dorsalCrest') || poly;
        }
      }
      if (!isCappedByCrest) {
        poly = clipPolyByHalfSpace(poly, [0, 1, 0], rawTop, 'eolianLedge') || poly;
      }

      poly = clipPolyByHalfSpace(poly, [0, -1, 0], -(tIdx === 0 ? groundY : yBot), 'tierBot') || poly;
      poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
      if (!poly) continue;

      const comp = compilePolyBlock(poly, baseFrame, {
        edgeBevel: bevel * 0.28,
        smoothUnionR: 0, // Exact min union prevents sequential smin slice-overlap quilt ribs!
        detailScale: tr.hard < 0.55 ? 0.65 : 0.42,
        hardness: tr.hard,
        isEolian: true,
        noSeamNotch: true,
        seedSalt: (yardSalt + tIdx * 37) & 0x7fffffff,
      });
      if (comp) blocks.push(comp);
    }
  }

  // Subordinate streamlined companion whaleback yardang fin offset downwind-parallel (buried-overlap seamless hull!)
  const nWhale = 6;
  for (let w = 0; w < nWhale; w++) {
    const u0 = 0.22 + (w / nWhale) * 0.56;
    const u1 = 0.22 + ((w + 1) / nWhale) * 0.56;
    const uw = 0.5 * (u0 + u1);
    const stW = spine.sampleAtArc(uw);
    const segW = (spine.totalHorizLength * 0.56) / nWhale;
    const halfWAlong = segW * 0.88;
    const latDist = S * 0.24 * stW.r;
    const wx = stW.x + stW.nx * latDist;
    const wz = stW.z + stW.nz * latDist;
    const reachW = halfWAlong * 1.6 + S * 0.14;
    let fPoly = createBoxPoly(wx - reachW, groundY, wz - reachW, wx + reachW, groundY + S * 0.22, wz + reachW, 'whaleback');

    if (w === 0) {
      const bkN = vnorm([-stW.tx * 0.84, 0.54, -stW.tz * 0.84]);
      fPoly = clipPolyByHalfSpace(fPoly, bkN, vdot(bkN, [wx - stW.tx * segW * 0.46, groundY + S * 0.04, wz - stW.tz * segW * 0.46]), 'whBk') || fPoly;
    } else {
      fPoly = clipPolyByHalfSpace(fPoly, [-stW.tx, 0, -stW.tz], vdot([-stW.tx, 0, -stW.tz], [wx - stW.tx * halfWAlong, groundY + S * 0.04, wz - stW.tz * halfWAlong]), 'whBk') || fPoly;
    }
    if (w === nWhale - 1) {
      const fwN = vnorm([stW.tx * 0.80, 0.60, stW.tz * 0.80]);
      fPoly = clipPolyByHalfSpace(fPoly, fwN, vdot(fwN, [wx + stW.tx * segW * 0.46, groundY + S * 0.04, wz + stW.tz * segW * 0.46]), 'whFw') || fPoly;
    } else {
      fPoly = clipPolyByHalfSpace(fPoly, [stW.tx, 0, stW.tz], vdot([stW.tx, 0, stW.tz], [wx + stW.tx * halfWAlong, groundY + S * 0.04, wz + stW.tz * halfWAlong]), 'whFw') || fPoly;
    }

    for (const subF of [-0.30, 0.0, 0.50, 1.0, 1.30]) {
      if ((subF < 0 && w === 0) || (subF > 1 && w === nWhale - 1)) continue;
      const isG = subF < -0.01 || subF > 1.01;
      const bury = isG ? S * 0.024 : 0.0;
      const uS = u0 + (u1 - u0) * subF;
      const tLoc = clamp((uS - 0.22) / 0.56, 0.02, 0.98);
      const env = Math.sin(Math.PI * clamp(tLoc * 0.92 + 0.04, 0.04, 0.96));
      const stS = spine.sampleAtArc(uS);
      const sx = stS.x + stS.nx * S * 0.24 * stS.r;
      const sz = stS.z + stS.nz * S * 0.24 * stS.r;
      const wh = Math.max(S * 0.018, S * (0.042 + 0.125 * env * (1.0 - 0.30 * tLoc)) - bury);
      const ww = Math.max(S * 0.018, S * (0.036 + 0.048 * env) - bury);
      const rN = vnorm([stS.nx * 0.80, 0.60, stS.nz * 0.80]);
      const lN = vnorm([-stS.nx * 0.80, 0.60, -stS.nz * 0.80]);
      fPoly = clipPolyByHalfSpace(fPoly, rN, vdot(rN, [sx + stS.nx * ww, groundY + wh * 0.45, sz + stS.nz * ww]), 'whR') || fPoly;
      fPoly = clipPolyByHalfSpace(fPoly, lN, vdot(lN, [sx - stS.nx * ww, groundY + wh * 0.45, sz - stS.nz * ww]), 'whL') || fPoly;
      const dEnv = Math.PI * 0.92 * Math.cos(Math.PI * clamp(tLoc * 0.92 + 0.04, 0.04, 0.96)) * S * 0.11 / Math.max(S * 0.1, spine.totalHorizLength * 0.56);
      const topWN = vnorm([-stS.tx * dEnv, 1.0, -stS.tz * dEnv]);
      fPoly = clipPolyByHalfSpace(fPoly, topWN, vdot(topWN, [sx, groundY + wh, sz]), 'whTop') || fPoly;
    }
    fPoly = clipPolyByHalfSpace(fPoly, [0, -1, 0], -groundY, 'base') || fPoly;
    if (!fPoly) continue;
    const compW = compilePolyBlock(fPoly, baseFrame, {
      edgeBevel: bevel * 0.28,
      smoothUnionR: 0,
      detailScale: 0.45,
      hardness: 0.86,
      isEolian: true,
      noSeamNotch: true,
      seedSalt: (yardSalt + 9100) & 0x7fffffff,
    });
    if (compW) blocks.push(compW);
  }

  return { blocks, cuts, groundY };
}

function smoothstep01(t) {
  const x = clamp(t, 0, 1);
  return x * x * (3 - 2 * x);
}

/**
 * 5. Calved Escarpment Outcrop & Fallen Megaboulders (`calved-cliff-boulders`):
 *    Fractured parent outcrop nose from which large angular blocks have detached,
 *    tumbled downslope, and settled naturally across the toe apron.
 */
export function buildCalvedCliffBoulders(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];
  const bevel = S * (0.0012 + 0.0020 * (params.edgeWear ?? 0.26));
  const strikeRad = (params.strikeAngle ?? 14) * DEG2RAD;
  const dipRad = clamp(params.dipAngle ?? 26, 10, 54) * DEG2RAD;
  const frame = buildStrikeDipFrame(strikeRad, dipRad);

  // Rear fractured parent outcrop nose with fresh detachment scar
  let parent = sculptGeologicalParentPoly(
    [-S * 0.10, groundY + S * 0.30, -S * 0.12],
    [S * 0.28, S * 0.34, S * 0.24],
    frame,
    rng,
    { jointDispersion: 0.20, cornerBlunt: 0.76, flatBaseY: groundY, extraBevelCuts: 7 }
  );
  if (parent) {
    const scarN = vnorm([0.42, 0.24, 0.87]);
    const [rearMass, detachedSlab] = splitPolyByPlane(parent, scarN, -S * 0.02, S * 0.022, 'calveScar');
    if (rearMass) {
      const gp = clipPolyByHalfSpace(rearMass, [0, -1, 0], -groundY, 'base') || rearMass;
      const compP = compilePolyBlock(gp, frame, {
        edgeBevel: bevel,
        smoothUnionR: S * 0.005,
        detailScale: 1.05,
        seedSalt: rng.int(1, 999999),
      });
      if (compP) blocks.push(compP);
    }
    if (detachedSlab) {
      // Slide & tilt the freshly calved block down to rest against the base of the scar!
      let fallen = transformPoly(
        detachedSlab,
        [S * 0.12, -S * 0.14, S * 0.18],
        [1, 0, -0.4],
        0.42,
        [0, groundY + S * 0.12, 0]
      );
      fallen = clipPolyByHalfSpace(fallen, [0, -1, 0], -groundY, 'base') || fallen;
      const compD = compilePolyBlock(fallen, frame, {
        edgeBevel: bevel * 0.85,
        detailScale: 1.04,
        isBoulder: true,
        seedSalt: rng.int(1, 999999),
      });
      if (compD) blocks.push(compD);
    }
  }

  // Gravity-settled megaboulders & tabular spall blocks fanned downslope in front (+X/+Z)
  const boulderSpecs = [
    { x:  0.18, z:  0.16, rx: 0.14, ry: 0.11, rz: 0.12, tilt: 28 },
    { x: -0.08, z:  0.22, rx: 0.11, ry: 0.085, rz: 0.10, tilt: 36 },
    { x:  0.29, z: -0.02, rx: 0.10, ry: 0.080, rz: 0.09, tilt: 22 },
    { x:  0.06, z:  0.31, rx: 0.085, ry: 0.062, rz: 0.08, tilt: 41 },
    { x: -0.24, z:  0.14, rx: 0.090, ry: 0.070, rz: 0.085, tilt: 32 },
    { x:  0.24, z:  0.29, rx: 0.072, ry: 0.052, rz: 0.068, tilt: 19 },
  ];
  for (const bs of boulderSpecs) {
    const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, bs.tilt * DEG2RAD);
    let bPoly = sculptGeologicalParentPoly(
      [bs.x * S, groundY + bs.ry * S * 0.72, bs.z * S],
      [bs.rx * S, bs.ry * S, bs.rz * S],
      bf,
      rng,
      { jointDispersion: 0.22, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 6 }
    );
    if (!bPoly) continue;
    bPoly = clipPolyByHalfSpace(bPoly, [0, -1, 0], -groundY, 'base') || bPoly;
    const compB = compilePolyBlock(bPoly, bf, {
      edgeBevel: bevel * 0.90,
      detailScale: 1.0,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compB) blocks.push(compB);
  }

  return { blocks, cuts, groundY };
}
