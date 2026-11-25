/**
 * Frontier Studio — Wind Erosion Debug Vector Field (CPU mirror of the GPU flow model).
 *
 * The aeolian erosion in `erosion-shaders.js` (terrain integrator) does not
 * blow along one compass vector: the prevailing wind is bent by seeded
 * turbulent gusts that advect downwind, wraps around massifs and channels
 * through valleys (multi-scale obstacle deflection), accelerates over
 * stoss slopes and crests, separates in immediate lee wakes, and recovers
 * farther downwind as the boundary layer reattaches. That makes the erosion
 * hard to predict from the azimuth slider alone, so this module reproduces
 * the same flow model on the CPU (same noise, scales and formulas) to draw
 * a debug vector field over the terrain: arrow direction = local flow
 * direction, arrow length/colour = local flow speed.
 *
 * Compass convention (stated in the UI legend): world -Z is North and +X is
 * East. The engine's `windAzimuth` parameter is the mathematical angle of the
 * flow vector in the XZ plane: dir = (cos az, sin az) — the direction the wind
 * blows TOWARD. Helpers below convert to meteorological "wind FROM" bearings.
 */

const f32 = Math.fround;
const fract = (x) => x - Math.floor(x);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const smoothstep = (e0, e1, x) => {
  const t = clamp((x - e0) / Math.max(1e-6, e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
};

// --- GPU noise mirror (hash12 / noise2D / fbm2D from erosion-shaders.js) ---
export function hash12(px, py) {
  let x = f32(fract(f32(px * 0.1031)));
  let y = f32(fract(f32(py * 0.1031)));
  let z = x; // vec3(p.xyx): third component reuses p.x
  const d = f32(f32(x * f32(y + 33.33)) + f32(y * f32(z + 33.33)) + f32(z * f32(x + 33.33)));
  x = f32(x + d);
  y = f32(y + d);
  z = f32(z + d);
  return f32(fract(f32(f32(x + y) * z)));
}

export function noise2D(x, y) {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const fx = x - ix;
  const fy = y - iy;
  const ux = fx * fx * (3 - 2 * fx);
  const uy = fy * fy * (3 - 2 * fy);
  const a = hash12(ix, iy);
  const b = hash12(ix + 1, iy);
  const c = hash12(ix, iy + 1);
  const d = hash12(ix + 1, iy + 1);
  const ab = a + (b - a) * ux;
  const cd = c + (d - c) * ux;
  return ab + (cd - ab) * uy;
}

export function fbm2D(px, py, octaves, roughness) {
  let sum = 0;
  let amp = 0.5;
  let norm = 0;
  let x = px;
  let y = py;
  const rough = clamp(roughness, 0.35, 0.72);
  for (let i = 0; i < 8 && i < octaves; i++) {
    sum += amp * noise2D(x, y);
    norm += amp;
    // mat2(0.80, -0.60, 0.60, 0.80) (column-major) * p * 2.03 + (17.1, 31.7)
    const nx = (0.80 * x + 0.60 * y) * 2.03 + 17.1;
    const ny = (-0.60 * x + 0.80 * y) * 2.03 + 31.7;
    x = nx;
    y = ny;
    amp *= rough;
  }
  return sum / Math.max(norm, 1e-4);
}

// --- Compass helpers --------------------------------------------------------
export const WIND_COMPASS_POINTS = [
  'N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE',
  'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW',
];

export function compassPoint(bearingDeg) {
  const idx = Math.round((((bearingDeg % 360) + 360) % 360) / 22.5) % 16;
  return WIND_COMPASS_POINTS[idx];
}

/**
 * Convert the engine flow azimuth (degrees, dir = (cos, sin) in XZ, blowing
 * TOWARD) into compass bearings with North = -Z, East = +X.
 */
export function windCompassFromAzimuth(azimuthDeg) {
  const az = (Number(azimuthDeg) || 0) * Math.PI / 180;
  const dirX = Math.cos(az);
  const dirZ = Math.sin(az);
  // Bearing is clockwise from North (-Z): bearing = atan2(east, north).
  const towardDeg = ((Math.atan2(dirX, -dirZ) * 180 / Math.PI) + 360) % 360;
  const fromDeg = (towardDeg + 180) % 360;
  return {
    dirX,
    dirZ,
    towardDeg,
    towardPoint: compassPoint(towardDeg),
    fromDeg,
    fromPoint: compassPoint(fromDeg),
  };
}

/**
 * Inverse: the engine `windAzimuth` (normalized to -180..180) that makes the
 * wind blow FROM the given compass bearing (0 = from North, 90 = from East…).
 */
export function azimuthForWindFromBearing(fromBearingDeg) {
  const toward = ((Number(fromBearingDeg) || 0) + 180) * Math.PI / 180;
  const dirX = Math.sin(toward);   // east component
  const dirZ = -Math.cos(toward);  // north is -Z
  let deg = Math.atan2(dirZ, dirX) * 180 / Math.PI;
  if (deg > 180) deg -= 360;
  if (deg < -180) deg += 360;
  const rounded = Math.round(deg);
  return rounded === 0 ? 0 : rounded; // normalize -0
}

// --- Flow-field sampling ----------------------------------------------------

const norm2 = (x, z, fbX, fbZ) => {
  const l = Math.hypot(x, z);
  return l > 1e-5 ? [x / l, z / l] : [fbX, fbZ];
};

/**
 * Sample the erosion flow at one world position, mirroring the integrator:
 * prevailing vector -> downwind-advecting gust wander -> terrain wrap ->
 * crest speedup -> multi-distance lee separation and wake reattachment.
 *
 * @param {{azimuthDeg:number, windSpeed:number, turbulence:number,
 *          streamlining:number, seed:number, cellSize:number,
 *          phase?:number, step?:number}} p
 * @param {number} wx world X (m)
 * @param {number} wz world Z (m)
 * @param {(x:number, z:number) => number} heightAt total surface height (m)
 */
export function sampleWindVector(p, wx, wz, heightAt) {
  const az = (Number(p.azimuthDeg) || 0) * Math.PI / 180;
  const baseX = Math.cos(az);
  const baseZ = Math.sin(az);
  const perpX = -baseZ;
  const perpZ = baseX;
  const seed = Number(p.seed) || 0;
  const turbulence = clamp(Number(p.turbulence) || 0, 0, 1);
  const streamlining = clamp(Number(p.streamlining) || 0, 0, 1);
  const cellSize = Math.max(0.05, Number(p.cellSize) || 1);

  const phase = Number(p.phase ?? p.step ?? p.timeSteps) || 0;
  const gustNoiseA = fbm2D(
    wx * 0.018 + seed * 0.011 - baseX * phase * 0.006,
    wz * 0.018 - seed * 0.007 - baseZ * phase * 0.006,
    3, 0.55
  );
  const gustNoiseB = fbm2D(
    wx * 0.047 + 23.7 - seed * 0.005 - baseX * phase * 0.009 + perpX * phase * 0.0015,
    wz * 0.047 + seed * 0.009 - baseZ * phase * 0.009 + perpZ * phase * 0.0015,
    2, 0.50
  );
  const wander = ((gustNoiseA - 0.5) * 1.45 + (gustNoiseB - 0.5) * 0.55) * turbulence;
  let [lx, lz] = norm2(baseX + perpX * wander, baseZ + perpZ * wander, baseX, baseZ);
  const gust = 0.60 + 0.85 * smoothstep(0.25, 0.85, gustNoiseA);

  const hC = heightAt(wx, wz);
  const hR = heightAt(wx + cellSize, wz);
  const hL = heightAt(wx - cellSize, wz);
  const hU = heightAt(wx, wz + cellSize);
  const hD = heightAt(wx, wz - cellSize);
  const gradX = (hR - hL) / (2 * cellSize);
  const gradZ = (hU - hD) / (2 * cellSize);
  const avgNeighbourH = 0.25 * (hR + hL + hU + hD);
  const convexity = clamp((hC - avgNeighbourH) / cellSize * 0.18, -0.4, 1.2);

  // Wrap-around terrain flow (identical to the shaders): a smoothed
  // multi-scale obstacle gradient (±6 / ±16 cells) progressively blocks the
  // into-hill component of the wind, so the remaining tangential flow splits
  // around massifs, follows contours along flanks and funnels through
  // saddles and valley axes instead of riding straight over every ridge.
  const ogAt = (cells) => [
    (heightAt(wx + cells * cellSize, wz) - heightAt(wx - cells * cellSize, wz)) / (2 * cells * cellSize),
    (heightAt(wx, wz + cells * cellSize) - heightAt(wx, wz - cells * cellSize)) / (2 * cells * cellSize),
  ];
  const og6 = ogAt(6);
  const og16 = ogAt(16);
  const ogX = og6[0] * 0.60 + og16[0] * 0.40;
  const ogZ = og6[1] * 0.60 + og16[1] * 0.40;
  const ogLen = Math.hypot(ogX, ogZ);
  let wrapDeflect = 0;
  if (ogLen > 1e-4) {
    const ogDirX = ogX / ogLen;
    const ogDirZ = ogZ / ogLen;
    const intoHill = lx * ogDirX + lz * ogDirZ;
    const wrapBlock = streamlining * smoothstep(0.06, 0.80, ogLen) * 0.94;
    if (intoHill > 0 && wrapBlock > 0.001) {
      const beforeX = lx;
      const beforeZ = lz;
      [lx, lz] = norm2(lx - ogDirX * intoHill * wrapBlock, lz - ogDirZ * intoHill * wrapBlock, lx, lz);
      wrapDeflect = 1 - (beforeX * lx + beforeZ * lz);
    }
  }

  const slopeAlong = gradX * lx + gradZ * lz;
  const broadSlopeAlong = ogX * lx + ogZ * lz;
  const crestSpeedup = clamp(
    1.0 + 0.48 * broadSlopeAlong
      + 0.38 * Math.max(convexity, 0)
      + 0.18 * smoothstep(0.10, 0.85, slopeAlong),
    0.68, 1.85
  );

  // Multi-distance upwind shelter plus downwind lee-drop geometry, as in
  // the shader. The near/far shelter difference identifies the band where
  // a separated wake has travelled far enough to reattach.
  const shelterAt = (cells) => (heightAt(wx - lx * cells * cellSize, wz - lz * cells * cellSize) - hC) / Math.max(cells * cellSize, 0.001) - 0.22;
  const shelterNearSlope = shelterAt(3);
  const shelterMidSlope = shelterAt(9);
  const shelterFarSlope = shelterAt(20);
  const shelterSlope = Math.max(shelterNearSlope, shelterMidSlope, shelterFarSlope);
  const leeDropAt = (cells) => (hC - heightAt(wx + lx * cells * cellSize, wz + lz * cells * cellSize)) / Math.max(cells * cellSize, 0.001) - 0.20;
  const leeDropSlope = Math.max(leeDropAt(1.35), leeDropAt(9), leeDropAt(20));
  const shadow = smoothstep(0.02, 0.38, shelterSlope) * (0.30 + 0.70 * streamlining);
  const separation = smoothstep(0.02, 0.42, leeDropSlope)
    * smoothstep(0.08, 0.55, -slopeAlong)
    * (0.35 + 0.65 * streamlining);
  const separatedWake = clamp(Math.max(shadow, separation), 0, 1);
  const wakeRecovery = 1.0 - smoothstep(0.05, 0.45, leeDropSlope);
  const shelterNear = smoothstep(0.02, 0.38, shelterNearSlope);
  const shelterFar = smoothstep(0.02, 0.38, Math.max(shelterMidSlope, shelterFarSlope));
  const reattachment = clamp(shelterFar - shelterNear, 0, 1)
    * wakeRecovery
    * smoothstep(-0.90, -0.05, slopeAlong)
    * (1.0 - smoothstep(0.05, 0.35, slopeAlong))
    * (0.35 + 0.65 * streamlining);
  const exposure = clamp(1.0 - 0.82 * separatedWake + 0.48 * reattachment, 0.12, 1.12);

  // Engine speed uniform when the wind season plays: 0.25 + 0.95 * speed.
  // Continuity: flow bent around terrain speeds up along flanks / gaps,
  // and streamline compression speeds it over stoss slopes and crests.
  const baseSpeed = 0.25 + 0.95 * clamp(Number(p.windSpeed) || 0, 0, 1);
  const speed = baseSpeed * gust * exposure * crestSpeedup * (1 + 0.55 * wrapDeflect);

  return {
    dirX: lx, dirZ: lz, speed, gust, exposure, shadow,
    separation, separatedWake, reattachment, crestSpeedup,
    slopeAlong, broadSlopeAlong, phase,
    baseSpeed, wrapDeflect, height: hC,
  };
}

/** Reference maximum of `speed` (full speed slider, peak gust, unsheltered, flank speedup). */
export const WIND_SPEED_REF_MAX = 1.2 * 1.45 * 1.30;

/** Speed -> display colour (cool/slow -> mint -> warm/fast), as [r, g, b]. */
export function windSpeedColor(t) {
  const stops = [
    [0.00, 61, 123, 253],   // sheltered blue
    [0.45, 101, 230, 178],  // mint
    [0.75, 255, 209, 102],  // warm amber
    [1.00, 255, 122, 107],  // hot coral
  ];
  const x = clamp(t, 0, 1);
  for (let i = 1; i < stops.length; i++) {
    if (x <= stops[i][0]) {
      const [t0, r0, g0, b0] = stops[i - 1];
      const [t1, r1, g1, b1] = stops[i];
      const k = (x - t0) / Math.max(1e-6, t1 - t0);
      return [
        Math.round(r0 + (r1 - r0) * k),
        Math.round(g0 + (g1 - g0) * k),
        Math.round(b0 + (b1 - b0) * k),
      ];
    }
  }
  return [255, 122, 107];
}

/**
 * Build a regular grid of debug arrows over the terrain.
 *
 * @param {object} opts
 * @param {(x:number, z:number) => number} opts.heightAt total height sampler
 * @param {(x:number, z:number) => ({water:number, snow:number}|null)} [opts.gateAt]
 *        optional erosion gate: wind erosion is inactive under water/snow
 * @returns {{arrows: Array<object>, spacing: number, divisions: number}}
 */
export function buildWindVectorField(opts) {
  const {
    heightAt,
    gateAt = null,
    span = 500,
    divisions = 17,
    azimuthDeg = 35,
    windSpeed = 0.58,
    turbulence = 0.65,
    streamlining = 0.55,
    seed = 1,
    cellSize = 1,
    phase = 0,
    lift = 5,
  } = opts || {};
  const arrows = [];
  if (typeof heightAt !== 'function') return { arrows, spacing: 0, divisions };

  const n = Math.max(5, Math.min(33, Math.round(divisions)));
  const extent = span * 0.47;
  const spacing = (extent * 2) / (n - 1);
  const params = { azimuthDeg, windSpeed, turbulence, streamlining, seed, cellSize, phase };

  for (let j = 0; j < n; j++) {
    for (let i = 0; i < n; i++) {
      const wx = -extent + i * spacing;
      const wz = -extent + j * spacing;
      const v = sampleWindVector(params, wx, wz, heightAt);
      const speedNorm = clamp(v.speed / WIND_SPEED_REF_MAX, 0, 1);
      const len = spacing * (0.30 + 0.85 * speedNorm);
      const tipX = wx + v.dirX * len;
      const tipZ = wz + v.dirZ * len;
      let active = true;
      if (gateAt) {
        const g = gateAt(wx, wz);
        if (g && (g.water > 0.015 || g.snow > 0.04)) active = false;
      }
      arrows.push({
        x: wx,
        z: wz,
        y: v.height + lift,
        tipX,
        tipZ,
        tipY: heightAt(tipX, tipZ) + lift,
        dirX: v.dirX,
        dirZ: v.dirZ,
        speed: v.speed,
        speedNorm,
        exposure: v.exposure,
        shadow: v.shadow,
        separation: v.separation,
        reattachment: v.reattachment,
        crestSpeedup: v.crestSpeedup,
        wrapDeflect: v.wrapDeflect,
        active,
      });
    }
  }
  return { arrows, spacing, divisions: n };
}
