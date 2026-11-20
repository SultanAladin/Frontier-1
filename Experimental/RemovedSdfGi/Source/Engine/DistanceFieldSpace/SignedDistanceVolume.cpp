/*============================================================================================================================================
                                                           SIGNEDDISTANCEVOLUME.CPP
============================================================================================================================================*/
// 🧩 Implementation of sparse 8x8x8 narrow-band brick 3D Signed Distance Volume, gradient sampling, and cone penumbra trace

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::DistanceFieldSpace
{

namespace
{

[[nodiscard]] float EvaluatePointTriangleDistanceSquared(
    Mathematics::Axis3D Point,
    Mathematics::Axis3D CornerA,
    Mathematics::Axis3D EdgeAB,
    Mathematics::Axis3D EdgeAC,
    Mathematics::Axis3D& OutClosestPoint) noexcept
{
    const Mathematics::Axis3D DiffAP = Point - CornerA;
    const float Dot1 = Mathematics::EvaluateDot(EdgeAB, DiffAP);
    const float Dot2 = Mathematics::EvaluateDot(EdgeAC, DiffAP);
    if (Dot1 <= 0.0f && Dot2 <= 0.0f)
    {
        OutClosestPoint = CornerA;
        return Mathematics::EvaluateLengthSquared(Point - OutClosestPoint);
    }

    const Mathematics::Axis3D CornerB = CornerA + EdgeAB;
    const Mathematics::Axis3D DiffBP  = Point - CornerB;
    const float Dot3 = Mathematics::EvaluateDot(EdgeAB, DiffBP);
    const float Dot4 = Mathematics::EvaluateDot(EdgeAC, DiffBP);
    if (Dot3 >= 0.0f && Dot4 <= Dot3)
    {
        OutClosestPoint = CornerB;
        return Mathematics::EvaluateLengthSquared(Point - OutClosestPoint);
    }

    const float EdgeVC = Dot1 * Dot4 - Dot3 * Dot2;
    if (EdgeVC <= 0.0f && Dot1 >= 0.0f && Dot3 <= 0.0f)
    {
        const float WeightV = Dot1 / std::max(1.0e-12f, Dot1 - Dot3);
        OutClosestPoint = CornerA + EdgeAB * WeightV;
        return Mathematics::EvaluateLengthSquared(Point - OutClosestPoint);
    }

    const Mathematics::Axis3D CornerC = CornerA + EdgeAC;
    const Mathematics::Axis3D DiffCP  = Point - CornerC;
    const float Dot5 = Mathematics::EvaluateDot(EdgeAB, DiffCP);
    const float Dot6 = Mathematics::EvaluateDot(EdgeAC, DiffCP);
    if (Dot6 >= 0.0f && Dot5 <= Dot6)
    {
        OutClosestPoint = CornerC;
        return Mathematics::EvaluateLengthSquared(Point - OutClosestPoint);
    }

    const float EdgeVB = Dot5 * Dot2 - Dot1 * Dot6;
    if (EdgeVB <= 0.0f && Dot2 >= 0.0f && Dot6 <= 0.0f)
    {
        const float WeightW = Dot2 / std::max(1.0e-12f, Dot2 - Dot6);
        OutClosestPoint = CornerA + EdgeAC * WeightW;
        return Mathematics::EvaluateLengthSquared(Point - OutClosestPoint);
    }

    const float EdgeVA = Dot3 * Dot6 - Dot5 * Dot4;
    if (EdgeVA <= 0.0f && (Dot4 - Dot3) >= 0.0f && (Dot5 - Dot6) >= 0.0f)
    {
        const float WeightW = (Dot4 - Dot3) / std::max(1.0e-12f, (Dot4 - Dot3) + (Dot5 - Dot6));
        OutClosestPoint = CornerB + (CornerC - CornerB) * WeightW;
        return Mathematics::EvaluateLengthSquared(Point - OutClosestPoint);
    }

    const float Denom   = 1.0f / std::max(1.0e-12f, EdgeVA + EdgeVB + EdgeVC);
    const float WeightV = EdgeVB * Denom;
    const float WeightW = EdgeVC * Denom;
    OutClosestPoint = CornerA + EdgeAB * WeightV + EdgeAC * WeightW;
    return Mathematics::EvaluateLengthSquared(Point - OutClosestPoint);
}

[[nodiscard]] float EvaluatePointAabbDistanceSquared(
    Mathematics::Axis3D Pt,
    float               MinX,
    float               MaxX,
    float               MinY,
    float               MaxY,
    float               MinZ,
    float               MaxZ) noexcept
{
    if (MinX > MaxX)
    {
        return std::numeric_limits<float>::max();
    }
    const float dX = std::max({ 0.0f, MinX - Pt.X, Pt.X - MaxX });
    const float dY = std::max({ 0.0f, MinY - Pt.Y, Pt.Y - MaxY });
    const float dZ = std::max({ 0.0f, MinZ - Pt.Z, Pt.Z - MaxZ });
    return dX * dX + dY * dY + dZ * dZ;
}

// 📝 Branch-and-bound exact closest-triangle unsigned distance query over BVH4 + majority-vote ray parity sign
[[nodiscard]] float QueryExactSignedDistance(
    const SpatialAcceleration::WideBoundingTree& Tree,
    Mathematics::Axis3D                          QueryPoint,
    float                                        UpperCutoffDistance) noexcept
{
    if (Tree.WideEntries.empty())
    {
        return UpperCutoffDistance;
    }

    float BestDistSq = UpperCutoffDistance * UpperCutoffDistance;
    bool  FoundNear  = false;

    std::array<uint32_t, 64u> Stack {};
    uint32_t StackTop = 0u;
    Stack[StackTop++] = 0u;

    while (StackTop > 0u)
    {
        const auto& Entry = Tree.WideEntries[Stack[--StackTop]];

        for (uint32_t Slot = 0u; Slot < 4u; ++Slot)
        {
            const uint32_t Ref = Entry.BranchReference[Slot];
            if (Ref == SpatialAcceleration::EmptyBranchReference)
            {
                continue;
            }

            const float BoxDistSq = EvaluatePointAabbDistanceSquared(
                QueryPoint,
                Entry.BranchBoundsMinX[Slot],
                Entry.BranchBoundsMaxX[Slot],
                Entry.BranchBoundsMinY[Slot],
                Entry.BranchBoundsMaxY[Slot],
                Entry.BranchBoundsMinZ[Slot],
                Entry.BranchBoundsMaxZ[Slot]);
            if (BoxDistSq > BestDistSq)
            {
                continue;
            }

            if ((Ref & SpatialAcceleration::WideLeafReferenceMask) != 0u)
            {
                const uint32_t PrimStart = Ref & ~SpatialAcceleration::WideLeafReferenceMask;
                const uint32_t PrimCount = Entry.BranchPrimitiveCount[Slot];
                for (uint32_t Offset = 0u; Offset < PrimCount; ++Offset)
                {
                    const auto& Tri = Tree.Primitives[PrimStart + Offset];
                    Mathematics::Axis3D Closest {};
                    const float DistSq = EvaluatePointTriangleDistanceSquared(
                        QueryPoint,
                        Tri.Corner0,
                        Tri.Edge01,
                        Tri.Edge02,
                        Closest);
                    if (DistSq < BestDistSq)
                    {
                        BestDistSq = DistSq;
                        FoundNear  = true;
                    }
                }
            }
            else if (StackTop < Stack.size())
            {
                Stack[StackTop++] = Ref;
            }
        }
    }

    const float UnsignedDist = FoundNear
        ? std::sqrt(std::max(0.0f, BestDistSq))
        : UpperCutoffDistance;

    // 📝 Robust 3-direction normal-alignment vote for inside/outside sign (even when clamped to UpperCutoffDistance!)
    static constexpr std::array<Mathematics::Axis3D, 3u> SignSampleDirs {{
        {  0.57735027f,  0.57735027f,  0.57735027f },
        { -0.68599434f,  0.51449576f, -0.51449576f },
        {  0.26726124f, -0.80178373f,  0.53452248f },
    }};

    uint32_t InsideVotes = 0u;
    for (const Mathematics::Axis3D& Dir : SignSampleDirs)
    {
        SpatialAcceleration::RaySegmentQuery Ray {};
        Ray.Origin          = QueryPoint;
        Ray.UnitDirection   = Dir;
        Ray.MinimumDistance = 0.0f;
        Ray.MaximumDistance = 1.0e6f;
        const auto Hit = Tree.TraceClosestRay(Ray, false);
        if (Hit.HitConfirmed && Mathematics::EvaluateDot(Hit.GeometricNormal, Dir) > 0.0f)
        {
            ++InsideVotes;
        }
    }

    return (InsideVotes >= 2u) ? -UnsignedDist : UnsignedDist;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      SPARSE BRICK SIGNED DISTANCE VOLUME BUILDER
//------------------------------------------------------------------------------------------------------------------------

void SignedDistanceVolume::Reset() noexcept
{
    VolumeBoundsMin          = {};
    VolumeBoundsMax          = {};
    VoxelSpacingMeters       = 0.1f;
    NarrowBandMeters         = 0.8f;
    BrickCountX              = 0u;
    BrickCountY              = 0u;
    BrickCountZ              = 0u;
    BrickIndirection.clear();
    CoarseBrickCenterDistance.clear();
    AllocatedBricks.clear();
}

bool SignedDistanceVolume::BuildFromWideBoundingTree(
    const SpatialAcceleration::WideBoundingTree& Tree,
    float                                        TargetVoxelSpacingMeters,
    float                                        PaddingMarginMeters)
{
    Reset();
    if (Tree.WideEntries.empty())
    {
        return false;
    }

    VoxelSpacingMeters = std::max(1.0e-3f, TargetVoxelSpacingMeters);
    const float BrickWorldSpan = VoxelSpacingMeters * static_cast<float>(BrickEdgeVoxelCount);
    NarrowBandMeters           = std::max(4.0f * VoxelSpacingMeters, PaddingMarginMeters);

    const Mathematics::Axis3D MarginVec { PaddingMarginMeters, PaddingMarginMeters, PaddingMarginMeters };
    VolumeBoundsMin = Tree.RootBoundsMin - MarginVec;
    const Mathematics::Axis3D RawSpan = (Tree.RootBoundsMax + MarginVec) - VolumeBoundsMin;

    BrickCountX = std::clamp(static_cast<uint32_t>(std::ceil(RawSpan.X / BrickWorldSpan)), 1u, 64u);
    BrickCountY = std::clamp(static_cast<uint32_t>(std::ceil(RawSpan.Y / BrickWorldSpan)), 1u, 64u);
    BrickCountZ = std::clamp(static_cast<uint32_t>(std::ceil(RawSpan.Z / BrickWorldSpan)), 1u, 64u);

    VolumeBoundsMax = {
        VolumeBoundsMin.X + static_cast<float>(BrickCountX) * BrickWorldSpan,
        VolumeBoundsMin.Y + static_cast<float>(BrickCountY) * BrickWorldSpan,
        VolumeBoundsMin.Z + static_cast<float>(BrickCountZ) * BrickWorldSpan,
    };

    const std::size_t TotalBricks =
        static_cast<std::size_t>(BrickCountX) *
        static_cast<std::size_t>(BrickCountY) *
        static_cast<std::size_t>(BrickCountZ);

    BrickIndirection.assign(TotalBricks, UnallocatedBrickSlot);
    CoarseBrickCenterDistance.assign(TotalBricks, NarrowBandMeters);

    const float BrickHalfDiagonal = 0.8660254f * BrickWorldSpan;

    for (uint32_t BZ = 0u; BZ < BrickCountZ; ++BZ)
    {
        for (uint32_t BY = 0u; BY < BrickCountY; ++BY)
        {
            for (uint32_t BX = 0u; BX < BrickCountX; ++BX)
            {
                const std::size_t MacroIdx =
                    (static_cast<std::size_t>(BZ) * BrickCountY + BY) * BrickCountX + BX;

                const Mathematics::Axis3D BrickOrigin {
                    VolumeBoundsMin.X + static_cast<float>(BX) * BrickWorldSpan,
                    VolumeBoundsMin.Y + static_cast<float>(BY) * BrickWorldSpan,
                    VolumeBoundsMin.Z + static_cast<float>(BZ) * BrickWorldSpan,
                };
                const Mathematics::Axis3D BrickCenter =
                    BrickOrigin + Mathematics::Axis3D{ 0.5f * BrickWorldSpan, 0.5f * BrickWorldSpan, 0.5f * BrickWorldSpan };

                const float CenterSignedDist = QueryExactSignedDistance(Tree, BrickCenter, 1.0e5f);
                CoarseBrickCenterDistance[MacroIdx] = CenterSignedDist;

                // 📝 Prune far-field exterior/interior bricks whose closest surface exceeds NarrowBandMeters!
                if (std::fabs(CenterSignedDist) > (NarrowBandMeters + BrickHalfDiagonal))
                {
                    continue;
                }

                const uint32_t AllocatedSlot = static_cast<uint32_t>(AllocatedBricks.size());
                BrickIndirection[MacroIdx]   = AllocatedSlot;
                AllocatedBricks.push_back(SparseDistanceBrick{});
                SparseDistanceBrick& Brick = AllocatedBricks.back();

                for (uint32_t VZ = 0u; VZ < BrickEdgeVoxelCount; ++VZ)
                {
                    for (uint32_t VY = 0u; VY < BrickEdgeVoxelCount; ++VY)
                    {
                        for (uint32_t VX = 0u; VX < BrickEdgeVoxelCount; ++VX)
                        {
                            const Mathematics::Axis3D SamplePos {
                                BrickOrigin.X + (static_cast<float>(VX) + 0.5f) * VoxelSpacingMeters,
                                BrickOrigin.Y + (static_cast<float>(VY) + 0.5f) * VoxelSpacingMeters,
                                BrickOrigin.Z + (static_cast<float>(VZ) + 0.5f) * VoxelSpacingMeters,
                            };
                            const float SignedDist = QueryExactSignedDistance(Tree, SamplePos, NarrowBandMeters);
                            const float Normalized = std::clamp(SignedDist / NarrowBandMeters, -1.0f, 1.0f);
                            const uint32_t LocalIdx = (VZ * BrickEdgeVoxelCount + VY) * BrickEdgeVoxelCount + VX;
                            Brick.QuantizedSamples[LocalIdx] = static_cast<int16_t>(std::lround(Normalized * 32767.0f));
                        }
                    }
                }
            }
        }
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       TRILINEAR SAMPLING, GRADIENT & CONE TRACE
//------------------------------------------------------------------------------------------------------------------------

float SignedDistanceVolume::SampleSignedDistance(Mathematics::Axis3D WorldPosition) const noexcept
{
    if (BrickCountX == 0u || BrickIndirection.empty())
    {
        return 1.0e6f;
    }

    // 📝 Clamp to volume domain and add exact exterior Euclidean box distance outside padded AABB
    const Mathematics::Axis3D ClampedPos {
        std::clamp(WorldPosition.X, VolumeBoundsMin.X, VolumeBoundsMax.X),
        std::clamp(WorldPosition.Y, VolumeBoundsMin.Y, VolumeBoundsMax.Y),
        std::clamp(WorldPosition.Z, VolumeBoundsMin.Z, VolumeBoundsMax.Z),
    };
    const float ExteriorOffset = Mathematics::EvaluateDistance(WorldPosition, ClampedPos);

    const float InvSpacing = 1.0f / VoxelSpacingMeters;
    const float GridX = (ClampedPos.X - VolumeBoundsMin.X) * InvSpacing - 0.5f;
    const float GridY = (ClampedPos.Y - VolumeBoundsMin.Y) * InvSpacing - 0.5f;
    const float GridZ = (ClampedPos.Z - VolumeBoundsMin.Z) * InvSpacing - 0.5f;

    const int32_t MaxVoxX = static_cast<int32_t>(BrickCountX * BrickEdgeVoxelCount) - 1;
    const int32_t MaxVoxY = static_cast<int32_t>(BrickCountY * BrickEdgeVoxelCount) - 1;
    const int32_t MaxVoxZ = static_cast<int32_t>(BrickCountZ * BrickEdgeVoxelCount) - 1;

    const auto FetchVoxelDistance = [&](int32_t VX, int32_t VY, int32_t VZ) noexcept -> float
    {
        const uint32_t ClampedVX = static_cast<uint32_t>(std::clamp(VX, 0, MaxVoxX));
        const uint32_t ClampedVY = static_cast<uint32_t>(std::clamp(VY, 0, MaxVoxY));
        const uint32_t ClampedVZ = static_cast<uint32_t>(std::clamp(VZ, 0, MaxVoxZ));

        const uint32_t BX = ClampedVX / BrickEdgeVoxelCount;
        const uint32_t BY = ClampedVY / BrickEdgeVoxelCount;
        const uint32_t BZ = ClampedVZ / BrickEdgeVoxelCount;
        const std::size_t MacroIdx = (static_cast<std::size_t>(BZ) * BrickCountY + BY) * BrickCountX + BX;

        const uint32_t BrickSlot = BrickIndirection[MacroIdx];
        if (BrickSlot == UnallocatedBrickSlot)
        {
            return CoarseBrickCenterDistance[MacroIdx];
        }
        const uint32_t LX = ClampedVX % BrickEdgeVoxelCount;
        const uint32_t LY = ClampedVY % BrickEdgeVoxelCount;
        const uint32_t LZ = ClampedVZ % BrickEdgeVoxelCount;
        const uint32_t LocalIdx = (LZ * BrickEdgeVoxelCount + LY) * BrickEdgeVoxelCount + LX;
        return (static_cast<float>(AllocatedBricks[BrickSlot].QuantizedSamples[LocalIdx]) / 32767.0f) * NarrowBandMeters;
    };

    const int32_t LowerX = static_cast<int32_t>(std::floor(GridX));
    const int32_t LowerY = static_cast<int32_t>(std::floor(GridY));
    const int32_t LowerZ = static_cast<int32_t>(std::floor(GridZ));
    const float   FracX  = GridX - static_cast<float>(LowerX);
    const float   FracY  = GridY - static_cast<float>(LowerY);
    const float   FracZ  = GridZ - static_cast<float>(LowerZ);

    const float C000 = FetchVoxelDistance(LowerX,     LowerY,     LowerZ);
    const float C100 = FetchVoxelDistance(LowerX + 1, LowerY,     LowerZ);
    const float C010 = FetchVoxelDistance(LowerX,     LowerY + 1, LowerZ);
    const float C110 = FetchVoxelDistance(LowerX + 1, LowerY + 1, LowerZ);
    const float C001 = FetchVoxelDistance(LowerX,     LowerY,     LowerZ + 1);
    const float C101 = FetchVoxelDistance(LowerX + 1, LowerY,     LowerZ + 1);
    const float C011 = FetchVoxelDistance(LowerX,     LowerY + 1, LowerZ + 1);
    const float C111 = FetchVoxelDistance(LowerX + 1, LowerY + 1, LowerZ + 1);

    const float C00 = C000 + (C100 - C000) * FracX;
    const float C10 = C010 + (C110 - C010) * FracX;
    const float C01 = C001 + (C101 - C001) * FracX;
    const float C11 = C011 + (C111 - C011) * FracX;
    const float C0  = C00 + (C10 - C00) * FracY;
    const float C1  = C01 + (C11 - C01) * FracY;
    const float TrilinearDist = C0 + (C1 - C0) * FracZ;

    return (ExteriorOffset > 0.0f) ? (std::max(0.0f, TrilinearDist) + ExteriorOffset) : TrilinearDist;
}

Mathematics::Axis3D SignedDistanceVolume::EvaluateSurfaceGradient(Mathematics::Axis3D WorldPosition) const noexcept
{
    const float Step = std::max(1.0e-3f, 0.5f * VoxelSpacingMeters);
    const float GradX = SampleSignedDistance({ WorldPosition.X + Step, WorldPosition.Y, WorldPosition.Z }) -
                        SampleSignedDistance({ WorldPosition.X - Step, WorldPosition.Y, WorldPosition.Z });
    const float GradY = SampleSignedDistance({ WorldPosition.X, WorldPosition.Y + Step, WorldPosition.Z }) -
                        SampleSignedDistance({ WorldPosition.X, WorldPosition.Y - Step, WorldPosition.Z });
    const float GradZ = SampleSignedDistance({ WorldPosition.X, WorldPosition.Y, WorldPosition.Z + Step }) -
                        SampleSignedDistance({ WorldPosition.X, WorldPosition.Y, WorldPosition.Z - Step });
    return Mathematics::NormalizeAxis({ GradX, GradY, GradZ }, { 0.0f, 1.0f, 0.0f });
}

float SignedDistanceVolume::TraceConeVisibility(
    Mathematics::Axis3D RayOrigin,
    Mathematics::Axis3D UnitDirection,
    float               MaximumTraceDistanceMeters,
    float               TanHalfConeAngle,
    uint32_t            MaxSphereSteps) const noexcept
{
    if (BrickCountX == 0u)
    {
        return 1.0f;
    }

    const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis(UnitDirection, { 0.0f, 1.0f, 0.0f });
    const float SafeTanCone       = std::max(1.0e-3f, TanHalfConeAngle);
    const float MinStep           = 0.35f * VoxelSpacingMeters;

    float TravelT    = std::max(1.5f * VoxelSpacingMeters, 1.0e-3f);
    float Visibility = 1.0f;

    for (uint32_t Step = 0u; Step < MaxSphereSteps && TravelT < MaximumTraceDistanceMeters; ++Step)
    {
        const Mathematics::Axis3D SamplePt = RayOrigin + Dir * TravelT;
        const float SignedDist             = SampleSignedDistance(SamplePt);
        const float ConeRadius             = std::max(0.25f * VoxelSpacingMeters, TravelT * SafeTanCone);

        if (SignedDist <= -0.35f * VoxelSpacingMeters)
        {
            return 0.0f;
        }

        const float RawRatio  = std::clamp(0.5f + 0.5f * (SignedDist / ConeRadius), 0.0f, 1.0f);
        const float SmoothVis = RawRatio * RawRatio * (3.0f - 2.0f * RawRatio);
        Visibility            = std::min(Visibility, SmoothVis);
        if (Visibility <= 1.0e-3f)
        {
            return 0.0f;
        }

        TravelT += std::max(MinStep, std::fabs(SignedDist) * 0.85f);
    }

    return Visibility;
}

std::size_t SignedDistanceVolume::QueryOccupiedMemoryBytes() const noexcept
{
    return BrickIndirection.size() * sizeof(uint32_t) +
           CoarseBrickCenterDistance.size() * sizeof(float) +
           AllocatedBricks.size() * sizeof(SparseDistanceBrick);
}

} // namespace Frontier::DistanceFieldSpace
