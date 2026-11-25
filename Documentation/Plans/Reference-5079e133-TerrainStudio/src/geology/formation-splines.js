/**
 * Universal Multi-Spline Rig & 3D Geological Curve Parameterization for Rock & Cliff Studio.
 *
 * Every formation archetype exposes an intuitive set of 3D Catmull-Rom control splines:
 *   - `canyon` (`slot-canyon`, `studlagil-colonnade`):
 *       3 splines — Left Wall, Centre Channel/River, and Right Wall.
 *   - `cliff` (`sedimentary-cliff`, `utah-canyon`, `cliff-wall`, `exfoliation-dome`,
 *              `metamorphic-outcrop`, `saharan-yardang-fin`, `basalt-columns`,
 *              `sandstone-butte`, `grand-canyon-temple`, `limestone-karst`,
 *              `icelandic-tuya-crag`, `calved-cliff-boulders`):
 *       Spine spline controlling strike direction, planform curve, crest height, and cliff length.
 *   - `arch` (`desert-arch`, `double-arch`, `sea-arch`, `arch-encampment`):
 *       1–2 Arch Span splines controlling pier-to-crown-to-pier span direction, length, and arc.
 *   - `spire` (`desert-spires`, `hoodoo-columns`, `balanced-rock`, `alpine-crag`,
 *              `granite-tor`, `hero-boulder`, `cracked-monolith`):
 *       1–3 vertical/leaning Spire Stem splines (`Base -> Waist -> Summit`) controlling position,
 *       lean, height, and waist/pedestal radius.
 */

import { clamp, vnorm } from '../core/vec3.js';
import { evalCatmullRom3D } from './custom-arch-spline.js';
import { FORMATION_PRESETS } from './presets.js';

function round2(v) {
  return Number(v.toFixed(2));
}

function pt(x, y, z, r = 1.0) {
  return { x: round2(x), y: round2(y), z: round2(z), r: round2(r) };
}

export function cloneSplineCurves(curves) {
  if (!Array.isArray(curves)) return [];
  return curves.map((c) =>
    Array.isArray(c)
      ? c.map((p) => ({
          x: Number(p.x) || 0,
          y: Number(p.y) || 0,
          z: Number(p.z) || 0,
          r: Number.isFinite(Number(p.r)) ? Number(p.r) : 1.0,
        }))
      : []
  );
}

/**
 * Returns the canonical spline family, role metadata, and rest-pose 3D splines
 * for any Rock & Cliff Studio archetype at world scale `scale`.
 */
export function getFormationSplineSpec(archetype = 'desert-arch', scaleInput = null) {
  const preset = FORMATION_PRESETS[archetype] || FORMATION_PRESETS['desert-arch'];
  const S = Number.isFinite(Number(scaleInput)) && Number(scaleInput) > 0
    ? Number(scaleInput)
    : (preset?.params?.scale ?? 16.0);

  // ---------------------------------------------------------------------------
  // 1. CANYONS & GORGES: 3 Required Splines (Left Wall, Centre Channel, Right Wall)
  // ---------------------------------------------------------------------------
  if (archetype === 'slot-canyon' || archetype === 'studlagil-colonnade') {
    const isBasaltGorge = archetype === 'studlagil-colonnade';
    const lenZ = isBasaltGorge ? S * 1.18 : S * 1.22;
    const wallH = isBasaltGorge ? S * 0.64 : S * 0.80;
    const halfCorr = isBasaltGorge ? S * 0.165 : S * 0.088;
    const zPts = [-0.48, -0.24, 0.0, 0.24, 0.48];

    const leftCurve = [];
    const centerCurve = [];
    const rightCurve = [];
    for (let i = 0; i < zPts.length; i++) {
      const zf = zPts[i];
      const z = zf * lenZ;
      const meanderX = isBasaltGorge
        ? S * 0.085 * Math.sin(zf * Math.PI * 1.65 + 0.35)
        : S * 0.095 * Math.sin(zf * Math.PI * 1.75 + 0.40);
      const flare = 1.0 + (isBasaltGorge ? 0.22 : 0.28) * Math.pow(Math.abs(zf) * 2.0, 2.0);
      const gap = halfCorr * flare;
      const sDown = zf + 0.5;
      const floorY = isBasaltGorge
        ? S * (0.048 * (1 - sDown) + 0.022 * sDown)
        : S * (0.048 * (1 - sDown) + 0.024 * sDown);
      const rimEnv = 1.0 - 0.16 * Math.pow(Math.abs(zf) * 2.0, 2.2);

      leftCurve.push(pt(meanderX - gap, wallH * rimEnv, z, 1.0));
      centerCurve.push(pt(meanderX, floorY, z, 1.0));
      rightCurve.push(pt(meanderX + gap, wallH * rimEnv * (isBasaltGorge ? 0.96 : 1.0), z, 1.0));
    }

    return {
      family: 'canyon',
      modeLabel: '3-Spline Canyon (L · C · R)',
      canAddBranch: false,
      roles: [
        { id: 'left', label: 'Left Wall', color: '#ff7b54' },
        { id: 'center', label: 'Riverbed / Thalweg', color: '#4dc4ff' },
        { id: 'right', label: 'Right Wall', color: '#4ce08a' },
      ],
      defaultCurves: [leftCurve, centerCurve, rightCurve],
    };
  }

  // ---------------------------------------------------------------------------
  // 2. ARCHES: Span Spline(s) (Pier -> Shoulder -> Crown -> Shoulder -> Pier)
  // ---------------------------------------------------------------------------
  if (
    archetype === 'desert-arch' ||
    archetype === 'double-arch' ||
    archetype === 'sea-arch' ||
    archetype === 'arch-encampment'
  ) {
    const spanHalf = archetype === 'sea-arch' ? S * 0.42 : S * 0.46;
    const crownH = archetype === 'sea-arch' ? S * 0.50 : S * 0.54;
    const mainSpan = [
      pt(-spanHalf, 0, -S * 0.03, 1.25),
      pt(-spanHalf * 0.56, crownH * 0.72, S * 0.02, 1.04),
      pt(0, crownH, S * 0.04, 0.88),
      pt(spanHalf * 0.56, crownH * 0.70, 0, 1.04),
      pt(spanHalf, 0, -S * 0.04, 1.25),
    ];
    const defaultCurves = [mainSpan];
    const roles = [{ id: 'arch-main', label: 'Main Arch Span', color: '#6bc2ff' }];

    if (archetype === 'double-arch' || archetype === 'arch-encampment') {
      defaultCurves.push([
        pt(-spanHalf * 0.82, 0, -S * 0.06, 1.15),
        pt(-spanHalf * 0.25, crownH * 0.62, S * 0.16, 0.95),
        pt(spanHalf * 0.28, crownH * 0.80, S * 0.22, 0.84),
        pt(spanHalf * 0.78, 0, S * 0.18, 1.15),
      ]);
      roles.push({ id: 'arch-secondary', label: 'Secondary Span', color: '#c084fc' });
    }

    return {
      family: 'arch',
      modeLabel: 'Arch Span Splines',
      canAddBranch: true,
      roles,
      defaultCurves,
    };
  }

  // ---------------------------------------------------------------------------
  // 3. SPIRES & MONOLITHS: Intuitive Spire Axis / Cluster Splines
  // ---------------------------------------------------------------------------
  if (archetype === 'desert-spires') {
    const spreadX = S * 0.32;
    const mainH = S * 0.88;
    const leftStem = [
      pt(-spreadX, 0, -S * 0.04, 1.12),
      pt(-spreadX * 0.96, mainH * 0.42, -S * 0.02, 0.84),
      pt(-spreadX * 0.92, mainH * 0.78, 0, 0.92),
    ];
    const centerStem = [
      pt(0, 0, S * 0.03, 1.20),
      pt(S * 0.01, mainH * 0.48, S * 0.02, 0.86),
      pt(S * 0.02, mainH, S * 0.01, 0.96),
    ];
    const rightStem = [
      pt(spreadX, 0, -S * 0.03, 1.10),
      pt(spreadX * 0.95, mainH * 0.38, -S * 0.01, 0.82),
      pt(spreadX * 0.90, mainH * 0.72, S * 0.01, 0.88),
    ];
    return {
      family: 'spire',
      modeLabel: 'Multi-Spire Axes',
      canAddBranch: false,
      roles: [
        { id: 'spire-left', label: 'Left Spire', color: '#ff8a5b' },
        { id: 'spire-main', label: 'Central Spire', color: '#ffd166' },
        { id: 'spire-right', label: 'Right Spire', color: '#5ce0a8' },
      ],
      defaultCurves: [leftStem, centerStem, rightStem],
    };
  }

  if (
    archetype === 'balanced-rock' ||
    archetype === 'alpine-crag' ||
    archetype === 'granite-tor' ||
    archetype === 'hero-boulder' ||
    archetype === 'cracked-monolith'
  ) {
    const topH =
      archetype === 'balanced-rock' ? S * 0.82
      : archetype === 'alpine-crag' ? S * 0.86
      : archetype === 'cracked-monolith' ? S * 0.76
      : archetype === 'granite-tor' ? S * 0.66
      : S * 0.56;
    const waistR = archetype === 'balanced-rock' ? 0.62 : 0.96;
    const capR = archetype === 'balanced-rock' ? 1.24 : 0.88;
    return {
      family: 'spire',
      modeLabel: 'Spire / Monolith Axis',
      canAddBranch: false,
      roles: [{ id: 'spire-axis', label: 'Spire / Core Axis', color: '#ffd166' }],
      defaultCurves: [[
        pt(0, 0, 0, 1.12),
        pt(0, topH * 0.46, 0, waistR),
        pt(0, topH, 0, capR),
      ]],
    };
  }

  // ---------------------------------------------------------------------------
  // 4. CLIFFS, HOGBACKS, YARDANGS, DOMES & BUTTES: Strike / Length Spine Spline
  // ---------------------------------------------------------------------------
  if (archetype === 'hoodoo-columns') {
    const finSpan = S * 0.96;
    const finH = S * 0.92;
    return {
      family: 'cliff',
      modeLabel: 'Hoodoo Amphitheater Fin Spine',
      canAddBranch: false,
      roles: [{ id: 'hoodoo-fin', label: 'Hoodoo Fin Spine', color: '#ff8a5b' }],
      defaultCurves: [[
        pt(-finSpan * 0.50, finH * 0.48, -S * 0.02, 0.92),
        pt(-finSpan * 0.24, finH * 0.82, S * 0.02, 1.00),
        pt(0, finH, 0, 1.05),
        pt(finSpan * 0.24, finH * 0.78, -S * 0.02, 1.00),
        pt(finSpan * 0.50, finH * 0.52, 0, 0.90),
      ]],
    };
  }

  if (archetype === 'saharan-yardang-fin') {
    // Wind-carved yardang spine runs from tall upwind prow (+Z) to tapering leeward tail (-Z)
    const lenZ = S * 1.18;
    const prowH = S * 0.62;
    return {
      family: 'cliff',
      modeLabel: 'Yardang Fin Spine (Prow → Tail)',
      canAddBranch: false,
      roles: [{ id: 'spine', label: 'Yardang Fin Spine', color: '#ffb84d' }],
      defaultCurves: [[
        pt(0, prowH * 0.96, lenZ * 0.44, 1.12),
        pt(-S * 0.015, prowH, lenZ * 0.16, 1.05),
        pt(S * 0.012, prowH * 0.74, -lenZ * 0.14, 0.86),
        pt(0, prowH * 0.34, -lenZ * 0.48, 0.58),
      ]],
    };
  }

  if (archetype === 'metamorphic-outcrop') {
    const span = S * 1.16;
    const crestH = S * 0.66;
    const strikeRad = ((preset?.params?.strikeAngle ?? -24) * Math.PI) / 180;
    const cs = Math.cos(strikeRad), ss = Math.sin(strikeRad);
    const stations = [-0.48, -0.16, 0.16, 0.48];
    const hWeights = [0.70, 0.98, 1.0, 0.74];
    const pts = stations.map((u, idx) => {
      const s = u * span;
      const bend = Math.sin(u * Math.PI * 1.4) * S * 0.045;
      return pt(s * cs - bend * ss, crestH * hWeights[idx], s * ss + bend * cs, 1.0);
    });
    return {
      family: 'cliff',
      modeLabel: 'Hogback Strike & Length Spine',
      canAddBranch: false,
      roles: [{ id: 'spine', label: 'Hogback Strike Spine', color: '#ffb84d' }],
      defaultCurves: [pts],
    };
  }

  if (archetype === 'exfoliation-dome') {
    const span = S * 1.02;
    const domeH = S * 0.74;
    return {
      family: 'cliff',
      modeLabel: 'Half-Dome Headwall & Massif Spine',
      canAddBranch: false,
      roles: [{ id: 'spine', label: 'Dome Headwall Spine', color: '#ffb84d' }],
      defaultCurves: [[
        pt(-span * 0.48, domeH * 0.56, S * 0.03, 0.88),
        pt(-span * 0.16, domeH * 0.95, -S * 0.02, 1.04),
        pt(span * 0.16, domeH, -S * 0.02, 1.04),
        pt(span * 0.48, domeH * 0.52, S * 0.03, 0.86),
      ]],
    };
  }

  // Broad stratified cliffs, Utah canyon wall, basalt cliff, mesa buttes & escarpments
  const cliffSpan =
    archetype === 'sedimentary-cliff' ? S * 1.38
    : archetype === 'utah-canyon' ? S * 1.44
    : archetype === 'cliff-wall' ? S * 1.32
    : S * 1.08;
  const cliffH =
    archetype === 'sedimentary-cliff' ? S * 0.72
    : archetype === 'utah-canyon' ? S * 0.76
    : archetype === 'grand-canyon-temple' ? S * 0.76
    : S * 0.68;
  const zBay = archetype === 'utah-canyon' ? -S * 0.035 : S * 0.03;

  return {
    family: 'cliff',
    modeLabel: 'Cliff Direction & Length Spine',
    canAddBranch: false,
    roles: [{ id: 'spine', label: 'Cliff / Ridge Spine', color: '#ffb84d' }],
    defaultCurves: [[
      pt(-cliffSpan * 0.48, cliffH * (archetype === 'sedimentary-cliff' ? 0.92 : 0.80), -zBay, 0.96),
      pt(-cliffSpan * 0.16, cliffH, zBay, 1.04),
      pt(cliffSpan * 0.16, cliffH * (archetype === 'sedimentary-cliff' ? 0.86 : 0.96), zBay * 0.7, 1.02),
      pt(cliffSpan * 0.48, cliffH * (archetype === 'sedimentary-cliff' ? 0.76 : 0.78), -zBay, 0.94),
    ]],
  };
}

/**
 * Checks whether `curves` matches `defaultCurves` within `tol` world units.
 * Supports both `(curves, defaultCurves, tol)` and `(archetype, scale, curves, tol)`.
 */
export function areSplinesAtDefault(arg1, arg2, arg3 = 1e-2, arg4 = 1e-2) {
  let curves = arg1;
  let defaultCurves = arg2;
  let tol = arg3;
  if (typeof arg1 === 'string') {
    const spec = getFormationSplineSpec(arg1, arg2);
    defaultCurves = spec.defaultCurves;
    curves = arg3;
    tol = Number.isFinite(Number(arg4)) ? Number(arg4) : 1e-2;
  }
  if (!Array.isArray(curves) || !curves.length) return true;
  if (!Array.isArray(defaultCurves) || curves.length !== defaultCurves.length) return false;
  for (let c = 0; c < curves.length; c++) {
    const a = curves[c];
    const b = defaultCurves[c];
    if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) {
      if (
        Math.abs((Number(a[i].x) || 0) - b[i].x) > tol ||
        Math.abs((Number(a[i].y) || 0) - b[i].y) > tol ||
        Math.abs((Number(a[i].z) || 0) - b[i].z) > tol ||
        Math.abs((Number(a[i].r ?? 1) || 1) - (b[i].r ?? 1)) > tol
      ) {
        return false;
      }
    }
  }
  return true;
}

/**
 * Compiles a dense arc-length lookup table for a 3D Catmull-Rom spine spline (`u in [0, 1]`).
 */
export function compileSplineArcTable(points, samples = 96) {
  const clean = Array.isArray(points) && points.length >= 2
    ? points
    : [{ x: -6, y: 8, z: 0, r: 1 }, { x: 6, y: 8, z: 0, r: 1 }];
  const N = Math.max(16, samples);
  const xs = new Float64Array(N + 1);
  const ys = new Float64Array(N + 1);
  const zs = new Float64Array(N + 1);
  const rs = new Float64Array(N + 1);
  const cumLen = new Float64Array(N + 1);
  const cumHoriz = new Float64Array(N + 1);

  for (let i = 0; i <= N; i++) {
    const st = evalCatmullRom3D(clean, i / N);
    xs[i] = st.x;
    ys[i] = st.y;
    zs[i] = st.z;
    rs[i] = Math.max(0.25, st.r ?? 1.0);
    if (i > 0) {
      const dx = xs[i] - xs[i - 1];
      const dy = ys[i] - ys[i - 1];
      const dz = zs[i] - zs[i - 1];
      cumLen[i] = cumLen[i - 1] + Math.hypot(dx, dy, dz);
      cumHoriz[i] = cumHoriz[i - 1] + Math.hypot(dx, dz);
    }
  }

  const totalLength = Math.max(1e-4, cumLen[N]);
  const totalHorizLength = Math.max(1e-4, cumHoriz[N]);

  function sampleAtArc(uInput) {
    const u = clamp(uInput, 0, 1);
    const target = u * totalHorizLength;
    let lo = 0, hi = N;
    while (lo + 1 < hi) {
      const mid = (lo + hi) >> 1;
      if (cumHoriz[mid] <= target) lo = mid;
      else hi = mid;
    }
    const segLen = Math.max(1e-8, cumHoriz[hi] - cumHoriz[lo]);
    const t = clamp((target - cumHoriz[lo]) / segLen, 0, 1);
    const x = xs[lo] + (xs[hi] - xs[lo]) * t;
    const y = ys[lo] + (ys[hi] - ys[lo]) * t;
    const z = zs[lo] + (zs[hi] - zs[lo]) * t;
    const r = rs[lo] + (rs[hi] - rs[lo]) * t;

    const i0 = Math.max(0, lo - 1);
    const i1 = Math.min(N, hi + 1);
    let tx = xs[i1] - xs[i0];
    let tz = zs[i1] - zs[i0];
    const tl = Math.hypot(tx, tz);
    if (tl > 1e-6) {
      tx /= tl;
      tz /= tl;
    } else {
      tx = 1;
      tz = 0;
    }
    // Right-handed outward front normal when spine runs -X -> +X is (nx = -tz, nz = tx) -> (0, +1) = +Z!
    const nx = -tz;
    const nz = tx;
    return { u, x, y, z, r, tx, tz, nx, nz };
  }

  return {
    N,
    xs, ys, zs, rs,
    totalLength,
    totalHorizLength,
    sampleAtArc,
  };
}

/**
 * Compiles synchronized 3-spline Canyon/Gorge stations (`[Left Wall, Centre Channel, Right Wall]`).
 */
export function compileCanyonTripletTable(curves, scale = 16, samples = 96) {
  const fallback = getFormationSplineSpec('slot-canyon', scale).defaultCurves;
  const leftPts = Array.isArray(curves?.[0]) && curves[0].length >= 2 ? curves[0] : fallback[0];
  const centerPts = Array.isArray(curves?.[1]) && curves[1].length >= 2 ? curves[1] : fallback[1];
  const rightPts = Array.isArray(curves?.[2]) && curves[2].length >= 2 ? curves[2] : fallback[2];

  const leftTab = compileSplineArcTable(leftPts, samples);
  const centerTab = compileSplineArcTable(centerPts, samples);
  const rightTab = compileSplineArcTable(rightPts, samples);

  function stationAt(uInput) {
    const u = clamp(uInput, 0, 1);
    const c = centerTab.sampleAtArc(u);
    const l = leftTab.sampleAtArc(u);
    const r = rightTab.sampleAtArc(u);

    // Tangent along downstream canyon direction; rightVec points from Left Wall toward Right Wall
    let tx = c.tx, tz = c.tz;
    let rx = r.x - l.x, rz = r.z - l.z;
    const rl = Math.hypot(rx, rz);
    if (rl > 1e-5) {
      rx /= rl;
      rz /= rl;
      // Keep downstream tangent consistent with right-hand cross product
      if (-tz * rx + tx * rz < 0) {
        // rightVec is on +X when tangent is +Z: (-1)*1 + 0 = -1; use perpendicular pointing toward r-l
      }
    } else {
      rx = tz;
      rz = -tx;
    }
    // Unit cross-canyon axis pointing from center toward Right Wall
    let acrossX = r.x - l.x;
    let acrossZ = r.z - l.z;
    // Project perpendicular to center tangent for clean orthogonal wall cross-sections
    const dotT = acrossX * tx + acrossZ * tz;
    acrossX -= tx * dotT;
    acrossZ -= tz * dotT;
    const al = Math.hypot(acrossX, acrossZ);
    if (al > 1e-5) {
      acrossX /= al;
      acrossZ /= al;
    } else {
      acrossX = -tz;
      acrossZ = tx;
    }

    const distL = Math.max(scale * 0.038, -((l.x - c.x) * acrossX + (l.z - c.z) * acrossZ));
    const distR = Math.max(scale * 0.038, (r.x - c.x) * acrossX + (r.z - c.z) * acrossZ);

    return {
      u,
      center: c,
      left: l,
      right: r,
      tx,
      tz,
      acrossX,
      acrossZ,
      halfGapL: distL,
      halfGapR: distR,
      halfGapMean: 0.5 * (distL + distR),
      crestL: Math.max(scale * 0.16, l.y),
      crestR: Math.max(scale * 0.16, r.y),
      floorY: Math.max(scale * 0.008, c.y),
      thickL: clamp(l.r, 0.35, 2.5),
      thickR: clamp(r.r, 0.35, 2.5),
    };
  }

  return {
    totalLength: centerTab.totalHorizLength,
    leftTab,
    centerTab,
    rightTab,
    stationAt,
  };
}

/**
 * Applies a local 3D similarity/frame transform `x' = newCenter + scale * R * (x - oldCenter)`
 * (with optional independent vertical scale `scaleY`) to a compiled polyhedron or ellipsoid block
 * while keeping ground-clip planes (`n ~ [0, -1, 0]`) pinned flush to `groundY = 0`.
 */
function transformCompiledBlock(block, oldC, newC, rot2D, scaleXZ, scaleY, groundY = 0) {
  const cosA = rot2D[0], sinA = rot2D[1];
  const sxz = clamp(scaleXZ, 0.30, 3.5);
  const sy = clamp(scaleY, 0.30, 3.5);

  const mapPoint = (px, py, pz) => {
    const dx = (px - oldC[0]) * sxz;
    const dy = (py - oldC[1]) * sy;
    const dz = (pz - oldC[2]) * sxz;
    return [
      newC[0] + dx * cosA - dz * sinA,
      Math.max(groundY, newC[1] + dy),
      newC[2] + dx * sinA + dz * cosA,
    ];
  };

  const mapNormal = (nx, ny, nz) => {
    // Inverse-transpose of diag(sxz, sy, sxz) * R_y:
    const rx = (nx * cosA - nz * sinA) / sxz;
    const ry = ny / sy;
    const rz = (nx * sinA + nz * cosA) / sxz;
    return vnorm([rx, ry, rz]);
  };

  const nc = mapPoint(block.cx, block.cy, block.cz);
  const f0 = mapNormal(block.fx0, block.fx1, block.fx2);
  const f1 = mapNormal(block.fy0, block.fy1, block.fy2);
  const f2 = mapNormal(block.fz0, block.fz1, block.fz2);

  // Conservative transformed AABB around all 8 corners of original AABB
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  for (const bx of [block.minX, block.maxX]) {
    for (const by of [block.minY, block.maxY]) {
      for (const bz of [block.minZ, block.maxZ]) {
        const q = mapPoint(bx, by, bz);
        if (q[0] < minX) minX = q[0];
        if (q[1] < minY) minY = q[1];
        if (q[2] < minZ) minZ = q[2];
        if (q[0] > maxX) maxX = q[0];
        if (q[1] > maxY) maxY = q[1];
        if (q[2] > maxZ) maxZ = q[2];
      }
    }
  }
  minY = Math.max(groundY, minY);

  if (block.primitiveType === 1) {
    const e0 = mapNormal(block.ex0, block.ex1, block.ex2);
    const e1 = mapNormal(block.ey0, block.ey1, block.ey2);
    const e2 = mapNormal(block.ez0, block.ez1, block.ez2);
    const rx = block.rx * sxz;
    const ry = block.ry * sy;
    const rz = block.rz * sxz;
    const maxR = Math.max(rx, ry, rz);
    block.cx = nc[0]; block.cy = nc[1]; block.cz = nc[2];
    block.rx = rx; block.ry = ry; block.rz = rz;
    block.radius = maxR;
    block.minX = nc[0] - maxR; block.minY = Math.max(groundY, nc[1] - maxR); block.minZ = nc[2] - maxR;
    block.maxX = nc[0] + maxR; block.maxY = nc[1] + maxR; block.maxZ = nc[2] + maxR;
    block.ex0 = e0[0]; block.ex1 = e0[1]; block.ex2 = e0[2];
    block.ey0 = e1[0]; block.ey1 = e1[1]; block.ey2 = e1[2];
    block.ez0 = e2[0]; block.ez1 = e2[1]; block.ez2 = e2[2];
  } else if (block.planes) {
    const p = block.planes;
    for (let i = 0; i < block.numPlanes; i++) {
      const ox = p[i * 4], oy = p[i * 4 + 1], oz = p[i * 4 + 2], off = p[i * 4 + 3];
      // Preserve flat Y=groundY base clip plane
      if (oy < -0.992 && Math.abs(ox) < 0.08 && Math.abs(oz) < 0.08 && Math.abs(off + groundY) < 0.15) {
        p[i * 4] = 0;
        p[i * 4 + 1] = -1;
        p[i * 4 + 2] = 0;
        p[i * 4 + 3] = -groundY;
        continue;
      }
      // Point on original plane: x0 = oldC + (off - n . oldC) * n
      const distFromOldC = off - (ox * oldC[0] + oy * oldC[1] + oz * oldC[2]);
      const px0 = oldC[0] + ox * distFromOldC;
      const py0 = oldC[1] + oy * distFromOldC;
      const pz0 = oldC[2] + oz * distFromOldC;
      const q0 = [
        newC[0] + ((px0 - oldC[0]) * sxz) * cosA - ((pz0 - oldC[2]) * sxz) * sinA,
        newC[1] + (py0 - oldC[1]) * sy,
        newC[2] + ((px0 - oldC[0]) * sxz) * sinA + ((pz0 - oldC[2]) * sxz) * cosA,
      ];
      const nn = mapNormal(ox, oy, oz);
      p[i * 4] = nn[0];
      p[i * 4 + 1] = nn[1];
      p[i * 4 + 2] = nn[2];
      p[i * 4 + 3] = nn[0] * q0[0] + nn[1] * q0[1] + nn[2] * q0[2];
    }
    block.cx = nc[0]; block.cy = nc[1]; block.cz = nc[2];
    block.radius *= Math.max(sxz, sy);
    block.minX = minX; block.minY = minY; block.maxX = maxX; block.maxY = maxY;
    block.minZ = minZ; block.maxZ = maxZ;
  }

  block.fx0 = f0[0]; block.fx1 = f0[1]; block.fx2 = f0[2];
  block.fy0 = f1[0]; block.fy1 = f1[1]; block.fy2 = f1[2];
  block.fz0 = f2[0]; block.fz1 = f2[1]; block.fz2 = f2[2];
}

/**
 * Smoothly warps any compiled macro formation (`blocks`, `cuts`, `curves`) from its rest spline rig
 * onto user-edited `customCurves` (used for presets that do not already consume `formationSplines`
 * directly inside their builder).
 */
export function deformMacroFormationBySplines(macro, archetype, scale, customCurves) {
  if (!macro || !Array.isArray(customCurves) || !customCurves.length) return macro;
  const spec = getFormationSplineSpec(archetype, scale);
  if (areSplinesAtDefault(customCurves, spec.defaultCurves)) return macro;

  const groundY = macro.groundY ?? 0;

  if (spec.family === 'spire') {
    const restStems = spec.defaultCurves;
    const curStems = customCurves;
    const stemCount = Math.min(restStems.length, curStems.length);

    const warpList = (list) => {
      if (!Array.isArray(list)) return;
      for (const blk of list) {
        // Assign block to nearest rest spire stem in XZ
        let bestIdx = 0;
        let bestDist = Infinity;
        for (let s = 0; s < stemCount; s++) {
          const b0 = restStems[s][0];
          const d = Math.hypot(blk.cx - b0.x, blk.cz - b0.z);
          if (d < bestDist) { bestDist = d; bestIdx = s; }
        }
        const restPts = restStems[bestIdx];
        const curPts = curStems[bestIdx];
        if (!curPts || curPts.length < 2) continue;
        const restTopY = Math.max(0.5, restPts[restPts.length - 1].y);
        const curTopY = Math.max(0.5, curPts[curPts.length - 1].y);
        const u = clamp((blk.cy - groundY) / restTopY, 0, 1);
        const rSt = evalCatmullRom3D(restPts, u);
        const cSt = evalCatmullRom3D(curPts, u);
        const scaleY = curTopY / restTopY;
        const scaleXZ = clamp((cSt.r ?? 1) / Math.max(0.25, rSt.r ?? 1), 0.35, 2.8);
        transformCompiledBlock(
          blk,
          [rSt.x, groundY + u * restTopY, rSt.z],
          [cSt.x, groundY + u * curTopY, cSt.z],
          [1, 0],
          scaleXZ,
          scaleY,
          groundY
        );
      }
    };
    warpList(macro.blocks);
    warpList(macro.cuts);
    return macro;
  }

  // Spine-based warp (`cliff` family fallback) with arc-length adaptive bay population!
  // When a spline is stretched significantly longer (`lengthRatio > 1.35`), populate additional
  // mirrored/decorrelated geological bays along the spine so features multiply naturally instead of stretching thin!
  const restTab = compileSplineArcTable(spec.defaultCurves[0], 96);
  const curTab = compileSplineArcTable(customCurves[0], 96);
  const lengthRatio = clamp(curTab.totalHorizLength / Math.max(0.5, restTab.totalHorizLength), 0.35, 5.0);
  const nBays = lengthRatio > 1.38 ? Math.min(5, Math.max(2, Math.round(lengthRatio))) : 1;
  const localSpanRatio = clamp(lengthRatio / nBays, 0.55, 1.45);

  const projectU = (wx, wz) => {
    let bestI = 0;
    let bestD2 = Infinity;
    for (let i = 0; i <= restTab.N; i++) {
      const dx = wx - restTab.xs[i];
      const dz = wz - restTab.zs[i];
      const d2 = dx * dx + dz * dz;
      if (d2 < bestD2) { bestD2 = d2; bestI = i; }
    }
    return bestI / restTab.N;
  };

  const cloneCompiledBlock = (blk, saltAdd) => ({
    ...blk,
    planes: blk.planes ? new Float32Array(blk.planes) : null,
    seedSalt: ((blk.seedSalt || 1) + saltAdd) & 0x7fffffff,
  });

  const warpSpineList = (list, populateBays = true) => {
    if (!Array.isArray(list) || !list.length) return;
    const origBlocks = populateBays && nBays > 1 ? list.map((b) => cloneCompiledBlock(b, 0)) : null;

    const mapOneBlock = (blk, uSrc, uDst) => {
      const rS = restTab.sampleAtArc(uSrc);
      const cS = curTab.sampleAtArc(uDst);
      const cosA = rS.tx * cS.tx + rS.tz * cS.tz;
      const sinA = rS.tx * cS.tz - rS.tz * cS.tx;
      const scaleY = clamp(cS.y / Math.max(0.5, rS.y), 0.35, 2.8);
      const scaleThick = clamp(cS.r / Math.max(0.25, rS.r), 0.40, 2.6);
      const latOff = (blk.cx - rS.x) * rS.nx + (blk.cz - rS.z) * rS.nz;
      const newCx = cS.x + cS.nx * latOff * scaleThick;
      const newCz = cS.z + cS.nz * latOff * scaleThick;
      const newCy = groundY + (blk.cy - groundY) * scaleY;
      transformCompiledBlock(
        blk,
        [blk.cx, blk.cy, blk.cz],
        [newCx, newCy, newCz],
        [cosA, sinA],
        0.5 * (localSpanRatio + scaleThick),
        scaleY,
        groundY
      );
    };

    for (const blk of list) {
      const u = projectU(blk.cx, blk.cz);
      const uDst = populateBays && nBays > 1 ? u / nBays : u;
      mapOneBlock(blk, u, uDst);
    }

    if (origBlocks && nBays > 1) {
      for (let bay = 1; bay < nBays; bay++) {
        for (const srcBlk of origBlocks) {
          const copyBlk = cloneCompiledBlock(srcBlk, bay * 7919);
          const uOrig = projectU(copyBlk.cx, copyBlk.cz);
          const uLocal = bay % 2 === 1 ? 1.0 - uOrig : uOrig;
          const uDst = (bay + uLocal) / nBays;
          mapOneBlock(copyBlk, uOrig, uDst);
          list.push(copyBlk);
        }
      }
    }
  };

  warpSpineList(macro.blocks, true);
  warpSpineList(macro.cuts, true);
  return macro;
}
