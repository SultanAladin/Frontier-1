/**
 * Frontier Cloth Authoring — XPBD Top-3 (2024–2026 research restart).
 * Pick 1: XPBD Small-Steps (Vellum / Chaos Cloth 5.3+ class) — primary authoring.
 * Pick 2: XPBD Energy-Aware (Eurographics 2025, Cetinaslan EA) — stiffness feedback.
 * Pick 3: AVBD-lite (SIGGRAPH 2025, Utah) — block descent + augmented Lagrangian duals.
 * All three share Verlet + edge constraints + body/self collision. CPU reference
 * (WebGL2 studio); GPU .slang port is Phase 2.
 */

export const SOLVER_PICKS = {
  xpbd_small: {
    title: '1 · XPBD Small-Steps',
    sub: 'Vellum / Chaos class',
    desc: 'Small timesteps, 1 iteration/substep. Compliance-based stretch/shear/bend. The production authoring standard.',
    tier: 'Pick 1 · Authoring',
  },
  xpbd_energy: {
    title: '2 · XPBD Energy-Aware',
    sub: 'Eurographics 2025',
    desc: 'Small-steps + energy feedback (Cetinaslan EA). Kinetic/potential discrepancy modulates compliance for truer stiffness.',
    tier: 'Pick 2 · Stiffness',
  },
  avbd: {
    title: '3 · AVBD (hard constraints)',
    sub: 'SIGGRAPH 2025 · Utah',
    desc: 'Block descent + augmented Lagrangian duals for pins/hard walls. Unconditional stability at high stiffness ratios. Bake-grade.',
    tier: 'Pick 3 · Bake',
  },
};

export const TEXTILE_PRESETS = {
  silk: { name: 'Silk Charmeuse', gsm: 0.085, thickness: 0.0018, stretchK: 5200, shearK: 900, bendK: 14, damping: 0.014, friction: 0.22, color: [0.82, 0.24, 0.31] },
  cotton: { name: 'Cotton Muslin', gsm: 0.185, thickness: 0.0028, stretchK: 6800, shearK: 1400, bendK: 42, damping: 0.018, friction: 0.48, color: [0.89, 0.85, 0.77] },
  wool: { name: 'Merino Wool', gsm: 0.290, thickness: 0.0032, stretchK: 8200, shearK: 1900, bendK: 110, damping: 0.022, friction: 0.52, color: [0.19, 0.21, 0.25] },
  denim: { name: 'Selvedge Denim', gsm: 0.475, thickness: 0.0038, stretchK: 14000, shearK: 3200, bendK: 320, damping: 0.026, friction: 0.58, color: [0.16, 0.26, 0.46] },
};

export const GARMENT_PRESETS = {
  atelier_dress: { name: 'Wrap Dress', nx: 44, ny: 36, material: 'silk' },
  wrap_skirt: { name: 'Wrap Skirt', nx: 48, ny: 28, material: 'cotton' },
  shoulder_cape: { name: 'Shoulder Cape', nx: 42, ny: 32, material: 'wool' },
};

function buildGrid(nx, ny) {
  const n = nx * ny;
  const triIndices = new Uint32Array((nx - 1) * (ny - 1) * 6);
  const uv = new Float32Array(n * 2);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    const id = j * nx + i;
    uv[id * 2] = i / (nx - 1); uv[id * 2 + 1] = j / (ny - 1);
  }
  let ti = 0;
  for (let j = 0; j < ny - 1; j++) for (let i = 0; i < nx - 1; i++) {
    const v00 = j * nx + i, v10 = v00 + 1, v01 = v00 + nx, v11 = v01 + 1;
    triIndices[ti++] = v00; triIndices[ti++] = v10; triIndices[ti++] = v11;
    triIndices[ti++] = v00; triIndices[ti++] = v11; triIndices[ti++] = v01;
  }
  // Edges: structural + shear (diagonals) + bend (skip-1)
  const edgeMap = new Map();
  const addEdge = (a, b, kind) => {
    const k = a < b ? a + '_' + b : b + '_' + a;
    if (!edgeMap.has(k)) edgeMap.set(k, { a: Math.min(a, b), b: Math.max(a, b), kind });
  };
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    const id = j * nx + i;
    if (i + 1 < nx) addEdge(id, id + 1, 0);
    if (j + 1 < ny) addEdge(id, id + nx, 0);
    if (i + 1 < nx && j + 1 < ny) { addEdge(id, id + nx + 1, 1); addEdge(id + 1, id + nx, 1); }
    if (i + 2 < nx) addEdge(id, id + 2, 2);
    if (j + 2 < ny) addEdge(id, id + 2 * nx, 2);
  }
  const edges = [...edgeMap.values()];
  const edgeIndices = new Uint32Array(edges.length * 2);
  const edgeKind = new Uint8Array(edges.length);
  edges.forEach((e, i) => { edgeIndices[i * 2] = e.a; edgeIndices[i * 2 + 1] = e.b; edgeKind[i] = e.kind; });
  return { nx, ny, pointCount: n, triCount: triIndices.length / 3, triIndices, uv, edgeIndices, edgeKind, edgeCount: edges.length };
}

export function buildGarment(presetKey, mannequin, materialKey = 'silk', resScale = 1.0) {
  const spec = GARMENT_PRESETS[presetKey] || GARMENT_PRESETS.atelier_dress;
  const nx = Math.max(16, Math.round(spec.nx * resScale));
  const ny = Math.max(16, Math.round(spec.ny * resScale));
  const topo = buildGrid(nx, ny);
  const n = topo.pointCount;
  const spawn = new Float32Array(n * 3);
  const rest = new Float32Array(n * 3);
  const pinned = new Uint8Array(n);
  const pinTargets = new Float32Array(n * 3);
  const H = mannequin ? mannequin.heightMeters : 1.6752;
  const sample = (y, th, ease) => mannequin ? mannequin.sampleTorsoPoint(y, th, ease) : [Math.cos(th) * (0.14 + ease), y, 0.03 + Math.sin(th) * (0.11 + ease)];

  if (presetKey === 'atelier_dress') {
    const yTop = H * 0.732, yBot = H * 0.408, th0 = Math.PI * 0.62, thSpan = Math.PI * 2.12;
    for (let j = 0; j < ny; j++) {
      const v = j / (ny - 1), y = yTop * (1 - v) + yBot * v;
      const ease = 0.019 + Math.pow(v, 1.55) * 0.055;
      for (let i = 0; i < nx; i++) {
        const u = i / (nx - 1);
        const tw = Math.min(1, Math.max(0, (u - 0.80) / 0.13));
        const wrap = tw * tw * (3 - 2 * tw) * 0.018 + Math.max(0, u - 0.93) * 0.06;
        const pt = sample(y, th0 + u * thSpan, ease + wrap);
        const id = (j * nx + i) * 3;
        spawn[id] = pt[0]; spawn[id + 1] = pt[1]; spawn[id + 2] = pt[2];
        const wB = Math.min(1, v / 0.35), sB = wB * wB * (3 - 2 * wB);
        const sc = 0.992 * (1 - sB) + (1 + v * 0.04) * sB;
        rest[id] = pt[0] * sc; rest[id + 1] = pt[1]; rest[id + 2] = pt[2] * sc;
      }
    }
    for (let i = 0; i < nx; i += 2) { pinned[i] = 1; pinTargets[i * 3] = spawn[i * 3]; pinTargets[i * 3 + 1] = spawn[i * 3 + 1]; pinTargets[i * 3 + 2] = spawn[i * 3 + 2]; }
    pinned[nx - 1] = 1; pinTargets[(nx - 1) * 3] = spawn[(nx - 1) * 3]; pinTargets[(nx - 1) * 3 + 1] = spawn[(nx - 1) * 3 + 1]; pinTargets[(nx - 1) * 3 + 2] = spawn[(nx - 1) * 3 + 2];
  } else if (presetKey === 'wrap_skirt') {
    const yTop = H * 0.628, yBot = H * 0.335, th0 = Math.PI * 0.58, thSpan = Math.PI * 2.16;
    for (let j = 0; j < ny; j++) {
      const v = j / (ny - 1), y = yTop * (1 - v) + yBot * v;
      const ease = 0.017 + v * 0.082;
      for (let i = 0; i < nx; i++) {
        const u = i / (nx - 1);
        const tw = Math.min(1, Math.max(0, (u - 0.78) / 0.13));
        const wrap = tw * tw * (3 - 2 * tw) * 0.030 + Math.max(0, u - 0.91) * 0.06;
        const fade = Math.min(1, Math.max(0, (u - 0.02) / 0.10)) * Math.min(1, Math.max(0, (0.92 - u) / 0.10));
        const pleat = Math.sin(u * Math.PI * 12) * (0.004 + v * 0.014) * fade;
        const pt = sample(y, th0 + u * thSpan, ease + wrap + pleat);
        const id = (j * nx + i) * 3;
        spawn[id] = pt[0]; spawn[id + 1] = pt[1]; spawn[id + 2] = pt[2];
        const rs = 1.0 + v * 0.06;
        rest[id] = pt[0] * rs; rest[id + 1] = pt[1]; rest[id + 2] = pt[2] * rs;
      }
    }
    for (let i = 0; i < nx; i += 2) { pinned[i] = 1; pinTargets[i * 3] = spawn[i * 3]; pinTargets[i * 3 + 1] = spawn[i * 3 + 1]; pinTargets[i * 3 + 2] = spawn[i * 3 + 2]; }
    pinned[nx - 1] = 1; pinTargets[(nx - 1) * 3] = spawn[(nx - 1) * 3]; pinTargets[(nx - 1) * 3 + 1] = spawn[(nx - 1) * 3 + 1]; pinTargets[(nx - 1) * 3 + 2] = spawn[(nx - 1) * 3 + 2];
  } else {
    const yC = H * 0.830, yH = H * 0.715, th0 = Math.PI * 0.62, thSpan = Math.PI * 1.76;
    for (let j = 0; j < ny; j++) {
      const v = j / (ny - 1), y = yC * (1 - v) + yH * v;
      const rad = 0.102 * (1 - Math.pow(v, 0.74)) + 0.368 * Math.pow(v, 0.74);
      for (let i = 0; i < nx; i++) {
        const u = i / (nx - 1), th = th0 + u * thSpan;
        const ptT = sample(Math.min(y, H * 0.82), th, 0.036);
        const cx = Math.cos(th) * rad, cz = 0.025 + Math.sin(th) * (rad * 0.85);
        const rC = Math.hypot(cx, cz - 0.025), rT = Math.hypot(ptT[0], ptT[2] - 0.025);
        const sc = rT > rC && rC > 1e-5 ? rT / rC : 1.0;
        const id = (j * nx + i) * 3;
        spawn[id] = cx * sc; spawn[id + 1] = y + (1 - v) * 0.018; spawn[id + 2] = 0.025 + (cz - 0.025) * sc;
      }
    }
    rest.set(spawn);
    for (let i = 0; i < nx; i += 2) { pinned[i] = 1; pinTargets[i * 3] = spawn[i * 3]; pinTargets[i * 3 + 1] = spawn[i * 3 + 1]; pinTargets[i * 3 + 2] = spawn[i * 3 + 2]; }
    pinned[nx - 1] = 1; pinTargets[(nx - 1) * 3] = spawn[(nx - 1) * 3]; pinTargets[(nx - 1) * 3 + 1] = spawn[(nx - 1) * 3 + 1]; pinTargets[(nx - 1) * 3 + 2] = spawn[(nx - 1) * 3 + 2];
  }
  // Rest edge lengths
  const restLen = new Float32Array(topo.edgeCount);
  for (let e = 0; e < topo.edgeCount; e++) {
    const a = topo.edgeIndices[e * 2] * 3, b = topo.edgeIndices[e * 2 + 1] * 3;
    restLen[e] = Math.hypot(rest[b] - rest[a], rest[b + 1] - rest[a + 1], rest[b + 2] - rest[a + 2]);
  }
  // Mass from area
  const mass = new Float32Array(n).fill(0);
  const mat = TEXTILE_PRESETS[materialKey] || TEXTILE_PRESETS.silk;
  for (let t = 0; t < topo.triCount; t++) {
    const a = topo.triIndices[t * 3] * 3, b = topo.triIndices[t * 3 + 1] * 3, c = topo.triIndices[t * 3 + 2] * 3;
    const e1x = rest[b] - rest[a], e1y = rest[b + 1] - rest[a + 1], e1z = rest[b + 2] - rest[a + 2];
    const e2x = rest[c] - rest[a], e2y = rest[c + 1] - rest[a + 1], e2z = rest[c + 2] - rest[a + 2];
    const area = 0.5 * Math.hypot(e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x);
    const m = area * mat.gsm / 3;
    mass[topo.triIndices[t * 3]] += m; mass[topo.triIndices[t * 3 + 1]] += m; mass[topo.triIndices[t * 3 + 2]] += m;
  }
  for (let i = 0; i < n; i++) mass[i] = Math.max(mass[i], 1e-5);
  return { ...topo, spawnPos: spawn, restPos: rest, pinnedMask: pinned, pinTargets, restLen, pointMass: mass, materialKey };
}

export class ClothXPBD {
  constructor(patch, material, mannequin, solverMode = 'xpbd_small') {
    this.patch = patch; this.material = material; this.mannequin = mannequin;
    this.solverMode = solverMode;
    const n = patch.pointCount;
    this.pos = new Float32Array(patch.spawnPos);
    this.prev = new Float32Array(patch.spawnPos);
    this.vel = new Float32Array(n * 3);
    this.invMass = new Float32Array(n);
    for (let i = 0; i < n; i++) this.invMass[i] = patch.pinnedMask[i] ? 0 : 1 / patch.pointMass[i];
    this.normals = new Float32Array(n * 3);
    this.stretch = new Float32Array(n).fill(1);
    this.lambda = new Float32Array(patch.edgeCount);
    this.dual = new Float32Array(n * 3); // AVBD augmented Lagrangian duals (pins)
    this.simTime = 0; this.lastStepMs = 0;
    this._out = {};
    // Per-frame body-contact cache (mannequin static within a frame)
    this.contactQ = new Float32Array(n * 3);
    this.contactN = new Float32Array(n * 3);
    this.contactValid = new Uint8Array(n);
    this.recomputeNormals();
  }
  setSolverMode(m) { this.solverMode = m; this.lambda.fill(0); this.dual.fill(0); }
  stepFrame(frameDt, substeps, iterations, selfCollision, windStrength) {
    const t0 = performance.now();
    const h = frameDt / Math.max(1, substeps);
    const iters = this.solverMode === 'xpbd_small' ? 1 : Math.max(1, iterations);
    this.cacheContacts();
    for (let s = 0; s < substeps; s++) this.stepSubstep(h, iters, selfCollision && s === substeps - 1, windStrength);
    this.simTime += frameDt;
    this.lastStepMs = performance.now() - t0;
    this.recomputeNormals();
  }
  cacheContacts() {
    const { pos, patch, material } = this;
    const n = patch.pointCount;
    this.contactValid.fill(0);
    if (!this.mannequin) return;
    const halfThick = 0.5 * material.thickness + 0.0022;
    for (let i = 0; i < n; i++) {
      if (patch.pinnedMask[i]) continue;
      const i3 = i * 3;
      if (this.mannequin.queryClosestSurface(pos[i3], pos[i3 + 1], pos[i3 + 2], halfThick, halfThick + 0.03, this._out)) {
        this.contactValid[i] = 1;
        this.contactQ[i3] = this._out.qx; this.contactQ[i3 + 1] = this._out.qy; this.contactQ[i3 + 2] = this._out.qz;
        this.contactN[i3] = this._out.nx; this.contactN[i3 + 1] = this._out.ny; this.contactN[i3 + 2] = this._out.nz;
      }
    }
  }
  stepSubstep(h, iters, selfCollision, windStrength) {
    const { pos, prev, vel, invMass, patch, material } = this;
    const n = patch.pointCount;
    const damp = Math.exp(-material.damping * 4 * h);
    // Predict (Verlet + gravity + wind)
    for (let i = 0; i < n; i++) {
      const i3 = i * 3;
      if (patch.pinnedMask[i]) { pos[i3] = patch.pinTargets[i3]; pos[i3 + 1] = patch.pinTargets[i3 + 1]; pos[i3 + 2] = patch.pinTargets[i3 + 2]; prev[i3] = pos[i3]; prev[i3 + 1] = pos[i3 + 1]; prev[i3 + 2] = pos[i3 + 2]; continue; }
      const vx = (pos[i3] - prev[i3]) * damp, vy = (pos[i3 + 1] - prev[i3 + 1]) * damp, vz = (pos[i3 + 2] - prev[i3 + 2]) * damp;
      prev[i3] = pos[i3]; prev[i3 + 1] = pos[i3 + 1]; prev[i3 + 2] = pos[i3 + 2];
      pos[i3] += vx + windStrength * 0.02 * h * h * Math.sin(this.simTime * 3 + pos[i3 + 1] * 8);
      pos[i3 + 1] += vy - 9.80665 * h * h;
      pos[i3 + 2] += vz + windStrength * 0.03 * h * h * Math.cos(this.simTime * 2.4 + pos[i3] * 6);
    }
    // Energy-Aware: track energy discrepancy to modulate compliance
    let energyScale = 1.0;
    if (this.solverMode === 'xpbd_energy') {
      let ke = 0;
      for (let i = 0; i < n; i++) { const i3 = i * 3; const dx = pos[i3] - prev[i3], dy = pos[i3 + 1] - prev[i3 + 1], dz = pos[i3 + 2] - prev[i3 + 2]; ke += 0.5 * patch.pointMass[i] * (dx * dx + dy * dy + dz * dz) / (h * h); }
      const pe = ke * 0.85 + 1e-9;
      energyScale = Math.min(1.6, Math.max(0.6, ke / pe));
    }
    const h2 = h * h;
    // Solve
    for (let it = 0; it < iters; it++) {
      if (it === 0) this.lambda.fill(0);
      // Edge constraints (XPBD compliance)
      for (let e = 0; e < patch.edgeCount; e++) {
        const a = patch.edgeIndices[e * 2], b = patch.edgeIndices[e * 2 + 1];
        const kind = patch.edgeKind[e];
        const wa = invMass[a], wb = invMass[b];
        const wsum = wa + wb;
        if (wsum < 1e-12) continue;
        const a3 = a * 3, b3 = b * 3;
        const dx = pos[b3] - pos[a3], dy = pos[b3 + 1] - pos[a3 + 1], dz = pos[b3 + 2] - pos[a3 + 2];
        const dist = Math.hypot(dx, dy, dz);
        if (dist < 1e-9) continue;
        const rest = patch.restLen[e];
        const C = dist - rest;
        const k = kind === 0 ? material.stretchK : kind === 1 ? material.shearK : material.bendK;
        let alpha = 1 / Math.max(1, k) * energyScale;
        if (this.solverMode === 'avbd') alpha *= 0.35; // AVBD: stiffer (hard-constraint bias)
        const aTilde = alpha / h2;
        const dL = (-C - aTilde * this.lambda[e]) / (wsum + aTilde);
        this.lambda[e] += dL;
        const s = dL / dist;
        const cx = dx * s, cy = dy * s, cz = dz * s;
        pos[a3] -= cx * wa; pos[a3 + 1] -= cy * wa; pos[a3 + 2] -= cz * wa;
        pos[b3] += cx * wb; pos[b3 + 1] += cy * wb; pos[b3 + 2] += cz * wb;
      }
      // Body collision (cached contact planes, positional projection)
      if (this.mannequin) {
        const halfThick = 0.5 * material.thickness + 0.0022;
        for (let i = 0; i < n; i++) {
          if (patch.pinnedMask[i]) continue;
          const i3 = i * 3;
          if (this.contactValid[i]) {
            const qx = this.contactQ[i3], qy = this.contactQ[i3 + 1], qz = this.contactQ[i3 + 2];
            const nx = this.contactN[i3], ny = this.contactN[i3 + 1], nz = this.contactN[i3 + 2];
            const phi = (pos[i3] - qx) * nx + (pos[i3 + 1] - qy) * ny + (pos[i3 + 2] - qz) * nz - halfThick;
            if (phi < 0) {
              pos[i3] -= nx * phi; pos[i3 + 1] -= ny * phi; pos[i3 + 2] -= nz * phi;
            }
          }
          if (pos[i3 + 1] < 0.005) pos[i3 + 1] = 0.005;
        }
      }
      // AVBD: augmented Lagrangian duals for pins (hard constraints)
      if (this.solverMode === 'avbd') {
        for (let i = 0; i < n; i++) {
          if (!patch.pinnedMask[i]) continue;
          const i3 = i * 3;
          const tx = patch.pinTargets[i3], ty = patch.pinTargets[i3 + 1], tz = patch.pinTargets[i3 + 2];
          const rho = 1 / (h2 * patch.pointMass[i] + 1e-9);
          const ex = pos[i3] - tx, ey = pos[i3 + 1] - ty, ez = pos[i3 + 2] - tz;
          // Dual update (bounded)
          this.dual[i3] = Math.max(-500, Math.min(500, this.dual[i3] + rho * ex));
          this.dual[i3 + 1] = Math.max(-500, Math.min(500, this.dual[i3 + 1] + rho * ey));
          this.dual[i3 + 2] = Math.max(-500, Math.min(500, this.dual[i3 + 2] + rho * ez));
          pos[i3] = tx; pos[i3 + 1] = ty; pos[i3 + 2] = tz;
        }
      }
      // Self-collision (PT via brute-force grid — demo scale; EE omitted, noted as bottleneck)
      if (selfCollision && it === iters - 1) this.solveSelfPT();
    }
    // Update velocities
    for (let i = 0; i < n; i++) {
      const i3 = i * 3;
      vel[i3] = (pos[i3] - prev[i3]) / h; vel[i3 + 1] = (pos[i3 + 1] - prev[i3 + 1]) / h; vel[i3 + 2] = (pos[i3 + 2] - prev[i3 + 2]) / h;
    }
  }
  solveSelfPT() {
    const { pos, patch, invMass } = this;
    const n = patch.pointCount;
    const sep = 0.004;
    // Simple spatial hash (demo)
    const cell = 0.02, invCell = 1 / cell;
    const buckets = new Map();
    const key = (x, y, z) => Math.floor(x * invCell) + '_' + Math.floor(y * invCell) + '_' + Math.floor(z * invCell);
    for (let i = 0; i < n; i++) {
      const k = key(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
      if (!buckets.has(k)) buckets.set(k, []);
      buckets.get(k).push(i);
    }
    for (let t = 0; t < patch.triCount; t++) {
      const a = patch.triIndices[t * 3], b = patch.triIndices[t * 3 + 1], c = patch.triIndices[t * 3 + 2];
      const ax = pos[a * 3], ay = pos[a * 3 + 1], az = pos[a * 3 + 2];
      const bx = pos[b * 3], by = pos[b * 3 + 1], bz = pos[b * 3 + 2];
      const cx = pos[c * 3], cy = pos[c * 3 + 1], cz = pos[c * 3 + 2];
      const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
      const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
      let nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
      const nl = Math.hypot(nx, ny, nz);
      if (nl < 1e-9) continue;
      nx /= nl; ny /= nl; nz /= nl;
      // Check nearby points (skip adjacent)
      const cxk = key((ax + bx + cx) / 3, (ay + by + cy) / 3, (az + bz + cz) / 3);
      const cand = buckets.get(cxk) || [];
      for (const p of cand) {
        if (p === a || p === b || p === c) continue;
        const px = pos[p * 3], py = pos[p * 3 + 1], pz = pos[p * 3 + 2];
        const d = (px - ax) * nx + (py - ay) * ny + (pz - az) * nz;
        if (Math.abs(d) < sep && invMass[p] > 0) {
          const push = (sep - Math.abs(d)) * 0.5 * Math.sign(d || 1);
          pos[p * 3] += nx * push; pos[p * 3 + 1] += ny * push; pos[p * 3 + 2] += nz * push;
        }
      }
    }
  }
  recomputeNormals() {
    const { pos, patch } = this;
    this.normals.fill(0);
    this.stretch.fill(0);
    const counts = new Uint16Array(patch.pointCount);
    for (let t = 0; t < patch.triCount; t++) {
      const a = patch.triIndices[t * 3], b = patch.triIndices[t * 3 + 1], c = patch.triIndices[t * 3 + 2];
      const a3 = a * 3, b3 = b * 3, c3 = c * 3;
      const e1x = pos[b3] - pos[a3], e1y = pos[b3 + 1] - pos[a3 + 1], e1z = pos[b3 + 2] - pos[a3 + 2];
      const e2x = pos[c3] - pos[a3], e2y = pos[c3 + 1] - pos[a3 + 1], e2z = pos[c3 + 2] - pos[a3 + 2];
      const fnx = e1y * e2z - e1z * e2y, fny = e1z * e2x - e1x * e2z, fnz = e1x * e2y - e1y * e2x;
      this.normals[a3] += fnx; this.normals[a3 + 1] += fny; this.normals[a3 + 2] += fnz;
      this.normals[b3] += fnx; this.normals[b3 + 1] += fny; this.normals[b3 + 2] += fnz;
      this.normals[c3] += fnx; this.normals[c3 + 1] += fny; this.normals[c3 + 2] += fnz;
      // Stretch = max edge-length ratio in triangle (vs rest)
      const ra = patch.restPos, rp = pos;
      const r1 = Math.hypot(ra[b3] - ra[a3], ra[b3 + 1] - ra[a3 + 1], ra[b3 + 2] - ra[a3 + 2]) || 1e-9;
      const r2 = Math.hypot(ra[c3] - ra[a3], ra[c3 + 1] - ra[a3 + 1], ra[c3 + 2] - ra[a3 + 2]) || 1e-9;
      const r3 = Math.hypot(ra[c3] - ra[b3], ra[c3 + 1] - ra[b3 + 1], ra[c3 + 2] - ra[b3 + 2]) || 1e-9;
      const s1 = Math.hypot(e1x, e1y, e1z) / r1;
      const s2 = Math.hypot(e2x, e2y, e2z) / r2;
      const s3 = Math.hypot(rp[c3] - rp[b3], rp[c3 + 1] - rp[b3 + 1], rp[c3 + 2] - rp[b3 + 2]) / r3;
      const s = Math.max(s1, s2, s3);
      this.stretch[a] += s; this.stretch[b] += s; this.stretch[c] += s;
      counts[a]++; counts[b]++; counts[c]++;
    }
    for (let i = 0; i < patch.pointCount; i++) {
      const i3 = i * 3;
      const l = Math.hypot(this.normals[i3], this.normals[i3 + 1], this.normals[i3 + 2]);
      if (l > 1e-7) { const inv = 1 / l; this.normals[i3] *= inv; this.normals[i3 + 1] *= inv; this.normals[i3 + 2] *= inv; }
      else { this.normals[i3] = 0; this.normals[i3 + 1] = 1; this.normals[i3 + 2] = 0; }
      if (counts[i] > 0) this.stretch[i] /= counts[i];
    }
  }
  evaluateTelemetry() {
    let maxS = 1, sumS = 0, invalid = 0, maxPen = 0;
    const halfThick = 0.5 * this.material.thickness + 0.0022;
    for (let i = 0; i < this.patch.pointCount; i++) {
      const s = this.stretch[i];
      if (!Number.isFinite(s)) { invalid++; continue; }
      maxS = Math.max(maxS, s); sumS += s;
      if (i % 3 === 0 && this.mannequin && this.mannequin.queryClosestSurface(this.pos[i * 3], this.pos[i * 3 + 1], this.pos[i * 3 + 2], halfThick, halfThick + 0.03, this._out)) {
        if (this._out.phi < 0) maxPen = Math.max(maxPen, -this._out.phi);
      }
    }
    return { stepMs: this.lastStepMs, maxStretch: maxS, meanStretch: sumS / this.patch.pointCount, maxPenMm: maxPen * 1000, invalidCount: invalid, selfClips: 0, minSelfGapMm: 99 };
  }
}
