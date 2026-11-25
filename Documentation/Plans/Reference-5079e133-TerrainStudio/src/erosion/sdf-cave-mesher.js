/**
 * Real 3D Volumetric SDF Cave & Cliff-Undercut Evaluator and Surface Extractor.
 *
 * Subtracts true 3D cave tunnels, karst chambers, and cliff-base undercuts from the terrain
 * volume F(x, y, z) = smax(y - H(x, z), -CaveField(x, y, z)), producing:
 *   1. A 3D triangle mesh (positions, outward normals, caveAO + hardness) for cave ceilings,
 *      overhanging lips, pillars, and recessed cliff alcoves that heightmaps cannot represent.
 *   2. A 2D portal cutout field (RGBA32F / R8) telling the heightfield shader where 3D cave
 *      openings breach the surface skin.
 */

function fract(x) {
  return x - Math.floor(x);
}

function hash3(ix, iy, iz, seed) {
  const n = Math.sin(ix * 127.1 + iy * 311.7 + iz * 74.7 + seed * 19.19) * 43758.5453;
  return n - Math.floor(n);
}

function noise3D(x, y, z, seed) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
  const fx = x - ix, fy = y - iy, fz = z - iz;
  const ux = fx * fx * (3 - 2 * fx);
  const uy = fy * fy * (3 - 2 * fy);
  const uz = fz * fz * (3 - 2 * fz);

  const n000 = hash3(ix, iy, iz, seed);
  const n100 = hash3(ix + 1, iy, iz, seed);
  const n010 = hash3(ix, iy + 1, iz, seed);
  const n110 = hash3(ix + 1, iy + 1, iz, seed);
  const n001 = hash3(ix, iy, iz + 1, seed);
  const n101 = hash3(ix + 1, iy, iz + 1, seed);
  const n011 = hash3(ix, iy + 1, iz + 1, seed);
  const n111 = hash3(ix + 1, iy + 1, iz + 1, seed);

  const nx00 = n000 + (n100 - n000) * ux;
  const nx10 = n010 + (n110 - n010) * ux;
  const nx01 = n001 + (n101 - n001) * ux;
  const nx11 = n011 + (n111 - n011) * ux;
  const nxy0 = nx00 + (nx10 - nx00) * uy;
  const nxy1 = nx01 + (nx11 - nx01) * uy;
  return nxy0 + (nxy1 - nxy0) * uz;
}

function smoothstep(e0, e1, x) {
  const t = Math.max(0, Math.min(1, (x - e0) / Math.max(1e-6, e1 - e0)));
  return t * t * (3 - 2 * t);
}

function smax(a, b, k = 2.0) {
  const h = Math.max(0, Math.min(1, 0.5 + (0.5 * (a - b)) / k));
  return b + (a - b) * h + k * h * (1 - h);
}

function catmullRom2D(p0, p1, p2, p3, t) {
  const t2 = t * t;
  const t3 = t2 * t;
  return [
    0.5 * ((2.0 * p1[0]) + (-p0[0] + p2[0]) * t +
      (2.0 * p0[0] - 5.0 * p1[0] + 4.0 * p2[0] - p3[0]) * t2 +
      (-p0[0] + 3.0 * p1[0] - 3.0 * p2[0] + p3[0]) * t3),
    0.5 * ((2.0 * p1[1]) + (-p0[1] + p2[1]) * t +
      (2.0 * p0[1] - 5.0 * p1[1] + 4.0 * p2[1] - p3[1]) * t2 +
      (-p0[1] + 3.0 * p1[1] - 3.0 * p2[1] + p3[1]) * t3),
  ];
}

function catmullRom1D(p0, p1, p2, p3, t) {
  const t2 = t * t;
  const t3 = t2 * t;
  return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
    (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
    (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

// Marching Cubes Edge & Triangle Lookup Tables (compact bitmask + tri connectivity)
const EDGE_VERTS = [
  [0, 1], [1, 2], [2, 3], [3, 0],
  [4, 5], [5, 6], [6, 7], [7, 4],
  [0, 4], [1, 5], [2, 6], [3, 7],
];

// Marching Tetrahedra decomposition of a voxel cube into 6 tetrahedra (guarantees manifold 2D surface without 256-entry table!)
const CUBE_TETS = [
  [0, 5, 1, 6],
  [0, 1, 2, 6],
  [0, 2, 3, 6],
  [0, 3, 7, 6],
  [0, 7, 4, 6],
  [0, 4, 5, 6],
];

const TET_EDGES = [
  [0, 1], [1, 2], [2, 0],
  [0, 3], [1, 3], [2, 3],
];

// Lookup for the 16 inside/outside states of a tetrahedron (oriented outward toward F > 0 air!)
const TET_TRIS = [
  [],
  [[0, 3, 2]],
  [[0, 1, 4]],
  [[1, 4, 2], [2, 4, 3]],
  [[1, 2, 5]],
  [[0, 3, 5], [0, 5, 1]],
  [[0, 2, 5], [0, 5, 4]],
  [[3, 5, 4]],
  [[3, 4, 5]],
  [[0, 4, 5], [0, 5, 2]],
  [[0, 1, 5], [0, 5, 3]],
  [[1, 5, 2]],
  [[1, 2, 4], [2, 3, 4]],
  [[0, 4, 1]],
  [[0, 2, 3]],
  [],
];

export class SdfCaveSystem {
  constructor() {
    this.portalMaskSize = 128;
    this.portalTexData = new Float32Array(this.portalMaskSize * this.portalMaskSize * 4);
    this.positions = new Float32Array(0);
    this.normals = new Float32Array(0);
    this.attribs = new Float32Array(0); // (caveAO, hardness, heightNorm, caveDepth)
    this.vertexCount = 0;
  }

  /**
   * Evaluates all active `sdf-caves` layers and `sculpt-layer` 3D cave strokes against the
   * current terrain heightfield snapshot (`gridRgba`, size `gridN x gridN`, `worldSpan`).
   */
  rebuildCaveVolume({
    gridRgba,
    gridN,
    worldSpan,
    caveLayers = [],
    sculptCaveStrokes = [],
    splineTunnels = [],
    maxRelief = 180,
  }) {
    const P = this.portalMaskSize;
    this.portalTexData.fill(0);

    const activeCaves = caveLayers.filter((l) => l.enabled !== false && (l.params.opacity ?? 1) > 0.02);
    if (activeCaves.length === 0 && sculptCaveStrokes.length === 0 && splineTunnels.length === 0) {
      this.vertexCount = 0;
      return { vertexCount: 0, hasCaves: false };
    }

    const half = worldSpan * 0.5;
    const sampleHeightSlope = (wx, wz) => {
      const u = Math.max(0, Math.min(1, wx / worldSpan + 0.5)) * (gridN - 1);
      const v = Math.max(0, Math.min(1, wz / worldSpan + 0.5)) * (gridN - 1);
      const i0 = Math.min(gridN - 2, Math.floor(u));
      const j0 = Math.min(gridN - 2, Math.floor(v));
      const fu = u - i0, fv = v - j0;

      const hAt = (ii, jj) => {
        const idx = (jj * gridN + ii) * 4;
        return gridRgba[idx] + gridRgba[idx + 1] + gridRgba[idx + 2];
      };
      const h00 = hAt(i0, j0), h10 = hAt(i0 + 1, j0);
      const h01 = hAt(i0, j0 + 1), h11 = hAt(i0 + 1, j0 + 1);
      const h = (h00 * (1 - fu) + h10 * fu) * (1 - fv) + (h01 * (1 - fu) + h11 * fu) * fv;

      const cell = worldSpan / gridN;
      const dhdx = ((h10 - h00) * (1 - fv) + (h11 - h01) * fv) / cell;
      const dhdz = ((h01 - h00) * (1 - fu) + (h11 - h10) * fu) / cell;
      const slope = Math.hypot(dhdx, dhdz);
      return { h, slope, dhdx, dhdz };
    };

    // Continuous Spline Landform tunnel paths. Each path is sampled as a
    // smooth Catmull-Rom curve, lifted to a start->end elevation grade (or a
    // constant depth below the local surface), then evaluated below as a
    // true 3D capsule/tube SDF. Endpoints are extended beyond the authored
    // points so a bore can cleanly exit both sides of a ridge.
    const tunnelPaths = [];
    for (const tunnel of splineTunnels) {
      if (!tunnel || tunnel.enabled === false) continue;
      const controls = (Array.isArray(tunnel.points) ? tunnel.points : [])
        .map((pt) => Array.isArray(pt) ? [Number(pt[0]), Number(pt[1]), Number(pt[2])] : [Number(pt?.x ?? pt?.[0]), Number(pt?.z ?? pt?.[1]), Number(pt?.y ?? pt?.[2])])
        .filter((pt) => Number.isFinite(pt[0]) && Number.isFinite(pt[1]))
        .slice(0, 16);
      if (controls.length < 2) continue;
      const radius = Math.max(1.5, Number(tunnel.radius ?? tunnel.tunnelRadius ?? 7.5) || 7.5);
      const startY = Number(tunnel.startElevation ?? tunnel.startY ?? 46) || 46;
      const endY = Number(tunnel.endElevation ?? tunnel.endY ?? startY) || startY;
      const depth = Math.max(4, Number(tunnel.depth ?? tunnel.tunnelDepth ?? 22) || 22);
      const followSurface = tunnel.followSurface === true || tunnel.tunnelFollowSurface > 0.5;
      // Optional authored 3D path: a finite third coordinate on any control
      // point is its absolute bore elevation. The tube then follows a true 3D
      // Catmull-Rom through those heights; unset points fall back to the
      // same grade / surface-depth the 2D path used. With no authored Y
      // anywhere the sampling below is exactly the original behaviour.
      const hasY = controls.some((c) => Number.isFinite(c[2]));
      const controlYs = hasY ? controls.map((c, ci) => {
        if (Number.isFinite(c[2])) return c[2];
        const t = controls.length > 1 ? ci / (controls.length - 1) : 0;
        return followSurface ? sampleHeightSlope(c[0], c[1]).h - depth : startY + (endY - startY) * t;
      }) : null;
      const samples = [];
      const pushSample = (x, z, along, authoredY = NaN) => {
        const y = Number.isFinite(authoredY)
          ? authoredY
          : (followSurface ? sampleHeightSlope(x, z).h - depth : startY + (endY - startY) * along);
        samples.push({ x, y, z });
      };
      pushSample(controls[0][0], controls[0][1], 0, hasY ? controlYs[0] : NaN);
      const segCount = controls.length - 1;
      for (let i = 0; i < segCount; i++) {
        const p0 = controls[Math.max(0, i - 1)];
        const p1 = controls[i];
        const p2 = controls[Math.min(segCount, i + 1)];
        const p3 = controls[Math.min(segCount, i + 2)];
        const chord = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]);
        const subdiv = Math.max(3, Math.min(12, Math.ceil(chord / Math.max(9, radius * 1.15))));
        for (let j = 1; j <= subdiv; j++) {
          const t = j / subdiv;
          const [x, z] = catmullRom2D(p0, p1, p2, p3, t);
          const authoredY = hasY ? catmullRom1D(
            controlYs[Math.max(0, i - 1)],
            controlYs[i],
            controlYs[Math.min(segCount, i + 1)],
            controlYs[Math.min(segCount, i + 2)],
            t
          ) : NaN;
          pushSample(x, z, (i + t) / segCount, authoredY);
        }
      }
      if (samples.length >= 2) {
        const extendEnd = (from, to, sign) => {
          let dx = to.x - from.x;
          let dz = to.z - from.z;
          const len = Math.hypot(dx, dz) || 1;
          dx /= len; dz /= len;
          const dist = radius * 3.0 + worldSpan / P * 2.0;
          const dyPerM = Math.max(-0.8, Math.min(0.8, (to.y - from.y) / len));
          return { x: to.x + dx * dist * sign, y: to.y + dyPerM * dist * sign, z: to.z + dz * dist * sign };
        };
        samples.unshift(extendEnd(samples[1], samples[0], 1));
        samples.push(extendEnd(samples[samples.length - 2], samples[samples.length - 1], 1));
      }
      const pad = radius + 7;
      const segments = [];
      let minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9, minZ = 1e9, maxZ = -1e9;
      for (const sm of samples) {
        minX = Math.min(minX, sm.x); maxX = Math.max(maxX, sm.x);
        minY = Math.min(minY, sm.y); maxY = Math.max(maxY, sm.y);
        minZ = Math.min(minZ, sm.z); maxZ = Math.max(maxZ, sm.z);
      }
      for (let i = 0; i < samples.length - 1; i++) {
        const a = samples[i], b = samples[i + 1];
        segments.push({
          a, b,
          minX: Math.min(a.x, b.x) - pad, maxX: Math.max(a.x, b.x) + pad,
          minY: Math.min(a.y, b.y) - pad, maxY: Math.max(a.y, b.y) + pad,
          minZ: Math.min(a.z, b.z) - pad, maxZ: Math.max(a.z, b.z) + pad,
        });
      }
      const bucketN = 32;
      const buckets = Array.from({ length: bucketN * bucketN }, () => []);
      segments.forEach((seg, segIdx) => {
        const bx0 = Math.max(0, Math.min(bucketN - 1, Math.floor(((seg.minX + half) / worldSpan) * bucketN)));
        const bx1 = Math.max(0, Math.min(bucketN - 1, Math.floor(((seg.maxX + half) / worldSpan) * bucketN)));
        const bz0 = Math.max(0, Math.min(bucketN - 1, Math.floor(((seg.minZ + half) / worldSpan) * bucketN)));
        const bz1 = Math.max(0, Math.min(bucketN - 1, Math.floor(((seg.maxZ + half) / worldSpan) * bucketN)));
        for (let bz = bz0; bz <= bz1; bz++) {
          for (let bx = bx0; bx <= bx1; bx++) buckets[bz * bucketN + bx].push(segIdx);
        }
      });
      tunnelPaths.push({
        samples,
        segments,
        buckets,
        bucketN,
        radius,
        roughness: Math.max(0, Math.min(1, Number(tunnel.roughness ?? tunnel.tunnelRoughness ?? 0.32) || 0)),
        strength: Math.max(0.05, Math.min(1, Number(tunnel.strength ?? tunnel.opacity ?? 1) || 1)),
        seed: Number(tunnel.seed ?? 7201) || 7201,
        minX: minX - pad, maxX: maxX + pad,
        minY: minY - pad, maxY: maxY + pad,
        minZ: minZ - pad, maxZ: maxZ + pad,
      });
    }

    // Evaluate positive cave void field C(x, y, z) > 0 inside carved cave/undercut
    const evalCaveVoid = (wx, wy, wz, surf) => {
      let voidVal = -999.0;
      const depthIntoGround = surf.h - wy;

      for (let li = 0; li < activeCaves.length; li++) {
        const p = activeCaves[li].params;
        const seed = p.seed || 6419;
        const slopeTan = Math.tan(((p.slopeMin ?? 24) * Math.PI) / 180);
        const slopeGate = smoothstep(slopeTan * 0.65, slopeTan * 1.45, surf.slope);
        if (slopeGate <= 0.01) continue;

        const elevCenterM = 12.0 + (p.elevationCenter ?? 0.36) * maxRelief * 0.85;
        const elevHalfM = Math.max(8.0, (p.elevationBand ?? 0.28) * maxRelief * 0.55);
        const archH = Math.max(6.0, p.archHeight ?? 16.0);

        // Sinuous cave floor warp + stratigraphic bedding rib modulation
        const freq = (p.tunnelScale ?? 2.3) / 160.0;
        const tunnelWave = noise3D(wx * freq, wy * freq * 0.65, wz * freq, seed);
        const tunnelTube = noise3D(wx * freq * 1.8 + 11.3, wy * freq * 1.1, wz * freq * 1.8 - 7.1, seed + 37);
        const rib = (noise3D(wx * 0.09, wy * 0.28, wz * 0.09, seed + 91) - 0.5) * 4.5 * (p.ribRoughness ?? 0.5);

        const localCenterY = elevCenterM + (tunnelWave - 0.5) * elevHalfM * 0.9;
        const dy = (wy - localCenterY) / (archH * 0.55);
        // Flat cave floor, arched ceiling!
        const asymY = dy < 0 ? dy * 1.45 : dy;
        const vertProfile = 1.0 - asymY * asymY;
        if (vertProfile <= -0.6) continue;

        // Horizontal tunnel pockets + continuous wave-cut cliff undercut
        const pocket = smoothstep(0.40, 0.72, tunnelTube) * (p.carveDepth ?? 24.0);
        const undercut = (p.undercutStrength ?? 0.68) * 11.5 * smoothstep(0.32, 0.68, tunnelWave);
        const targetPenetration = (pocket + undercut) * slopeGate * (p.opacity ?? 1.0);
        if (targetPenetration <= 1.2) continue;

        // Cave void is positive when wy is within the arch band AND within targetPenetration of the cliff face!
        const cVal = Math.min(
          vertProfile * archH * 0.55 + rib,
          targetPenetration - depthIntoGround * 0.72
        );
        if (cVal > voidVal) voidVal = cVal;
      }

      // User-sculpted 3D SDF Cave Carve & Cave Fill strokes!
      for (let si = 0; si < sculptCaveStrokes.length; si++) {
        const s = sculptCaveStrokes[si];
        const dx = wx - s.x;
        const dy = (wy - s.y) * 1.35; // slightly oblate cavern arch
        const dz = wz - s.z;
        const sphere = s.radius - Math.hypot(dx, dy, dz);
        if (s.op === 'cave-fill') {
          voidVal = Math.min(voidVal, -sphere);
        } else {
          voidVal = smax(voidVal, sphere * (s.strength ?? 1.0), 2.5);
        }
      }

      // Authored spline tunnels: distance to the nearest smooth 3D tube
      // segment. A slight vertical squash gives a road/rail-like arched
      // bore rather than a perfectly circular pipe.
      for (let ti = 0; ti < tunnelPaths.length; ti++) {
        const path = tunnelPaths[ti];
        if (wx < path.minX || wx > path.maxX || wy < path.minY || wy > path.maxY || wz < path.minZ || wz > path.maxZ) continue;
        let bestDist = 1e9;
        const bx = Math.max(0, Math.min(path.bucketN - 1, Math.floor(((wx + half) / worldSpan) * path.bucketN)));
        const bz = Math.max(0, Math.min(path.bucketN - 1, Math.floor(((wz + half) / worldSpan) * path.bucketN)));
        const candidates = path.buckets[bz * path.bucketN + bx];
        for (let ci = 0; ci < candidates.length; ci++) {
          const seg = path.segments[candidates[ci]];
          if (wx < seg.minX || wx > seg.maxX || wy < seg.minY || wy > seg.maxY || wz < seg.minZ || wz > seg.maxZ) continue;
          const abx = seg.b.x - seg.a.x;
          const aby = seg.b.y - seg.a.y;
          const abz = seg.b.z - seg.a.z;
          const len2 = Math.max(1e-6, abx * abx + aby * aby + abz * abz);
          const t = Math.max(0, Math.min(1, ((wx - seg.a.x) * abx + (wy - seg.a.y) * aby + (wz - seg.a.z) * abz) / len2));
          const dx = wx - (seg.a.x + abx * t);
          const dy = (wy - (seg.a.y + aby * t)) * 1.18;
          const dz = wz - (seg.a.z + abz * t);
          const d = Math.hypot(dx, dy, dz);
          if (d < bestDist) bestDist = d;
        }
        if (bestDist < path.radius + 5.0) {
          const rough = ((noise3D(wx * 0.055, wy * 0.11, wz * 0.055, path.seed) - 0.5) * path.radius * 0.42 +
            (noise3D(wx * 0.16, wy * 0.24, wz * 0.16, path.seed + 43) - 0.5) * path.radius * 0.12) * path.roughness;
          const tube = (path.radius + rough - bestDist) * path.strength;
          voidVal = smax(voidVal, tube, 2.2);
        }
      }

      return voidVal;
    };

    // 1. Populate 2D Portal Mask (`128 x 128`) and find active 3D bounding cells
    let minI = P, maxI = -1, minK = P, maxK = -1;
    let minCaveY = 1e6, maxCaveY = -1e6;
    const surfCache = new Array(P * P);

    for (let k = 0; k < P; k++) {
      const wz = ((k + 0.5) / P - 0.5) * worldSpan;
      for (let i = 0; i < P; i++) {
        const wx = ((i + 0.5) / P - 0.5) * worldSpan;
        const surf = sampleHeightSlope(wx, wz);
        surfCache[k * P + i] = surf;

        // Probe vertical column below surface for cave void
        let colMaxVoid = -999;
        let portalOpen = 0;
        let lowY = surf.h, highY = surf.h;
        for (let step = 0; step <= 7; step++) {
          const wy = surf.h - step * 4.2;
          const cv = evalCaveVoid(wx, wy, wz, surf);
          if (cv > colMaxVoid) colMaxVoid = cv;
          if (step <= 2 && cv > 0.25) portalOpen = Math.max(portalOpen, Math.min(1, cv / 3.5));
          if (cv > -2.5) {
            if (wy - 8 < lowY) lowY = wy - 8;
            if (wy + 8 > highY) highY = wy + 8;
          }
        }

        const pIdx = (k * P + i) * 4;
        this.portalTexData[pIdx + 0] = portalOpen; // R: surface portal opening alpha
        this.portalTexData[pIdx + 1] = Math.max(0, colMaxVoid); // G: subterranean cave thickness
        if (colMaxVoid > 0.15) {
          if (i < minI) minI = i;
          if (i > maxI) maxI = i;
          if (k < minK) minK = k;
          if (k > maxK) maxK = k;
          if (lowY < minCaveY) minCaveY = lowY;
          if (highY > maxCaveY) maxCaveY = highY;
        }
      }
    }

    // Deep authored tunnels can lie below the shallow portal probe, so add
    // their exact tube bounds to the extraction volume.
    for (const path of tunnelPaths) {
      const cellSpan = worldSpan / P;
      const radiusCells = Math.ceil((path.radius + 8) / cellSpan);
      for (const sm of path.samples) {
        if (sm.x < -half - path.radius || sm.x > half + path.radius || sm.z < -half - path.radius || sm.z > half + path.radius) continue;
        const ci = Math.max(0, Math.min(P - 1, Math.floor((sm.x / worldSpan + 0.5) * P)));
        const ck = Math.max(0, Math.min(P - 1, Math.floor((sm.z / worldSpan + 0.5) * P)));
        minI = Math.min(minI, ci - radiusCells);
        maxI = Math.max(maxI, ci + radiusCells);
        minK = Math.min(minK, ck - radiusCells);
        maxK = Math.max(maxK, ck + radiusCells);
        minCaveY = Math.min(minCaveY, sm.y - path.radius - 8);
        maxCaveY = Math.max(maxCaveY, sm.y + path.radius + 8);
      }
    }

    if (maxI < minI || maxCaveY <= minCaveY) {
      this.vertexCount = 0;
      return { vertexCount: 0, hasCaves: false };
    }

    // 2. Extract 3D Volumetric SDF Cave Interior & Overhang Lip Mesh via Marching Tetrahedra
    // Restrict voxel grid tightly to active cave bounds for < 6 ms interactive extraction!
    minI = Math.max(1, minI - 2);
    maxI = Math.min(P - 2, maxI + 2);
    minK = Math.max(1, minK - 2);
    maxK = Math.min(P - 2, maxK + 2);
    minCaveY = Math.max(-10, minCaveY - 4);
    maxCaveY = Math.min(maxRelief + 30, maxCaveY + 4);

    const nx = maxI - minI + 1;
    const nz = maxK - minK + 1;
    const voxelYStep = Math.max(2.6, (maxCaveY - minCaveY) / 26);
    const ny = Math.max(4, Math.min(30, Math.ceil((maxCaveY - minCaveY) / voxelYStep) + 1));

    const sdfField = new Float32Array(nx * ny * nz);
    const caveMaskField = new Float32Array(nx * ny * nz);
    const fIdx = (ix, iy, iz) => (iz * ny + iy) * nx + ix;

    for (let iz = 0; iz < nz; iz++) {
      const k = minK + iz;
      const wz = ((k + 0.5) / P - 0.5) * worldSpan;
      for (let ix = 0; ix < nx; ix++) {
        const i = minI + ix;
        const wx = ((i + 0.5) / P - 0.5) * worldSpan;
        const surf = surfCache[k * P + i];
        for (let iy = 0; iy < ny; iy++) {
          const wy = minCaveY + iy * voxelYStep;
          const sTerr = wy - surf.h; // < 0 inside rock, > 0 in open air
          const cVoid = evalCaveVoid(wx, wy, wz, surf); // > 0 inside carved cave
          // F > 0 is air (either outside terrain OR inside carved cave), F < 0 is solid rock!
          const F = smax(sTerr, cVoid, 1.8);
          const id = fIdx(ix, iy, iz);
          sdfField[id] = F;
          caveMaskField[id] = cVoid;
        }
      }
    }

    const posOut = [];
    const nrmOut = [];
    const attOut = [];

    const cellSpanXZ = worldSpan / P;
    const gradAt = (ix, iy, iz) => {
      const xm = Math.max(0, ix - 1), xp = Math.min(nx - 1, ix + 1);
      const ym = Math.max(0, iy - 1), yp = Math.min(ny - 1, iy + 1);
      const zm = Math.max(0, iz - 1), zp = Math.min(nz - 1, iz + 1);
      const gx = (sdfField[fIdx(xp, iy, iz)] - sdfField[fIdx(xm, iy, iz)]) / ((xp - xm || 1) * cellSpanXZ);
      const gy = (sdfField[fIdx(ix, yp, iz)] - sdfField[fIdx(ix, ym, iz)]) / ((yp - ym || 1) * voxelYStep);
      const gz = (sdfField[fIdx(ix, iy, zp)] - sdfField[fIdx(ix, iy, zm)]) / ((zp - zm || 1) * cellSpanXZ);
      const l = Math.hypot(gx, gy, gz) || 1;
      return [gx / l, gy / l, gz / l];
    };

    const cornerCoords = new Array(8);
    const cornerF = new Float32Array(8);
    const cornerC = new Float32Array(8);
    const cornerIJK = new Array(8);

    for (let iz = 0; iz < nz - 1; iz++) {
      const z0 = ((minK + iz + 0.5) / P - 0.5) * worldSpan;
      const z1 = z0 + cellSpanXZ;
      for (let ix = 0; ix < nx - 1; ix++) {
        const x0 = ((minI + ix + 0.5) / P - 0.5) * worldSpan;
        const x1 = x0 + cellSpanXZ;
        for (let iy = 0; iy < ny - 1; iy++) {
          const y0 = minCaveY + iy * voxelYStep;
          const y1 = y0 + voxelYStep;

          const ids = [
            fIdx(ix, iy, iz), fIdx(ix + 1, iy, iz), fIdx(ix + 1, iy, iz + 1), fIdx(ix, iy, iz + 1),
            fIdx(ix, iy + 1, iz), fIdx(ix + 1, iy + 1, iz), fIdx(ix + 1, iy + 1, iz + 1), fIdx(ix, iy + 1, iz + 1),
          ];

          let neg = 0, pos = 0, maxCave = -999;
          for (let c = 0; c < 8; c++) {
            const v = sdfField[ids[c]];
            const cv = caveMaskField[ids[c]];
            cornerF[c] = v;
            cornerC[c] = cv;
            if (cv > maxCave) maxCave = cv;
            if (v < 0) neg++; else pos++;
          }
          // Only emit triangles along the carved cave walls/ceilings/lips (where cave field participates!)
          if (neg === 0 || pos === 0 || maxCave < -1.6) continue;

          cornerCoords[0] = [x0, y0, z0]; cornerIJK[0] = [ix, iy, iz];
          cornerCoords[1] = [x1, y0, z0]; cornerIJK[1] = [ix + 1, iy, iz];
          cornerCoords[2] = [x1, y0, z1]; cornerIJK[2] = [ix + 1, iy, iz + 1];
          cornerCoords[3] = [x0, y0, z1]; cornerIJK[3] = [ix, iy, iz + 1];
          cornerCoords[4] = [x0, y1, z0]; cornerIJK[4] = [ix, iy + 1, iz];
          cornerCoords[5] = [x1, y1, z0]; cornerIJK[5] = [ix + 1, iy + 1, iz];
          cornerCoords[6] = [x1, y1, z1]; cornerIJK[6] = [ix + 1, iy + 1, iz + 1];
          cornerCoords[7] = [x0, y1, z1]; cornerIJK[7] = [ix, iy + 1, iz + 1];

          for (let ti = 0; ti < 6; ti++) {
            const tet = CUBE_TETS[ti];
            let mask = 0;
            if (cornerF[tet[0]] < 0) mask |= 1;
            if (cornerF[tet[1]] < 0) mask |= 2;
            if (cornerF[tet[2]] < 0) mask |= 4;
            if (cornerF[tet[3]] < 0) mask |= 8;
            const tris = TET_TRIS[mask];
            if (tris.length === 0) continue;

            for (let t = 0; t < tris.length; t++) {
              const tri = tris[t];
              for (let vi = 0; vi < 3; vi++) {
                const [ea, eb] = TET_EDGES[tri[vi]];
                const ca = tet[ea], cb = tet[eb];
                const fa = cornerF[ca], fb = cornerF[cb];
                const mu = Math.max(0, Math.min(1, -fa / (fb - fa || 1e-6)));
                const pa = cornerCoords[ca], pb = cornerCoords[cb];
                const px = pa[0] + (pb[0] - pa[0]) * mu;
                const py = pa[1] + (pb[1] - pa[1]) * mu;
                const pz = pa[2] + (pb[2] - pa[2]) * mu;

                const na = gradAt(...cornerIJK[ca]);
                const nb = gradAt(...cornerIJK[cb]);
                let nxv = na[0] + (nb[0] - na[0]) * mu;
                let nyv = na[1] + (nb[1] - na[1]) * mu;
                let nzv = na[2] + (nb[2] - na[2]) * mu;
                const nl = Math.hypot(nxv, nyv, nzv) || 1;
                nxv /= nl; nyv /= nl; nzv /= nl;

                const cDepth = Math.max(0, cornerC[ca] + (cornerC[cb] - cornerC[ca]) * mu);
                const ao = Math.max(0.14, Math.min(1.0, 0.82 - cDepth * 0.045 + nyv * 0.16));

                posOut.push(px, py, pz);
                nrmOut.push(nxv, nyv, nzv);
                attOut.push(ao, 0.72, Math.max(0, Math.min(1, py / maxRelief)), cDepth);
              }
            }
          }
        }
      }
    }

    this.positions = new Float32Array(posOut);
    this.normals = new Float32Array(nrmOut);
    this.attribs = new Float32Array(attOut);
    this.vertexCount = posOut.length / 3;
    return { vertexCount: this.vertexCount, hasCaves: this.vertexCount > 0 };
  }
}
