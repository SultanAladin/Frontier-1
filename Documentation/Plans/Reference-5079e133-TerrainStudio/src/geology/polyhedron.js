/**
 * Exact 3D Convex Polyhedron Clipper, Recursive Geological BSP Fracture Splitter,
 * and Fast Compiled Half-Space Rock Block Compiler.
 */

import { vdot, vcross, vnorm, rotateAroundAxis, perturbNormal } from '../core/vec3.js';

const EPS = 1e-5;

export function createBoxPoly(minX, minY, minZ, maxX, maxY, maxZ, tag = 'exterior') {
  const v000 = [minX, minY, minZ];
  const v100 = [maxX, minY, minZ];
  const v110 = [maxX, maxY, minZ];
  const v010 = [minX, maxY, minZ];
  const v001 = [minX, minY, maxZ];
  const v101 = [maxX, minY, maxZ];
  const v111 = [maxX, maxY, maxZ];
  const v011 = [minX, maxY, maxZ];

  return {
    faces: [
      { normal: [1, 0, 0],  offset: maxX,  verts: [v100, v110, v111, v101], tag },
      { normal: [-1, 0, 0], offset: -minX, verts: [v001, v011, v010, v000], tag },
      { normal: [0, 1, 0],  offset: maxY,  verts: [v010, v011, v111, v110], tag },
      { normal: [0, -1, 0], offset: -minY, verts: [v000, v100, v101, v001], tag },
      { normal: [0, 0, 1],  offset: maxZ,  verts: [v001, v101, v111, v011], tag },
      { normal: [0, 0, -1], offset: -minZ, verts: [v100, v000, v010, v110], tag },
    ],
  };
}

function polygonArea3D(verts) {
  if (verts.length < 3) return 0;
  const ax = verts[0][0], ay = verts[0][1], az = verts[0][2];
  let cx = 0, cy = 0, cz = 0;
  for (let i = 1; i < verts.length - 1; i++) {
    const ux = verts[i][0] - ax, uy = verts[i][1] - ay, uz = verts[i][2] - az;
    const vx = verts[i + 1][0] - ax, vy = verts[i + 1][1] - ay, vz = verts[i + 1][2] - az;
    cx += uy * vz - uz * vy;
    cy += uz * vx - ux * vz;
    cz += ux * vy - uy * vx;
  }
  return 0.5 * Math.hypot(cx, cy, cz);
}

/**
 * Clips convex polyhedron `poly` against half-space `dot(n, p) - offset <= 0`.
 * Returns a new polyhedron, or `null` if completely clipped away.
 */
export function clipPolyByHalfSpace(poly, rawNormal, offset, tag = 'fracture') {
  if (!poly || !poly.faces || poly.faces.length < 4) return null;
  const n = vnorm(rawNormal);
  const nx = n[0], ny = n[1], nz = n[2];

  const keptFaces = [];
  const capCandidates = [];
  let anyClipped = false;

  for (let fi = 0; fi < poly.faces.length; fi++) {
    const f = poly.faces[fi];
    const verts = f.verts;
    const m = verts.length;
    const outVerts = [];

    for (let i = 0; i < m; i++) {
      const a = verts[i];
      const b = verts[(i + 1) % m];
      const da = nx * a[0] + ny * a[1] + nz * a[2] - offset;
      const db = nx * b[0] + ny * b[1] + nz * b[2] - offset;

      if (da > EPS) anyClipped = true;

      if (da <= EPS) {
        outVerts.push(a);
        if (da >= -EPS) capCandidates.push(a);
      }

      if ((da < -EPS && db > EPS) || (da > EPS && db < -EPS)) {
        const t = da / (da - db);
        const ix = a[0] + (b[0] - a[0]) * t;
        const iy = a[1] + (b[1] - a[1]) * t;
        const iz = a[2] + (b[2] - a[2]) * t;
        const pt = [ix, iy, iz];
        outVerts.push(pt);
        capCandidates.push(pt);
      }
    }

    // Deduplicate consecutive near-identical vertices
    const clean = [];
    for (let i = 0; i < outVerts.length; i++) {
      const p = outVerts[i];
      const prev = clean.length > 0 ? clean[clean.length - 1] : outVerts[outVerts.length - 1];
      const d2 = (p[0] - prev[0]) ** 2 + (p[1] - prev[1]) ** 2 + (p[2] - prev[2]) ** 2;
      if (d2 > 1e-8) clean.push(p);
    }

    if (clean.length >= 3 && polygonArea3D(clean) > 1e-6) {
      keptFaces.push({
        normal: f.normal,
        offset: f.offset,
        verts: clean,
        tag: f.tag,
      });
    }
  }

  if (!anyClipped) return poly;
  if (keptFaces.length < 3) return null;

  // Deduplicate cap vertices and order CCW around planeNormal
  const uniqueCap = [];
  for (let i = 0; i < capCandidates.length; i++) {
    const p = capCandidates[i];
    let dup = false;
    for (let j = 0; j < uniqueCap.length; j++) {
      const q = uniqueCap[j];
      if ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2 < 1e-6) {
        dup = true;
        break;
      }
    }
    if (!dup) uniqueCap.push(p);
  }

  if (uniqueCap.length >= 3) {
    let cx = 0, cy = 0, cz = 0;
    for (let i = 0; i < uniqueCap.length; i++) {
      cx += uniqueCap[i][0];
      cy += uniqueCap[i][1];
      cz += uniqueCap[i][2];
    }
    cx /= uniqueCap.length;
    cy /= uniqueCap.length;
    cz /= uniqueCap.length;

    const arb = Math.abs(ny) < 0.9 ? [0, 1, 0] : [1, 0, 0];
    const t1 = vnorm(vcross(arb, n));
    const t2 = vcross(n, t1);

    const withAngles = uniqueCap.map((p) => {
      const rx = p[0] - cx, ry = p[1] - cy, rz = p[2] - cz;
      const u = rx * t1[0] + ry * t1[1] + rz * t1[2];
      const w = rx * t2[0] + ry * t2[1] + rz * t2[2];
      return { p, ang: Math.atan2(w, u) };
    });
    withAngles.sort((a, b) => a.ang - b.ang);
    const capVerts = withAngles.map((x) => x.p);

    if (polygonArea3D(capVerts) > 1e-6) {
      keptFaces.push({
        normal: [nx, ny, nz],
        offset,
        verts: capVerts,
        tag,
      });
    }
  }

  if (keptFaces.length < 4) return null;
  return { faces: keptFaces };
}

/**
 * Splits a convex polyhedron across plane `dot(n, p) = offset` with optional frost-wedge gap `gap >= 0`.
 * Returns [negChild, posChild].
 */
export function splitPolyByPlane(poly, rawNormal, offset, gap = 0, tag = 'joint') {
  const n = vnorm(rawNormal);
  const halfGap = gap * 0.5;
  const neg = clipPolyByHalfSpace(poly, n, offset - halfGap, tag);
  const pos = clipPolyByHalfSpace(poly, [-n[0], -n[1], -n[2]], -(offset + halfGap), tag);
  return [neg, pos];
}

/**
 * Computes unique vertices, centroid, AABB, bounding sphere radius, and approximate volume of `poly`.
 */
export function measurePoly(poly) {
  const verts = [];
  for (let fi = 0; fi < poly.faces.length; fi++) {
    const fv = poly.faces[fi].verts;
    for (let i = 0; i < fv.length; i++) {
      const p = fv[i];
      let dup = false;
      for (let j = 0; j < verts.length; j++) {
        const q = verts[j];
        if ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2 < 1e-5) {
          dup = true;
          break;
        }
      }
      if (!dup) verts.push(p);
    }
  }

  if (verts.length < 4) return null;

  let cx = 0, cy = 0, cz = 0;
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;

  for (let i = 0; i < verts.length; i++) {
    const [x, y, z] = verts[i];
    cx += x; cy += y; cz += z;
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (y < minY) minY = y; if (y > maxY) maxY = y;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  }
  cx /= verts.length;
  cy /= verts.length;
  cz /= verts.length;

  let maxR2 = 0;
  for (let i = 0; i < verts.length; i++) {
    const dx = verts[i][0] - cx, dy = verts[i][1] - cy, dz = verts[i][2] - cz;
    const r2 = dx * dx + dy * dy + dz * dz;
    if (r2 > maxR2) maxR2 = r2;
  }

  // Exact divergence-theorem polyhedron volume around centroid
  let vol = 0;
  for (let fi = 0; fi < poly.faces.length; fi++) {
    const f = poly.faces[fi];
    const area = polygonArea3D(f.verts);
    const h = f.offset - (f.normal[0] * cx + f.normal[1] * cy + f.normal[2] * cz);
    vol += (area * Math.max(0, h)) / 3;
  }

  return {
    verts,
    centroid: [cx, cy, cz],
    radius: Math.sqrt(maxR2),
    minX, minY, minZ,
    maxX, maxY, maxZ,
    spanX: maxX - minX,
    spanY: maxY - minY,
    spanZ: maxZ - minZ,
    minSpan: Math.min(maxX - minX, maxY - minY, maxZ - minZ),
    volume: vol,
  };
}

export function transformPoly(poly, deltaPos = [0, 0, 0], rotAxis = [0, 1, 0], rotAngle = 0, pivot = null) {
  const m = pivot ? { centroid: pivot } : measurePoly(poly);
  if (!m) return poly;
  const [px, py, pz] = m.centroid;
  const [dx, dy, dz] = deltaPos;
  const doRot = Math.abs(rotAngle) > 1e-6;
  const axis = doRot ? vnorm(rotAxis) : [0, 1, 0];

  const newFaces = poly.faces.map((f) => {
    const nn = doRot ? rotateAroundAxis(f.normal, axis, rotAngle) : [f.normal[0], f.normal[1], f.normal[2]];
    const nv = f.verts.map((v) => {
      let rx = v[0] - px, ry = v[1] - py, rz = v[2] - pz;
      if (doRot) {
        [rx, ry, rz] = rotateAroundAxis([rx, ry, rz], axis, rotAngle);
      }
      return [rx + px + dx, ry + py + dy, rz + pz + dz];
    });
    const newOffset = nn[0] * nv[0][0] + nn[1] * nv[0][1] + nn[2] * nv[0][2];
    return {
      normal: nn,
      offset: newOffset,
      verts: nv,
      tag: f.tag,
    };
  });

  return { faces: newFaces };
}

/**
 * Sculpts an organic, stress-aligned parent rock polyhedron (not a box, not a noise sphere!)
 * using conjugate structural joint planes + basal cleavage + corner-blunting bevels.
 */
export function sculptGeologicalParentPoly(center, radii, frame, rng, options = {}) {
  const [cx, cy, cz] = center;
  const [rx, ry, rz] = radii;
  const dispersion = options.jointDispersion ?? 0.22;
  const cornerBlunt = options.cornerBlunt ?? 0.78;
  const flatBaseY = options.flatBaseY ?? null;

  const bound = Math.max(rx, ry, rz) * 2.2;
  let poly = createBoxPoly(
    cx - bound, flatBaseY !== null ? flatBaseY : cy - bound, cz - bound,
    cx + bound, cy + bound, cz + bound,
    'bound'
  );

  const { strikeVec, dipVec, normal: bedNormal } = frame;
  // Orthogonal joint direction perpendicular to strike & bedNormal (= dipVec)
  // Conjugate Andersonian shear joints (+/- ~58 deg around bedNormal)
  const conj1 = rotateAroundAxis(dipVec, bedNormal, 0.98 + rng.signed() * 0.16);
  const conj2 = rotateAroundAxis(dipVec, bedNormal, -0.98 + rng.signed() * 0.16);

  // Primary structural half-space pairs (top/bottom bedding, front/back dip-joints, conjugate side joints)
  const dirSpecs = [
    { dir: bedNormal, dist: ry * rng.range(0.82, 1.12), disp: dispersion * 0.7, tag: 'bedding' },
    { dir: [-bedNormal[0], -bedNormal[1], -bedNormal[2]], dist: ry * rng.range(0.85, 1.15), disp: dispersion * 0.7, tag: 'bedding' },
    { dir: dipVec, dist: rz * rng.range(0.78, 1.15), disp: dispersion, tag: 'joint1' },
    { dir: [-dipVec[0], -dipVec[1], -dipVec[2]], dist: rz * rng.range(0.78, 1.15), disp: dispersion, tag: 'joint1' },
    { dir: strikeVec, dist: rx * rng.range(0.80, 1.16), disp: dispersion, tag: 'strike' },
    { dir: [-strikeVec[0], -strikeVec[1], -strikeVec[2]], dist: rx * rng.range(0.80, 1.16), disp: dispersion, tag: 'strike' },
    { dir: conj1, dist: Math.hypot(rx, rz) * rng.range(0.58, 0.78), disp: dispersion * 1.1, tag: 'conjugate' },
    { dir: [-conj1[0], -conj1[1], -conj1[2]], dist: Math.hypot(rx, rz) * rng.range(0.58, 0.78), disp: dispersion * 1.1, tag: 'conjugate' },
    { dir: conj2, dist: Math.hypot(rx, rz) * rng.range(0.58, 0.80), disp: dispersion * 1.1, tag: 'conjugate' },
    { dir: [-conj2[0], -conj2[1], -conj2[2]], dist: Math.hypot(rx, rz) * rng.range(0.58, 0.80), disp: dispersion * 1.1, tag: 'conjugate' },
  ];

  // Add 3-5 oblique crown/shoulder spall bevel planes for asymmetric organic silhouettes
  const extraCuts = options.extraBevelCuts ?? 4;
  for (let i = 0; i < extraCuts; i++) {
    const phi = rng.float() * Math.PI * 2;
    const tilt = rng.range(0.25, 1.15);
    const horiz = [
      strikeVec[0] * Math.cos(phi) + dipVec[0] * Math.sin(phi),
      strikeVec[1] * Math.cos(phi) + dipVec[1] * Math.sin(phi),
      strikeVec[2] * Math.cos(phi) + dipVec[2] * Math.sin(phi),
    ];
    const sgn = rng.chance(0.72) ? 1 : -1;
    const d = vnorm([
      horiz[0] * Math.sin(tilt) + bedNormal[0] * sgn * Math.cos(tilt),
      horiz[1] * Math.sin(tilt) + bedNormal[1] * sgn * Math.cos(tilt),
      horiz[2] * Math.sin(tilt) + bedNormal[2] * sgn * Math.cos(tilt),
    ]);
    const effR = Math.hypot(
      rx * vdot(d, strikeVec),
      ry * vdot(d, bedNormal),
      rz * vdot(d, dipVec)
    );
    dirSpecs.push({ dir: d, dist: effR * rng.range(0.84, 1.08), disp: dispersion * 0.8, tag: 'spall' });
  }

  for (let i = 0; i < dirSpecs.length; i++) {
    const sp = dirSpecs[i];
    if (flatBaseY !== null && sp.dir[1] < -0.75) continue;
    const n = perturbNormal(sp.dir, rng, sp.disp);
    const off = n[0] * cx + n[1] * cy + n[2] * cz + sp.dist;
    const next = clipPolyByHalfSpace(poly, n, off, sp.tag);
    if (next) poly = next;
  }

  // Blunt any overly acute protruding needle vertices (real brittle stone snaps acute tips!)
  const info = measurePoly(poly);
  if (info && cornerBlunt > 0) {
    const [mcx, mcy, mcz] = info.centroid;
    const maxAllowed = Math.max(rx, ry, rz) * (1.15 + (1 - cornerBlunt) * 0.45);
    for (let i = 0; i < info.verts.length; i++) {
      const v = info.verts[i];
      if (flatBaseY !== null && v[1] <= flatBaseY + 0.05) continue;
      const dx = v[0] - mcx, dy = v[1] - mcy, dz = v[2] - mcz;
      const dist = Math.hypot(dx, dy, dz);
      if (dist > maxAllowed) {
        const dir = perturbNormal([dx / dist, dy / dist, dz / dist], rng, 0.14);
        const clipD = maxAllowed * rng.range(0.88, 0.97);
        const off = dir[0] * mcx + dir[1] * mcy + dir[2] * mcz + clipD;
        const next = clipPolyByHalfSpace(poly, dir, off, 'chisel');
        if (next) poly = next;
      }
    }
  }

  return poly;
}

/**
 * Compiles a ConvexRockPoly into a flat, zero-allocation typed representation
 * for fast inner-loop brick-culled SDF evaluation.
 */
export function compilePolyBlock(poly, frame, options = {}) {
  const m = measurePoly(poly);
  if (!m || m.volume < 1e-4 || m.minSpan < 0.04) return null;

  const numPlanes = poly.faces.length;
  const planes = new Float32Array(numPlanes * 4);
  for (let i = 0; i < numPlanes; i++) {
    const f = poly.faces[i];
    planes[i * 4] = f.normal[0];
    planes[i * 4 + 1] = f.normal[1];
    planes[i * 4 + 2] = f.normal[2];
    planes[i * 4 + 3] = f.offset;
  }

  return {
    cx: m.centroid[0],
    cy: m.centroid[1],
    cz: m.centroid[2],
    radius: m.radius,
    minX: m.minX, minY: m.minY, minZ: m.minZ,
    maxX: m.maxX, maxY: m.maxY, maxZ: m.maxZ,
    volume: m.volume,
    planes,
    numPlanes,
    edgeBevel: options.edgeBevel ?? 0.035,
    smoothUnionR: options.smoothUnionR ?? 0,
    detailScale: options.detailScale ?? 1.0,
    hardness: options.hardness ?? (options.isTalus ? 0.22 : 0.72),
    noCracks: !!options.noCracks || !!options.isTalus,
    noSeamNotch: !!options.noSeamNotch,
    isTalus: !!options.isTalus,
    isBoulder: !!options.isBoulder,
    isEolian: !!options.isEolian,
    isClast: !!options.isClast,
    primitiveType: 0, // 0 = half-space polyhedron, 1 = oriented eolian ellipsoid
    seedSalt: options.seedSalt ?? 0,
    // Orthonormal local rock fabric axes for anisotropic planar cleavage / cross-bedding
    fx0: frame.strikeVec[0], fx1: frame.strikeVec[1], fx2: frame.strikeVec[2],
    fy0: frame.normal[0],    fy1: frame.normal[1],    fy2: frame.normal[2],
    fz0: frame.dipVec[0],    fz1: frame.dipVec[1],    fz2: frame.dipVec[2],
  };
}

/**
 * Compiles a smooth wind-sculpted 3D oriented ellipsoid primitive (for Utah Navajo Sandstone
 * arches, slickrock domes, blind alcoves, and hoodoo drums).
 */
export function compileEllipsoidBlock(center, radii, frame, options = {}) {
  const [cx, cy, cz] = center;
  const [rx, ry, rz] = radii;
  const maxR = Math.max(rx, ry, rz);

  // Orthonormal axes of the ellipsoid (defaults to frame axes or custom basis)
  const ax = options.axisX || frame.strikeVec;
  const ay = options.axisY || frame.normal;
  const az = options.axisZ || frame.dipVec;

  return {
    cx, cy, cz,
    radius: maxR,
    minX: cx - maxR, minY: cy - maxR, minZ: cz - maxR,
    maxX: cx + maxR, maxY: cy + maxR, maxZ: cz + maxR,
    rx: Math.max(0.02, rx),
    ry: Math.max(0.02, ry),
    rz: Math.max(0.02, rz),
    ex0: ax[0], ex1: ax[1], ex2: ax[2],
    ey0: ay[0], ey1: ay[1], ey2: ay[2],
    ez0: az[0], ez1: az[1], ez2: az[2],
    edgeBevel: options.edgeBevel ?? 0.02,
    smoothUnionR: options.smoothUnionR ?? 0,
    detailScale: options.detailScale ?? 1.0,
    hardness: options.hardness ?? (options.isTalus ? 0.22 : 0.65),
    noCracks: !!options.noCracks || !!options.isTalus,
    isTalus: !!options.isTalus,
    isBoulder: !!options.isBoulder,
    isEolian: options.isEolian ?? true,
    isClast: !!options.isClast,
    primitiveType: 1,
    seedSalt: options.seedSalt ?? 0,
    fx0: frame.strikeVec[0], fx1: frame.strikeVec[1], fx2: frame.strikeVec[2],
    fy0: frame.normal[0],    fy1: frame.normal[1],    fy2: frame.normal[2],
    fz0: frame.dipVec[0],    fz1: frame.dipVec[1],    fz2: frame.dipVec[2],
  };
}

export let lastSecondPlaneDist = -1e9;

/**
 * Evaluates exact chamfer-beveled half-space polyhedron SDF at (wx, wy, wz).
 */
export function evalCompiledBlock(block, wx, wy, wz) {
  if (block.primitiveType === 1) {
    const dx = wx - block.cx, dy = wy - block.cy, dz = wz - block.cz;
    const lx = dx * block.ex0 + dy * block.ex1 + dz * block.ex2;
    const ly = dx * block.ey0 + dy * block.ey1 + dz * block.ey2;
    const lz = dx * block.ez0 + dy * block.ez1 + dz * block.ez2;

    const qx = lx / block.rx, qy = ly / block.ry, qz = lz / block.rz;
    const k0 = Math.hypot(qx, qy, qz);
    if (k0 < 1e-8) {
      lastSecondPlaneDist = -Math.min(block.rx, block.ry, block.rz);
      return -Math.min(block.rx, block.ry, block.rz);
    }
    const k1 = Math.hypot(qx / block.rx, qy / block.ry, qz / block.rz);
    lastSecondPlaneDist = -0.5 * block.ry;
    return (k0 * (k0 - 1.0)) / k1;
  }

  const planes = block.planes;
  const r = block.edgeBevel;
  let d1 = -1e9;
  let d2 = -1e9;

  for (let i = 0; i < planes.length; i += 4) {
    const pd = planes[i] * wx + planes[i + 1] * wy + planes[i + 2] * wz - planes[i + 3];
    if (pd > d1) {
      d2 = d1;
      d1 = pd;
    } else if (pd > d2) {
      d2 = pd;
    }
  }

  lastSecondPlaneDist = d2;

  if (r > 1e-5 && d2 > -r * 2) {
    const ch = (d1 + r + d2) * 0.7071067811865475;
    if (ch > d1) return ch;
  }
  return d1;
}
