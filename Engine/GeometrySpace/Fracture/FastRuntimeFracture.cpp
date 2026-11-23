/*============================================================================================================================================
                                                        FASTRUNTIMEFRACTURE.CPP
============================================================================================================================================*/
// 🧩 Sub-millisecond preallocated flat-array convex fracture kernel, Boussinesq-Hertz stress, and CutToken bond builder

#include "Engine/GeometrySpace/Fracture/FastRuntimeFracture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <unordered_set>

namespace Frontier::GeometrySpace::Fracture
{

namespace
{

constexpr double ClipEpsilon = 1.0e-7;
constexpr double PiConst     = 3.14159265358979323846;

uint32_t FindOrInsertCapPoint(FlatClipScratch& Scratch, double Px, double Py, double Pz, uint32_t Count) noexcept
{
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const double Dx = Scratch.CapPts[Index * 3u + 0u] - Px;
        const double Dy = Scratch.CapPts[Index * 3u + 1u] - Py;
        const double Dz = Scratch.CapPts[Index * 3u + 2u] - Pz;
        if (Dx * Dx + Dy * Dy + Dz * Dz < 1.0e-10)
        {
            return Count;
        }
    }
    if (Count < FlatClipScratch::MaxCapPts)
    {
        Scratch.CapPts[Count * 3u + 0u] = Px;
        Scratch.CapPts[Count * 3u + 1u] = Py;
        Scratch.CapPts[Count * 3u + 2u] = Pz;
        return Count + 1u;
    }
    return Count;
}

uint32_t FindOrAppendScratchVert(double* Buffer, uint32_t VertCount, double Px, double Py, double Pz) noexcept
{
    const uint32_t ScanStart = VertCount > 24u ? VertCount - 24u : 0u;
    for (int64_t Index = static_cast<int64_t>(VertCount) - 1; Index >= static_cast<int64_t>(ScanStart); --Index)
    {
        const uint32_t Slot = static_cast<uint32_t>(Index);
        const double   Dx   = Buffer[Slot * 3u + 0u] - Px;
        const double   Dy   = Buffer[Slot * 3u + 1u] - Py;
        const double   Dz   = Buffer[Slot * 3u + 2u] - Pz;
        if (Dx * Dx + Dy * Dy + Dz * Dz < 1.0e-11)
        {
            return Slot;
        }
    }
    if (VertCount < FlatClipScratch::MaxVerts)
    {
        Buffer[VertCount * 3u + 0u] = Px;
        Buffer[VertCount * 3u + 1u] = Py;
        Buffer[VertCount * 3u + 2u] = Pz;
    }
    return VertCount;
}

void ComputeBoussinesqNormal(
    const FractureMaterialSpec& Material,
    double                      Cx,
    double                      Cy,
    double                      Cz,
    double                      Ipx,
    double                      Ipy,
    double                      Ipz,
    double                      Inx,
    double                      Iny,
    double                      Inz,
    double                      SolidRadius,
    double                      GrainRad,
    DeterministicRandomStream&  Rand,
    double&                     OutNx,
    double&                     OutNy,
    double&                     OutNz) noexcept
{
    const double Dx     = -Inx;
    const double Dy     = -Iny;
    const double Dz     = -Inz;
    const double Rx0    = Cx - Ipx;
    const double Ry0    = Cy - Ipy;
    const double Rz0    = Cz - Ipz;
    const double ZAxial = Rx0 * Dx + Ry0 * Dy + Rz0 * Dz;

    double Rpx = Rx0 - Dx * ZAxial;
    double Rpy = Ry0 - Dy * ZAxial;
    double Rpz = Rz0 - Dz * ZAxial;
    double R   = std::hypot(Rpx, Rpy, Rpz);
    if (R < 1.0e-5)
    {
        if (std::abs(Dx) < 0.85) { Rpx = 0.0; Rpy = Dz; Rpz = -Dy; }
        else { Rpx = -Dz; Rpy = 0.0; Rpz = Dx; }
        R = std::hypot(Rpx, Rpy, Rpz);
    }
    const double InvR = 1.0 / (R > 1.0e-12 ? R : 1.0);
    Rpx *= InvR;
    Rpy *= InvR;
    Rpz *= InvR;

    const double Hpx = Dy * Rpz - Dz * Rpy;
    const double Hpy = Dz * Rpx - Dx * Rpz;
    const double Hpz = Dx * Rpy - Dy * Rpx;

    double Nx = Hpx;
    double Ny = Hpy;
    double Nz = Hpz;

    if (Material.Category == FractureMaterialCategory::PlateBrittle)
    {
        if (Material.StoredEnergyJPerM3 > 0.0f)
        {
            Nx = Rand.NextDouble() * 2.0 - 1.0;
            Ny = Rand.NextDouble() * 2.0 - 1.0;
            Nz = Rand.NextDouble() * 2.0 - 1.0;
            const double Proj = (Nx * Dx + Ny * Dy + Nz * Dz) * 0.88;
            Nx -= Dx * Proj;
            Ny -= Dy * Proj;
            Nz -= Dz * Proj;
        }
        else
        {
            const double Ratio    = std::clamp(R / std::max(1.0e-4, SolidRadius * 0.45), 0.0, 1.0);
            const double RingProb = std::pow(Ratio, 1.4) * (0.35 + static_cast<double>(Material.RingBias));
            if (Rand.NextDouble() < RingProb)
            {
                Nx = Rpx;
                Ny = Rpy;
                Nz = Rpz;
            }
            const double Proj = (Nx * Dx + Ny * Dy + Nz * Dz) * 0.90;
            Nx -= Dx * Proj;
            Ny -= Dy * Proj;
            Nz -= Dz * Proj;
        }
    }
    else if (Material.Category == FractureMaterialCategory::Fibrous)
    {
        const double Gx   = std::cos(GrainRad);
        const double Gy   = std::sin(GrainRad);
        const double Gz   = 0.08;
        const double Tx   = Rand.NextDouble() * 2.0 - 1.0;
        const double Ty   = Rand.NextDouble() * 2.0 - 1.0;
        const double Tz   = Rand.NextDouble() * 2.0 - 1.0;
        Nx = Gy * Tz - Gz * Ty;
        Ny = Gz * Tx - Gx * Tz;
        Nz = Gx * Ty - Gy * Tx;
        const double Bend = std::max(0.0, 1.0 - R / std::max(1.0e-4, SolidRadius * 0.5));
        if (Rand.NextDouble() < 0.12 + 0.24 * Bend)
        {
            Nx = Gx + Hpx * 0.22;
            Ny = Gy + Hpy * 0.22;
            Nz = Gz + Hpz * 0.22;
        }
    }
    else if (Material.Category == FractureMaterialCategory::BulkBrittle)
    {
        const double Ratio    = std::clamp(R / std::max(1.0e-4, SolidRadius * 0.58), 0.0, 1.0);
        const double RingProb = std::pow(Ratio, 1.45) * static_cast<double>(Material.RingBias);
        if (R < SolidRadius * 0.24 && ZAxial > 0.0 && Material.HertzConeRad > 0.0f && Rand.NextDouble() < 0.40)
        {
            const double Ch = std::sin(static_cast<double>(Material.HertzConeRad));
            const double Sh = std::cos(static_cast<double>(Material.HertzConeRad));
            Nx = Rpx * Ch - Dx * Sh;
            Ny = Rpy * Ch - Dy * Sh;
            Nz = Rpz * Ch - Dz * Sh;
        }
        else if (Rand.NextDouble() < RingProb)
        {
            Nx = Rpx;
            Ny = Rpy;
            Nz = Rpz;
        }
        if (Material.BeddingWeakness > 0.0f && Rand.NextDouble() < 0.35 + static_cast<double>(Material.BeddingWeakness) * 0.4)
        {
            const double Bx = -std::sin(GrainRad);
            const double By = std::cos(GrainRad);
            Nx = Nx * 0.2 + Bx * 0.8;
            Ny = Ny * 0.2 + By * 0.8;
            Nz *= 0.2;
        }
    }
    else
    {
        const double T = Rand.NextDouble() * 0.5;
        Nx = Hpx * (1.0 - T) + Rpx * T;
        Ny = Hpy * (1.0 - T) + Rpy * T;
        Nz = Hpz * (1.0 - T) + Rpz * T;
    }

    const double Jitter = static_cast<double>(Material.CrackJitter);
    Nx += (Rand.NextDouble() - 0.5) * Jitter;
    Ny += (Rand.NextDouble() - 0.5) * Jitter;
    Nz += (Rand.NextDouble() - 0.5) * Jitter;
    const double Length = std::hypot(Nx, Ny, Nz);
    const double InvLen = 1.0 / (Length > 1.0e-12 ? Length : 1.0);
    OutNx = Nx * InvLen;
    OutNy = Ny * InvLen;
    OutNz = Nz * InvLen;
}

double ComputeFastDirectionalToughness(
    const FractureMaterialSpec& Material,
    double                      Nx,
    double                      Ny,
    double                      Nz,
    double                      GrainRad,
    DeterministicRandomStream&  Rand) noexcept
{
    double Gc = static_cast<double>(Material.FractureToughnessGc);
    if (Material.Category == FractureMaterialCategory::Fibrous)
    {
        const double Across = std::abs(Nx * std::cos(GrainRad) + Ny * std::sin(GrainRad));
        Gc *= 1.0 + (static_cast<double>(Material.AnisotropyRatio) - 1.0) * Across * Across;
    }
    if (Material.BeddingWeakness > 0.0f)
    {
        const double Align  = std::abs(-Nx * std::sin(GrainRad) + Ny * std::cos(GrainRad));
        const double Align2 = Align * Align;
        Gc *= 1.0 - static_cast<double>(Material.BeddingWeakness) * Align2 * Align2;
    }
    (void)Nz;
    const double Spread = 1.0 / std::max(2.0, static_cast<double>(Material.WeibullModulus));
    return Gc * (1.0 - Spread + 2.0 * Spread * Rand.NextDouble());
}

} // namespace

FlatConvexCell FastRuntimeFracture::PackFlatConvexCell(const ConvexPolyhedron& Polyhedron)
{
    FlatConvexCell Cell {};
    Cell.VertexCount = static_cast<uint32_t>(Polyhedron.Vertices.size());
    Cell.FaceCount   = static_cast<uint32_t>(Polyhedron.Faces.size());
    Cell.Vertices.resize(Cell.VertexCount * 3u);
    for (uint32_t Index = 0u; Index < Cell.VertexCount; ++Index)
    {
        const auto& Pt = Polyhedron.Vertices[Index];
        Cell.Vertices[Index * 3u + 0u] = static_cast<double>(Pt.X);
        Cell.Vertices[Index * 3u + 1u] = static_cast<double>(Pt.Y);
        Cell.Vertices[Index * 3u + 2u] = static_cast<double>(Pt.Z);
    }

    uint32_t TotalIndices = 0u;
    for (const auto& Face : Polyhedron.Faces)
    {
        TotalIndices += static_cast<uint32_t>(Face.Indices.size());
    }

    Cell.FaceOffsets.resize(Cell.FaceCount + 1u);
    Cell.FaceIndices.resize(TotalIndices);
    Cell.FaceTokens.resize(Cell.FaceCount);

    int32_t Cursor = 0;
    for (uint32_t FaceIndex = 0u; FaceIndex < Cell.FaceCount; ++FaceIndex)
    {
        Cell.FaceOffsets[FaceIndex] = Cursor;
        const auto& Face            = Polyhedron.Faces[FaceIndex];
        Cell.FaceTokens[FaceIndex]  = Face.Interior ? static_cast<int32_t>(Face.CutToken != 0u ? Face.CutToken : 1u) : 0;
        for (uint32_t VertIdx : Face.Indices)
        {
            Cell.FaceIndices[static_cast<size_t>(Cursor++)] = static_cast<int32_t>(VertIdx);
        }
    }
    Cell.FaceOffsets[Cell.FaceCount] = Cursor;
    ComputeFlatCellMetrics(Cell);
    return Cell;
}

ConvexPolyhedron FastRuntimeFracture::UnpackFlatConvexCell(const FlatConvexCell& Cell)
{
    ConvexPolyhedron Polyhedron {};
    Polyhedron.Vertices.resize(Cell.VertexCount);
    for (uint32_t Index = 0u; Index < Cell.VertexCount; ++Index)
    {
        Polyhedron.Vertices[Index] = {
            static_cast<float>(Cell.Vertices[Index * 3u + 0u]),
            static_cast<float>(Cell.Vertices[Index * 3u + 1u]),
            static_cast<float>(Cell.Vertices[Index * 3u + 2u])
        };
    }

    Polyhedron.Faces.resize(Cell.FaceCount);
    for (uint32_t FaceIndex = 0u; FaceIndex < Cell.FaceCount; ++FaceIndex)
    {
        const int32_t Start = Cell.FaceOffsets[FaceIndex];
        const int32_t End   = Cell.FaceOffsets[FaceIndex + 1u];
        auto&         Face  = Polyhedron.Faces[FaceIndex];
        Face.Indices.resize(static_cast<size_t>(std::max(0, End - Start)));
        for (int32_t Cursor = Start; Cursor < End; ++Cursor)
        {
            Face.Indices[static_cast<size_t>(Cursor - Start)] = static_cast<uint32_t>(Cell.FaceIndices[static_cast<size_t>(Cursor)]);
        }
        const int32_t Tok = Cell.FaceTokens[FaceIndex];
        Face.Interior     = Tok > 0;
        Face.CutToken     = Tok > 0 ? static_cast<uint32_t>(Tok) : 0u;
    }
    return Polyhedron;
}

void FastRuntimeFracture::ComputeFlatCellMetrics(FlatConvexCell& Cell) noexcept
{
    if (Cell.VertexCount < 4u || Cell.FaceCount < 4u)
    {
        Cell.VolumeM3 = 0.0;
        return;
    }

    const double* V = Cell.Vertices.data();
    double MinX = std::numeric_limits<double>::infinity();
    double MinY = std::numeric_limits<double>::infinity();
    double MinZ = std::numeric_limits<double>::infinity();
    double MaxX = -std::numeric_limits<double>::infinity();
    double MaxY = -std::numeric_limits<double>::infinity();
    double MaxZ = -std::numeric_limits<double>::infinity();

    for (uint32_t Index = 0u; Index < Cell.VertexCount; ++Index)
    {
        const double X = V[Index * 3u + 0u];
        const double Y = V[Index * 3u + 1u];
        const double Z = V[Index * 3u + 2u];
        MinX = std::min(MinX, X); MaxX = std::max(MaxX, X);
        MinY = std::min(MinY, Y); MaxY = std::max(MaxY, Y);
        MinZ = std::min(MinZ, Z); MaxZ = std::max(MaxZ, Z);
    }

    Cell.MinX = MinX; Cell.MinY = MinY; Cell.MinZ = MinZ;
    Cell.MaxX = MaxX; Cell.MaxY = MaxY; Cell.MaxZ = MaxZ;
    Cell.MinSpanM = std::min({ MaxX - MinX, MaxY - MinY, MaxZ - MinZ });

    const double Ox = V[0], Oy = V[1], Oz = V[2];
    double Vol6 = 0.0;
    double CxAcc = 0.0, CyAcc = 0.0, CzAcc = 0.0;
    double CrackArea = 0.0;

    for (uint32_t FaceIndex = 0u; FaceIndex < Cell.FaceCount; ++FaceIndex)
    {
        const int32_t Start = Cell.FaceOffsets[FaceIndex];
        const int32_t End   = Cell.FaceOffsets[FaceIndex + 1u];
        if (End - Start < 3)
        {
            continue;
        }
        const bool    IsInterior = Cell.FaceTokens[FaceIndex] > 0;
        const int32_t I0         = Cell.FaceIndices[static_cast<size_t>(Start)] * 3;
        const double  Ax         = V[I0] - Ox;
        const double  Ay         = V[I0 + 1] - Oy;
        const double  Az         = V[I0 + 2] - Oz;

        for (int32_t Cursor = Start + 1; Cursor + 1 < End; ++Cursor)
        {
            const int32_t I1  = Cell.FaceIndices[static_cast<size_t>(Cursor)] * 3;
            const int32_t I2  = Cell.FaceIndices[static_cast<size_t>(Cursor + 1)] * 3;
            const double  Bx  = V[I1] - Ox, By = V[I1 + 1] - Oy, Bz = V[I1 + 2] - Oz;
            const double  Dx  = V[I2] - Ox, Dy = V[I2 + 1] - Oy, Dz = V[I2 + 2] - Oz;
            const double  Crx = By * Dz - Bz * Dy;
            const double  Cry = Bz * Dx - Bx * Dz;
            const double  Crz = Bx * Dy - By * Dx;
            const double  Det = Ax * Crx + Ay * Cry + Az * Crz;
            Vol6  += Det;
            CxAcc += Det * (Ox + V[I0] + V[I1] + V[I2]) * 0.25;
            CyAcc += Det * (Oy + V[I0 + 1] + V[I1 + 1] + V[I2 + 1]) * 0.25;
            CzAcc += Det * (Oz + V[I0 + 2] + V[I1 + 2] + V[I2 + 2]) * 0.25;

            if (IsInterior)
            {
                const double E1x = Bx - Ax, E1y = By - Ay, E1z = Bz - Az;
                const double E2x = Dx - Ax, E2y = Dy - Ay, E2z = Dz - Az;
                CrackArea += 0.5 * std::hypot(
                    E1y * E2z - E1z * E2y,
                    E1z * E2x - E1x * E2z,
                    E1x * E2y - E1y * E2x);
            }
        }
    }

    Cell.CrackAreaM2 = CrackArea;
    if (std::abs(Vol6) > 1.0e-12)
    {
        Cell.VolumeM3 = std::abs(Vol6) / 6.0;
        const double Inv = 1.0 / Vol6;
        Cell.CentroidX   = CxAcc * Inv;
        Cell.CentroidY   = CyAcc * Inv;
        Cell.CentroidZ   = CzAcc * Inv;
    }
    else
    {
        Cell.VolumeM3  = 0.0;
        Cell.CentroidX = 0.5 * (MinX + MaxX);
        Cell.CentroidY = 0.5 * (MinY + MaxY);
        Cell.CentroidZ = 0.5 * (MinZ + MaxZ);
    }

    double R2Max = 0.0;
    for (uint32_t Index = 0u; Index < Cell.VertexCount; ++Index)
    {
        const double Dx = V[Index * 3u + 0u] - Cell.CentroidX;
        const double Dy = V[Index * 3u + 1u] - Cell.CentroidY;
        const double Dz = V[Index * 3u + 2u] - Cell.CentroidZ;
        R2Max           = std::max(R2Max, Dx * Dx + Dy * Dy + Dz * Dz);
    }
    Cell.RadiusM = std::sqrt(R2Max);
}

bool FastRuntimeFracture::ClipFlatConvexCell(
    const FlatConvexCell& Cell,
    double                NormalX,
    double                NormalY,
    double                NormalZ,
    double                PlaneOffset,
    int32_t               CutToken,
    FlatClipScratch&      Scratch)
{
    Scratch.CutAreaM2 = 0.0;
    const uint32_t VCount = Cell.VertexCount;
    if (VCount == 0u || VCount >= FlatClipScratch::MaxVerts || Cell.FaceCount >= FlatClipScratch::MaxFaces)
    {
        return false;
    }

    const double* V        = Cell.Vertices.data();
    uint32_t      PosCount = 0u;
    uint32_t      NegCount = 0u;

    for (uint32_t Index = 0u; Index < VCount; ++Index)
    {
        const double Dist = NormalX * V[Index * 3u + 0u]
                          + NormalY * V[Index * 3u + 1u]
                          + NormalZ * V[Index * 3u + 2u]
                          - PlaneOffset;
        Scratch.Dist[Index] = Dist;
        if (Dist > ClipEpsilon)       { Scratch.Side[Index] =  1; ++PosCount; }
        else if (Dist < -ClipEpsilon) { Scratch.Side[Index] = -1; ++NegCount; }
        else                          { Scratch.Side[Index] =  0; }
        Scratch.FrontRemap[Index] = -1;
        Scratch.BackRemap[Index]  = -1;
    }

    if (PosCount == 0u || NegCount == 0u)
    {
        return false;
    }

    uint32_t FvCount = 0u;
    uint32_t BvCount = 0u;
    for (uint32_t Index = 0u; Index < VCount; ++Index)
    {
        const int8_t Side = Scratch.Side[Index];
        const double X    = V[Index * 3u + 0u];
        const double Y    = V[Index * 3u + 1u];
        const double Z    = V[Index * 3u + 2u];
        if (Side >= 0)
        {
            Scratch.FrontRemap[Index]             = static_cast<int32_t>(FvCount);
            Scratch.FrontVerts[FvCount * 3u + 0u] = X;
            Scratch.FrontVerts[FvCount * 3u + 1u] = Y;
            Scratch.FrontVerts[FvCount * 3u + 2u] = Z;
            ++FvCount;
        }
        if (Side <= 0)
        {
            Scratch.BackRemap[Index]             = static_cast<int32_t>(BvCount);
            Scratch.BackVerts[BvCount * 3u + 0u] = X;
            Scratch.BackVerts[BvCount * 3u + 1u] = Y;
            Scratch.BackVerts[BvCount * 3u + 2u] = Z;
            ++BvCount;
        }
    }

    uint32_t FFaceCount = 0u, FIdxCursor = 0u;
    uint32_t BFaceCount = 0u, BIdxCursor = 0u;
    uint32_t CapCount   = 0u;

    for (uint32_t FaceIndex = 0u; FaceIndex < Cell.FaceCount; ++FaceIndex)
    {
        const int32_t Start = Cell.FaceOffsets[FaceIndex];
        const int32_t End   = Cell.FaceOffsets[FaceIndex + 1u];
        const int32_t Len   = End - Start;
        if (Len < 3)
        {
            continue;
        }

        uint32_t FfLen = 0u;
        uint32_t BfLen = 0u;
        for (int32_t K = 0; K < Len; ++K)
        {
            const uint32_t Curr = static_cast<uint32_t>(Cell.FaceIndices[static_cast<size_t>(Start + K)]);
            const uint32_t Next = static_cast<uint32_t>(Cell.FaceIndices[static_cast<size_t>(Start + ((K + 1) % Len))]);
            const double   Dc   = Scratch.Dist[Curr];
            const double   Dn   = Scratch.Dist[Next];
            const int8_t   Sc   = Scratch.Side[Curr];
            const int8_t   Sn   = Scratch.Side[Next];

            if (Sc >= 0 && FfLen < FlatClipScratch::MaxCapPts)
            {
                const int32_t IdxF = Scratch.FrontRemap[Curr];
                if (FfLen == 0u || Scratch.FaceFront[FfLen - 1u] != IdxF) Scratch.FaceFront[FfLen++] = IdxF;
            }
            if (Sc <= 0 && BfLen < FlatClipScratch::MaxCapPts)
            {
                const int32_t IdxB = Scratch.BackRemap[Curr];
                if (BfLen == 0u || Scratch.FaceBack[BfLen - 1u] != IdxB) Scratch.FaceBack[BfLen++] = IdxB;
            }
            if (Sc == 0)
            {
                CapCount = FindOrInsertCapPoint(Scratch, V[Curr * 3u], V[Curr * 3u + 1u], V[Curr * 3u + 2u], CapCount);
            }

            if ((Sc > 0 && Sn < 0) || (Sc < 0 && Sn > 0))
            {
                const double T  = Dc / (Dc - Dn);
                const double Ix = V[Curr * 3u + 0u] + (V[Next * 3u + 0u] - V[Curr * 3u + 0u]) * T;
                const double Iy = V[Curr * 3u + 1u] + (V[Next * 3u + 1u] - V[Curr * 3u + 1u]) * T;
                const double Iz = V[Curr * 3u + 2u] + (V[Next * 3u + 2u] - V[Curr * 3u + 2u]) * T;

                const uint32_t FId = FindOrAppendScratchVert(Scratch.FrontVerts, FvCount, Ix, Iy, Iz);
                if (FId == FvCount && FvCount < FlatClipScratch::MaxVerts) ++FvCount;
                if (FfLen < FlatClipScratch::MaxCapPts && (FfLen == 0u || Scratch.FaceFront[FfLen - 1u] != static_cast<int32_t>(FId)))
                {
                    Scratch.FaceFront[FfLen++] = static_cast<int32_t>(FId);
                }

                const uint32_t BId = FindOrAppendScratchVert(Scratch.BackVerts, BvCount, Ix, Iy, Iz);
                if (BId == BvCount && BvCount < FlatClipScratch::MaxVerts) ++BvCount;
                if (BfLen < FlatClipScratch::MaxCapPts && (BfLen == 0u || Scratch.FaceBack[BfLen - 1u] != static_cast<int32_t>(BId)))
                {
                    Scratch.FaceBack[BfLen++] = static_cast<int32_t>(BId);
                }

                CapCount = FindOrInsertCapPoint(Scratch, Ix, Iy, Iz, CapCount);
            }
        }

        if (FfLen > 1u && Scratch.FaceFront[0] == Scratch.FaceFront[FfLen - 1u]) --FfLen;
        if (BfLen > 1u && Scratch.FaceBack[0] == Scratch.FaceBack[BfLen - 1u])   --BfLen;

        if (FfLen >= 3u && FFaceCount + 1u < FlatClipScratch::MaxFaces && FIdxCursor + FfLen < FlatClipScratch::MaxIndices)
        {
            Scratch.FrontFaceOff[FFaceCount] = static_cast<int32_t>(FIdxCursor);
            Scratch.FrontTokens[FFaceCount]  = Cell.FaceTokens[FaceIndex];
            for (uint32_t J = 0u; J < FfLen; ++J) Scratch.FrontFaceIdx[FIdxCursor++] = Scratch.FaceFront[J];
            ++FFaceCount;
        }
        if (BfLen >= 3u && BFaceCount + 1u < FlatClipScratch::MaxFaces && BIdxCursor + BfLen < FlatClipScratch::MaxIndices)
        {
            Scratch.BackFaceOff[BFaceCount] = static_cast<int32_t>(BIdxCursor);
            Scratch.BackTokens[BFaceCount]  = Cell.FaceTokens[FaceIndex];
            for (uint32_t J = 0u; J < BfLen; ++J) Scratch.BackFaceIdx[BIdxCursor++] = Scratch.FaceBack[J];
            ++BFaceCount;
        }
    }

    if (CapCount < 3u)
    {
        return false;
    }

    double Cx = 0.0, Cy = 0.0, Cz = 0.0;
    for (uint32_t Index = 0u; Index < CapCount; ++Index)
    {
        Cx += Scratch.CapPts[Index * 3u + 0u];
        Cy += Scratch.CapPts[Index * 3u + 1u];
        Cz += Scratch.CapPts[Index * 3u + 2u];
    }
    const double InvCap = 1.0 / static_cast<double>(CapCount);
    Cx *= InvCap;
    Cy *= InvCap;
    Cz *= InvCap;

    double Ux = 0.0, Uy = 0.0, Uz = 0.0;
    if (std::abs(NormalX) < 0.85) { Ux = 0.0; Uy = NormalZ; Uz = -NormalY; }
    else { Ux = -NormalZ; Uy = 0.0; Uz = NormalX; }
    const double ULen = 1.0 / std::hypot(Ux, Uy, Uz);
    Ux *= ULen; Uy *= ULen; Uz *= ULen;
    const double Vx = NormalY * Uz - NormalZ * Uy;
    const double Vy = NormalZ * Ux - NormalX * Uz;
    const double Vz = NormalX * Uy - NormalY * Ux;

    for (uint32_t Index = 0u; Index < CapCount; ++Index)
    {
        const double Dx = Scratch.CapPts[Index * 3u + 0u] - Cx;
        const double Dy = Scratch.CapPts[Index * 3u + 1u] - Cy;
        const double Dz = Scratch.CapPts[Index * 3u + 2u] - Cz;
        Scratch.CapAngles[Index] = std::atan2(Dx * Vx + Dy * Vy + Dz * Vz, Dx * Ux + Dy * Uy + Dz * Uz);
        Scratch.CapOrder[Index]  = static_cast<int32_t>(Index);
    }

    for (uint32_t I = 1u; I < CapCount; ++I)
    {
        const int32_t Ord = Scratch.CapOrder[I];
        const double  Ang = Scratch.CapAngles[Ord];
        int64_t       J   = static_cast<int64_t>(I) - 1;
        while (J >= 0 && Scratch.CapAngles[Scratch.CapOrder[J]] > Ang)
        {
            Scratch.CapOrder[J + 1] = Scratch.CapOrder[J];
            --J;
        }
        Scratch.CapOrder[J + 1] = Ord;
    }

    double CutArea = 0.0;
    const uint32_t O0  = static_cast<uint32_t>(Scratch.CapOrder[0]) * 3u;
    const double   P0x = Scratch.CapPts[O0], P0y = Scratch.CapPts[O0 + 1u], P0z = Scratch.CapPts[O0 + 2u];
    for (uint32_t I = 1u; I + 1u < CapCount; ++I)
    {
        const uint32_t O1  = static_cast<uint32_t>(Scratch.CapOrder[I]) * 3u;
        const uint32_t O2  = static_cast<uint32_t>(Scratch.CapOrder[I + 1u]) * 3u;
        const double   E1x = Scratch.CapPts[O1] - P0x, E1y = Scratch.CapPts[O1 + 1u] - P0y, E1z = Scratch.CapPts[O1 + 2u] - P0z;
        const double   E2x = Scratch.CapPts[O2] - P0x, E2y = Scratch.CapPts[O2 + 1u] - P0y, E2z = Scratch.CapPts[O2 + 2u] - P0z;
        CutArea += 0.5 * std::hypot(E1y * E2z - E1z * E2y, E1z * E2x - E1x * E2z, E1x * E2y - E1y * E2x);
    }
    if (CutArea < 1.0e-9)
    {
        return false;
    }
    Scratch.CutAreaM2 = CutArea;

    if (FFaceCount + 1u >= FlatClipScratch::MaxFaces || BFaceCount + 1u >= FlatClipScratch::MaxFaces ||
        FIdxCursor + CapCount >= FlatClipScratch::MaxIndices || BIdxCursor + CapCount >= FlatClipScratch::MaxIndices)
    {
        return false;
    }

    Scratch.FrontFaceOff[FFaceCount] = static_cast<int32_t>(FIdxCursor);
    Scratch.FrontTokens[FFaceCount]  = CutToken;
    for (int64_t I = static_cast<int64_t>(CapCount) - 1; I >= 0; --I)
    {
        const uint32_t Oi = static_cast<uint32_t>(Scratch.CapOrder[I]) * 3u;
        const uint32_t Id = FindOrAppendScratchVert(
            Scratch.FrontVerts, FvCount,
            Scratch.CapPts[Oi], Scratch.CapPts[Oi + 1u], Scratch.CapPts[Oi + 2u]);
        if (Id == FvCount && FvCount < FlatClipScratch::MaxVerts) ++FvCount;
        Scratch.FrontFaceIdx[FIdxCursor++] = static_cast<int32_t>(Id);
    }
    ++FFaceCount;
    Scratch.FrontFaceOff[FFaceCount] = static_cast<int32_t>(FIdxCursor);

    Scratch.BackFaceOff[BFaceCount] = static_cast<int32_t>(BIdxCursor);
    Scratch.BackTokens[BFaceCount]  = CutToken;
    for (uint32_t I = 0u; I < CapCount; ++I)
    {
        const uint32_t Oi = static_cast<uint32_t>(Scratch.CapOrder[I]) * 3u;
        const uint32_t Id = FindOrAppendScratchVert(
            Scratch.BackVerts, BvCount,
            Scratch.CapPts[Oi], Scratch.CapPts[Oi + 1u], Scratch.CapPts[Oi + 2u]);
        if (Id == BvCount && BvCount < FlatClipScratch::MaxVerts) ++BvCount;
        Scratch.BackFaceIdx[BIdxCursor++] = static_cast<int32_t>(Id);
    }
    ++BFaceCount;
    Scratch.BackFaceOff[BFaceCount] = static_cast<int32_t>(BIdxCursor);

    if (FFaceCount < 4u || BFaceCount < 4u)
    {
        return false;
    }

    auto PopulateCell = [](FlatConvexCell& Target, const double* Verts, uint32_t Vc, const int32_t* Off, uint32_t Fc, const int32_t* Idx, uint32_t Ic, const int32_t* Tok)
    {
        Target.VertexCount = Vc;
        Target.FaceCount   = Fc;
        Target.Vertices.assign(Verts, Verts + Vc * 3u);
        Target.FaceOffsets.assign(Off, Off + Fc + 1u);
        Target.FaceIndices.assign(Idx, Idx + Ic);
        Target.FaceTokens.assign(Tok, Tok + Fc);
        ComputeFlatCellMetrics(Target);
    };

    PopulateCell(Scratch.FrontCell, Scratch.FrontVerts, FvCount, Scratch.FrontFaceOff, FFaceCount, Scratch.FrontFaceIdx, FIdxCursor, Scratch.FrontTokens);
    PopulateCell(Scratch.BackCell, Scratch.BackVerts, BvCount, Scratch.BackFaceOff, BFaceCount, Scratch.BackFaceIdx, BIdxCursor, Scratch.BackTokens);

    return Scratch.FrontCell.VolumeM3 >= 1.0e-10 && Scratch.BackCell.VolumeM3 >= 1.0e-10;
}

std::vector<FractureBondEntry> FastRuntimeFracture::BuildTokenBucketBonds(
    const std::vector<FlatConvexCell>&     Cells,
    const std::vector<FracturePieceEntry>& Fragments,
    const FractureMaterialSpec&            Material,
    Mathematics::Axis3D                    ImpactPoint,
    double                                 SolidRadiusM,
    double                                 BondStrengthScale,
    int32_t                                MaxToken)
{
    struct TokenFaceRef
    {
        uint32_t FragIndex = 0u;
        double   Fcx       = 0.0;
        double   Fcy       = 0.0;
        double   Fcz       = 0.0;
        double   Area      = 0.0;
    };

    if (MaxToken <= 0)
    {
        return {};
    }

    std::vector<std::vector<TokenFaceRef>> Buckets(static_cast<size_t>(MaxToken) + 1u);
    for (uint32_t FragIdx = 0u; FragIdx < Cells.size(); ++FragIdx)
    {
        const FlatConvexCell& Cell = Cells[FragIdx];
        const double*         V    = Cell.Vertices.data();
        for (uint32_t F = 0u; F < Cell.FaceCount; ++F)
        {
            const int32_t Tok = Cell.FaceTokens[F];
            if (Tok <= 0 || Tok > MaxToken) continue;
            const int32_t S = Cell.FaceOffsets[F];
            const int32_t E = Cell.FaceOffsets[F + 1u];
            const int32_t N = E - S;
            if (N < 3) continue;

            double Fcx = 0.0, Fcy = 0.0, Fcz = 0.0, Area = 0.0;
            const int32_t I0  = Cell.FaceIndices[static_cast<size_t>(S)] * 3;
            const double  P0x = V[I0], P0y = V[I0 + 1], P0z = V[I0 + 2];
            for (int32_t K = S; K < E; ++K)
            {
                const int32_t Iv = Cell.FaceIndices[static_cast<size_t>(K)] * 3;
                Fcx += V[Iv]; Fcy += V[Iv + 1]; Fcz += V[Iv + 2];
            }
            const double InvN = 1.0 / static_cast<double>(N);
            Fcx *= InvN; Fcy *= InvN; Fcz *= InvN;

            for (int32_t K = S + 1; K + 1 < E; ++K)
            {
                const int32_t I1  = Cell.FaceIndices[static_cast<size_t>(K)] * 3;
                const int32_t I2  = Cell.FaceIndices[static_cast<size_t>(K + 1)] * 3;
                const double  E1x = V[I1] - P0x, E1y = V[I1 + 1] - P0y, E1z = V[I1 + 2] - P0z;
                const double  E2x = V[I2] - P0x, E2y = V[I2 + 1] - P0y, E2z = V[I2 + 2] - P0z;
                Area += 0.5 * std::hypot(E1y * E2z - E1z * E2y, E1z * E2x - E1x * E2z, E1x * E2y - E1y * E2x);
            }
            Buckets[static_cast<size_t>(Tok)].push_back({ FragIdx, Fcx, Fcy, Fcz, Area });
        }
    }

    std::vector<FractureBondEntry> Bonds;
    std::unordered_set<uint64_t>   SeenPairs;

    for (int32_t Tok = 1; Tok <= MaxToken; ++Tok)
    {
        const auto& List = Buckets[static_cast<size_t>(Tok)];
        if (List.size() < 2u) continue;
        for (size_t A = 0u; A < List.size(); ++A)
        {
            for (size_t B = A + 1u; B < List.size(); ++B)
            {
                const auto& EntryA = List[A];
                const auto& EntryB = List[B];
                if (EntryA.FragIndex == EntryB.FragIndex) continue;
                const uint32_t Lo  = std::min(EntryA.FragIndex, EntryB.FragIndex);
                const uint32_t Hi  = std::max(EntryA.FragIndex, EntryB.FragIndex);
                const uint64_t Key = (static_cast<uint64_t>(Lo) << 32u) | static_cast<uint64_t>(Hi);
                if (SeenPairs.contains(Key)) continue;

                const double DFace      = std::hypot(EntryA.Fcx - EntryB.Fcx, EntryA.Fcy - EntryB.Fcy, EntryA.Fcz - EntryB.Fcz);
                const double TouchLimit = std::sqrt(EntryA.Area + EntryB.Area) * 0.95 + 0.02;
                if (DFace <= TouchLimit)
                {
                    SeenPairs.insert(Key);
                    const auto&  Fa         = Fragments[Lo];
                    const auto&  Fb         = Fragments[Hi];
                    const double SharedArea = std::min(EntryA.Area, EntryB.Area);
                    const double Mx         = 0.5 * (static_cast<double>(Fa.Centroid.X) + static_cast<double>(Fb.Centroid.X));
                    const double My         = 0.5 * (static_cast<double>(Fa.Centroid.Y) + static_cast<double>(Fb.Centroid.Y));
                    const double Mz         = 0.5 * (static_cast<double>(Fa.Centroid.Z) + static_cast<double>(Fb.Centroid.Z));
                    const double DImp       = std::hypot(
                        Mx - static_cast<double>(ImpactPoint.X),
                        My - static_cast<double>(ImpactPoint.Y),
                        Mz - static_cast<double>(ImpactPoint.Z));
                    const double ImpactStress = 1.0 / (0.16 + std::pow(DImp / std::max(0.25, SolidRadiusM), 1.65));
                    const double Capacity     = (0.55 + 0.45 * (static_cast<double>(Material.TensileStrengthMPa) / 60.0))
                                              * BondStrengthScale
                                              * (0.65 + std::min(1.4, SharedArea * 4.0));
                    const double StrainRatio  = std::min(1.6, ImpactStress / std::max(0.15, Capacity * 2.4));

                    Bonds.push_back({
                        Lo, Hi,
                        Fa.Centroid, Fb.Centroid,
                        SharedArea,
                        SharedArea * static_cast<double>(Material.TensileStrengthMPa) * 1.0e3 * BondStrengthScale,
                        StrainRatio,
                        Material.StoredEnergyJPerM3 > 0.0f ? true : (StrainRatio >= 1.0)
                    });
                }
            }
        }
    }

    return Bonds;
}

FracturePartitionResult FastRuntimeFracture::Execute(
    const ConvexPolyhedron&      SourcePolyhedron,
    const FractureSpecification& Specification)
{
    const auto StartClock = std::chrono::steady_clock::now();
    const auto& Material  = FractureMaterialRepository::LookupMaterial(Specification.MaterialId);
    DeterministicRandomStream Rand(Specification.Seed != 0u ? Specification.Seed : 42u);

    const uint32_t Ceiling      = std::clamp(Specification.Ceiling, 2u, 256u);
    const double   ImpactEnergy = std::max(10.0, Specification.EnergyJoules);
    const double   MinSpan      = std::max(0.015, Specification.MinimumSizeM);
    const double   GrainRad     = Specification.GrainAngleDeg * (PiConst / 180.0);
    const double   BondScale    = Specification.BondStrengthScale > 0.0 ? Specification.BondStrengthScale : 1.0;

    FlatConvexCell RootCell     = PackFlatConvexCell(SourcePolyhedron);
    const double   TotalVol0    = RootCell.VolumeM3;
    const double   SolidRadius  = std::max(0.1, RootCell.RadiusM);

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

    const double ResidualPool      = static_cast<double>(Material.StoredEnergyJPerM3) * TotalVol0;
    const double TotalEnergyBudget = ImpactEnergy + ResidualPool;

    struct WorkCandidate
    {
        FlatConvexCell Cell       {};
        double         Energy     = 0.0;
        uint32_t       Generation = 0u;
    };

    auto Scratch = std::make_unique<FlatClipScratch>();
    int32_t TokenCounter = 10;
    std::vector<WorkCandidate> WorkQueue;
    std::vector<WorkCandidate> Completed;
    WorkQueue.push_back({ std::move(RootCell), TotalEnergyBudget, 0u });
    double SpentEnergy = 0.0;

    if (Material.HertzConeRad > 0.1f && TotalEnergyBudget > 80.0 && Ceiling >= 6u)
    {
        constexpr uint32_t Sides = 4u;
        const double ConeBudget  = TotalEnergyBudget * 0.22;
        const double Dx = -Inx, Dy = -Iny, Dz = -Inz;
        const double ContactR = std::max(0.03, SolidRadius * 0.12);
        const double Ax = Ipx - Dx * ContactR * 0.35;
        const double Ay = Ipy - Dy * ContactR * 0.35;
        const double Az = Ipz - Dz * ContactR * 0.35;

        double U0x = 0.0, U0y = Dz, U0z = -Dy;
        if (std::abs(Dx) >= 0.85) { U0x = -Dz; U0y = 0.0; U0z = Dx; }
        const double U0Inv = 1.0 / std::hypot(U0x, U0y, U0z);
        U0x *= U0Inv; U0y *= U0Inv; U0z *= U0Inv;
        const double U1x = Dy * U0z - Dz * U0y;
        const double U1y = Dz * U0x - Dx * U0z;
        const double U1z = Dx * U0y - Dy * U0x;

        FlatConvexCell CoreCell = std::move(WorkQueue.back().Cell);
        WorkQueue.pop_back();
        for (uint32_t S = 0u; S < Sides && (WorkQueue.size() + Completed.size() + 1u) < Ceiling; ++S)
        {
            const double Ang = (static_cast<double>(S) / static_cast<double>(Sides)) * PiConst * 2.0 + Rand.NextDouble() * 0.16;
            const double Ca  = std::cos(Ang), Sa = std::sin(Ang);
            const double Ch  = std::sin(static_cast<double>(Material.HertzConeRad));
            const double Sh  = std::cos(static_cast<double>(Material.HertzConeRad));
            double Pnx = (U0x * Ca + U1x * Sa) * Ch - Dx * Sh;
            double Pny = (U0y * Ca + U1y * Sa) * Ch - Dy * Sh;
            double Pnz = (U0z * Ca + U1z * Sa) * Ch - Dz * Sh;
            const double PInv = 1.0 / std::hypot(Pnx, Pny, Pnz);
            Pnx *= PInv; Pny *= PInv; Pnz *= PInv;
            const double  Pd  = Pnx * Ax + Pny * Ay + Pnz * Az;
            const int32_t Tok = ++TokenCounter;
            if (ClipFlatConvexCell(CoreCell, Pnx, Pny, Pnz, Pd, Tok, *Scratch))
            {
                SpentEnergy += 2.0 * static_cast<double>(Material.FractureToughnessGc) * Scratch->CutAreaM2;
                WorkQueue.push_back({ std::move(Scratch->FrontCell), ConeBudget / static_cast<double>(Sides), 1u });
                CoreCell = std::move(Scratch->BackCell);
            }
        }
        WorkQueue.push_back({ std::move(CoreCell), TotalEnergyBudget - ConeBudget * 0.5, 0u });
    }

    const uint32_t MaxIters = Ceiling * 5u;
    uint32_t Iter = 0u;
    while (!WorkQueue.empty() && (WorkQueue.size() + Completed.size()) < Ceiling && Iter++ < MaxIters)
    {
        size_t BestIdx   = 0u;
        double BestScore = -1.0e30;
        for (size_t I = 0u; I < WorkQueue.size(); ++I)
        {
            const double Score = (WorkQueue[I].Energy + WorkQueue[I].Cell.VolumeM3 * 350.0)
                               / (1.0 + static_cast<double>(WorkQueue[I].Generation) * 0.32);
            if (Score > BestScore) { BestScore = Score; BestIdx = I; }
        }

        WorkCandidate Candidate = std::move(WorkQueue[BestIdx]);
        WorkQueue[BestIdx]      = std::move(WorkQueue.back());
        WorkQueue.pop_back();

        const FlatConvexCell& Cell = Candidate.Cell;
        if (Cell.VolumeM3 < 1.0e-6 || Cell.MinSpanM < MinSpan * 0.8 || Candidate.Energy <= 0.0)
        {
            Completed.push_back({ std::move(Candidate.Cell), std::max(0.0, Candidate.Energy), Candidate.Generation });
            continue;
        }

        double Nx = 0.0, Ny = 0.0, Nz = 0.0;
        ComputeBoussinesqNormal(
            Material, Cell.CentroidX, Cell.CentroidY, Cell.CentroidZ,
            Ipx, Ipy, Ipz, Inx, Iny, Inz, SolidRadius, GrainRad, Rand, Nx, Ny, Nz);

        const double Jitter  = (Rand.NextDouble() - 0.5) * 0.24 * Cell.RadiusM;
        const double ToImpX  = Ipx - Cell.CentroidX;
        const double ToImpY  = Ipy - Cell.CentroidY;
        const double ToImpZ  = Ipz - Cell.CentroidZ;
        const double DistImp = std::hypot(ToImpX, ToImpY, ToImpZ);
        const double Pull    = 0.10 * std::max(0.0, 1.0 - DistImp / (SolidRadius + 1.0e-5));
        const double Fx      = Cell.CentroidX + ToImpX * Pull;
        const double Fy      = Cell.CentroidY + ToImpY * Pull;
        const double Fz      = Cell.CentroidZ + ToImpZ * Pull;
        const double PlaneD  = Nx * Fx + Ny * Fy + Nz * Fz + Jitter;

        const int32_t Tok = ++TokenCounter;
        if (!ClipFlatConvexCell(Cell, Nx, Ny, Nz, PlaneD, Tok, *Scratch))
        {
            Completed.push_back({ std::move(Candidate.Cell), std::max(0.0, Candidate.Energy), Candidate.Generation });
            continue;
        }

        if (Scratch->FrontCell.MinSpanM < MinSpan * 0.42 || Scratch->BackCell.MinSpanM < MinSpan * 0.42)
        {
            Completed.push_back({ std::move(Candidate.Cell), std::max(0.0, Candidate.Energy), Candidate.Generation });
            continue;
        }

        const double LocalGc = ComputeFastDirectionalToughness(Material, Nx, Ny, Nz, GrainRad, Rand);
        const double Cost    = 2.0 * LocalGc * Scratch->CutAreaM2;
        if (Cost > Candidate.Energy && (Completed.size() + WorkQueue.size()) >= 4u)
        {
            Completed.push_back({ std::move(Candidate.Cell), std::max(0.0, Candidate.Energy), Candidate.Generation });
            continue;
        }

        SpentEnergy += Cost;
        const double RemE = std::max(12.0, Candidate.Energy - Cost);
        const double Lam  = SolidRadius * 0.55;
        const double Df   = std::hypot(Scratch->FrontCell.CentroidX - Ipx, Scratch->FrontCell.CentroidY - Ipy, Scratch->FrontCell.CentroidZ - Ipz);
        const double Db   = std::hypot(Scratch->BackCell.CentroidX - Ipx, Scratch->BackCell.CentroidY - Ipy, Scratch->BackCell.CentroidZ - Ipz);
        const double Wf   = Scratch->FrontCell.VolumeM3 * std::exp(-Df / Lam);
        const double Wb   = Scratch->BackCell.VolumeM3 * std::exp(-Db / Lam);
        const double WSum = Wf + Wb + 1.0e-12;

        WorkQueue.push_back({ std::move(Scratch->FrontCell), RemE * (Wf / WSum), Candidate.Generation + 1u });
        WorkQueue.push_back({ std::move(Scratch->BackCell),  RemE * (Wb / WSum), Candidate.Generation + 1u });
    }

    for (auto& Remaining : WorkQueue)
    {
        Completed.push_back({ std::move(Remaining.Cell), std::max(0.0, Remaining.Energy), Remaining.Generation });
    }
    std::sort(Completed.begin(), Completed.end(), [](const WorkCandidate& A, const WorkCandidate& B)
    {
        return A.Cell.VolumeM3 > B.Cell.VolumeM3;
    });

    const double EjectionBase = std::sqrt(
        (2.0 * std::max(15.0, TotalEnergyBudget * 0.08))
        / std::max(0.8, TotalVol0 * static_cast<double>(Material.DensityKgPerM3)));

    double SumVolume    = 0.0;
    double SumCrackArea = 0.0;

    std::vector<FlatConvexCell>     Cells;
    std::vector<FracturePieceEntry> Fragments;
    Cells.reserve(Completed.size());
    Fragments.reserve(Completed.size());

    for (uint32_t Idx = 0u; Idx < Completed.size(); ++Idx)
    {
        const FlatConvexCell& Cell = Completed[Idx].Cell;
        SumVolume    += Cell.VolumeM3;
        SumCrackArea += Cell.CrackAreaM2;

        const double DImp = std::hypot(Cell.CentroidX - Ipx, Cell.CentroidY - Ipy, Cell.CentroidZ - Ipz);
        const double Prox = std::max(0.0, 1.0 - DImp / std::max(0.25, SolidRadius * 1.65));
        const double Mass = std::max(0.01, Cell.VolumeM3 * static_cast<double>(Material.DensityKgPerM3));

        double Rx = (Cell.CentroidX - Ipx) + Inx * 0.35;
        double Ry = (Cell.CentroidY - Ipy) + Iny * 0.35;
        double Rz = (Cell.CentroidZ - Ipz) + Inz * 0.35;
        const double RLen = std::hypot(Rx, Ry, Rz);
        const double RInv = 1.0 / (RLen > 1.0e-12 ? RLen : 1.0);
        Rx *= RInv; Ry *= RInv; Rz *= RInv;
        const double EjectSpeed = std::min(18.0, EjectionBase * (0.28 + 1.45 * Prox * Prox));

        char TokenBuf[24] {};
        std::snprintf(TokenBuf, sizeof(TokenBuf), "FRAG-%03u", Idx + 1u);

        FracturePieceEntry Entry {};
        Entry.Index          = Idx;
        Entry.Token          = TokenBuf;
        Entry.Polyhedron     = UnpackFlatConvexCell(Cell);
        Entry.VolumeM3       = Cell.VolumeM3;
        Entry.ConvexVolumeM3 = Cell.VolumeM3;
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
        Entry.StressRatio    = static_cast<float>(std::min(1.0, 0.18 + Prox * 0.92));
        Entry.InitialVelocity = {
            static_cast<float>(Rx * EjectSpeed),
            static_cast<float>(Ry * EjectSpeed),
            static_cast<float>(Rz * EjectSpeed)
        };
        Entry.InitialAngular = {
            static_cast<float>((Rand.NextDouble() - 0.5) * 9.0 * (0.3 + Prox)),
            static_cast<float>((Rand.NextDouble() - 0.5) * 9.0 * (0.3 + Prox)),
            static_cast<float>((Rand.NextDouble() - 0.5) * 9.0 * (0.3 + Prox))
        };
        Entry.VisibilityEnabled = true;

        Fragments.push_back(std::move(Entry));
        Cells.push_back(Cell);
    }

    auto Bonds = BuildTokenBucketBonds(
        Cells, Fragments, Material,
        { static_cast<float>(Ipx), static_cast<float>(Ipy), static_cast<float>(Ipz) },
        SolidRadius, BondScale, TokenCounter);

    const double TotalCrackArea   = SumCrackArea * 0.5;
    const double VolumeDriftRatio = TotalVol0 > 1.0e-9 ? std::abs(SumVolume - TotalVol0) / TotalVol0 : 0.0;
    const auto   EndClock         = std::chrono::steady_clock::now();
    const double ElapsedMs        = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();

    uint32_t IntactCount = 0u;
    for (const auto& Bond : Bonds)
    {
        if (!Bond.BrokenOnImpact) ++IntactCount;
    }

    FracturePartitionResult Result {};
    Result.Solver        = FractureSolverMethod::FastRuntimeBoussinesq;
    Result.MaterialSpec  = Material;
    Result.SourceMetrics = SourceMetrics;
    Result.Fragments     = std::move(Fragments);
    Result.Bonds         = std::move(Bonds);
    Result.Receipt       = {
        static_cast<uint32_t>(Result.Fragments.size()),
        static_cast<uint32_t>(Result.Bonds.size()),
        IntactCount,
        TotalVol0,
        SumVolume,
        VolumeDriftRatio,
        TotalCrackArea,
        TotalEnergyBudget,
        std::min(TotalEnergyBudget, SpentEnergy > 0.0 ? SpentEnergy : 2.0 * static_cast<double>(Material.FractureToughnessGc) * TotalCrackArea),
        FractureMaterialRepository::ComputeRayleighWaveSpeedMps(Material),
        ElapsedMs,
        VolumeDriftRatio < 1.0e-4
    };
    return Result;
}

} // namespace Frontier::GeometrySpace::Fracture
