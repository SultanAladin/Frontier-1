/**
 * High-Speed, Alias-Free Feature-Preserving Dual Contouring
 * using Cached Grid Central-Difference Normals, Centered 3x3 QEF Solver,
 * and Crease-Aligned Quad Triangulation.
 */

import { BRICK_SIZE } from './sparse-sdf.js';

class GrowableFloat64 {
  constructor(initialCap = 16384) {
    this.buf = new Float64Array(initialCap);
    this.length = 0;
  }
  ensure(needed) {
    if (needed <= this.buf.length) return;
    let cap = this.buf.length;
    while (cap < needed) cap *= 2;
    const next = new Float64Array(cap);
    next.set(this.buf);
    this.buf = next;
  }
}

class GrowableInt32 {
  constructor(initialCap = 16384) {
    this.buf = new Int32Array(initialCap);
    this.length = 0;
  }
  ensure(needed) {
    if (needed <= this.buf.length) return;
    let cap = this.buf.length;
    while (cap < needed) cap *= 2;
    const next = new Int32Array(cap);
    next.set(this.buf);
    this.buf = next;
  }
  push4(a, b, c, d) {
    const i = this.length;
    this.ensure(i + 4);
    this.buf[i] = a;
    this.buf[i + 1] = b;
    this.buf[i + 2] = c;
    this.buf[i + 3] = d;
    this.length = i + 4;
  }
}

export function extractDualContourMesh(sdf, options = {}, onProgress) {
  const {
    nx, ny, nz,
    originX, originY, originZ,
    voxelSize: h,
    activeBrickCoords,
    nbx, nby, nbz,
    ensureSample,
  } = sdf;

  // Slightly higher regularization prevents QEF overshoot chatter on curved bevels while snapping 90-deg planes
  const qefReg = options.qefRegularization ?? 0.05;

  const brickVisited = new Uint8Array(nbx * nby * nbz);
  const bricksToScan = [];
  for (let i = 0; i < activeBrickCoords.length; i += 4) {
    const bx = activeBrickCoords[i];
    const by = activeBrickCoords[i + 1];
    const bz = activeBrickCoords[i + 2];
    const bKey = activeBrickCoords[i + 3];
    if (!brickVisited[bKey]) {
      brickVisited[bKey] = 1;
      bricksToScan.push(bx, by, bz);
    }
  }

  const cellStrideY = nx;
  const cellStrideZ = nx * ny;
  const cellToVert = new Map();

  const qefData = new GrowableFloat64(32768 * 16);
  const cellCoords = new GrowableInt32(32768 * 3);
  const quads = new GrowableInt32(65536 * 4);
  let numVerts = 0;

  function getOrCreateCellVert(cx, cy, cz) {
    const key = cx + cy * cellStrideY + cz * cellStrideZ;
    let vIdx = cellToVert.get(key);
    if (vIdx === undefined) {
      vIdx = numVerts++;
      cellToVert.set(key, vIdx);
      qefData.ensure(numVerts * 16);
      cellCoords.ensure(numVerts * 3);
      const ci = vIdx * 3;
      cellCoords.buf[ci] = cx;
      cellCoords.buf[ci + 1] = cy;
      cellCoords.buf[ci + 2] = cz;
    }
    return vIdx;
  }

  function addCrossingToCell(vIdx, px, py, pz, nxN, nyN, nzN) {
    const ci = vIdx * 3;
    const ox = originX + cellCoords.buf[ci] * h;
    const oy = originY + cellCoords.buf[ci + 1] * h;
    const oz = originZ + cellCoords.buf[ci + 2] * h;

    const dx = px - ox;
    const dy = py - oy;
    const dz = pz - oz;
    const nd = nxN * dx + nyN * dy + nzN * dz;

    const base = vIdx * 16;
    const buf = qefData.buf;
    buf[base + 0] += nxN * nxN;
    buf[base + 1] += nxN * nyN;
    buf[base + 2] += nxN * nzN;
    buf[base + 3] += nyN * nyN;
    buf[base + 4] += nyN * nzN;
    buf[base + 5] += nzN * nzN;

    buf[base + 6] += nxN * nd;
    buf[base + 7] += nyN * nd;
    buf[base + 8] += nzN * nd;

    buf[base + 9] += dx;
    buf[base + 10] += dy;
    buf[base + 11] += dz;

    buf[base + 12] += nxN;
    buf[base + 13] += nyN;
    buf[base + 14] += nzN;
    buf[base + 15] += 1;
  }

  const _gOut = [0, 0, 0];
  function gridGrad(ix, iy, iz, out) {
    const xm = ix > 0 ? ix - 1 : 0;
    const xp = ix < nx ? ix + 1 : nx;
    const ym = iy > 0 ? iy - 1 : 0;
    const yp = iy < ny ? iy + 1 : ny;
    const zm = iz > 0 ? iz - 1 : 0;
    const zp = iz < nz ? iz + 1 : nz;
    out[0] = ensureSample(xp, iy, iz) - ensureSample(xm, iy, iz);
    out[1] = ensureSample(ix, yp, iz) - ensureSample(ix, ym, iz);
    out[2] = ensureSample(ix, iy, zp) - ensureSample(ix, iy, zm);
  }

  const _gA = [0, 0, 0];
  const _gB = [0, 0, 0];

  function processGridEdge(
    iax, iay, iaz, va,
    ibx, iby, ibz, vb,
    c00x, c00y, c00z,
    c10x, c10y, c10z,
    c11x, c11y, c11z,
    c01x, c01y, c01z
  ) {
    const denom = va - vb;
    let t = Math.abs(denom) > 1e-12 ? va / denom : 0.5;
    t = t < 0.005 ? 0.005 : t > 0.995 ? 0.995 : t;

    const ax = originX + iax * h, ay = originY + iay * h, az = originZ + iaz * h;
    const bx = originX + ibx * h, by = originY + iby * h, bz = originZ + ibz * h;
    const px = ax + (bx - ax) * t;
    const py = ay + (by - ay) * t;
    const pz = az + (bz - az) * t;

    // Fast cached grid central-difference gradient (zero extra SDF evaluations & zero sub-voxel aliasing!)
    gridGrad(iax, iay, iaz, _gA);
    gridGrad(ibx, iby, ibz, _gB);

    let gx = _gA[0] + (_gB[0] - _gA[0]) * t;
    let gy = _gA[1] + (_gB[1] - _gA[1]) * t;
    let gz = _gA[2] + (_gB[2] - _gA[2]) * t;
    const gl = Math.hypot(gx, gy, gz);
    if (gl > 1e-12) {
      const inv = 1 / gl;
      gx *= inv; gy *= inv; gz *= inv;
    } else {
      gx = ibx - iax; gy = iby - iay; gz = ibz - iaz;
      const sgn = va <= 0 ? 1 : -1;
      gx *= sgn; gy *= sgn; gz *= sgn;
    }

    const v00 = getOrCreateCellVert(c00x, c00y, c00z);
    const v10 = getOrCreateCellVert(c10x, c10y, c10z);
    const v11 = getOrCreateCellVert(c11x, c11y, c11z);
    const v01 = getOrCreateCellVert(c01x, c01y, c01z);

    addCrossingToCell(v00, px, py, pz, gx, gy, gz);
    addCrossingToCell(v10, px, py, pz, gx, gy, gz);
    addCrossingToCell(v11, px, py, pz, gx, gy, gz);
    addCrossingToCell(v01, px, py, pz, gx, gy, gz);

    if (va <= 0) {
      quads.push4(v00, v10, v11, v01);
    } else {
      quads.push4(v00, v01, v11, v10);
    }
  }

  const totalScanBricks = bricksToScan.length / 3;
  const reportStep = Math.max(1, Math.floor(totalScanBricks / 4));

  for (let bi = 0; bi < bricksToScan.length; bi += 3) {
    const bx = bricksToScan[bi];
    const by = bricksToScan[bi + 1];
    const bz = bricksToScan[bi + 2];

    const bIdx = bi / 3;
    if (onProgress && (bIdx % reportStep === 0)) {
      onProgress(0.66 + 0.14 * (bIdx / Math.max(1, totalScanBricks)), 'Extracting Dual Contour mesh...');
    }

    const xStart = bx * BRICK_SIZE;
    const xEnd = Math.min(nx, xStart + BRICK_SIZE);
    const yStart = by * BRICK_SIZE;
    const yEnd = Math.min(ny, yStart + BRICK_SIZE);
    const zStart = bz * BRICK_SIZE;
    const zEnd = Math.min(nz, zStart + BRICK_SIZE);

    for (let iz = zStart; iz < zEnd; iz++) {
      for (let iy = yStart; iy < yEnd; iy++) {
        for (let ix = xStart; ix < xEnd; ix++) {
          const v0 = ensureSample(ix, iy, iz);
          const in0 = v0 <= 0;

          if (ix < nx && iy >= 1 && iy < ny && iz >= 1 && iz < nz) {
            const vx = ensureSample(ix + 1, iy, iz);
            if (in0 !== (vx <= 0)) {
              processGridEdge(
                ix, iy, iz, v0, ix + 1, iy, iz, vx,
                ix, iy - 1, iz - 1,
                ix, iy,     iz - 1,
                ix, iy,     iz,
                ix, iy - 1, iz
              );
            }
          }

          if (iy < ny && ix >= 1 && ix < nx && iz >= 1 && iz < nz) {
            const vy = ensureSample(ix, iy + 1, iz);
            if (in0 !== (vy <= 0)) {
              processGridEdge(
                ix, iy, iz, v0, ix, iy + 1, iz, vy,
                ix - 1, iy, iz - 1,
                ix - 1, iy, iz,
                ix,     iy, iz,
                ix,     iy, iz - 1
              );
            }
          }

          if (iz < nz && ix >= 1 && ix < nx && iy >= 1 && iy < ny) {
            const vz = ensureSample(ix, iy, iz + 1);
            if (in0 !== (vz <= 0)) {
              processGridEdge(
                ix, iy, iz, v0, ix, iy, iz + 1, vz,
                ix - 1, iy - 1, iz,
                ix,     iy - 1, iz,
                ix,     iy,     iz,
                ix - 1, iy,     iz
              );
            }
          }
        }
      }
    }
  }

  const positions = new Float32Array(numVerts * 3);
  const cellNormals = new Float32Array(numVerts * 3);
  const creaseDispersion = new Float32Array(numVerts);

  const qBuf = qefData.buf;
  const cBuf = cellCoords.buf;
  const pad = h * 0.04;
  const hiLocal = h - pad;

  for (let v = 0; v < numVerts; v++) {
    const base = v * 16;
    const count = qBuf[base + 15];
    const invN = 1 / (count || 1);

    const mx = qBuf[base + 9] * invN;
    const my = qBuf[base + 10] * invN;
    const mz = qBuf[base + 11] * invN;

    const nsx = qBuf[base + 12];
    const nsy = qBuf[base + 13];
    const nsz = qBuf[base + 14];
    const nl = Math.hypot(nsx, nsy, nsz);
    creaseDispersion[v] = Math.max(0, 1 - nl * invN);

    const vi = v * 3;
    if (nl > 1e-8) {
      cellNormals[vi] = nsx / nl;
      cellNormals[vi + 1] = nsy / nl;
      cellNormals[vi + 2] = nsz / nl;
    } else {
      cellNormals[vi] = 0;
      cellNormals[vi + 1] = 1;
      cellNormals[vi + 2] = 0;
    }

    const raw00 = qBuf[base + 0], raw01 = qBuf[base + 1], raw02 = qBuf[base + 2];
    const raw11 = qBuf[base + 3], raw12 = qBuf[base + 4], raw22 = qBuf[base + 5];

    const b0 = qBuf[base + 6] - (raw00 * mx + raw01 * my + raw02 * mz);
    const b1 = qBuf[base + 7] - (raw01 * mx + raw11 * my + raw12 * mz);
    const b2 = qBuf[base + 8] - (raw02 * mx + raw12 * my + raw22 * mz);

    const a00 = raw00 + qefReg;
    const a11 = raw11 + qefReg;
    const a22 = raw22 + qefReg;

    const c00 = a11 * a22 - raw12 * raw12;
    const c01 = raw02 * raw12 - raw01 * a22;
    const c02 = raw01 * raw12 - raw02 * a11;
    const det = a00 * c00 + raw01 * c01 + raw02 * c02;

    let ux = 0, uy = 0, uz = 0;
    if (Math.abs(det) > 1e-12) {
      const invDet = 1 / det;
      const c11 = a00 * a22 - raw02 * raw02;
      const c12 = raw01 * raw02 - a00 * raw12;
      const c22 = a00 * a11 - raw01 * raw01;

      ux = (c00 * b0 + c01 * b1 + c02 * b2) * invDet;
      uy = (c01 * b0 + c11 * b1 + c12 * b2) * invDet;
      uz = (c02 * b0 + c12 * b1 + c22 * b2) * invDet;
    }

    let lx = mx + ux;
    let ly = my + uy;
    let lz = mz + uz;

    if (lx < pad || lx > hiLocal || ly < pad || ly > hiLocal || lz < pad || lz > hiLocal) {
      let t = 1.0;
      if (ux > 1e-9 && lx > hiLocal) t = Math.min(t, Math.max(0, (hiLocal - mx) / ux));
      if (ux < -1e-9 && lx < pad) t = Math.min(t, Math.max(0, (pad - mx) / ux));
      if (uy > 1e-9 && ly > hiLocal) t = Math.min(t, Math.max(0, (hiLocal - my) / uy));
      if (uy < -1e-9 && ly < pad) t = Math.min(t, Math.max(0, (pad - my) / uy));
      if (uz > 1e-9 && lz > hiLocal) t = Math.min(t, Math.max(0, (hiLocal - mz) / uz));
      if (uz < -1e-9 && lz < pad) t = Math.min(t, Math.max(0, (pad - mz) / uz));
      lx = mx + ux * t;
      ly = my + uy * t;
      lz = mz + uz * t;
    }

    lx = lx < pad ? pad : lx > hiLocal ? hiLocal : lx;
    ly = ly < pad ? pad : ly > hiLocal ? hiLocal : ly;
    lz = lz < pad ? pad : lz > hiLocal ? hiLocal : lz;

    positions[vi] = originX + cBuf[vi] * h + lx;
    positions[vi + 1] = originY + cBuf[vi + 1] * h + ly;
    positions[vi + 2] = originZ + cBuf[vi + 2] * h + lz;
  }

  const numQuads = quads.length >> 2;
  const indices = new Uint32Array(numQuads * 6);
  const qb = quads.buf;

  for (let q = 0; q < numQuads; q++) {
    const qi = q * 4;
    const v0 = qb[qi];
    const v1 = qb[qi + 1];
    const v2 = qb[qi + 2];
    const v3 = qb[qi + 3];

    const cDiag02 = creaseDispersion[v0] + creaseDispersion[v2];
    const cDiag13 = creaseDispersion[v1] + creaseDispersion[v3];

    let split02;
    if (Math.abs(cDiag02 - cDiag13) > 0.04) {
      split02 = cDiag02 > cDiag13;
    } else {
      const dx02 = positions[v0 * 3] - positions[v2 * 3];
      const dy02 = positions[v0 * 3 + 1] - positions[v2 * 3 + 1];
      const dz02 = positions[v0 * 3 + 2] - positions[v2 * 3 + 2];
      const dx13 = positions[v1 * 3] - positions[v3 * 3];
      const dy13 = positions[v1 * 3 + 1] - positions[v3 * 3 + 1];
      const dz13 = positions[v1 * 3 + 2] - positions[v3 * 3 + 2];
      split02 = (dx02 * dx02 + dy02 * dy02 + dz02 * dz02) <= (dx13 * dx13 + dy13 * dy13 + dz13 * dz13);
    }

    const ti = q * 6;
    if (split02) {
      indices[ti] = v0; indices[ti + 1] = v1; indices[ti + 2] = v2;
      indices[ti + 3] = v0; indices[ti + 4] = v2; indices[ti + 5] = v3;
    } else {
      indices[ti] = v0; indices[ti + 1] = v1; indices[ti + 2] = v3;
      indices[ti + 3] = v1; indices[ti + 4] = v2; indices[ti + 5] = v3;
    }
  }

  return {
    positions,
    indices,
    cellNormals,
    creaseDispersion,
  };
}
