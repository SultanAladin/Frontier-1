/**
 * Fast, zero-allocation 3x3 Symmetric Regularized QEF (Quadratic Error Function) Solver
 * for Feature-Preserving Dual Contouring of sharp geological fracture edges & corners.
 *
 * Minimizes: sum_i (n_i . (x - p_i))^2 + reg * ||x - massPoint||^2
 * Solving (A^T A + reg * I) u = A^T b, where u = x - massPoint.
 */

export class QefAccumulator {
  constructor() {
    this.reset();
  }

  reset() {
    this.ata00 = 0; this.ata01 = 0; this.ata02 = 0;
    this.ata11 = 0; this.ata12 = 0;
    this.ata22 = 0;
    this.atb0 = 0; this.atb1 = 0; this.atb2 = 0;
    this.mp0 = 0; this.mp1 = 0; this.mp2 = 0;
    this.ns0 = 0; this.ns1 = 0; this.ns2 = 0;
    this.count = 0;
    // Temporary storage for up to 12 edge crossings per voxel cell
    this.px = new Float64Array(12);
    this.py = new Float64Array(12);
    this.pz = new Float64Array(12);
    this.nx = new Float64Array(12);
    this.ny = new Float64Array(12);
    this.nz = new Float64Array(12);
  }

  add(px, py, pz, nx, ny, nz) {
    const i = this.count;
    if (i >= 12) return;
    this.px[i] = px; this.py[i] = py; this.pz[i] = pz;
    this.nx[i] = nx; this.ny[i] = ny; this.nz[i] = nz;
    this.mp0 += px; this.mp1 += py; this.mp2 += pz;
    this.ns0 += nx; this.ns1 += ny; this.ns2 += nz;
    this.count = i + 1;
  }

  /**
   * Solves for optimal sharp-feature vertex inside [minX..maxX] x [minY..maxY] x [minZ..maxZ].
   * Writes position to outPos[0..2] and returns normal dispersion (crease sharpness metric in [0, 1]).
   */
  solve(minX, minY, minZ, maxX, maxY, maxZ, reg, outPos, outAvgNorm) {
    const n = this.count;
    if (n === 0) {
      outPos[0] = (minX + maxX) * 0.5;
      outPos[1] = (minY + maxY) * 0.5;
      outPos[2] = (minZ + maxZ) * 0.5;
      outAvgNorm[0] = 0; outAvgNorm[1] = 1; outAvgNorm[2] = 0;
      return 0;
    }

    const invN = 1 / n;
    const mx = this.mp0 * invN;
    const my = this.mp1 * invN;
    const mz = this.mp2 * invN;

    const nl = Math.hypot(this.ns0, this.ns1, this.ns2);
    const dispersion = Math.max(0, 1 - nl * invN);
    if (nl > 1e-8) {
      outAvgNorm[0] = this.ns0 / nl;
      outAvgNorm[1] = this.ns1 / nl;
      outAvgNorm[2] = this.ns2 / nl;
    } else {
      outAvgNorm[0] = this.nx[0];
      outAvgNorm[1] = this.ny[0];
      outAvgNorm[2] = this.nz[0];
    }

    // Assemble centered normal equations around mass point (mx, my, mz) for high precision
    let a00 = reg, a01 = 0, a02 = 0;
    let a11 = reg, a12 = 0, a22 = reg;
    let b0 = 0, b1 = 0, b2 = 0;

    for (let i = 0; i < n; i++) {
      const nx = this.nx[i], ny = this.ny[i], nz = this.nz[i];
      const dx = this.px[i] - mx;
      const dy = this.py[i] - my;
      const dz = this.pz[i] - mz;
      const rhs = nx * dx + ny * dy + nz * dz;

      a00 += nx * nx; a01 += nx * ny; a02 += nx * nz;
      a11 += ny * ny; a12 += ny * nz;
      a22 += nz * nz;

      b0 += nx * rhs;
      b1 += ny * rhs;
      b2 += nz * rhs;
    }

    // Analytic 3x3 symmetric positive-definite Cholesky / Cramer solve
    const c00 = a11 * a22 - a12 * a12;
    const c01 = a02 * a12 - a01 * a22;
    const c02 = a01 * a12 - a02 * a11;
    const det = a00 * c00 + a01 * c01 + a02 * c02;

    let ux = 0, uy = 0, uz = 0;
    if (Math.abs(det) > 1e-12) {
      const invDet = 1 / det;
      const c11 = a00 * a22 - a02 * a02;
      const c12 = a01 * a02 - a00 * a12;
      const c22 = a00 * a11 - a01 * a01;

      ux = (c00 * b0 + c01 * b1 + c02 * b2) * invDet;
      uy = (c01 * b0 + c11 * b1 + c12 * b2) * invDet;
      uz = (c02 * b0 + c12 * b1 + c22 * b2) * invDet;
    }

    let vx = mx + ux;
    let vy = my + uy;
    let vz = mz + uz;

    // Ray-clip back toward mass point if QEF overshoot leaves padded cell bounds
    const padX = (maxX - minX) * 0.02;
    const padY = (maxY - minY) * 0.02;
    const padZ = (maxZ - minZ) * 0.02;
    const loX = minX + padX, hiX = maxX - padX;
    const loY = minY + padY, hiY = maxY - padY;
    const loZ = minZ + padZ, hiZ = maxZ - padZ;

    if (vx < loX || vx > hiX || vy < loY || vy > hiY || vz < loZ || vz > hiZ) {
      let t = 1.0;
      if (ux > 1e-9 && vx > hiX) t = Math.min(t, Math.max(0, (hiX - mx) / ux));
      if (ux < -1e-9 && vx < loX) t = Math.min(t, Math.max(0, (loX - mx) / ux));
      if (uy > 1e-9 && vy > hiY) t = Math.min(t, Math.max(0, (hiY - my) / uy));
      if (uy < -1e-9 && vy < loY) t = Math.min(t, Math.max(0, (loY - my) / uy));
      if (uz > 1e-9 && vz > hiZ) t = Math.min(t, Math.max(0, (hiZ - mz) / uz));
      if (uz < -1e-9 && vz < loZ) t = Math.min(t, Math.max(0, (loZ - mz) / uz));
      vx = mx + ux * t;
      vy = my + uy * t;
      vz = mz + uz * t;
    }

    outPos[0] = Math.min(hiX, Math.max(loX, vx));
    outPos[1] = Math.min(hiY, Math.max(loY, vy));
    outPos[2] = Math.min(hiZ, Math.max(loZ, vz));
    return dispersion;
  }
}
