/**
 * Detailed Stratigraphic, Desert Canyon, Badlands Hoodoo, Balanced Rock,
 * Dissolution Karst & Granite Exfoliation Dome Builders.
 */

import {
  DEG2RAD,
  buildStrikeDipFrame,
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
  sculptGeologicalParentPoly,
  compilePolyBlock,
} from './polyhedron.js';
import { compileCurvedArchRib } from './arch-curves.js';
import {
  getFormationSplineSpec,
  compileSplineArcTable,
  compileCanyonTripletTable,
} from './formation-splines.js';

/**
 * STRATIFIED SEDIMENTARY CLIFF (`sedimentary-cliff`)
 * Broad, continuous stratified canyon escarpment driven natively by its Cliff Spine Spline:
 * - Stretching or curving the Spine Spline controls cliff direction, curvature, and cliff length,
 *   dynamically adding staggered sandstone blocks along the arc so proportions never stretch!
 * - Massive projecting sandstone cliff-formers overhang crisp recessed siltstone/shale partings
 *   across one unbroken escarpment (zero central split trench, zero piano-key teeth).
 */
export function buildSedimentaryCliff(params, tectonics, rng, recursiveFractureFn, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const activeSplines = Array.isArray(params.formationSplines) && params.formationSplines.length >= 1
    ? params.formationSplines
    : getFormationSplineSpec('sedimentary-cliff', S).defaultCurves;
  const spine = compileSplineArcTable(activeSplines[0], 96);

  const spanLen = Math.max(S * 0.55, spine.totalHorizLength);
  const baseHeight = S * 0.72;
  const baseDepth = S * 0.36;

  const dipRad = clamp((params.dipAngle ?? 5), 0, 18) * 0.36 * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? -8) * DEG2RAD;
  const bedFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const dipSlopeAlong = Math.sin(dipRad) * 0.38;

  const overhang = params.overhangStrength ?? 0.82;
  const pluck = params.pluckingAmount ?? 0.58;
  const aperture = params.jointAperture ?? 0.42;
  const bevel = S * (0.0008 + 0.0014 * (params.edgeWear ?? 0.26));
  const planSalt = rng.int(1, 99999);

  // Planform front/back offsets relative to the Spine Spline (in local normal N(u) units)
  function frontOffsetAt(u, rMul) {
    const su = (u - 0.5) * 2.0; // -1 .. +1
    const buttressA = Math.exp(-((su + 0.34) * (su + 0.34)) / 0.16) * 0.12;
    const buttressB = Math.exp(-((su - 0.30) * (su - 0.30)) / 0.18) * 0.10;
    const embay = -Math.exp(-((su + 0.02) * (su + 0.02)) / 0.12) * 0.045;
    const endTaper = -0.14 * Math.pow(Math.abs(su), 3.2);
    const micro = scalarField3D(su * 4.2, 0.5, 0.8, planSalt) * 0.032;
    return (0.34 + buttressA + buttressB + embay + endTaper + micro) * baseDepth * rMul;
  }

  function backOffsetAt(u, rMul) {
    const su = (u - 0.5) * 2.0;
    const endNarrow = 0.14 * Math.pow(Math.abs(su), 2.6);
    const jag = scalarField3D(su * 2.8, 1.7, 0.4, planSalt + 37) * 0.035;
    return (-0.48 + endNarrow + jag) * baseDepth * rMul;
  }

  function rimHeightAt(u, splineY) {
    const su = (u - 0.5) * 2.0;
    const shoulderTrim = Math.abs(su) > 0.82
      ? 1.0 - 0.32 * Math.pow((Math.abs(su) - 0.82) / 0.18, 1.7)
      : 1.0;
    const benchWave = 1.0 + 0.035 * Math.cos((su + 0.25) * 3.6) +
      0.032 * scalarField3D(su * 4.0, 2.1, 0.9, planSalt + 71);
    return clamp(splineY * shoulderTrim * benchWave, baseHeight * 0.28, baseHeight * 1.65);
  }

  // 1. Narrow Grounded Bedrock Toe Bench following the Spine Spline
  const nToe = clamp(Math.round(spanLen / (S * 0.11)), 10, 28);
  for (let i = 0; i < nToe; i++) {
    const uMid = (i + 0.5) / nToe;
    const st = spine.sampleAtArc(uMid);
    const localF = buildStrikeDipFrame(Math.atan2(-st.nx, st.nz), dipRad);
    const fOff = frontOffsetAt(uMid, st.r);
    const segW = (spanLen / nToe) * 0.72;
    const platH = baseHeight * rng.range(0.038, 0.056);
    const cx = st.x + st.nx * (fOff + baseDepth * 0.04);
    const cz = st.z + st.nz * (fOff + baseDepth * 0.04);

    let poly = sculptGeologicalParentPoly(
      [cx, groundY + platH * 0.46, cz],
      [segW, platH * 0.54, baseDepth * 0.20 * st.r],
      localF,
      rng,
      { jointDispersion: 0.05, cornerBlunt: 0.62, flatBaseY: groundY, extraBevelCuts: 3 }
    );
    if (!poly) continue;
    const rampN = vnorm([st.nx * 0.12 + rng.signed() * 0.03, 0.99, st.nz * 0.12]);
    poly = clipPolyByHalfSpace(poly, rampN, vdot(rampN, [cx, groundY + platH * 0.90, cz]), 'toeTop') || poly;
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
    const compP = compilePolyBlock(poly, bedFrame, {
      edgeBevel: bevel * 1.05,
      smoothUnionR: S * 0.0032,
      detailScale: 0.86,
      hardness: 0.86,
      isSedimentary: true,
      seedSalt: (planSalt + i * 17) & 0x7fffffff,
    });
    if (compP) blocks.push(compP);
  }

  // 1B. Unbroken Solid Recessed Stratigraphic Core across 100% of the Spine
  //     Guarantees crisp vertical joints read as shadow-filled rock fractures — NEVER air gaps!
  const nCore = clamp(Math.round(spanLen / (S * 0.075)), 18, 42);
  for (let i = 0; i < nCore; i++) {
    const u0 = i / nCore;
    const u1 = (i + 1) / nCore;
    const uMid = 0.5 * (u0 + u1);
    const st0 = spine.sampleAtArc(u0);
    const st1 = spine.sampleAtArc(u1);
    const st = spine.sampleAtArc(uMid);
    const h0 = rimHeightAt(u0, st0.y) * 0.965;
    const h1 = rimHeightAt(u1, st1.y) * 0.965;
    const hLocal = rimHeightAt(uMid, st.y) * 0.965;
    const dCoreRim = clamp((h1 - h0) / Math.max(S * 0.04, spanLen / nCore), -0.9, 0.9);
    const fCore = frontOffsetAt(uMid, st.r) - S * 0.022;
    const bCore = Math.min(fCore - S * 0.085, backOffsetAt(uMid, st.r) + S * 0.015);
    const midOff = 0.5 * (fCore + bCore);
    const halfThick = Math.max(S * 0.048, 0.5 * (fCore - bCore) * 1.12);
    const segHalfW = (spanLen / nCore) * 0.88;
    const cx = st.x + st.nx * midOff;
    const cz = st.z + st.nz * midOff;
    const localF = buildStrikeDipFrame(Math.atan2(-st.nx, st.nz), dipRad);

    let cPoly = sculptGeologicalParentPoly(
      [cx, groundY + hLocal * 0.50, cz],
      [segHalfW, hLocal * 0.56, halfThick],
      localF,
      rng,
      { jointDispersion: 0.04, cornerBlunt: 0.68, flatBaseY: groundY, extraBevelCuts: 3 }
    );
    if (!cPoly) continue;
    const coreRimN = vnorm([-st.tx * dCoreRim + st.nx * 0.08, 1.0, -st.tz * dCoreRim + st.nz * 0.08]);
    cPoly = clipPolyByHalfSpace(cPoly, coreRimN, vdot(coreRimN, [cx, groundY + hLocal, cz]), 'coreRim') || cPoly;
    cPoly = clipPolyByHalfSpace(cPoly, [0, -1, 0], -groundY, 'base') || cPoly;
    const compCore = compilePolyBlock(cPoly, bedFrame, {
      edgeBevel: bevel * 1.1,
      smoothUnionR: S * 0.0060,
      detailScale: 0.88,
      hardness: 0.80,
      isSedimentary: true,
      seedSalt: (planSalt + 901) & 0x7fffffff,
    });
    if (compCore) blocks.push(compCore);
  }

  // Non-periodic master vertical cross-joint positions along strike (5 irregular fracture zones, NOT 25 bricks!)
  const baseMasterJoints = [0.16, 0.34, 0.53, 0.71, 0.86];

  // 2. Broad Interlocked Stratigraphic Cliff Slabs & Recessed Shale Partings along the Spine
  function addSplineStratum(refY0, bedH, projFrac, hardness, opts = {}) {
    const {
      uStart = 0.015,
      uEnd = 0.985,
      blockTargetM = S * 0.095,
      stepSalt = 1,
      upperRetreat = 0.0,
    } = opts;

    const uSpan = Math.max(0.1, uEnd - uStart);
    const nBlocks = clamp(Math.round((spanLen * uSpan) / blockTargetM), 16, 36);
    const du = uSpan / nBlocks;
    // Shift master joints subtly per stratigraphic packet so cross-joints step naturally across shale partings!
    const packetGroup = Math.floor((stepSalt - 1) / 3);
    const bedJoints = baseMasterJoints.map((ju, idx) =>
      ju + scalarField3D(idx * 2.1, packetGroup * 1.9, stepSalt * 0.35, planSalt + 311) * 0.032
    );
    const jointHairline = S * (0.0018 + 0.0032 * aperture);

    for (let i = 0; i < nBlocks; i++) {
      const u0 = uStart + i * du;
      const u1 = uStart + (i + 1) * du;
      const uMid = 0.5 * (u0 + u1);

      const st0 = spine.sampleAtArc(u0);
      const st1 = spine.sampleAtArc(u1);
      const stM = spine.sampleAtArc(uMid);

      // Determine which intact sandstone slab (`slabIdx = 0..5`) this curved segment belongs to,
      // and whether its left (`u0`) or right (`u1`) edge lies on a master cross-joint!
      let slabIdx = 0;
      let jointAtLeft = false;
      let jointAtRight = false;
      for (let j = 0; j < bedJoints.length; j++) {
        if (uMid > bedJoints[j]) slabIdx = j + 1;
        if (Math.abs(u0 - bedJoints[j]) < du * 0.55) jointAtLeft = true;
        if (Math.abs(u1 - bedJoints[j]) < du * 0.55) jointAtRight = true;
      }

      const alongCoord = (uMid - 0.5) * spanLen;
      const wy0 = refY0 - alongCoord * dipSlopeAlong;
      const wy1 = wy0 + bedH;
      const rim0 = rimHeightAt(u0, st0.y);
      const rim1 = rimHeightAt(u1, st1.y);
      const localRim = rimHeightAt(uMid, stM.y);
      const rimMax = Math.max(rim0, rim1, localRim);
      if (wy0 > rimMax - bedH * 0.16) continue;

      // Coherent per-slab projection step (whole geological slab steps together, NOT tiny brick teeth!)
      const slabHash = scalarField3D(slabIdx * 2.3, stepSalt * 3.1, 0.7, planSalt + 503);
      const isPluckedSlab = slabHash > (0.66 - 0.18 * pluck) && slabIdx > 0 && slabIdx < bedJoints.length && hardness > 0.65;
      const stepNormal = (
        projFrac * (0.68 + 0.48 * overhang)
        - upperRetreat
        - (isPluckedSlab ? 0.038 : 0)
        + scalarField3D(uMid * 3.6, stepSalt * 1.4, 0.5, planSalt + stepSalt * 17) * 0.016
      ) * baseDepth * stM.r;

      const fOff0 = frontOffsetAt(u0, st0.r) + stepNormal;
      const fOff1 = frontOffsetAt(u1, st1.r) + stepNormal;
      const fOffM = 0.5 * (fOff0 + fOff1);
      const bOffM = Math.min(fOffM - S * 0.08, backOffsetAt(uMid, stM.r));

      const segArcW = Math.max(S * 0.032, (u1 - u0) * spanLen);
      // Overlap cleanly inside an intact slab; trim cleanly at master cross-joints!
      const leftExtent = jointAtLeft ? Math.max(S * 0.014, segArcW * 0.50 - jointHairline) : segArcW * 0.74;
      const rightExtent = jointAtRight ? Math.max(S * 0.014, segArcW * 0.50 - jointHairline) : segArcW * 0.74;

      const cappedByRim = wy1 >= rimMax - bedH * 0.18;
      const botY = Math.max(groundY + baseHeight * 0.042, wy0 - (hardness < 0.45 ? bedH * 0.18 : bedH * 0.04));
      const topY = (cappedByRim ? localRim : wy1) + (hardness < 0.45 ? bedH * 0.18 : bedH * 0.02);
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
        baseHeight * 1.85,
        centerPt[2] + reach,
        'strataBlock'
      );

      // Continuous C1 tangent-sloped rim plane wherever a bed intersects the skyline (ZERO Lego steps!)
      const dRimDs = clamp((rim1 - rim0) / Math.max(S * 0.03, segArcW), -0.95, 0.95);
      if (wy1 > Math.min(rim0, rim1) - bedH * 0.12) {
        const rimPlaneN = vnorm([-stM.tx * dRimDs + stM.nx * 0.10, 1.0, -stM.tz * dRimDs + stM.nz * 0.10]);
        poly = clipPolyByHalfSpace(poly, rimPlaneN, vdot(rimPlaneN, [centerPt[0], groundY + localRim, centerPt[2]]), 'rimSlope') || poly;
      }
      if (!cappedByRim) {
        poly = clipPolyByHalfSpace(poly, bedFrame.normal, vdot(bedFrame.normal, [centerPt[0], wy1 + (hardness < 0.45 ? bedH * 0.18 : bedH * 0.02), centerPt[2]]), 'bedTop') || poly;
      }
      const botPlaneN = [-bedFrame.normal[0], -bedFrame.normal[1], -bedFrame.normal[2]];
      poly = clipPolyByHalfSpace(poly, botPlaneN, vdot(botPlaneN, [centerPt[0], botY, centerPt[2]]), 'bedBot') || poly;
      if (!poly) continue;

      // Smooth deterministic cliff face within each intact sandstone slab
      const dfds = clamp((fOff1 - fOff0) / Math.max(S * 0.03, segArcW), -0.85, 0.85);
      const slabTilt = scalarField3D(slabIdx * 1.9, stepSalt * 2.3, 0.2, planSalt + 617) * 0.04;
      const batter = (hardness > 0.72 ? -0.03 : 0.16) + slabTilt;
      const frontN = vnorm([
        stM.nx - stM.tx * dfds,
        batter,
        stM.nz - stM.tz * dfds,
      ]);
      poly = clipPolyByHalfSpace(poly, frontN, vdot(frontN, frontPt), 'cliffFace') || poly;

      // Cross-joint / segment boundary planes
      const yawL = jointAtLeft ? scalarField3D(slabIdx * 3.1, stepSalt, 0.4, planSalt + 733) * 0.14 : 0;
      const yawR = jointAtRight ? scalarField3D((slabIdx + 1) * 3.1, stepSalt, 0.4, planSalt + 733) * 0.14 : 0;
      const leftJ = vnorm([
        -stM.tx * Math.cos(yawL) + stM.nx * Math.sin(yawL),
        jointAtLeft ? 0.03 : 0,
        -stM.tz * Math.cos(yawL) + stM.nz * Math.sin(yawL),
      ]);
      const rightJ = vnorm([
        stM.tx * Math.cos(yawR) + stM.nx * Math.sin(yawR),
        jointAtRight ? 0.03 : 0,
        stM.tz * Math.cos(yawR) + stM.nz * Math.sin(yawR),
      ]);
      const leftAnchor = [frontPt[0] - stM.tx * leftExtent, midY, frontPt[2] - stM.tz * leftExtent];
      const rightAnchor = [frontPt[0] + stM.tx * rightExtent, midY, frontPt[2] + stM.tz * rightExtent];
      poly = clipPolyByHalfSpace(poly, leftJ, vdot(leftJ, leftAnchor), 'jointL') || poly;
      poly = clipPolyByHalfSpace(poly, rightJ, vdot(rightJ, rightAnchor), 'jointR') || poly;

      const backN = vnorm([-stM.nx, 0.18, -stM.nz]);
      poly = clipPolyByHalfSpace(poly, backN, vdot(backN, backPt), 'backFace') || poly;
      poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
      if (!poly) continue;

      const hasMasterJoint = jointAtLeft || jointAtRight;
      const comp = compilePolyBlock(poly, bedFrame, {
        edgeBevel: hardness > 0.65 ? bevel : bevel * 1.35,
        smoothUnionR: hasMasterJoint ? S * 0.0014 : S * 0.0052,
        detailScale: hardness > 0.65 ? 0.84 : 1.02,
        hardness,
        isSedimentary: true,
        // Shared seedSalt across all segments of the same intact sandstone slab -> zero fake brick seams!
        seedSalt: (planSalt + stepSalt * 131 + slabIdx * 37) & 0x7fffffff,
      });
      if (comp) blocks.push(comp);
    }
  }

  // 16-Horizon Stratified Canyon Column — continuous across the full spine (no half-wall chop!)
  const strataSpec = [
    { h: 0.044, proj: -0.14, hard: 0.24, wMul: 1.05, ret: 0.00 }, // 1. Deep basal undercut shale notch
    { h: 0.092, proj:  0.09, hard: 0.94, wMul: 0.84, ret: 0.00 }, // 2. Massive rust-ochre cliff bench A
    { h: 0.086, proj:  0.11, hard: 0.92, wMul: 0.88, ret: 0.00 }, // 3. Projecting rust-ochre cliff bench B
    { h: 0.028, proj: -0.07, hard: 0.28, wMul: 1.00, ret: 0.01 }, // 4. Sharp recessed siltstone parting
    { h: 0.078, proj:  0.08, hard: 0.90, wMul: 0.82, ret: 0.02 }, // 5. Pale buff cross-jointed sandstone A
    { h: 0.070, proj:  0.07, hard: 0.88, wMul: 0.86, ret: 0.03 }, // 6. Pale buff cross-jointed sandstone B
    { h: 0.030, proj: -0.08, hard: 0.28, wMul: 1.02, ret: 0.04 }, // 7. Deep maroon shale recess
    { h: 0.052, proj:  0.06, hard: 0.82, wMul: 0.76, ret: 0.05 }, // 8. Laminated maroon siltstone ledge 1
    { h: 0.046, proj:  0.02, hard: 0.74, wMul: 0.80, ret: 0.06 }, // 9. Laminated maroon siltstone ledge 2
    { h: 0.056, proj:  0.08, hard: 0.88, wMul: 0.78, ret: 0.07 }, // 10. Overhanging maroon-ochre rib bed
    { h: 0.034, proj: -0.05, hard: 0.36, wMul: 0.96, ret: 0.08 }, // 11. Recessed slope-forming parting
    { h: 0.064, proj:  0.07, hard: 0.90, wMul: 0.84, ret: 0.09 }, // 12. Upper cliff-former sandstone A
    { h: 0.068, proj:  0.06, hard: 0.92, wMul: 0.86, ret: 0.11 }, // 13. Upper cliff-former sandstone B
    { h: 0.030, proj: -0.04, hard: 0.40, wMul: 0.98, ret: 0.13 }, // 14. Sub-caprock parting notch
    { h: 0.078, proj:  0.05, hard: 0.94, wMul: 0.88, ret: 0.15 }, // 15. Monumental caprock brow across full spine
    { h: 0.074, proj:  0.02, hard: 0.92, wMul: 0.96, ret: 0.19 }, // 16. Continuous upper mesa caprock rim
  ];

  let refY = groundY + baseHeight * 0.048;
  for (let s = 0; s < strataSpec.length; s++) {
    const sp = strataSpec[s];
    const bedH = sp.h * baseHeight;
    addSplineStratum(refY, bedH, sp.proj, sp.hard, {
      uStart: sp.u0 ?? 0.015,
      uEnd: sp.u1 ?? 0.985,
      blockTargetM: S * 0.066 * sp.wMul,
      stepSalt: s + 1,
      upperRetreat: sp.ret,
    });
    refY += bedH * 0.955;
  }

  // 3. Restrained Angular Caprock Rim Slabs & Fallen Toe Blocks along Spine
  const numToeBlocks = Math.round(5 + (params.talusAmount ?? 0.52) * 6);
  for (let t = 0; t < numToeBlocks; t++) {
    const u = rng.range(0.08, 0.92);
    const st = spine.sampleAtArc(u);
    const rT = S * rng.range(0.025, 0.052);
    const fOff = frontOffsetAt(u, st.r) + baseDepth * rng.range(0.05, 0.22);
    const tx = st.x + st.nx * fOff;
    const tz = st.z + st.nz * fOff;
    const tf = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(12, 36) * DEG2RAD);
    let tPoly = sculptGeologicalParentPoly(
      [tx, groundY + baseHeight * 0.045 + rT * 0.44, tz],
      [rT * 1.30, rT * 0.62, rT * 1.05],
      tf,
      rng,
      { jointDispersion: 0.16, cornerBlunt: 0.72, flatBaseY: groundY, extraBevelCuts: 5 }
    );
    if (!tPoly) continue;
    tPoly = clipPolyByHalfSpace(tPoly, [0, -1, 0], -groundY, 'base') || tPoly;
    const compT = compilePolyBlock(tPoly, tf, {
      edgeBevel: bevel,
      detailScale: 0.92,
      hardness: 0.90,
      isSedimentary: true,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compT) blocks.push(compT);
  }

  return { blocks, cuts, groundY };
}

/**
 * SCULPTED NARROW SLOT CANYON (`slot-canyon`)
 * Antelope Canyon / Buckskin Gulch water-sculpted Navajo sandstone gorge driven by its 3-Spline Rig:
 * - Spline 0 (`Left Rim & Wall`), Spline 1 (`Canyon Floor Thalweg`), Spline 2 (`Right Rim & Wall`).
 * - Proportional vertical tier parameterization (`tf = 0..1` of `crest(u)`) + boundary-matched C1
 *   rim & inter-tier planes completely eliminates skyline stair-steps!
 * - Corkscrew flash-flood inner vortex ribs + cross-bedded Navajo slickrock exterior flanks.
 */
export function buildSlotCanyon(params, tectonics, rng) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const dipRad = clamp((params.dipAngle ?? 6), 2, 16) * 0.50 * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? -8) * DEG2RAD;
  const bedFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const overhang = params.overhangStrength ?? 0.80;
  const bevel = S * (0.0018 + 0.0026 * (params.edgeWear ?? 0.56));
  const smoothR = 0; // Hard union prevents smooth-min cell waffle & satisfies flood test contract!

  const activeSplines = Array.isArray(params.formationSplines) && params.formationSplines.length >= 3
    ? params.formationSplines
    : getFormationSplineSpec('slot-canyon', S).defaultCurves;
  const canyon = compileCanyonTripletTable(activeSplines, S, 96);

  const lengthZ = Math.max(S * 0.85, canyon.totalLength);
  const height = S * 0.82;
  const phase = rng.float() * Math.PI * 2;
  const waveSalt = rng.int(1, 99999);

  function floorYAtU(u) {
    const st = canyon.stationAt(u);
    let y = groundY + st.floorY;
    y += S * 0.0040 * scalarField3D(u * 3.1, 0.4, phase * 0.31, waveSalt + 127);
    y -= S * 0.0055 * (0.5 + 0.5 * Math.tanh((u - 0.34) * 18));
    y -= S * 0.0070 * (0.5 + 0.5 * Math.tanh((u - 0.66) * 18));
    return Math.max(groundY + S * 0.012, y);
  }

  function crestHeightAtU(u, side) {
    const st = canyon.stationAt(u);
    const baseCrest = side < 0 ? st.crestL : st.crestR;
    const zf = u - 0.5;
    const portalEnv = clamp(1.04 - 0.24 * Math.pow(Math.abs(zf) * 2.0, 2.4), 0.72, 1.04);
    const slickrockDomes = 0.96 + 0.065 * Math.cos(zf * Math.PI * 2.8 + side * 0.85) +
      0.035 * scalarField3D(zf * 2.6, side * 1.9, 0.5, waveSalt + 83);
    return clamp(baseCrest * portalEnv * slickrockDomes, S * 0.48, S * 1.15);
  }

  // Outward lateral distance from `st.center` along `(side * acrossX, side * acrossZ)` for the inner wall
  function innerOffsetAt(u, tf, side) {
    const st = canyon.stationAt(u);
    const zf = u - 0.5;
    const splineGap = side < 0 ? st.halfGapL : st.halfGapR;
    const mouthFlare = 1.0 + 0.32 * Math.pow(Math.abs(zf) * 2.0, 2.2);
    const pinchSwell = clamp(
      1.0 + 0.14 * Math.sin(zf * Math.PI * 3.1 + phase * 0.70) +
        0.055 * scalarField3D(zf * 2.7, 0.8, 1.3, waveSalt + 61),
      0.84,
      1.22
    );
    // Keep splineGap scaled cleanly while guaranteeing walkable corridor clearance (> 0.65m halfGap)
    const baseHalfGap = clamp(splineGap * 0.52, S * 0.058, S * 0.145) *
      (1.0 + 0.22 * Math.pow(tf - 0.44, 2) * 2.0) * mouthFlare * pinchSwell;

    // Antelope Canyon helical corkscrew flood ribs & undercut scour waves!
    const vortexWave1 = Math.sin(tf * Math.PI * 2.55 + zf * Math.PI * 2.1 + side * 0.45) * S * (0.019 + 0.013 * overhang);
    const vortexWave2 = Math.cos(tf * Math.PI * 4.2 - zf * Math.PI * 2.6 + side * 1.1) * S * 0.0075;
    const fineUndul = scalarField3D(zf * 3.1, tf * 3.0, side * 1.7, waveSalt) * S * 0.0065;
    return Math.max(S * 0.044, baseHalfGap + vortexWave1 + vortexWave2 + side * fineUndul);
  }

  // Outward lateral distance from `st.center` for the weathered Navajo slickrock exterior flank
  function outerOffsetAt(u, tf, side) {
    const st = canyon.stationAt(u);
    const zf = u - 0.5;
    const splineOuter = (side < 0 ? st.halfGapL : st.halfGapR) + S * 0.195 * (side < 0 ? st.thickL : st.thickR);
    const portalTaper = clamp(1.04 - 0.30 * Math.pow(Math.abs(zf) * 2.0, 2.3), 0.54, 1.04);
    // Natural Navajo slickrock cross-bedded dome curvature + subtle eolian ledges (not blank flat concrete!)
    const domeCurve = 1.0 - 0.24 * Math.pow(tf, 1.55);
    const eolianRib = 1.0 + 0.042 * Math.sin(tf * Math.PI * 5.0 + zf * 3.2 + side * 0.7) +
      0.065 * scalarField3D(zf * 2.5, tf * 1.4, side * 2.3, waveSalt + 41);
    const innerMin = innerOffsetAt(u, tf, side) + S * 0.085;
    return Math.max(innerMin, splineOuter * portalTaper * domeCurve * eolianRib);
  }

  const nTiers = 12;
  const nSegs = clamp(Math.round(lengthZ / (S * 0.066)), 18, 34);
  const du = 1 / nSegs;

  // 1. Bilateral Opposing Corkscrew-Fluted Slot Canyon Fins (proportional tiers -> ZERO rim stair-steps!)
  for (const side of [-1, 1]) {
    for (let b = 0; b < nTiers; b++) {
      const tf0 = b / nTiers;
      const tf1 = (b + 1) / nTiers;
      const tfMid = 0.5 * (tf0 + tf1);

      for (let iz = 0; iz < nSegs; iz++) {
        const u0 = iz * du;
        const u1 = (iz + 1) * du;
        const uMid = 0.5 * (u0 + u1);

        const st0 = canyon.stationAt(u0);
        const st1 = canyon.stationAt(u1);
        const stM = canyon.stationAt(uMid);

        const outX = side * stM.acrossX;
        const outZ = side * stM.acrossZ;

        const c0 = crestHeightAtU(u0, side);
        const c1 = crestHeightAtU(u1, side);
        const cM = crestHeightAtU(uMid, side);

        const yBot0 = groundY + tf0 * c0;
        const yBot1 = groundY + tf0 * c1;
        const yBotM = groundY + tf0 * cM;
        const yTop0 = groundY + tf1 * c0;
        const yTop1 = groundY + tf1 * c1;
        const yTopM = groundY + tf1 * cM;

        const segLen = Math.max(S * 0.035, lengthZ * du);
        const spanY = Math.max(S * 0.028, yTopM - yBotM);
        const overlapY = spanY * 0.28;

        const inBot = innerOffsetAt(uMid, tf0, side);
        const inTop = innerOffsetAt(uMid, tf1, side);
        const inMid = 0.5 * (inBot + inTop);
        const inU0 = innerOffsetAt(u0, tfMid, side);
        const inU1 = innerOffsetAt(u1, tfMid, side);

        const outBot = outerOffsetAt(uMid, tf0, side);
        const outTop = outerOffsetAt(uMid, tf1, side);
        const outMid = 0.5 * (outBot + outTop);
        const outU0 = outerOffsetAt(u0, tfMid, side);
        const outU1 = outerOffsetAt(u1, tfMid, side);

        const midOff = 0.5 * (inMid + outMid);
        const midY = 0.5 * (yBotM + yTopM);
        const cx = stM.center.x + outX * midOff;
        const cz = stM.center.z + outZ * midOff;
        const reach = Math.max(segLen, outMid - inMid) + S * 0.14;

        let poly = createBoxPoly(
          cx - reach,
          b === 0 ? groundY : Math.min(yBot0, yBot1, yBotM) - overlapY,
          cz - reach,
          cx + reach,
          Math.max(yTop0, yTop1, yTopM) + (b === nTiers - 1 ? S * 0.05 : overlapY),
          cz + reach,
          'slotWaveWall'
        );

        // Exact boundary-matched station planes at `u0` and `u1` (tiny 3% seal prevents concave-bend tangent extrapolation saw-teeth!)
        const backSegN = [-st0.tx, 0, -st0.tz];
        const fwdSegN = [st1.tx, 0, st1.tz];
        const pBoundary0 = [
          st0.center.x + side * st0.acrossX * midOff - st0.tx * segLen * 0.03,
          midY,
          st0.center.z + side * st0.acrossZ * midOff - st0.tz * segLen * 0.03,
        ];
        const pBoundary1 = [
          st1.center.x + side * st1.acrossX * midOff + st1.tx * segLen * 0.03,
          midY,
          st1.center.z + side * st1.acrossZ * midOff + st1.tz * segLen * 0.03,
        ];
        poly = clipPolyByHalfSpace(poly, backSegN, vdot(backSegN, pBoundary0), 'segBk') || poly;
        poly = clipPolyByHalfSpace(poly, fwdSegN, vdot(fwdSegN, pBoundary1), 'segFw') || poly;

        // Top plane: on topmost tier (`b === nTiers - 1`), smooth C1 Navajo slickrock dome rim + rounded inner rim brow!
        const dTopDs = clamp((yTop1 - yTop0) / segLen, -0.85, 0.85);
        if (b === nTiers - 1) {
          const rimN = vnorm([outX * 0.34 - stM.tx * dTopDs, 0.92, outZ * 0.34 - stM.tz * dTopDs]);
          poly = clipPolyByHalfSpace(poly, rimN, vdot(rimN, [cx, yTopM, cz]), 'slickrockRim') || poly;
          const innerBrowN = vnorm([-outX * 0.48 - stM.tx * dTopDs * 0.5, 0.87, -outZ * 0.48 - stM.tz * dTopDs * 0.5]);
          const innerRimPt = [stM.center.x + outX * (inTop + S * 0.025), yTopM - S * 0.012, stM.center.z + outZ * (inTop + S * 0.025)];
          poly = clipPolyByHalfSpace(poly, innerBrowN, vdot(innerBrowN, innerRimPt), 'innerRimBrow') || poly;
        } else {
          const tierTopN = vnorm([-stM.tx * dTopDs, 1.0, -stM.tz * dTopDs]);
          poly = clipPolyByHalfSpace(poly, tierTopN, vdot(tierTopN, [cx, yTopM + overlapY * 0.5, cz]), 'tierTop') || poly;
        }

        if (b > 0) {
          const dBotDs = clamp((yBot1 - yBot0) / segLen, -0.85, 0.85);
          const tierBotN = vnorm([stM.tx * dBotDs, -1.0, stM.tz * dBotDs]);
          poly = clipPolyByHalfSpace(poly, tierBotN, vdot(tierBotN, [cx, yBotM - overlapY * 0.5, cz]), 'tierBot') || poly;
        }

        // Exact 3D world-space cross-product surface normal for inner corkscrew gorge wall (includes S-bend curvature -> zero rim saw-teeth!)
        const pInU0 = [st0.center.x + side * st0.acrossX * inU0, midY, st0.center.z + side * st0.acrossZ * inU0];
        const pInU1 = [st1.center.x + side * st1.acrossX * inU1, midY, st1.center.z + side * st1.acrossZ * inU1];
        const pInBot = [stM.center.x + outX * inBot, yBotM, stM.center.z + outZ * inBot];
        const pInTop = [stM.center.x + outX * inTop, yTopM, stM.center.z + outZ * inTop];
        const tanInS = [pInU1[0] - pInU0[0], 0, pInU1[2] - pInU0[2]];
        const tanInY = [pInTop[0] - pInBot[0], Math.max(S * 0.02, pInTop[1] - pInBot[1]), pInTop[2] - pInBot[2]];
        let gorgeN = vnorm(vcross(tanInS, tanInY));
        if (gorgeN[0] * (-outX) + gorgeN[2] * (-outZ) < 0) {
          gorgeN = [-gorgeN[0], -gorgeN[1], -gorgeN[2]];
        }
        const inPt = [stM.center.x + outX * inMid, midY, stM.center.z + outZ * inMid];
        poly = clipPolyByHalfSpace(poly, gorgeN, vdot(gorgeN, inPt), 'gorgeWave') || poly;

        // Exact 3D world-space cross-product surface normal for exterior Navajo slickrock flank
        const pOutU0 = [st0.center.x + side * st0.acrossX * outU0, midY, st0.center.z + side * st0.acrossZ * outU0];
        const pOutU1 = [st1.center.x + side * st1.acrossX * outU1, midY, st1.center.z + side * st1.acrossZ * outU1];
        const pOutBot = [stM.center.x + outX * outBot, yBotM, stM.center.z + outZ * outBot];
        const pOutTop = [stM.center.x + outX * outTop, yTopM, stM.center.z + outZ * outTop];
        const tanOutS = [pOutU1[0] - pOutU0[0], 0, pOutU1[2] - pOutU0[2]];
        const tanOutY = [pOutTop[0] - pOutBot[0], Math.max(S * 0.02, pOutTop[1] - pOutBot[1]), pOutTop[2] - pOutBot[2]];
        let outerN = vnorm(vcross(tanOutS, tanOutY));
        if (outerN[0] * outX + outerN[2] * outZ < 0) {
          outerN = [-outerN[0], -outerN[1], -outerN[2]];
        }
        const outPt = [stM.center.x + outX * outMid, midY, stM.center.z + outZ * outMid];
        poly = clipPolyByHalfSpace(poly, outerN, vdot(outerN, outPt), 'outerSlickrock') || poly;

        // Clean tapered sandstone prows at entrance (`iz === 0`) and exit (`iz === nSegs - 1`) portals
        if (iz === 0 || iz === nSegs - 1) {
          const sSign = iz === 0 ? -1 : 1;
          const prowOff = 0.5 * (innerOffsetAt(uMid, 0.45, side) + outerOffsetAt(uMid, 0.45, side));
          const prowAnchor = [
            stM.center.x + outX * prowOff + stM.tx * sSign * segLen * 0.38,
            groundY + cM * 0.45,
            stM.center.z + outZ * prowOff + stM.tz * sSign * segLen * 0.38,
          ];
          const prowN = vnorm([outX * 0.34 + stM.tx * sSign * 0.92, 0.10, outZ * 0.34 + stM.tz * sSign * 0.92]);
          poly = clipPolyByHalfSpace(poly, prowN, vdot(prowN, prowAnchor), 'portalProw') || poly;
        }

        poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
        if (!poly) continue;

        const comp = compilePolyBlock(poly, bedFrame, {
          edgeBevel: bevel,
          smoothUnionR: smoothR,
          detailScale: 0.92,
          isEolian: true,
          // Shared seedSalt along each horizontal bedding tier -> seamless slickrock ribs without vertical seam lines!
          seedSalt: (waveSalt + b * 73 + (side > 0 ? 401 : 0)) & 0x7fffffff,
        });
        if (comp) blocks.push(comp);
      }
    }
  }

  // 2. Segmented Sculptable Bedrock Thalweg Floor along the Centre Spline
  for (let iz = 0; iz < nSegs; iz++) {
    const u0 = iz * du;
    const u1 = (iz + 1) * du;
    const uMid = 0.5 * (u0 + u1);
    const stM = canyon.stationAt(uMid);
    const segLen = Math.max(S * 0.035, lengthZ * du);

    const floor0 = floorYAtU(u0);
    const floor1 = floorYAtU(u1);
    const floorMid = floorYAtU(uMid);
    const halfSpan = Math.max(innerOffsetAt(uMid, 0, -1), innerOffsetAt(uMid, 0, 1)) + S * 0.095;
    const reach = Math.max(halfSpan, segLen) + S * 0.08;

    let floorPoly = createBoxPoly(
      stM.center.x - reach,
      groundY,
      stM.center.z - reach,
      stM.center.x + reach,
      Math.max(floor0, floor1, floorMid) + S * 0.03,
      stM.center.z + reach,
      'slotBedrockFloor'
    );
    const halfAlong = segLen * 0.66;
    floorPoly = clipPolyByHalfSpace(floorPoly, [-stM.tx, 0, -stM.tz], vdot([-stM.tx, 0, -stM.tz], [stM.center.x - stM.tx * halfAlong, floorMid * 0.5, stM.center.z - stM.tz * halfAlong]), 'flBk') || floorPoly;
    floorPoly = clipPolyByHalfSpace(floorPoly, [stM.tx, 0, stM.tz], vdot([stM.tx, 0, stM.tz], [stM.center.x + stM.tx * halfAlong, floorMid * 0.5, stM.center.z + stM.tz * halfAlong]), 'flFw') || floorPoly;
    floorPoly = clipPolyByHalfSpace(floorPoly, [-stM.acrossX, 0, -stM.acrossZ], vdot([-stM.acrossX, 0, -stM.acrossZ], [stM.center.x - stM.acrossX * halfSpan, floorMid * 0.5, stM.center.z - stM.acrossZ * halfSpan]), 'flL') || floorPoly;
    floorPoly = clipPolyByHalfSpace(floorPoly, [stM.acrossX, 0, stM.acrossZ], vdot([stM.acrossX, 0, stM.acrossZ], [stM.center.x + stM.acrossX * halfSpan, floorMid * 0.5, stM.center.z + stM.acrossZ * halfSpan]), 'flR') || floorPoly;

    const floorSlope = clamp((floor1 - floor0) / segLen, -0.5, 0.5);
    const crossSlope = scalarField3D((uMid - 0.5) * 2.2, 1.7, 0.3, waveSalt + 149) * 0.04;
    const floorN = vnorm([
      -stM.acrossX * crossSlope - stM.tx * floorSlope,
      1.0,
      -stM.acrossZ * crossSlope - stM.tz * floorSlope,
    ]);
    floorPoly = clipPolyByHalfSpace(floorPoly, floorN, vdot(floorN, [stM.center.x, floorMid, stM.center.z]), 'slotFloorTop') || floorPoly;
    floorPoly = clipPolyByHalfSpace(floorPoly, [0, -1, 0], -groundY, 'base') || floorPoly;
    if (!floorPoly) continue;

    const compFloor = compilePolyBlock(floorPoly, bedFrame, {
      edgeBevel: bevel,
      smoothUnionR: 0,
      detailScale: 0.72,
      hardness: 0.58,
      isEolian: true,
      seedSalt: (waveSalt + 7001) & 0x7fffffff,
    });
    if (compFloor) blocks.push(compFloor);
  }

  // 3. Two Modest Water-Worn Floor Stones in the Middle Reach
  for (const uStone of [0.34, 0.68]) {
    const stB = canyon.stationAt(uStone);
    const lat = rng.signed() * S * 0.015;
    const bx = stB.center.x + stB.acrossX * lat;
    const bz = stB.center.z + stB.acrossZ * lat;
    const rChock = S * rng.range(0.024, 0.038);
    const bFrame = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(18, 46) * DEG2RAD);
    let bPoly = sculptGeologicalParentPoly(
      [bx, floorYAtU(uStone) + rChock * 0.66, bz],
      [rChock * 1.15, rChock * 0.78, rChock * 1.22],
      bFrame,
      rng,
      { jointDispersion: 0.16, cornerBlunt: 0.76, flatBaseY: groundY, extraBevelCuts: 5 }
    );
    if (!bPoly) continue;
    bPoly = clipPolyByHalfSpace(bPoly, [0, -1, 0], -groundY, 'base') || bPoly;
    const compB = compilePolyBlock(bPoly, bFrame, {
      edgeBevel: bevel * 0.85,
      smoothUnionR: 0,
      detailScale: 0.88,
      isEolian: true,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compB) blocks.push(compB);
  }

  // 4. World-Space Flow-Path Metadata for Dedicated Slot Flash-Flood & Channel-Wind SDF Process
  const slotSamples = [];
  const nPathSamples = 49;
  for (let i = 0; i < nPathSamples; i++) {
    const u = i / (nPathSamples - 1);
    const st = canyon.stationAt(u);
    const gapL = innerOffsetAt(u, 0.25, -1);
    const gapR = innerOffsetAt(u, 0.25, 1);
    const midShift = 0.5 * (gapR - gapL);
    slotSamples.push({
      z: st.center.z + st.acrossZ * midShift,
      x: st.center.x + st.acrossX * midShift,
      floorY: floorYAtU(u),
      halfGap: clamp(0.5 * (gapL + gapR), 0.58, 2.45),
      crestY: Math.min(crestHeightAtU(u, -1), crestHeightAtU(u, 1)),
    });
  }
  const slotCanyon = {
    version: 1,
    axis: 'z',
    flowDirection: 1,
    groundY,
    height,
    length: lengthZ,
    phase,
    samples: slotSamples,
  };

  return { blocks, cuts, groundY, slotCanyon };
}

/**
 * BRYCE BADLANDS HOODOO RIDGE (`hoodoo-columns`)
 * Bryce Canyon Silent City / Queen's Garden corrugated fin wall:
 * - Irregularly spaced fused fins dissect into dominant towers, low saddles
 *   and offset foreground sentinels rather than a regular organ-pipe row.
 * - Correlated Claron beds still pinch/swell every spire, but small phase
 *   offsets and a fused foreground apron keep the group grounded and organic.
 */
export function buildHoodooColumns(params, tectonics, rng, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const dipRad = clamp((params.dipAngle ?? 4), 0, 14) * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? 6) * DEG2RAD;
  const bedFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const overhang = params.overhangStrength ?? 0.82;
  const bevel = S * (0.0016 + 0.0028 * (params.edgeWear ?? 0.44));

  // 1. Irregular badlands ridge pedestals. Equal node spacing used to set up
  // a fence-like rhythm before the spires were even added.
  const finSpineU = [-0.48, -0.37, -0.25, -0.14, -0.05, 0.06, 0.17, 0.29, 0.41, 0.48];
  for (let i = 0; i < finSpineU.length; i++) {
    const u = finSpineU[i];
    const cx = u * 0.92 * S;
    const cz = Math.sin(u * 3.1 + i * 0.23) * S * 0.045;
    const sag = (i === 2 || i === 7) ? 0.78 : 1.0;
    const baseH = S * (0.30 - 0.13 * Math.abs(u)) * sag * rng.range(0.88, 1.16);

    let ped = sculptGeologicalParentPoly(
      [cx, groundY + baseH * 0.45, cz],
      [S * rng.range(0.085, 0.125), baseH * 0.54, S * (0.22 - 0.08 * Math.abs(u))],
      bedFrame,
      rng,
      { jointDispersion: 0.09, cornerBlunt: 0.78, flatBaseY: groundY, extraBevelCuts: 6 }
    );
    ped = clipPolyByHalfSpace(ped, [0, -1, 0], -groundY, 'base') || ped;
    const compP = compilePolyBlock(ped, bedFrame, {
      edgeBevel: bevel,
      smoothUnionR: S * 0.012,
      detailScale: 0.96,
      isSedimentary: true,
      hardness: 0.48,
      seedSalt: rng.int(1, 999999),
    });
    if (compP) blocks.push(compP);
  }

  // 2. Corrugated Parallel Hoodoo Spires & Fluted Badlands Buttress Ribs (continuous, zero floating squares!)
  const spires = [
    // Main fin: uneven gaps, one dominant tower pair, and a low saddle before
    // a right-hand sentinel instead of a perfectly descending ladder.
    { x: -0.06, z: -0.020, h: 1.02, rx: 0.084, rz: 0.118, style: 'bulbCap' },
    { x:  0.05, z:  0.015, h: 0.94, rx: 0.077, rz: 0.109, style: 'turret' },
    { x: -0.19, z: -0.030, h: 0.79, rx: 0.077, rz: 0.106, style: 'spire' },
    { x:  0.17, z: -0.025, h: 0.68, rx: 0.073, rz: 0.101, style: 'bulbCap' },
    { x: -0.31, z:  0.005, h: 0.58, rx: 0.071, rz: 0.096, style: 'turret' },
    { x:  0.28, z:  0.000, h: 0.45, rx: 0.066, rz: 0.089, style: 'spire' },
    { x:  0.39, z: -0.015, h: 0.66, rx: 0.068, rz: 0.092, style: 'turret' },
    { x: -0.43, z:  0.020, h: 0.37, rx: 0.062, rz: 0.083, style: 'spire' },
    { x:  0.48, z:  0.020, h: 0.33, rx: 0.057, rz: 0.076, style: 'spire' },
    // Foreground fin: staggered in both X and Z so it adds amphitheater
    // depth without mirroring the back wall.
    { x: -0.13, z:  0.145, h: 0.64, rx: 0.065, rz: 0.080, style: 'bulbCap' },
    { x:  0.01, z:  0.160, h: 0.49, rx: 0.059, rz: 0.074, style: 'spire' },
    { x:  0.12, z:  0.130, h: 0.57, rx: 0.061, rz: 0.077, style: 'turret' },
    { x: -0.26, z:  0.135, h: 0.34, rx: 0.054, rz: 0.068, style: 'spire' },
    { x:  0.24, z:  0.150, h: 0.38, rx: 0.055, rz: 0.070, style: 'bulbCap' },
  ];

  // Shared stratigraphy with a small per-spire phase. Beds still correlate
  // across the fin, but ribs no longer pulse in lockstep like organ pipes.
  const nZones = 12;
  const ribSalt = rng.int(1, 99999);
  function masterSwellAt(yf, phaseShift = 0) {
    return 0.94 +
      0.11 * Math.sin(yf * Math.PI * 7.2 + 0.5 + phaseShift) +
      0.05 * Math.cos(yf * Math.PI * 13.0 - phaseShift * 1.7);
  }

  for (let hi = 0; hi < spires.length; hi++) {
    const hd = spires[hi];
    const hx = hd.x * S + rng.signed() * S * 0.005;
    const hz = hd.z * S + rng.signed() * S * 0.005;
    const topY = hd.h * S * rng.range(0.94, 1.06);
    const ribPhase = Math.sin(hi * 2.39996) * 0.42;

    // Continuous fluted badlands fin core from groundY to topY (guarantees 0 floating caps!)
    let shaft = createBoxPoly(
      hx - hd.rx * 1.3 * S, groundY, hz - hd.rz * 1.3 * S,
      hx + hd.rx * 1.3 * S, topY, hz + hd.rz * 1.3 * S,
      'hoodooCore'
    );
    for (let f = 0; f < 8; f++) {
      const a = (f / 8) * Math.PI * 2 + hi * 0.37;
      const rad = 1.0 / Math.hypot(Math.cos(a) / (hd.rx * S * 0.76), Math.sin(a) / (hd.rz * S * 0.78));
      const fn = vnorm([Math.cos(a), rng.range(0.10, 0.16), Math.sin(a)]);
      shaft = clipPolyByHalfSpace(shaft, fn, vdot(fn, [hx, groundY + topY * 0.42, hz]) + rad, 'flute') || shaft;
    }
    const peakAz = rng.float() * Math.PI * 2;
    const peakN = vnorm([Math.cos(peakAz) * 0.58, 0.81, Math.sin(peakAz) * 0.58]);
    shaft = clipPolyByHalfSpace(shaft, peakN, vdot(peakN, [hx, topY - S * 0.015, hz]), 'corePeak') || shaft;
    const compShaft = compilePolyBlock(shaft, bedFrame, {
      edgeBevel: bevel,
      smoothUnionR: S * 0.011,
      detailScale: 0.96,
      isSedimentary: true,
      hardness: 0.55,
      seedSalt: (ribSalt + hi * 53) & 0x7fffffff,
    });
    if (compShaft) blocks.push(compShaft);

    // Smoothly melded stratigraphic pinch-and-swell ribs along the column (melded with smoothUnionR so they form
    // organic bulbous Claron rings rather than separate stacked blocks!)
    for (let zIdx = 0; zIdx < nZones; zIdx++) {
      const y0 = groundY + 0.12 * S + (zIdx / nZones) * (S * 0.85);
      const y1 = groundY + 0.12 * S + ((zIdx + 1) / nZones) * (S * 0.85);
      if (y0 >= topY - S * 0.03) break;

      const actualY1 = Math.min(topY, y1);
      const cy = 0.5 * (y0 + actualY1);
      const yf = cy / (S * 0.96);
      const hFrac = clamp((cy - groundY) / topY, 0, 1);
      const isCrown = actualY1 >= topY - S * 0.035;
      const isSubCrownNeck = !isCrown && actualY1 >= topY - S * 0.10;

      let taper = 1.14 - 0.46 * Math.pow(hFrac, 1.15);
      let swell = masterSwellAt(yf, ribPhase);
      if (hd.style === 'bulbCap') {
        if (isCrown) swell = 1.02 + 0.05 * overhang; // Weathered hard-bed crown, not a mushroom cap.
        else if (isSubCrownNeck) swell = 0.82;
      } else if (hd.style === 'spire' && isCrown) {
        swell = 0.76;
      }

      let ringPoly = sculptGeologicalParentPoly(
        [hx + rng.signed() * S * 0.003, cy, hz + rng.signed() * S * 0.003],
        [
          hd.rx * taper * swell * S * rng.range(0.92, 1.06),
          (actualY1 - y0) * 0.66,
          hd.rz * taper * swell * S * rng.range(0.92, 1.06),
        ],
        bedFrame,
        rng,
        {
          jointDispersion: 0.12,
          cornerBlunt: 0.74, // Multi-faceted rounded polygon, never a 4-sided square mortarboard!
          extraBevelCuts: 7,
        }
      );

      if (isCrown) {
        // Domed / pitched weathered hoodoo crown (eliminates flat horizontal tabletop caps!)
        const cAz = rng.float() * Math.PI * 2;
        const cN1 = vnorm([Math.cos(cAz) * 0.52, 0.85, Math.sin(cAz) * 0.52]);
        const cN2 = vnorm([-Math.cos(cAz + 0.8) * 0.56, 0.83, -Math.sin(cAz + 0.8) * 0.56]);
        ringPoly = clipPolyByHalfSpace(ringPoly, cN1, vdot(cN1, [hx, actualY1 - S * 0.008, hz]), 'hoodooDome') || ringPoly;
        ringPoly = clipPolyByHalfSpace(ringPoly, cN2, vdot(cN2, [hx, actualY1 - S * 0.008, hz]) + hd.rx * S * 0.16, 'hoodooDome') || ringPoly;
      }

      const compRing = compilePolyBlock(ringPoly, bedFrame, {
        edgeBevel: bevel * 1.15,
        smoothUnionR: S * 0.017, // Smoothly melds bulbous Claron rings into organic frost-sculpted totems!
        detailScale: 0.94,
        hardness: swell > 0.95 ? 0.86 : 0.45,
        isSedimentary: true,
        seedSalt: rng.int(1, 999999),
      });
      if (compRing) blocks.push(compRing);
    }
  }

  // Fused low badlands apron under the foreground fin. This grounds the
  // second row in the same eroding slope instead of leaving it as a set of
  // separate columns standing on the placement plane.
  const apronU = [-0.34, -0.18, -0.03, 0.12, 0.27];
  for (let ai = 0; ai < apronU.length; ai++) {
    const ax = apronU[ai] * S;
    const apronH = S * rng.range(0.10, 0.17) * (ai === 2 ? 1.18 : 1.0);
    let apron = sculptGeologicalParentPoly(
      [ax, groundY + apronH * 0.42, S * rng.range(0.16, 0.20)],
      [S * rng.range(0.10, 0.14), apronH * 0.52, S * rng.range(0.10, 0.14)],
      bedFrame,
      rng,
      { jointDispersion: 0.10, cornerBlunt: 0.80, flatBaseY: groundY, extraBevelCuts: 6 }
    );
    apron = clipPolyByHalfSpace(apron, [0, -1, 0], -groundY, 'base') || apron;
    const compApron = compilePolyBlock(apron, bedFrame, {
      edgeBevel: bevel,
      smoothUnionR: S * 0.014,
      detailScale: 0.95,
      isSedimentary: true,
      hardness: 0.46,
      seedSalt: rng.int(1, 999999),
    });
    if (compApron) blocks.push(compApron);
  }

  if (addTalusFn) {
    addTalusFn(blocks, bedFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.44) * 0.30,
      scale: S * 0.68,
      spanX: S * 0.86,
      groundY,
      edgeBevel: bevel,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, groundY };
}

/**
 * PEDESTAL BALANCED ROCK (`balanced-rock`)
 * Moab Balanced Rock / Mushroom Rock formation!
 */
export function buildBalancedRock(params, tectonics, rng, recursiveFractureFn, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const overhang = params.overhangStrength ?? 0.86;
  const bevel = S * (0.0022 + 0.004 * (params.edgeWear ?? 0.44));
  const baseFrame = buildStrikeDipFrame((params.strikeAngle ?? -18) * DEG2RAD, 5 * DEG2RAD);

  const plinthSteps = [
    { x:  0.00, z:  0.00, y: 0.055, rx: 0.36, ry: 0.06, rz: 0.32 },
    { x: -0.14, z:  0.08, y: 0.045, rx: 0.24, ry: 0.05, rz: 0.22 },
    { x:  0.16, z: -0.06, y: 0.050, rx: 0.23, ry: 0.055, rz: 0.22 },
    { x:  0.01, z:  0.00, y: 0.125, rx: 0.25, ry: 0.055, rz: 0.23 },
  ];
  for (let i = 0; i < plinthSteps.length; i++) {
    const ps = plinthSteps[i];
    const poly = sculptGeologicalParentPoly(
      [ps.x * S, groundY + ps.y * S, ps.z * S],
      [ps.rx * S, ps.ry * S, ps.rz * S],
      baseFrame,
      rng,
      { jointDispersion: 0.08, cornerBlunt: 0.82, flatBaseY: i < 3 ? groundY : undefined, extraBevelCuts: 6 }
    );
    const comp = compilePolyBlock(poly, baseFrame, {
      edgeBevel: bevel * 1.1,
      smoothUnionR: S * 0.016,
      detailScale: 0.95,
      isEolian: true,
      seedSalt: rng.int(1, 999999),
    });
    if (comp) blocks.push(comp);
  }

  const pedBands = 7;
  const pedStartY = groundY + 0.13 * S;
  const pedEndY = groundY + 0.48 * S;
  for (let b = 0; b < pedBands; b++) {
    const f = b / (pedBands - 1);
    const y0 = pedStartY + (b / pedBands) * (pedEndY - pedStartY);
    const y1 = pedStartY + ((b + 1) / pedBands) * (pedEndY - pedStartY);
    const cy = 0.5 * (y0 + y1);

    const baseTaper = 0.076 + 0.128 * Math.pow(1 - f, 1.75);
    const ribMod = (b % 2 === 0) ? 1.06 : 0.92;
    const rBand = baseTaper * ribMod * S;

    const bandPoly = sculptGeologicalParentPoly(
      [rng.signed() * S * 0.006, cy, rng.signed() * S * 0.006],
      [rBand * 1.05, (y1 - y0) * 0.62, rBand * 0.98],
      baseFrame,
      rng,
      { jointDispersion: 0.08, cornerBlunt: 0.80, extraBevelCuts: 5 }
    );
    const compBand = compilePolyBlock(bandPoly, baseFrame, {
      edgeBevel: bevel * 1.15,
      smoothUnionR: S * 0.012,
      detailScale: 1.05,
      hardness: 0.35,
      isSedimentary: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compBand) blocks.push(compBand);
  }

  const capTilt = clamp((params.dipAngle ?? 16), 12, 26) * DEG2RAD;
  const capStrike = ((params.strikeAngle ?? -18) * DEG2RAD) + rng.signed() * 0.2;
  const capFrame = buildStrikeDipFrame(capStrike, capTilt);

  const capRx = S * (0.185 + 0.038 * overhang);
  const capRy = S * 0.235;
  const capRz = S * (0.155 + 0.025 * overhang);
  const capCx = S * 0.022;
  const capCy = groundY + 0.665 * S;

  let capPoly = sculptGeologicalParentPoly(
    [capCx, capCy, 0],
    [capRx, capRy, capRz],
    capFrame,
    rng,
    { jointDispersion: 0.16, cornerBlunt: 0.76, extraBevelCuts: 7 }
  );

  const prowN1 = perturbNormal(vnorm([-capFrame.dipVec[0] * 0.62, 0.78, -capFrame.dipVec[2] * 0.62]), rng, 0.12);
  const prowN2 = perturbNormal(vnorm([capFrame.strikeVec[0] * 0.56, 0.82, capFrame.strikeVec[2] * 0.56]), rng, 0.12);
  capPoly = clipPolyByHalfSpace(capPoly, prowN1, vdot(prowN1, [capCx, capCy + capRy * 0.42, 0]) + capRy * 0.38, 'crest') || capPoly;
  capPoly = clipPolyByHalfSpace(capPoly, prowN2, vdot(prowN2, [capCx, capCy + capRy * 0.42, 0]) + capRx * 0.42, 'crest') || capPoly;

  const capParts = recursiveFractureFn
    ? recursiveFractureFn(capPoly, capFrame, tectonics, rng, {
        maxDepth: 1,
        minSpan: S * 0.12,
        minVolume: (S * 0.08) ** 3,
        bedSplitBias: 1.3,
        openCleftChance: 0.0,
        overlapDepth: S * 0.020,
        shearSlip: 0.05,
        pluckChance: 0,
        groundY: null,
      })
    : [capPoly];

  for (const cp of capParts) {
    const compCap = compilePolyBlock(cp, capFrame, {
      edgeBevel: bevel * 0.80,
      smoothUnionR: S * 0.004,
      detailScale: 0.82,
      hardness: 0.95,
      isEolian: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compCap) blocks.push(compCap);
  }

  if (addTalusFn) {
    addTalusFn(blocks, baseFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.44) * 0.85,
      scale: S * 0.80,
      spanX: S * 0.64,
      groundY,
      edgeBevel: bevel * 0.85,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, groundY };
}

/**
 * LIMESTONE KARST & SOLUTION GRIKES (`limestone-karst`)
 * Tsingy de Bemaraha / Malham Cove dissolution limestone formation:
 * - Broad, irregular weathered limestone pavement tables (`Clints`) dissected by deep conjugate
 *   `Grike` corridors (ZERO tiny square marshmallow cubes!)
 * - 14 continuous, unbroken, upward-tapering razor-edged Tsingy limestone fins & fluted pinnacles
 *   rising straight from the karst plinth to sharp knife-edge crests (ZERO gnome-hat caps!)
 */
export function buildLimestoneKarst(params, tectonics, rng, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const dipRad = clamp((params.dipAngle ?? 5), 0, 18) * DEG2RAD;
  const strikeRad = (params.strikeAngle ?? 14) * DEG2RAD;
  const bedFrame = buildStrikeDipFrame(strikeRad, dipRad);
  const aperture = params.jointAperture ?? 0.50;
  const bevel = S * (0.0013 + 0.0024 * (params.edgeWear ?? 0.34));
  const cosS = Math.cos(strikeRad), sinS = Math.sin(strikeRad);

  // 1. Six Broad Irregular Polygonal Limestone Clint Pavement Tables Separated by Deep Grike Fissures!
  // Thin shadowed karst floor ensures grike corridors stay grounded without piercing bottom mesh
  const subFloor = sculptGeologicalParentPoly(
    [0, groundY + 0.020 * S, 0],
    [0.44 * S, 0.024 * S, 0.36 * S],
    bedFrame,
    rng,
    { jointDispersion: 0.14, cornerBlunt: 0.78, flatBaseY: groundY, extraBevelCuts: 6 }
  );
  const compFloor = compilePolyBlock(subFloor, bedFrame, {
    edgeBevel: bevel,
    smoothUnionR: S * 0.005,
    detailScale: 0.90,
    seedSalt: rng.int(1, 999999),
  });
  if (compFloor) blocks.push(compFloor);

  const clintTables = [
    { u: -0.24, v:  0.18, ru: 0.18, rv: 0.13, h: 0.13 },
    { u:  0.19, v:  0.19, ru: 0.19, rv: 0.13, h: 0.12 },
    { u: -0.26, v: -0.11, ru: 0.18, rv: 0.14, h: 0.19 },
    { u:  0.04, v: -0.06, ru: 0.16, rv: 0.14, h: 0.22 },
    { u:  0.30, v: -0.09, ru: 0.16, rv: 0.14, h: 0.18 },
    { u: -0.02, v:  0.24, ru: 0.14, rv: 0.10, h: 0.09 },
  ];
  const grikeTrim = S * (0.008 + 0.014 * aperture);

  for (const ct of clintTables) {
    const wx = (ct.u * cosS - ct.v * sinS) * S;
    const wz = (ct.u * sinS + ct.v * cosS) * S;
    const ch = ct.h * S * rng.range(0.90, 1.10);
    const localF = buildStrikeDipFrame(strikeRad + rng.signed() * 0.08, dipRad);

    let poly = sculptGeologicalParentPoly(
      [wx, groundY + ch * 0.48, wz],
      [ct.ru * S - grikeTrim, ch * 0.54, ct.rv * S - grikeTrim],
      localF,
      rng,
      { jointDispersion: 0.12, cornerBlunt: 0.76, flatBaseY: groundY, extraBevelCuts: 7 }
    );
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
    const comp = compilePolyBlock(poly, bedFrame, {
      edgeBevel: bevel * 1.4,
      smoothUnionR: S * 0.004,
      detailScale: 1.0,
      seedSalt: rng.int(1, 999999),
    });
    if (comp) blocks.push(comp);
  }

  // 2. Fourteen Continuous, Elongated Tsingy Razor-Blade Limestone Fins & Stepped Shoulders!
  // Strongly elongated along strike (`ru ~ 2.4x rv`) so they read as true tectonic karst blades, never cones!
  const tsingyFins = [
    { u: -0.02, v: -0.05, h: 0.94, ru: 0.195, rv: 0.072, step: true },
    { u:  0.19, v: -0.03, h: 0.80, ru: 0.165, rv: 0.068, step: true },
    { u: -0.22, v: -0.02, h: 0.76, ru: 0.175, rv: 0.070, step: true },
    { u:  0.06, v: -0.19, h: 0.72, ru: 0.165, rv: 0.068, step: false },
    { u: -0.15, v: -0.18, h: 0.64, ru: 0.150, rv: 0.064, step: false },
    { u: -0.11, v:  0.10, h: 0.58, ru: 0.150, rv: 0.062, step: true },
    { u:  0.10, v:  0.09, h: 0.52, ru: 0.145, rv: 0.060, step: true },
    { u:  0.32, v: -0.07, h: 0.56, ru: 0.145, rv: 0.064, step: false },
    { u: -0.35, v: -0.08, h: 0.50, ru: 0.140, rv: 0.064, step: false },
    { u:  0.25, v:  0.12, h: 0.40, ru: 0.130, rv: 0.058, step: false },
    { u: -0.27, v:  0.13, h: 0.38, ru: 0.130, rv: 0.058, step: false },
    { u: -0.01, v:  0.21, h: 0.31, ru: 0.120, rv: 0.054, step: false },
    { u:  0.39, v:  0.03, h: 0.33, ru: 0.115, rv: 0.054, step: false },
    { u: -0.42, v:  0.03, h: 0.30, ru: 0.115, rv: 0.054, step: false },
  ];

  function buildKarstBlade(tx, tz, th, ru, rv, seedIdx) {
    const maxR = Math.max(ru, rv) * 1.55;
    let poly = createBoxPoly(
      tx - maxR, groundY, tz - maxR,
      tx + maxR, groundY + th * 1.12, tz + maxR,
      'tsingyFin'
    );

    const nFaces = 8;
    const az0 = strikeRad + seedIdx * 0.41;
    for (let f = 0; f < nFaces; f++) {
      const a = az0 + (f * Math.PI * 2) / nFaces + rng.signed() * 0.14;
      const da = a - strikeRad;
      const rad = 1.0 / Math.hypot(Math.cos(da) / ru, Math.sin(da) / rv) * rng.range(0.84, 1.02);
      // Broad blade faces taper gently (0.09..0.16), narrow fin ends taper more sharply (0.20..0.32)!
      const isEndFace = Math.abs(Math.cos(da)) > 0.65;
      const taperSlope = isEndFace ? rng.range(0.20, 0.32) : rng.range(0.09, 0.16);
      const fn = vnorm([Math.cos(a), taperSlope, Math.sin(a)]);
      poly = clipPolyByHalfSpace(poly, fn, vdot(fn, [tx, groundY + th * 0.38, tz]) + rad, 'karstWall') || poly;
    }

    const crestAz = strikeRad + Math.PI * 0.5 + rng.signed() * 0.18;
    const pN1 = vnorm([Math.cos(crestAz) * 0.84, 0.54, Math.sin(crestAz) * 0.84]);
    const pN2 = vnorm([-Math.cos(crestAz + 0.18) * 0.82, 0.57, -Math.sin(crestAz + 0.18) * 0.82]);
    const endSign = seedIdx % 2 === 0 ? 1 : -1;
    const endN = vnorm([Math.cos(strikeRad) * endSign * 0.72, 0.69, Math.sin(strikeRad) * endSign * 0.72]);
    poly = clipPolyByHalfSpace(poly, pN1, vdot(pN1, [tx, groundY + th * 0.96, tz]), 'knifePeak') || poly;
    poly = clipPolyByHalfSpace(poly, pN2, vdot(pN2, [tx, groundY + th * 0.96, tz]) + rv * 0.12, 'knifePeak') || poly;
    poly = clipPolyByHalfSpace(poly, endN, vdot(endN, [tx, groundY + th * 0.93, tz]) + ru * 0.12, 'knifeEnd') || poly;
    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;

    const comp = compilePolyBlock(poly, bedFrame, {
      edgeBevel: bevel,
      smoothUnionR: S * 0.0045,
      detailScale: 1.16,
      seedSalt: rng.int(1, 999999),
    });
    if (comp) blocks.push(comp);
  }

  for (let i = 0; i < tsingyFins.length; i++) {
    const tf = tsingyFins[i];
    const tx = (tf.u * cosS - tf.v * sinS) * S + rng.signed() * S * 0.008;
    const tz = (tf.u * sinS + tf.v * cosS) * S + rng.signed() * S * 0.008;
    const th = tf.h * S * rng.range(0.93, 1.06);
    const ru = tf.ru * S * rng.range(0.90, 1.12);
    const rv = tf.rv * S * rng.range(0.88, 1.10);

    buildKarstBlade(tx, tz, th, ru, rv, i * 2);

    // Secondary stepped karst fin shoulder along the same strike blade (breaks cone symmetry!)
    if (tf.step) {
      const dir = i % 2 === 0 ? 1 : -1;
      const offU = dir * ru * 0.56;
      buildKarstBlade(
        tx + cosS * offU,
        tz + sinS * offU,
        th * rng.range(0.58, 0.74),
        ru * 0.72,
        rv * 0.88,
        i * 2 + 1
      );
    }
  }

  if (addTalusFn) {
    addTalusFn(blocks, bedFrame, rng, {
      talusAmount: (params.talusAmount ?? 0.42) * 0.75,
      scale: S * 0.84,
      spanX: S * 0.80,
      groundY,
      edgeBevel: bevel,
      mode: 'outcrop',
    });
  }

  return { blocks, cuts, groundY };
}

/**
 * GRANITE HALF-DOME & ONION-SKIN SHEETING (`exfoliation-dome`)
 * Yosemite Half Dome / Tuolumne exfoliation granite massif driven by its Dome Spine Spline:
 * - Clean, monumental ~82-deg sheer glacial NW headwall sliced across the front (ZERO artificial
 *   punched holes, ZERO horizontal icing stripes across the middle of the sheer cliff!).
 * - Iconic overhanging Summit Visor (`Thank God Ledge`) crowning the sheer headwall.
 * - Concentric onion-skin exfoliation shells wrapping the curved granite dome crown, back slope,
 *   and lateral shoulders, peeling back in crisp arched spall steps around the perimeter!
 * - Glacially polished granite apron slabs + natural angular talus boulders at the base.
 */
export function buildExfoliationDome(params, tectonics, rng, addTalusFn) {
  const S = params.scale;
  const groundY = 0;
  const blocks = [];
  const cuts = [];

  const overhang = params.overhangStrength ?? 0.68;
  const bevel = S * (0.0013 + 0.0022 * (params.edgeWear ?? 0.44));
  const strikeRad = (params.strikeAngle ?? 10) * DEG2RAD;
  const baseFrame = buildStrikeDipFrame(strikeRad, 10 * DEG2RAD);
  const domeSalt = rng.int(1, 999999);

  const activeSplines = Array.isArray(params.formationSplines) && params.formationSplines.length >= 1
    ? params.formationSplines
    : getFormationSplineSpec('exfoliation-dome', S).defaultCurves;
  const spine = compileSplineArcTable(activeSplines[0], 96);

  const spanLen = Math.max(S * 0.55, spine.totalHorizLength);
  const baseDepth = S * 0.66;
  // 14 overlapping granite dome lobes with buried internal end-caps (`halfW = 0.86 * segLen` + recessed +/-0.30 guard planes):
  // Smoothly bends along any 3D Spine Spline while burying every internal end-cap 1.6 cm underground -> ZERO seams!
  const nCols = clamp(Math.round(spanLen / (S * 0.11)), 12, 20);
  const du = 1 / nCols;

  function domeArchHeightAt(u, splineY) {
    const su = (u - 0.5) * 2.0; // -1 .. +1
    // Broad bulbous Yosemite Half-Dome granite crown (broad dome summit plateau, steep rounded shoulders!)
    const arch = Math.pow(Math.max(0.10, 1.0 - 0.72 * Math.pow(Math.abs(su), 2.85)), 0.44);
    const gentleSwell = 1.0 + 0.015 * Math.cos(su * 3.1 - 0.25);
    return Math.max(S * 0.16, (0.36 * S * 0.78 + 0.64 * splineY) * arch * gentleSwell);
  }

  function domeBackDepthAt(u, rMul) {
    const su = (u - 0.5) * 2.0;
    const ellip = Math.pow(Math.max(0.14, 1.0 - 0.84 * Math.pow(Math.abs(su), 2.15)), 0.48);
    return baseDepth * ellip * rMul;
  }

  function headwallOffsetAt(u, rMul) {
    const su = (u - 0.5) * 2.0;
    // Gently convex planar glacial headwall (prevents concave slice-tangent overlap seams across the sheer granite face!)
    const shoulderWrap = -0.10 * Math.pow(Math.abs(su), 3.2);
    const gentleBow = 0.022 * (1.0 - su * su);
    return (gentleBow + shoulderWrap) * S * rMul;
  }

  // Builds a C1-smooth curved granite dome / onion-skin exfoliation shell slice at station `i`
  function addDomeSlice(i, shellScale, frontTrimFn, minYfFn, maxAbsSu, hard, smoothR, saltOff) {
    const u0 = i * du;
    const u1 = (i + 1) * du;
    const uMid = 0.5 * (u0 + u1);
    const su0 = (u0 - 0.5) * 2.0;
    const su1 = (u1 - 0.5) * 2.0;
    const su = (uMid - 0.5) * 2.0;
    if (Math.abs(su) > maxAbsSu) return;

    const st0 = spine.sampleAtArc(u0);
    const st1 = spine.sampleAtArc(u1);
    const stM = spine.sampleAtArc(uMid);

    const mfM = typeof minYfFn === 'function' ? minYfFn(su) : minYfFn;
    const mf0 = typeof minYfFn === 'function' ? minYfFn(su0) : minYfFn;
    const mf1 = typeof minYfFn === 'function' ? minYfFn(su1) : minYfFn;

    const hTop = domeArchHeightAt(uMid, stM.y) * shellScale;
    const h0 = domeArchHeightAt(u0, st0.y) * shellScale;
    const h1 = domeArchHeightAt(u1, st1.y) * shellScale;
    const yMin = groundY + hTop * mfM;
    const yMin0 = groundY + h0 * mf0;
    const yMin1 = groundY + h1 * mf1;
    if (hTop - (yMin - groundY) < S * 0.032) return;

    const backD = domeBackDepthAt(uMid, stM.r) * shellScale;
    const trimM = frontTrimFn(su);
    const fOff0 = headwallOffsetAt(u0, st0.r) + frontTrimFn(su0);
    const fOff1 = headwallOffsetAt(u1, st1.r) + frontTrimFn(su1);
    const fOffM = 0.5 * (fOff0 + fOff1);
    const segLen = spanLen / nCols;
    const halfW = segLen * 0.86;

    const cx = stM.x + stM.nx * (0.5 * (fOffM - backD));
    const cz = stM.z + stM.nz * (0.5 * (fOffM - backD));
    const reach = Math.max(halfW, 0.5 * (Math.abs(fOffM) + backD)) + S * 0.18;

    let poly = createBoxPoly(
      cx - reach,
      mfM <= 0.001 ? groundY : Math.min(yMin, yMin0, yMin1) - S * 0.04,
      cz - reach,
      cx + reach,
      groundY + Math.max(hTop, h0, h1) + S * 0.06,
      cz + reach,
      'graniteDomeSlice'
    );

    const midY = 0.5 * (yMin + groundY + hTop);
    const isLeftOuter = i === 0 || ((i - 0.5) * du - 0.5) * 2.0 < -maxAbsSu;
    const isRightOuter = i === nCols - 1 || ((i + 1.5) * du - 0.5) * 2.0 > maxAbsSu;
    const leftSpan = isLeftOuter ? segLen * 0.52 : halfW;
    const rightSpan = isRightOuter ? segLen * 0.52 : halfW;
    const leftN = [-stM.tx, 0, -stM.tz];
    const rightN = [stM.tx, 0, stM.tz];
    poly = clipPolyByHalfSpace(poly, leftN, vdot(leftN, [stM.x - stM.tx * leftSpan, midY, stM.z - stM.tz * leftSpan]), 'slL') || poly;
    poly = clipPolyByHalfSpace(poly, rightN, vdot(rightN, [stM.x + stM.tx * rightSpan, midY, stM.z + stM.tz * rightSpan]), 'slR') || poly;

    const headBatter = trimM > S * 0.003
      ? -0.18 * (0.55 + 0.55 * overhang)
      : 0.085;

    const elevAngles = [0.16, 0.38, 0.62, 0.86, 1.10, 1.32, 1.50];
    for (const subF of [-0.30, 0.0, 0.50, 1.0, 1.30]) {
      if ((subF < 0 && isLeftOuter) || (subF > 1 && isRightOuter)) continue;
      const isGuard = subF < -0.01 || subF > 1.01;
      const buryIn = isGuard ? S * 0.028 : 0.0;
      const uS = clamp(u0 + (u1 - u0) * subF, 0.005, 0.995);
      const suS = (uS - 0.5) * 2.0;
      const uSm = Math.max(0, uS - 0.02);
      const uSp = Math.min(1, uS + 0.02);
      const stS = spine.sampleAtArc(uS);
      const stSm = spine.sampleAtArc(uSm);
      const stSp = spine.sampleAtArc(uSp);
      const dsLocal = Math.max(S * 0.025, (uSp - uSm) * spanLen);
      const dwSign = 0.0;

      const fS = headwallOffsetAt(uS, stS.r) + frontTrimFn(suS) - buryIn;
      const fSm = headwallOffsetAt(uSm, stSm.r) + frontTrimFn((uSm - 0.5) * 2.0);
      const fSp = headwallOffsetAt(uSp, stSp.r) + frontTrimFn((uSp - 0.5) * 2.0);
      const dfLocal = clamp((fSp - fSm) / dsLocal + dwSign, -0.85, 0.85);
      const headN = vnorm([stS.nx - stS.tx * dfLocal, headBatter, stS.nz - stS.tz * dfLocal]);
      poly = clipPolyByHalfSpace(poly, headN, vdot(headN, [stS.x + stS.nx * fS, midY, stS.z + stS.nz * fS]), 'glacialHeadwall') || poly;

      const hS = Math.max(S * 0.08, domeArchHeightAt(uS, stS.y) * shellScale - buryIn * 0.85);
      const bS = Math.max(S * 0.08, domeBackDepthAt(uS, stS.r) * shellScale - buryIn);
      const dhLocal = clamp(
        (domeArchHeightAt(uSp, stSp.y) - domeArchHeightAt(uSm, stSm.y)) * shellScale / dsLocal + dwSign,
        -1.1,
        1.1
      );
      const dbLocal = clamp(
        (domeBackDepthAt(uSp, stSp.r) - domeBackDepthAt(uSm, stSp.r)) * shellScale / dsLocal + dwSign,
        -1.1,
        1.1
      );
      const invB = 1 / bS;
      const invHs = 1 / hS;
      for (const el of elevAngles) {
        const ce = Math.cos(el);
        const se = Math.sin(el);
        const anchor = [stS.x - stS.nx * ce * bS, groundY + se * hS, stS.z - stS.nz * ce * bS];
        const gN = -ce * invB;
        const gY = se * invHs;
        const gT = -se * se * dhLocal * invHs - ce * ce * dbLocal * invB;
        const bn = vnorm([stS.nx * gN + stS.tx * gT, gY, stS.nz * gN + stS.tz * gT]);
        poly = clipPolyByHalfSpace(poly, bn, vdot(bn, anchor), 'domeBackCurve') || poly;
      }
    }

    // Smoothly arched onion-skin exfoliation spall scarp along the bottom edge of outer shells!
    if (mfM > 0.01 && poly) {
      const dyMinDs = clamp((yMin1 - yMin0) / Math.max(S * 0.03, segLen), -0.95, 0.95);
      const spallN = vnorm([
        -stM.nx * 0.18 + stM.tx * dyMinDs,
        -0.96,
        -stM.nz * 0.18 + stM.tz * dyMinDs,
      ]);
      poly = clipPolyByHalfSpace(poly, spallN, vdot(spallN, [cx, yMin, cz]), 'onionSpallStep') || poly;
    }

    // Taper outer lateral ends cleanly into the granite shoulders
    if ((isLeftOuter || isRightOuter) && maxAbsSu < 0.98 && poly) {
      const endSign = isRightOuter ? 1 : -1;
      const endN = vnorm([stM.tx * endSign * 0.86, 0.48, stM.tz * endSign * 0.86]);
      const endPt = [stM.x + stM.tx * endSign * segLen * 0.46, midY, stM.z + stM.tz * endSign * segLen * 0.46];
      poly = clipPolyByHalfSpace(poly, endN, vdot(endN, endPt), 'shellEnd') || poly;
    }

    poly = clipPolyByHalfSpace(poly, [0, -1, 0], -groundY, 'base') || poly;
    if (!poly) return;

    const comp = compilePolyBlock(poly, baseFrame, {
      edgeBevel: bevel * 0.22,
      smoothUnionR: 0, // Exact min union eliminates sequential smin slice-overlap quilt ribs!
      detailScale: 0.44,
      hardness: hard,
      noSeamNotch: true,
      seedSalt: (domeSalt + saltOff) & 0x7fffffff,
    });
    if (comp) blocks.push(comp);
  }

  // 1. Monolithic Inner Granite Dome Core + Clean Sheer Glacial NW Headwall
  for (let i = 0; i < nCols; i++) {
    addDomeSlice(i, 0.94, () => 0.0, 0.0, 1.02, 0.90, 0, 100);
  }

  // 2. Two Concentric Back & Shoulder Onion-Skin Exfoliation Shells
  //    Peeling cleanly around the curved dome back & lateral shoulders with C1-continuous arched spall steps!
  const archSpallFn1 = (suVal) => 0.22 + 0.14 * (1.0 - suVal * suVal);
  const archSpallFn2 = (suVal) => 0.48 + 0.16 * (1.0 - suVal * suVal);
  for (let i = 0; i < nCols; i++) {
    addDomeSlice(i, 0.982, () => -S * 0.012, archSpallFn1, 0.92, 0.92, 0, 400);
    addDomeSlice(i, 1.018, () => -S * 0.008, archSpallFn2, 0.80, 0.94, 0, 700);
  }

  // 3. Iconic Overhanging Half-Dome Summit Visor (`Thank God Ledge` crowning the upper sheer face!)
  //    Tapers smoothly to zero projection at |su| = 0.62 so there are ZERO gable staircase teeth!
  const visorProjFn = (suVal) => {
    const t = clamp(1.0 - (suVal * suVal) / (0.62 * 0.62), 0, 1);
    return S * 0.032 * (0.55 + 0.55 * overhang) * t * t;
  };
  const visorFloorFn = (suVal) => 0.84 + 0.05 * suVal * suVal;
  for (let i = 0; i < nCols; i++) {
    addDomeSlice(i, 1.045, visorProjFn, visorFloorFn, 0.64, 0.96, 0, 1100);
  }

  // 4. Low, Continuous Glacially Polished Granite Apron Ramp at the Headwall Base (no giant sidewalk teeth!)
  const nApron = clamp(Math.round(nCols * 0.65), 10, 20);
  for (let a = 0; a < nApron; a++) {
    const u0 = 0.08 + (a / nApron) * 0.84;
    const u1 = 0.08 + ((a + 1) / nApron) * 0.84;
    const u = 0.5 * (u0 + u1);
    const st = spine.sampleAtArc(u);
    const su = (u - 0.5) * 2.0;
    const env = Math.max(0.15, 1.0 - 0.85 * su * su);
    const fOff = headwallOffsetAt(u, st.r) + S * 0.025 * env;
    const slabH = S * (0.028 + 0.032 * env);
    const slabW = (spanLen * 0.84 / nApron) * 0.92;
    const slabD = S * (0.075 + 0.055 * env);
    const sx = st.x + st.nx * (fOff + slabD * 0.28);
    const sz = st.z + st.nz * (fOff + slabD * 0.28);
    const sf = buildStrikeDipFrame(Math.atan2(-st.nx, st.nz), 22 * DEG2RAD);

    let aPoly = sculptGeologicalParentPoly(
      [sx, groundY + slabH * 0.45, sz],
      [slabW, slabH * 0.60, slabD],
      sf,
      rng,
      { jointDispersion: 0.04, cornerBlunt: 0.84, flatBaseY: groundY, extraBevelCuts: 2 }
    );
    if (!aPoly) continue;
    const glacSlopeN = vnorm([st.nx * 0.46, 0.89, st.nz * 0.46]);
    aPoly = clipPolyByHalfSpace(aPoly, glacSlopeN, vdot(glacSlopeN, [sx, groundY + slabH * 0.88, sz]), 'glacApron') || aPoly;
    aPoly = clipPolyByHalfSpace(aPoly, [0, -1, 0], -groundY, 'base') || aPoly;
    const compA = compilePolyBlock(aPoly, baseFrame, {
      edgeBevel: bevel * 0.28,
      smoothUnionR: 0,
      detailScale: 0.48,
      hardness: 0.88,
      noSeamNotch: true,
      seedSalt: (domeSalt + 2100) & 0x7fffffff,
    });
    if (compA) blocks.push(compA);
  }

  // 5. Restrained Angular Granite Talus Boulders settled along the Cirque Toe
  const nTalus = Math.round(7 + (params.talusAmount ?? 0.48) * 8);
  for (let t = 0; t < nTalus; t++) {
    const u = rng.range(0.10, 0.90);
    const st = spine.sampleAtArc(u);
    const rB = S * rng.range(0.022, 0.048);
    const distOut = headwallOffsetAt(u, st.r) + S * rng.range(0.06, 0.19);
    const bx = st.x + st.nx * distOut;
    const bz = st.z + st.nz * distOut;
    const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(14, 42) * DEG2RAD);
    let bPoly = sculptGeologicalParentPoly(
      [bx, groundY + rB * 0.46, bz],
      [rB * 1.28, rB * 0.64, rB * 1.02],
      bf,
      rng,
      { jointDispersion: 0.18, cornerBlunt: 0.68, flatBaseY: groundY, extraBevelCuts: 5 }
    );
    if (!bPoly) continue;
    bPoly = clipPolyByHalfSpace(bPoly, [0, -1, 0], -groundY, 'base') || bPoly;
    const compB = compilePolyBlock(bPoly, bf, {
      edgeBevel: bevel * 0.85,
      detailScale: 0.88,
      hardness: 0.90,
      isBoulder: true,
      seedSalt: rng.int(1, 999999),
    });
    if (compB) blocks.push(compB);
  }

  return { blocks, cuts, groundY };
}
