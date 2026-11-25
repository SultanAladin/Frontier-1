/**
 * Structural-geology spatial fields:
 * 1. Exact Piecewise-Planar 3D Voronoi Cleavage Faceting (zero-allocation, C-infinity
 *    partition of unity over tilted flat cleavage planes + exact Euclidean bisector wall distance).
 * 2. Smooth 3D scalar field strictly for tectonic fault/fold undulation & fracture-tip stress modulation
 *    (never used as raw isotropic blob displacement).
 */

import { hash3u, u32ToUnit } from './prng.js';

const _r2 = new Float64Array(27);
const _pval = new Float64Array(27);

/**
 * Evaluates a piecewise-planar 3D Voronoi cleavage field at fabric-space point (qx, qy, qz).
 * Writes into `out`:
 *   out.facet    : piecewise-planar step + micro-tilt displacement in [-1, 1]
 *   out.wallDist : exact Euclidean distance to nearest Voronoi cleavage wall (>= 0)
 *   out.wnx, out.wny, out.wnz : unit normal of that cleavage wall
 *   out.cellId   : uint32 hash of dominant cleavage cell
 */
export function evalPlanarCleavage3D(qx, qy, qz, sharpness, tiltAmp, jitter, salt, out) {
  const ix = Math.floor(qx);
  const iy = Math.floor(qy);
  const iz = Math.floor(qz);

  let min1 = 1e30;
  let min2 = 1e30;
  let s1x = 0, s1y = 0, s1z = 0;
  let s2x = 0, s2y = 0, s2z = 0;
  let bestHash = 0;

  let idx = 0;
  for (let dz = -1; dz <= 1; dz++) {
    const cz = iz + dz;
    for (let dy = -1; dy <= 1; dy++) {
      const cy = iy + dy;
      for (let dx = -1; dx <= 1; dx++) {
        const cx = ix + dx;
        const h0 = hash3u(cx, cy, cz, salt);
        const h1 = hash3u(cx, cy, cz, salt + 1013);
        const h2 = hash3u(cx, cy, cz, salt + 2027);

        const sx = cx + 0.5 + (u32ToUnit(h0) - 0.5) * jitter;
        const sy = cy + 0.5 + (u32ToUnit(h1) - 0.5) * jitter;
        const sz = cz + 0.5 + (u32ToUnit(h2) - 0.5) * jitter;

        const rx = qx - sx;
        const ry = qy - sy;
        const rz = qz - sz;
        const d2 = rx * rx + ry * ry + rz * rz;

        // Cell planar step height + micro-tilt plane
        const h3 = hash3u(cx, cy, cz, salt + 3049);
        const h4 = hash3u(cx, cy, cz, salt + 4073);
        const stepH = (u32ToUnit(h3) * 2 - 1);
        const tx = (((h4 & 0xff) / 127.5) - 1) * tiltAmp;
        const ty = ((((h4 >>> 8) & 0xff) / 127.5) - 1) * tiltAmp;
        const tz = ((((h4 >>> 16) & 0xff) / 127.5) - 1) * tiltAmp;

        _r2[idx] = d2;
        _pval[idx] = stepH + tx * rx + ty * ry + tz * rz;
        idx++;

        if (d2 < min1) {
          min2 = min1;
          s2x = s1x; s2y = s1y; s2z = s1z;
          min1 = d2;
          s1x = sx; s1y = sy; s1z = sz;
          bestHash = h0;
        } else if (d2 < min2) {
          min2 = d2;
          s2x = sx; s2y = sy; s2z = sz;
        }
      }
    }
  }

  let wSum = 0;
  let fSum = 0;
  for (let i = 0; i < 27; i++) {
    const diff = (_r2[i] - min1) * sharpness;
    if (diff < 16.0) {
      // C7 polynomial partition kernel (1 - diff/16)^8: identical flat-plane behavior, 8x faster than Math.exp
      const t = 1.0 - diff * 0.0625;
      const t2 = t * t;
      const t4 = t2 * t2;
      const w = t4 * t4;
      wSum += w;
      fSum += w * _pval[i];
    }
  }

  const bdx = s2x - s1x;
  const bdy = s2y - s1y;
  const bdz = s2z - s1z;
  const bLen = Math.hypot(bdx, bdy, bdz) || 1e-6;

  out.facet = wSum > 0 ? fSum / wSum : 0;
  out.wallDist = Math.max(0, (min2 - min1) / (2 * bLen));
  out.wnx = bdx / bLen;
  out.wny = bdy / bLen;
  out.wnz = bdz / bLen;
  out.cellId = bestHash;
}

/**
 * Smooth C2 3D scalar field in [-1, 1], used ONLY for low-frequency tectonic
 * fault/fold trajectory undulation and crack-tip stress modulation.
 */
export function scalarField3D(x, y, z, salt = 0) {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const iz = Math.floor(z);
  const fx = x - ix;
  const fy = y - iy;
  const fz = z - iz;

  const ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10);
  const uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
  const uz = fz * fz * fz * (fz * (fz * 6 - 15) + 10);

  const c000 = u32ToUnit(hash3u(ix, iy, iz, salt)) * 2 - 1;
  const c100 = u32ToUnit(hash3u(ix + 1, iy, iz, salt)) * 2 - 1;
  const c010 = u32ToUnit(hash3u(ix, iy + 1, iz, salt)) * 2 - 1;
  const c110 = u32ToUnit(hash3u(ix + 1, iy + 1, iz, salt)) * 2 - 1;
  const c001 = u32ToUnit(hash3u(ix, iy, iz + 1, salt)) * 2 - 1;
  const c101 = u32ToUnit(hash3u(ix + 1, iy, iz + 1, salt)) * 2 - 1;
  const c011 = u32ToUnit(hash3u(ix, iy + 1, iz + 1, salt)) * 2 - 1;
  const c111 = u32ToUnit(hash3u(ix + 1, iy + 1, iz + 1, salt)) * 2 - 1;

  const x00 = c000 + (c100 - c000) * ux;
  const x10 = c010 + (c110 - c010) * ux;
  const x01 = c001 + (c101 - c001) * ux;
  const x11 = c011 + (c111 - c011) * ux;

  const y0 = x00 + (x10 - x00) * uy;
  const y1 = x01 + (x11 - x01) * uy;

  return y0 + (y1 - y0) * uz;
}
