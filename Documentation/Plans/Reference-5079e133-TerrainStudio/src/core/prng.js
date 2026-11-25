/**
 * Deterministic PRNG (sfc32) and zero-allocation 3D spatial hashing
 * for reproducible procedural structural geology.
 */

export function hashSeed(seed) {
  const str = String(seed ?? 1337);
  let h1 = 1779033703, h2 = 3144134277, h3 = 1013904242, h4 = 2773480762;
  for (let i = 0; i < str.length; i++) {
    const k = str.charCodeAt(i);
    h1 = h2 ^ Math.imul(h1 ^ k, 597399067);
    h2 = h3 ^ Math.imul(h2 ^ k, 2869860233);
    h3 = h4 ^ Math.imul(h3 ^ k, 951274213);
    h4 = h1 ^ Math.imul(h4 ^ k, 2716044179);
  }
  h1 = Math.imul(h3 ^ (h1 >>> 18), 597399067);
  h2 = Math.imul(h4 ^ (h2 >>> 22), 2869860233);
  h3 = Math.imul(h1 ^ (h3 >>> 17), 951274213);
  h4 = Math.imul(h2 ^ (h4 >>> 19), 2716044179);
  return [(h1 ^ h2 ^ h3 ^ h4) >>> 0, (h2 ^ h1) >>> 0, (h3 ^ h1) >>> 0, (h4 ^ h1) >>> 0];
}

export function createRng(seed = 1337) {
  let [a, b, c, d] = hashSeed(seed);

  function nextU32() {
    a >>>= 0; b >>>= 0; c >>>= 0; d >>>= 0;
    let t = (a + b) | 0;
    a = b ^ (b >>> 9);
    b = (c + (c << 3)) | 0;
    c = (c << 21) | (c >>> 11);
    d = (d + 1) | 0;
    t = (t + d) | 0;
    c = (c + t) | 0;
    return t >>> 0;
  }

  function float() {
    return nextU32() / 4294967296;
  }

  function range(min, max) {
    return min + (max - min) * float();
  }

  function signed() {
    return float() * 2 - 1;
  }

  function int(min, maxInclusive) {
    return min + Math.floor(float() * (maxInclusive - min + 1));
  }

  function chance(prob) {
    return float() < prob;
  }

  let spareGauss = null;
  function gauss(mean = 0, std = 1) {
    if (spareGauss !== null) {
      const v = spareGauss;
      spareGauss = null;
      return mean + std * v;
    }
    let u = 0, v = 0, s = 0;
    do {
      u = float() * 2 - 1;
      v = float() * 2 - 1;
      s = u * u + v * v;
    } while (s >= 1 || s === 0);
    const mul = Math.sqrt((-2 * Math.log(s)) / s);
    spareGauss = v * mul;
    return mean + std * (u * mul);
  }

  function logNormal(median = 1, sigma = 0.45) {
    return median * Math.exp(gauss(0, sigma));
  }

  function pick(arr) {
    return arr[Math.floor(float() * arr.length)];
  }

  function unitVec3() {
    const z = float() * 2 - 1;
    const phi = float() * Math.PI * 2;
    const r = Math.sqrt(Math.max(0, 1 - z * z));
    return [r * Math.cos(phi), z, r * Math.sin(phi)];
  }

  function fork(label) {
    return createRng(`${seed}:${label}:${nextU32()}`);
  }

  return {
    nextU32,
    float,
    range,
    signed,
    int,
    chance,
    gauss,
    logNormal,
    pick,
    unitVec3,
    fork,
  };
}

/**
 * Fast zero-allocation 3D integer hash -> uint32
 */
export function hash3u(ix, iy, iz, salt = 0) {
  let h = (Math.imul(ix, 374761393) + Math.imul(iy, 668265263) + Math.imul(iz, 2147483647) + Math.imul(salt, 1597334677)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return (h ^ (h >>> 16)) >>> 0;
}

export function u32ToUnit(h) {
  return (h >>> 0) * 2.3283064365386963e-10;
}
