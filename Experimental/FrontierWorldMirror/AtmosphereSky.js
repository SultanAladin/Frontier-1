//==========================================================================================================================================
//                                                      ATMOSPHERESKY.JS
//==========================================================================================================================================
// Physical Bruneton/Hillaire Rayleigh + Mie + Ozone Sky-View LUT, Solar Transmittance,
// Volumetric 3D Cloud Raymarcher (CPU parity), Moon Phase/Albedos, and Deterministic Starfield.

export const SKY_VIEW_WIDTH = 96;
export const SKY_VIEW_HEIGHT = 48;

const EARTH_RADIUS_M = 6360000.0;
const BASE_ATMO_TOP_M = 60000.0;
const BASE_RAYLEIGH_SCATTER = [5.802e-6, 13.558e-6, 33.1e-6];
const BASE_MIE_SCATTER = 3.996e-6;
const BASE_MIE_EXTINCTION = 4.40e-6;
const BASE_OZONE_ABS = [0.650e-6, 1.881e-6, 0.085e-6];
const OBSERVER_ALT_M = 100.0;

export const MOON_PRESET_TINTS = [
  [0.94, 0.93, 0.90], // 0: Luna
  [1.00, 0.56, 0.34], // 1: Ember
  [0.76, 0.90, 1.00], // 2: Glacier
  [0.98, 0.88, 0.42], // 3: Sulfur
  [0.72, 0.74, 0.82], // 4: Shroud
  [0.84, 0.96, 0.94], // 5: Shard
];

function Clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function IntersectSphereDist(originZ, dirZ, radius) {
  const b = originZ * dirZ;
  const c = originZ * originZ - radius * radius;
  const disc = b * b - c;
  if (disc < 0.0) return [-1.0, -1.0];
  const root = Math.sqrt(disc);
  return [-b - root, -b + root];
}

function IntersectSphereVec(ox, oy, oz, dx, dy, dz, radius) {
  const b = ox * dx + oy * dy + oz * dz;
  const c = ox * ox + oy * oy + oz * oz - radius * radius;
  const disc = b * b - c;
  if (disc < 0.0) return [-1.0, -1.0];
  const root = Math.sqrt(disc);
  return [-b - root, -b + root];
}

export function ReconstructSkyViewDirection(u, v) {
  const azimuth = (u - 0.5) * 2.0 * Math.PI;
  const signed = (0.5 - v) * 2.0;
  const elevation = Math.sign(signed) * signed * signed * (Math.PI * 0.5);
  const cosE = Math.cos(elevation);
  return [cosE * Math.cos(azimuth), cosE * Math.sin(azimuth), Math.sin(elevation)];
}

export function ProjectSkyViewCoordinates(dx, dy, dz) {
  const azimuth = Math.atan2(dy, dx);
  const elevation = Math.asin(Clamp(dz, -1.0, 1.0));
  const u = azimuth / (2.0 * Math.PI) + 0.5;
  const v = 0.5 - 0.5 * Math.sign(elevation) * Math.sqrt(Math.abs(elevation) / (Math.PI * 0.5));
  return [u, v];
}

export function ComputeSolarTransmittance(sunDir, atmo = {}) {
  const rayleighMul = Math.max(0.0, Number(atmo.rayleigh ?? 1.0));
  const mieMul = Math.max(0.0, Number(atmo.mie ?? 1.0));
  const ozoneMul = Math.max(0.0, Number(atmo.ozone ?? 1.2));
  const rayleighH = Math.max(500.0, Number(atmo.rayleighH ?? 8000.0));
  const mieH = Math.max(200.0, Number(atmo.mieH ?? 1200.0));
  const atmoTop = Math.max(20000.0, Number(atmo.atmoTop ?? BASE_ATMO_TOP_M));
  const atmoR = EARTH_RADIUS_M + atmoTop;

  const origZ = EARTH_RADIUS_M + OBSERVER_ALT_M;
  const [gNear] = IntersectSphereDist(origZ, sunDir[2], EARTH_RADIUS_M);
  if (gNear > 0.0) return [0.0, 0.0, 0.0];
  const [, exit] = IntersectSphereDist(origZ, sunDir[2], atmoR);
  if (exit <= 0.0) return [1.0, 1.0, 1.0];

  const samples = 48;
  const step = exit / samples;
  let dR = 0.0, dG = 0.0, dB = 0.0;
  for (let i = 0; i < samples; ++i) {
    const t = (i + 0.5) * step;
    const px = sunDir[0] * t;
    const py = sunDir[1] * t;
    const pz = origZ + sunDir[2] * t;
    const alt = Math.max(0.0, Math.hypot(px, py, pz) - EARTH_RADIUS_M);
    const denR = Math.exp(-alt / rayleighH) * rayleighMul;
    const denM = Math.exp(-alt / mieH) * mieMul;
    const denO = Math.max(0.0, 1.0 - Math.abs(alt - 25000.0) / 15000.0) * ozoneMul;
    const mieE = BASE_MIE_EXTINCTION * denM;
    dR += (BASE_RAYLEIGH_SCATTER[0] * denR + mieE + BASE_OZONE_ABS[0] * denO) * step;
    dG += (BASE_RAYLEIGH_SCATTER[1] * denR + mieE + BASE_OZONE_ABS[1] * denO) * step;
    dB += (BASE_RAYLEIGH_SCATTER[2] * denR + mieE + BASE_OZONE_ABS[2] * denO) * step;
  }
  return [Math.exp(-dR), Math.exp(-dG), Math.exp(-dB)];
}

export function GetSkyViewCacheKey(sunDir, atmo = {}) {
  return [
    sunDir[0].toFixed(4),
    sunDir[1].toFixed(4),
    sunDir[2].toFixed(4),
    Number(atmo.rayleigh ?? 1.0).toFixed(3),
    Number(atmo.mie ?? 1.0).toFixed(3),
    Number(atmo.mieG ?? 0.78).toFixed(3),
    Number(atmo.ozone ?? 1.2).toFixed(3),
    Math.round(atmo.rayleighH ?? 8000),
    Math.round(atmo.mieH ?? 1200),
    Math.round(atmo.atmoTop ?? BASE_ATMO_TOP_M),
    Number(atmo.horizonGlow ?? 1.0).toFixed(2),
    Number(atmo.skyBrightness ?? 1.0).toFixed(2),
    (atmo.skyTint || [1, 1, 1]).map((c) => c.toFixed(2)).join(","),
    (atmo.groundAlbedo || [0.22, 0.19, 0.15]).map((c) => c.toFixed(2)).join(","),
    atmo.enabled === false ? "0" : "1",
  ].join("|");
}

const TRANS_W = 128;
const TRANS_H = 32;

function SampleTransmittanceLut(tLut, alt, cosZ, atmoTop) {
  const u = Clamp(0.5 + 0.5 * Math.sign(cosZ) * Math.pow(Math.abs(cosZ), 0.35), 0.0, 1.0);
  const v = Clamp(alt / atmoTop, 0.0, 1.0);
  const fx = u * (TRANS_W - 1);
  const fy = v * (TRANS_H - 1);
  const x0 = Math.min(TRANS_W - 2, Math.floor(fx));
  const y0 = Math.min(TRANS_H - 2, Math.floor(fy));
  const tx = fx - x0;
  const ty = fy - y0;
  const i00 = (y0 * TRANS_W + x0) * 3;
  const i10 = i00 + 3;
  const i01 = i00 + TRANS_W * 3;
  const i11 = i01 + 3;
  const lerp = (a, b, t) => a + (b - a) * t;
  return [
    lerp(lerp(tLut[i00], tLut[i10], tx), lerp(tLut[i01], tLut[i11], tx), ty),
    lerp(lerp(tLut[i00 + 1], tLut[i10 + 1], tx), lerp(tLut[i01 + 1], tLut[i11 + 1], tx), ty),
    lerp(lerp(tLut[i00 + 2], tLut[i10 + 2], tx), lerp(tLut[i01 + 2], tLut[i11 + 2], tx), ty),
  ];
}

function BuildTransmittanceLut(rayleighMul, mieMul, ozoneMul, rayleighH, mieH, atmoTop) {
  const lut = new Float32Array(TRANS_W * TRANS_H * 3);
  const atmoR = EARTH_RADIUS_M + atmoTop;
  const steps = 16;
  for (let y = 0; y < TRANS_H; ++y) {
    const v = y / (TRANS_H - 1);
    const alt = v * atmoTop;
    const r = EARTH_RADIUS_M + alt;
    for (let x = 0; x < TRANS_W; ++x) {
      const u = x / (TRANS_W - 1);
      const signed = (u - 0.5) * 2.0;
      const cosZ = Math.sign(signed) * Math.pow(Math.abs(signed), 1.0 / 0.35);
      const sinZ = Math.sqrt(Math.max(0.0, 1.0 - cosZ * cosZ));
      const [gNear] = IntersectSphereVec(0.0, 0.0, r, sinZ, 0.0, cosZ, EARTH_RADIUS_M);
      const idx = (y * TRANS_W + x) * 3;
      if (gNear > 1e-2) {
        lut[idx] = 0.0; lut[idx + 1] = 0.0; lut[idx + 2] = 0.0;
        continue;
      }
      const [, exit] = IntersectSphereVec(0.0, 0.0, r, sinZ, 0.0, cosZ, atmoR);
      const step = Math.max(0.0, exit) / steps;
      let dR = 0.0, dG = 0.0, dB = 0.0;
      for (let s = 0; s < steps; ++s) {
        const lt = (s + 0.5) * step;
        const lx = sinZ * lt;
        const lz = r + cosZ * lt;
        const pAlt = Math.max(0.0, Math.hypot(lx, lz) - EARTH_RADIUS_M);
        const denR = Math.exp(-pAlt / rayleighH) * rayleighMul;
        const denM = Math.exp(-pAlt / mieH) * mieMul;
        const denO = Math.max(0.0, 1.0 - Math.abs(pAlt - 25000.0) / 15000.0) * ozoneMul;
        const mieE = BASE_MIE_EXTINCTION * denM;
        dR += (BASE_RAYLEIGH_SCATTER[0] * denR + mieE + BASE_OZONE_ABS[0] * denO) * step;
        dG += (BASE_RAYLEIGH_SCATTER[1] * denR + mieE + BASE_OZONE_ABS[1] * denO) * step;
        dB += (BASE_RAYLEIGH_SCATTER[2] * denR + mieE + BASE_OZONE_ABS[2] * denO) * step;
      }
      lut[idx] = Math.exp(-dR);
      lut[idx + 1] = Math.exp(-dG);
      lut[idx + 2] = Math.exp(-dB);
    }
  }
  return lut;
}

// Computes Hillaire/Bruneton Sky-View LUT (192x108 RGBA32F) with Rayleigh + Mie + Ozone + Multi-Scatter
export function BuildSkyViewLut(sunDir, atmo = {}, outBuf = null) {
  const w = SKY_VIEW_WIDTH;
  const h = SKY_VIEW_HEIGHT;
  const data = outBuf && outBuf.length === w * h * 4 ? outBuf : new Float32Array(w * h * 4);

  const rayleighMul = Math.max(0.0, Number(atmo.rayleigh ?? 1.0));
  const mieMul = Math.max(0.0, Number(atmo.mie ?? 1.0));
  const mieG = Clamp(Number(atmo.mieG ?? 0.78), 0.0, 0.94);
  const ozoneMul = Math.max(0.0, Number(atmo.ozone ?? 1.2));
  const rayleighH = Math.max(500.0, Number(atmo.rayleighH ?? 8000.0));
  const mieH = Math.max(200.0, Number(atmo.mieH ?? 1200.0));
  const atmoTop = Math.max(20000.0, Number(atmo.atmoTop ?? BASE_ATMO_TOP_M));
  const atmoR = EARTH_RADIUS_M + atmoTop;
  const horizonGlow = Math.max(0.0, Number(atmo.horizonGlow ?? 1.0));
  const skyBright = Math.max(0.0, Number(atmo.skyBrightness ?? 1.0));
  const skyTint = atmo.skyTint || [1.0, 1.0, 1.0];
  const groundAlb = atmo.groundAlbedo || [0.22, 0.19, 0.15];
  const atmoEnabled = atmo.enabled !== false;

  const origZ = EARTH_RADIUS_M + OBSERVER_ALT_M;
  const g2 = mieG * mieG;
  const viewSteps = 14;

  // Sun elevation factor for twilight / night sky floor
  const sunElev = Math.asin(Clamp(sunDir[2], -1.0, 1.0));
  const sunZenithT = ComputeSolarTransmittance(sunDir, atmo);
  const transLut = BuildTransmittanceLut(rayleighMul, mieMul, ozoneMul, rayleighH, mieH, atmoTop);
  const solarIrrScale = 14.5; // physical radiance scale matching FrontierWorldMirror ACES exposure

  const sunTransmittanceAtPoint = (px, py, pz) => {
    const r = Math.hypot(px, py, pz);
    const cosZ = (px * sunDir[0] + py * sunDir[1] + pz * sunDir[2]) / Math.max(1.0, r);
    const alt = Math.max(0.0, r - EARTH_RADIUS_M);
    const u = Clamp(0.5 + 0.5 * Math.sign(cosZ) * Math.pow(Math.abs(cosZ), 0.35), 0.0, 1.0);
    const v = Clamp(alt / atmoTop, 0.0, 1.0);
    const fx = u * (TRANS_W - 1);
    const fy = v * (TRANS_H - 1);
    const x0 = Math.min(TRANS_W - 2, Math.floor(fx));
    const y0 = Math.min(TRANS_H - 2, Math.floor(fy));
    const tx = fx - x0;
    const ty = fy - y0;
    const i00 = (y0 * TRANS_W + x0) * 3;
    const i10 = i00 + 3;
    const i01 = i00 + TRANS_W * 3;
    const i11 = i01 + 3;
    stOutR = (transLut[i00] + (transLut[i10] - transLut[i00]) * tx) * (1 - ty) + (transLut[i01] + (transLut[i11] - transLut[i01]) * tx) * ty;
    stOutG = (transLut[i00 + 1] + (transLut[i10 + 1] - transLut[i00 + 1]) * tx) * (1 - ty) + (transLut[i01 + 1] + (transLut[i11 + 1] - transLut[i01 + 1]) * tx) * ty;
    stOutB = (transLut[i00 + 2] + (transLut[i10 + 2] - transLut[i00 + 2]) * tx) * (1 - ty) + (transLut[i01 + 2] + (transLut[i11 + 2] - transLut[i01 + 2]) * tx) * ty;
  };
  let stOutR = 1.0, stOutG = 1.0, stOutB = 1.0;

  for (let y = 0; y < h; ++y) {
    const vCoord = (y + 0.5) / h;
    for (let x = 0; x < w; ++x) {
      const uCoord = (x + 0.5) / w;
      const [dx, dy, rawDz] = ReconstructSkyViewDirection(uCoord, vCoord);
      const isGroundRay = rawDz < 0.0;
      const dz = Math.max(rawDz, 0.0);
      const dLen = Math.hypot(dx, dy, dz) || 1.0;
      const rdx = dx / dLen, rdy = dy / dLen, rdz = Math.max(dz / dLen, 1e-4);

      if (!atmoEnabled) {
        const idx = (y * w + x) * 4;
        data[idx + 0] = 0.008;
        data[idx + 1] = 0.010;
        data[idx + 2] = 0.016;
        data[idx + 3] = 1.0;
        continue;
      }

      const [gNear] = IntersectSphereDist(origZ, rdz, EARTH_RADIUS_M);
      let [, travel] = IntersectSphereDist(origZ, rdz, atmoR);
      if (gNear > 0.0 && gNear < travel) travel = gNear;

      const step = travel / viewSteps;
      const cosTheta = rdx * sunDir[0] + rdy * sunDir[1] + rdz * sunDir[2];
      const phaseR = (3.0 / (16.0 * Math.PI)) * (1.0 + cosTheta * cosTheta);
      const phaseM =
        (3.0 / (8.0 * Math.PI)) *
        (((1.0 - g2) * (1.0 + cosTheta * cosTheta)) /
          ((2.0 + g2) * Math.pow(Math.max(1e-4, 1.0 + g2 - 2.0 * mieG * cosTheta), 1.5)));

      let tR = 1.0, tG = 1.0, tB = 1.0;
      let radR = 0.0, radG = 0.0, radB = 0.0;
      let msR = 0.0, msG = 0.0, msB = 0.0;

      for (let s = 0; s < viewSteps; ++s) {
        const dist = (s + 0.5) * step;
        const px = rdx * dist;
        const py = rdy * dist;
        const pz = origZ + rdz * dist;
        const alt = Math.max(0.0, Math.hypot(px, py, pz) - EARTH_RADIUS_M);

        const denR = Math.exp(-alt / rayleighH) * rayleighMul;
        const denM = Math.exp(-alt / mieH) * mieMul;
        const denO = Math.max(0.0, 1.0 - Math.abs(alt - 25000.0) / 15000.0) * ozoneMul;

        const sRayR = BASE_RAYLEIGH_SCATTER[0] * denR;
        const sRayG = BASE_RAYLEIGH_SCATTER[1] * denR;
        const sRayB = BASE_RAYLEIGH_SCATTER[2] * denR;
        const sMie = BASE_MIE_SCATTER * denM;
        const eMie = BASE_MIE_EXTINCTION * denM;

        const extR = Math.max(1e-12, sRayR + eMie + BASE_OZONE_ABS[0] * denO);
        const extG = Math.max(1e-12, sRayG + eMie + BASE_OZONE_ABS[1] * denO);
        const extB = Math.max(1e-12, sRayB + eMie + BASE_OZONE_ABS[2] * denO);

        const segTR = Math.exp(-extR * step);
        const segTG = Math.exp(-extG * step);
        const segTB = Math.exp(-extB * step);

        const intR = (1.0 - segTR) / extR;
        const intG = (1.0 - segTG) / extG;
        const intB = (1.0 - segTB) / extB;

        sunTransmittanceAtPoint(px, py, pz);

        const scR = sRayR * phaseR + sMie * phaseM;
        const scG = sRayG * phaseR + sMie * phaseM;
        const scB = sRayB * phaseR + sMie * phaseM;

        radR += tR * scR * stOutR * intR;
        radG += tG * scG * stOutG * intG;
        radB += tB * scB * stOutB * intB;

        // Hillaire multiple-scattering isotropic contribution + ground bounce
        const isoScatter = 1.0 / (4.0 * Math.PI);
        const msBounce = 0.24 + 0.18 * (groundAlb[0] + groundAlb[1] + groundAlb[2]) * 0.333;
        msR += tR * (sRayR + sMie) * isoScatter * (stOutR * 0.65 + sunZenithT[0] * 0.35) * msBounce * intR;
        msG += tG * (sRayG + sMie) * isoScatter * (stOutG * 0.65 + sunZenithT[1] * 0.35) * msBounce * intG;
        msB += tB * (sRayB + sMie) * isoScatter * (stOutB * 0.65 + sunZenithT[2] * 0.35) * msBounce * intB;

        tR *= segTR;
        tG *= segTG;
        tB *= segTB;
      }

      // Horizon glow enhancement & twilight/night airglow floor (physically dark night sky)
      const horizFactor = Math.exp(-Math.max(0.0, rdz) * 7.5);
      const glowScale = 1.0 + (horizonGlow - 1.0) * horizFactor * 0.55;
      const dayVis = Clamp((sunElev * 180.0 / Math.PI + 6.0) / 14.0, 0.0, 1.0);
      const nightR = 0.0007 + 0.0009 * horizFactor;
      const nightG = 0.0010 + 0.0013 * horizFactor;
      const nightB = 0.0022 + 0.0024 * horizFactor;

      let outR = ((radR + msR) * solarIrrScale * glowScale + nightR * (1.0 - dayVis * 0.9)) * skyTint[0] * skyBright;
      let outG = ((radG + msG) * solarIrrScale * glowScale + nightG * (1.0 - dayVis * 0.9)) * skyTint[1] * skyBright;
      let outB = ((radB + msB) * solarIrrScale * glowScale + nightB * (1.0 - dayVis * 0.9)) * skyTint[2] * skyBright;

      // Below-horizon ground bounce for reflections & lower hemisphere ambient
      if (isGroundRay) {
        const sunUp = Math.max(0.0, sunDir[2]);
        const groundBounce = 0.24 * sunUp;
        const gR = groundAlb[0] * (sunZenithT[0] * sunUp * 0.8 + outR * 0.35 + groundBounce * 0.08 + 0.0005);
        const gG = groundAlb[1] * (sunZenithT[1] * sunUp * 0.8 + outG * 0.35 + groundBounce * 0.08 + 0.0006);
        const gB = groundAlb[2] * (sunZenithT[2] * sunUp * 0.8 + outB * 0.35 + groundBounce * 0.08 + 0.0010);
        const blend = Clamp(-rawDz * 3.5, 0.0, 1.0);
        outR = outR * (1.0 - blend) + gR * blend;
        outG = outG * (1.0 - blend) + gG * blend;
        outB = outB * (1.0 - blend) + gB * blend;
      }

      const idx = (y * w + x) * 4;
      data[idx + 0] = Math.max(0.0, outR);
      data[idx + 1] = Math.max(0.0, outG);
      data[idx + 2] = Math.max(0.0, outB);
      data[idx + 3] = 1.0;
    }
  }

  return data;
}

export function SampleSkyViewLutCpu(lut, dx, dy, dz) {
  if (!lut) return [0.35, 0.45, 0.65];
  const [u0, v0] = ProjectSkyViewCoordinates(dx, dy, dz);
  const u = ((u0 % 1.0) + 1.0) % 1.0;
  const v = Clamp(v0, 0.0, 1.0);
  const fx = u * SKY_VIEW_WIDTH - 0.5;
  const fy = v * SKY_VIEW_HEIGHT - 0.5;
  const x0 = ((Math.floor(fx) % SKY_VIEW_WIDTH) + SKY_VIEW_WIDTH) % SKY_VIEW_WIDTH;
  const x1 = (x0 + 1) % SKY_VIEW_WIDTH;
  const y0 = Clamp(Math.floor(fy), 0, SKY_VIEW_HEIGHT - 1);
  const y1 = Clamp(y0 + 1, 0, SKY_VIEW_HEIGHT - 1);
  const tx = fx - Math.floor(fx);
  const ty = fy - Math.floor(fy);

  const i00 = (y0 * SKY_VIEW_WIDTH + x0) * 4;
  const i10 = (y0 * SKY_VIEW_WIDTH + x1) * 4;
  const i01 = (y1 * SKY_VIEW_WIDTH + x0) * 4;
  const i11 = (y1 * SKY_VIEW_WIDTH + x1) * 4;

  const mix0 = (a, b, t) => a + (b - a) * t;
  return [
    mix0(mix0(lut[i00], lut[i10], tx), mix0(lut[i01], lut[i11], tx), ty),
    mix0(mix0(lut[i00 + 1], lut[i10 + 1], tx), mix0(lut[i01 + 1], lut[i11 + 1], tx), ty),
    mix0(mix0(lut[i00 + 2], lut[i10 + 2], tx), mix0(lut[i01 + 2], lut[i11 + 2], tx), ty),
  ];
}

// CPU 3D Value-Noise FBM for volumetric cloud parity in CpuGtxFallbackRenderer
function Hash3(ix, iy, iz) {
  let n = (ix * 374761393 + iy * 668265263 + iz * 2147483647) | 0;
  n = (n ^ (n >>> 13)) * 1274126177;
  return ((n ^ (n >>> 16)) >>> 0) / 4294967296.0;
}

function Noise3(x, y, z) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
  const fx = x - ix, fy = y - iy, fz = z - iz;
  const ux = fx * fx * (3.0 - 2.0 * fx);
  const uy = fy * fy * (3.0 - 2.0 * fy);
  const uz = fz * fz * (3.0 - 2.0 * fz);
  const lerp = (a, b, t) => a + (b - a) * t;
  const n000 = Hash3(ix, iy, iz), n100 = Hash3(ix + 1, iy, iz);
  const n010 = Hash3(ix, iy + 1, iz), n110 = Hash3(ix + 1, iy + 1, iz);
  const n001 = Hash3(ix, iy, iz + 1), n101 = Hash3(ix + 1, iy, iz + 1);
  const n011 = Hash3(ix, iy + 1, iz + 1), n111 = Hash3(ix + 1, iy + 1, iz + 1);
  return lerp(
    lerp(lerp(n000, n100, ux), lerp(n010, n110, ux), uy),
    lerp(lerp(n001, n101, ux), lerp(n011, n111, ux), uy),
    uz,
  );
}

function CloudFbm3(x, y, z) {
  let sum = 0.0, amp = 0.52, freq = 1.0;
  for (let o = 0; o < 4; ++o) {
    sum += amp * Noise3(x * freq, y * freq, z * freq);
    freq *= 2.07;
    amp *= 0.48;
  }
  return sum;
}

export function EvalVolumetricCloudsCpu(dx, dy, dz, sunDir, sunIrr, skyCol, clouds, timeSec = 0.0) {
  if (!clouds?.enabled || dz <= 0.008 || clouds.coverage <= 0.01 || clouds.density <= 0.01) {
    return { rad: [0, 0, 0], trans: 1.0 };
  }
  const baseM = Math.max(200.0, clouds.baseM ?? 1400.0);
  const thickM = Math.max(150.0, clouds.thicknessM ?? 1100.0);
  const rInner = EARTH_RADIUS_M + baseM;
  const rOuter = EARTH_RADIUS_M + baseM + thickM;
  const origZ = EARTH_RADIUS_M + OBSERVER_ALT_M;

  const [, tEnter] = IntersectSphereDist(origZ, dz, rInner);
  const [, tExit] = IntersectSphereDist(origZ, dz, rOuter);
  const maxDist = Math.min(tExit, 48000.0);
  if (maxDist <= tEnter || tEnter < 0.0) return { rad: [0, 0, 0], trans: 1.0 };

  const steps = 16;
  const dt = (maxDist - tEnter) / steps;
  const fScale = 0.00065 / Math.max(0.08, clouds.featureScale ?? 0.35);
  const wx = (clouds.windX ?? 6.0) * timeSec;
  const wy = (clouds.windY ?? 2.0) * timeSec;
  const cov = Clamp(clouds.coverage ?? 0.34, 0.0, 1.0);
  const densMul = Math.max(0.0, clouds.density ?? 2.4) * 0.008;
  const anvil = Clamp(clouds.anvil ?? 0.5, 0.0, 1.0);
  const g = Clamp(clouds.anisotropy ?? 0.45, -0.5, 0.9);
  const cosTheta = dx * sunDir[0] + dy * sunDir[1] + dz * sunDir[2];
  const hg = (gg) => (1.0 - gg * gg) / (4.0 * Math.PI * Math.pow(Math.max(1e-3, 1.0 + gg * gg - 2.0 * gg * cosTheta), 1.5));
  const phase = 0.78 * hg(g) + 0.22 * hg(-0.25);

  const sampleDensity = (px, py, pz) => {
    const alt = Math.hypot(px, py, pz) - EARTH_RADIUS_M;
    const h = Clamp((alt - baseM) / thickM, 0.0, 1.0);
    const bot = Clamp(h / 0.16, 0.0, 1.0);
    const top = Clamp((1.0 - h) / (0.28 - 0.16 * anvil), 0.0, 1.0);
    const profile = bot * top;
    if (profile <= 0.001) return 0.0;
    const shapeCov = Clamp(cov + anvil * h * h * 0.25, 0.0, 1.0);
    const n = CloudFbm3((px + wx) * fScale, (py + wy) * fScale, (alt - baseM) * fScale * 1.4);
    const nNorm = Clamp((n - 0.28) / 0.44, 0.0, 1.0);
    const thr = 1.0 - shapeCov;
    return Math.max(0.0, (nNorm - thr) / Math.max(0.18, shapeCov)) * profile * densMul;
  };

  let trans = 1.0;
  let rR = 0.0, rG = 0.0, rB = 0.0;
  for (let i = 0; i < steps; ++i) {
    const t = tEnter + (i + 0.5) * dt;
    const px = dx * t, py = dy * t, pz = origZ + dz * t;
    const d = sampleDensity(px, py, pz);
    if (d > 1e-6) {
      let sunTau = 0.0;
      if (clouds.shadowEnabled !== false) {
        const sStep = (thickM * 0.55) / 3.0;
        for (let s = 1; s <= 3; ++s) {
          sunTau += sampleDensity(px + sunDir[0] * s * sStep, py + sunDir[1] * s * sStep, pz + sunDir[2] * s * sStep) * sStep;
        }
      }
      const sunAtt = Math.exp(-sunTau * 1.15) * (0.35 + 0.65 * (1.0 - Math.exp(-sunTau * 2.2 + 0.25)));
      const segT = Math.exp(-d * dt);
      const scatter = (1.0 - segT);
      const alt = Math.hypot(px, py, pz) - EARTH_RADIUS_M;
      const h = Clamp((alt - baseM) / thickM, 0.0, 1.0);
      const amb = 0.28 + 0.52 * h;
      const litR = sunIrr[0] * sunAtt * phase * 2.4 + skyCol[0] * amb * 0.55;
      const litG = sunIrr[1] * sunAtt * phase * 2.4 + skyCol[1] * amb * 0.55;
      const litB = sunIrr[2] * sunAtt * phase * 2.4 + skyCol[2] * amb * 0.55;
      rR += trans * litR * scatter;
      rG += trans * litG * scatter;
      rB += trans * litB * scatter;
      trans *= segT;
      if (trans < 0.015) break;
    }
  }
  const haze = 1.0 - Math.exp(-tEnter * 0.00004);
  rR = rR * (1.0 - haze) + skyCol[0] * (1.0 - trans) * haze;
  rG = rG * (1.0 - haze) + skyCol[1] * (1.0 - trans) * haze;
  rB = rB * (1.0 - haze) + skyCol[2] * (1.0 - trans) * haze;
  return { rad: [rR, rG, rB], trans };
}

export function EvalMoonAndStarsCpu(dx, dy, dz, sunDir, moon, stars, timeSec = 0.0) {
  let outR = 0.0, outG = 0.0, outB = 0.0;
  // Moon disc with 3D spherical phase & mare/crater detail
  if (moon?.visible && moon.dir && dz > -0.05) {
    const mx = moon.dir[0], my = moon.dir[1], mz = moon.dir[2];
    const cosM = dx * mx + dy * my + dz * mz;
    const angR = moon.sizeRad ?? 0.012;
    const cosEdge = Math.cos(angR);
    const glow = Math.pow(Math.max(0.0, cosM), 420.0) * (moon.glow ?? 0.8) * 0.28 * (moon.bright ?? 1.6);
    const tint = moon.tint || [0.94, 0.93, 0.90];
    outR += tint[0] * glow;
    outG += tint[1] * glow;
    outB += tint[2] * glow;

    if (cosM > cosEdge) {
      // Tangent basis on moon sphere
      const upRef = Math.abs(mz) < 0.98 ? [0, 0, 1] : [1, 0, 0];
      let tx = upRef[1] * mz - upRef[2] * my;
      let ty = upRef[2] * mx - upRef[0] * mz;
      let tz = upRef[0] * my - upRef[1] * mx;
      const tl = Math.hypot(tx, ty, tz) || 1.0;
      tx /= tl; ty /= tl; tz /= tl;
      const bx = my * tz - mz * ty, by = mz * tx - mx * tz, bz = mx * ty - my * tx;
      const px = (dx * tx + dy * ty + dz * tz) / Math.max(1e-4, angR);
      const py = (dx * bx + dy * by + dz * bz) / Math.max(1e-4, angR);
      const r2 = px * px + py * py;
      if (r2 < 1.0) {
        const pz = Math.sqrt(Math.max(0.0, 1.0 - r2));
        // Lunar phase angle from 0 (new) -> 0.5 (full) -> 1.0 (new)
        const phAng = ((moon.phase ?? 0.62) - 0.5) * Math.PI * 2.0;
        const lx = Math.sin(phAng), ly = 0.18, lz = Math.cos(phAng);
        const ll = Math.hypot(lx, ly, lz);
        const ndl = Math.max(0.0, (px * lx + py * ly + pz * lz) / ll);
        const mare = 0.68 + 0.32 * Noise3(px * 3.8 + 4.1, py * 3.8 - 2.3, pz * 3.8);
        const edgeAA = Clamp((1.0 - Math.sqrt(r2)) * 14.0, 0.0, 1.0);
        const lum = (0.015 + ndl * 1.45) * mare * (moon.bright ?? 1.6) * edgeAA;
        outR += tint[0] * lum;
        outG += tint[1] * lum;
        outB += tint[2] * lum;
      }
    }
  }

  // Deterministic procedural starfield
  if (stars?.enabled && dz > 0.005) {
    const dayFade = Clamp(1.0 - (sunDir[2] + 0.08) * 5.0, 0.0, 1.0);
    if (dayFade > 0.01) {
      const rot = stars.rotationRad ?? 0.0;
      const cr = Math.cos(rot), sr = Math.sin(rot);
      const rx = dx * cr - dy * sr;
      const ry = dx * sr + dy * cr;
      const rz = dz;
      const scale = 175.0;
      const gx = rx * scale, gy = ry * scale, gz = rz * scale;
      const ix = Math.floor(gx), iy = Math.floor(gy), iz = Math.floor(gz);
      const h1 = Hash3(ix, iy, iz);
      const h2 = Hash3(ix + 19, iy - 7, iz + 31);
      const h3 = Hash3(ix - 13, iy + 29, iz - 5);
      const magThresh = Clamp((stars.limMag ?? 5.5) / 8.5, 0.05, 0.98);
      if (h1 > 1.0 - 0.18 * magThresh) {
        const cx = ix + 0.2 + 0.6 * h2;
        const cy = iy + 0.2 + 0.6 * h3;
        const cz = iz + 0.2 + 0.6 * ((h1 * 7.13) % 1.0);
        const d2 = (gx - cx) * (gx - cx) + (gy - cy) * (gy - cy) + (gz - cz) * (gz - cz);
        const rad = 0.08 * Clamp(stars.pointSize ?? 1.0, 0.4, 3.0);
        if (d2 < rad * rad) {
          const falloff = Math.exp(-d2 / (rad * rad * 0.35));
          const tw = stars.twinkle
            ? 1.0 - ((stars.twinkleDepth ?? 30) * 0.008) * (0.5 + 0.5 * Math.sin(timeSec * (stars.twinkleRate ?? 1.2) * 5.0 + h2 * 40.0))
            : 1.0;
          const intensity = (0.45 + 1.55 * h3 * h3) * (stars.brightness ?? 1.0) * dayFade * tw * falloff * Clamp(dz * 4.0, 0.0, 1.0);
          const warm = h2;
          outR += intensity * (0.78 + 0.42 * warm);
          outG += intensity * (0.88 + 0.12 * warm);
          outB += intensity * (1.20 - 0.35 * warm);
        }
      }
    }
  }
  return [outR, outG, outB];
}

const _f32View = new Float32Array(1);
const _u32View = new Uint32Array(_f32View.buffer);

function FloatToHalf(val) {
  _f32View[0] = val;
  const x = _u32View[0];
  const sign = (x >>> 16) & 0x8000;
  let exp = ((x >>> 23) & 0xff) - 127 + 15;
  let mant = x & 0x7fffff;
  if (exp <= 0) {
    if (exp < -10) return sign;
    mant = (mant | 0x800000) >>> (1 - exp);
    return sign | ((mant + 0x1000) >>> 13);
  }
  if (exp >= 31) return sign | 0x7c00;
  return sign | (exp << 10) | ((mant + 0x1000) >>> 13);
}

export function PackSkyViewRgba16f(floatData, outU16 = null) {
  const len = floatData.length;
  const u16 = outU16 && outU16.length === len ? outU16 : new Uint16Array(len);
  for (let i = 0; i < len; ++i) {
    u16[i] = FloatToHalf(floatData[i]);
  }
  return u16;
}


