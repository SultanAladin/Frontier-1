/*==========================================================================================================================================
                                                       GEOMETRYICONBAKER.JS
==========================================================================================================================================*/
/* Deterministic transparent-background 128x128 3D specimen thumbnail baker for TextureStudio AssetBrowser.
   Mirrors FrontierWorldMirror/ModelThumbnailRenderer.js (3-point OpenPBR studio lighting, tight silhouette framing, alpha === 0 outside). */

import { createBakeStudyPair } from "./Engine/BakingEngine.js";

const ICON_DATA_URL_MAP = new Map();

function clamp01(v) {
  return Math.max(0, Math.min(1, v));
}

function normalize3(x, y, z) {
  const l = Math.hypot(x, y, z) || 1;
  return [x / l, y / l, z / l];
}

function rotateCamera(x, y, z, yaw, pitch) {
  const cy = Math.cos(yaw), sy = Math.sin(yaw);
  const cp = Math.cos(pitch), sp = Math.sin(pitch);
  const rx = x * cy + z * sy;
  const rz0 = -x * sy + z * cy;
  const ry = y * cp - rz0 * sp;
  const rz = y * sp + rz0 * cp;
  return [rx, ry, rz];
}

/**
 * Software scanline 3D studio rasterizer producing raw RGBA pixels (size x size) with pure transparent background.
 */
export function renderSpecimenIconPixels(specimenId, size = 128) {
  const pair = createBakeStudyPair(specimenId || "teapot");
  const parts = pair.high?.length ? pair.high : pair.low;
  const rgba = new Uint8ClampedArray(size * size * 4);
  if (!parts || !parts.length) return rgba;

  // Gather world bounds across parts
  let minX = Infinity, minY = Infinity, minZ = Infinity;
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
  const tris = [];
  for (const part of parts) {
    const g = part.geometry;
    const pos = g?.attributes?.position;
    if (!pos) continue;
    const idx = g.index;
    const count = idx ? idx.count : pos.count;
    for (let i = 0; i < count; i += 3) {
      const i0 = idx ? idx.getX(i) : i;
      const i1 = idx ? idx.getX(i + 1) : i + 1;
      const i2 = idx ? idx.getX(i + 2) : i + 2;
      const x0 = pos.getX(i0), y0 = pos.getY(i0), z0 = pos.getZ(i0);
      const x1 = pos.getX(i1), y1 = pos.getY(i1), z1 = pos.getZ(i1);
      const x2 = pos.getX(i2), y2 = pos.getY(i2), z2 = pos.getZ(i2);
      minX = Math.min(minX, x0, x1, x2); maxX = Math.max(maxX, x0, x1, x2);
      minY = Math.min(minY, y0, y1, y2); maxY = Math.max(maxY, y0, y1, y2);
      minZ = Math.min(minZ, z0, z1, z2); maxZ = Math.max(maxZ, z0, z1, z2);
      tris.push([x0, y0, z0, x1, y1, z1, x2, y2, z2]);
    }
  }
  if (!tris.length || !Number.isFinite(minX)) return rgba;

  const cx = (minX + maxX) * 0.5;
  const cy = (minY + maxY) * 0.5;
  const cz = (minZ + maxZ) * 0.5;
  const extent = Math.max(1e-4, Math.hypot(maxX - minX, maxY - minY, maxZ - minZ));
  const invScale = 1.68 / extent;

  const yaw = 0.62;
  const pitch = 0.34;
  const camDist = 3.1;
  const focal = size * 1.48;
  const half = size * 0.5;

  const zBuf = new Float32Array(size * size);
  zBuf.fill(Infinity);

  const keyL = normalize3(0.58, 0.72, 0.45);
  const fillL = normalize3(-0.68, 0.28, 0.52);
  const rimL = normalize3(-0.24, 0.54, -0.78);
  const halfKey = normalize3(keyL[0], keyL[1], keyL[2] + 1);

  for (let t = 0; t < tris.length; t++) {
    const tr = tris[t];
    const p0 = rotateCamera((tr[0] - cx) * invScale, (tr[1] - cy) * invScale, (tr[2] - cz) * invScale, yaw, pitch);
    const p1 = rotateCamera((tr[3] - cx) * invScale, (tr[4] - cy) * invScale, (tr[5] - cz) * invScale, yaw, pitch);
    const p2 = rotateCamera((tr[6] - cx) * invScale, (tr[7] - cy) * invScale, (tr[8] - cz) * invScale, yaw, pitch);

    const e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
    const e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];
    const nx0 = e1y * e2z - e1z * e2y;
    const ny0 = e1z * e2x - e1x * e2z;
    const nz0 = e1x * e2y - e1y * e2x;
    const nl = Math.hypot(nx0, ny0, nz0);
    if (nl < 1e-8) continue;
    const nx = nx0 / nl, ny = ny0 / nl, nz = nz0 / nl;

    const z0 = camDist - p0[2], z1 = camDist - p1[2], z2 = camDist - p2[2];
    if (z0 <= 0.2 || z1 <= 0.2 || z2 <= 0.2) continue;

    const sx0 = half + (p0[0] * focal) / z0, sy0 = half - (p0[1] * focal) / z0;
    const sx1 = half + (p1[0] * focal) / z1, sy1 = half - (p1[1] * focal) / z1;
    const sx2 = half + (p2[0] * focal) / z2, sy2 = half - (p2[1] * focal) / z2;

    const area = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
    if (Math.abs(area) < 0.05) continue;

    const xMin = Math.max(0, Math.floor(Math.min(sx0, sx1, sx2)));
    const xMax = Math.min(size - 1, Math.ceil(Math.max(sx0, sx1, sx2)));
    const yMin = Math.max(0, Math.floor(Math.min(sy0, sy1, sy2)));
    const yMax = Math.min(size - 1, Math.ceil(Math.max(sy0, sy1, sy2)));

    // 3-point studio metallic-clay shading
    const flip = nz < 0 ? -1 : 1;
    const fnx = nx * flip, fny = ny * flip, fnz = nz * flip;
    const ndKey = Math.max(0, fnx * keyL[0] + fny * keyL[1] + fnz * keyL[2]);
    const ndFill = Math.max(0, fnx * fillL[0] + fny * fillL[1] + fnz * fillL[2]);
    const ndRim = Math.max(0, fnx * rimL[0] + fny * rimL[1] + fnz * rimL[2]);
    const ndH = Math.max(0, fnx * halfKey[0] + fny * halfKey[1] + fnz * halfKey[2]);
    const spec = Math.pow(ndH, 28) * 0.52;
    const fresnel = Math.pow(1 - Math.abs(fnz), 3.0) * (0.28 + ndRim * 0.45);
    const hemi = 0.14 + 0.12 * (fny * 0.5 + 0.5);

    const rLin = clamp01(hemi * 0.88 + ndKey * 0.72 + ndFill * 0.22 + spec * 1.05 + fresnel * 0.75);
    const gLin = clamp01(hemi * 0.91 + ndKey * 0.71 + ndFill * 0.26 + spec * 1.02 + fresnel * 0.86);
    const bLin = clamp01(hemi * 0.98 + ndKey * 0.67 + ndFill * 0.34 + spec * 0.96 + fresnel * 1.0);
    const r = Math.round(Math.pow(rLin, 1 / 2.2) * 255);
    const g = Math.round(Math.pow(gLin, 1 / 2.2) * 255);
    const b = Math.round(Math.pow(bLin, 1 / 2.2) * 255);

    const invArea = 1 / area;
    for (let py = yMin; py <= yMax; py++) {
      for (let px = xMin; px <= xMax; px++) {
        const cxp = px + 0.5, cyp = py + 0.5;
        const w0 = ((sx1 - cxp) * (sy2 - cyp) - (sy1 - cyp) * (sx2 - cxp)) * invArea;
        const w1 = ((sx2 - cxp) * (sy0 - cyp) - (sy2 - cyp) * (sx0 - cxp)) * invArea;
        const w2 = 1 - w0 - w1;
        if (w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) continue;
        const depth = w0 * z0 + w1 * z1 + w2 * z2;
        const pi = py * size + px;
        if (depth < zBuf[pi]) {
          zBuf[pi] = depth;
          const o = pi * 4;
          rgba[o] = r;
          rgba[o + 1] = g;
          rgba[o + 2] = b;
          rgba[o + 3] = 255;
        }
      }
    }
  }

  return rgba;
}

export function getBakedGeometryIcon(specimenId, size = 128) {
  const key = `${specimenId || "teapot"}:${size}`;
  if (ICON_DATA_URL_MAP.has(key)) return ICON_DATA_URL_MAP.get(key);
  if (typeof document === "undefined") return null;
  try {
    const canvas = document.createElement("canvas");
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext("2d");
    if (!ctx || typeof ctx.createImageData !== "function") return null;
    const pixels = renderSpecimenIconPixels(specimenId, size);
    const img = ctx.createImageData(size, size);
    img.data.set(pixels);
    ctx.putImageData(img, 0, 0);
    const url = canvas.toDataURL("image/png");
    ICON_DATA_URL_MAP.set(key, url);
    return url;
  } catch {
    return null;
  }
}
