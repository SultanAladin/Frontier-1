/*============================================================================================================================================
                                                      ENGINEERINGBAKEDFRACTURE.CPP
============================================================================================================================================*/
// 🧩 Offline engineering fracture solver: Navier-Cauchy lattice, Rankine/Mohr-Coulomb wedge failure, wake relaxation, and non-planar fractography

#include "Engine/GeometrySpace/Fracture/EngineeringBakedFracture.h"
#include "Engine/GeometrySpace/Fracture/FastRuntimeFracture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>

namespace Frontier::GeometrySpace::Fracture
{

namespace
{

constexpr double PiConst = 3.14159265358979323846;

double Hash3D(int32_t Ix, int32_t Iy, int32_t Iz, uint32_t Seed) noexcept
{
    uint32_t H = static_cast<uint32_t>(Ix) * 374761393u
               + static_cast<uint32_t>(Iy) * 668265263u
               + static_cast<uint32_t>(Iz) * 2147483647u
               + Seed * 1597334677u;
    H = (H ^ (H >> 13u)) * 1274126177u;
    return static_cast<double>(H ^ (H >> 16u)) / 4294967296.0;
}

double ValueNoise3D(double X, double Y, double Z, uint32_t Seed) noexcept
{
    const int32_t Ix = static_cast<int32_t>(std::floor(X));
    const int32_t Iy = static_cast<int32_t>(std::floor(Y));
    const int32_t Iz = static_cast<int32_t>(std::floor(Z));
    const double  Fx = X - static_cast<double>(Ix);
    const double  Fy = Y - static_cast<double>(Iy);
    const double  Fz = Z - static_cast<double>(Iz);
    const double  Ux = Fx * Fx * (3.0 - 2.0 * Fx);
    const double  Uy = Fy * Fy * (3.0 - 2.0 * Fy);
    const double  Uz = Fz * Fz * (3.0 - 2.0 * Fz);

    const double N000 = Hash3D(Ix,     Iy,     Iz,     Seed);
    const double N100 = Hash3D(Ix + 1, Iy,     Iz,     Seed);
    const double N010 = Hash3D(Ix,     Iy + 1, Iz,     Seed);
    const double N110 = Hash3D(Ix + 1, Iy + 1, Iz,     Seed);
    const double N001 = Hash3D(Ix,     Iy,     Iz + 1, Seed);
    const double N101 = Hash3D(Ix + 1, Iy,     Iz + 1, Seed);
    const double N011 = Hash3D(Ix,     Iy + 1, Iz + 1, Seed);
    const double N111 = Hash3D(Ix + 1, Iy + 1, Iz + 1, Seed);

    const double Nx00 = N000 + (N100 - N000) * Ux;
    const double Nx10 = N010 + (N110 - N010) * Ux;
    const double Nx01 = N001 + (N101 - N001) * Ux;
    const double Nx11 = N011 + (N111 - N011) * Ux;
    const double Nxy0 = Nx00 + (Nx10 - Nx00) * Uy;
    const double Nxy1 = Nx01 + (Nx11 - Nx01) * Uy;
    return (Nxy0 + (Nxy1 - Nxy0) * Uz) * 2.0 - 1.0;
}

double SelfAffineBouchaudFbm(double X, double Y, double Z, double Hurst, uint32_t Octaves, uint32_t Seed) noexcept
{
    const double Gain = std::pow(2.0, -Hurst);
    double Freq = 1.0, Amp = 1.0, Sum = 0.0, Norm = 0.0;
    for (uint32_t Octave = 0u; Octave < Octaves; ++Octave)
    {
        Sum  += Amp * ValueNoise3D(X * Freq, Y * Freq, Z * Freq, Seed + Octave * 101u);
        Norm += Amp;
        Freq *= 2.07;
        Amp  *= Gain;
    }
    return Sum / std::max(1.0e-6, Norm);
}

double SampleAggregateInclusionField(double X, double Y, double Z, double Scale, uint32_t Seed) noexcept
{
    const double  Sx = X * Scale, Sy = Y * Scale, Sz = Z * Scale;
    const int32_t Bx = static_cast<int32_t>(std::floor(Sx));
    const int32_t By = static_cast<int32_t>(std::floor(Sy));
    const int32_t Bz = static_cast<int32_t>(std::floor(Sz));

    double BestD2     = 99.0;
    double BestParity = 1.0;
    double BestRadius = 0.38;

    for (int32_t Dz = -1; Dz <= 1; ++Dz)
    {
        for (int32_t Dy = -1; Dy <= 1; ++Dy)
        {
            for (int32_t Dx = -1; Dx <= 1; ++Dx)
            {
                const int32_t Cx = Bx + Dx, Cy = By + Dy, Cz = Bz + Dz;
                const double  Jx = static_cast<double>(Cx) + 0.18 + 0.64 * Hash3D(Cx, Cy, Cz, Seed + 11u);
                const double  Jy = static_cast<double>(Cy) + 0.18 + 0.64 * Hash3D(Cx, Cy, Cz, Seed + 29u);
                const double  Jz = static_cast<double>(Cz) + 0.18 + 0.64 * Hash3D(Cx, Cy, Cz, Seed + 47u);
                const double  D2 = (Sx - Jx) * (Sx - Jx) + (Sy - Jy) * (Sy - Jy) + (Sz - Jz) * (Sz - Jz);
                if (D2 < BestD2)
                {
                    BestD2     = D2;
                    BestParity = Hash3D(Cx, Cy, Cz, Seed + 73u) > 0.5 ? 1.0 : -1.0;
                    BestRadius = 0.28 + 0.20 * Hash3D(Cx, Cy, Cz, Seed + 91u);
                }
            }
        }
    }

    const double R2 = BestRadius * BestRadius;
    if (BestD2 >= R2)
    {
        return 0.0;
    }
    return BestParity * std::sqrt(std::max(0.0, 1.0 - BestD2 / R2));
}

bool PointInsideFlatConvexCell(const FlatConvexCell& Cell, double Px, double Py, double Pz) noexcept
{
    const double* V = Cell.Vertices.data();
    for (uint32_t F = 0u; F < Cell.FaceCount; ++F)
    {
        const int32_t S  = Cell.FaceOffsets[F];
        const int32_t I0 = Cell.FaceIndices[static_cast<size_t>(S + 0)] * 3;
        const int32_t I1 = Cell.FaceIndices[static_cast<size_t>(S + 1)] * 3;
        const int32_t I2 = Cell.FaceIndices[static_cast<size_t>(S + 2)] * 3;
        const double  E1x = V[I1] - V[I0], E1y = V[I1 + 1] - V[I0 + 1], E1z = V[I1 + 2] - V[I0 + 2];
        const double  E2x = V[I2] - V[I0], E2y = V[I2 + 1] - V[I0 + 1], E2z = V[I2 + 2] - V[I0 + 2];
        const double  Nx  = E1y * E2z - E1z * E2y;
        const double  Ny  = E1z * E2x - E1x * E2z;
        const double  Nz  = E1x * E2y - E1y * E2x;
        const double  Dot = (Px - V[I0]) * Nx + (Py - V[I0 + 1]) * Ny + (Pz - V[I0 + 2]) * Nz;
        if (Dot > 1.0e-5)
        {
            return false;
        }
    }
    return true;
}

struct ElastostaticLattice
{
    uint32_t             GridN               = 12u;
    uint32_t             Total               = 1728u;
    std::vector<uint8_t> Active              {};
    std::vector<double>  Positions           {};
    std::vector<double>  StressTensor        {};
    double               VoxelVolume         = 0.0;
    double               CellSize            = 0.0;
    double               MinX                = 0.0, MinY = 0.0, MinZ = 0.0;
    double               SpanX               = 0.0, SpanY = 0.0, SpanZ = 0.0;
    double               ContactRadiusM      = 0.0;
    double               PeakHertzPressureMPa = 0.0;
};

ElastostaticLattice SolveVolumetricStressLattice(
    const FlatConvexCell&              RootCell,
    const FractureMaterialSpec&        Material,
    const EngineeringConstitutiveSpec& EngSpec,
    double                             ImpactEnergyJ,
    double                             Ipx,
    double                             Ipy,
    double                             Ipz,
    double                             Inx,
    double                             Iny,
    double                             Inz,
    uint32_t                           GridN = 12u)
{
    ElastostaticLattice Lattice {};
    Lattice.GridN = GridN;
    Lattice.Total = GridN * GridN * GridN;

    const double MinX  = RootCell.MinX, MinY = RootCell.MinY, MinZ = RootCell.MinZ;
    const double SpanX = std::max(0.05, RootCell.MaxX - MinX);
    const double SpanY = std::max(0.05, RootCell.MaxY - MinY);
    const double SpanZ = std::max(0.05, RootCell.MaxZ - MinZ);
    const double Hx    = SpanX / static_cast<double>(GridN);
    const double Hy    = SpanY / static_cast<double>(GridN);
    const double Hz    = SpanZ / static_cast<double>(GridN);
    const double HMean = (Hx + Hy + Hz) / 3.0;

    Lattice.MinX        = MinX;  Lattice.MinY  = MinY;  Lattice.MinZ  = MinZ;
    Lattice.SpanX       = SpanX; Lattice.SpanY = SpanY; Lattice.SpanZ = SpanZ;
    Lattice.VoxelVolume = Hx * Hy * Hz;
    Lattice.CellSize    = HMean;

    const double EMPa    = static_cast<double>(Material.YoungModulusGPa) * 1000.0;
    const double Nu      = std::clamp(static_cast<double>(Material.PoissonRatio), 0.10, 0.45);
    const double EStarPa = (EMPa * 1.0e6) / (1.0 - Nu * Nu);
    const double RIndent = std::max(0.025, RootCell.RadiusM * 0.10);
    const double FMaxN   = std::pow(1.25 * ImpactEnergyJ, 0.6) * std::pow(1.3333 * EStarPa * std::sqrt(RIndent), 0.4);
    const double ContactRadiusM = std::clamp(
        std::cbrt((0.75 * FMaxN * RIndent) / EStarPa),
        HMean * 1.15,
        RootCell.RadiusM * 0.42);
    const double PeakHertzMPa = std::min(
        static_cast<double>(EngSpec.CompressiveStrengthMPa) * 6.5,
        (1.5 * FMaxN) / (PiConst * ContactRadiusM * ContactRadiusM * 1.0e6));

    Lattice.ContactRadiusM       = ContactRadiusM;
    Lattice.PeakHertzPressureMPa = PeakHertzMPa;

    const double Lambda = (EMPa * Nu) / ((1.0 + Nu) * (1.0 - 2.0 * Nu));
    const double Mu     = EMPa / (2.0 * (1.0 + Nu));

    Lattice.Active.assign(Lattice.Total, 0u);
    std::vector<uint8_t> Anchored(Lattice.Total, 0u);
    std::vector<double>  Disp(Lattice.Total * 3u, 0.0);
    std::vector<double>  Force(Lattice.Total * 3u, 0.0);
    Lattice.Positions.resize(Lattice.Total * 3u, 0.0);
    Lattice.StressTensor.assign(Lattice.Total * 6u, 0.0);

    const double DxImp = -Inx, DyImp = -Iny, DzImp = -Inz;

    for (uint32_t Iz = 0u; Iz < GridN; ++Iz)
    {
        const double Pz = MinZ + (static_cast<double>(Iz) + 0.5) * Hz;
        for (uint32_t Iy = 0u; Iy < GridN; ++Iy)
        {
            const double Py = MinY + (static_cast<double>(Iy) + 0.5) * Hy;
            for (uint32_t Ix = 0u; Ix < GridN; ++Ix)
            {
                const double   Px  = MinX + (static_cast<double>(Ix) + 0.5) * Hx;
                const uint32_t Idx = (Iz * GridN + Iy) * GridN + Ix;
                Lattice.Positions[Idx * 3u + 0u] = Px;
                Lattice.Positions[Idx * 3u + 1u] = Py;
                Lattice.Positions[Idx * 3u + 2u] = Pz;

                if (!PointInsideFlatConvexCell(RootCell, Px, Py, Pz))
                {
                    continue;
                }
                Lattice.Active[Idx] = 1u;
                // RHR +Z Up foundation anchorage on bottom slab face
                if (Iz == 0u)
                {
                    Anchored[Idx] = 1u;
                }

                const double Rx     = Px - Ipx, Ry = Py - Ipy, Rz = Pz - Ipz;
                const double ZDepth = Rx * DxImp + Ry * DyImp + Rz * DzImp;
                const double Rpx    = Rx - DxImp * ZDepth;
                const double Rpy    = Ry - DyImp * ZDepth;
                const double Rpz    = Rz - DzImp * ZDepth;
                const double RPerp  = std::hypot(Rpx, Rpy, Rpz);

                if (ZDepth >= -HMean * 1.2 && ZDepth <= ContactRadiusM * 2.4 && RPerp <= ContactRadiusM * 1.6)
                {
                    const double QNorm      = std::max(0.0, 1.0 - (RPerp * RPerp) / std::pow(ContactRadiusM * 1.6, 2.0));
                    const double DepthAtten = std::exp(-std::max(0.0, ZDepth) / (ContactRadiusM * 1.5));
                    const double PLocal     = PeakHertzMPa * std::sqrt(QNorm) * DepthAtten;
                    const double InvRp      = RPerp > 1.0e-6 ? 1.0 / RPerp : 0.0;
                    Force[Idx * 3u + 0u] += (DxImp * PLocal + Rpx * InvRp * PLocal * Nu * 0.85) / HMean;
                    Force[Idx * 3u + 1u] += (DyImp * PLocal + Rpy * InvRp * PLocal * Nu * 0.85) / HMean;
                    Force[Idx * 3u + 2u] += (DzImp * PLocal + Rpz * InvRp * PLocal * Nu * 0.85) / HMean;
                }

                if (ZDepth > RootCell.RadiusM * 0.45 && RPerp < ContactRadiusM * 2.8)
                {
                    const double SpallPull = PeakHertzMPa * 0.16 * std::exp(-(RPerp * RPerp) / std::pow(ContactRadiusM * 2.5, 2.0));
                    Force[Idx * 3u + 0u] += DxImp * SpallPull / HMean;
                    Force[Idx * 3u + 1u] += DyImp * SpallPull / HMean;
                    Force[Idx * 3u + 2u] += DzImp * SpallPull / HMean;
                }
            }
        }
    }

    const double InvH2 = 1.0 / (HMean * HMean);
    const double Diag  = 2.0 * (Lambda + 4.0 * Mu) * InvH2;
    const double Omega = 1.48;

    for (uint32_t Iter = 0u; Iter < 28u; ++Iter)
    {
        for (uint32_t Iz = 0u; Iz < GridN; ++Iz)
        {
            for (uint32_t Iy = 0u; Iy < GridN; ++Iy)
            {
                for (uint32_t Ix = 0u; Ix < GridN; ++Ix)
                {
                    const uint32_t Idx = (Iz * GridN + Iy) * GridN + Ix;
                    if (!Lattice.Active[Idx] || Anchored[Idx]) continue;

                    const uint32_t Xm = (Ix > 0u && Lattice.Active[Idx - 1u]) ? Idx - 1u : Idx;
                    const uint32_t Xp = (Ix + 1u < GridN && Lattice.Active[Idx + 1u]) ? Idx + 1u : Idx;
                    const uint32_t Ym = (Iy > 0u && Lattice.Active[Idx - GridN]) ? Idx - GridN : Idx;
                    const uint32_t Yp = (Iy + 1u < GridN && Lattice.Active[Idx + GridN]) ? Idx + GridN : Idx;
                    const uint32_t Zm = (Iz > 0u && Lattice.Active[Idx - GridN * GridN]) ? Idx - GridN * GridN : Idx;
                    const uint32_t Zp = (Iz + 1u < GridN && Lattice.Active[Idx + GridN * GridN]) ? Idx + GridN * GridN : Idx;

                    const double LapUx = (Disp[Xm * 3u] + Disp[Xp * 3u] + Disp[Ym * 3u] + Disp[Yp * 3u] + Disp[Zm * 3u] + Disp[Zp * 3u]) * InvH2;
                    const double LapUy = (Disp[Xm * 3u + 1u] + Disp[Xp * 3u + 1u] + Disp[Ym * 3u + 1u] + Disp[Yp * 3u + 1u] + Disp[Zm * 3u + 1u] + Disp[Zp * 3u + 1u]) * InvH2;
                    const double LapUz = (Disp[Xm * 3u + 2u] + Disp[Xp * 3u + 2u] + Disp[Ym * 3u + 2u] + Disp[Yp * 3u + 2u] + Disp[Zm * 3u + 2u] + Disp[Zp * 3u + 2u]) * InvH2;

                    const double D2xx = (Disp[Xm * 3u] + Disp[Xp * 3u]) * InvH2;
                    const double D2yy = (Disp[Ym * 3u + 1u] + Disp[Yp * 3u + 1u]) * InvH2;
                    const double D2zz = (Disp[Zm * 3u + 2u] + Disp[Zp * 3u + 2u]) * InvH2;

                    const double RhsX = Mu * LapUx + (Lambda + Mu) * D2xx + Force[Idx * 3u + 0u];
                    const double RhsY = Mu * LapUy + (Lambda + Mu) * D2yy + Force[Idx * 3u + 1u];
                    const double RhsZ = Mu * LapUz + (Lambda + Mu) * D2zz + Force[Idx * 3u + 2u];

                    Disp[Idx * 3u + 0u] += Omega * (RhsX / Diag - Disp[Idx * 3u + 0u]);
                    Disp[Idx * 3u + 1u] += Omega * (RhsY / Diag - Disp[Idx * 3u + 1u]);
                    Disp[Idx * 3u + 2u] += Omega * (RhsZ / Diag - Disp[Idx * 3u + 2u]);
                }
            }
        }
    }

    for (uint32_t Iz = 0u; Iz < GridN; ++Iz)
    {
        for (uint32_t Iy = 0u; Iy < GridN; ++Iy)
        {
            for (uint32_t Ix = 0u; Ix < GridN; ++Ix)
            {
                const uint32_t Idx = (Iz * GridN + Iy) * GridN + Ix;
                if (!Lattice.Active[Idx]) continue;

                const uint32_t Xm = (Ix > 0u && Lattice.Active[Idx - 1u]) ? Idx - 1u : Idx;
                const uint32_t Xp = (Ix + 1u < GridN && Lattice.Active[Idx + 1u]) ? Idx + 1u : Idx;
                const uint32_t Ym = (Iy > 0u && Lattice.Active[Idx - GridN]) ? Idx - GridN : Idx;
                const uint32_t Yp = (Iy + 1u < GridN && Lattice.Active[Idx + GridN]) ? Idx + GridN : Idx;
                const uint32_t Zm = (Iz > 0u && Lattice.Active[Idx - GridN * GridN]) ? Idx - GridN * GridN : Idx;
                const uint32_t Zp = (Iz + 1u < GridN && Lattice.Active[Idx + GridN * GridN]) ? Idx + GridN * GridN : Idx;

                const double InvDx = 1.0 / (static_cast<double>(Xp > Xm ? (Xp - Xm) : 1u) * Hx);
                const double InvDy = 1.0 / (static_cast<double>(Yp > Ym ? ((Yp - Ym) / GridN) : 1u) * Hy);
                const double InvDz = 1.0 / (static_cast<double>(Zp > Zm ? ((Zp - Zm) / (GridN * GridN)) : 1u) * Hz);

                const double Exx = (Disp[Xp * 3u + 0u] - Disp[Xm * 3u + 0u]) * InvDx;
                const double Eyy = (Disp[Yp * 3u + 1u] - Disp[Ym * 3u + 1u]) * InvDy;
                const double Ezz = (Disp[Zp * 3u + 2u] - Disp[Zm * 3u + 2u]) * InvDz;

                const double Exy = 0.5 * ((Disp[Yp * 3u + 0u] - Disp[Ym * 3u + 0u]) * InvDy + (Disp[Xp * 3u + 1u] - Disp[Xm * 3u + 1u]) * InvDx);
                const double Eyz = 0.5 * ((Disp[Zp * 3u + 1u] - Disp[Zm * 3u + 1u]) * InvDz + (Disp[Yp * 3u + 2u] - Disp[Ym * 3u + 2u]) * InvDy);
                const double Exz = 0.5 * ((Disp[Zp * 3u + 0u] - Disp[Zm * 3u + 0u]) * InvDz + (Disp[Xp * 3u + 2u] - Disp[Xm * 3u + 2u]) * InvDx);

                const double Trace = Exx + Eyy + Ezz;
                double Sxx = Lambda * Trace + 2.0 * Mu * Exx;
                double Syy = Lambda * Trace + 2.0 * Mu * Eyy;
                double Szz = Lambda * Trace + 2.0 * Mu * Ezz;
                const double Txy = 2.0 * Mu * Exy;
                const double Tyz = 2.0 * Mu * Eyz;
                const double Txz = 2.0 * Mu * Exz;

                const double Px = Lattice.Positions[Idx * 3u + 0u];
                const double Py = Lattice.Positions[Idx * 3u + 1u];
                const double Pz = Lattice.Positions[Idx * 3u + 2u];
                const double Rx = Px - Ipx, Ry = Py - Ipy, Rz = Pz - Ipz;
                const double Rho      = std::max(ContactRadiusM * 0.65, std::hypot(Rx, Ry, Rz));
                const double BoussMag = PeakHertzMPa * 0.24 * std::pow(ContactRadiusM / Rho, 1.65);

                double Hpx = DyImp * Rz - DzImp * Ry;
                double Hpy = DzImp * Rx - DxImp * Rz;
                double Hpz = DxImp * Ry - DyImp * Rx;
                const double HpLen = std::hypot(Hpx, Hpy, Hpz);
                if (HpLen > 1.0e-6) { Hpx /= HpLen; Hpy /= HpLen; Hpz /= HpLen; }
                else { Hpx = 1.0; Hpy = 0.0; Hpz = 0.0; }

                Sxx += BoussMag * Hpx * Hpx;
                Syy += BoussMag * Hpy * Hpy;
                Szz += BoussMag * Hpz * Hpz;

                if (Material.StoredEnergyJPerM3 > 0.0f)
                {
                    // Parabolic residual core-skin tension across the thinnest axis of the plate/solid
                    double NormT = 0.0;
                    if (SpanY <= SpanX && SpanY <= SpanZ)
                    {
                        NormT = (Py - RootCell.CentroidY) / std::max(0.02, 0.5 * SpanY);
                    }
                    else if (SpanX <= SpanY && SpanX <= SpanZ)
                    {
                        NormT = (Px - RootCell.CentroidX) / std::max(0.02, 0.5 * SpanX);
                    }
                    else
                    {
                        NormT = (Pz - RootCell.CentroidZ) / std::max(0.02, 0.5 * SpanZ);
                    }
                    const double ResMPa = 48.0 * (1.0 - 3.0 * NormT * NormT);
                    Sxx += ResMPa;
                    Syy += ResMPa;
                }

                const uint32_t B = Idx * 6u;
                Lattice.StressTensor[B + 0u] = Sxx;
                Lattice.StressTensor[B + 1u] = Syy;
                Lattice.StressTensor[B + 2u] = Szz;
                Lattice.StressTensor[B + 3u] = Txy;
                Lattice.StressTensor[B + 4u] = Tyz;
                Lattice.StressTensor[B + 5u] = Txz;
            }
        }
    }

    return Lattice;
}

void RelaxStressTensorAroundCrack(
    ElastostaticLattice&  Lattice,
    double                Nx,
    double                Ny,
    double                Nz,
    double                PlaneD,
    const FlatConvexCell& FragCell,
    double                ReliefLengthM) noexcept
{
    const double Pad  = ReliefLengthM * 1.15;
    const double MinX = FragCell.MinX - Pad, MaxX = FragCell.MaxX + Pad;
    const double MinY = FragCell.MinY - Pad, MaxY = FragCell.MaxY + Pad;
    const double MinZ = FragCell.MinZ - Pad, MaxZ = FragCell.MaxZ + Pad;

    for (uint32_t Index = 0u; Index < Lattice.Total; ++Index)
    {
        if (!Lattice.Active[Index]) continue;
        const double Px = Lattice.Positions[Index * 3u + 0u];
        const double Py = Lattice.Positions[Index * 3u + 1u];
        const double Pz = Lattice.Positions[Index * 3u + 2u];
        if (Px < MinX || Px > MaxX || Py < MinY || Py > MaxY || Pz < MinZ || Pz > MaxZ) continue;

        const double Dist = std::abs(Nx * Px + Ny * Py + Nz * Pz - PlaneD);
        if (Dist > ReliefLengthM * 2.2) continue;

        const double W   = 0.92 * std::exp(-Dist / std::max(1.0e-4, ReliefLengthM));
        const double P00 = 1.0 - W * Nx * Nx, P01 = -W * Nx * Ny, P02 = -W * Nx * Nz;
        const double P11 = 1.0 - W * Ny * Ny, P12 = -W * Ny * Nz;
        const double P22 = 1.0 - W * Nz * Nz;

        const uint32_t B   = Index * 6u;
        const double   S00 = Lattice.StressTensor[B + 0u], S11 = Lattice.StressTensor[B + 1u], S22 = Lattice.StressTensor[B + 2u];
        const double   S01 = Lattice.StressTensor[B + 3u], S12 = Lattice.StressTensor[B + 4u], S02 = Lattice.StressTensor[B + 5u];

        const double M00 = P00 * S00 + P01 * S01 + P02 * S02;
        const double M01 = P00 * S01 + P01 * S11 + P02 * S12;
        const double M02 = P00 * S02 + P01 * S12 + P02 * S22;

        const double M10 = P01 * S00 + P11 * S01 + P12 * S02;
        const double M11 = P01 * S01 + P11 * S11 + P12 * S12;
        const double M12 = P01 * S02 + P11 * S12 + P12 * S22;

        const double M20 = P02 * S00 + P12 * S01 + P22 * S02;
        const double M21 = P02 * S01 + P12 * S11 + P22 * S12;
        const double M22 = P02 * S02 + P12 * S12 + P22 * S22;

        Lattice.StressTensor[B + 0u] = M00 * P00 + M01 * P01 + M02 * P02;
        Lattice.StressTensor[B + 1u] = M10 * P01 + M11 * P11 + M12 * P12;
        Lattice.StressTensor[B + 2u] = M20 * P02 + M21 * P12 + M22 * P22;
        Lattice.StressTensor[B + 3u] = M00 * P01 + M01 * P11 + M02 * P12;
        Lattice.StressTensor[B + 4u] = M10 * P02 + M11 * P12 + M12 * P22;
        Lattice.StressTensor[B + 5u] = M00 * P02 + M01 * P12 + M02 * P22;
    }
}

struct CutPlaneEntry
{
    int32_t Token     = 0;
    double  CanonNx   = 0.0, CanonNy = 0.0, CanonNz = 1.0;
    double  TanUx     = 1.0, TanUy   = 0.0, TanUz   = 0.0;
    double  TanVx     = 0.0, TanVy   = 1.0, TanVz   = 0.0;
    double  NucX      = 0.0, NucY    = 0.0, NucZ    = 0.0;
    double  MaxRadius = 0.5;
    double  CupSign   = 1.0;
};

struct CrossingPlaneEquation
{
    double Nx = 0.0, Ny = 0.0, Nz = 0.0, Offset = 0.0;
};

double ComputeFractographyDisplacement(
    double                             Px,
    double                             Py,
    double                             Pz,
    const CutPlaneEntry&               CutInfo,
    const FractureMaterialSpec&        Material,
    const EngineeringConstitutiveSpec& EngSpec,
    double                             GrainRad,
    uint32_t                           Seed) noexcept
{
    const uint32_t CutSeed = Seed + static_cast<uint32_t>(CutInfo.Token) * 977u;

    if (Material.Category == FractureMaterialCategory::BulkBrittle)
    {
        const double Fbm = SelfAffineBouchaudFbm(Px * 5.2, Py * 5.2, Pz * 5.2, EngSpec.HurstExponent, 4u, CutSeed);
        const double Agg = SampleAggregateInclusionField(Px, Py, Pz, EngSpec.AggregateScale, CutSeed + 313u);
        return (1.0 - static_cast<double>(EngSpec.AggregateWeight)) * Fbm
             + static_cast<double>(EngSpec.AggregateWeight) * Agg;
    }
    if (Material.Category == FractureMaterialCategory::PlateBrittle)
    {
        const double Dx    = Px - CutInfo.NucX, Dy = Py - CutInfo.NucY, Dz = Pz - CutInfo.NucZ;
        const double R     = std::hypot(Dx, Dy, Dz);
        const double RNorm = std::min(1.5, R / std::max(0.06, CutInfo.MaxRadius));
        const double Env   = std::clamp((RNorm - 0.18) / 0.55, 0.0, 1.0);
        const double Ang   = std::atan2(
            Dx * CutInfo.TanVx + Dy * CutInfo.TanVy + Dz * CutInfo.TanVz,
            Dx * CutInfo.TanUx + Dy * CutInfo.TanUy + Dz * CutInfo.TanUz);
        const double Hackle  = std::sin(Ang * 18.0 + ValueNoise3D(Px * 9.0, Py * 9.0, Pz * 9.0, CutSeed) * 2.2) * Env;
        const double Wallner = 0.35 * std::sin(R * 46.0) * std::exp(-RNorm * 0.6);
        return 0.65 * Hackle + Wallner;
    }
    if (Material.Category == FractureMaterialCategory::Fibrous)
    {
        const double Gx      = std::cos(GrainRad), Gy = std::sin(GrainRad);
        const double Along   = Px * Gx + Py * Gy;
        const double Across1 = -Px * Gy + Py * Gx;
        const double Across2 = Pz;
        return SelfAffineBouchaudFbm(Along * 1.6, Across1 * 19.0, Across2 * 19.0, EngSpec.HurstExponent, 4u, CutSeed) * 1.15;
    }

    return 0.45 * SelfAffineBouchaudFbm(Px * 8.0, Py * 8.0, Pz * 8.0, EngSpec.HurstExponent, 3u, CutSeed);
}

using Polygon3D = std::vector<std::array<double, 3>>;

std::vector<Polygon3D> SplitPolygonByPlane3D(const Polygon3D& Poly, double Nx, double Ny, double Nz, double Offset)
{
    uint32_t Pos = 0u, Neg = 0u;
    std::vector<double> Dists(Poly.size(), 0.0);
    for (size_t I = 0u; I < Poly.size(); ++I)
    {
        const double V = Nx * Poly[I][0] + Ny * Poly[I][1] + Nz * Poly[I][2] - Offset;
        Dists[I] = V;
        if (V > 1.0e-6) ++Pos;
        else if (V < -1.0e-6) ++Neg;
    }
    if (Pos == 0u || Neg == 0u)
    {
        return { Poly };
    }

    Polygon3D Front, Back;
    for (size_t I = 0u; I < Poly.size(); ++I)
    {
        const size_t J  = (I + 1u) % Poly.size();
        const auto&  A  = Poly[I];
        const auto&  B  = Poly[J];
        const double Da = Dists[I];
        const double Db = Dists[J];
        if (Da >= -1.0e-6) Front.push_back(A);
        if (Da <=  1.0e-6) Back.push_back(A);
        if ((Da > 1.0e-6 && Db < -1.0e-6) || (Da < -1.0e-6 && Db > 1.0e-6))
        {
            const double T = Da / (Da - Db);
            const std::array<double, 3> Intersect {
                A[0] + (B[0] - A[0]) * T,
                A[1] + (B[1] - A[1]) * T,
                A[2] + (B[2] - A[2]) * T
            };
            Front.push_back(Intersect);
            Back.push_back(Intersect);
        }
    }

    std::vector<Polygon3D> Output;
    if (Front.size() >= 3u) Output.push_back(std::move(Front));
    if (Back.size()  >= 3u) Output.push_back(std::move(Back));
    if (Output.empty()) Output.push_back(Poly);
    return Output;
}

struct EngineeringSurfaceBatch
{
    std::vector<EngineeringSurface> Surfaces            {};
    double                          RoughnessRmsMm      = 0.0;
    double                          NonPlanarVolumeSum  = 0.0;
};

EngineeringSurfaceBatch BuildInterlockingEngineeringSurfaces(
    const std::vector<FlatConvexCell>&                            Cells,
    const std::unordered_map<int32_t, CutPlaneEntry>&             CutRegistry,
    const std::unordered_map<int32_t, std::vector<CrossingPlaneEquation>>& CutCrossingPlanes,
    const FractureMaterialSpec&                                   Material,
    const EngineeringConstitutiveSpec&                            EngSpec,
    double                                                        GrainRad,
    uint32_t                                                      Seed)
{
    std::unordered_map<int32_t, double> TokenMinSpan;
    for (const auto& Cell : Cells)
    {
        for (uint32_t F = 0u; F < Cell.FaceCount; ++F)
        {
            const int32_t Tok = Cell.FaceTokens[F];
            if (Tok <= 0) continue;
            auto It = TokenMinSpan.find(Tok);
            if (It == TokenMinSpan.end() || Cell.MinSpanM < It->second)
            {
                TokenMinSpan[Tok] = Cell.MinSpanM;
            }
        }
    }

    double   SqReliefSum   = 0.0;
    uint64_t ReliefSamples = 0ull;
    double   TotalVolSum   = 0.0;

    std::vector<EngineeringSurface> Surfaces(Cells.size());

    for (size_t C = 0u; C < Cells.size(); ++C)
    {
        const FlatConvexCell& Cell = Cells[C];
        const double*         V    = Cell.Vertices.data();

        EngineeringSurface& Surface = Surfaces[C];
        double ShardSignedVol = 0.0;

        auto PushTri = [&](double Ax, double Ay, double Az,
                           double Bx, double By, double Bz,
                           double Cx, double Cy, double Cz,
                           bool   IsInterior) -> double
        {
            const double E1x = Bx - Ax, E1y = By - Ay, E1z = Bz - Az;
            const double E2x = Cx - Ax, E2y = Cy - Ay, E2z = Cz - Az;
            double Nx = E1y * E2z - E1z * E2y;
            double Ny = E1z * E2x - E1x * E2z;
            double Nz = E1x * E2y - E1y * E2x;
            const double NLen = std::hypot(Nx, Ny, Nz);
            if (NLen < 1.0e-13) return 0.0;
            Nx /= NLen; Ny /= NLen; Nz /= NLen;

            Surface.Positions.push_back({ static_cast<float>(Ax), static_cast<float>(Ay), static_cast<float>(Az) });
            Surface.Positions.push_back({ static_cast<float>(Bx), static_cast<float>(By), static_cast<float>(Bz) });
            Surface.Positions.push_back({ static_cast<float>(Cx), static_cast<float>(Cy), static_cast<float>(Cz) });

            const Mathematics::Axis3D NormalVec { static_cast<float>(Nx), static_cast<float>(Ny), static_cast<float>(Nz) };
            Surface.Normals.push_back(NormalVec);
            Surface.Normals.push_back(NormalVec);
            Surface.Normals.push_back(NormalVec);

            const uint8_t IntVal = IsInterior ? 1u : 0u;
            Surface.InteriorMask.push_back(IntVal);
            Surface.InteriorMask.push_back(IntVal);
            Surface.InteriorMask.push_back(IntVal);
            ++Surface.TriangleCount;

            return (Ax * (By * Cz - Bz * Cy) + Ay * (Bz * Cx - Bx * Cz) + Az * (Bx * Cy - By * Cx)) / 6.0;
        };

        for (uint32_t F = 0u; F < Cell.FaceCount; ++F)
        {
            const int32_t S   = Cell.FaceOffsets[F];
            const int32_t E   = Cell.FaceOffsets[F + 1u];
            const int32_t K   = E - S;
            if (K < 3) continue;
            const int32_t Tok = Cell.FaceTokens[F];

            if (Tok <= 0)
            {
                const int32_t I0 = Cell.FaceIndices[static_cast<size_t>(S)] * 3;
                const double  Ax = V[I0], Ay = V[I0 + 1], Az = V[I0 + 2];
                for (int32_t Cursor = S + 1; Cursor + 1 < E; ++Cursor)
                {
                    const int32_t I1 = Cell.FaceIndices[static_cast<size_t>(Cursor)] * 3;
                    const int32_t I2 = Cell.FaceIndices[static_cast<size_t>(Cursor + 1)] * 3;
                    ShardSignedVol += PushTri(
                        Ax, Ay, Az,
                        V[I1], V[I1 + 1], V[I1 + 2],
                        V[I2], V[I2 + 1], V[I2 + 2],
                        false);
                }
                continue;
            }

            Polygon3D InitPoly(static_cast<size_t>(K));
            for (int32_t J = 0; J < K; ++J)
            {
                const int32_t Ia = Cell.FaceIndices[static_cast<size_t>(S + J)] * 3;
                InitPoly[static_cast<size_t>(J)] = { V[Ia], V[Ia + 1], V[Ia + 2] };
            }

            std::vector<Polygon3D> SubPolys { std::move(InitPoly) };
            if (auto CrossIt = CutCrossingPlanes.find(Tok); CrossIt != CutCrossingPlanes.end())
            {
                for (const auto& Plane : CrossIt->second)
                {
                    std::vector<Polygon3D> NextPolys;
                    for (const auto& Sp : SubPolys)
                    {
                        auto Parts = SplitPolygonByPlane3D(Sp, Plane.Nx, Plane.Ny, Plane.Nz, Plane.Offset);
                        for (auto& Part : Parts) NextPolys.push_back(std::move(Part));
                    }
                    SubPolys = std::move(NextPolys);
                }
            }

            const auto          CutIt    = CutRegistry.find(Tok);
            const CutPlaneEntry* CutInfo = (CutIt != CutRegistry.end()) ? &CutIt->second : nullptr;
            const double        PairSpan = TokenMinSpan.contains(Tok) ? TokenMinSpan[Tok] : Cell.MinSpanM;
            const double        AmpMax   = std::min(static_cast<double>(EngSpec.ReliefAmplitudeM), PairSpan * 0.22);

            for (const auto& RawCorners : SubPolys)
            {
                if (RawCorners.size() < 3u) continue;
                const size_t M = RawCorners.size() * 2u;
                Polygon3D    Boundary(M);
                for (size_t J = 0u; J < RawCorners.size(); ++J)
                {
                    const auto& A = RawCorners[J];
                    const auto& B = RawCorners[(J + 1u) % RawCorners.size()];
                    Boundary[J * 2u + 0u] = A;
                    Boundary[J * 2u + 1u] = { 0.5 * (A[0] + B[0]), 0.5 * (A[1] + B[1]), 0.5 * (A[2] + B[2]) };
                }

                Polygon3D SortedCorners = Boundary;
                std::sort(SortedCorners.begin(), SortedCorners.end(), [](const auto& P, const auto& Q)
                {
                    if (P[0] != Q[0]) return P[0] < Q[0];
                    if (P[1] != Q[1]) return P[1] < Q[1];
                    return P[2] < Q[2];
                });

                double Pcx = 0.0, Pcy = 0.0, Pcz = 0.0;
                for (const auto& Pt : SortedCorners)
                {
                    Pcx += Pt[0]; Pcy += Pt[1]; Pcz += Pt[2];
                }
                Pcx /= static_cast<double>(M);
                Pcy /= static_cast<double>(M);
                Pcz /= static_cast<double>(M);

                auto DisplacePoint = [&](double Bx, double By, double Bz, double Rho) -> std::array<double, 3>
                {
                    if (Rho >= 0.9999 || CutInfo == nullptr)
                    {
                        return { Bx, By, Bz };
                    }
                    const double Qx      = Pcx + (Bx - Pcx) * Rho;
                    const double Qy      = Pcy + (By - Pcy) * Rho;
                    const double Qz      = Pcz + (Bz - Pcz) * Rho;
                    const double TCollar = std::clamp((1.0 - Rho) / 0.34, 0.0, 1.0);
                    const double WSeam   = TCollar * TCollar * (3.0 - 2.0 * TCollar);

                    double Relief = ComputeFractographyDisplacement(Qx, Qy, Qz, *CutInfo, Material, EngSpec, GrainRad, Seed);
                    if (Material.Category == FractureMaterialCategory::Ductile)
                    {
                        const double Lip = std::sin(PiConst * std::min(1.0, (1.0 - Rho) * 1.8)) * 0.55;
                        Relief += CutInfo->CupSign * Lip;
                    }
                    const double Delta = std::clamp(WSeam * AmpMax * Relief, -AmpMax, AmpMax);
                    SqReliefSum += Delta * Delta;
                    ++ReliefSamples;

                    return {
                        Qx + CutInfo->CanonNx * Delta,
                        Qy + CutInfo->CanonNy * Delta,
                        Qz + CutInfo->CanonNz * Delta
                    };
                };

                const auto CenterPt = DisplacePoint(Pcx, Pcy, Pcz, 0.0);
                Polygon3D  Ring0(M), Ring1(M);
                for (size_t J = 0u; J < M; ++J)
                {
                    Ring0[J] = DisplacePoint(Boundary[J][0], Boundary[J][1], Boundary[J][2], 0.34);
                    Ring1[J] = DisplacePoint(Boundary[J][0], Boundary[J][1], Boundary[J][2], 0.68);
                }

                auto EmitCanonicalQuad = [&](const std::array<double, 3>& A0,
                                             const std::array<double, 3>& A1,
                                             const std::array<double, 3>& B1,
                                             const std::array<double, 3>& B0)
                {
                    const double DMain = (A0[0] - B1[0]) * (A0[0] - B1[0]) + (A0[1] - B1[1]) * (A0[1] - B1[1]) + (A0[2] - B1[2]) * (A0[2] - B1[2]);
                    const double DAlt  = (A1[0] - B0[0]) * (A1[0] - B0[0]) + (A1[1] - B0[1]) * (A1[1] - B0[1]) + (A1[2] - B0[2]) * (A1[2] - B0[2]);
                    if (DMain <= DAlt)
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

                for (size_t J = 0u; J < M; ++J)
                {
                    const size_t Jn = (J + 1u) % M;
                    ShardSignedVol += PushTri(
                        CenterPt[0], CenterPt[1], CenterPt[2],
                        Ring0[J][0], Ring0[J][1], Ring0[J][2],
                        Ring0[Jn][0], Ring0[Jn][1], Ring0[Jn][2],
                        true);
                    EmitCanonicalQuad(Ring0[J], Ring0[Jn], Ring1[Jn], Ring1[J]);
                    EmitCanonicalQuad(Ring1[J], Ring1[Jn], Boundary[Jn], Boundary[J]);
                }
            }
        }

        Surface.ExactNonPlanarVolM3 = std::abs(ShardSignedVol);
        TotalVolSum += Surface.ExactNonPlanarVolM3;
    }

    const double RmsMm = ReliefSamples > 0ull ? std::sqrt(SqReliefSum / static_cast<double>(ReliefSamples)) * 1000.0 : 0.0;
    return { std::move(Surfaces), RmsMm, TotalVolSum };
}

} // namespace

void EngineeringBakedFracture::SolveSymmetricEigen3x3(
    double                Sxx,
    double                Syy,
    double                Szz,
    double                Txy,
    double                Tyz,
    double                Txz,
    PrincipalStressEigen& OutEigen) noexcept
{
    const double P1 = Txy * Txy + Txz * Txz + Tyz * Tyz;
    const double Q  = (Sxx + Syy + Szz) / 3.0;

    double S1 = 0.0, S2 = 0.0, S3 = 0.0;
    if (P1 < 1.0e-20)
    {
        S1 = Sxx; S2 = Syy; S3 = Szz;
        if (S1 < S2) std::swap(S1, S2);
        if (S2 < S3) std::swap(S2, S3);
        if (S1 < S2) std::swap(S1, S2);
    }
    else
    {
        const double Bxx  = Sxx - Q, Byy = Syy - Q, Bzz = Szz - Q;
        const double P2   = Bxx * Bxx + Byy * Byy + Bzz * Bzz + 2.0 * P1;
        const double P    = std::sqrt(P2 / 6.0);
        const double InvP = 1.0 / P;
        const double Cxx  = Bxx * InvP, Cyy = Byy * InvP, Czz = Bzz * InvP;
        const double Cxy  = Txy * InvP, Cyz = Tyz * InvP, Cxz = Txz * InvP;

        const double DetC = Cxx * (Cyy * Czz - Cyz * Cyz)
                          - Cxy * (Cxy * Czz - Cyz * Cxz)
                          + Cxz * (Cxy * Cyz - Cyy * Cxz);
        const double R    = std::clamp(0.5 * DetC, -1.0, 1.0);
        const double Phi  = std::acos(R) / 3.0;

        S1 = Q + 2.0 * P * std::cos(Phi);
        S3 = Q + 2.0 * P * std::cos(Phi + (2.0 * PiConst / 3.0));
        S2 = 3.0 * Q - S1 - S3;
    }

    auto ExtractEigenvector = [&](double Lam, double& Vx, double& Vy, double& Vz, double Fx, double Fy, double Fz)
    {
        const double R0x = Sxx - Lam, R0y = Txy,       R0z = Txz;
        const double R1x = Txy,       R1y = Syy - Lam, R1z = Tyz;
        const double R2x = Txz,       R2y = Tyz,       R2z = Szz - Lam;

        const double C01x = R0y * R1z - R0z * R1y, C01y = R0z * R1x - R0x * R1z, C01z = R0x * R1y - R0y * R1x;
        const double C02x = R0y * R2z - R0z * R2y, C02y = R0z * R2x - R0x * R2z, C02z = R0x * R2y - R0y * R2x;
        const double C12x = R1y * R2z - R1z * R2y, C12y = R1z * R2x - R1x * R2z, C12z = R1x * R2y - R1y * R2x;

        const double D01 = C01x * C01x + C01y * C01y + C01z * C01z;
        const double D02 = C02x * C02x + C02y * C02y + C02z * C02z;
        const double D12 = C12x * C12x + C12y * C12y + C12z * C12z;

        Vx = Fx; Vy = Fy; Vz = Fz;
        double Best = D01;
        if (D01 > 1.0e-22) { Vx = C01x; Vy = C01y; Vz = C01z; }
        if (D02 > Best && D02 > 1.0e-22) { Best = D02; Vx = C02x; Vy = C02y; Vz = C02z; }
        if (D12 > Best && D12 > 1.0e-22) { Vx = C12x; Vy = C12y; Vz = C12z; }

        const double NLen = std::hypot(Vx, Vy, Vz);
        if (NLen > 1.0e-12) { Vx /= NLen; Vy /= NLen; Vz /= NLen; }
    };

    OutEigen.Sigma1 = S1;
    OutEigen.Sigma2 = S2;
    OutEigen.Sigma3 = S3;
    ExtractEigenvector(S1, OutEigen.E1X, OutEigen.E1Y, OutEigen.E1Z, 1.0, 0.0, 0.0);
    ExtractEigenvector(S3, OutEigen.E3X, OutEigen.E3Y, OutEigen.E3Z, 0.0, 0.0, 1.0);

    const double D12 = S1 - S2, D23 = S2 - S3, D31 = S3 - S1;
    OutEigen.VonMisesMPa = std::sqrt(0.5 * (D12 * D12 + D23 * D23 + D31 * D31));
}

FracturePartitionResult EngineeringBakedFracture::Execute(
    const ConvexPolyhedron&      SourcePolyhedron,
    const FractureSpecification& Specification)
{
    const auto  StartClock = std::chrono::steady_clock::now();
    const auto& Material   = FractureMaterialRepository::LookupMaterial(Specification.MaterialId);
    const auto& EngSpec    = FractureMaterialRepository::LookupEngineeringSpec(Specification.MaterialId);
    DeterministicRandomStream Rand(Specification.Seed != 0u ? Specification.Seed : 42u);

    const uint32_t Ceiling      = std::clamp(Specification.Ceiling, 2u, 160u);
    const double   ImpactEnergy = std::max(15.0, Specification.EnergyJoules);
    const double   MinSpan      = std::max(0.018, Specification.MinimumSizeM);
    const double   GrainRad     = Specification.GrainAngleDeg * (PiConst / 180.0);
    const double   BondScale    = Specification.BondStrengthScale > 0.0 ? Specification.BondStrengthScale : 1.0;

    FlatConvexCell RootCell    = FastRuntimeFracture::PackFlatConvexCell(SourcePolyhedron);
    const double   TotalVol0   = RootCell.VolumeM3;
    const double   SolidRadius = std::max(0.1, RootCell.RadiusM);

    PolyhedronMetrics SourceMetrics {};
    SourceMetrics.VolumeM3  = TotalVol0;
    SourceMetrics.Centroid  = { static_cast<float>(RootCell.CentroidX), static_cast<float>(RootCell.CentroidY), static_cast<float>(RootCell.CentroidZ) };
    SourceMetrics.BoundsMin = { static_cast<float>(RootCell.MinX), static_cast<float>(RootCell.MinY), static_cast<float>(RootCell.MinZ) };
    SourceMetrics.BoundsMax = { static_cast<float>(RootCell.MaxX), static_cast<float>(RootCell.MaxY), static_cast<float>(RootCell.MaxZ) };
    SourceMetrics.Span      = {
        static_cast<float>(RootCell.MaxX - RootCell.MinX),
        static_cast<float>(RootCell.MaxY - RootCell.MinY),
        static_cast<float>(RootCell.MaxZ - RootCell.MinZ)
    };
    SourceMetrics.RadiusM   = SolidRadius;

    const double Ipx = static_cast<double>(Specification.ImpactPoint.X);
    const double Ipy = static_cast<double>(Specification.ImpactPoint.Y);
    const double Ipz = static_cast<double>(Specification.ImpactPoint.Z);
    const double RawInLen = std::hypot(
        static_cast<double>(Specification.ImpactNormal.X),
        static_cast<double>(Specification.ImpactNormal.Y),
        static_cast<double>(Specification.ImpactNormal.Z));
    const double InvInLen = 1.0 / (RawInLen > 1.0e-12 ? RawInLen : 1.0);
    const double Inx      = RawInLen > 1.0e-12 ? static_cast<double>(Specification.ImpactNormal.X) * InvInLen : 0.0;
    const double Iny      = RawInLen > 1.0e-12 ? static_cast<double>(Specification.ImpactNormal.Y) * InvInLen : -1.0;
    const double Inz      = RawInLen > 1.0e-12 ? static_cast<double>(Specification.ImpactNormal.Z) * InvInLen : 0.0;

    ElastostaticLattice Lattice = SolveVolumetricStressLattice(
        RootCell, Material, EngSpec, ImpactEnergy, Ipx, Ipy, Ipz, Inx, Iny, Inz, 12u);

    const double EPa               = static_cast<double>(Material.YoungModulusGPa) * 1.0e9;
    const double FtMPa             = static_cast<double>(Material.TensileStrengthMPa);
    const double HillerborgLengthM = (EPa * static_cast<double>(Material.FractureToughnessGc)) / std::pow(FtMPa * 1.0e6, 2.0);
    const double PhiRad            = static_cast<double>(EngSpec.InternalFrictionDeg) * (PiConst / 180.0);
    const double SinPhi            = std::sin(PhiRad);
    const double CosPhi            = std::cos(PhiRad);
    const double CohesionMPa       = (static_cast<double>(EngSpec.CompressiveStrengthMPa) * (1.0 - SinPhi)) / (2.0 * std::max(0.2, CosPhi));
    const double ThetaMohrCoulomb  = 0.25 * PiConst - 0.5 * PhiRad;
    const double ReliefLengthM     = std::clamp(HillerborgLengthM * 0.25, Lattice.CellSize * 1.45, SolidRadius * 0.38);

    std::vector<double> VoxelWeibullFt(Lattice.Total, FtMPa);
    const double VolumeScale = std::pow(
        std::max(0.1, TotalVol0 / (std::max(1.0e-9, Lattice.VoxelVolume) * 64.0)),
        1.0 / static_cast<double>(Material.WeibullModulus));
    for (uint32_t Index = 0u; Index < Lattice.Total; ++Index)
    {
        if (!Lattice.Active[Index]) continue;
        const double U      = std::clamp(Rand.NextDouble(), 0.02, 0.995);
        const double Factor = std::pow(-std::log(1.0 - U), 1.0 / static_cast<double>(Material.WeibullModulus));
        VoxelWeibullFt[Index] = std::max(FtMPa * 0.28, FtMPa * Factor * VolumeScale);
    }

    PrincipalStressEigen EigenScratch {};
    double PeakSigma1MPa   = 0.0;
    double PeakVonMisesMPa = 0.0;
    double PeakKI          = 0.0;
    double PeakMohrRatio   = 0.0;

    for (uint32_t Index = 0u; Index < Lattice.Total; ++Index)
    {
        if (!Lattice.Active[Index]) continue;
        const uint32_t B = Index * 6u;
        SolveSymmetricEigen3x3(
            Lattice.StressTensor[B + 0u], Lattice.StressTensor[B + 1u], Lattice.StressTensor[B + 2u],
            Lattice.StressTensor[B + 3u], Lattice.StressTensor[B + 4u], Lattice.StressTensor[B + 5u],
            EigenScratch);
        PeakSigma1MPa   = std::max(PeakSigma1MPa, EigenScratch.Sigma1);
        PeakVonMisesMPa = std::max(PeakVonMisesMPa, EigenScratch.VonMisesMPa);

        const double FlawA = std::max(0.0015, Lattice.CellSize * 0.35);
        const double KI    = 1.12 * std::max(0.0, EigenScratch.Sigma1) * std::sqrt(PiConst * FlawA);
        PeakKI = std::max(PeakKI, KI);

        const double TauMax  = 0.5 * (EigenScratch.Sigma1 - EigenScratch.Sigma3);
        const double SigMean = 0.5 * (EigenScratch.Sigma1 + EigenScratch.Sigma3);
        const double McDenom = std::max(0.5, CohesionMPa * CosPhi - SigMean * SinPhi);
        PeakMohrRatio = std::max(PeakMohrRatio, TauMax / McDenom);
    }

    const double ResidualPool      = static_cast<double>(Material.StoredEnergyJPerM3) * TotalVol0;
    const double TotalEnergyBudget = ImpactEnergy + ResidualPool;
    double       RemainingEnergy   = TotalEnergyBudget;
    double       SpentEnergy       = 0.0;
    uint32_t     ModeICount        = 0u;
    uint32_t     ModeIICount       = 0u;
    uint32_t     TJunctionCount    = 0u;

    int32_t TokenCounter = 100;
    std::unordered_map<int32_t, CutPlaneEntry>                      CutRegistry;
    std::unordered_map<int32_t, std::vector<CrossingPlaneEquation>> CutCrossingPlanes;

    auto RegisterCut = [&](int32_t Tok, double Nx, double Ny, double Nz, double NucX, double NucY, double NucZ, double MaxR)
    {
        double Cnx = Nx, Cny = Ny, Cnz = Nz;
        if (Cnx < -1.0e-9 || (std::abs(Cnx) <= 1.0e-9 && (Cny < -1.0e-9 || (std::abs(Cny) <= 1.0e-9 && Cnz < 0.0))))
        {
            Cnx = -Cnx; Cny = -Cny; Cnz = -Cnz;
        }
        double Ux = 0.0, Uy = Cnz, Uz = -Cny;
        if (std::abs(Cnx) >= 0.85) { Ux = -Cnz; Uy = 0.0; Uz = Cnx; }
        const double UInv = 1.0 / std::hypot(Ux, Uy, Uz);
        Ux *= UInv; Uy *= UInv; Uz *= UInv;
        const double Vx = Cny * Uz - Cnz * Uy;
        const double Vy = Cnz * Ux - Cnx * Uz;
        const double Vz = Cnx * Uy - Cny * Ux;

        CutRegistry[Tok] = {
            Tok, Cnx, Cny, Cnz, Ux, Uy, Uz, Vx, Vy, Vz,
            NucX, NucY, NucZ, MaxR, (Tok & 1) ? 1.0 : -1.0
        };
    };

    struct ActiveShard
    {
        FlatConvexCell Cell       {};
        uint32_t       Generation = 0u;
    };

    auto Scratch = std::make_unique<FlatClipScratch>();
    std::vector<ActiveShard> ActiveShards;
    ActiveShards.push_back({ std::move(RootCell), 0u });
    const uint32_t MaxIterations = Ceiling * 6u;
    uint32_t Iter = 0u;

    while (ActiveShards.size() < Ceiling && RemainingEnergy > 0.0 && Iter++ < MaxIterations)
    {
        int32_t BestShardIdx = -1;
        double  BestScore    = -1.0e30;
        double  BestNx = 1.0, BestNy = 0.0, BestNz = 0.0;
        double  BestPx = 0.0, BestPy = 0.0, BestPz = 0.0;
        bool    BestIsShear  = false;

        for (size_t S = 0u; S < ActiveShards.size(); ++S)
        {
            const FlatConvexCell& Cell = ActiveShards[S].Cell;
            if (Cell.VolumeM3 < 1.0e-6 || Cell.MinSpanM < MinSpan * 0.82) continue;

            const int32_t Gx = std::clamp(static_cast<int32_t>(std::floor(((Cell.CentroidX - Lattice.MinX) / Lattice.SpanX) * Lattice.GridN)), 0, static_cast<int32_t>(Lattice.GridN) - 1);
            const int32_t Gy = std::clamp(static_cast<int32_t>(std::floor(((Cell.CentroidY - Lattice.MinY) / Lattice.SpanY) * Lattice.GridN)), 0, static_cast<int32_t>(Lattice.GridN) - 1);
            const int32_t Gz = std::clamp(static_cast<int32_t>(std::floor(((Cell.CentroidZ - Lattice.MinZ) / Lattice.SpanZ) * Lattice.GridN)), 0, static_cast<int32_t>(Lattice.GridN) - 1);

            double LocalPeak = -1.0e30;
            double Lnx = 1.0, Lny = 0.0, Lnz = 0.0;
            double Lpx = Cell.CentroidX, Lpy = Cell.CentroidY, Lpz = Cell.CentroidZ;
            bool   LShear = false;

            for (int32_t Dz = -1; Dz <= 1; ++Dz)
            {
                const int32_t Iz = Gz + Dz;
                if (Iz < 0 || Iz >= static_cast<int32_t>(Lattice.GridN)) continue;
                for (int32_t Dy = -1; Dy <= 1; ++Dy)
                {
                    const int32_t Iy = Gy + Dy;
                    if (Iy < 0 || Iy >= static_cast<int32_t>(Lattice.GridN)) continue;
                    for (int32_t Dx = -1; Dx <= 1; ++Dx)
                    {
                        const int32_t Ix = Gx + Dx;
                        if (Ix < 0 || Ix >= static_cast<int32_t>(Lattice.GridN)) continue;
                        const uint32_t VIdx = (static_cast<uint32_t>(Iz) * Lattice.GridN + static_cast<uint32_t>(Iy)) * Lattice.GridN + static_cast<uint32_t>(Ix);
                        if (!Lattice.Active[VIdx]) continue;

                        const double Vx = Lattice.Positions[VIdx * 3u + 0u];
                        const double Vy = Lattice.Positions[VIdx * 3u + 1u];
                        const double Vz = Lattice.Positions[VIdx * 3u + 2u];
                        if (Vx < Cell.MinX || Vx > Cell.MaxX || Vy < Cell.MinY || Vy > Cell.MaxY || Vz < Cell.MinZ || Vz > Cell.MaxZ) continue;

                        const uint32_t B = VIdx * 6u;
                        SolveSymmetricEigen3x3(
                            Lattice.StressTensor[B + 0u], Lattice.StressTensor[B + 1u], Lattice.StressTensor[B + 2u],
                            Lattice.StressTensor[B + 3u], Lattice.StressTensor[B + 4u], Lattice.StressTensor[B + 5u],
                            EigenScratch);

                        const double RankineIdx = std::max(0.05, EigenScratch.Sigma1) / std::max(0.2, VoxelWeibullFt[VIdx]);
                        const double TauMax     = 0.5 * (EigenScratch.Sigma1 - EigenScratch.Sigma3);
                        const double SigMean    = 0.5 * (EigenScratch.Sigma1 + EigenScratch.Sigma3);
                        const double MohrIdx    = TauMax / std::max(0.5, CohesionMPa * CosPhi - SigMean * SinPhi);
                        const double Priority   = std::max(RankineIdx, MohrIdx * 0.92) * std::pow(Cell.VolumeM3 / std::max(1.0e-9, TotalVol0), 0.28);

                        if (Priority > LocalPeak)
                        {
                            LocalPeak = Priority;
                            Lpx       = 0.65 * Cell.CentroidX + 0.35 * Vx;
                            Lpy       = 0.65 * Cell.CentroidY + 0.35 * Vy;
                            Lpz       = 0.65 * Cell.CentroidZ + 0.35 * Vz;

                            if (MohrIdx * 0.92 > RankineIdx && Material.Category != FractureMaterialCategory::Fibrous)
                            {
                                const double Sign = ((Iter + static_cast<uint32_t>(S)) & 1u) ? 1.0 : -1.0;
                                const double Ct   = std::cos(ThetaMohrCoulomb);
                                const double St   = std::sin(ThetaMohrCoulomb) * Sign;
                                Lnx    = EigenScratch.E1X * Ct + EigenScratch.E3X * St;
                                Lny    = EigenScratch.E1Y * Ct + EigenScratch.E3Y * St;
                                Lnz    = EigenScratch.E1Z * Ct + EigenScratch.E3Z * St;
                                LShear = true;
                            }
                            else
                            {
                                Lnx    = EigenScratch.E1X;
                                Lny    = EigenScratch.E1Y;
                                Lnz    = EigenScratch.E1Z;
                                LShear = false;
                            }
                        }
                    }
                }
            }

            if (LocalPeak < -1.0e20)
            {
                const double DImp = std::hypot(Cell.CentroidX - Ipx, Cell.CentroidY - Ipy, Cell.CentroidZ - Ipz);
                LocalPeak = (1.2 / (0.2 + DImp)) * std::pow(Cell.VolumeM3 / std::max(1.0e-9, TotalVol0), 0.35);
                Lnx       = -(Cell.CentroidY - Ipy);
                Lny       = (Cell.CentroidX - Ipx);
                Lnz       = (Rand.NextDouble() - 0.5) * 0.4;
                if (std::hypot(Lnx, Lny, Lnz) < 1.0e-4) { Lnx = 1.0; Lny = 0.0; Lnz = 0.0; }
            }

            if (LocalPeak > BestScore)
            {
                BestScore    = LocalPeak;
                BestShardIdx = static_cast<int32_t>(S);
                BestNx       = Lnx; BestNy = Lny; BestNz = Lnz;
                BestPx       = Lpx; BestPy = Lpy; BestPz = Lpz;
                BestIsShear  = LShear;
            }
        }

        if (BestShardIdx < 0) break;
        ActiveShard&   Target = ActiveShards[static_cast<size_t>(BestShardIdx)];
        FlatConvexCell& Cell  = Target.Cell;

        if (Material.Category == FractureMaterialCategory::Fibrous)
        {
            const double Gx = std::cos(GrainRad), Gy = std::sin(GrainRad), Gz = 0.05;
            const double DImp = std::hypot(Cell.CentroidX - Ipx, Cell.CentroidY - Ipy, Cell.CentroidZ - Ipz);
            if (Rand.NextDouble() > 0.14 * std::max(0.0, 1.0 - DImp / SolidRadius))
            {
                const double DotG = BestNx * Gx + BestNy * Gy + BestNz * Gz;
                BestNx -= Gx * DotG * 0.94;
                BestNy -= Gy * DotG * 0.94;
                BestNz -= Gz * DotG * 0.94;
            }
        }
        else if (Material.BeddingWeakness > 0.0f && Rand.NextDouble() < 0.38 + static_cast<double>(Material.BeddingWeakness) * 0.35)
        {
            const double Bx = -std::sin(GrainRad), By = std::cos(GrainRad);
            BestNx = BestNx * 0.25 + Bx * 0.75;
            BestNy = BestNy * 0.25 + By * 0.75;
            BestNz *= 0.25;
        }

        BestNx += (Rand.NextDouble() - 0.5) * static_cast<double>(Material.CrackJitter) * 0.45;
        BestNy += (Rand.NextDouble() - 0.5) * static_cast<double>(Material.CrackJitter) * 0.45;
        BestNz += (Rand.NextDouble() - 0.5) * static_cast<double>(Material.CrackJitter) * 0.45;
        const double BnLen = std::hypot(BestNx, BestNy, BestNz);
        const double BnInv = 1.0 / (BnLen > 1.0e-12 ? BnLen : 1.0);
        BestNx *= BnInv; BestNy *= BnInv; BestNz *= BnInv;

        const double Px     = 0.78 * Cell.CentroidX + 0.22 * BestPx + (Rand.NextDouble() - 0.5) * 0.14 * Cell.RadiusM;
        const double Py     = 0.78 * Cell.CentroidY + 0.22 * BestPy + (Rand.NextDouble() - 0.5) * 0.14 * Cell.RadiusM;
        const double Pz     = 0.78 * Cell.CentroidZ + 0.22 * BestPz + (Rand.NextDouble() - 0.5) * 0.14 * Cell.RadiusM;
        const double DPlane = BestNx * Px + BestNy * Py + BestNz * Pz;

        const int32_t Tok = ++TokenCounter;
        if (!FastRuntimeFracture::ClipFlatConvexCell(Cell, BestNx, BestNy, BestNz, DPlane, Tok, *Scratch))
        {
            Cell.MinSpanM *= 0.65;
            continue;
        }

        if (Scratch->FrontCell.MinSpanM < MinSpan * 0.38 || Scratch->BackCell.MinSpanM < MinSpan * 0.38)
        {
            Cell.MinSpanM *= 0.72;
            continue;
        }

        double LocalGc = static_cast<double>(Material.FractureToughnessGc);
        if (Material.Category == FractureMaterialCategory::Fibrous)
        {
            const double Across = std::abs(BestNx * std::cos(GrainRad) + BestNy * std::sin(GrainRad));
            LocalGc *= 1.0 + (static_cast<double>(Material.AnisotropyRatio) - 1.0) * Across * Across;
        }
        else if (Material.BeddingWeakness > 0.0f)
        {
            const double Align = std::abs(-BestNx * std::sin(GrainRad) + BestNy * std::cos(GrainRad));
            LocalGc *= 1.0 - static_cast<double>(Material.BeddingWeakness) * std::pow(Align, 4.0);
        }
        if (BestIsShear) LocalGc *= 1.75;

        const double Cost = 2.0 * LocalGc * Scratch->CutAreaM2;
        if (Cost > RemainingEnergy && ActiveShards.size() >= 4u)
        {
            break;
        }

        RemainingEnergy = std::max(0.0, RemainingEnergy - Cost);
        SpentEnergy += Cost;
        if (BestIsShear) ++ModeIICount; else ++ModeICount;
        if (Target.Generation > 0u) ++TJunctionCount;

        for (uint32_t F = 0u; F < Cell.FaceCount; ++F)
        {
            const int32_t OldTok = Cell.FaceTokens[F];
            if (OldTok <= 0) continue;
            const int32_t S = Cell.FaceOffsets[F];
            const int32_t E = Cell.FaceOffsets[F + 1u];
            bool HasPos = false, HasNeg = false;
            for (int32_t K = S; K < E; ++K)
            {
                const int32_t Iv = Cell.FaceIndices[static_cast<size_t>(K)] * 3;
                const double  Dv = BestNx * Cell.Vertices[Iv] + BestNy * Cell.Vertices[Iv + 1] + BestNz * Cell.Vertices[Iv + 2] - DPlane;
                if (Dv > 1.0e-6) HasPos = true;
                else if (Dv < -1.0e-6) HasNeg = true;
            }
            if (HasPos && HasNeg)
            {
                CutCrossingPlanes[OldTok].push_back({ BestNx, BestNy, BestNz, DPlane });
            }
        }

        RegisterCut(Tok, BestNx, BestNy, BestNz, Px, Py, Pz, std::max(Scratch->FrontCell.RadiusM, Scratch->BackCell.RadiusM));
        RelaxStressTensorAroundCrack(Lattice, BestNx, BestNy, BestNz, DPlane, Cell, ReliefLengthM);

        const uint32_t NextGen = Target.Generation + 1u;
        ActiveShards[static_cast<size_t>(BestShardIdx)] = { std::move(Scratch->FrontCell), NextGen };
        ActiveShards.push_back({ std::move(Scratch->BackCell), NextGen });
    }

    std::sort(ActiveShards.begin(), ActiveShards.end(), [](const ActiveShard& A, const ActiveShard& B)
    {
        return A.Cell.VolumeM3 > B.Cell.VolumeM3;
    });

    std::vector<FlatConvexCell> FinalCells;
    FinalCells.reserve(ActiveShards.size());
    for (auto& Shard : ActiveShards) FinalCells.push_back(std::move(Shard.Cell));

    auto Fractography = BuildInterlockingEngineeringSurfaces(
        FinalCells, CutRegistry, CutCrossingPlanes, Material, EngSpec, GrainRad,
        Specification.Seed != 0u ? Specification.Seed : 42u);

    const double EjectionBase = std::sqrt(
        (2.0 * std::max(15.0, TotalEnergyBudget * 0.08))
        / std::max(0.8, TotalVol0 * static_cast<double>(Material.DensityKgPerM3)));

    double SumCrackArea = 0.0;
    std::vector<FracturePieceEntry> Fragments;
    Fragments.reserve(FinalCells.size());

    for (uint32_t Idx = 0u; Idx < FinalCells.size(); ++Idx)
    {
        const FlatConvexCell& Cell    = FinalCells[Idx];
        EngineeringSurface&   EngSurf = Fractography.Surfaces[Idx];
        SumCrackArea += Cell.CrackAreaM2;

        const double DImp = std::hypot(Cell.CentroidX - Ipx, Cell.CentroidY - Ipy, Cell.CentroidZ - Ipz);
        const double Prox = std::max(0.0, 1.0 - DImp / std::max(0.25, SolidRadius * 1.65));
        const double Mass = std::max(0.01, EngSurf.ExactNonPlanarVolM3 * static_cast<double>(Material.DensityKgPerM3));

        double Rx = (Cell.CentroidX - Ipx) + Inx * 0.35;
        double Ry = (Cell.CentroidY - Ipy) + Iny * 0.35;
        double Rz = (Cell.CentroidZ - Ipz) + Inz * 0.35;
        const double RLen = std::hypot(Rx, Ry, Rz);
        const double RInv = 1.0 / (RLen > 1.0e-12 ? RLen : 1.0);
        Rx *= RInv; Ry *= RInv; Rz *= RInv;
        const double EjectSpeed = std::min(18.0, EjectionBase * (0.28 + 1.45 * Prox * Prox));

        char TokenBuf[24] {};
        std::snprintf(TokenBuf, sizeof(TokenBuf), "ENG-%03u", Idx + 1u);

        FracturePieceEntry Entry {};
        Entry.Index          = Idx;
        Entry.Token          = TokenBuf;
        Entry.Polyhedron     = FastRuntimeFracture::UnpackFlatConvexCell(Cell);
        Entry.VolumeM3       = EngSurf.ExactNonPlanarVolM3;
        Entry.ConvexVolumeM3 = Cell.VolumeM3;
        Entry.DetailedSurface = std::move(EngSurf);
        Entry.HasDetailedSurf = true;
        Entry.MassKg         = Mass;
        Entry.Centroid       = { static_cast<float>(Cell.CentroidX), static_cast<float>(Cell.CentroidY), static_cast<float>(Cell.CentroidZ) };
        Entry.BoundsMin      = { static_cast<float>(Cell.MinX), static_cast<float>(Cell.MinY), static_cast<float>(Cell.MinZ) };
        Entry.BoundsMax      = { static_cast<float>(Cell.MaxX), static_cast<float>(Cell.MaxY), static_cast<float>(Cell.MaxZ) };
        Entry.Span           = {
            static_cast<float>(Cell.MaxX - Cell.MinX),
            static_cast<float>(Cell.MaxY - Cell.MinY),
            static_cast<float>(Cell.MaxZ - Cell.MinZ)
        };
        Entry.RadiusM        = Cell.RadiusM;
        Entry.CrackAreaM2    = Cell.CrackAreaM2;
        Entry.Proximity      = static_cast<float>(Prox);
        Entry.StressRatio    = static_cast<float>(std::min(1.0, 0.22 + Prox * 0.88));
        Entry.InitialVelocity = {
            static_cast<float>(Rx * EjectSpeed),
            static_cast<float>(Ry * EjectSpeed),
            static_cast<float>(Rz * EjectSpeed)
        };
        Entry.InitialAngular = {
            static_cast<float>((Rand.NextDouble() - 0.5) * 8.5 * (0.3 + Prox)),
            static_cast<float>((Rand.NextDouble() - 0.5) * 8.5 * (0.3 + Prox)),
            static_cast<float>((Rand.NextDouble() - 0.5) * 8.5 * (0.3 + Prox))
        };
        Entry.VisibilityEnabled = true;

        Fragments.push_back(std::move(Entry));
    }

    auto Bonds = FastRuntimeFracture::BuildTokenBucketBonds(
        FinalCells, Fragments, Material,
        { static_cast<float>(Ipx), static_cast<float>(Ipy), static_cast<float>(Ipz) },
        SolidRadius, BondScale * 1.18, TokenCounter);

    const double TotalCrackArea       = SumCrackArea * 0.5;
    const double NonPlanarVolumeDrift = TotalVol0 > 1.0e-9 ? std::abs(Fractography.NonPlanarVolumeSum - TotalVol0) / TotalVol0 : 0.0;
    const auto   EndClock             = std::chrono::steady_clock::now();
    const double ElapsedMs            = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();

    uint32_t IntactCount = 0u;
    for (const auto& Bond : Bonds)
    {
        if (!Bond.BrokenOnImpact) ++IntactCount;
    }

    FracturePartitionResult Result {};
    Result.Solver             = FractureSolverMethod::EngineeringBakedSolid;
    Result.MaterialSpec       = Material;
    Result.SourceMetrics      = SourceMetrics;
    Result.Fragments          = std::move(Fragments);
    Result.Bonds              = std::move(Bonds);
    Result.Receipt            = {
        static_cast<uint32_t>(Result.Fragments.size()),
        static_cast<uint32_t>(Result.Bonds.size()),
        IntactCount,
        TotalVol0,
        Fractography.NonPlanarVolumeSum,
        NonPlanarVolumeDrift,
        TotalCrackArea,
        TotalEnergyBudget,
        std::min(TotalEnergyBudget, SpentEnergy > 0.0 ? SpentEnergy : 2.0 * static_cast<double>(Material.FractureToughnessGc) * TotalCrackArea),
        FractureMaterialRepository::ComputeRayleighWaveSpeedMps(Material),
        ElapsedMs,
        NonPlanarVolumeDrift < 1.0e-3
    };
    Result.HasEngineering     = true;
    Result.EngineeringMetrics = {
        Lattice.GridN,
        PeakSigma1MPa,
        PeakVonMisesMPa,
        PeakKI,
        PeakMohrRatio,
        Lattice.ContactRadiusM,
        Lattice.PeakHertzPressureMPa,
        ModeICount,
        ModeIICount,
        TJunctionCount,
        Fractography.RoughnessRmsMm,
        Fractography.NonPlanarVolumeSum,
        NonPlanarVolumeDrift,
        EngSpec.FractographyLabel
    };
    return Result;
}

} // namespace Frontier::GeometrySpace::Fracture
