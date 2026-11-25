/**
 * Terrain-equivalent erosion processes adapted to a true 3D SDF surface.
 *
 * Terrain Studio's live GPU model is a 2.5D heightfield (bedrock, sediment,
 * water, snow and chemistry textures). A free-standing rock/cliff asset has
 * overhangs, vertical faces and undersides, so those shader passes cannot be
 * pasted onto it unchanged. This suite preserves the same process laws in
 * the representation a rock actually needs:
 *
 *   Rain       -> existing capacity-limited droplet runoff on the SDF.
 *   Wind       -> saltating grains driven by a gusting, terrain-deflected
 *                 flow; head-on + tangential flank abrasion, lee deposition.
 *   Thermal    -> dry granular particles that only detach above repose and
 *                 deposit below it (rockfall/talus transport).
 *   Cryo       -> snow/melt particles settle into ledges and concavities,
 *                 then freeze-thaw expansion wedges grains loose.
 *   Chemical   -> water-residence dissolution in wet hollows and soluble
 *                 bands; the separate mesh stain pass colours the result.
 *   Slot flood -> for `slot-canyon` only: sediment-laden flash-flood packets
 *                 confined to the recorded canyon centreline (bedload +
 *                 suspended tools, dynamic cover, constriction acceleration,
 *                 helical secondary flow, expansion/eddy deposition), then
 *                 a lower-rate channelled-wind polish along the same slot.
 *
 * Every process uses a sub-voxel SDF brush, geological hardness, convex-edge
 * protection, thin-wall protection, and a per-node carve cap. There is no
 * blanket Laplacian smoothing, so high-resolution erosion incises instead of
 * turning the formation into melted chocolate.
 */

import { clamp, smoothstep } from '../core/vec3.js';
import { scalarField3D } from '../core/spatial-noise.js';
import {
  effectiveWindAzimuthDeg,
  normalizeWindAzimuthDeg,
  normalizeWindShiftMode,
  sampleWindDirectionSequenceDeg,
} from '../erosion/wind-regime.js';
import {
  erodeSdfWithRain,
  meanderTangentDirection3D,
  redistanceSdfNarrowBand,
  structuralRockWeakness3D,
} from './rain-erosion.js';

const BRUSH_R = 1.64;

function defaultParticleCount(sdf, baseAt192) {
  const maxDim = Math.max(sdf?.nx || 192, sdf?.ny || 192, sdf?.nz || 192);
  // Most particles sweep a physical strip whose width scales with h while
  // their path length in steps scales with 1/h. A 1.25 exponent maintains
  // coverage without the cubic total work a naive area-squared count causes.
  const scale = (maxDim / 192) ** 1.25;
  return Math.round(clamp(baseAt192 * scale, 600, 26000));
}

function createSdfProcessTool(sdf, options = {}) {
  const { nx, ny, nz, gx, gy, gz, strideY, strideZ } = sdf;
  const h = sdf.voxelSize;
  const ox = sdf.originX, oy = sdf.originY, oz = sdf.originZ;
  const values = sdf.values;
  const band = 3.0 * h;
  const maxCarveDepth = options.maxCarveDepth ?? h * 7;
  const carveTotals = new Map();

  const ensureNode = (ix, iy, iz) => {
    const idx = ix + iy * strideY + iz * strideZ;
    let v = values[idx];
    if (Number.isNaN(v)) {
      v = sdf.ensureSample ? sdf.ensureSample(ix, iy, iz) : h * 4;
      values[idx] = v;
    }
    return v;
  };

  // Materialize a small halo around any sparse/converted narrow band so
  // trilinear samples and brushes do not encounter NaN at brick boundaries.
  for (let pass = 0; pass < 2; pass++) {
    const toFill = [];
    for (let iz = 1; iz < gz - 1; iz++) {
      const zOff = iz * strideZ;
      for (let iy = 1; iy < gy - 1; iy++) {
        let idx = zOff + iy * strideY + 1;
        for (let ix = 1; ix < gx - 1; ix++, idx++) {
          if (!Number.isNaN(values[idx])) continue;
          if (
            !Number.isNaN(values[idx - 1]) || !Number.isNaN(values[idx + 1]) ||
            !Number.isNaN(values[idx - strideY]) || !Number.isNaN(values[idx + strideY]) ||
            !Number.isNaN(values[idx - strideZ]) || !Number.isNaN(values[idx + strideZ])
          ) toFill.push(ix, iy, iz);
        }
      }
    }
    for (let i = 0; i < toFill.length; i += 3) ensureNode(toFill[i], toFill[i + 1], toFill[i + 2]);
    if (!toFill.length) break;
  }

  const sg = new Float64Array(4); // phi, grad x/y/z (world gradient)
  function sampleGrad(wx, wy, wz) {
    const fx = clamp((wx - ox) / h, 0, nx - 1.001);
    const fy = clamp((wy - oy) / h, 0, ny - 1.001);
    const fz = clamp((wz - oz) / h, 0, nz - 1.001);
    const ix = Math.floor(fx), iy = Math.floor(fy), iz = Math.floor(fz);
    const tx = fx - ix, ty = fy - iy, tz = fz - iz;

    const c000 = ensureNode(ix, iy, iz);
    const c100 = ensureNode(ix + 1, iy, iz);
    const c010 = ensureNode(ix, iy + 1, iz);
    const c110 = ensureNode(ix + 1, iy + 1, iz);
    const c001 = ensureNode(ix, iy, iz + 1);
    const c101 = ensureNode(ix + 1, iy, iz + 1);
    const c011 = ensureNode(ix, iy + 1, iz + 1);
    const c111 = ensureNode(ix + 1, iy + 1, iz + 1);

    const x00 = c000 + (c100 - c000) * tx;
    const x10 = c010 + (c110 - c010) * tx;
    const x01 = c001 + (c101 - c001) * tx;
    const x11 = c011 + (c111 - c011) * tx;
    const y0 = x00 + (x10 - x00) * ty;
    const y1 = x01 + (x11 - x01) * ty;
    sg[0] = y0 + (y1 - y0) * tz;

    const dx00 = c100 - c000, dx10 = c110 - c010, dx01 = c101 - c001, dx11 = c111 - c011;
    sg[1] = ((dx00 + (dx10 - dx00) * ty) * (1 - tz) + (dx01 + (dx11 - dx01) * ty) * tz) / h;
    const dy00 = c010 - c000, dy10 = c110 - c100, dy01 = c011 - c001, dy11 = c111 - c101;
    sg[2] = ((dy00 + (dy10 - dy00) * tx) * (1 - tz) + (dy01 + (dy11 - dy01) * tx) * tz) / h;
    const dz00 = c001 - c000, dz10 = c101 - c100, dz01 = c011 - c010, dz11 = c111 - c110;
    sg[3] = ((dz00 + (dz01 - dz00) * ty) * (1 - tx) + (dz10 + (dz11 - dz10) * ty) * tx) / h;
    return sg;
  }

  function sampleOnly(wx, wy, wz) {
    sampleGrad(wx, wy, wz);
    return sg[0];
  }

  function laplacianAt(wx, wy, wz) {
    const ix = clamp(Math.round((wx - ox) / h), 1, nx - 1);
    const iy = clamp(Math.round((wy - oy) / h), 1, ny - 1);
    const iz = clamp(Math.round((wz - oz) / h), 1, nz - 1);
    const c = ensureNode(ix, iy, iz);
    const sum =
      ensureNode(ix - 1, iy, iz) + ensureNode(ix + 1, iy, iz) +
      ensureNode(ix, iy - 1, iz) + ensureNode(ix, iy + 1, iz) +
      ensureNode(ix, iy, iz - 1) + ensureNode(ix, iy, iz + 1);
    return (sum - 6 * c) / (h * h);
  }

  const brushNodes = [];
  function applyBrushAt(wx, wy, wz, delta) {
    if (!Number.isFinite(delta) || Math.abs(delta) <= 1e-12) return 0;
    brushNodes.length = 0;
    const cx = Math.round((wx - ox) / h);
    const cy = Math.round((wy - oy) / h);
    const cz = Math.round((wz - oz) / h);
    const R = Math.ceil(BRUSH_R);
    let wSum = 0;

    for (let jz = cz - R; jz <= cz + R; jz++) {
      if (jz < 1 || jz > nz - 1) continue;
      for (let jy = cy - R; jy <= cy + R; jy++) {
        if (jy < 1 || jy > ny - 1) continue;
        for (let jx = cx - R; jx <= cx + R; jx++) {
          if (jx < 1 || jx > nx - 1) continue;
          const dx = ox + jx * h - wx;
          const dy = oy + jy * h - wy;
          const dz = oz + jz * h - wz;
          const dist = Math.sqrt(dx * dx + dy * dy + dz * dz) / h;
          if (dist > BRUSH_R) continue;
          const idx = jx + jy * strideY + jz * strideZ;
          if (Number.isNaN(values[idx])) ensureNode(jx, jy, jz);
          const u = dist / BRUSH_R;
          const om = 1 - u * u;
          const w = om * om;
          brushNodes.push(idx, w);
          wSum += w;
        }
      }
    }
    if (wSum <= 1e-12) return 0;

    let applied = 0;
    for (let i = 0; i < brushNodes.length; i += 2) {
      const idx = brushNodes[i];
      const old = values[idx];
      if (Number.isNaN(old) || old < -band || old > band) continue;
      let step = delta * (brushNodes[i + 1] / wSum);
      if (step > 0) {
        const used = carveTotals.get(idx) || 0;
        const room = maxCarveDepth - used;
        if (room <= 0) continue;
        step = Math.min(step, room);
        carveTotals.set(idx, used + step);
      }
      const next = clamp(old + step, -band * 1.35, band * 1.35);
      values[idx] = next;
      applied += next - old;
    }
    return applied;
  }

  function protectionMultiplier(wx, wy, wz, nnx, nny, nnz, opts = {}) {
    const edgeProtection = clamp(opts.edgeProtection ?? 0.7, 0, 1);
    let hardness = 0.6;
    if (opts.hardnessAt) {
      const hv = opts.hardnessAt(wx, wy, wz);
      if (Number.isFinite(hv)) hardness = clamp(hv, 0, 1);
    }
    let m = 1 / (0.35 + 2.6 * hardness * hardness);

    const lap = laplacianAt(wx, wy, wz) * h;
    if (lap > 0.22) {
      const k = clamp((lap - 0.22) * 0.78, 0, 1);
      m *= Math.exp(-edgeProtection * 2.4 * k) * (1 - edgeProtection * 0.68 * k);
    } else if (lap < -0.10) {
      m *= 1 + 0.16 * clamp((-lap - 0.10) * 0.45, 0, 1);
    }
    if (opts.dipAngle !== undefined || opts.jointDensity !== undefined) {
      m *= structuralRockWeakness3D(wx, wy, wz, opts);
    }

    const behind = sampleOnly(wx - nnx * 3 * h, wy - nny * 3 * h, wz - nnz * 3 * h);
    if (behind > -1.6 * h) m *= 0.22;
    return m;
  }

  const projected = new Float64Array(3);
  function projectToSurface(wx, wy, wz) {
    let px = wx, py = wy, pz = wz;
    for (let i = 0; i < 2; i++) {
      sampleGrad(px, py, pz);
      const gl = Math.hypot(sg[1], sg[2], sg[3]);
      if (gl < 1e-9) break;
      px -= sg[1] / gl * sg[0];
      py -= sg[2] / gl * sg[0];
      pz -= sg[3] / gl * sg[0];
    }
    projected[0] = px; projected[1] = py; projected[2] = pz;
    return projected;
  }

  const minX = ox + 1.5 * h, maxX = ox + (nx - 1.5) * h;
  const minY = oy + 1.5 * h, maxY = oy + (ny - 1.5) * h;
  const minZ = oz + 1.5 * h, maxZ = oz + (nz - 1.5) * h;
  const insideInner = (x, y, z) => x >= minX && x <= maxX && y >= minY && y <= maxY && z >= minZ && z <= maxZ;

  const hit = new Float64Array(3);
  function traceRay(px, py, pz, dx, dy, dz, maxSteps = 360) {
    const dl = Math.hypot(dx, dy, dz);
    if (dl < 1e-12) return false;
    dx /= dl; dy /= dl; dz /= dl;
    for (let i = 0; i < maxSteps; i++) {
      if (!insideInner(px, py, pz)) return false;
      const phi = sampleOnly(px, py, pz);
      if (phi <= 0.30 * h) {
        hit[0] = px; hit[1] = py; hit[2] = pz;
        return true;
      }
      const step = clamp(phi * 0.85, 0.55 * h, 4.0 * h);
      px += dx * step; py += dy * step; pz += dz * step;
    }
    return false;
  }

  function traceDown(x, z) {
    return traceRay(x, maxY - h, z, 0, -1, 0, 420);
  }

  return {
    h, ox, oy, oz, nx, ny, nz,
    minX, maxX, minY, maxY, minZ, maxZ,
    sampleGrad, sampleOnly, laplacianAt,
    applyBrushAt, protectionMultiplier, projectToSurface,
    traceRay, traceDown, insideInner, hit, sg,
  };
}

function downhillFromNormal(nnx, nny, nnz, out) {
  // Project gravity (0,-1,0) onto the tangent plane.
  const gDotN = -nny;
  let sx = -nnx * gDotN;
  let sy = -1 - nny * gDotN;
  let sz = -nnz * gDotN;
  const len = Math.hypot(sx, sy, sz);
  if (len < 1e-10) {
    out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
    return out;
  }
  out[0] = sx / len; out[1] = sy / len; out[2] = sz / len; out[3] = len;
  return out;
}

/** Wind saltation/abrasion with gust wander, obstacle wrap and lee shelter. */
export function erodeSdfWithWind(sdf, options = {}) {
  const t0 = performance.now();
  const amount = clamp(options.amount ?? 0, 0, 1);
  const stats = {
    particlesRequested: 0, particles: 0, steps: 0,
    erodedVolume: 0, depositedVolume: 0, abrasionEvents: 0, depositionEvents: 0,
    elapsedMs: 0,
  };
  if (!sdf || amount <= 0) {
    stats.elapsedMs = Math.round(performance.now() - t0);
    return stats;
  }

  const particleCount = Math.max(0, Math.round(options.particleCount ?? defaultParticleCount(sdf, 7000)));
  stats.particlesRequested = particleCount;
  const prevailingAzimuthDeg = options.azimuthDeg ?? options.windAzimuth ?? 35;
  const windRegime = {
    windAzimuth: prevailingAzimuthDeg,
    windShiftMode: options.shiftMode ?? options.windShiftMode ?? options.windDirectionMode ?? 'fixed',
    windShiftRange: options.shiftRangeDeg ?? options.windShiftRange ?? options.windShiftRangeDeg ?? 55,
    windShiftSectors: options.shiftSectors ?? options.windShiftSectors ?? 5,
    windShiftEpisodes: options.shiftEpisodes ?? options.windShiftEpisodes,
    seed: options.seedSalt ?? options.seed ?? 0,
  };
  const shiftMode = normalizeWindShiftMode(windRegime.windShiftMode);
  const shiftCycles = clamp(
    Number(options.shiftCycles ?? options.windShiftCycles ?? (shiftMode === 'fixed' ? 0 : 1)),
    0,
    12
  );
  stats.directionMode = shiftMode;
  stats.directionRangeDeg = clamp(Number(windRegime.windShiftRange) || 0, 0, 180);
  stats.directionCycles = shiftCycles;
  // Report actual weather-spell centres and frontal-transition midpoints.
  // This makes the HUD/CLI path match the continuous directions used by
  // the particles below, including bearings eroded through during a shift.
  stats.directionSamplesDeg = sampleWindDirectionSequenceDeg(windRegime, shiftCycles, 17)
    .map((deg) => Number(deg.toFixed(2)));
  // Actual particle-time distribution in eight sectors relative to the
  // prevailing bearing. This is the regression signal for the old complaint
  // that one side received systematically more wind than the other.
  stats.directionSectorOffsetsDeg = [-180, -135, -90, -45, 0, 45, 90, 135];
  stats.directionSectorCounts = new Array(8).fill(0);
  stats.directionSectorShares = new Array(8).fill(0);
  const turbulence = clamp(options.turbulence ?? 0.45, 0, 1);
  const streamlining = clamp(options.streamlining ?? options.windStreamlining ?? 0.65, 0, 1);
  const sandSupply = clamp(options.sandSupply ?? 0.65, 0, 1);
  const fluting = clamp(options.fluting ?? 0.65, 0, 1);
  const curtainHeight = clamp(options.curtainHeight ?? 0.42, 0.03, 1);
  const baseSpeed = clamp(options.speed ?? 0.62, 0.05, 1.5);
  const edgeProtection = clamp(options.edgeProtection ?? 0.7, 0, 1);
  const groundY = options.groundY ?? -Infinity;
  const rng = options.rng;
  const rand = () => (rng ? rng.float() : Math.random());
  const onProgress = options.onProgress || null;
  const seedSalt = (options.seedSalt ?? 0x51ab3d) | 0;

  const tool = createSdfProcessTool(sdf, { maxCarveDepth: sdf.voxelSize * (2.0 + 8.0 * amount) });
  const preWindValues = new Float32Array(sdf.values);
  const { h } = tool;
  const centerX = tool.ox + tool.nx * h * 0.5;
  const centerY = tool.oy + tool.ny * h * 0.5;
  const centerZ = tool.oz + tool.nz * h * 0.5;
  const spanX = tool.nx * h, spanY = tool.ny * h, spanZ = tool.nz * h;
  const surfaceMinX = sdf.meshBounds?.minX ?? tool.minX;
  const surfaceMaxX = sdf.meshBounds?.maxX ?? tool.maxX;
  const surfaceMinY = sdf.meshBounds?.minY ?? (Number.isFinite(groundY) ? groundY : tool.minY);
  const surfaceMaxY = sdf.meshBounds?.maxY ?? tool.maxY;
  const surfaceMinZ = sdf.meshBounds?.minZ ?? tool.minZ;
  const surfaceMaxZ = sdf.meshBounds?.maxZ ?? tool.maxZ;
  const surfaceHeight = Math.max(h, surfaceMaxY - surfaceMinY);
  const surfaceAlongSpanFor = (windX, windZ) =>
    Math.abs((surfaceMaxX - surfaceMinX) * windX) + Math.abs((surfaceMaxZ - surfaceMinZ) * windZ);
  const maxSteps = Math.round(clamp(Math.max(tool.nx, tool.ny, tool.nz) * 2.0, 220, 850));
  const reportEvery = Math.max(1, Math.floor(particleCount / 12));

  for (let pIdx = 0; pIdx < particleCount; pIdx++) {
    if (onProgress && pIdx % reportEvery === 0) onProgress(pIdx / Math.max(1, particleCount));

    // Particle index is geologic time in an offline bake: later grains
    // belong to later weather. A grain keeps one regime direction for its
    // short saltation life, while thousands of grains sample the continuous
    // weather-spell/frontal-transition trajectory across the bake.
    const regimeFraction = particleCount > 1 ? pIdx / (particleCount - 1) : 0;
    const particleAzimuthDeg = effectiveWindAzimuthDeg(windRegime, regimeFraction * shiftCycles);
    let relativeAzimuthDeg = normalizeWindAzimuthDeg(particleAzimuthDeg - prevailingAzimuthDeg);
    if (relativeAzimuthDeg > 180) relativeAzimuthDeg -= 360;
    const sectorIndex = Math.floor((((relativeAzimuthDeg + 202.5) % 360) + 360) % 360 / 45) % 8;
    stats.directionSectorCounts[sectorIndex]++;
    const azimuthRad = particleAzimuthDeg * Math.PI / 180;
    const baseWindX = Math.cos(azimuthRad), baseWindZ = Math.sin(azimuthRad);
    const perpX = -baseWindZ, perpZ = baseWindX;
    const surfaceAlongSpan = surfaceAlongSpanFor(baseWindX, baseWindZ);

    const gust = 0.72 + rand() * 0.72;
    const initialYaw = azimuthRad + (rand() - 0.5) * turbulence * 0.38;
    let vx = Math.cos(initialYaw), vy = (rand() - 0.5) * turbulence * 0.08, vz = Math.sin(initialYaw);
    const vl0 = Math.hypot(vx, vy, vz) || 1;
    vx /= vl0; vy /= vl0; vz /= vl0;

    // Pick a target point inside the asset bounds, then back-cast along
    // -wind to the SDF domain boundary. Sampling a rectangle by projected
    // half-extents places many diagonal-wind starts outside the domain.
    const targetX = surfaceMinX + rand() * Math.max(0, surfaceMaxX - surfaceMinX);
    const targetZ = surfaceMinZ + rand() * Math.max(0, surfaceMaxZ - surfaceMinZ);
    const sy0 = clamp(surfaceMinY + rand() * surfaceHeight, tool.minY + h, tool.maxY - h);
    let back = Infinity;
    if (Math.abs(baseWindX) > 1e-9) {
      back = Math.min(back, baseWindX > 0
        ? (targetX - tool.minX) / baseWindX
        : (tool.maxX - targetX) / -baseWindX);
    }
    if (Math.abs(baseWindZ) > 1e-9) {
      back = Math.min(back, baseWindZ > 0
        ? (targetZ - tool.minZ) / baseWindZ
        : (tool.maxZ - targetZ) / -baseWindZ);
    }
    if (!Number.isFinite(back) || back <= 0) continue;
    const startBack = Math.max(0, back - Math.min(2 * h, back * 0.05));
    const sx0 = targetX - baseWindX * startBack;
    const sz0 = targetZ - baseWindZ * startBack;
    if (!tool.traceRay(sx0, sy0, sz0, vx, vy, vz, 480)) continue;

    let px = tool.hit[0], py = tool.hit[1], pz = tool.hit[2];
    const pr = tool.projectToSurface(px, py, pz);
    px = pr[0]; py = pr[1]; pz = pr[2];

    stats.particles++;
    let speed = baseSpeed * gust;
    let sediment = h * (0.04 + 0.20 * sandSupply) * gust;
    let attached = true;
    let maxAlong = px * baseWindX + pz * baseWindZ;

    for (let step = 0; step < maxSteps; step++) {
      if (!tool.insideInner(px, py, pz)) break;
      stats.steps++;
      tool.sampleGrad(px, py, pz);
      const phi = tool.sg[0];
      let gx = tool.sg[1], gy = tool.sg[2], gz = tool.sg[3];
      const gl = Math.hypot(gx, gy, gz);
      if (gl < 1e-9) break;
      const nnx = gx / gl, nny = gy / gl, nnz = gz / gl;
      const alongPos = px * baseWindX + pz * baseWindZ;
      maxAlong = Math.max(maxAlong, alongPos);
      // A saltating grain should leave in the wake, never orbit a closed
      // rock back onto its own windward face.
      if (alongPos < maxAlong - surfaceAlongSpan * 0.38) break;
      let detachAfterExchange = false;

      // Gusting target flow. scalarField3D is a flow-wander field here,
      // not a shape/colour texture.
      const yawNoise = scalarField3D(px * 0.16 + 17.3, py * 0.12 - 4.1, pz * 0.16 + 9.7, seedSalt);
      const pitchNoise = scalarField3D(px * 0.11 - 8.2, py * 0.15 + 3.4, pz * 0.11 - 12.8, seedSalt ^ 0x27d4eb2d);
      const yaw = azimuthRad + yawNoise * turbulence * 0.58;
      let tx = Math.cos(yaw), ty = pitchNoise * turbulence * 0.12, tz = Math.sin(yaw);
      vx += (tx - vx) * 0.22; vy += (ty - vy) * 0.22; vz += (tz - vz) * 0.22;

      // Obstacle wrap: remove into-solid motion close to the surface.
      if (attached && phi < 4.5 * h) {
        const dotVN = vx * nnx + vy * nny + vz * nnz;
        if (dotVN < 0) {
          const remove = dotVN * (0.25 + 0.72 * streamlining);
          vx -= nnx * remove; vy -= nny * remove; vz -= nnz * remove;
        }
      }
      let vl = Math.hypot(vx, vy, vz);
      if (vl < 0.22) {
        // A head-on gust stagnates at the face; split around the side the
        // particle is already offset towards, as streamlines do.
        const side = ((px - centerX) * perpX + (pz - centerZ) * perpZ) >= 0 ? 1 : -1;
        vx = perpX * side; vy = pitchNoise * 0.30; vz = perpZ * side;
        vl = Math.hypot(vx, vy, vz) || 1;
      }
      vx /= vl; vy /= vl; vz /= vl;

      if (attached && Math.abs(phi) <= 1.35 * h) {
        const windDotN = nnx * baseWindX + nnz * baseWindZ;
        const headOn = clamp(-windDotN, 0, 1);
        const tangential = Math.sqrt(Math.max(0, 1 - windDotN * windDotN));
        const steepness = Math.sqrt(Math.max(0, 1 - nny * nny));
        const exposure = Math.pow(headOn, 1.3) + streamlining * 0.55 * tangential * (0.25 + 0.75 * steepness);

        let shelter = 0;
        for (const back of [2, 5, 9]) {
          const upPhi = tool.sampleOnly(px - baseWindX * back * h, py, pz - baseWindZ * back * h);
          shelter = Math.max(shelter, 1 - smoothstep(-0.5 * h, 1.6 * h, upPhi));
        }

        if (windDotN > 0.20 && shelter > 0.42 && rand() < 0.22) detachAfterExchange = true;

        const speedTarget = baseSpeed * gust * (1 - shelter * 0.55) *
          (1 + streamlining * 0.38 * tangential * steepness);
        speed += (speedTarget - speed) * 0.20;
        speed = clamp(speed, 0.06, 2.2);

        const capacity = h * (0.08 + 0.50 * amount) * speed * (0.22 + exposure);
        const lee = shelter > 0.30 || windDotN > 0.14;
        const canHold = steepness < 0.80 && nny > -0.35;

        if (lee && canHold && sediment > capacity * 0.45) {
          const dep = Math.min(sediment, sediment * (0.10 + 0.30 * shelter));
          if (dep > 1e-9) {
            const applied = tool.applyBrushAt(px, py, pz, -dep);
            stats.depositedVolume -= applied * h * h;
            sediment = Math.max(0, sediment + applied);
            stats.depositionEvents++;
          }
        } else if (exposure > 0.035 && sediment < capacity) {
          const along = px * baseWindX + pz * baseWindZ;
          const acrossCoord = px * perpX + pz * perpZ;
          // 3D domain-warped aeolian pocket/scallop variation (avoids straight parallel ruler streaks!)
          const aeolianWarp = scalarField3D(px * 0.34, py * 0.42, pz * 0.34, seedSalt ^ 0x9e3779b9);
          const pocket = scalarField3D(
            acrossCoord * 0.64 + aeolianWarp * 0.95,
            py * 0.72 - aeolianWarp * 0.62,
            along * 0.54 + aeolianWarp * 0.80,
            seedSalt ^ 0x5bd1e995
          );
          const fluteFactor = 1 + fluting * 0.42 * pocket;
          const heightN = clamp((py - surfaceMinY) / surfaceHeight, 0, 1);
          const curtainBoost = 1 + 0.80 * (1 - smoothstep(0, curtainHeight, heightN));
          const protection = tool.protectionMultiplier(px, py, pz, nnx, nny, nnz, {
            edgeProtection, hardnessAt: options.hardnessAt,
          });
          let erode = h * 0.16 * (0.20 + amount) * exposure * speed * fluteFactor * curtainBoost *
            (0.30 + 0.70 * sandSupply) * protection;
          erode = Math.min(erode, Math.max(0, capacity - sediment));
          if (erode > 1e-9) {
            const applied = tool.applyBrushAt(px, py, pz, erode);
            if (applied > 0) {
              stats.erodedVolume += applied * h * h;
              sediment += applied;
              stats.abrasionEvents++;
            }
          }
        }
      }

      if (py <= groundY + 0.55 * h) {
        if (sediment > 1e-9) {
          const applied = tool.applyBrushAt(px, py, pz, -sediment);
          stats.depositedVolume -= applied * h * h;
        }
        break;
      }

      if (detachAfterExchange) {
        attached = false;
        vx = baseWindX + nnx * 0.28;
        vy = vy * 0.35 + nny * 0.10;
        vz = baseWindZ + nnz * 0.28;
        const dl = Math.hypot(vx, vy, vz) || 1;
        vx /= dl; vy /= dl; vz /= dl;
      }

      const stepLen = 0.8 * h * clamp(speed, 0.55, 1.65) * (attached ? 1 : 2.4);
      let qx = px + vx * stepLen;
      let qy = py + vy * stepLen - (phi > 1.5 * h ? 0.025 * h : 0);
      let qz = pz + vz * stepLen;
      if (!tool.insideInner(qx, qy, qz)) break;
      const qPhi = tool.sampleOnly(qx, qy, qz);
      if (attached && Math.abs(qPhi) < 2.6 * h) {
        const qp = tool.projectToSurface(qx, qy, qz);
        qx = qp[0]; qy = qp[1]; qz = qp[2];
      } else if (!attached && qPhi <= 0.32 * h) {
        // Ballistic wake grains only reattach on a genuine new impact,
        // not merely because their trajectory passes near another face.
        const qp = tool.projectToSurface(qx, qy, qz);
        qx = qp[0]; qy = qp[1]; qz = qp[2];
        attached = true;
      }
      px = qx; py = qy; pz = qz;
    }
  }

  if (stats.erodedVolume > 0 || stats.depositedVolume > 0) {
    redistanceSdfNarrowBand(sdf, preWindValues, 3.0, 2);
  }
  if (onProgress) onProgress(1);
  stats.erodedVolume = Math.max(0, stats.erodedVolume);
  stats.depositedVolume = Math.max(0, stats.depositedVolume);
  stats.directionSectorShares = stats.directionSectorCounts.map((count) =>
    Number((count / Math.max(1, particleCount)).toFixed(4)));
  stats.elapsedMs = Math.round(performance.now() - t0);
  return stats;
}

/** Dry granular/rockfall transport gated by joint planes, overhangs, and talus angle of repose. */
export function erodeSdfWithThermalTalus(sdf, options = {}) {
  const t0 = performance.now();
  const amount = clamp(options.amount ?? 0, 0, 1);
  const stats = {
    particlesRequested: 0, particles: 0, steps: 0,
    erodedVolume: 0, depositedVolume: 0, elapsedMs: 0,
  };
  if (!sdf || amount <= 0) {
    stats.elapsedMs = Math.round(performance.now() - t0);
    return stats;
  }

  const particleCount = Math.max(0, Math.round(options.particleCount ?? defaultParticleCount(sdf, 5500)));
  stats.particlesRequested = particleCount;
  const reposeSin = Math.sin(clamp(options.reposeAngleDeg ?? 34, 8, 70) * Math.PI / 180);
  const edgeProtection = clamp(options.edgeProtection ?? 0.7, 0, 1);
  const groundY = options.groundY ?? -Infinity;
  const rng = options.rng;
  const rand = () => (rng ? rng.float() : Math.random());
  const onProgress = options.onProgress || null;
  const tool = createSdfProcessTool(sdf, { maxCarveDepth: sdf.voxelSize * (1.5 + 5.2 * amount) });
  const preTalusValues = new Float32Array(sdf.values);
  const { h } = tool;
  const maxSteps = Math.round(clamp(Math.max(tool.nx, tool.ny, tool.nz) * 2.1, 200, 850));
  const reportEvery = Math.max(1, Math.floor(particleCount / 12));
  const down = new Float64Array(4);

  for (let pIdx = 0; pIdx < particleCount; pIdx++) {
    if (onProgress && pIdx % reportEvery === 0) onProgress(pIdx / Math.max(1, particleCount));

    let px = 0, py = 0, pz = 0, hit = false;
    for (let attempt = 0; attempt < 3; attempt++) {
      const sx = tool.minX + rand() * (tool.maxX - tool.minX);
      const sz = tool.minZ + rand() * (tool.maxZ - tool.minZ);
      if (!tool.traceDown(sx, sz)) continue;
      const pr = tool.projectToSurface(tool.hit[0], tool.hit[1], tool.hit[2]);
      px = pr[0]; py = pr[1]; pz = pr[2];
      tool.sampleGrad(px, py, pz);
      const gl = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
      if (gl < 1e-9) continue;
      const nny = tool.sg[2] / gl;
      if (nny < -0.45) continue;
      const slopeSin = Math.sqrt(Math.max(0, 1 - nny * nny));
      hit = true;
      if (slopeSin > reposeSin * 0.55 || attempt === 2) break;
    }
    if (!hit) continue;

    stats.particles++;
    let dirX = 0, dirY = 0, dirZ = 0;
    let speed = 0.35;
    let load = 0;

    for (let step = 0; step < maxSteps; step++) {
      stats.steps++;
      tool.sampleGrad(px, py, pz);
      const gl = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
      if (gl < 1e-9) break;
      const nnx = tool.sg[1] / gl, nny = tool.sg[2] / gl, nnz = tool.sg[3] / gl;
      downhillFromNormal(nnx, nny, nnz, down);
      const slopeSin = down[3];

      if (slopeSin > reposeSin) {
        // Competent vertical cliff faces stand well above granular repose!
        // Spalling concentrates on overhangs (nny < 0) and fractured joint/bedding seams
        // rather than uniformly melting solid 80-deg bedrock walls into a 34-deg sand cone.
        const overhangGate = clamp(-nny * 2.4, 0, 1);
        const structWeak = structuralRockWeakness3D(px, py, pz, options);
        const jointSpallGate = clamp((structWeak - 0.82) * 1.65, 0.16, 1.35);
        const rockSpallFactor = Math.max(overhangGate, jointSpallGate * (0.45 + 0.55 * overhangGate));
        const capacity = (slopeSin - reposeSin) * speed * h * 2.2 * (0.35 + amount) * rockSpallFactor;
        if (load < capacity) {
          const protection = tool.protectionMultiplier(px, py, pz, nnx, nny, nnz, {
            edgeProtection: Math.max(edgeProtection, 0.65),
            hardnessAt: options.hardnessAt,
            dipAngle: options.dipAngle,
            strikeAngle: options.strikeAngle,
            jointDensity: options.jointDensity,
            cleavageSharpness: options.cleavageSharpness,
          });
          const erode = Math.min(capacity - load, h * 0.11 * (0.25 + amount) * protection * rockSpallFactor);
          if (erode > 1e-9) {
            const applied = tool.applyBrushAt(px, py, pz, erode);
            if (applied > 0) {
              stats.erodedVolume += applied * h * h;
              load += applied;
            }
          }
        }
      } else if (load > 1e-9 && slopeSin < reposeSin * 0.94 && nny > -0.1) {
        const dep = slopeSin < 0.20 ? load : load * (0.28 + 0.48 * amount);
        const applied = tool.applyBrushAt(px, py, pz, -dep);
        stats.depositedVolume -= applied * h * h;
        load = Math.max(0, load + applied);
        if (slopeSin < 0.05) break;
      }

      if (slopeSin < 0.015) break;
      const talusMeander = new Float64Array(3);
      meanderTangentDirection3D(
        px, py, pz,
        nnx, nny, nnz,
        down[0], down[1], down[2],
        slopeSin,
        (options.seedSalt ?? options.seed ?? 0x71e2a9) | 0,
        (rand() - 0.5) * 0.28,
        talusMeander
      );
      let mx = talusMeander[0] * 0.82 + dirX * 0.18;
      let my = talusMeander[1] * 0.82 + dirY * 0.18;
      let mz = talusMeander[2] * 0.82 + dirZ * 0.18;
      const mdn = mx * nnx + my * nny + mz * nnz;
      mx -= nnx * mdn; my -= nny * mdn; mz -= nnz * mdn;
      const ml = Math.hypot(mx, my, mz);
      if (ml < 1e-9) break;
      mx /= ml; my /= ml; mz /= ml;
      dirX = mx; dirY = my; dirZ = mz;

      const oldY = py;
      let qx = px + mx * 0.8 * h;
      let qy = py + my * 0.8 * h;
      let qz = pz + mz * 0.8 * h;
      if (!tool.insideInner(qx, qy, qz)) break;
      if (qy <= groundY + 0.55 * h) {
        if (load > 1e-9) {
          const applied = tool.applyBrushAt(px, py, pz, -load);
          stats.depositedVolume -= applied * h * h;
        }
        break;
      }
      const qp = tool.projectToSurface(qx, qy, qz);
      qx = qp[0]; qy = qp[1]; qz = qp[2];
      const dh = oldY - qy;
      speed = clamp(Math.sqrt(Math.max(0.003, speed * speed + Math.max(dh, -0.4 * h) * 2.6)), 0.05, 5.0);
      px = qx; py = qy; pz = qz;
    }
  }

  if (stats.erodedVolume > 0 || stats.depositedVolume > 0) {
    redistanceSdfNarrowBand(sdf, preTalusValues, 3.0, 2);
  }
  if (onProgress) onProgress(1);
  stats.erodedVolume = Math.max(0, stats.erodedVolume);
  stats.depositedVolume = Math.max(0, stats.depositedVolume);
  stats.elapsedMs = Math.round(performance.now() - t0);
  return stats;
}

/** Freeze-thaw wedging fed by snow/melt particles settling in concavities. */
export function erodeSdfWithFreezeThaw(sdf, options = {}) {
  const t0 = performance.now();
  const amount = clamp(options.amount ?? 0, 0, 1);
  const stats = {
    particlesRequested: 0, particles: 0, steps: 0, frostEvents: 0,
    erodedVolume: 0, depositedVolume: 0, elapsedMs: 0,
  };
  if (!sdf || amount <= 0) {
    stats.elapsedMs = Math.round(performance.now() - t0);
    return stats;
  }

  const particleCount = Math.max(0, Math.round(options.particleCount ?? defaultParticleCount(sdf, 4500)));
  stats.particlesRequested = particleCount;
  const rng = options.rng;
  const rand = () => (rng ? rng.float() : Math.random());
  const onProgress = options.onProgress || null;
  const groundY = options.groundY ?? -Infinity;
  const seedSalt = (options.seedSalt ?? 0x2f6e1b) | 0;
  const tool = createSdfProcessTool(sdf, { maxCarveDepth: sdf.voxelSize * (1.6 + 5.5 * amount) });
  const preCryoValues = new Float32Array(sdf.values);
  const { h } = tool;
  const reportEvery = Math.max(1, Math.floor(particleCount / 12));
  const down = new Float64Array(4);

  const cryoMeander = new Float64Array(3);
  const moveDownhill = (state) => {
    tool.sampleGrad(state.x, state.y, state.z);
    const gl = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
    if (gl < 1e-9) return false;
    const nnx = tool.sg[1] / gl, nny = tool.sg[2] / gl, nnz = tool.sg[3] / gl;
    downhillFromNormal(nnx, nny, nnz, down);
    if (down[3] < 0.015) return false;
    meanderTangentDirection3D(
      state.x, state.y, state.z,
      nnx, nny, nnz,
      down[0], down[1], down[2],
      down[3],
      seedSalt,
      (rand() - 0.5) * 0.22,
      cryoMeander
    );
    const qx = state.x + cryoMeander[0] * 0.8 * h;
    const qy = state.y + cryoMeander[1] * 0.8 * h;
    const qz = state.z + cryoMeander[2] * 0.8 * h;
    if (!tool.insideInner(qx, qy, qz)) return false;
    const qp = tool.projectToSurface(qx, qy, qz);
    state.x = qp[0]; state.y = qp[1]; state.z = qp[2];
    return true;
  };

  for (let pIdx = 0; pIdx < particleCount; pIdx++) {
    if (onProgress && pIdx % reportEvery === 0) onProgress(pIdx / Math.max(1, particleCount));
    const sx = tool.minX + rand() * (tool.maxX - tool.minX);
    const sz = tool.minZ + rand() * (tool.maxZ - tool.minZ);
    if (!tool.traceDown(sx, sz)) continue;
    const pr = tool.projectToSurface(tool.hit[0], tool.hit[1], tool.hit[2]);
    const state = { x: pr[0], y: pr[1], z: pr[2] };
    if (state.y <= groundY + 0.5 * h) continue;

    stats.particles++;
    // Meltwater/snow creeps until a ledge or a water-trapping concavity.
    for (let s = 0; s < 120; s++) {
      stats.steps++;
      tool.sampleGrad(state.x, state.y, state.z);
      const gl = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
      if (gl < 1e-9) break;
      const nny = tool.sg[2] / gl;
      const slopeSin = Math.sqrt(Math.max(0, 1 - nny * nny));
      const concave = clamp(-tool.laplacianAt(state.x, state.y, state.z) * h * 0.75, 0, 1);
      if ((slopeSin < 0.34 && nny > 0.05) || concave > 0.48 || state.y <= groundY + 0.6 * h) break;
      if (!moveDownhill(state)) break;
    }

    // Repeated expansion in a crack sheet (noise zero-crossing), amplified
    // where water actually ponds in concave rock.
    let loose = 0;
    const cycles = 1 + Math.round(amount * 2);
    for (let cycle = 0; cycle < cycles; cycle++) {
      tool.sampleGrad(state.x, state.y, state.z);
      const gl = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
      if (gl < 1e-9) break;
      const nnx = tool.sg[1] / gl, nny = tool.sg[2] / gl, nnz = tool.sg[3] / gl;
      if (nny < -0.35) break;
      const concave = clamp(-tool.laplacianAt(state.x, state.y, state.z) * h * 0.80, 0, 1);
      const sheetNoise = scalarField3D(state.x * 0.55, state.y * 0.55, state.z * 0.55, seedSalt);
      const crack = 1 - smoothstep(0.015, 0.26, Math.abs(sheetNoise));
      const structJoint = structuralRockWeakness3D(state.x, state.y, state.z, options);
      const protection = tool.protectionMultiplier(state.x, state.y, state.z, nnx, nny, nnz, {
        edgeProtection: options.edgeProtection ?? 0.7, hardnessAt: options.hardnessAt,
      });
      const delta = h * (0.045 + 0.28 * amount) * (0.25 + 0.75 * concave) * (0.20 + 0.80 * crack) * structJoint * protection;
      const applied = tool.applyBrushAt(state.x, state.y, state.z, delta);
      if (applied > 0) {
        stats.erodedVolume += applied * h * h;
        loose += applied;
      }
    }
    stats.frostEvents++;

    // Loosened grains wash a short distance and lodge below the crack.
    for (let s = 0; s < 36 && loose > 1e-9; s++) {
      stats.steps++;
      tool.sampleGrad(state.x, state.y, state.z);
      const gl = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
      if (gl < 1e-9) break;
      const nny = tool.sg[2] / gl;
      const slopeSin = Math.sqrt(Math.max(0, 1 - nny * nny));
      if (slopeSin < 0.48 || state.y <= groundY + 0.6 * h) {
        const applied = tool.applyBrushAt(state.x, state.y, state.z, -loose * 0.45);
        stats.depositedVolume -= applied * h * h;
        loose = Math.max(0, loose + applied);
        if (slopeSin < 0.20) break;
      }
      if (!moveDownhill(state)) break;
    }
  }

  if (stats.erodedVolume > 0 || stats.depositedVolume > 0) {
    redistanceSdfNarrowBand(sdf, preCryoValues, 3.0, 2);
  }
  if (onProgress) onProgress(1);
  stats.erodedVolume = Math.max(0, stats.erodedVolume);
  stats.depositedVolume = Math.max(0, stats.depositedVolume);
  stats.elapsedMs = Math.round(performance.now() - t0);
  return stats;
}

/** Chemical dissolution along actual wet runoff/karren runnels and soluble beds. */
export function erodeSdfWithChemicalDissolution(sdf, options = {}) {
  const t0 = performance.now();
  const amount = clamp(options.amount ?? 0, 0, 1);
  const stats = {
    particlesRequested: 0, particles: 0, steps: 0,
    dissolvedVolume: 0, erodedVolume: 0, elapsedMs: 0,
  };
  if (!sdf || amount <= 0) {
    stats.elapsedMs = Math.round(performance.now() - t0);
    return stats;
  }

  const particleCount = Math.max(0, Math.round(options.particleCount ?? defaultParticleCount(sdf, 5500)));
  stats.particlesRequested = particleCount;
  const rng = options.rng;
  const rand = () => (rng ? rng.float() : Math.random());
  const onProgress = options.onProgress || null;
  const groundY = options.groundY ?? -Infinity;
  const seedSalt = (options.seedSalt ?? 0x73c19a) | 0;
  const tool = createSdfProcessTool(sdf, { maxCarveDepth: sdf.voxelSize * (1.4 + 4.6 * amount) });
  const preChemValues = new Float32Array(sdf.values);
  const { h } = tool;
  const maxSteps = Math.round(clamp(Math.max(tool.nx, tool.ny, tool.nz) * 1.8, 180, 750));
  const reportEvery = Math.max(1, Math.floor(particleCount / 12));
  const down = new Float64Array(4);

  for (let pIdx = 0; pIdx < particleCount; pIdx++) {
    if (onProgress && pIdx % reportEvery === 0) onProgress(pIdx / Math.max(1, particleCount));
    const sx = tool.minX + rand() * (tool.maxX - tool.minX);
    const sz = tool.minZ + rand() * (tool.maxZ - tool.minZ);
    if (!tool.traceDown(sx, sz)) continue;
    const pr = tool.projectToSurface(tool.hit[0], tool.hit[1], tool.hit[2]);
    let px = pr[0], py = pr[1], pz = pr[2];
    if (py <= groundY + 0.5 * h) continue;

    stats.particles++;
    let water = 1;
    for (let step = 0; step < maxSteps; step++) {
      stats.steps++;
      tool.sampleGrad(px, py, pz);
      const gl = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
      if (gl < 1e-9) break;
      const nnx = tool.sg[1] / gl, nny = tool.sg[2] / gl, nnz = tool.sg[3] / gl;
      if (nny < -0.35) break;
      downhillFromNormal(nnx, nny, nnz, down);
      const slopeSin = down[3];
      const lap = tool.laplacianAt(px, py, pz) * h;
      const concave = clamp(-lap * 0.80, 0, 1);
      // Karren runnels etch steep/concave seepage paths and recessed soluble beds,
      // while flat caprock tops only deepen inside genuine concave kamenitza hollows.
      const runnelFocus = 0.35 + 0.65 * Math.max(concave, slopeSin * 0.72);
      const mineral = scalarField3D(px * 0.38, py * 1.15, pz * 0.38, seedSalt);
      const structBed = structuralRockWeakness3D(px, py, pz, options);
      const susceptibility = (0.28 + 0.72 * smoothstep(-0.55, 0.72, mineral)) * structBed;
      const protection = tool.protectionMultiplier(px, py, pz, nnx, nny, nnz, {
        edgeProtection: Math.max(options.edgeProtection ?? 0.7, 0.68),
        hardnessAt: options.hardnessAt,
      });
      const kamenitzaPool = (slopeSin < 0.025 && concave > 0.22) ? 1.35 : 0.78;
      const delta = h * 0.068 * (0.20 + amount) * water * kamenitzaPool *
        runnelFocus * (0.35 + 0.65 * concave) * susceptibility * protection;
      const applied = tool.applyBrushAt(px, py, pz, delta);
      if (applied > 0) stats.dissolvedVolume += applied * h * h;

      if (slopeSin < 0.015 || py <= groundY + 0.55 * h) break;
      const chemMeander = new Float64Array(3);
      meanderTangentDirection3D(
        px, py, pz,
        nnx, nny, nnz,
        down[0], down[1], down[2],
        slopeSin,
        seedSalt ^ 0x41c64e6d,
        (rand() - 0.5) * 0.24,
        chemMeander
      );
      const qx = px + chemMeander[0] * 0.8 * h;
      const qy = py + chemMeander[1] * 0.8 * h;
      const qz = pz + chemMeander[2] * 0.8 * h;
      if (!tool.insideInner(qx, qy, qz)) break;
      const qp = tool.projectToSurface(qx, qy, qz);
      px = qp[0]; py = qp[1]; pz = qp[2];
      water *= 0.994;
      if (water < 0.04) break;
    }
  }

  if (stats.dissolvedVolume > 0) {
    redistanceSdfNarrowBand(sdf, preChemValues, 3.0, 2);
  }
  if (onProgress) onProgress(1);
  stats.dissolvedVolume = Math.max(0, stats.dissolvedVolume);
  stats.erodedVolume = stats.dissolvedVolume;
  stats.elapsedMs = Math.round(performance.now() - t0);
  return stats;
}

/**
 * SLOT-CANYON FLASH FLOOD + CHANNELLED WIND.
 *
 * Generic rain falls from the sky and generic wind saltates over the outside
 * of a fin; neither is how a slot is actually cut. Mapped/physical studies of
 * Wire Pass-style slots identify episodic, sediment-laden flash floods as the
 * dominant abrasion agent. This process therefore advects flow packets along
 * the builder's recorded centreline instead of spawning them over the asset:
 *
 * - frequent, medium and rare flood stages wet different wall heights;
 * - bedload packets stay near the bed, suspended packets occupy the flow;
 * - local speed rises in pinch reaches (continuity) and falls in swells;
 * - helical secondary circulation + turbulence drive packets into walls;
 * - abrasion is biased to upstream-facing and protruding surfaces;
 * - sediment first supplies abrasive "tools", then saturates/shields the bed
 *   as dynamic "cover"; excess load is deposited in expansions/eddies;
 * - channelled wind runs afterwards at a much lower rate, because eolian
 *   polish modifies slot sandstone between floods rather than excavating it.
 *
 * The path is world-space metadata from `buildSlotCanyon`, so the identical
 * process works on both the analytic SDF and a mesh-converted SDF.
 */
export function erodeSdfWithSlotFlood(sdf, options = {}) {
  const t0 = performance.now();
  const amount = clamp(options.amount ?? options.slotFloodAmount ?? 0, 0, 1);
  const stats = {
    particlesRequested: 0, particles: 0, floodParticles: 0, windParticles: 0,
    steps: 0, erodedVolume: 0, depositedVolume: 0,
    abrasionEvents: 0, depositionEvents: 0, wallEvents: 0, floorEvents: 0,
    potholeEvents: 0, elapsedMs: 0,
  };
  const finish = () => {
    stats.erodedVolume = Math.max(0, stats.erodedVolume);
    stats.depositedVolume = Math.max(0, stats.depositedVolume);
    stats.elapsedMs = Math.round(performance.now() - t0);
    return stats;
  };
  if (!sdf || amount <= 0) return finish();

  const slot = options.slotCanyon || options.slotPath || null;
  const rawSamples = Array.isArray(slot) ? slot : (slot?.samples || []);
  const optionGroundY = Number.isFinite(options.groundY) ? options.groundY : null;
  const slotGroundY = Number.isFinite(slot?.groundY) ? slot.groundY : optionGroundY;
  const samples = [];
  for (const s of rawSamples) {
    const z = Number(s?.z);
    const x = Number(s?.x ?? s?.centerX ?? 0);
    const halfGap = Number(s?.halfGap ?? s?.halfWidth ?? 0);
    if (!Number.isFinite(z) || !Number.isFinite(x) || !(halfGap > 1e-6)) continue;
    samples.push({
      z,
      x,
      floorY: Number.isFinite(s?.floorY) ? Number(s.floorY) : (slotGroundY ?? 0),
      halfGap,
      crestY: Number.isFinite(s?.crestY) ? Number(s.crestY) : NaN,
      dxdz: 0,
      dFloorDz: 0,
      dHalfDz: 0,
    });
  }
  samples.sort((a, b) => a.z - b.z);
  if (samples.length < 2) return finish();

  let minFloorY = Infinity;
  let meanHalfGap = 0;
  for (const s of samples) {
    minFloorY = Math.min(minFloorY, s.floorY);
    meanHalfGap += s.halfGap;
  }
  meanHalfGap = Math.max(1e-6, meanHalfGap / samples.length);
  const nominalHeight = Math.max(
    1e-6,
    Number(slot?.height) ||
      samples.reduce((m, s) => Math.max(m, (Number.isFinite(s.crestY) ? s.crestY : s.floorY) - s.floorY), 0) ||
      1
  );
  for (const s of samples) {
    if (!Number.isFinite(s.crestY) || s.crestY <= s.floorY) s.crestY = s.floorY + nominalHeight;
  }
  for (let i = 0; i < samples.length; i++) {
    const a = samples[Math.max(0, i - 1)];
    const b = samples[Math.min(samples.length - 1, i + 1)];
    const dz = Math.max(1e-9, b.z - a.z);
    samples[i].dxdz = (b.x - a.x) / dz;
    samples[i].dFloorDz = (b.floorY - a.floorY) / dz;
    samples[i].dHalfDz = (b.halfGap - a.halfGap) / dz;
  }

  const pathMinZ = samples[0].z;
  const pathMaxZ = samples[samples.length - 1].z;
  const pathLength = Math.max(1e-6, pathMaxZ - pathMinZ);
  const _path = { z: 0, x: 0, floorY: 0, halfGap: 1, crestY: 1, dxdz: 0, dFloorDz: 0, dHalfDz: 0 };
  function pathAt(z) {
    if (z <= pathMinZ) return samples[0];
    if (z >= pathMaxZ) return samples[samples.length - 1];
    let lo = 0;
    let hi = samples.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (samples[mid].z <= z) lo = mid;
      else hi = mid;
    }
    const a = samples[lo];
    const b = samples[hi];
    const t = clamp((z - a.z) / Math.max(1e-9, b.z - a.z), 0, 1);
    _path.z = z;
    _path.x = a.x + (b.x - a.x) * t;
    _path.floorY = a.floorY + (b.floorY - a.floorY) * t;
    _path.halfGap = a.halfGap + (b.halfGap - a.halfGap) * t;
    _path.crestY = a.crestY + (b.crestY - a.crestY) * t;
    _path.dxdz = a.dxdz + (b.dxdz - a.dxdz) * t;
    _path.dFloorDz = a.dFloorDz + (b.dFloorDz - a.dFloorDz) * t;
    _path.dHalfDz = a.dHalfDz + (b.dHalfDz - a.dHalfDz) * t;
    return _path;
  }

  const windAmount = clamp(options.windAmount ?? 0, 0, 1);
  const floodCount = Math.max(0, Math.round(
    options.particleCount ?? options.floodParticles ?? options.slotFloodParticles ??
    defaultParticleCount(sdf, 6200)
  ));
  const windPolishCount = Math.max(0, Math.round(
    options.windPolishParticles ?? options.slotWindParticles ??
    (windAmount > 0.001 ? floodCount * 0.38 : 0)
  ));
  const totalParticles = floodCount + windPolishCount;
  stats.particlesRequested = totalParticles;
  if (!totalParticles) return finish();

  const sedimentSupply = clamp(options.sedimentSupply ?? options.slotSedimentSupply ?? 0.62, 0, 1);
  const edgeProtection = clamp(options.edgeProtection ?? 0.7, 0, 1);
  const groundY = optionGroundY ?? slotGroundY ?? minFloorY;
  const flowDirection = Number(slot?.flowDirection) < 0 ? -1 : 1;
  const rng = options.rng;
  const rand = () => (rng ? rng.float() : Math.random());
  const onProgress = options.onProgress || null;
  const seedSalt = (options.seedSalt ?? 0x51f15e) | 0;
  const windRegime = {
    windAzimuth: options.windAzimuthDeg ?? options.azimuthDeg ?? options.windAzimuth ?? 35,
    windShiftMode: options.windShiftMode ?? options.shiftMode ?? options.windDirectionMode ?? 'fixed',
    windShiftRange: options.windShiftRange ?? options.shiftRangeDeg ?? options.windShiftRangeDeg ?? 55,
    windShiftSectors: options.windShiftSectors ?? options.shiftSectors ?? 5,
    windShiftEpisodes: options.windShiftEpisodes ?? options.shiftEpisodes,
    seed: seedSalt,
  };
  const shiftMode = normalizeWindShiftMode(windRegime.windShiftMode);
  const shiftCycles = clamp(
    Number(options.windShiftCycles ?? options.shiftCycles ?? (shiftMode === 'fixed' ? 0 : 1)),
    0,
    12
  );
  const baseWindSpeed = clamp(options.windSpeed ?? 0.62, 0.05, 1.5);

  const tool = createSdfProcessTool(sdf, {
    maxCarveDepth: sdf.voxelSize * (1.6 + 5.4 * Math.max(amount, windAmount * 0.55)),
  });
  const { h } = tool;
  const reportEvery = Math.max(1, Math.floor(totalParticles / 16));
  let globalParticle = 0;

  // Sediment tools/cover balance: abrasion first benefits from supplied
  // grains, then saturates as impacts interfere and bed cover accumulates.
  const toolSupplyFactor = 0.25 + 1.30 * sedimentSupply * (1 - 0.42 * sedimentSupply);
  const coverSupplyFactor = smoothstep(0.62, 1.0, sedimentSupply);

  function runKind(kind, count) {
    const isFlood = kind === 'flood';
    for (let pIdx = 0; pIdx < count; pIdx++, globalParticle++) {
      if (onProgress && globalParticle % reportEvery === 0) {
        onProgress(globalParticle / Math.max(1, totalParticles));
      }
      const bakeFraction = totalParticles > 1 ? globalParticle / (totalParticles - 1) : 0;

      let dirSign = flowDirection;
      let axisStrength = 1;
      if (!isFlood) {
        const azimuthRad = effectiveWindAzimuthDeg(windRegime, bakeFraction * shiftCycles) * Math.PI / 180;
        const axisZ = Math.sin(azimuthRad);
        dirSign = axisZ >= 0 ? 1 : -1;
        axisStrength = 0.28 + 0.72 * Math.abs(axisZ);
      }

      const endMargin = Math.min(h * 1.4, pathLength * 0.02);
      const zLo = Math.max(pathMinZ, tool.minZ + h);
      const zHi = Math.min(pathMaxZ, tool.maxZ - h);
      const startZ = clamp(dirSign > 0 ? pathMinZ + endMargin : pathMaxZ - endMargin, zLo, zHi);
      const endZ = clamp(dirSign > 0 ? pathMaxZ - endMargin : pathMinZ + endMargin, zLo, zHi);
      const sp = pathAt(startZ);
      const startX = sp.x;
      const startFloorY = sp.floorY;
      const startHalfGap = Math.max(h, sp.halfGap);
      const startCrestY = sp.crestY;
      const startDxDz = sp.dxdz;
      const startDFloorDz = sp.dFloorDz;
      const localHeight = Math.max(h, startCrestY - startFloorY);

      // Confined discharge does not sample the cross-section uniformly:
      // saltating bedload and near-wall suspended grains are the abrasive
      // tools, so spawn more packets near the bed and walls than mid-air.
      let q;
      const qRoll = rand();
      if (qRoll < 0.46) q = (rand() < 0.5 ? -1 : 1) * (0.68 + 0.30 * rand());
      else if (qRoll < 0.82) q = (rand() * 2 - 1) * 0.72;
      else q = (rand() * 2 - 1) * 0.36;

      let px = startX + q * startHalfGap;
      let py;
      let pz = startZ;
      let packetClass = 3; // channel wind
      let floodDepth = localHeight;
      let eventMagnitude = 1;
      if (isFlood) {
        const stageRoll = rand();
        let depthFrac;
        if (stageRoll < 0.52) depthFrac = 0.30 + rand() * 0.22;       // frequent flow
        else if (stageRoll < 0.88) depthFrac = 0.52 + rand() * 0.26;  // competent flood
        else depthFrac = 0.78 + rand() * 0.20;                        // rare deep flood
        floodDepth = depthFrac * localHeight;
        eventMagnitude = 0.62 + 0.72 * depthFrac;
        const classRoll = rand();
        if (classRoll < 0.48) {
          packetClass = 0; // bedload
          py = startFloorY + (0.035 + 0.18 * Math.pow(rand(), 1.6)) * floodDepth;
        } else if (classRoll < 0.90) {
          packetClass = 1; // suspended load
          py = startFloorY + (0.14 + 0.74 * rand()) * floodDepth;
        } else {
          packetClass = 2; // high wash / near-surface load
          py = startFloorY + (0.74 + 0.24 * rand()) * floodDepth;
        }
      } else {
        py = startFloorY + (0.04 + 0.92 * Math.pow(rand(), 1.35)) * localHeight * 0.94;
      }
      py = clamp(py, tool.minY + h, tool.maxY - h);
      if (!tool.insideInner(px, py, pz)) continue;

      // If a near-wall spawn lands inside a wave crest, pull it towards the
      // thalweg rather than starting a packet embedded in bedrock.
      let startPhi = tool.sampleOnly(px, py, pz);
      for (let attempt = 0; attempt < 4 && startPhi < 0.20 * h; attempt++) {
        px = startX + (px - startX) * 0.55;
        py = Math.min(startCrestY - 0.35 * h, py + 0.30 * h);
        startPhi = tool.sampleOnly(px, py, pz);
      }
      if (startPhi < -0.25 * h) continue;

      let tx = startDxDz * dirSign;
      let ty = startDFloorDz * dirSign * 0.28;
      let tz = dirSign;
      const tl = Math.hypot(tx, ty, tz) || 1;
      tx /= tl; ty /= tl; tz /= tl;
      let particleSpeed = isFlood
        ? (0.72 + rand() * 0.28) * eventMagnitude
        : (0.42 + 0.62 * baseWindSpeed) * axisStrength * (0.78 + rand() * 0.45);
      let vx = tx * particleSpeed;
      let vy = ty * particleSpeed;
      let vz = tz * particleSpeed;
      let sediment = isFlood
        ? h * (0.05 + 0.42 * sedimentSupply) * eventMagnitude
        : h * (0.02 + 0.13 * sedimentSupply);
      const minimumToolLoad = sediment * 0.30;
      let energy = 1;
      let maxAlong = dirSign * pz;
      let staleSteps = 0;
      const maxSteps = Math.round(clamp(
        (pathLength / Math.max(h, 0.62 * h)) * (isFlood ? 1.35 : 1.10),
        240,
        1050
      ));

      stats.particles++;
      if (isFlood) stats.floodParticles++;
      else stats.windParticles++;

      for (let step = 0; step < maxSteps; step++) {
        if (!tool.insideInner(px, py, pz)) break;
        if ((dirSign > 0 && pz >= endZ) || (dirSign < 0 && pz <= endZ)) break;
        stats.steps++;

        const lp = pathAt(pz);
        const pathX = lp.x;
        const floorY = lp.floorY;
        const halfGap = Math.max(h, lp.halfGap);
        const crestY = lp.crestY;
        const dxdz = lp.dxdz;
        const dFloorDz = lp.dFloorDz;
        const dHalfDz = lp.dHalfDz;
        const heightHere = Math.max(h, crestY - floorY);
        const qHere = (px - pathX) / halfGap;
        const sNorm = clamp((pz - pathMinZ) / pathLength, 0, 1);

        // Centreline-confined target flow. Width is a continuity proxy:
        // pinches accelerate, swells decelerate and become depositional.
        let flowX = dxdz * dirSign;
        let flowY = dFloorDz * dirSign * 0.28;
        let flowZ = dirSign;
        const flowLen = Math.hypot(flowX, flowY, flowZ) || 1;
        flowX /= flowLen; flowY /= flowLen; flowZ /= flowLen;
        const widthFactor = clamp(meanHalfGap / halfGap, 0.72, 1.80);
        const expansionRate = clamp(dirSign * dHalfDz * 4.0, -1, 1);
        const speedTarget = particleSpeed *
          Math.pow(widthFactor, isFlood ? 0.78 : 0.45) *
          (1 - 0.18 * Math.max(0, expansionRate)) * energy;

        // Helical secondary circulation in bends plus aperiodic turbulence.
        // This flow-wander field is not a texture: it only steers packets.
        const crossX = -flowZ;
        const crossZ = flowX;
        const helixPhase = sNorm * Math.PI * 7.0 * dirSign +
          clamp((py - floorY) / heightHere, 0, 1) * Math.PI * 2.3 + pIdx * 0.37;
        const turbulence = scalarField3D(px * 0.135 + 3.1, py * 0.110 - 1.7, pz * 0.135 + 8.4, seedSalt ^ 0x3a7bd2);
        const swirl = Math.sin(helixPhase) * (isFlood ? 0.13 : 0.07) + turbulence * (isFlood ? 0.16 : 0.08);
        let targetX = flowX + crossX * swirl - qHere * (isFlood ? 0.11 : 0.15);
        let targetY = flowY + Math.cos(helixPhase) * (qHere >= 0 ? 1 : -1) * (isFlood ? 0.040 : 0.018);
        let targetZ = flowZ + crossZ * swirl;
        if (isFlood && packetClass === 0) targetY -= 0.16;
        else if (isFlood && packetClass === 1) targetY -= 0.035;
        else if (!isFlood) targetY -= 0.018;
        const targetLen = Math.hypot(targetX, targetY, targetZ) || 1;
        targetX = targetX / targetLen * speedTarget;
        targetY = targetY / targetLen * speedTarget;
        targetZ = targetZ / targetLen * speedTarget;
        const blend = isFlood ? 0.28 : 0.22;
        vx += (targetX - vx) * blend;
        vy += (targetY - vy) * blend;
        vz += (targetZ - vz) * blend;

        // Keep packets inside the slot's analytic airspace; the SDF remains
        // the authority for where rock actually is.
        const minPacketY = floorY + 0.32 * h;
        if (py < minPacketY) {
          py = minPacketY;
          vy = Math.abs(vy) * 0.22;
        }
        const maxPacketY = crestY - 0.25 * h;
        if (py > maxPacketY) {
          py = maxPacketY;
          vy = -Math.abs(vy) * 0.18;
        }

        tool.sampleGrad(px, py, pz);
        let phi = tool.sg[0];
        if (phi < -0.80 * h) {
          const pr = tool.projectToSurface(px, py, pz);
          tool.sampleGrad(pr[0], pr[1], pr[2]);
          const gl0 = Math.hypot(tool.sg[1], tool.sg[2], tool.sg[3]);
          if (gl0 < 1e-9) break;
          px = pr[0] + (tool.sg[1] / gl0) * 0.50 * h;
          py = pr[1] + (tool.sg[2] / gl0) * 0.50 * h;
          pz = pr[2] + (tool.sg[3] / gl0) * 0.50 * h;
          phi = 0.50 * h;
          tool.sampleGrad(px, py, pz);
          phi = tool.sg[0];
        }

        if (phi < 1.45 * h) {
          const gx = tool.sg[1];
          const gy = tool.sg[2];
          const gz = tool.sg[3];
          const gl = Math.hypot(gx, gy, gz);
          if (gl >= 1e-9) {
            const nnx = gx / gl;
            const nny = gy / gl;
            const nnz = gz / gl;
            const sp2 = tool.projectToSurface(px, py, pz);
            const sx = sp2[0];
            const sy = sp2[1];
            const sz = sp2[2];
            const qSurf = (sx - pathX) / halfGap;
            const hab = sy - floorY;
            const steepness = Math.sqrt(Math.max(0, 1 - nny * nny));
            const isFloor = nny > 0.48 && hab < 0.20 * heightHere && Math.abs(qSurf) < 1.55;
            const isWall = !isFloor && steepness > 0.38 && Math.abs(qSurf) > 0.42 &&
              Math.abs(qSurf) < 2.15 && sy < crestY + 0.5 * h;

            if (isFloor || isWall) {
              const dotNF = nnx * flowX + nny * flowY + nnz * flowZ;
              const upstream = clamp(-dotNF, 0, 1);
              const tangential = Math.sqrt(Math.max(0, 1 - dotNF * dotNF));
              const protrusion = scalarField3D(sx * 0.22 + 7.8, sy * 0.18 - 2.6, sz * 0.22 + 4.9, seedSalt ^ 0x51ed27);
              const exposure = 0.18 + 0.95 * Math.pow(upstream, 1.2) +
                0.48 * tangential * steepness + 0.15 * Math.max(0, protrusion);

              let heightTool;
              if (isFlood) {
                if (sy > floorY + floodDepth + 0.7 * h) {
                  heightTool = 0;
                } else if (packetClass === 0) {
                  heightTool = 0.18 + 0.95 * Math.exp(-Math.max(0, hab) / Math.max(h, 0.18 * floodDepth));
                } else if (packetClass === 1) {
                  heightTool = 0.22 + 0.78 * Math.exp(-Math.max(0, hab) / Math.max(h, 0.62 * floodDepth));
                } else {
                  heightTool = 0.16 + 0.55 * Math.exp(
                    -Math.abs(hab - floodDepth * 0.85) / Math.max(h, 0.35 * floodDepth)
                  );
                }
              } else {
                heightTool = 0.28 + 0.72 * Math.exp(-Math.max(0, hab) / Math.max(h, 0.40 * heightHere));
              }

              let coverFactor = 1;
              if (isFloor) {
                coverFactor = 1 - 0.58 * coverSupplyFactor * Math.exp(-Math.max(0, hab) / Math.max(h, 0.10 * heightHere));
              } else if (hab < 0.16 * heightHere) {
                coverFactor = 1 - 0.25 * coverSupplyFactor * Math.exp(-Math.max(0, hab) / Math.max(h, 0.10 * heightHere));
              }

              const eddyField = scalarField3D(sx * 0.16 - 5.2, sy * 0.11 + 2.1, sz * 0.16 - 8.7, seedSalt ^ 0x27d4eb);
              const eddy = clamp(0.5 + 0.5 * eddyField + 0.45 * Math.max(0, expansionRate) + 0.25 * upstream, 0, 1.4);
              const speedNow = clamp(Math.hypot(vx, vy, vz), 0.05, 2.4);
              const capacity = h * (isFlood ? (0.10 + 0.62 * amount) : (0.04 + 0.28 * windAmount)) *
                (0.20 + exposure) * (0.45 + speedNow) * (0.45 + sedimentSupply);
              const canHold = isFloor || nny > 0.08;
              const shouldDeposit = heightTool > 0 && canHold && sediment > capacity * 1.02 &&
                (isFloor || expansionRate > 0.05 || speedNow < 0.48 || dotNF > 0.25);

              if (shouldDeposit) {
                const dep = Math.min(sediment * 0.38, (sediment - capacity) * 0.55 + h * 0.015);
                if (dep > 1e-9) {
                  const applied = tool.applyBrushAt(sx, sy, sz, -dep);
                  if (applied < 0) {
                    stats.depositedVolume -= applied * h * h;
                    sediment = Math.max(0, sediment + applied);
                    stats.depositionEvents++;
                    if (isFloor) stats.floorEvents++;
                    else stats.wallEvents++;
                  }
                }
              } else if (heightTool > 0.001) {
                const protection = tool.protectionMultiplier(sx, sy, sz, nnx, nny, nnz, {
                  edgeProtection: edgeProtection * (isFlood ? 0.92 : 1.0),
                  hardnessAt: options.hardnessAt,
                });
                const eddyBoost = isFloor ? (0.75 + 1.15 * eddy) : (0.85 + 0.45 * eddy);
                let rate = h * (isFlood ? 0.125 : 0.055) *
                  (0.18 + (isFlood ? amount : windAmount)) * exposure * heightTool *
                  toolSupplyFactor * coverFactor * eddyBoost * speedNow * protection;
                if (!isFlood) rate *= axisStrength;
                const room = Math.max(0, capacity - sediment);
                // Even at nominal capacity, the packet grain itself can chip
                // an upstream-facing protrusion; keep this two orders below
                // the free-tools rate so cover still throttles the bed.
                const impactAllowance = upstream > 0.55 ? Math.min(rate * 0.16, h * 0.002) : 0;
                const erode = Math.min(rate, room + impactAllowance);
                if (erode > 1e-9) {
                  const applied = tool.applyBrushAt(sx, sy, sz, erode);
                  if (applied > 0) {
                    stats.erodedVolume += applied * h * h;
                    sediment += applied;
                    stats.abrasionEvents++;
                    if (isFloor) {
                      stats.floorEvents++;
                      if (eddy > 0.72 && rand() < 0.08) stats.potholeEvents++;
                    } else {
                      stats.wallEvents++;
                    }
                  }
                }
              }

              // Slide/reflect off the wall or bed, then stand the packet off
              // the surface by a fraction of a voxel so it does not grind
              // the identical brush centre on every substep.
              const vn = vx * nnx + vy * nny + vz * nnz;
              if (vn < 0) {
                const rebound = (isFloor && packetClass === 0) ? 1.02 : 1.28;
                vx -= nnx * vn * rebound;
                vy -= nny * vn * rebound;
                vz -= nnz * vn * rebound;
              }
              vx += flowX * speedTarget * 0.16;
              vy += flowY * speedTarget * 0.16;
              vz += flowZ * speedTarget * 0.16;
              const standOff = (isFloor && packetClass === 0) ? 0.38 : 0.52;
              px = sx + nnx * h * standOff;
              py = sy + nny * h * standOff;
              pz = sz + nnz * h * standOff;
              energy *= isFlood ? 0.996 : 0.998;
            }
          }
        }

        if (py <= groundY + 0.55 * h) {
          if (sediment > 1e-9 && tool.insideInner(px, py, pz)) {
            const applied = tool.applyBrushAt(px, Math.max(py, groundY + 0.55 * h), pz, -sediment);
            if (applied < 0) stats.depositedVolume -= applied * h * h;
          }
          break;
        }

        let speedMag = Math.hypot(vx, vy, vz);
        if (speedMag < 1e-9) {
          vx = flowX * Math.max(0.05, speedTarget);
          vy = flowY * Math.max(0.05, speedTarget);
          vz = flowZ * Math.max(0.05, speedTarget);
          speedMag = Math.hypot(vx, vy, vz) || 1;
        }
        const stepLen = phi > 2.0 * h
          ? Math.min(3.4 * h, Math.max(0.85 * h, phi * 0.72))
          : 0.80 * h;
        const scaleStep = stepLen / speedMag;
        px += vx * scaleStep;
        py += vy * scaleStep;
        pz += vz * scaleStep;

        // A packet is a local control volume moving through the canyon:
        // some suspended load is continually exported downstream. Without
        // this leak every packet saturates after its first wall and the
        // tools/cover law would switch all downstream abrasion off.
        sediment = Math.max(minimumToolLoad, sediment * (isFlood ? 0.9985 : 0.9992));
        energy *= isFlood ? 0.9995 : 0.9998;

        const along = dirSign * pz;
        if (along > maxAlong + 0.05 * h) {
          maxAlong = along;
          staleSteps = 0;
        } else if (++staleSteps > 150) {
          break;
        }
      }
    }
  }

  const preSlotValues = new Float32Array(sdf.values);
  runKind('flood', floodCount);
  runKind('wind', windPolishCount);
  if (stats.erodedVolume > 0 || stats.depositedVolume > 0) {
    redistanceSdfNarrowBand(sdf, preSlotValues, 3.0, 2);
  }
  if (onProgress) onProgress(1);
  return finish();
}

/**
 * Runs the enabled SDF processes in a stable geological order.
 * Input is a flat options object; see generator.js for the UI mapping.
 */
export function applySdfErosionSuite(sdf, options = {}) {
  const t0 = performance.now();
  const stats = {
    chemicalDissolution: null,
    wind: null,
    rain: null,
    slotFlood: null,
    cryo: null,
    thermal: null,
    previewFrames: 0,
    elapsedMs: 0,
  };
  if (!sdf) return stats;

  const common = {
    edgeProtection: options.edgeProtection ?? 0.7,
    hardnessAt: options.hardnessAt || null,
    groundY: options.groundY ?? -Infinity,
    dipAngle: options.dipAngle,
    strikeAngle: options.strikeAngle,
    jointDensity: options.jointDensity,
    cleavageSharpness: options.cleavageSharpness,
  };
  const forkRng = (label) => (options.rng?.fork ? options.rng.fork(label) : options.rng);
  const onProgress = options.onProgress || null;
  const onPreview = typeof options.onPreview === 'function' ? options.onPreview : null;
  const previewFrameCount = onPreview
    ? clamp(Math.round(options.previewFrames ?? 6), 1, 16)
    : 0;
  // The requested count is the total number of snapshots, including the
  // source frame at 0 and completed frame at 1.
  const previewIntervals = Math.max(1, previewFrameCount - 1);
  let nextPreviewAt = previewFrameCount > 1 ? 1 / previewIntervals : Infinity;
  const tasks = [];

  if ((options.chemicalDissolutionAmount ?? 0) > 0.001) {
    tasks.push({
      key: 'chemicalDissolution',
      stage: 'Chemical dissolution on SDF (wet residence paths)...',
      run: (progress) => erodeSdfWithChemicalDissolution(sdf, {
        ...common,
        amount: options.chemicalDissolutionAmount,
        particleCount: options.chemicalDroplets,
        rng: forkRng('sdf-chemical-dissolution'),
        seedSalt: options.seedSalt,
        onProgress: progress,
      }),
    });
  }
  if ((options.windAmount ?? 0) > 0.001) {
    tasks.push({
      key: 'wind',
      stage: 'Wind saltation on SDF (gusting wrap-around flow)...',
      run: (progress) => erodeSdfWithWind(sdf, {
        ...common,
        amount: options.windAmount,
        particleCount: options.windParticles,
        azimuthDeg: options.windAzimuthDeg ?? options.windAzimuth ?? 35,
        shiftMode: options.windShiftMode ?? options.windDirectionMode ?? 'fixed',
        shiftRangeDeg: options.windShiftRange ?? options.windShiftRangeDeg ?? 55,
        shiftCycles: options.windShiftCycles ?? options.windShiftCycleCount ?? 1,
        shiftSectors: options.windShiftSectors ?? 5,
        turbulence: options.windTurbulence ?? 0.45,
        streamlining: options.windStreamlining ?? 0.65,
        sandSupply: options.windSandSupply ?? 0.65,
        fluting: options.windFluting ?? 0.65,
        curtainHeight: options.windCurtainHeight ?? 0.42,
        speed: options.windSpeed ?? 0.62,
        rng: forkRng('sdf-wind-erosion'),
        seedSalt: options.seedSalt,
        onProgress: progress,
      }),
    });
  }
  if ((options.rainAmount ?? 0) > 0.001) {
    tasks.push({
      key: 'rain',
      stage: 'Rain droplet erosion on SDF (particle runoff)...',
      run: (progress) => erodeSdfWithRain(sdf, {
        ...common,
        dropletCount: options.rainDroplets ?? defaultParticleCount(sdf, 9000),
        amount: options.rainAmount,
        rng: forkRng('rain-erosion'),
        onProgress: progress,
      }),
    });
  }
  if ((options.slotFloodAmount ?? 0) > 0.001 && options.slotCanyon) {
    tasks.push({
      key: 'slotFlood',
      stage: 'Slot flash-flood abrasion + channelled wind polish...',
      run: (progress) => erodeSdfWithSlotFlood(sdf, {
        ...common,
        amount: options.slotFloodAmount,
        slotCanyon: options.slotCanyon,
        particleCount: options.slotFloodParticles,
        windPolishParticles: options.slotWindParticles,
        sedimentSupply: options.slotSedimentSupply ?? 0.62,
        windAmount: options.windAmount ?? 0,
        windAzimuthDeg: options.windAzimuthDeg ?? options.windAzimuth ?? 35,
        windShiftMode: options.windShiftMode ?? options.windDirectionMode ?? 'fixed',
        windShiftRange: options.windShiftRange ?? options.windShiftRangeDeg ?? 55,
        windShiftCycles: options.windShiftCycles ?? options.windShiftCycleCount ?? 1,
        windShiftSectors: options.windShiftSectors ?? 5,
        windSpeed: options.windSpeed ?? 0.62,
        rng: forkRng('sdf-slot-flash-flood'),
        seedSalt: options.seedSalt,
        onProgress: progress,
      }),
    });
  }
  if ((options.cryoAmount ?? 0) > 0.001) {
    tasks.push({
      key: 'cryo',
      stage: 'Cryo freeze-thaw on SDF (settled meltwater)...',
      run: (progress) => erodeSdfWithFreezeThaw(sdf, {
        ...common,
        amount: options.cryoAmount,
        particleCount: options.cryoParticles,
        rng: forkRng('sdf-cryo-erosion'),
        seedSalt: options.seedSalt,
        onProgress: progress,
      }),
    });
  }
  if ((options.thermalAmount ?? 0) > 0.001) {
    tasks.push({
      key: 'thermal',
      stage: 'Thermal talus on SDF (angle-of-repose transport)...',
      run: (progress) => erodeSdfWithThermalTalus(sdf, {
        ...common,
        amount: options.thermalAmount,
        particleCount: options.thermalParticles,
        reposeAngleDeg: options.thermalReposeAngleDeg ?? 34,
        rng: forkRng('sdf-thermal-talus'),
        onProgress: progress,
      }),
    });
  }

  let lastPreviewFraction = -1;
  const emitPreview = (fraction, stage, process, processFraction) => {
    if (!onPreview) return;
    const f = clamp(fraction, 0, 1);
    if (f <= lastPreviewFraction && lastPreviewFraction >= 0) return;
    lastPreviewFraction = f;
    stats.previewFrames++;
    onPreview({
      fraction: f,
      stage,
      process: process || null,
      processFraction: clamp(processFraction ?? f, 0, 1),
    });
  };

  if (onPreview && tasks.length) {
    emitPreview(0, 'Before SDF erosion (source formation)', null, 0);
  }

  for (let i = 0; i < tasks.length; i++) {
    const task = tasks[i];
    const base = i / tasks.length;
    const slice = 1 / tasks.length;
    if (onProgress) onProgress(base, task.stage, task.key);
    const progress = (onProgress || onPreview)
      ? (f) => {
          const pf = clamp(f, 0, 1);
          const global = base + slice * pf;
          if (onProgress) onProgress(global, task.stage, task.key);
          if (onPreview && global >= nextPreviewAt) {
            emitPreview(global, task.stage, task.key, pf);
            nextPreviewAt = global >= 0.999
              ? Infinity
              : (Math.floor(global * previewIntervals) + 1) / previewIntervals;
          }
        }
      : null;
    stats[task.key] = task.run(progress);
  }
  if (onPreview && previewFrameCount > 1 && tasks.length && lastPreviewFraction < 0.999) {
    emitPreview(1, 'SDF erosion complete (before final contour/QEM)', 'done', 1);
  }
  if (onProgress && tasks.length) onProgress(1, 'SDF erosion suite complete', 'done');
  stats.elapsedMs = Math.round(performance.now() - t0);
  return stats;
}
