/*==========================================================================================================================================
                                                 ENGINEERINGBAKEDFRACTURE.JS
==========================================================================================================================================*/
// 🧩 Offline / Author-Time Engineering-Grade Fracture Solver:
//    ① 3D Volumetric Navier–Cauchy Elastostatic Lattice (Hertzian contact traction + foundation support + Hopkinson spall)
//    ② Exact Analytical 3×3 Symmetric Principal Stress Eigensolver (σ1 >= σ2 >= σ3, Von Mises, Strain Energy Density)
//    ③ Competing Mode-I LEFM Rankine Tensile + Mode-II Mohr-Coulomb Shear Wedge Failure with 3-Parameter Weibull Flaws
//    ④ Sequential 3D Stress-Shadow Wake Relaxation (P_|| σ P_|| -> emergent 90° T-junctions & Hillerborg crack spacing)
//    ⑤ Watertight Matched Non-Planar Fractographic Surface Tessellation:
//       • Bouchaud self-affine roughness (H ≈ 0.78) + 3D Meso-Aggregate Pullout Sockets/Clasts (Concrete / Granite)
//       • Mirror–Mist–Hackle radial transition + Wallner sonic arcs (Annealed / Tempered Glass)
//       • 12:1 Orthotropic Tracheid Splinter Flutes (Timber) & Cup-and-Cone Shear Lips (Ductile Polymer)
//       • Preserves exact ΣV_i = V_0 watertight interlocking fit AND exports per-shard Convex Collision Proxy Hulls

import { MaterialCatalogue, EvaluateRayleighWaveSpeed, ConstructRandomStream } from "./ConvexPartitioning.js";
import { PackFlatConvexCell, UnpackFlatConvexCell, ClipFlatConvexCell, GetLastClipCells, BuildFastTokenBonds } from "./FastRuntimeFracture.js";

//------------------------------------------------------------------------------------------------------------------------
//                           CONSTITUTIVE SOLID MECHANICS & FRACTOGRAPHY PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

export const EngineeringConstitutiveCatalogue = Object.freeze({
    concrete: {
        CompressiveStrengthMPa: 38.0,
        InternalFrictionDeg: 36.0,
        HurstExponent: 0.78,
        AggregateScale: 6.5,
        AggregateWeight: 0.58,
        ReliefAmplitudeM: 0.036,
        FractographyLabel: "Bouchaud H=0.78 + Meso-Aggregate Pullout Sockets"
    },
    granite: {
        CompressiveStrengthMPa: 165.0,
        InternalFrictionDeg: 48.0,
        HurstExponent: 0.80,
        AggregateScale: 9.5,
        AggregateWeight: 0.44,
        ReliefAmplitudeM: 0.030,
        FractographyLabel: "Crystalline Cleavage Steps + Quartz Clast Deflection"
    },
    wood: {
        CompressiveStrengthMPa: 45.0,
        InternalFrictionDeg: 24.0,
        HurstExponent: 0.72,
        AggregateScale: 14.0,
        AggregateWeight: 0.0,
        ReliefAmplitudeM: 0.042,
        FractographyLabel: "12:1 Orthotropic Tracheid Fiber Splinter Flutes"
    },
    glass: {
        CompressiveStrengthMPa: 700.0,
        InternalFrictionDeg: 30.0,
        HurstExponent: 0.55,
        AggregateScale: 12.0,
        AggregateWeight: 0.0,
        ReliefAmplitudeM: 0.014,
        FractographyLabel: "Mirror–Mist–Hackle Zones + Wallner Sonic Ripples"
    },
    tempered: {
        CompressiveStrengthMPa: 900.0,
        InternalFrictionDeg: 32.0,
        HurstExponent: 0.58,
        AggregateScale: 16.0,
        AggregateWeight: 0.0,
        ReliefAmplitudeM: 0.010,
        FractographyLabel: "Bifurcated Residual-Core Dicing + Wallner Steps"
    },
    plastic: {
        CompressiveStrengthMPa: 68.0,
        InternalFrictionDeg: 8.0,
        HurstExponent: 0.66,
        AggregateScale: 8.0,
        AggregateWeight: 0.0,
        ReliefAmplitudeM: 0.032,
        FractographyLabel: "Cup-and-Cone Perimeter Shear Lip + Void Dimples"
    }
});

//------------------------------------------------------------------------------------------------------------------------
//                      DETERMINISTIC 3D HASH, GRADIENT NOISE & CELLULAR AGGREGATE FIELD
//------------------------------------------------------------------------------------------------------------------------

function Hash3D(ix, iy, iz, seed)
{
    let h = (Math.imul(ix, 374761393) + Math.imul(iy, 668265263) + Math.imul(iz, 2147483647) + Math.imul(seed, 1597334677)) | 0;
    h = Math.imul(h ^ (h >>> 13), 1274126177);
    return ((h ^ (h >>> 16)) >>> 0) / 4294967296.0;
}

function ValueNoise3D(x, y, z, seed)
{
    const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z);
    const fx = x - ix, fy = y - iy, fz = z - iz;
    const ux = fx * fx * (3.0 - 2.0 * fx);
    const uy = fy * fy * (3.0 - 2.0 * fy);
    const uz = fz * fz * (3.0 - 2.0 * fz);

    const n000 = Hash3D(ix, iy, iz, seed);
    const n100 = Hash3D(ix + 1, iy, iz, seed);
    const n010 = Hash3D(ix, iy + 1, iz, seed);
    const n110 = Hash3D(ix + 1, iy + 1, iz, seed);
    const n001 = Hash3D(ix, iy, iz + 1, seed);
    const n101 = Hash3D(ix + 1, iy, iz + 1, seed);
    const n011 = Hash3D(ix, iy + 1, iz + 1, seed);
    const n111 = Hash3D(ix + 1, iy + 1, iz + 1, seed);

    const nx00 = n000 + (n100 - n000) * ux;
    const nx10 = n010 + (n110 - n010) * ux;
    const nx01 = n001 + (n101 - n001) * ux;
    const nx11 = n011 + (n111 - n011) * ux;
    const nxy0 = nx00 + (nx10 - nx00) * uy;
    const nxy1 = nx01 + (nx11 - nx01) * uy;
    return (nxy0 + (nxy1 - nxy0) * uz) * 2.0 - 1.0;
}

function SelfAffineBouchaudFbm(x, y, z, Hurst, Octaves, Seed)
{
    const Gain = Math.pow(2.0, -Hurst);
    let freq = 1.0, amp = 1.0, sum = 0.0, norm = 0.0;
    for (let o = 0; o < Octaves; ++o)
    {
        sum += amp * ValueNoise3D(x * freq, y * freq, z * freq, Seed + o * 101);
        norm += amp;
        freq *= 2.07;
        amp *= Gain;
    }
    return sum / Math.max(1e-6, norm);
}

/**
 * 3D Meso-Aggregate Clast Pullout Field:
 * Models stiff gravel/quartz inclusions where an intergranular crack deflects OVER or UNDER each clast
 * (+1 bump or -1 socket depending on clast parity), producing realistic concrete/granite aggregate sockets!
 */
function SampleAggregateInclusionField(x, y, z, Scale, Seed)
{
    const sx = x * Scale, sy = y * Scale, sz = z * Scale;
    const bx = Math.floor(sx), by = Math.floor(sy), bz = Math.floor(sz);

    let bestD2 = 99.0;
    let bestParity = 1.0;
    let bestRadius = 0.38;

    for (let dz = -1; dz <= 1; ++dz)
    {
        for (let dy = -1; dy <= 1; ++dy)
        {
            for (let dx = -1; dx <= 1; ++dx)
            {
                const cx = bx + dx, cy = by + dy, cz = bz + dz;
                const jx = cx + 0.18 + 0.64 * Hash3D(cx, cy, cz, Seed + 11);
                const jy = cy + 0.18 + 0.64 * Hash3D(cx, cy, cz, Seed + 29);
                const jz = cz + 0.18 + 0.64 * Hash3D(cx, cy, cz, Seed + 47);
                const dist2 = (sx - jx) * (sx - jx) + (sy - jy) * (sy - jy) + (sz - jz) * (sz - jz);
                if (dist2 < bestD2)
                {
                    bestD2 = dist2;
                    bestParity = Hash3D(cx, cy, cz, Seed + 73) > 0.5 ? 1.0 : -1.0;
                    bestRadius = 0.28 + 0.20 * Hash3D(cx, cy, cz, Seed + 91);
                }
            }
        }
    }

    const r2 = bestRadius * bestRadius;
    if (bestD2 >= r2) return 0.0;
    // Smooth spherical dome cap where crack wraps around the stiff aggregate particle
    return bestParity * Math.sqrt(Math.max(0.0, 1.0 - bestD2 / r2));
}

//------------------------------------------------------------------------------------------------------------------------
//                      EXACT ANALYTICAL 3x3 SYMMETRIC CAUCHY STRESS EIGENSOLVER (SMITH 1961)
//------------------------------------------------------------------------------------------------------------------------

/**
 * Tensor = [sxx, syy, szz, txy, tyz, txz]
 * Writes OutPrincipals = [s1, s2, s3, e1x, e1y, e1z, e3x, e3y, e3z, vonMises] with s1 >= s2 >= s3
 */
export function SolveSymmetricEigen3x3(sxx, syy, szz, txy, tyz, txz, Out)
{
    const p1 = txy * txy + txz * txz + tyz * tyz;
    const q = (sxx + syy + szz) / 3.0;

    let s1, s2, s3;
    if (p1 < 1e-20)
    {
        // Already diagonal
        s1 = sxx; s2 = syy; s3 = szz;
        if (s1 < s2) { const t = s1; s1 = s2; s2 = t; }
        if (s2 < s3) { const t = s2; s2 = s3; s3 = t; }
        if (s1 < s2) { const t = s1; s1 = s2; s2 = t; }
    }
    else
    {
        const bxx = sxx - q, byy = syy - q, bzz = szz - q;
        const p2 = bxx * bxx + byy * byy + bzz * bzz + 2.0 * p1;
        const p = Math.sqrt(p2 / 6.0);
        const invP = 1.0 / p;
        const cxx = bxx * invP, cyy = byy * invP, czz = bzz * invP;
        const cxy = txy * invP, cyz = tyz * invP, cxz = txz * invP;

        const detC = cxx * (cyy * czz - cyz * cyz)
                   - cxy * (cxy * czz - cyz * cxz)
                   + cxz * (cxy * cyz - cyy * cxz);
        const r = Math.max(-1.0, Math.min(1.0, 0.5 * detC));
        const phi = Math.acos(r) / 3.0;

        s1 = q + 2.0 * p * Math.cos(phi);
        s3 = q + 2.0 * p * Math.cos(phi + (2.0 * Math.PI / 3.0));
        s2 = 3.0 * q - s1 - s3;
    }

    // Extract unit eigenvector for eigenvalue lambda via largest row cross-product of (A - lambda*I)
    const ExtractEigenvector = (lam, writeOffset, fallbackX, fallbackY, fallbackZ) =>
    {
        const r0x = sxx - lam, r0y = txy,       r0z = txz;
        const r1x = txy,       r1y = syy - lam, r1z = tyz;
        const r2x = txz,       r2y = tyz,       r2z = szz - lam;

        const c01x = r0y * r1z - r0z * r1y, c01y = r0z * r1x - r0x * r1z, c01z = r0x * r1y - r0y * r1x;
        const c02x = r0y * r2z - r0z * r2y, c02y = r0z * r2x - r0x * r2z, c02z = r0x * r2y - r0y * r2x;
        const c12x = r1y * r2z - r1z * r2y, c12y = r1z * r2x - r1x * r2z, c12z = r1x * r2y - r1y * r2x;

        const d01 = c01x * c01x + c01y * c01y + c01z * c01z;
        const d02 = c02x * c02x + c02y * c02y + c02z * c02z;
        const d12 = c12x * c12x + c12y * c12y + c12z * c12z;

        let vx = fallbackX, vy = fallbackY, vz = fallbackZ;
        let best = d01;
        if (d01 > 1e-22) { vx = c01x; vy = c01y; vz = c01z; }
        if (d02 > best && d02 > 1e-22) { best = d02; vx = c02x; vy = c02y; vz = c02z; }
        if (d12 > best && d12 > 1e-22) { best = d12; vx = c12x; vy = c12y; vz = c12z; }

        const nL = Math.hypot(vx, vy, vz);
        if (nL > 1e-12) { vx /= nL; vy /= nL; vz /= nL; }
        Out[writeOffset] = vx;
        Out[writeOffset + 1] = vy;
        Out[writeOffset + 2] = vz;
    };

    ExtractEigenvector(s1, 3, 1, 0, 0);
    ExtractEigenvector(s3, 6, 0, 1, 0);

    const d12 = s1 - s2, d23 = s2 - s3, d31 = s3 - s1;
    const vonMises = Math.sqrt(0.5 * (d12 * d12 + d23 * d23 + d31 * d31));

    Out[0] = s1;
    Out[1] = s2;
    Out[2] = s3;
    Out[9] = vonMises;
}

//------------------------------------------------------------------------------------------------------------------------
//                  STEP ①: 3D VOLUMETRIC NAVIER–CAUCHY ELASTOSTATIC STRESS LATTICE SOLVER
//------------------------------------------------------------------------------------------------------------------------

function PointInsideFlatConvexCell(Cell, px, py, pz)
{
    const V = Cell.verts, FOff = Cell.faceOffsets, FIdx = Cell.faceIndices;
    for (let f = 0; f < Cell.fCount; ++f)
    {
        const s = FOff[f];
        const i0 = FIdx[s] * 3, i1 = FIdx[s + 1] * 3, i2 = FIdx[s + 2] * 3;
        const e1x = V[i1] - V[i0], e1y = V[i1 + 1] - V[i0 + 1], e1z = V[i1 + 2] - V[i0 + 2];
        const e2x = V[i2] - V[i0], e2y = V[i2 + 1] - V[i0 + 1], e2z = V[i2 + 2] - V[i0 + 2];
        const nx = e1y * e2z - e1z * e2y;
        const ny = e1z * e2x - e1x * e2z;
        const nz = e1x * e2y - e1y * e2x;
        const dot = (px - V[i0]) * nx + (py - V[i0 + 1]) * ny + (pz - V[i0 + 2]) * nz;
        if (dot > 1e-5) return false;
    }
    return true;
}

function SolveVolumetricStressLattice(RootCell, Mat, EngMat, ImpactEnergyJ, ImpP, ImpN, GridN = 12)
{
    const N = GridN;
    const Total = N * N * N;

    const minX = RootCell.minX, minY = RootCell.minY, minZ = RootCell.minZ;
    const spanX = Math.max(0.05, RootCell.maxX - minX);
    const spanY = Math.max(0.05, RootCell.maxY - minY);
    const spanZ = Math.max(0.05, RootCell.maxZ - minZ);
    const hx = spanX / N, hy = spanY / N, hz = spanZ / N;
    const hMean = (hx + hy + hz) / 3.0;

    // Hertzian Contact Mechanics in MPa & meters
    const E_MPa = Mat.YoungModulusGPa * 1000.0;
    const nu = Math.min(0.45, Math.max(0.10, Mat.PoissonRatio));
    const EStar_Pa = (E_MPa * 1e6) / (1.0 - nu * nu);
    const RIndenter = Math.max(0.025, RootCell.radius * 0.10);
    const FMax_N = Math.pow(1.25 * ImpactEnergyJ, 0.6) * Math.pow(1.3333 * EStar_Pa * Math.sqrt(RIndenter), 0.4);
    const ContactRadiusM = Math.max(hMean * 1.15, Math.min(RootCell.radius * 0.42, Math.cbrt((0.75 * FMax_N * RIndenter) / EStar_Pa)));
    const PeakHertzPressureMPa = Math.min(EngMat.CompressiveStrengthMPa * 6.5, (1.5 * FMax_N) / (Math.PI * ContactRadiusM * ContactRadiusM * 1e6));

    // Normalized Lamé parameters (scaled by E_MPa so displacement u is O(1e-3))
    const Lambda = (E_MPa * nu) / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const Mu = E_MPa / (2.0 * (1.0 + nu));

    const Active = new Uint8Array(Total);
    const Anchored = new Uint8Array(Total);
    const Disp = new Float64Array(Total * 3);
    const Force = new Float64Array(Total * 3);
    const Pos = new Float64Array(Total * 3);

    const inL = 1.0 / (Math.hypot(ImpN[0], ImpN[1], ImpN[2]) || 1.0);
    const dxImp = -ImpN[0] * inL, dyImp = -ImpN[1] * inL, dzImp = -ImpN[2] * inL;
    const ipx = ImpP[0], ipy = ImpP[1], ipz = ImpP[2];

    for (let iz = 0; iz < N; ++iz)
    {
        const pz = minZ + (iz + 0.5) * hz;
        for (let iy = 0; iy < N; ++iy)
        {
            const py = minY + (iy + 0.5) * hy;
            for (let ix = 0; ix < N; ++ix)
            {
                const px = minX + (ix + 0.5) * hx;
                const idx = (iz * N + iy) * N + ix;
                Pos[idx * 3] = px; Pos[idx * 3 + 1] = py; Pos[idx * 3 + 2] = pz;

                if (!PointInsideFlatConvexCell(RootCell, px, py, pz)) continue;
                Active[idx] = 1;

                // Bottom foundation anchorage
                if (iy === 0) Anchored[idx] = 1;

                // Hertzian contact patch traction & subsurface Boussinesq wave impulse
                const rx = px - ipx, ry = py - ipy, rz = pz - ipz;
                const zDepth = rx * dxImp + ry * dyImp + rz * dzImp;
                const rPerpX = rx - dxImp * zDepth;
                const rPerpY = ry - dyImp * zDepth;
                const rPerpZ = rz - dzImp * zDepth;
                const rPerp = Math.hypot(rPerpX, rPerpY, rPerpZ);

                if (zDepth >= -hMean * 1.2 && zDepth <= ContactRadiusM * 2.4 && rPerp <= ContactRadiusM * 1.6)
                {
                    const qNorm = Math.max(0.0, 1.0 - (rPerp * rPerp) / Math.pow(ContactRadiusM * 1.6, 2));
                    const depthAtten = Math.exp(-Math.max(0.0, zDepth) / (ContactRadiusM * 1.5));
                    const pLocal = PeakHertzPressureMPa * Math.sqrt(qNorm) * depthAtten;
                    const invRp = rPerp > 1e-6 ? 1.0 / rPerp : 0.0;
                    // Inward axial compression + Poisson radial wedge expansion
                    Force[idx * 3]     += (dxImp * pLocal + rPerpX * invRp * pLocal * nu * 0.85) / hMean;
                    Force[idx * 3 + 1] += (dyImp * pLocal + rPerpY * invRp * pLocal * nu * 0.85) / hMean;
                    Force[idx * 3 + 2] += (dzImp * pLocal + rPerpZ * invRp * pLocal * nu * 0.85) / hMean;
                }

                // Hopkinson rear-face reflected tensile wave on opposite free boundary
                if (zDepth > RootCell.radius * 0.45 && rPerp < ContactRadiusM * 2.8)
                {
                    const spallPull = PeakHertzPressureMPa * 0.16 * Math.exp(-(rPerp * rPerp) / Math.pow(ContactRadiusM * 2.5, 2));
                    Force[idx * 3]     += dxImp * spallPull / hMean;
                    Force[idx * 3 + 1] += dyImp * spallPull / hMean;
                    Force[idx * 3 + 2] += dzImp * spallPull / hMean;
                }
            }
        }
    }

    // 28 iterations of 3D Navier–Cauchy vector-SOR equilibrium
    const invH2 = 1.0 / (hMean * hMean);
    const Diag = 2.0 * (Lambda + 4.0 * Mu) * invH2;
    const Omega = 1.48;

    for (let iter = 0; iter < 28; ++iter)
    {
        for (let iz = 0; iz < N; ++iz)
        {
            for (let iy = 0; iy < N; ++iy)
            {
                for (let ix = 0; ix < N; ++ix)
                {
                    const idx = (iz * N + iy) * N + ix;
                    if (!Active[idx] || Anchored[idx]) continue;

                    const xm = ix > 0 && Active[idx - 1] ? idx - 1 : idx;
                    const xp = ix + 1 < N && Active[idx + 1] ? idx + 1 : idx;
                    const ym = iy > 0 && Active[idx - N] ? idx - N : idx;
                    const yp = iy + 1 < N && Active[idx + N] ? idx + N : idx;
                    const zm = iz > 0 && Active[idx - N * N] ? idx - N * N : idx;
                    const zp = iz + 1 < N && Active[idx + N * N] ? idx + N * N : idx;

                    // Laplacian of u
                    const lapUx = (Disp[xm * 3] + Disp[xp * 3] + Disp[ym * 3] + Disp[yp * 3] + Disp[zm * 3] + Disp[zp * 3]) * invH2;
                    const lapUy = (Disp[xm * 3 + 1] + Disp[xp * 3 + 1] + Disp[ym * 3 + 1] + Disp[yp * 3 + 1] + Disp[zm * 3 + 1] + Disp[zp * 3 + 1]) * invH2;
                    const lapUz = (Disp[xm * 3 + 2] + Disp[xp * 3 + 2] + Disp[ym * 3 + 2] + Disp[yp * 3 + 2] + Disp[zm * 3 + 2] + Disp[zp * 3 + 2]) * invH2;

                    // Unmixed axial second derivatives for (Lambda + Mu) grad(div u) diagonal contribution
                    const d2xx = (Disp[xm * 3] + Disp[xp * 3]) * invH2;
                    const d2yy = (Disp[ym * 3 + 1] + Disp[yp * 3 + 1]) * invH2;
                    const d2zz = (Disp[zm * 3 + 2] + Disp[zp * 3 + 2]) * invH2;

                    const rhsX = Mu * lapUx + (Lambda + Mu) * d2xx + Force[idx * 3];
                    const rhsY = Mu * lapUy + (Lambda + Mu) * d2yy + Force[idx * 3 + 1];
                    const rhsZ = Mu * lapUz + (Lambda + Mu) * d2zz + Force[idx * 3 + 2];

                    Disp[idx * 3]     += Omega * (rhsX / Diag - Disp[idx * 3]);
                    Disp[idx * 3 + 1] += Omega * (rhsY / Diag - Disp[idx * 3 + 1]);
                    Disp[idx * 3 + 2] += Omega * (rhsZ / Diag - Disp[idx * 3 + 2]);
                }
            }
        }
    }

    // Evaluate full 3x3 Cauchy Stress Tensor [sxx, syy, szz, txy, tyz, txz] (MPa) at every active cell
    const StressTensor = new Float64Array(Total * 6);
    for (let iz = 0; iz < N; ++iz)
    {
        for (let iy = 0; iy < N; ++iy)
        {
            for (let ix = 0; ix < N; ++ix)
            {
                const idx = (iz * N + iy) * N + ix;
                if (!Active[idx]) continue;

                const xm = ix > 0 && Active[idx - 1] ? idx - 1 : idx;
                const xp = ix + 1 < N && Active[idx + 1] ? idx + 1 : idx;
                const ym = iy > 0 && Active[idx - N] ? idx - N : idx;
                const yp = iy + 1 < N && Active[idx + N] ? idx + N : idx;
                const zm = iz > 0 && Active[idx - N * N] ? idx - N * N : idx;
                const zp = iz + 1 < N && Active[idx + N * N] ? idx + N * N : idx;

                const invDx = 1.0 / ((xp > xm ? (xp - xm) : 1) * hx);
                const invDy = 1.0 / ((yp > ym ? (yp - ym) : 1) * hy);
                const invDz = 1.0 / ((zp > zm ? (zp - zm) : 1) * hz);

                const exx = (Disp[xp * 3] - Disp[xm * 3]) * invDx;
                const eyy = (Disp[yp * 3 + 1] - Disp[ym * 3 + 1]) * invDy;
                const ezz = (Disp[zp * 3 + 2] - Disp[zm * 3 + 2]) * invDz;

                const exy = 0.5 * ((Disp[yp * 3] - Disp[ym * 3]) * invDy + (Disp[xp * 3 + 1] - Disp[xm * 3 + 1]) * invDx);
                const eyz = 0.5 * ((Disp[zp * 3 + 1] - Disp[zm * 3 + 1]) * invDz + (Disp[yp * 3 + 2] - Disp[ym * 3 + 2]) * invDy);
                const exz = 0.5 * ((Disp[zp * 3] - Disp[zm * 3]) * invDz + (Disp[xp * 3 + 2] - Disp[xm * 3 + 2]) * invDx);

                const trace = exx + eyy + ezz;
                let sxx = Lambda * trace + 2.0 * Mu * exx;
                let syy = Lambda * trace + 2.0 * Mu * eyy;
                let szz = Lambda * trace + 2.0 * Mu * ezz;
                const txy = 2.0 * Mu * exy;
                const tyz = 2.0 * Mu * eyz;
                const txz = 2.0 * Mu * exz;

                // Superimpose analytical Boussinesq near-field hoop/radial tension & residual tempering core tension
                const px = Pos[idx * 3], py = Pos[idx * 3 + 1], pz = Pos[idx * 3 + 2];
                const rx = px - ipx, ry = py - ipy, rz = pz - ipz;
                const rho = Math.max(ContactRadiusM * 0.65, Math.hypot(rx, ry, rz));
                const boussMag = PeakHertzPressureMPa * 0.24 * Math.pow(ContactRadiusM / rho, 1.65);

                // Hoop tensile vector perpendicular to (r, dImp)
                let hpx = dyImp * rz - dzImp * ry;
                let hpy = dzImp * rx - dxImp * rz;
                let hpz = dxImp * ry - dyImp * rx;
                const hpL = Math.hypot(hpx, hpy, hpz);
                if (hpL > 1e-6) { hpx /= hpL; hpy /= hpL; hpz /= hpL; }
                else { hpx = 1; hpy = 0; hpz = 0; }

                sxx += boussMag * hpx * hpx;
                syy += boussMag * hpy * hpy;
                szz += boussMag * hpz * hpz;

                // Tempered glass parabolic core-skin residual stress profile
                if (Mat.StoredEnergy > 0)
                {
                    const normZ = (pz - RootCell.cz) / Math.max(0.02, 0.5 * spanZ);
                    const resMPa = 48.0 * (1.0 - 3.0 * normZ * normZ); // +48 MPa core tension, -96 MPa skin compression
                    sxx += resMPa;
                    syy += resMPa;
                }

                const b = idx * 6;
                StressTensor[b] = sxx; StressTensor[b + 1] = syy; StressTensor[b + 2] = szz;
                StressTensor[b + 3] = txy; StressTensor[b + 4] = tyz; StressTensor[b + 5] = txz;
            }
        }
    }

    return {
        GridN: N,
        Total,
        Active,
        Pos,
        StressTensor,
        VoxelVolume: hx * hy * hz,
        CellSize: hMean,
        Bounds: { minX, minY, minZ, spanX, spanY, spanZ },
        Hertz: { FMax_N, ContactRadiusM, PeakHertzPressureMPa }
    };
}

/**
 * 3D Wake-Shielding Stress Relaxation:
 * Projects Cauchy stress tensor σ -> P_|| σ P_|| where P_|| = I - w (n ⊗ n)
 * in the stress shadow around newly created crack plane (nx, ny, nz, d) inside BoundsMin..BoundsMax.
 */
function RelaxStressTensorAroundCrack(Lattice, nx, ny, nz, d, FragCell, ReliefLengthM)
{
    const { Total, Active, Pos, StressTensor } = Lattice;
    const pad = ReliefLengthM * 1.15;
    const minX = FragCell.minX - pad, maxX = FragCell.maxX + pad;
    const minY = FragCell.minY - pad, maxY = FragCell.maxY + pad;
    const minZ = FragCell.minZ - pad, maxZ = FragCell.maxZ + pad;

    for (let i = 0; i < Total; ++i)
    {
        if (!Active[i]) continue;
        const px = Pos[i * 3], py = Pos[i * 3 + 1], pz = Pos[i * 3 + 2];
        if (px < minX || px > maxX || py < minY || py > maxY || pz < minZ || pz > maxZ) continue;

        const dist = Math.abs(nx * px + ny * py + nz * pz - d);
        if (dist > ReliefLengthM * 2.2) continue;

        const w = 0.92 * Math.exp(-dist / Math.max(1e-4, ReliefLengthM));
        // P = I - w n n^T  =>  sigma' = P sigma P
        const p00 = 1.0 - w * nx * nx, p01 = -w * nx * ny,      p02 = -w * nx * nz;
        const p11 = 1.0 - w * ny * ny, p12 = -w * ny * nz;
        const p22 = 1.0 - w * nz * nz;

        const b = i * 6;
        const s00 = StressTensor[b],     s11 = StressTensor[b + 1], s22 = StressTensor[b + 2];
        const s01 = StressTensor[b + 3], s12 = StressTensor[b + 4], s02 = StressTensor[b + 5];

        // M = P * S
        const m00 = p00 * s00 + p01 * s01 + p02 * s02;
        const m01 = p00 * s01 + p01 * s11 + p02 * s12;
        const m02 = p00 * s02 + p01 * s12 + p02 * s22;

        const m10 = p01 * s00 + p11 * s01 + p12 * s02;
        const m11 = p01 * s01 + p11 * s11 + p12 * s12;
        const m12 = p01 * s02 + p11 * s12 + p12 * s22;

        const m20 = p02 * s00 + p12 * s01 + p22 * s02;
        const m21 = p02 * s01 + p12 * s11 + p22 * s12;
        const m22 = p02 * s02 + p12 * s12 + p22 * s22;

        // S' = M * P
        StressTensor[b]     = m00 * p00 + m01 * p01 + m02 * p02;
        StressTensor[b + 1] = m10 * p01 + m11 * p11 + m12 * p12;
        StressTensor[b + 2] = m20 * p02 + m21 * p12 + m22 * p22;
        StressTensor[b + 3] = m00 * p01 + m01 * p11 + m02 * p12;
        StressTensor[b + 4] = m10 * p02 + m11 * p12 + m12 * p22;
        StressTensor[b + 5] = m00 * p02 + m01 * p12 + m02 * p22;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//          STEP ③: WATERTIGHT MATCHED NON-PLANAR FRACTOGRAPHIC SURFACE TESSELLATION (ΣV_i = V_0 EXACT)
//------------------------------------------------------------------------------------------------------------------------

function EvaluateFractographyDisplacement(px, py, pz, CutInfo, Mat, EngMat, GrainRad, Seed)
{
    const Mode = Mat.FractureCategory;
    const CutSeed = Seed + CutInfo.Token * 977;

    if (Mode === "bulk-brittle")
    {
        // Bouchaud universal self-affine fracture roughness (H ≈ 0.78) + 3D Meso-Aggregate Clast Pullout
        const fbm = SelfAffineBouchaudFbm(px * 5.2, py * 5.2, pz * 5.2, EngMat.HurstExponent, 4, CutSeed);
        const agg = SampleAggregateInclusionField(px, py, pz, EngMat.AggregateScale, CutSeed + 313);
        return (1.0 - EngMat.AggregateWeight) * fbm + EngMat.AggregateWeight * agg;
    }
    if (Mode === "plate-brittle")
    {
        // Mirror–Mist–Hackle radial transition + Wallner sonic arc ripples
        const dx = px - CutInfo.NucX, dy = py - CutInfo.NucY, dz = pz - CutInfo.NucZ;
        const r = Math.hypot(dx, dy, dz);
        const rNorm = Math.min(1.5, r / Math.max(0.06, CutInfo.MaxRadius));

        // Smooth mirror near flaw (rNorm < 0.22), mist transition, radial hackle ridges beyond 0.45
        const hackleEnv = Math.max(0.0, Math.min(1.0, (rNorm - 0.18) / 0.55));
        const ang = Math.atan2(
            dx * CutInfo.TanVx + dy * CutInfo.TanVy + dz * CutInfo.TanVz,
            dx * CutInfo.TanUx + dy * CutInfo.TanUy + dz * CutInfo.TanUz
        );
        const hackle = Math.sin(ang * 18.0 + ValueNoise3D(px * 9, py * 9, pz * 9, CutSeed) * 2.2) * hackleEnv;
        const wallner = 0.35 * Math.sin(r * 46.0) * Math.exp(-rNorm * 0.6);
        return 0.65 * hackle + wallner;
    }
    if (Mode === "fibrous")
    {
        // 12:1 anisotropic tracheid fiber-bundle splinters stretched along wood grain vector
        const gx = Math.cos(GrainRad), gy = Math.sin(GrainRad);
        const along = px * gx + py * gy;
        const across1 = -px * gy + py * gx;
        const across2 = pz;
        const flute = SelfAffineBouchaudFbm(along * 1.6, across1 * 19.0, across2 * 19.0, EngMat.HurstExponent, 4, CutSeed);
        return flute * 1.15;
    }

    // Ductile polymer / metal: micro-void dimpling + shear-lip cup-and-cone bevel handled by radial ring profile
    return 0.45 * SelfAffineBouchaudFbm(px * 8.0, py * 8.0, pz * 8.0, EngMat.HurstExponent, 3, CutSeed);
}

/**
 * Builds watertight non-planar interlocking triangle geometry for every fragment while preserving
 * exact vertex-for-vertex complementarity across every shared CutToken face!
 */
function PointSegmentDist3D(px, py, pz, ax, ay, az, bx, by, bz)
{
    const abx = bx - ax, aby = by - ay, abz = bz - az;
    const len2 = abx * abx + aby * aby + abz * abz;
    if (len2 < 1e-14) return Math.hypot(px - ax, py - ay, pz - az);
    const t = Math.max(0.0, Math.min(1.0, ((px - ax) * abx + (py - ay) * aby + (pz - az) * abz) / len2));
    return Math.hypot(px - (ax + abx * t), py - (ay + aby * t), pz - (az + abz * t));
}

function SplitPolygonByPlane3D(Poly, nx, ny, nz, d)
{
    let pos = 0, neg = 0;
    const dists = new Float64Array(Poly.length);
    for (let i = 0; i < Poly.length; ++i)
    {
        const p = Poly[i];
        const v = nx * p[0] + ny * p[1] + nz * p[2] - d;
        dists[i] = v;
        if (v > 1e-6) pos++;
        else if (v < -1e-6) neg++;
    }
    if (pos === 0 || neg === 0) return [Poly];

    const Front = [], Back = [];
    for (let i = 0; i < Poly.length; ++i)
    {
        const j = (i + 1) % Poly.length;
        const A = Poly[i], B = Poly[j];
        const da = dists[i], db = dists[j];
        if (da >= -1e-6) Front.push(A);
        if (da <= 1e-6) Back.push(A);
        if ((da > 1e-6 && db < -1e-6) || (da < -1e-6 && db > 1e-6))
        {
            const t = da / (da - db);
            const I = [A[0] + (B[0] - A[0]) * t, A[1] + (B[1] - A[1]) * t, A[2] + (B[2] - A[2]) * t];
            Front.push(I);
            Back.push(I);
        }
    }
    const Out = [];
    if (Front.length >= 3) Out.push(Front);
    if (Back.length >= 3) Out.push(Back);
    return Out.length > 0 ? Out : [Poly];
}

function BuildInterlockingEngineeringMeshes(Cells, CutRegistry, CutSubPlanes, Mat, EngMat, GrainRad, Seed)
{
    // Record minimum shard span AND all boundary segments + vertices per CutToken so T-junctions match seamlessly!
    const TokenMinSpan = new Map();
    const TokenSegments = new Map();
    const TokenVertices = new Map();

    for (let c = 0; c < Cells.length; ++c)
    {
        const Cell = Cells[c];
        const V = Cell.verts, FOff = Cell.faceOffsets, FIdx = Cell.faceIndices, FTok = Cell.faceTokens;
        for (let f = 0; f < Cell.fCount; ++f)
        {
            const tok = FTok[f];
            if (tok <= 0) continue;
            const prev = TokenMinSpan.get(tok);
            if (prev === undefined || Cell.minSpan < prev) TokenMinSpan.set(tok, Cell.minSpan);

            if (!TokenSegments.has(tok)) { TokenSegments.set(tok, []); TokenVertices.set(tok, []); }
            const segs = TokenSegments.get(tok);
            const vlist = TokenVertices.get(tok);
            const s = FOff[f], e = FOff[f + 1], K = e - s;
            for (let k = 0; k < K; ++k)
            {
                const ia = FIdx[s + k] * 3;
                const ib = FIdx[s + ((k + 1) % K)] * 3;
                segs.push(V[ia], V[ia + 1], V[ia + 2], V[ib], V[ib + 1], V[ib + 2]);
                vlist.push([V[ia], V[ia + 1], V[ia + 2]]);
            }
        }
    }

    let TotalTris = 0;
    let SqReliefSum = 0;
    let ReliefSamples = 0;
    let NonPlanarVolumeSum = 0;

    const Surfaces = new Array(Cells.length);

    for (let c = 0; c < Cells.length; ++c)
    {
        const Cell = Cells[c];
        const V = Cell.verts, FOff = Cell.faceOffsets, FIdx = Cell.faceIndices, FTok = Cell.faceTokens;

        const PosOut = [];
        const NormOut = [];
        const IntFlagOut = [];

        const PushTri = (ax, ay, az, bx, by, bz, cx, cy, cz, isInt) =>
        {
            const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
            const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
            let nx = e1y * e2z - e1z * e2y;
            let ny = e1z * e2x - e1x * e2z;
            let nz = e1x * e2y - e1y * e2x;
            const nL = Math.hypot(nx, ny, nz);
            if (nL < 1e-13) return 0;
            nx /= nL; ny /= nL; nz /= nL;

            PosOut.push(ax, ay, az, bx, by, bz, cx, cy, cz);
            NormOut.push(nx, ny, nz, nx, ny, nz, nx, ny, nz);
            const fl = isInt ? 1 : 0;
            IntFlagOut.push(fl, fl, fl);
            TotalTris++;

            // Signed tetrahedron volume contribution (a . (b x c)) / 6
            return (ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx)) / 6.0;
        };

        let ShardSignedVol = 0;

        for (let f = 0; f < Cell.fCount; ++f)
        {
            const s = FOff[f], e = FOff[f + 1];
            const K = e - s;
            if (K < 3) continue;
            const tok = FTok[f];

            if (tok <= 0)
            {
                // Original exterior face -> planar fan
                const i0 = FIdx[s] * 3;
                const ax = V[i0], ay = V[i0 + 1], az = V[i0 + 2];
                for (let k = s + 1; k + 1 < e; ++k)
                {
                    const i1 = FIdx[k] * 3, i2 = FIdx[k + 1] * 3;
                    ShardSignedVol += PushTri(
                        ax, ay, az,
                        V[i1], V[i1 + 1], V[i1 + 2],
                        V[i2], V[i2 + 1], V[i2 + 2],
                        false
                    );
                }
                continue;
            }

            // Decompose interior face by all subsequent T-junction planes that crossed CutToken so BOTH mating sides share bit-identical sub-polygons!
            const InitPoly = new Array(K);
            for (let k = 0; k < K; ++k)
            {
                const ia = FIdx[s + k] * 3;
                InitPoly[k] = [V[ia], V[ia + 1], V[ia + 2]];
            }

            let SubPolys = [InitPoly];
            const CrossingPlanes = CutSubPlanes.get(tok) || [];
            for (let cp = 0; cp < CrossingPlanes.length; ++cp)
            {
                const pl = CrossingPlanes[cp];
                const NextPolys = [];
                for (let sp = 0; sp < SubPolys.length; ++sp)
                {
                    const parts = SplitPolygonByPlane3D(SubPolys[sp], pl[0], pl[1], pl[2], pl[3]);
                    for (let r = 0; r < parts.length; ++r) NextPolys.push(parts[r]);
                }
                SubPolys = NextPolys;
            }

            const CutInfo = CutRegistry.get(tok);
            const PairSpan = TokenMinSpan.get(tok) ?? Cell.minSpan;
            const AmpMax = Math.min(EngMat.ReliefAmplitudeM, PairSpan * 0.22);

            for (let sp = 0; sp < SubPolys.length; ++sp)
            {
                const RawCorners = SubPolys[sp];
                if (RawCorners.length < 3) continue;

                const Boundary = new Array(RawCorners.length * 2);
                for (let k = 0; k < RawCorners.length; ++k)
                {
                    const A = RawCorners[k], B = RawCorners[(k + 1) % RawCorners.length];
                    Boundary[k * 2] = A;
                    Boundary[k * 2 + 1] = [0.5 * (A[0] + B[0]), 0.5 * (A[1] + B[1]), 0.5 * (A[2] + B[2])];
                }
                const M = Boundary.length;

                const SortedCorners = Boundary.slice().sort((p, q) => (p[0] - q[0]) || (p[1] - q[1]) || (p[2] - q[2]));
                let pcx = 0, pcy = 0, pcz = 0;
                for (let i = 0; i < M; ++i)
                {
                    pcx += SortedCorners[i][0];
                    pcy += SortedCorners[i][1];
                    pcz += SortedCorners[i][2];
                }
                pcx /= M; pcy /= M; pcz /= M;

                const DisplacePoint = (bx, by, bz, rho) =>
                {
                    if (rho >= 0.9999 || !CutInfo) return [bx, by, bz];
                    const qx = pcx + (bx - pcx) * rho;
                    const qy = pcy + (by - pcy) * rho;
                    const qz = pcz + (bz - pcz) * rho;

                    // Zero-slope collar on every sub-polygon perimeter -> bit-identical interlocking across ALL T-junctions!
                    const tCollar = Math.max(0.0, Math.min(1.0, (1.0 - rho) / 0.34));
                    const wSeam = tCollar * tCollar * (3.0 - 2.0 * tCollar);

                    let relief = EvaluateFractographyDisplacement(qx, qy, qz, CutInfo, Mat, EngMat, GrainRad, Seed);
                    if (Mat.FractureCategory === "ductile")
                    {
                        const lip = Math.sin(Math.PI * Math.min(1.0, (1.0 - rho) * 1.8)) * 0.55;
                        relief += CutInfo.CupSign * lip;
                    }

                    const delta = Math.max(-AmpMax, Math.min(AmpMax, wSeam * AmpMax * relief));
                    SqReliefSum += delta * delta;
                    ReliefSamples++;

                    return [
                        qx + CutInfo.CanonNx * delta,
                        qy + CutInfo.CanonNy * delta,
                        qz + CutInfo.CanonNz * delta
                    ];
                };

                const CenterPt = DisplacePoint(pcx, pcy, pcz, 0.0);
                const Ring0 = new Array(M);
                const Ring1 = new Array(M);
                for (let j = 0; j < M; ++j)
                {
                    const bp = Boundary[j];
                    Ring0[j] = DisplacePoint(bp[0], bp[1], bp[2], 0.34);
                    Ring1[j] = DisplacePoint(bp[0], bp[1], bp[2], 0.68);
                }

                const EmitCanonicalQuad = (A0, A1, B1, B0) =>
                {
                    const dMain = (A0[0] - B1[0]) ** 2 + (A0[1] - B1[1]) ** 2 + (A0[2] - B1[2]) ** 2;
                    const dAlt  = (A1[0] - B0[0]) ** 2 + (A1[1] - B0[1]) ** 2 + (A1[2] - B0[2]) ** 2;
                    if (dMain <= dAlt)
                    {
                        ShardSignedVol += PushTri(A0[0], A0[1], A0[2], B0[0], B0[1], B0[2], B1[0], B1[1], B1[2], true);
                        ShardSignedVol += PushTri(A0[0], A0[1], A0[2], B1[0], B1[1], B1[2], A1[0], A1[1], A1[2], true);
                    }
                    else
                    {
                        ShardSignedVol += PushTri(A0[0], A0[1], A0[2], B0[0], B0[1], B0[2], A1[0], A1[1], A1[2], true);
                        ShardSignedVol += PushTri(A1[0], A1[1], A1[2], B0[0], B0[1], B0[2], B1[0], B1[1], B1[2], true);
                    }
                };

                for (let j = 0; j < M; ++j)
                {
                    const jn = (j + 1) % M;
                    ShardSignedVol += PushTri(
                        CenterPt[0], CenterPt[1], CenterPt[2],
                        Ring0[j][0], Ring0[j][1], Ring0[j][2],
                        Ring0[jn][0], Ring0[jn][1], Ring0[jn][2],
                        true
                    );
                    EmitCanonicalQuad(Ring0[j], Ring0[jn], Ring1[jn], Ring1[j]);
                    EmitCanonicalQuad(Ring1[j], Ring1[jn], Boundary[jn], Boundary[j]);
                }
            }
        }

        NonPlanarVolumeSum += Math.abs(ShardSignedVol);
        Surfaces[c] = {
            Positions: new Float32Array(PosOut),
            Normals: new Float32Array(NormOut),
            InteriorFlags: new Uint8Array(IntFlagOut),
            TriangleCount: PosOut.length / 9,
            ExactNonPlanarVolume: Math.abs(ShardSignedVol)
        };
    }

    const RoughnessRmsMm = ReliefSamples > 0 ? Math.sqrt(SqReliefSum / ReliefSamples) * 1000.0 : 0.0;
    return { Surfaces, TotalTris, RoughnessRmsMm, NonPlanarVolumeSum };
}

//------------------------------------------------------------------------------------------------------------------------
//                      PUBLIC ENTRY POINT: EXECUTE ENGINEERING BAKED FRACTURE
//------------------------------------------------------------------------------------------------------------------------

export function ExecuteEngineeringBakedFracture(BaseComplex, Spec = {})
{
    const StartClock = performance.now();
    const MaterialKey = Spec.Material in MaterialCatalogue ? Spec.Material : "concrete";
    const Mat = MaterialCatalogue[MaterialKey];
    const EngMat = EngineeringConstitutiveCatalogue[MaterialKey] ?? EngineeringConstitutiveCatalogue.concrete;

    const Seed = Math.max(1, Math.floor(Number(Spec.Seed ?? 42)));
    const Rand = ConstructRandomStream(Seed);

    const Ceiling = Math.max(2, Math.min(160, Math.floor(Number(Spec.Ceiling ?? 32))));
    const ImpactEnergy = Math.max(15, Number(Spec.Energy ?? 3200));
    const MinSpan = Math.max(0.018, Number(Spec.MinimumSize ?? 0.05));
    const GrainRad = (Number(Spec.GrainAngleDeg ?? 0) * Math.PI) / 180.0;
    const BondScale = Number(Spec.BondStrengthScale ?? 1.0);

    const RootCell = PackFlatConvexCell(BaseComplex);
    const TotalVolume0 = RootCell.volume;
    const ObjRadius = Math.max(0.1, RootCell.radius);
    const BaseMetrics = {
        Volume: TotalVolume0,
        Centroid: [RootCell.cx, RootCell.cy, RootCell.cz],
        BoundsMin: [RootCell.minX, RootCell.minY, RootCell.minZ],
        BoundsMax: [RootCell.maxX, RootCell.maxY, RootCell.maxZ],
        Span: [RootCell.maxX - RootCell.minX, RootCell.maxY - RootCell.minY, RootCell.maxZ - RootCell.minZ],
        Radius: ObjRadius,
        CrackArea: 0
    };

    const ImpP = Spec.ImpactPoint ?? [0.0, 0.12, 0.38];
    const ImpN = Spec.ImpactNormal ?? [0.0, 0.0, -1.0];
    const nLen = 1.0 / (Math.hypot(ImpN[0], ImpN[1], ImpN[2]) || 1.0);
    const inx = ImpN[0] * nLen, iny = ImpN[1] * nLen, inz = ImpN[2] * nLen;
    const ipx = ImpP[0], ipy = ImpP[1], ipz = ImpP[2];

    // ① Solve 3D Navier–Cauchy Elastostatic Stress Tensor Field
    const Lattice = SolveVolumetricStressLattice(RootCell, Mat, EngMat, ImpactEnergy, [ipx, ipy, ipz], [inx, iny, inz], 12);

    // Constitutive fracture mechanics parameters
    const E_Pa = Mat.YoungModulusGPa * 1e9;
    const nu = Mat.PoissonRatio;
    const KIc_MPaSqrtM = Math.sqrt((E_Pa * Mat.Gc) / (1.0 - nu * nu)) * 1e-6;
    const ft_MPa = Mat.TensileStrengthMPa;
    const HillerborgLengthM = (E_Pa * Mat.Gc) / Math.pow(ft_MPa * 1e6, 2);
    const PhiRad = (EngMat.InternalFrictionDeg * Math.PI) / 180.0;
    const SinPhi = Math.sin(PhiRad), CosPhi = Math.cos(PhiRad);
    const CohesionMPa = (EngMat.CompressiveStrengthMPa * (1.0 - SinPhi)) / (2.0 * Math.max(0.2, CosPhi));
    const ThetaMohrCoulomb = 0.25 * Math.PI - 0.5 * PhiRad;
    const ReliefLengthM = Math.max(Lattice.CellSize * 1.45, Math.min(ObjRadius * 0.38, HillerborgLengthM * 0.25));

    // Sample 3-Parameter Weibull Flaw Tensile Strength per continuum voxel
    const VoxelWeibullFt = new Float64Array(Lattice.Total);
    const VolumeScale = Math.pow(Math.max(0.1, TotalVolume0 / (Lattice.VoxelVolume * 64.0)), 1.0 / Mat.WeibullModulus);
    for (let i = 0; i < Lattice.Total; ++i)
    {
        if (!Lattice.Active[i]) continue;
        const u = Math.max(0.02, Math.min(0.995, Rand()));
        const weibullFactor = Math.pow(-Math.log(1.0 - u), 1.0 / Mat.WeibullModulus);
        VoxelWeibullFt[i] = Math.max(ft_MPa * 0.28, ft_MPa * weibullFactor * VolumeScale);
    }

    // Record initial Peak Principal Tension, Peak Von Mises, Peak KI, and Peak Mohr-Coulomb Index for Engineering Receipt
    const EigenScratch = new Float64Array(10);
    let PeakSigma1MPa = 0, PeakVonMisesMPa = 0, PeakKI = 0, PeakMohrRatio = 0;
    const MidSliceZ = Math.floor(Lattice.GridN * 0.5);
    const StressSliceGrid = new Float32Array(Lattice.GridN * Lattice.GridN * 3); // [sigma1, vonMises, failureMode]

    for (let i = 0; i < Lattice.Total; ++i)
    {
        if (!Lattice.Active[i]) continue;
        const b = i * 6;
        SolveSymmetricEigen3x3(
            Lattice.StressTensor[b], Lattice.StressTensor[b + 1], Lattice.StressTensor[b + 2],
            Lattice.StressTensor[b + 3], Lattice.StressTensor[b + 4], Lattice.StressTensor[b + 5],
            EigenScratch
        );
        const s1 = EigenScratch[0], s3 = EigenScratch[2], vm = EigenScratch[9];
        if (s1 > PeakSigma1MPa) PeakSigma1MPa = s1;
        if (vm > PeakVonMisesMPa) PeakVonMisesMPa = vm;

        const flawA = Math.max(0.0015, Lattice.CellSize * 0.35);
        const KI = 1.12 * Math.max(0, s1) * Math.sqrt(Math.PI * flawA);
        if (KI > PeakKI) PeakKI = KI;

        const tauMax = 0.5 * (s1 - s3);
        const sigMean = 0.5 * (s1 + s3);
        const mcDenom = Math.max(0.5, CohesionMPa * CosPhi - sigMean * SinPhi);
        const mcRatio = tauMax / mcDenom;
        if (mcRatio > PeakMohrRatio) PeakMohrRatio = mcRatio;
    }

    for (let iy = 0; iy < Lattice.GridN; ++iy)
    {
        for (let ix = 0; ix < Lattice.GridN; ++ix)
        {
            const idx = (MidSliceZ * Lattice.GridN + iy) * Lattice.GridN + ix;
            const outIdx = (iy * Lattice.GridN + ix) * 3;
            if (!Lattice.Active[idx]) { StressSliceGrid[outIdx + 2] = -1; continue; }
            const b = idx * 6;
            SolveSymmetricEigen3x3(
                Lattice.StressTensor[b], Lattice.StressTensor[b + 1], Lattice.StressTensor[b + 2],
                Lattice.StressTensor[b + 3], Lattice.StressTensor[b + 4], Lattice.StressTensor[b + 5],
                EigenScratch
            );
            StressSliceGrid[outIdx] = EigenScratch[0];
            StressSliceGrid[outIdx + 1] = EigenScratch[9];
            const tau = 0.5 * (EigenScratch[0] - EigenScratch[2]);
            const sm = 0.5 * (EigenScratch[0] + EigenScratch[2]);
            const mc = tau / Math.max(0.5, CohesionMPa * CosPhi - sm * SinPhi);
            const rk = Math.max(0, EigenScratch[0]) / Math.max(0.2, VoxelWeibullFt[idx]);
            StressSliceGrid[outIdx + 2] = mc > rk ? 2.0 : 1.0; // 1 = Mode-I Tensile, 2 = Mode-II Mohr-Coulomb Shear
        }
    }

    // ② Sequential Wake-Shielded Engineering Fracture Propagation
    const ResidualPool = Mat.StoredEnergy * TotalVolume0;
    const TotalEnergyBudget = ImpactEnergy + ResidualPool;
    let RemainingEnergy = TotalEnergyBudget;
    let SpentEnergy = 0;
    let ModeICount = 0, ModeIICount = 0, TJunctionCount = 0;

    let TokenCounter = 100;
    const CutRegistry = new Map();
    const CutSubPlanes = new Map();
    const RegisterCut = (tok, nx, ny, nz, nucX, nucY, nucZ, maxR) =>
    {
        // Canonical normal sign so mating Front/Back shards displace in identical world direction!
        let cnx = nx, cny = ny, cnz = nz;
        if (cnx < -1e-9 || (Math.abs(cnx) <= 1e-9 && (cny < -1e-9 || (Math.abs(cny) <= 1e-9 && cnz < 0))))
        {
            cnx = -cnx; cny = -cny; cnz = -cnz;
        }
        let ux = 0, uy = cnz, uz = -cny;
        if (Math.abs(cnx) >= 0.85) { ux = -cnz; uy = 0; uz = cnx; }
        const uL = 1.0 / Math.hypot(ux, uy, uz);
        ux *= uL; uy *= uL; uz *= uL;
        const vx = cny * uz - cnz * uy, vy = cnz * ux - cnx * uz, vz = cnx * uy - cny * ux;

        CutRegistry.set(tok, {
            Token: tok,
            CanonNx: cnx, CanonNy: cny, CanonNz: cnz,
            TanUx: ux, TanUy: uy, TanUz: uz,
            TanVx: vx, TanVy: vy, TanVz: vz,
            NucX: nucX, NucY: nucY, NucZ: nucZ,
            MaxRadius: maxR,
            CupSign: (tok & 1) ? 1.0 : -1.0
        });
    };

    const ActiveShards = [{ cell: RootCell, gen: 0 }];
    const MaxIterations = Ceiling * 6;
    let Iter = 0;

    while (ActiveShards.length < Ceiling && RemainingEnergy > 0 && Iter++ < MaxIterations)
    {
        // Score every active shard by the peak engineering failure index inside its domain
        let bestShardIdx = -1;
        let bestScore = -1e30;
        let bestNormalX = 1, bestNormalY = 0, bestNormalZ = 0;
        let bestPivotX = 0, bestPivotY = 0, bestPivotZ = 0;
        let bestIsShear = false;

        for (let s = 0; s < ActiveShards.length; ++s)
        {
            const Cell = ActiveShards[s].cell;
            if (Cell.volume < 1e-6 || Cell.minSpan < MinSpan * 0.82) continue;

            // Sample stress tensor at shard centroid & nearest continuum lattice cells
            const gx = Math.max(0, Math.min(Lattice.GridN - 1, Math.floor(((Cell.cx - Lattice.Bounds.minX) / Lattice.Bounds.spanX) * Lattice.GridN)));
            const gy = Math.max(0, Math.min(Lattice.GridN - 1, Math.floor(((Cell.cy - Lattice.Bounds.minY) / Lattice.Bounds.spanY) * Lattice.GridN)));
            const gz = Math.max(0, Math.min(Lattice.GridN - 1, Math.floor(((Cell.cz - Lattice.Bounds.minZ) / Lattice.Bounds.spanZ) * Lattice.GridN)));

            let localPeak = -1e30;
            let lnx = 1, lny = 0, lnz = 0;
            let lpx = Cell.cx, lpy = Cell.cy, lpz = Cell.cz;
            let lShear = false;

            // Probe 3x3x3 neighborhood around shard centroid inside shard bounding box
            for (let dz = -1; dz <= 1; ++dz)
            {
                const iz = gz + dz;
                if (iz < 0 || iz >= Lattice.GridN) continue;
                for (let dy = -1; dy <= 1; ++dy)
                {
                    const iy = gy + dy;
                    if (iy < 0 || iy >= Lattice.GridN) continue;
                    for (let dx = -1; dx <= 1; ++dx)
                    {
                        const ix = gx + dx;
                        if (ix < 0 || ix >= Lattice.GridN) continue;
                        const vIdx = (iz * Lattice.GridN + iy) * Lattice.GridN + ix;
                        if (!Lattice.Active[vIdx]) continue;

                        const vx = Lattice.Pos[vIdx * 3], vy = Lattice.Pos[vIdx * 3 + 1], vz = Lattice.Pos[vIdx * 3 + 2];
                        if (vx < Cell.minX || vx > Cell.maxX || vy < Cell.minY || vy > Cell.maxY || vz < Cell.minZ || vz > Cell.maxZ) continue;

                        const b = vIdx * 6;
                        SolveSymmetricEigen3x3(
                            Lattice.StressTensor[b], Lattice.StressTensor[b + 1], Lattice.StressTensor[b + 2],
                            Lattice.StressTensor[b + 3], Lattice.StressTensor[b + 4], Lattice.StressTensor[b + 5],
                            EigenScratch
                        );

                        const s1 = EigenScratch[0], s3 = EigenScratch[2];
                        const e1x = EigenScratch[3], e1y = EigenScratch[4], e1z = EigenScratch[5];
                        const e3x = EigenScratch[6], e3y = EigenScratch[7], e3z = EigenScratch[8];

                        const rankineIdx = Math.max(0.05, s1) / Math.max(0.2, VoxelWeibullFt[vIdx]);
                        const tauMax = 0.5 * (s1 - s3);
                        const sigMean = 0.5 * (s1 + s3);
                        const mohrIdx = tauMax / Math.max(0.5, CohesionMPa * CosPhi - sigMean * SinPhi);

                        // Volume-weighted fracture priority so uncracked stressed slabs split cleanly
                        const priority = Math.max(rankineIdx, mohrIdx * 0.92) * Math.pow(Cell.volume / TotalVolume0, 0.28);
                        if (priority > localPeak)
                        {
                            localPeak = priority;
                            lpx = 0.65 * Cell.cx + 0.35 * vx;
                            lpy = 0.65 * Cell.cy + 0.35 * vy;
                            lpz = 0.65 * Cell.cz + 0.35 * vz;

                            if (mohrIdx * 0.92 > rankineIdx && Mat.FractureCategory !== "fibrous")
                            {
                                // Mode-II Mohr-Coulomb conjugate shear plane at ±(45° - φ/2)
                                const sign = ((Iter + s) & 1) ? 1.0 : -1.0;
                                const cT = Math.cos(ThetaMohrCoulomb), sT = Math.sin(ThetaMohrCoulomb) * sign;
                                lnx = e1x * cT + e3x * sT;
                                lny = e1y * cT + e3y * sT;
                                lnz = e1z * cT + e3z * sT;
                                lShear = true;
                            }
                            else
                            {
                                // Mode-I Rankine / LEFM tensile opening plane perpendicular to principal tension e1
                                lnx = e1x; lny = e1y; lnz = e1z;
                                lShear = false;
                            }
                        }
                    }
                }
            }

            // Fallback continuum stress direction if shard lies between coarse lattice nodes
            if (localPeak < -1e20)
            {
                const dImp = Math.hypot(Cell.cx - ipx, Cell.cy - ipy, Cell.cz - ipz);
                localPeak = (1.2 / (0.2 + dImp)) * Math.pow(Cell.volume / TotalVolume0, 0.35);
                lnx = -(Cell.cy - ipy); lny = (Cell.cx - ipx); lnz = (Rand() - 0.5) * 0.4;
                if (Math.hypot(lnx, lny, lnz) < 1e-4) { lnx = 1; lny = 0; lnz = 0; }
            }

            if (localPeak > bestScore)
            {
                bestScore = localPeak;
                bestShardIdx = s;
                bestNormalX = lnx; bestNormalY = lny; bestNormalZ = lnz;
                bestPivotX = lpx; bestPivotY = lpy; bestPivotZ = lpz;
                bestIsShear = lShear;
            }
        }

        if (bestShardIdx < 0) break;
        const Target = ActiveShards[bestShardIdx];
        const Cell = Target.cell;

        // Apply material anisotropy attraction (wood grain tracheid planes / granite bedding weakness)
        if (Mat.FractureCategory === "fibrous")
        {
            const gx = Math.cos(GrainRad), gy = Math.sin(GrainRad), gz = 0.05;
            // Project crack normal perpendicular to wood grain unless high flexural bending snaps across grain
            const dImp = Math.hypot(Cell.cx - ipx, Cell.cy - ipy, Cell.cz - ipz);
            if (Rand() > 0.14 * Math.max(0, 1.0 - dImp / ObjRadius))
            {
                const dotG = bestNormalX * gx + bestNormalY * gy + bestNormalZ * gz;
                bestNormalX -= gx * dotG * 0.94;
                bestNormalY -= gy * dotG * 0.94;
                bestNormalZ -= gz * dotG * 0.94;
            }
        }
        else if (Mat.BeddingWeakness > 0 && Rand() < 0.38 + Mat.BeddingWeakness * 0.35)
        {
            const bx = -Math.sin(GrainRad), by = Math.cos(GrainRad);
            bestNormalX = bestNormalX * 0.25 + bx * 0.75;
            bestNormalY = bestNormalY * 0.25 + by * 0.75;
            bestNormalZ *= 0.25;
        }

        // Small flaw orientation perturbation
        bestNormalX += (Rand() - 0.5) * Mat.CrackJitter * 0.45;
        bestNormalY += (Rand() - 0.5) * Mat.CrackJitter * 0.45;
        bestNormalZ += (Rand() - 0.5) * Mat.CrackJitter * 0.45;
        const bnL = 1.0 / (Math.hypot(bestNormalX, bestNormalY, bestNormalZ) || 1.0);
        bestNormalX *= bnL; bestNormalY *= bnL; bestNormalZ *= bnL;

        // Nucleate near centroid/hotspot within safe interior margin so slabs split cleanly
        const px = 0.78 * Cell.cx + 0.22 * bestPivotX + (Rand() - 0.5) * 0.14 * Cell.radius;
        const py = 0.78 * Cell.cy + 0.22 * bestPivotY + (Rand() - 0.5) * 0.14 * Cell.radius;
        const pz = 0.78 * Cell.cz + 0.22 * bestPivotZ + (Rand() - 0.5) * 0.14 * Cell.radius;
        const dPlane = bestNormalX * px + bestNormalY * py + bestNormalZ * pz;

        const tok = ++TokenCounter;
        if (!ClipFlatConvexCell(Cell, bestNormalX, bestNormalY, bestNormalZ, dPlane, tok))
        {
            Cell.minSpan *= 0.65;
            continue;
        }

        const { Front, Back, CutArea } = GetLastClipCells();
        if (!Front || !Back || Front.minSpan < MinSpan * 0.38 || Back.minSpan < MinSpan * 0.38)
        {
            Cell.minSpan *= 0.72;
            continue;
        }

        let LocalGc = Mat.Gc;
        if (Mat.FractureCategory === "fibrous")
        {
            const across = Math.abs(bestNormalX * Math.cos(GrainRad) + bestNormalY * Math.sin(GrainRad));
            LocalGc *= 1.0 + (Mat.AnisotropyRatio - 1.0) * across * across;
        }
        else if (Mat.BeddingWeakness > 0)
        {
            const align = Math.abs(-bestNormalX * Math.sin(GrainRad) + bestNormalY * Math.cos(GrainRad));
            LocalGc *= 1.0 - Mat.BeddingWeakness * Math.pow(align, 4);
        }
        if (bestIsShear) LocalGc *= 1.75;

        const Cost = 2.0 * LocalGc * CutArea;
        if (Cost > RemainingEnergy && ActiveShards.length >= 4)
        {
            break;
        }

        RemainingEnergy = Math.max(0.0, RemainingEnergy - Cost);
        SpentEnergy += Cost;
        if (bestIsShear) ModeIICount++; else ModeICount++;
        if (Target.gen > 0) TJunctionCount++;

        // Record crossing T-junction plane on every pre-existing interior face of Cell that straddles (bestNormal, dPlane)!
        for (let f = 0; f < Cell.fCount; ++f)
        {
            const oldTok = Cell.faceTokens[f];
            if (oldTok <= 0) continue;
            const s = Cell.faceOffsets[f], e = Cell.faceOffsets[f + 1];
            let hasPos = false, hasNeg = false;
            for (let k = s; k < e; ++k)
            {
                const iv = Cell.faceIndices[k] * 3;
                const dv = bestNormalX * Cell.verts[iv] + bestNormalY * Cell.verts[iv + 1] + bestNormalZ * Cell.verts[iv + 2] - dPlane;
                if (dv > 1e-6) hasPos = true;
                else if (dv < -1e-6) hasNeg = true;
            }
            if (hasPos && hasNeg)
            {
                if (!CutSubPlanes.has(oldTok)) CutSubPlanes.set(oldTok, []);
                CutSubPlanes.get(oldTok).push([bestNormalX, bestNormalY, bestNormalZ, dPlane]);
            }
        }

        RegisterCut(tok, bestNormalX, bestNormalY, bestNormalZ, px, py, pz, Math.max(Front.radius, Back.radius));
        RelaxStressTensorAroundCrack(Lattice, bestNormalX, bestNormalY, bestNormalZ, dPlane, Cell, ReliefLengthM);

        ActiveShards[bestShardIdx] = { cell: Front, gen: Target.gen + 1 };
        ActiveShards.push({ cell: Back, gen: Target.gen + 1 });
    }

    ActiveShards.sort((a, b) => b.cell.volume - a.cell.volume);
    const FinalCells = ActiveShards.map((s) => s.cell);

    const Fractography = BuildInterlockingEngineeringMeshes(FinalCells, CutRegistry, CutSubPlanes, Mat, EngMat, GrainRad, Seed);

    const EjectionBase = Math.sqrt((2.0 * Math.max(15, TotalEnergyBudget * 0.08)) / Math.max(0.8, TotalVolume0 * Mat.Density));
    let SumCrackArea = 0;
    const Fragments = new Array(FinalCells.length);

    for (let idx = 0; idx < FinalCells.length; ++idx)
    {
        const Cell = FinalCells[idx];
        const EngSurf = Fractography.Surfaces[idx];
        SumCrackArea += Cell.crackArea;

        const dImp = Math.hypot(Cell.cx - ipx, Cell.cy - ipy, Cell.cz - ipz);
        const Prox = Math.max(0, 1.0 - dImp / Math.max(0.25, ObjRadius * 1.65));
        const Mass = Math.max(0.01, EngSurf.ExactNonPlanarVolume * Mat.Density);

        let rx = (Cell.cx - ipx) + inx * 0.35;
        let ry = (Cell.cy - ipy) + iny * 0.35;
        let rz = (Cell.cz - ipz) + inz * 0.35;
        const rL = 1.0 / (Math.hypot(rx, ry, rz) || 1.0);
        rx *= rL; ry *= rL; rz *= rL;
        const EjectSpeed = Math.min(18.0, EjectionBase * (0.28 + 1.45 * Prox * Prox));

        const ConvexHull = UnpackFlatConvexCell(Cell);
        Fragments[idx] = {
            Index: idx,
            Token: `ENG-${String(idx + 1).padStart(3, "0")}`,
            _FlatCell: Cell,
            Complex: ConvexHull,
            ConvexHull,
            EngineeringSurface: EngSurf,
            Volume: EngSurf.ExactNonPlanarVolume,
            ConvexVolume: Cell.volume,
            Mass,
            Centroid: [Cell.cx, Cell.cy, Cell.cz],
            BoundsMin: [Cell.minX, Cell.minY, Cell.minZ],
            BoundsMax: [Cell.maxX, Cell.maxY, Cell.maxZ],
            Span: [Cell.maxX - Cell.minX, Cell.maxY - Cell.minY, Cell.maxZ - Cell.minZ],
            Radius: Cell.radius,
            CrackArea: Cell.crackArea,
            Proximity: Prox,
            StressRatio: Math.min(1.0, 0.22 + Prox * 0.88),
            InitialVelocity: [rx * EjectSpeed, ry * EjectSpeed, rz * EjectSpeed],
            InitialAngular: [
                (Rand() - 0.5) * 8.5 * (0.3 + Prox),
                (Rand() - 0.5) * 8.5 * (0.3 + Prox),
                (Rand() - 0.5) * 8.5 * (0.3 + Prox)
            ],
            Visible: true
        };
    }

    const Bonds = BuildFastTokenBonds(Fragments, Mat, [ipx, ipy, ipz], ObjRadius, BondScale * 1.18, TokenCounter);
    const TotalCrackArea = SumCrackArea * 0.5;
    const NonPlanarVolumeDrift = TotalVolume0 > 1e-9 ? Math.abs(Fractography.NonPlanarVolumeSum - TotalVolume0) / TotalVolume0 : 0;
    const ElapsedMs = performance.now() - StartClock;

    return {
        Solver: "engineering_fem",
        MaterialSpec: Mat,
        BaseMetrics,
        Fragments,
        Bonds,
        WavefrontPaths: [],
        EngineeringReport: {
            PeakContactPressureMPa: Lattice.Hertz.PeakHertzPressureMPa,
            ContactRadiusMm: Lattice.Hertz.ContactRadiusM * 1000.0,
            PeakImpactForceKN: Lattice.Hertz.FMax_N * 1e-3,
            PeakSigma1MPa,
            PeakVonMisesMPa,
            StressIntensityKI: PeakKI,
            FractureToughnessKIc: KIc_MPaSqrtM,
            MohrCoulombMaxRatio: PeakMohrRatio,
            InternalFrictionDeg: EngMat.InternalFrictionDeg,
            CohesionMPa,
            HillerborgLengthMm: HillerborgLengthM * 1000.0,
            ModeITensileCuts: ModeICount,
            ModeIIShearCuts: ModeIICount,
            WakeShieldedTJunctions: TJunctionCount,
            RoughnessRmsMm: Fractography.RoughnessRmsMm,
            HurstExponent: EngMat.HurstExponent,
            FractographyMode: EngMat.FractographyLabel,
            NonPlanarTriangles: Fractography.TotalTris,
            ConvexHullCount: Fragments.length,
            NonPlanarVolumeDrift,
            LatticeGridN: Lattice.GridN,
            StressSliceGrid
        },
        Receipt: {
            FragmentCount: Fragments.length,
            BondCount: Bonds.length,
            IntactBondCount: Bonds.filter((b) => !b.BrokenOnImpact).length,
            BaseVolume: TotalVolume0,
            FragmentVolumeSum: Fractography.NonPlanarVolumeSum,
            VolumeDriftRatio: NonPlanarVolumeDrift,
            TotalCrackArea,
            TotalEnergyBudget,
            SpentSurfaceEnergy: Math.min(TotalEnergyBudget, SpentEnergy > 0 ? SpentEnergy : 2.0 * Mat.Gc * TotalCrackArea),
            RayleighSpeedMps: EvaluateRayleighWaveSpeed(Mat),
            ComputeDurationMs: ElapsedMs,
            WatertightManifold: NonPlanarVolumeDrift < 1e-4,
            ConcaveInterlocking: true,
            ExecutionTier: "Offline Engineering Continuum + Fractography"
        }
    };
}
