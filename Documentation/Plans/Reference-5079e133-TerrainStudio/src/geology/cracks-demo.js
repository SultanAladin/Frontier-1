/**
 * Dedicated 3D Brittle Fracture-Mechanics & Jigsaw Split Monolith (`cracked-monolith` ONLY).
 *
 * Strictly isolated to the single `'cracked-monolith'` preset ("Cracked Monolith (3D Cracks)")
 * so NO other rock formation in the studio is modified or affected.
 *
 * Uses Exact 3D Polyhedral Brittle-Fracture Kinematics + Branching Surface Stress Fissures:
 * 1. Unified Craggy Granite Spire Parent Massif:
 *    Sculpted first as coherent, towering granite buttresses and spires, then progressively
 *    cleaved along real geological fracture planes so opposing walls match 1-to-1 like a jigsaw puzzle!
 * 2. Crooked Stepped Master Chasm Split (En-Echelon Stepped Fault Planes + Frost-Wedge Tilt):
 *    Cleaved across stepped conjugate master fault segments and kinematically rotated & heaved
 *    apart from a deep basal hinge — widening naturally toward the sky with wedged chockstones!
 * 3. Multi-Order Branching Spire Splits, Sheeting Steps & Surface Stress Cracks:
 *    Upper crowns and flanks split along secondary Y-fork and conjugate planes with genuine
 *    rigid-body tilt, shear slip, minimum-thickness filtering (zero thin slivers!), plus
 *    realistic crooked branching en-echelon stress fissures recessed into broad granite faces.
 * 4. Matching 3D Undulating Fracture-Wall Relief & Plumose Hackle Striations:
 *    Correlated 3D spatial noise warps opposing fracture walls identically so left and right
 *    walls interlock with real crooked rock roughness, iron oxide staining, and volumetric AO.
 */

import {
  DEG2RAD,
  buildStrikeDipFrame,
  vnorm,
  vdot,
  clamp,
  smoothstep,
} from '../core/vec3.js';
import { scalarField3D } from '../core/spatial-noise.js';
import {
  clipPolyByHalfSpace,
  splitPolyByPlane,
  transformPoly,
  sculptGeologicalParentPoly,
  compilePolyBlock,
} from './polyhedron.js';

/** Check that a fractured polyhedron is a substantial rock block (never a paper-thin splinter). */
function isSubstantialBlock(poly, minThickness) {
  if (!poly || !poly.faces || poly.faces.length < 4) return false;
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (let f = 0; f < poly.faces.length; f++) {
    const fVerts = poly.faces[f].verts;
    for (let i = 0; i < fVerts.length; i++) {
      const v = fVerts[i];
      if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
      if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
      if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
    }
  }
  return (maxX - minX) >= minThickness && (maxY - minY) >= minThickness && (maxZ - minZ) >= minThickness;
}

export function buildCrackedMonolith(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const aperture = params.jointAperture ?? 0.58;
  const overhang = params.overhangStrength ?? 0.56;
  const faultSlip = params.faultIntensity ?? 0.48;
  const bevel = S * (0.0010 + 0.0016 * (params.edgeWear ?? 0.24));
  const minBlockThick = S * 0.068; // ~5 voxels minimum block dimension -> zero splinters!

  const strikeRad = (params.strikeAngle ?? 18) * DEG2RAD;
  const dipRad = clamp(params.dipAngle ?? 34, 18, 58) * DEG2RAD;
  const crackSalt = rng.int(1000, 999999);

  // Align the master split (~26 deg) directly with the default 3/4 Hero camera (`yaw ~ 27.5 deg`)
  // so the user immediately sees straight through the split granite chasm & wedged chockstones!
  const chasmAz = 0.455 + rng.signed() * 0.025;
  const masterT = [Math.sin(chasmAz), 0, Math.cos(chasmAz)];  // Along chasm (back -> hero camera)
  const masterN = [Math.cos(chasmAz), 0, -Math.sin(chasmAz)]; // Across chasm (hero-left -> hero-right)

  const toWorld = (across, yFrac, along) => [
    masterN[0] * (across * S) + masterT[0] * (along * S),
    groundY + yFrac * S,
    masterN[2] * (across * S) + masterT[2] * (along * S),
  ];

  // Track fracture wall planes for fresh-split interior coloring & plumose hackle detail
  const fracturePlanes = [];
  const registerFracturePlane = (nx, ny, nz, d, halfGap) => {
    fracturePlanes.push({ nx, ny, nz, d, halfGap });
  };

  // Master chasm opening kinematics: BOTH halves frost-wedge slightly outward from basal hinges,
  // while the right massif also drops along normal-fault heave & tilts dramatically!
  const baseSep = (0.015 + 0.012 * aperture) * S; // Clean open separation at base (~28-35 cm)
  const leftTiltRad = 0.036 + 0.022 * aperture;   // Left spire leans slightly outward-left
  const rightTiltRad = -(0.088 + 0.048 * aperture); // Right massif leans outward-right
  const rightDropY = -(0.038 + 0.024 * faultSlip) * S;
  const rightSlipAlong = (0.016 + 0.014 * faultSlip) * S;
  const leftHinge = toWorld(-0.08, 0.02, 0.0);
  const rightHinge = toWorld(0.10, 0.02, 0.0);

  // ============================================================================
  // 1. MULTI-SPIRE GRANITE PARENT MASSIF -> KINEMATICALLY CLEAVED & DISPLACED
  // ============================================================================
  const spireButtresses = [
    // Central twin-horn summit monolith (tallest central spire straddling the master chasm)
    { across:  0.00, y: 0.50, along: -0.01, rx: 0.28, ry: 0.55, rz: 0.25, sharp: 1.08, azOff:  0.22, kink:  0.05, fDip:  0.05, crownFork: true },
    // Front steeply undercut prow buttress (jogs slightly left so chasm kinks front-to-back!)
    { across: -0.01, y: 0.31, along:  0.15, rx: 0.25, ry: 0.34, rz: 0.19, sharp: 0.90, azOff: -0.32, kink: -0.07, fDip: -0.06, crownFork: false },
    // Rear craggy ridge spine (jogs slightly right)
    { across:  0.01, y: 0.36, along: -0.15, rx: 0.25, ry: 0.40, rz: 0.19, sharp: 0.96, azOff:  2.25, kink:  0.08, fDip:  0.04, crownFork: true },
    // Upper-left secondary granite horn (cleaved by Y-fork branching fissure!)
    { across: -0.17, y: 0.44, along: -0.02, rx: 0.19, ry: 0.47, rz: 0.21, sharp: 1.12, azOff:  0.88, kink:  0.00, fDip:  0.00, crownFork: true },
    // Upper-right canted shoulder spire (cleaved by right conjugate splay!)
    { across:  0.16, y: 0.41, along:  0.02, rx: 0.19, ry: 0.44, rz: 0.21, sharp: 1.05, azOff: -0.76, kink:  0.00, fDip:  0.00, crownFork: true },
    // Lower-left outer buttress
    { across: -0.24, y: 0.24, along:  0.07, rx: 0.17, ry: 0.27, rz: 0.21, sharp: 0.84, azOff:  1.15, kink:  0.00, fDip:  0.00, crownFork: false },
    // Lower-right outer buttress
    { across:  0.24, y: 0.22, along: -0.04, rx: 0.17, ry: 0.25, rz: 0.21, sharp: 0.82, azOff: -1.04, kink:  0.00, fDip:  0.00, crownFork: false },
  ];

  for (let i = 0; i < spireButtresses.length; i++) {
    const sb = spireButtresses[i];
    const center = toWorld(sb.across, sb.y, sb.along);
    const bf = buildStrikeDipFrame(
      strikeRad + rng.signed() * 0.08,
      dipRad + rng.signed() * 0.06
    );

    let poly = sculptGeologicalParentPoly(
      center,
      [sb.rx * S, sb.ry * S, sb.rz * S],
      bf,
      rng,
      {
        jointDispersion: 0.19,
        cornerBlunt: 0.76,
        flatBaseY: groundY,
        extraBevelCuts: 8,
      }
    );
    if (!poly) continue;

    // Sculpt craggy 3-plane dihedral arête peaks
    const cAz = strikeRad + sb.azOff;
    const steep = 0.56 * sb.sharp;
    const pN1 = vnorm([Math.cos(cAz) * 0.80, steep, Math.sin(cAz) * 0.80]);
    const pN2 = vnorm([Math.cos(cAz + 2.10) * 0.78, steep * 1.06, Math.sin(cAz + 2.10) * 0.78]);
    const pN3 = vnorm([Math.cos(cAz - 2.08) * 0.76, steep * 1.12, Math.sin(cAz - 2.08) * 0.76]);
    const apexPt = [center[0], groundY + (sb.y + sb.ry * 0.86) * S, center[2]];
    poly = clipPolyByHalfSpace(poly, pN1, vdot(pN1, apexPt) + sb.rx * 0.11 * S, 'arete1') || poly;
    poly = clipPolyByHalfSpace(poly, pN2, vdot(pN2, apexPt) + sb.rx * 0.13 * S, 'arete2') || poly;
    poly = clipPolyByHalfSpace(poly, pN3, vdot(pN3, apexPt) + sb.rx * 0.15 * S, 'arete3') || poly;

    if (sb.along > 0.05) {
      const browN = vnorm([
        masterT[0] * 0.88 + rng.signed() * 0.10,
        -0.28 * (0.45 + 0.60 * overhang),
        masterT[2] * 0.88 + rng.signed() * 0.10,
      ]);
      const browPt = [
        center[0] + masterT[0] * sb.rz * 0.92 * S,
        groundY + S * 0.10,
        center[2] + masterT[2] * sb.rz * 0.92 * S,
      ];
      poly = clipPolyByHalfSpace(poly, browN, vdot(browN, browPt), 'undercut') || poly;
    }

    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;

    // --- TIER 1: Cleave across crooked Master Fault Plane & Frost-Wedge Both Halves Apart ---
    const localAz = chasmAz + sb.kink;
    const localFaultN = vnorm([Math.cos(localAz), sb.fDip, -Math.sin(localAz)]);
    const localOffset = sb.kink * 0.12 * S;
    registerFracturePlane(localFaultN[0], localFaultN[1], localFaultN[2], localOffset, baseSep * 1.4);

    const sideHalves = [];
    if (Math.abs(sb.across) < 0.10) {
      const [leftP, rightP] = splitPolyByPlane(poly, localFaultN, localOffset, baseSep, 'masterChasm');
      if (isSubstantialBlock(leftP, minBlockThick)) {
        const tiltedL = transformPoly(
          leftP,
          [-masterN[0] * baseSep * 0.45, 0, -masterN[2] * baseSep * 0.45],
          masterT,
          leftTiltRad,
          leftHinge
        );
        sideHalves.push({ p: tiltedL, side: -1 });
      }
      if (isSubstantialBlock(rightP, minBlockThick)) {
        const tiltedR = transformPoly(
          rightP,
          [
            masterN[0] * baseSep * 0.65 + masterT[0] * rightSlipAlong,
            rightDropY,
            masterN[2] * baseSep * 0.65 + masterT[2] * rightSlipAlong,
          ],
          masterT,
          rightTiltRad,
          rightHinge
        );
        sideHalves.push({ p: tiltedR, side: 1 });
      }
    } else if (sb.across < 0) {
      const tiltedL = transformPoly(
        poly,
        [-masterN[0] * baseSep * 0.45, 0, -masterN[2] * baseSep * 0.45],
        masterT,
        leftTiltRad,
        leftHinge
      );
      sideHalves.push({ p: tiltedL, side: -1 });
    } else {
      const tiltedR = transformPoly(
        poly,
        [
          masterN[0] * baseSep * 0.65 + masterT[0] * rightSlipAlong,
          rightDropY,
          masterN[2] * baseSep * 0.65 + masterT[2] * rightSlipAlong,
        ],
        masterT,
        rightTiltRad,
        rightHinge
      );
      sideHalves.push({ p: tiltedR, side: 1 });
    }

    // --- TIER 2: Secondary Y-Fork Branching Spire Splits (Grounded Twin Sub-Spires!) ---
    const branchedPieces = [];
    for (const sh of sideHalves) {
      if (sb.crownFork && Math.abs(sb.across) >= 0.12) {
        const forkN = vnorm([
          sh.side * masterN[0] * 0.80 + masterT[0] * 0.56,
          0.18,
          sh.side * masterN[2] * 0.80 + masterT[2] * 0.56,
        ]);
        const forkAnchor = [center[0], groundY + sb.y * 0.72 * S, center[2]];
        const forkD = vdot(forkN, forkAnchor);
        registerFracturePlane(forkN[0], forkN[1], forkN[2], forkD, S * 0.016);

        const [innerSpire, outerHorn] = splitPolyByPlane(sh.p, forkN, forkD, -S * 0.025, 'yForkSplit');
        if (isSubstantialBlock(innerSpire, minBlockThick) && isSubstantialBlock(outerHorn, minBlockThick)) {
          const wedgedOuter = transformPoly(
            outerHorn,
            [sh.side * masterN[0] * S * 0.008, -S * 0.010, sh.side * masterN[2] * S * 0.008],
            masterT,
            sh.side * (-0.068),
            [center[0], groundY + S * 0.12, center[2]]
          );
          branchedPieces.push({ p: innerSpire, side: sh.side }, { p: wedgedOuter, side: sh.side });
          continue;
        }
      }
      branchedPieces.push(sh);
    }

    // --- TIER 3: Multi-Level Dipping Sheeting & Conjugate Shear-Stepped Block Splits ---
    for (const bp of branchedPieces) {
      const finalBlocks = [];
      const sheetN1 = vnorm([
        bp.side * masterN[0] * 0.34 + masterT[0] * 0.30,
        0.88,
        bp.side * masterN[2] * 0.34 + masterT[2] * 0.30,
      ]);
      const splitY1 = groundY + (sb.y * 0.80) * S;
      const sheetD1 = sheetN1[0] * center[0] + sheetN1[1] * splitY1 + sheetN1[2] * center[2];
      const [lowBlk, upperBlk] = splitPolyByPlane(bp.p, sheetN1, sheetD1, -S * 0.022, 'sheeting1');

      if (sb.ry >= 0.27 && isSubstantialBlock(lowBlk, minBlockThick) && isSubstantialBlock(upperBlk, minBlockThick)) {
        finalBlocks.push(lowBlk);
        const slip1 = S * (0.013 + 0.010 * faultSlip);
        const slippedUpper = transformPoly(
          upperBlk,
          [
            bp.side * masterN[0] * slip1 + masterT[0] * slip1 * 0.45,
            -slip1 * 0.30,
            bp.side * masterN[2] * slip1 + masterT[2] * slip1 * 0.45,
          ],
          masterT,
          -bp.side * 0.028,
          [center[0], splitY1, center[2]]
        );

        if (sb.crownFork) {
          const sheetN2 = vnorm([
            -bp.side * masterN[0] * 0.32 + masterT[0] * 0.48,
            0.82,
            -bp.side * masterN[2] * 0.32 + masterT[2] * 0.48,
          ]);
          const splitY2 = groundY + (sb.y * 1.26) * S;
          const sheetD2 = sheetN2[0] * center[0] + sheetN2[1] * splitY2 + sheetN2[2] * center[2];
          const [midBlk, crownBlk] = splitPolyByPlane(slippedUpper, sheetN2, sheetD2, -S * 0.018, 'sheeting2');
          if (isSubstantialBlock(midBlk, minBlockThick) && isSubstantialBlock(crownBlk, minBlockThick)) {
            const slip2 = S * (0.010 + 0.008 * faultSlip);
            const slippedCrown = transformPoly(
              crownBlk,
              [
                bp.side * masterN[0] * slip2 * 0.7 - masterT[0] * slip2 * 0.5,
                -slip2 * 0.20,
                bp.side * masterN[2] * slip2 * 0.7 - masterT[2] * slip2 * 0.5,
              ],
              masterN,
              bp.side * 0.025,
              [center[0], splitY2, center[2]]
            );
            finalBlocks.push(midBlk, slippedCrown);
          } else {
            finalBlocks.push(slippedUpper);
          }
        } else {
          finalBlocks.push(slippedUpper);
        }
      } else {
        finalBlocks.push(bp.p);
      }

      for (const piece of finalBlocks) {
        const gp = clipPolyByHalfSpace(piece, [0, -1, 0], -groundY, 'base') || piece;
        if (!isSubstantialBlock(gp, minBlockThick)) continue;
        const comp = compilePolyBlock(gp, bf, {
          edgeBevel: bevel,
          smoothUnionR: S * 0.0032,
          detailScale: 1.06,
          seedSalt: rng.int(1, 999999),
        });
        if (comp) blocks.push(comp);
      }
    }
  }

  // ============================================================================
  // 2. JAMMED ANGULAR CHOCKSTONES WEDGED IN THE CENTRAL SPLIT CHASM
  // ============================================================================
  const chasmCenterAtY = (yFrac) => {
    const dy = Math.max(0, yFrac - 0.02);
    const leftWallU = -baseSep * 0.95 - Math.sin(leftTiltRad) * dy * S;
    const rightWallU = baseSep * 1.15 + Math.sin(Math.abs(rightTiltRad)) * dy * S;
    return {
      uCenter: 0.5 * (leftWallU + rightWallU) / S,
      halfSpan: 0.5 * (rightWallU - leftWallU) / S,
    };
  };

  const chockSpecs = [
    { y: 0.55, along:  0.02, vertR: 0.048, alongR: 0.052 },
    { y: 0.28, along: -0.02, vertR: 0.038, alongR: 0.045 },
  ];
  for (const cs of chockSpecs) {
    const { uCenter, halfSpan } = chasmCenterAtY(cs.y);
    const cPos = toWorld(uCenter, cs.y, cs.along);
    const cf = buildStrikeDipFrame(chasmAz + rng.range(0.40, 1.15), rng.range(30, 58) * DEG2RAD);
    const wedgeHalfW = (halfSpan * 1.28 + 0.014) * S;
    const cPoly = sculptGeologicalParentPoly(
      cPos,
      [wedgeHalfW, cs.vertR * S, cs.alongR * S],
      cf,
      rng,
      { jointDispersion: 0.24, cornerBlunt: 0.68, extraBevelCuts: 5 }
    );
    const compC = compilePolyBlock(cPoly, cf, {
      edgeBevel: bevel * 0.82,
      detailScale: 0.98,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compC) blocks.push(compC);
  }

  // ============================================================================
  // 3. CALVED ANGULAR SPALL BLOCKS & TALUS SLABS AT CHASM MOUTHS & BASE
  // ============================================================================
  const spallClusters = [
    { across: -0.05, along:  0.26, r: 0.052 },
    { across:  0.08, along:  0.27, r: 0.046 },
    { across: -0.18, along:  0.24, r: 0.056 },
    { across:  0.20, along:  0.23, r: 0.052 },
    { across: -0.28, along:  0.11, r: 0.058 },
    { across:  0.29, along:  0.09, r: 0.054 },
    { across: -0.25, along: -0.19, r: 0.052 },
    { across:  0.26, along: -0.20, r: 0.050 },
    { across:  0.02, along: -0.25, r: 0.046 },
  ];
  for (const sc of spallClusters) {
    const pos = toWorld(sc.across + rng.signed() * 0.015, 0, sc.along + rng.signed() * 0.015);
    const rS = sc.r * S * rng.range(0.88, 1.15);
    const sf = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(24, 62) * DEG2RAD);
    let sPoly = sculptGeologicalParentPoly(
      [pos[0], groundY + rS * 0.50, pos[2]],
      [rS * rng.range(0.95, 1.36), rS * rng.range(0.55, 0.84), rS * rng.range(0.85, 1.24)],
      sf,
      rng,
      { jointDispersion: 0.24, cornerBlunt: 0.66, flatBaseY: groundY, extraBevelCuts: 5 }
    );
    sPoly = clipPolyByHalfSpace(sPoly, [0, -1, 0], -groundY, 'base') || sPoly;
    const compS = compilePolyBlock(sPoly, sf, {
      edgeBevel: bevel * 0.82,
      detailScale: 0.98,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compS) blocks.push(compS);
  }

  // ============================================================================
  // 4. BRANCHING SURFACE STRESS FISSURES + JIGSAW CHASM WALL UNDULATION
  // ============================================================================
  // Pre-build 6 crooked conjugate surface stress fissures across the broad spire flanks
  // with smooth V-notch aperture > 2.4 voxels so they carve authentic branching rock cracks
  // into exterior faces with ZERO floating splinters!
  const surfaceFissures = [];
  const fissureSpecs = [
    { across: -0.15, y: 0.36, along:  0.08, az: chasmAz + 0.52, dip:  0.24, len: 0.34, halfW: 0.020, depth: 0.019 },
    { across: -0.18, y: 0.48, along:  0.04, az: chasmAz - 0.62, dip: -0.28, len: 0.24, halfW: 0.016, depth: 0.015 },
    { across:  0.16, y: 0.34, along:  0.09, az: chasmAz - 0.48, dip: -0.22, len: 0.36, halfW: 0.021, depth: 0.020 },
    { across:  0.19, y: 0.46, along:  0.03, az: chasmAz + 0.68, dip:  0.30, len: 0.25, halfW: 0.016, depth: 0.015 },
    { across: -0.12, y: 0.28, along: -0.10, az: chasmAz + 0.44, dip:  0.20, len: 0.30, halfW: 0.018, depth: 0.017 },
    { across:  0.14, y: 0.30, along: -0.09, az: chasmAz - 0.42, dip: -0.25, len: 0.30, halfW: 0.018, depth: 0.017 },
  ];
  for (let i = 0; i < fissureSpecs.length; i++) {
    const fs = fissureSpecs[i];
    const anchor = toWorld(fs.across, fs.y, fs.along);
    const fn = vnorm([Math.cos(fs.az), fs.dip, -Math.sin(fs.az)]);
    surfaceFissures.push({
      ax: anchor[0], ay: anchor[1], az: anchor[2],
      nx: fn[0], ny: fn[1], nz: fn[2],
      invLen: 1 / (fs.len * S),
      halfW: fs.halfW * S * (0.75 + 0.45 * aperture),
      depth: fs.depth * S * (0.75 + 0.50 * aperture),
      salt: crackSalt + 811 + i * 97,
    });
  }

  const wallFreq1 = 1 / (S * 0.17);
  const wallFreq2 = 1 / (S * 0.068);
  const fisWarpFreq = 1 / (S * 0.095);

  function evalSurfaceFissureIndent(wx, wy, wz) {
    let bestIndent = 0;
    for (let i = 0; i < surfaceFissures.length; i++) {
      const f = surfaceFissures[i];
      const dx = wx - f.ax, dy = wy - f.ay, dz = wz - f.az;
      const rNorm = Math.hypot(dx, dy * 0.82, dz) * f.invLen;
      if (rNorm >= 1.0) continue;

      // Crooked rock-fracture trace warp + tip taper (zero displacement at crack tips!)
      const tipEnv = (1.0 - rNorm * rNorm);
      const kink = scalarField3D(wx * fisWarpFreq, wy * fisWarpFreq, wz * fisWarpFreq, f.salt) * S * 0.016;
      const planeDist = Math.abs(dx * f.nx + dy * f.ny + dz * f.nz + kink);
      const localHalfW = f.halfW * (0.45 + 0.55 * tipEnv);
      if (planeDist < localHalfW) {
        const prof = 1.0 - planeDist / localHalfW;
        // Sharp V-root crack profile (`pow(prof, 1.45)`) recessed cleanly into solid granite
        const indent = f.depth * tipEnv * Math.pow(prof, 1.45);
        if (indent > bestIndent) bestIndent = indent;
      }
    }
    return bestIndent;
  }

  function evalChasmWallJog(wx, wy, wz) {
    const v = clamp((wy - groundY) / S, 0.0, 1.0);
    const w = (wx * masterT[0] + wz * masterT[2]) / S;
    const stepJog = Math.sin(v * 8.6 - 0.4) * S * 0.011 + Math.sin(w * 5.2 + 0.3) * S * 0.008;
    const n1 = scalarField3D(wx * wallFreq1, wy * wallFreq1 * 0.9, wz * wallFreq1, crackSalt + 31) * S * 0.013;
    const n2 = scalarField3D(wx * wallFreq2, wy * wallFreq2, wz * wallFreq2, crackSalt + 97) * S * 0.005;
    return stepJog + n1 + n2;
  }

  function evalMacroCracks(wx, wy, wz, dBedrock) {
    if (Math.abs(dBedrock) > S * 0.055) return dBedrock;
    let dOut = dBedrock;

    // 1. Interlocking jigsaw chasm wall undulation
    const u = wx * masterN[0] + wz * masterN[2];
    const v = clamp((wy - groundY) / S, 0.0, 1.0);
    const { uCenter, halfSpan } = chasmCenterAtY(v);
    const distFromChasm = Math.abs(u - uCenter * S);
    const wallZone = (halfSpan + 0.085) * S;
    if (distFromChasm <= wallZone) {
      const env = 1.0 - smoothstep(halfSpan * S * 0.6, wallZone, distFromChasm);
      const sideSign = u < uCenter * S ? -1.0 : 1.0;
      dOut += sideSign * evalChasmWallJog(wx, wy, wz) * env;
    }

    // 2. Crooked branching surface stress fissures recessed into exterior spire flanks
    if (wy > groundY + S * 0.04) {
      dOut += evalSurfaceFissureIndent(wx, wy, wz);
    }
    return dOut;
  }

  function evalFineCracks(wx, wy, wz) {
    const u = wx * masterN[0] + wz * masterN[2];
    const v = clamp((wy - groundY) / S, 0.0, 1.0);
    const { uCenter, halfSpan } = chasmCenterAtY(v);
    const wallDist = Math.abs(Math.abs(u - uCenter * S) - halfSpan * S);
    if (wallDist > S * 0.055) return 0;

    // Plumose hackle striations & concentric arrest ribs on freshly cleaved chasm walls
    const wallProx = 1.0 - wallDist / (S * 0.055);
    const w = (wx * masterT[0] + wz * masterT[2]) / S;
    const plumeR = Math.hypot(w + 0.03, (v - 0.42) * 1.15);
    const plumeAng = Math.atan2(v - 0.42, w + 0.03);
    const hackle = scalarField3D(plumeAng * 9.5, plumeR * 6.0, u * 6.0 / S, crackSalt + 211);
    const arrestRib = Math.sin(plumeR * 25.0 + hackle * 1.4) * 0.5;
    return (hackle * 0.65 + arrestRib * 0.35) * S * 0.0046 * wallProx;
  }

  function sampleCrackMask(wx, wy, wz) {
    let freshSplit = 0;
    for (let i = 0; i < fracturePlanes.length; i++) {
      const fp = fracturePlanes[i];
      const dist = Math.max(0, Math.abs(wx * fp.nx + wy * fp.ny + wz * fp.nz - fp.d) - fp.halfGap);
      const p = 1.0 - smoothstep(0.0, S * 0.045, dist);
      if (p > freshSplit) freshSplit = p;
    }
    const surfFis = clamp(evalSurfaceFissureIndent(wx, wy, wz) / (S * 0.014), 0, 1);
    if (surfFis > freshSplit) freshSplit = surfFis;
    return freshSplit;
  }

  const crackNetwork = {
    S,
    groundY,
    crackSalt,
    throughSplitCount: fracturePlanes.length,
    evalMacroCracks,
    evalFineCracks,
    sampleCrackMask,
  };
  return { blocks, cuts, groundY, crackNetwork };
}

/**
 * Realistic PBR Sierra / Joshua Tree Split Granite:
 * Relies 100% on real 3D fracture geometry + baked Volumetric AO & Cavity (ZERO fake painted lines!),
 * contrasting weathered sun-baked granodiorite exterior against freshly cleaved iron-stained chasm walls.
 */
export function evalCrackedMonolithColor(crackNet, wx, wy, wz, nx, ny, nz, ao, cav, outRgb) {
  const S = crackNet.S;
  const mBroad = scalarField3D(wx / (S * 0.24), wy / (S * 0.24), wz / (S * 0.24), crackNet.crackSalt + 401);
  const mMed   = scalarField3D(wx / (S * 0.068), wy / (S * 0.068), wz / (S * 0.068), crackNet.crackSalt + 509);
  const mSpeck = scalarField3D(wx / (S * 0.018), wy / (S * 0.018), wz / (S * 0.018), crackNet.crackSalt + 613);

  const freshSplit = crackNet.sampleCrackMask
    ? crackNet.sampleCrackMask(wx, wy, wz)
    : 0;

  let extR = 0.45 + 0.042 * mBroad + 0.022 * mMed + 0.016 * mSpeck;
  let extG = 0.42 + 0.038 * mBroad + 0.020 * mMed + 0.015 * mSpeck;
  let extB = 0.385 + 0.032 * mBroad + 0.020 * mMed + 0.014 * mSpeck;

  const lichen = clamp(mBroad * 1.1 + 0.15, 0, 1) * smoothstep(0.54, 0.88, ny) * (1.0 - freshSplit) * 0.20;
  extR = extR * (1 - lichen) + 0.43 * lichen;
  extG = extG * (1 - lichen) + 0.45 * lichen;
  extB = extB * (1 - lichen) + 0.35 * lichen;

  const seepNoise = scalarField3D(wx / (S * 0.042), wy / (S * 0.26), wz / (S * 0.042), crackNet.crackSalt + 727);
  const ironSeep = clamp(seepNoise * 0.95 + 0.30, 0, 1) * (1.0 - Math.abs(ny) * 0.65);

  let intR = 0.51 + 0.024 * mSpeck;
  let intG = 0.48 + 0.022 * mSpeck;
  let intB = 0.45 + 0.020 * mSpeck;
  intR = intR * (1 - ironSeep * 0.44) + 0.49 * (ironSeep * 0.44);
  intG = intG * (1 - ironSeep * 0.44) + 0.29 * (ironSeep * 0.44);
  intB = intB * (1 - ironSeep * 0.44) + 0.17 * (ironSeep * 0.44);

  let sR = extR * (1 - freshSplit) + intR * freshSplit;
  let sG = extG * (1 - freshSplit) + intG * freshSplit;
  let sB = extB * (1 - freshSplit) + intB * freshSplit;

  // Pure geometric AO + cavity darkening inside real 3D cracks & joints!
  const crevice = clamp((1.0 - ao) * 0.96 + cav * 0.78 - 0.14, 0, 1);
  const darkMul = 1.0 - 0.68 * crevice;
  sR *= darkMul;
  sG *= darkMul * 0.96;
  sB *= darkMul * 0.93;

  outRgb[0] = Math.pow(clamp(sR, 0.020, 0.85), 2.2);
  outRgb[1] = Math.pow(clamp(sG, 0.020, 0.85), 2.2);
  outRgb[2] = Math.pow(clamp(sB, 0.020, 0.85), 2.2);
}
