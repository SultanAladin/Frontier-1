/**
 * High-Resolution CPU Studio Renderer (2x2 SSAA + 2048^2 PCF Sun Shadow Map + Flat Stone PBR)
 * Renders generated 3D rock & cliff meshes directly to PNG files for visual QA inspection.
 */

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { generateRockFormation } from '../src/geology/generator.js';
import { FORMATION_PRESETS, LITHOLOGY_PALETTES } from '../src/geology/presets.js';

function hexToLinearRgb(hex) {
  const clean = hex.replace('#', '');
  const r = parseInt(clean.slice(0, 2), 16) / 255;
  const g = parseInt(clean.slice(2, 4), 16) / 255;
  const b = parseInt(clean.slice(4, 6), 16) / 255;
  return [Math.pow(r, 2.2), Math.pow(g, 2.2), Math.pow(b, 2.2)];
}

// --- Minimal Zero-Dependency PNG Encoder ---
const CRC_TABLE = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
  CRC_TABLE[n] = c >>> 0;
}

function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);
  const typeBuf = Buffer.from(type, 'ascii');
  const body = Buffer.concat([typeBuf, data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body), 0);
  return Buffer.concat([len, body, crc]);
}

export function encodeRgbPng(width, height, rgbUint8) {
  const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // 8-bit
  ihdr[9] = 2; // RGB
  ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

  const stride = width * 3 + 1;
  const raw = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    raw[y * stride] = 0; // Filter: None
    const srcOff = y * width * 3;
    raw.set(rgbUint8.subarray(srcOff, srcOff + width * 3), y * stride + 1);
  }
  const idat = zlib.deflateSync(raw, { level: 6 });
  return Buffer.concat([sig, pngChunk('IHDR', ihdr), pngChunk('IDAT', idat), pngChunk('IEND', Buffer.alloc(0))]);
}

// --- Software 3D Rasterizer + Shadow Mapper ---
function normalize3(v) {
  const l = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / l, v[1] / l, v[2] / l];
}

function cross3(a, b) {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}

function dot3(a, b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

export function renderFormationPng(mesh, dims, options = {}) {
  const outW = options.width ?? 900;
  const outH = options.height ?? 675;
  const ssaa = options.ssaa ?? 2;
  const W = outW * ssaa;
  const H = outH * ssaa;

  const [spanX, spanY, spanZ] = dims;
  const target = [0, spanY * 0.42, 0];
  const radius = Math.hypot(spanX * 0.52, spanY * 0.52, spanZ * 0.52);

  const yaw = ((options.yawDeg ?? 28) * Math.PI) / 180;
  const pitch = ((options.pitchDeg ?? 20) * Math.PI) / 180;
  const camDist = radius * (options.distMul ?? 2.35);

  const eye = [
    target[0] + Math.sin(yaw) * Math.cos(pitch) * camDist,
    target[1] + Math.sin(pitch) * camDist,
    target[2] + Math.cos(yaw) * Math.cos(pitch) * camDist,
  ];

  const fwd = normalize3([target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]]);
  const right = normalize3(cross3(fwd, [0, 1, 0]));
  const up = cross3(right, fwd);
  const fovScale = 1 / Math.tan((38 * Math.PI) / 360);
  const aspect = W / H;

  // Sun direction (dramatic oblique 3/4 key light to reveal planar rock facets & overhangs!)
  const sunDir = normalize3(options.sunDir ?? [0.62, 0.66, 0.42]);
  const sunRight = normalize3(cross3([0, 1, 0], sunDir));
  const sunUp = cross3(sunDir, sunRight);

  // Build 1536x1536 orthographic shadow map
  const smSize = 1536;
  const shadowZ = new Float32Array(smSize * smSize).fill(1e9);
  const smBound = radius * 1.35;

  function toShadowCoord(wx, wy, wz) {
    const rx = wx - target[0], ry = wy - target[1], rz = wz - target[2];
    const u = ((rx * sunRight[0] + ry * sunRight[1] + rz * sunRight[2]) / smBound * 0.5 + 0.5) * smSize;
    const v = (-(rx * sunUp[0] + ry * sunUp[1] + rz * sunUp[2]) / smBound * 0.5 + 0.5) * smSize;
    const d = -(rx * sunDir[0] + ry * sunDir[1] + rz * sunDir[2]);
    return [u, v, d];
  }

  const { positions, normals, colors, indices } = mesh;
  const numTris = indices.length / 3;

  // Pass 1: Shadow Map Rasterization
  for (let t = 0; t < numTris; t++) {
    const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
    const s0 = toShadowCoord(positions[i0 * 3], positions[i0 * 3 + 1], positions[i0 * 3 + 2]);
    const s1 = toShadowCoord(positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2]);
    const s2 = toShadowCoord(positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2]);

    const minX = Math.max(0, Math.floor(Math.min(s0[0], s1[0], s2[0])));
    const maxX = Math.min(smSize - 1, Math.ceil(Math.max(s0[0], s1[0], s2[0])));
    const minY = Math.max(0, Math.floor(Math.min(s0[1], s1[1], s2[1])));
    const maxY = Math.min(smSize - 1, Math.ceil(Math.max(s0[1], s1[1], s2[1])));

    const denom = (s1[1] - s2[1]) * (s0[0] - s2[0]) + (s2[0] - s1[0]) * (s0[1] - s2[1]);
    if (Math.abs(denom) < 1e-6) continue;
    const invD = 1 / denom;

    for (let py = minY; py <= maxY; py++) {
      const cy = py + 0.5;
      for (let px = minX; px <= maxX; px++) {
        const cx = px + 0.5;
        const w0 = ((s1[1] - s2[1]) * (cx - s2[0]) + (s2[0] - s1[0]) * (cy - s2[1])) * invD;
        const w1 = ((s2[1] - s0[1]) * (cx - s2[0]) + (s0[0] - s2[0]) * (cy - s2[1])) * invD;
        const w2 = 1 - w0 - w1;
        if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
          const z = w0 * s0[2] + w1 * s1[2] + w2 * s2[2];
          const idx = py * smSize + px;
          if (z < shadowZ[idx]) shadowZ[idx] = z;
        }
      }
    }
  }

  function sampleShadowPCF(wx, wy, wz, bias) {
    const [u, v, d] = toShadowCoord(wx, wy, wz);
    const ix = Math.floor(u), iy = Math.floor(v);
    let lit = 0, total = 0;
    for (let dy = -1; dy <= 1; dy++) {
      const sy = iy + dy;
      if (sy < 0 || sy >= smSize) continue;
      for (let dx = -1; dx <= 1; dx++) {
        const sx = ix + dx;
        if (sx < 0 || sx >= smSize) continue;
        total++;
        if (d - bias <= shadowZ[sy * smSize + sx]) lit++;
      }
    }
    return total > 0 ? lit / total : 1.0;
  }

  // Pass 2: Main Camera Z-Buffer + Studio Sky Gradient + Shadow-Catching Ground Plane
  const zBuf = new Float32Array(W * H).fill(1e9);
  const colorBuf = new Float32Array(W * H * 3);

  for (let py = 0; py < H; py++) {
    const ndcY = 1 - ((py + 0.5) / H) * 2;
    const skyT = Math.max(0, Math.min(1, (py / H)));
    const bgR = 0.14 + 0.09 * skyT;
    const bgG = 0.155 + 0.095 * skyT;
    const bgB = 0.175 + 0.105 * skyT;

    for (let px = 0; px < W; px++) {
      const ndcX = (((px + 0.5) / W) * 2 - 1) * aspect;
      const rdx = fwd[0] * fovScale + right[0] * ndcX + up[0] * ndcY;
      const rdy = fwd[1] * fovScale + right[1] * ndcX + up[1] * ndcY;
      const rdz = fwd[2] * fovScale + right[2] * ndcX + up[2] * ndcY;
      const rl = Math.hypot(rdx, rdy, rdz);
      const dirY = rdy / rl;

      let r = bgR, g = bgG, b = bgB;

      // Ground plane intersection at Y = 0
      if (dirY < -0.005 && eye[1] > 0) {
        const tGround = -eye[1] / (rdy / rl);
        const gx = eye[0] + (rdx / rl) * tGround;
        const gz = eye[2] + (rdz / rl) * tGround;
        const distFromOrigin = Math.hypot(gx, gz);
        if (distFromOrigin < radius * 2.8) {
          const fade = Math.max(0, 1 - (distFromOrigin / (radius * 2.8)) ** 2);
          const sh = sampleShadowPCF(gx, 0.002, gz, radius * 0.012);
          // Subtle 1m / 5m studio grid line
          const g1 = Math.min(Math.abs(gx - Math.round(gx)), Math.abs(gz - Math.round(gz)));
          const gridBoost = g1 < 0.03 ? 0.035 * fade : 0;
          const gLum = (0.13 + 0.12 * sh + gridBoost);
          r = r * (1 - fade * 0.85) + gLum * 0.96 * fade * 0.85;
          g = g * (1 - fade * 0.85) + gLum * 0.97 * fade * 0.85;
          b = b * (1 - fade * 0.85) + gLum * 1.00 * fade * 0.85;
          zBuf[py * W + px] = tGround * (fwd[0] * (rdx / rl) + fwd[1] * dirY + fwd[2] * (rdz / rl));
        }
      }

      const ci = (py * W + px) * 3;
      colorBuf[ci] = r;
      colorBuf[ci + 1] = g;
      colorBuf[ci + 2] = b;
    }
  }

  function projectVertex(vi) {
    const wx = positions[vi * 3], wy = positions[vi * 3 + 1], wz = positions[vi * 3 + 2];
    const rx = wx - eye[0], ry = wy - eye[1], rz = wz - eye[2];
    const cz = rx * fwd[0] + ry * fwd[1] + rz * fwd[2];
    if (cz <= 0.05) return null;
    const cx = rx * right[0] + ry * right[1] + rz * right[2];
    const cy = rx * up[0] + ry * up[1] + rz * up[2];
    const sx = ((cx * fovScale) / (cz * aspect) * 0.5 + 0.5) * W;
    const sy = (-(cy * fovScale) / cz * 0.5 + 0.5) * H;
    return [sx, sy, cz, wx, wy, wz];
  }

  const pureClay = !!options.pureClay;
  const baseAlbedo = options.baseColor ?? [0.39, 0.38, 0.365]; // Flat untextured mid-grey studio stone
  const shadowBias = radius * 0.011;

  for (let t = 0; t < numTris; t++) {
    const i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
    const p0 = projectVertex(i0);
    const p1 = projectVertex(i1);
    const p2 = projectVertex(i2);
    if (!p0 || !p1 || !p2) continue;

    // Backface cull
    const area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0]);
    if (area >= 0) continue; // Note: screen Y is down, so CCW 3D projects to negative screen area!

    const minX = Math.max(0, Math.floor(Math.min(p0[0], p1[0], p2[0])));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(p0[0], p1[0], p2[0])));
    const minY = Math.max(0, Math.floor(Math.min(p0[1], p1[1], p2[1])));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(p0[1], p1[1], p2[1])));

    const denom = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1]);
    if (Math.abs(denom) < 1e-6) continue;
    const invD = 1 / denom;
    const invZ0 = 1 / p0[2], invZ1 = 1 / p1[2], invZ2 = 1 / p2[2];

    for (let py = minY; py <= maxY; py++) {
      const cy = py + 0.5;
      for (let px = minX; px <= maxX; px++) {
        const cx = px + 0.5;
        const l0 = ((p1[1] - p2[1]) * (cx - p2[0]) + (p2[0] - p1[0]) * (cy - p2[1])) * invD;
        const l1 = ((p2[1] - p0[1]) * (cx - p2[0]) + (p0[0] - p2[0]) * (cy - p2[1])) * invD;
        const l2 = 1 - l0 - l1;
        if (l0 < 0 || l1 < 0 || l2 < 0) continue;

        const invZ = l0 * invZ0 + l1 * invZ1 + l2 * invZ2;
        const z = 1 / invZ;
        const pIdx = py * W + px;
        if (z >= zBuf[pIdx]) continue;
        zBuf[pIdx] = z;

        // Perspective-correct interpolation
        const w0 = l0 * invZ0 * z;
        const w1 = l1 * invZ1 * z;
        const w2 = l2 * invZ2 * z;

        const wx = w0 * p0[3] + w1 * p1[3] + w2 * p2[3];
        const wy = w0 * p0[4] + w1 * p1[4] + w2 * p2[4];
        const wz = w0 * p0[5] + w1 * p1[5] + w2 * p2[5];

        let nx = w0 * normals[i0 * 3] + w1 * normals[i1 * 3] + w2 * normals[i2 * 3];
        let ny = w0 * normals[i0 * 3 + 1] + w1 * normals[i1 * 3 + 1] + w2 * normals[i2 * 3 + 1];
        let nz = w0 * normals[i0 * 3 + 2] + w1 * normals[i1 * 3 + 2] + w2 * normals[i2 * 3 + 2];
        const nl = Math.hypot(nx, ny, nz) || 1;
        nx /= nl; ny /= nl; nz /= nl;

        const ao = w0 * colors[i0 * 4] + w1 * colors[i1 * 4] + w2 * colors[i2 * 4];
        const cav = w0 * colors[i0 * 4 + 1] + w1 * colors[i1 * 4 + 1] + w2 * colors[i2 * 4 + 1];
        const edg = w0 * colors[i0 * 4 + 2] + w1 * colors[i1 * 4 + 2] + w2 * colors[i2 * 4 + 2];

        // Flat or multilayer stratigraphic stone surface response
        const edgeLift = pureClay ? 0 : edg * 0.055;
        const cavDark = pureClay ? 1.0 : (1.0 - cav * 0.16);
        let bR = baseAlbedo[0], bG = baseAlbedo[1], bB = baseAlbedo[2];
        if (!pureClay && mesh.hasCustomAlbedo && mesh.albedos) {
          const albs = mesh.albedos;
          bR = w0 * albs[i0 * 3] + w1 * albs[i1 * 3] + w2 * albs[i2 * 3];
          bG = w0 * albs[i0 * 3 + 1] + w1 * albs[i1 * 3 + 1] + w2 * albs[i2 * 3 + 1];
          bB = w0 * albs[i0 * 3 + 2] + w1 * albs[i1 * 3 + 2] + w2 * albs[i2 * 3 + 2];
        }
        if (!pureClay && mesh.stains) {
          const stn = mesh.stains;
          const fe = w0 * stn[i0 * 2] + w1 * stn[i1 * 2] + w2 * stn[i2 * 2];
          const cu = w0 * stn[i0 * 2 + 1] + w1 * stn[i1 * 2 + 1] + w2 * stn[i2 * 2 + 1];
          const sstep = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
          if (fe > 0.004) {
            const k = sstep(0.35, 0.9, fe);
            const m = sstep(0.05, 0.7, fe) * 0.88;
            bR += ((0.46 + (0.15 - 0.46) * k) - bR) * m;
            bG += ((0.21 + (0.06 - 0.21) * k) - bG) * m;
            bB += ((0.06 + (0.03 - 0.06) * k) - bB) * m;
          }
          if (cu > 0.004) {
            const k = sstep(0.4, 0.95, cu);
            const m = sstep(0.05, 0.7, cu) * 0.85;
            bR += ((0.14 + (0.08 - 0.14) * k) - bR) * m;
            bG += ((0.48 + (0.26 - 0.48) * k) - bG) * m;
            bB += ((0.38 + (0.52 - 0.38) * k) - bB) * m;
          }
        }
        const albR = (bR + edgeLift) * cavDark;
        const albG = (bG + edgeLift) * cavDark;
        const albB = (bB + edgeLift * 0.95) * cavDark;

        const ndotl = Math.max(0, nx * sunDir[0] + ny * sunDir[1] + nz * sunDir[2]);
        const shadow = ndotl > 0.001 ? sampleShadowPCF(wx, wy, wz, shadowBias) : 0;

        // Sky dome + warm ground bounce fill + subtle cool rim light
        const skyHemi = 0.5 + 0.5 * ny;
        const rimDot = Math.max(0, -(nx * sunDir[0] + nz * sunDir[2])) * 0.35;
        const vx = eye[0] - wx, vy = eye[1] - wy, vz = eye[2] - wz;
        const vl = Math.hypot(vx, vy, vz) || 1;
        const hx = sunDir[0] + vx / vl, hy = sunDir[1] + vy / vl, hz = sunDir[2] + vz / vl;
        const hl = Math.hypot(hx, hy, hz) || 1;
        const ndoth = Math.max(0, (nx * hx + ny * hy + nz * hz) / hl);
        const spec = Math.pow(ndoth, 18) * 0.07 * shadow * ndotl;

        const sunR = 2.05 * ndotl * shadow;
        const sunG = 1.98 * ndotl * shadow;
        const sunB = 1.86 * ndotl * shadow;

        const ambR = (0.24 * (1 - skyHemi) + 0.42 * skyHemi + 0.14 * rimDot) * ao;
        const ambG = (0.22 * (1 - skyHemi) + 0.46 * skyHemi + 0.16 * rimDot) * ao;
        const ambB = (0.20 * (1 - skyHemi) + 0.54 * skyHemi + 0.20 * rimDot) * ao;

        let cR = albR * (sunR + ambR) + spec;
        let cG = albG * (sunG + ambG) + spec;
        let cB = albB * (sunB + ambB) + spec;

        // ACES-like filmic tonemap + sRGB gamma
        cR = (cR * (2.51 * cR + 0.03)) / (cR * (2.43 * cR + 0.59) + 0.14);
        cG = (cG * (2.51 * cG + 0.03)) / (cG * (2.43 * cG + 0.59) + 0.14);
        cB = (cB * (2.51 * cB + 0.03)) / (cB * (2.43 * cB + 0.59) + 0.14);

        const ci = pIdx * 3;
        colorBuf[ci] = Math.pow(Math.max(0, Math.min(1, cR)), 1 / 2.2);
        colorBuf[ci + 1] = Math.pow(Math.max(0, Math.min(1, cG)), 1 / 2.2);
        colorBuf[ci + 2] = Math.pow(Math.max(0, Math.min(1, cB)), 1 / 2.2);
      }
    }
  }

  // Downsample SSAA -> RGB8
  const outRgb = new Uint8Array(outW * outH * 3);
  const invSamples = 1 / (ssaa * ssaa);
  for (let y = 0; y < outH; y++) {
    for (let x = 0; x < outW; x++) {
      let r = 0, g = 0, b = 0;
      for (let sy = 0; sy < ssaa; sy++) {
        for (let sx = 0; sx < ssaa; sx++) {
          const idx = ((y * ssaa + sy) * W + (x * ssaa + sx)) * 3;
          r += colorBuf[idx];
          g += colorBuf[idx + 1];
          b += colorBuf[idx + 2];
        }
      }
      const oi = (y * outW + x) * 3;
      outRgb[oi] = Math.round(r * invSamples * 255);
      outRgb[oi + 1] = Math.round(g * invSamples * 255);
      outRgb[oi + 2] = Math.round(b * invSamples * 255);
    }
  }

  return encodeRgbPng(outW, outH, outRgb);
}

// CLI execution when invoked directly
if (import.meta.url === `file://${process.argv[1]}`) {
  const outDir = path.resolve('qa-renders');
  fs.mkdirSync(outDir, { recursive: true });

  const filterArg = process.argv.slice(2).find((a) => !a.startsWith('--')) || null;
  const keys = filterArg
    ? filterArg.split(',').map((s) => s.trim()).filter(Boolean)
    : Object.keys(FORMATION_PRESETS);

  for (const key of keys) {
    const res = generateRockFormation({
      archetype: key,
      seed: 4029,
      quality: 'high',
    });
    const pal = LITHOLOGY_PALETTES[res.lithology] || LITHOLOGY_PALETTES.neutralStone;
    let yawDeg = 28;
    let pitchDeg = 18;
    let distMul = 2.20;
    if (key === 'desert-arch' || key === 'sea-arch') { yawDeg = 14; pitchDeg = 11; distMul = 2.15; }
    else if (key === 'double-arch') { yawDeg = 22; pitchDeg = 12; distMul = 2.18; }
    else if (key === 'sedimentary-cliff' || key === 'cliff-wall' || key === 'utah-canyon' || key === 'exfoliation-dome') { yawDeg = 22; pitchDeg = 17; distMul = 2.08; }
    else if (key === 'slot-canyon') { yawDeg = 5; pitchDeg = 16; distMul = 2.05; }
    else if (key === 'balanced-rock') { yawDeg = 26; pitchDeg = 14; distMul = 2.15; }

    const png = renderFormationPng(res.mesh, res.stats.dimensions, {
      width: 960,
      height: 700,
      baseColor: hexToLinearRgb(pal.color),
      yawDeg,
      pitchDeg,
      distMul,
    });
    const outPath = path.join(outDir, `${key}.png`);
    fs.writeFileSync(outPath, png);
    console.log(`[QA] ${key.padEnd(22)} | ${String(res.stats.triangles).padStart(7)} tris | ${res.stats.blockCount} blocks | ${res.stats.removedIslands} islands | ${res.stats.elapsedMs}ms`);
  }
}
