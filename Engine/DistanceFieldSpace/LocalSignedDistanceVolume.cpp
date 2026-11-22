/*============================================================================================================================================
                                                       LOCALSIGNEDDISTANCEVOLUME.CPP
============================================================================================================================================*/
// 🧩 Order 2 GtxLowEnd-ONLY per-asset compact 48^3 distance volume with C0-continuous Euclidean outer-box distance clamping

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

namespace Frontier::DistanceFieldSpace
{

namespace
{

[[nodiscard]] uint16_t PackFloat32ToHalf16(float Scalar) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Scalar, sizeof(Bits));
    const uint32_t Sign = (Bits >> 16u) & 0x8000u;
    const int32_t  Exp  = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t       Mant = Bits & 0x007FFFFFu;
    if (Exp <= 0)
    {
        return static_cast<uint16_t>(Sign);
    }
    if (Exp >= 31)
    {
        return static_cast<uint16_t>(Sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exp) << 10u) | (Mant >> 13u));
}

[[nodiscard]] float UnpackHalf16ToFloat32(uint16_t HalfWord) noexcept
{
    const uint32_t Sign = (static_cast<uint32_t>(HalfWord & 0x8000u)) << 16u;
    const uint32_t Exp  = (HalfWord >> 10u) & 0x1Fu;
    const uint32_t Mant = HalfWord & 0x03FFu;
    uint32_t Bits       = 0u;
    if (Exp == 0u)
    {
        Bits = Sign;
    }
    else
    {
        Bits = Sign | ((Exp + 127u - 15u) << 23u) | (Mant << 13u);
    }
    float Out = 0.0f;
    std::memcpy(&Out, &Bits, sizeof(Out));
    return Out;
}

[[nodiscard]] float PointToTriangleDistanceSquared(
    Mathematics::Axis3D P,
    Mathematics::Axis3D A,
    Mathematics::Axis3D B,
    Mathematics::Axis3D C) noexcept
{
    const Mathematics::Axis3D AB = B - A;
    const Mathematics::Axis3D AC = C - A;
    const Mathematics::Axis3D AP = P - A;
    const float D1 = Mathematics::EvaluateDot(AB, AP);
    const float D2 = Mathematics::EvaluateDot(AC, AP);
    if (D1 <= 0.0f && D2 <= 0.0f)
    {
        return Mathematics::EvaluateLengthSquared(AP);
    }

    const Mathematics::Axis3D BP = P - B;
    const float D3 = Mathematics::EvaluateDot(AB, BP);
    const float D4 = Mathematics::EvaluateDot(AC, BP);
    if (D3 >= 0.0f && D4 <= D3)
    {
        return Mathematics::EvaluateLengthSquared(BP);
    }

    const float VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0f && D1 >= 0.0f && D3 <= 0.0f)
    {
        const float V = D1 / std::max(1.0e-12f, D1 - D3);
        return Mathematics::EvaluateLengthSquared(P - (A + AB * V));
    }

    const Mathematics::Axis3D CP = P - C;
    const float D5 = Mathematics::EvaluateDot(AB, CP);
    const float D6 = Mathematics::EvaluateDot(AC, CP);
    if (D6 >= 0.0f && D5 <= D6)
    {
        return Mathematics::EvaluateLengthSquared(CP);
    }

    const float VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0f && D2 >= 0.0f && D6 <= 0.0f)
    {
        const float W = D2 / std::max(1.0e-12f, D2 - D6);
        return Mathematics::EvaluateLengthSquared(P - (A + AC * W));
    }

    const float VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0f && (D4 - D3) >= 0.0f && (D5 - D6) >= 0.0f)
    {
        const float W = (D4 - D3) / std::max(1.0e-12f, (D4 - D3) + (D5 - D6));
        return Mathematics::EvaluateLengthSquared(P - (B + (C - B) * W));
    }

    const float Denom = 1.0f / std::max(1.0e-12f, VA + VB + VC);
    const float V     = VB * Denom;
    const float W     = VC * Denom;
    return Mathematics::EvaluateLengthSquared(P - (A + AB * V + AC * W));
}

struct BvhClosestSurfaceStack
{
    uint32_t EntryIndex;
    float    LowerBoundSq;
};

[[nodiscard]] float QueryExactUnsignedDistanceViaBvh(
    const SpatialAcceleration::WideBoundingTree& Tree,
    Mathematics::Axis3D                          QueryPoint) noexcept
{
    if (Tree.WideEntries.empty() || Tree.Primitives.empty())
    {
        return 10.0f;
    }

    float BestDistSq = 100.0f;
    std::array<BvhClosestSurfaceStack, 64u> Stack {};
    uint32_t Top = 0u;
    Stack[Top++] = { 0u, 0.0f };

    while (Top > 0u)
    {
        const auto Cur = Stack[--Top];
        if (Cur.LowerBoundSq >= BestDistSq || Cur.EntryIndex >= Tree.WideEntries.size())
        {
            continue;
        }
        const auto& CurEntry = Tree.WideEntries[Cur.EntryIndex];
        for (uint32_t S = 0u; S < 4u; ++S)
        {
            const uint32_t Ref = CurEntry.BranchReference[S];
            if (Ref == SpatialAcceleration::EmptyBranchReference)
            {
                continue;
            }
            const float DX = std::max({ 0.0f, CurEntry.BranchBoundsMinX[S] - QueryPoint.X, QueryPoint.X - CurEntry.BranchBoundsMaxX[S] });
            const float DY = std::max({ 0.0f, CurEntry.BranchBoundsMinY[S] - QueryPoint.Y, QueryPoint.Y - CurEntry.BranchBoundsMaxY[S] });
            const float DZ = std::max({ 0.0f, CurEntry.BranchBoundsMinZ[S] - QueryPoint.Z, QueryPoint.Z - CurEntry.BranchBoundsMaxZ[S] });
            const float BoxSq = DX * DX + DY * DY + DZ * DZ;
            if (BoxSq >= BestDistSq)
            {
                continue;
            }

            const uint32_t Count = CurEntry.BranchPrimitiveCount[S];
            if (Count > 0u)
            {
                const uint32_t First = Ref & (~SpatialAcceleration::WideLeafReferenceMask);
                for (uint32_t K = 0u; K < Count && (First + K) < Tree.Primitives.size(); ++K)
                {
                    const auto& Tri = Tree.Primitives[First + K];
                    const float D2  = PointToTriangleDistanceSquared(
                        QueryPoint, Tri.Corner0, Tri.Corner0 + Tri.Edge01, Tri.Corner0 + Tri.Edge02);
                    BestDistSq = std::min(BestDistSq, D2);
                }
            }
            else if (Top < Stack.size())
            {
                Stack[Top++] = { Ref, BoxSq };
            }
        }
    }

    return std::sqrt(BestDistSq);
}

[[nodiscard]] bool CountRayParityAlongDirection(
    const SpatialAcceleration::WideBoundingTree& Tree,
    Mathematics::Axis3D                          Origin,
    Mathematics::Axis3D                          UnitDir) noexcept
{
    if (Tree.WideEntries.empty() || Tree.Primitives.empty())
    {
        return false;
    }

    const Mathematics::Axis3D InvDir {
        1.0f / ((std::fabs(UnitDir.X) > 1.0e-8f) ? UnitDir.X : 1.0e-8f),
        1.0f / ((std::fabs(UnitDir.Y) > 1.0e-8f) ? UnitDir.Y : 1.0e-8f),
        1.0f / ((std::fabs(UnitDir.Z) > 1.0e-8f) ? UnitDir.Z : 1.0e-8f),
    };

    uint32_t Crossings = 0u;
    std::array<uint32_t, 64u> Stack {};
    uint32_t Top = 0u;
    Stack[Top++] = 0u;

    while (Top > 0u)
    {
        const uint32_t CurIndex = Stack[--Top];
        if (CurIndex >= Tree.WideEntries.size())
        {
            continue;
        }
        const auto& CurEntry = Tree.WideEntries[CurIndex];
        for (uint32_t S = 0u; S < 4u; ++S)
        {
            const uint32_t Ref = CurEntry.BranchReference[S];
            if (Ref == SpatialAcceleration::EmptyBranchReference)
            {
                continue;
            }
            const float TX0 = (CurEntry.BranchBoundsMinX[S] - Origin.X) * InvDir.X;
            const float TX1 = (CurEntry.BranchBoundsMaxX[S] - Origin.X) * InvDir.X;
            const float TY0 = (CurEntry.BranchBoundsMinY[S] - Origin.Y) * InvDir.Y;
            const float TY1 = (CurEntry.BranchBoundsMaxY[S] - Origin.Y) * InvDir.Y;
            const float TZ0 = (CurEntry.BranchBoundsMinZ[S] - Origin.Z) * InvDir.Z;
            const float TZ1 = (CurEntry.BranchBoundsMaxZ[S] - Origin.Z) * InvDir.Z;
            const float TMin = std::max({ 0.0f, std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
            const float TMax = std::min({ std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
            if (TMax < TMin)
            {
                continue;
            }

            const uint32_t Count = CurEntry.BranchPrimitiveCount[S];
            if (Count > 0u)
            {
                const uint32_t First = Ref & (~SpatialAcceleration::WideLeafReferenceMask);
                for (uint32_t K = 0u; K < Count && (First + K) < Tree.Primitives.size(); ++K)
                {
                    const auto& Tri = Tree.Primitives[First + K];
                    const Mathematics::Axis3D PVec = Mathematics::EvaluateCross(UnitDir, Tri.Edge02);
                    const float Det = Mathematics::EvaluateDot(Tri.Edge01, PVec);
                    if (std::fabs(Det) < 1.0e-9f)
                    {
                        continue;
                    }
                    const float InvDet = 1.0f / Det;
                    const Mathematics::Axis3D TVec = Origin - Tri.Corner0;
                    const float U = Mathematics::EvaluateDot(TVec, PVec) * InvDet;
                    if (U < 0.0f || U > 1.0f)
                    {
                        continue;
                    }
                    const Mathematics::Axis3D QVec = Mathematics::EvaluateCross(TVec, Tri.Edge01);
                    const float V = Mathematics::EvaluateDot(UnitDir, QVec) * InvDet;
                    if (V < 0.0f || (U + V) > 1.0f)
                    {
                        continue;
                    }
                    const float HitT = Mathematics::EvaluateDot(Tri.Edge02, QVec) * InvDet;
                    if (HitT > 1.0e-6f)
                    {
                        ++Crossings;
                    }
                }
            }
            else if (Top < Stack.size())
            {
                Stack[Top++] = Ref;
            }
        }
    }

    return (Crossings & 1u) != 0u;
}

[[nodiscard]] bool ClassifyInsideViaMajorityParity(
    const SpatialAcceleration::WideBoundingTree& Tree,
    Mathematics::Axis3D                          QueryPoint) noexcept
{
    if (QueryPoint.X <= Tree.RootBoundsMin.X || QueryPoint.X >= Tree.RootBoundsMax.X ||
        QueryPoint.Y <= Tree.RootBoundsMin.Y || QueryPoint.Y >= Tree.RootBoundsMax.Y ||
        QueryPoint.Z <= Tree.RootBoundsMin.Z || QueryPoint.Z >= Tree.RootBoundsMax.Z)
    {
        return false;
    }

    // Irrational non-axis-aligned unit directions prevent shared triangle-edge double-counting
    static constexpr std::array<Mathematics::Axis3D, 3u> ParityAxes {{
        {  0.9718253f,  0.1943651f,  0.1325216f },
        { -0.1714986f,  0.9622504f, -0.2116951f },
        {  0.1485221f, -0.2079310f,  0.9667892f },
    }};

    uint32_t InsideVotes = 0u;
    for (const Mathematics::Axis3D& Dir : ParityAxes)
    {
        if (CountRayParityAlongDirection(Tree, QueryPoint, Dir))
        {
            ++InsideVotes;
        }
        if (InsideVotes >= 2u)
        {
            return true;
        }
    }
    return false;
}

} // namespace

float ClosedOccluderBox::EvaluateSignedDistance(Mathematics::Axis3D WorldPos) const noexcept
{
    const float DX   = std::fabs(WorldPos.X - CenterWorld.X) - HalfExtent.X;
    const float DY   = std::fabs(WorldPos.Y - CenterWorld.Y) - HalfExtent.Y;
    const float DZ   = std::fabs(WorldPos.Z - CenterWorld.Z) - HalfExtent.Z;
    const float OX   = std::max(DX, 0.0f);
    const float OY   = std::max(DY, 0.0f);
    const float OZ   = std::max(DZ, 0.0f);
    const float OutD = std::sqrt(OX * OX + OY * OY + OZ * OZ);
    const float InD  = std::min(std::max({ DX, DY, DZ }), 0.0f);
    return OutD + InD;
}

void LocalSignedDistanceVolume::Reset() noexcept
{
    ResolutionX = 0u;
    ResolutionY = 0u;
    ResolutionZ = 0u;
    LocalMin    = {};
    LocalMax    = {};
    VoxelSpacing = {};
    HalfDistanceSamples.clear();
    HalfDistanceSamples.shrink_to_fit();
    DistanceSamples.clear();
    DistanceSamples.shrink_to_fit();
}

bool LocalSignedDistanceVolume::BakeFromTriangles(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
    uint32_t                                                   CubicResolution,
    float                                                      MarginMeters)
{
    return BakeFromTrianglesAspect(InputPrimitives, CubicResolution, CubicResolution, CubicResolution, MarginMeters);
}

bool LocalSignedDistanceVolume::BakeFromTrianglesAspect(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
    uint32_t                                                   ResX,
    uint32_t                                                   ResY,
    uint32_t                                                   ResZ,
    float                                                      MarginMeters)
{
    Reset();
    if (InputPrimitives.empty() || ResX < 8u || ResY < 8u || ResZ < 8u)
    {
        return false;
    }

    SpatialAcceleration::WideBoundingTree LocalTree {};
    if (!LocalTree.BuildFromTrianglePrimitives(InputPrimitives))
    {
        return false;
    }

    ResolutionX = ResX;
    ResolutionY = ResY;
    ResolutionZ = ResZ;
    LocalMin = {
        LocalTree.RootBoundsMin.X - MarginMeters,
        LocalTree.RootBoundsMin.Y - MarginMeters,
        LocalTree.RootBoundsMin.Z - MarginMeters,
    };
    LocalMax = {
        LocalTree.RootBoundsMax.X + MarginMeters,
        LocalTree.RootBoundsMax.Y + MarginMeters,
        LocalTree.RootBoundsMax.Z + MarginMeters,
    };
    VoxelSpacing = {
        (LocalMax.X - LocalMin.X) / static_cast<float>(ResolutionX - 1u),
        (LocalMax.Y - LocalMin.Y) / static_cast<float>(ResolutionY - 1u),
        (LocalMax.Z - LocalMin.Z) / static_cast<float>(ResolutionZ - 1u),
    };

    const std::size_t TotalVoxels = static_cast<std::size_t>(ResolutionX) * ResolutionY * ResolutionZ;
    HalfDistanceSamples.resize(TotalVoxels, 0u);
    DistanceSamples.resize(TotalVoxels, 0.0f);

    const uint32_t ThreadBudget = std::min(ResolutionZ, std::max(1u, std::thread::hardware_concurrency()));
    const auto BakeDepthBand  = [&](uint32_t ZBegin, uint32_t ZEnd)
    {
        for (uint32_t IZ = ZBegin; IZ < ZEnd; ++IZ)
        {
            const float PZ = LocalMin.Z + static_cast<float>(IZ) * VoxelSpacing.Z;
            for (uint32_t IY = 0u; IY < ResolutionY; ++IY)
            {
                const float PY = LocalMin.Y + static_cast<float>(IY) * VoxelSpacing.Y;
                for (uint32_t IX = 0u; IX < ResolutionX; ++IX)
                {
                    const float PX          = LocalMin.X + static_cast<float>(IX) * VoxelSpacing.X;
                    const float Dist        = QueryExactUnsignedDistanceViaBvh(LocalTree, { PX, PY, PZ });
                    const std::size_t Index = (static_cast<std::size_t>(IZ) * ResolutionY + IY) * ResolutionX + IX;
                    DistanceSamples[Index]        = Dist;
                    HalfDistanceSamples[Index]    = PackFloat32ToHalf16(Dist);
                }
            }
        }
    };

    if (ThreadBudget <= 1u)
    {
        BakeDepthBand(0u, ResolutionZ);
    }
    else
    {
        std::vector<std::thread> Threads;
        Threads.reserve(ThreadBudget);
        const uint32_t Chunk = (ResolutionZ + ThreadBudget - 1u) / ThreadBudget;
        for (uint32_t W = 0u; W < ThreadBudget; ++W)
        {
            const uint32_t Z0 = W * Chunk;
            const uint32_t Z1 = std::min(ResolutionZ, Z0 + Chunk);
            if (Z0 < Z1)
            {
                Threads.emplace_back([&, Z0, Z1]() { BakeDepthBand(Z0, Z1); });
            }
        }
        for (auto& T : Threads)
        {
            T.join();
        }
    }

    return true;
}

bool LocalSignedDistanceVolume::BakeSignedFromTrianglesAspect(
    std::span<const SpatialAcceleration::RayTrianglePrimitive> InputPrimitives,
    uint32_t                                                   ResX,
    uint32_t                                                   ResY,
    uint32_t                                                   ResZ,
    float                                                      MarginMeters)
{
    Reset();
    if (InputPrimitives.empty() || ResX < 8u || ResY < 8u || ResZ < 8u)
    {
        return false;
    }

    SpatialAcceleration::WideBoundingTree LocalTree {};
    if (!LocalTree.BuildFromTrianglePrimitives(InputPrimitives))
    {
        return false;
    }

    ResolutionX = ResX;
    ResolutionY = ResY;
    ResolutionZ = ResZ;
    LocalMin = {
        LocalTree.RootBoundsMin.X - MarginMeters,
        LocalTree.RootBoundsMin.Y - MarginMeters,
        LocalTree.RootBoundsMin.Z - MarginMeters,
    };
    LocalMax = {
        LocalTree.RootBoundsMax.X + MarginMeters,
        LocalTree.RootBoundsMax.Y + MarginMeters,
        LocalTree.RootBoundsMax.Z + MarginMeters,
    };
    VoxelSpacing = {
        (LocalMax.X - LocalMin.X) / static_cast<float>(ResolutionX - 1u),
        (LocalMax.Y - LocalMin.Y) / static_cast<float>(ResolutionY - 1u),
        (LocalMax.Z - LocalMin.Z) / static_cast<float>(ResolutionZ - 1u),
    };

    const std::size_t TotalVoxels = static_cast<std::size_t>(ResolutionX) * ResolutionY * ResolutionZ;
    HalfDistanceSamples.resize(TotalVoxels, 0u);
    DistanceSamples.resize(TotalVoxels, 0.0f);

    const uint32_t ThreadBudget = std::min(ResolutionZ, std::max(1u, std::thread::hardware_concurrency()));
    const auto BakeDepthBand = [&](uint32_t ZBegin, uint32_t ZEnd)
    {
        for (uint32_t IZ = ZBegin; IZ < ZEnd; ++IZ)
        {
            const float PZ = LocalMin.Z + static_cast<float>(IZ) * VoxelSpacing.Z;
            for (uint32_t IY = 0u; IY < ResolutionY; ++IY)
            {
                const float PY = LocalMin.Y + static_cast<float>(IY) * VoxelSpacing.Y;
                for (uint32_t IX = 0u; IX < ResolutionX; ++IX)
                {
                    const float PX = LocalMin.X + static_cast<float>(IX) * VoxelSpacing.X;
                    const Mathematics::Axis3D SamplePos { PX, PY, PZ };
                    const float UnsignedDist = QueryExactUnsignedDistanceViaBvh(LocalTree, SamplePos);
                    const bool  Inside       = ClassifyInsideViaMajorityParity(LocalTree, SamplePos);
                    const float SignedDist   = Inside ? -UnsignedDist : UnsignedDist;
                    const std::size_t Index  = (static_cast<std::size_t>(IZ) * ResolutionY + IY) * ResolutionX + IX;
                    DistanceSamples[Index]     = SignedDist;
                    HalfDistanceSamples[Index] = PackFloat32ToHalf16(SignedDist);
                }
            }
        }
    };

    if (ThreadBudget <= 1u)
    {
        BakeDepthBand(0u, ResolutionZ);
    }
    else
    {
        std::vector<std::thread> Threads;
        Threads.reserve(ThreadBudget);
        const uint32_t Chunk = (ResolutionZ + ThreadBudget - 1u) / ThreadBudget;
        for (uint32_t W = 0u; W < ThreadBudget; ++W)
        {
            const uint32_t Z0 = W * Chunk;
            const uint32_t Z1 = std::min(ResolutionZ, Z0 + Chunk);
            if (Z0 < Z1)
            {
                Threads.emplace_back([&, Z0, Z1]() { BakeDepthBand(Z0, Z1); });
            }
        }
        for (auto& T : Threads)
        {
            T.join();
        }
    }

    return true;
}

float LocalSignedDistanceVolume::SampleTrilinearLocal(Mathematics::Axis3D ClampedLocalPos) const noexcept
{
    if (HalfDistanceSamples.empty() || ResolutionX < 2u || ResolutionY < 2u || ResolutionZ < 2u)
    {
        return 10.0f;
    }

    const float FX = std::clamp((ClampedLocalPos.X - LocalMin.X) / VoxelSpacing.X, 0.0f, static_cast<float>(ResolutionX - 1u));
    const float FY = std::clamp((ClampedLocalPos.Y - LocalMin.Y) / VoxelSpacing.Y, 0.0f, static_cast<float>(ResolutionY - 1u));
    const float FZ = std::clamp((ClampedLocalPos.Z - LocalMin.Z) / VoxelSpacing.Z, 0.0f, static_cast<float>(ResolutionZ - 1u));

    const uint32_t IX0 = std::min(ResolutionX - 2u, static_cast<uint32_t>(FX));
    const uint32_t IY0 = std::min(ResolutionY - 2u, static_cast<uint32_t>(FY));
    const uint32_t IZ0 = std::min(ResolutionZ - 2u, static_cast<uint32_t>(FZ));
    const float    TX  = FX - static_cast<float>(IX0);
    const float    TY  = FY - static_cast<float>(IY0);
    const float    TZ  = FZ - static_cast<float>(IZ0);

    const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> float
    {
        const std::size_t Index = (static_cast<std::size_t>(Z) * ResolutionY + Y) * ResolutionX + X;
        return UnpackHalf16ToFloat32(HalfDistanceSamples[Index]);
    };

    const float C00 = At(IX0, IY0, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0) * TX;
    const float C10 = At(IX0, IY0 + 1u, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0) * TX;
    const float C01 = At(IX0, IY0, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0 + 1u) * TX;
    const float C11 = At(IX0, IY0 + 1u, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0 + 1u) * TX;
    const float C0  = C00 * (1.0f - TY) + C10 * TY;
    const float C1  = C01 * (1.0f - TY) + C11 * TY;
    return C0 * (1.0f - TZ) + C1 * TZ;
}

float LocalSignedDistanceVolume::SampleSmoothWorldDistance(
    const RigidLocalVolumePlacement& Placement,
    Mathematics::Axis3D              WorldPos) noexcept
{
    if (!Placement.ActiveToggle || Placement.VolumeAsset == nullptr || Placement.VolumeAsset->HalfDistanceSamples.empty())
    {
        return 20.0f;
    }

    const Mathematics::Axis3D Rel = WorldPos - Placement.Translation;
    const Mathematics::Axis3D LocalP {
         Placement.CosineYaw * Rel.X + Placement.SineYaw * Rel.Y,
        -Placement.SineYaw * Rel.X + Placement.CosineYaw * Rel.Y,
         Rel.Z,
    };

    const auto& Vol  = *Placement.VolumeAsset;
    const float MX   = std::max(0.0f, std::max(Vol.LocalMin.X - LocalP.X, LocalP.X - Vol.LocalMax.X));
    const float MY   = std::max(0.0f, std::max(Vol.LocalMin.Y - LocalP.Y, LocalP.Y - Vol.LocalMax.Y));
    const float MZ   = std::max(0.0f, std::max(Vol.LocalMin.Z - LocalP.Z, LocalP.Z - Vol.LocalMax.Z));
    const float BoxD = std::sqrt(MX * MX + MY * MY + MZ * MZ);

    const Mathematics::Axis3D Clamped {
        std::clamp(LocalP.X, Vol.LocalMin.X, Vol.LocalMax.X),
        std::clamp(LocalP.Y, Vol.LocalMin.Y, Vol.LocalMax.Y),
        std::clamp(LocalP.Z, Vol.LocalMin.Z, Vol.LocalMax.Z),
    };
    const float BoundaryD = Vol.SampleTrilinearLocal(Clamped);
    return (BoxD > 0.0f) ? std::hypot(BoundaryD, BoxD) : BoundaryD;
}

} // namespace Frontier::DistanceFieldSpace
