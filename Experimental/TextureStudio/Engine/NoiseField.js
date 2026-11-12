/*==========================================================================================================================================
                                                             NOISEFIELD.JS
==========================================================================================================================================*/
/* Seamless periodic 2D/3D coherent noise & shaping curves for Noise-Scaled Patterns, Modifiers, and Procedural Paint Generators. */

function hash2Periodic(ix, iy, rx, ry, seed) {
  const px = rx > 0 ? ((ix % rx) + rx) % rx : ix;
  const py = ry > 0 ? ((iy % ry) + ry) % ry : iy;
  let h = (px * 374761393 + py * 668265263 + seed * 1442695041) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  h = h ^ (h >>> 16);
  return (h >>> 0) / 4294967296;
}

function hash3(ix, iy, iz, seed) {
  let h = (ix * 374761393 + iy * 668265263 + iz * 2147483647 + seed * 1597334677) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

function fade(t) {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function grad2(ix, iy, fx, fy, rx, ry, seed) {
  const angle = hash2Periodic(ix, iy, rx, ry, seed) * Math.PI * 2;
  return Math.cos(angle) * fx + Math.sin(angle) * fy;
}

export function periodicGradientNoise2D(x, y, periodX = 0, periodY = 0, seed = 1337) {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const fx = x - ix;
  const fy = y - iy;
  const u = fade(fx);
  const v = fade(fy);

  const n00 = grad2(ix, iy, fx, fy, periodX, periodY, seed);
  const n10 = grad2(ix + 1, iy, fx - 1, fy, periodX, periodY, seed);
  const n01 = grad2(ix, iy + 1, fx, fy - 1, periodX, periodY, seed);
  const n11 = grad2(ix + 1, iy + 1, fx - 1, fy - 1, periodX, periodY, seed);

  const nx0 = lerp(n00, n10, u);
  const nx1 = lerp(n01, n11, u);
  return Math.max(0, Math.min(1, lerp(nx0, nx1, v) * 0.7071 + 0.5));
}

export function periodicWorley2D(x, y, periodX = 0, periodY = 0, seed = 1337) {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  let minD2 = 8.0;
  for (let dy = -1; dy <= 1; dy++) {
    for (let dx = -1; dx <= 1; dx++) {
      const cx = ix + dx;
      const cy = iy + dy;
      const jx = 0.15 + 0.7 * hash2Periodic(cx, cy, periodX, periodY, seed);
      const jy = 0.15 + 0.7 * hash2Periodic(cx, cy, periodX, periodY, seed + 911);
      const ddx = (cx + jx) - x;
      const ddy = (cy + jy) - y;
      const d2 = ddx * ddx + ddy * ddy;
      if (d2 < minD2) minD2 = d2;
    }
  }
  return Math.max(0, Math.min(1, 1.0 - Math.sqrt(minD2) * 0.92));
}

export function periodicWave2D(x, y, periodX = 0, periodY = 0, seed = 1337) {
  const phase = (seed % 360) * (Math.PI / 180);
  const px = periodX > 0 ? Math.max(1, Math.round(periodX)) : 4;
  const py = periodY > 0 ? Math.max(1, Math.round(periodY)) : 4;
  const w1 = Math.sin((x / px) * Math.PI * 2 * px + phase);
  const w2 = Math.cos((y / py) * Math.PI * 2 * py - phase * 0.7);
  const w3 = Math.sin(((x + y) * 0.5) * Math.PI * 2 + phase * 1.3) * 0.45;
  return Math.max(0, Math.min(1, ((w1 + w2) * 0.35 + w3) * 0.5 + 0.5));
}

export function fbm3D(x, y, z, octaves = 3, seed = 42) {
  let sum = 0;
  let amp = 0.5;
  let norm = 0;
  let freq = 1;
  for (let o = 0; o < octaves; o++) {
    const px = x * freq;
    const py = y * freq;
    const pz = z * freq;
    const ix = Math.floor(px), iy = Math.floor(py), iz = Math.floor(pz);
    const fx = fade(px - ix), fy = fade(py - iy), fz = fade(pz - iz);
    const s = seed + o * 101;
    const c000 = hash3(ix, iy, iz, s), c100 = hash3(ix + 1, iy, iz, s);
    const c010 = hash3(ix, iy + 1, iz, s), c110 = hash3(ix + 1, iy + 1, iz, s);
    const c001 = hash3(ix, iy, iz + 1, s), c101 = hash3(ix + 1, iy, iz + 1, s);
    const c011 = hash3(ix, iy + 1, iz + 1, s), c111 = hash3(ix + 1, iy + 1, iz + 1, s);
    const nx00 = lerp(c000, c100, fx), nx10 = lerp(c010, c110, fx);
    const nx01 = lerp(c001, c101, fx), nx11 = lerp(c011, c111, fx);
    const nxy0 = lerp(nx00, nx10, fy), nxy1 = lerp(nx01, nx11, fy);
    sum += lerp(nxy0, nxy1, fz) * amp;
    norm += amp;
    amp *= 0.5;
    freq *= 2.02;
  }
  return norm > 0 ? sum / norm : 0.5;
}

export function shapeNoiseValue(value, { contrast = 1.0, bias = 0.0, curve = "smoothstep", invert = false } = {}) {
  let t = Math.max(0, Math.min(1, value + bias));
  if (invert) t = 1 - t;
  if (curve === "smoothstep") {
    t = t * t * (3 - 2 * t);
  } else if (curve === "sharp") {
    t = Math.pow(t, 2.2);
  } else if (curve === "cubic") {
    t = t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
  } else if (curve === "threshold") {
    t = t >= 0.5 ? 1 : 0;
  }
  if (contrast !== 1.0) {
    t = Math.max(0, Math.min(1, (t - 0.5) * contrast + 0.5));
  }
  return t;
}

/**
 * Evaluate coherent noise in normalized tile space [0..1] x [0..1].
 * When `seamless` is true, frequencies snap to integer multiples so edges match identically across U/V wraps.
 */
export function evaluateTileNoise(u, v, config = {}) {
  const {
    noiseType = "perlin", // "perlin" | "fbm" | "ridged" | "worley" | "wave" | "radial"
    frequency = 3,
    octaves = 3,
    seed = 1337,
    offsetX = 0,
    offsetY = 0,
    stretchX = 1,
    stretchY = 1,
    angle = 0,
    seamless = true,
    contrast = 1.0,
    bias = 0.0,
    curve = "smoothstep",
    invert = false,
  } = config;

  if (noiseType === "radial") {
    const dx = (u - 0.5) * stretchX;
    const dy = (v - 0.5) * stretchY;
    const d = Math.max(0, Math.min(1, 1 - Math.hypot(dx, dy) * 2.0));
    return shapeNoiseValue(d, { contrast, bias, curve, invert });
  }

  const baseFreq = seamless ? Math.max(1, Math.round(frequency)) : Math.max(0.2, frequency);
  const rad = (angle * Math.PI) / 180;
  const cos = Math.cos(rad);
  const sin = Math.sin(rad);

  // Rotate only when non-seamless OR use integer lattice rotation carefully; keep periodic domain intact
  const cx = u - 0.5 + offsetX;
  const cy = v - 0.5 + offsetY;
  const rx = seamless ? u + offsetX : (cx * cos - cy * sin + 0.5);
  const ry = seamless ? v + offsetY : (cx * sin + cy * cos + 0.5);

  const sx = Math.max(1, Math.round(baseFreq * stretchX));
  const sy = Math.max(1, Math.round(baseFreq * stretchY));
  const fx = seamless ? rx * sx : rx * baseFreq * stretchX;
  const fy = seamless ? ry * sy : ry * baseFreq * stretchY;

  let value = 0.5;
  if (noiseType === "perlin") {
    value = periodicGradientNoise2D(fx, fy, seamless ? sx : 0, seamless ? sy : 0, seed);
  } else if (noiseType === "worley") {
    value = periodicWorley2D(fx, fy, seamless ? sx : 0, seamless ? sy : 0, seed);
  } else if (noiseType === "wave") {
    value = periodicWave2D(fx, fy, seamless ? sx : 0, seamless ? sy : 0, seed);
  } else {
    const oct = Math.max(1, Math.min(5, Math.round(octaves)));
    let total = 0;
    let amp = 0.5;
    let norm = 0;
    let mult = 1;
    for (let o = 0; o < oct; o++) {
      const px = seamless ? sx * mult : 0;
      const py = seamless ? sy * mult : 0;
      let n = periodicGradientNoise2D(fx * mult, fy * mult, px, py, seed + o * 197);
      if (noiseType === "ridged") {
        n = 1.0 - Math.abs(n * 2.0 - 1.0);
      }
      total += n * amp;
      norm += amp;
      amp *= 0.5;
      mult *= 2;
    }
    value = norm > 0 ? total / norm : 0.5;
  }

  return shapeNoiseValue(value, { contrast, bias, curve, invert });
}
