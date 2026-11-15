/*============================================================================================================================================
                                                             WIDEBOUNDINGTREE.CPP
============================================================================================================================================*/
// 🧩 Implementation of 12-bin SAH BVH builder, 4-wide 128 B collapse, and deterministic closest-hit / shadow ray queries

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Frontier::SpatialAcceleration
{

namespace
{

constexpr uint32_t WideTreeBinaryMagic   = 0x34485642u; // 'BVH4'
constexpr uint32_t WideTreeBinaryVersion = 1u;
constexpr uint32_t SahBinCount           = 12u;
constexpr uint32_t MaxLeafPrimitives     = 4u;

struct AxisAlignedBox
{
    Mathematics::Axis3D Min {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    Mathematics::Axis3D Max {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };

    void ExpandPoint(Mathematics::Axis3D Pt) noexcept
    {
        Min.X = std::min(Min.X, Pt.X);
        Min.Y = std::min(Min.Y, Pt.Y);
        Min.Z = std::min(Min.Z, Pt.Z);
        Max.X = std::max(Max.X, Pt.X);
        Max.Y = std::max(Max.Y, Pt.Y);
        Max.Z = std::max(Max.Z, Pt.Z);
    }

    void ExpandBox(const AxisAlignedBox& Other) noexcept
    {
        if (Other.Min.X > Other.Max.X)
        {
            return;
        }
        ExpandPoint(Other.Min);
        ExpandPoint(Other.Max);
    }

    [[nodiscard]] float EvaluateHalfArea() const noexcept
    {
        if (Min.X > Max.X)
        {
            return 0.0f;
        }
        const float dX = std::max(0.0f, Max.X - Min.X);
        const float dY = std::max(0.0f, Max.Y - Min.Y);
        const float dZ = std::max(0.0f, Max.Z - Min.Z);
        return dX * dY + dY * dZ + dZ * dX;
    }
};

struct BinaryBuildSlot
{
    AxisAlignedBox Bounds         {};
    uint32_t       SubtreeLeft    = EmptyBranchReference;
    uint32_t       SubtreeRight   = EmptyBranchReference;
    uint32_t       PrimitiveStart = 0u;
    uint32_t       PrimitiveCount = 0u;
};

struct BuildPrimitiveEntry
{
    RayTrianglePrimitive Primitive        {};
    AxisAlignedBox       Bounds           {};
    Mathematics::Axis3D  Centroid         {};
    uint32_t             SourceInputIndex = 0u;
};

[[nodiscard]] float ExtractAxisCoordinate(Mathematics::Axis3D Vec, uint32_t Axis) noexcept
{
    return (Axis == 0u) ? Vec.X : ((Axis == 1u) ? Vec.Y : Vec.Z);
}

uint32_t BuildBinarySahSubtree(
    std::vector<BinaryBuildSlot>&     OutBinarySlots,
    std::vector<BuildPrimitiveEntry>& WorkPrimitives,
    uint32_t                          Start,
    uint32_t                          End)
{
    const uint32_t SlotIndex = static_cast<uint32_t>(OutBinarySlots.size());
    OutBinarySlots.push_back(BinaryBuildSlot{});

    AxisAlignedBox TotalBounds {};
    AxisAlignedBox CentroidBounds {};
    for (uint32_t Idx = Start; Idx < End; ++Idx)
    {
        TotalBounds.ExpandBox(WorkPrimitives[Idx].Bounds);
        CentroidBounds.ExpandPoint(WorkPrimitives[Idx].Centroid);
    }

    const uint32_t Count = End - Start;
    if (Count <= MaxLeafPrimitives)
    {
        OutBinarySlots[SlotIndex].Bounds         = TotalBounds;
        OutBinarySlots[SlotIndex].PrimitiveStart = Start;
        OutBinarySlots[SlotIndex].PrimitiveCount = Count;
        return SlotIndex;
    }

    // 📝 Choose split axis & bin via 12-bin Surface Area Heuristic
    uint32_t BestAxis     = 0u;
    uint32_t BestSplitBin = 0u;
    float    BestSahCost  = std::numeric_limits<float>::max();
    const float InvOuterArea = 1.0f / std::max(1.0e-12f, TotalBounds.EvaluateHalfArea());

    for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        const float AxisMin  = ExtractAxisCoordinate(CentroidBounds.Min, Axis);
        const float AxisSpan = ExtractAxisCoordinate(CentroidBounds.Max, Axis) - AxisMin;
        if (AxisSpan <= 1.0e-7f)
        {
            continue;
        }

        std::array<AxisAlignedBox, SahBinCount> BinBoxes {};
        std::array<uint32_t, SahBinCount>       BinCounts {};
        const float Scale = static_cast<float>(SahBinCount) / AxisSpan;

        for (uint32_t Idx = Start; Idx < End; ++Idx)
        {
            const float Coord = ExtractAxisCoordinate(WorkPrimitives[Idx].Centroid, Axis);
            const uint32_t Bin = std::min<uint32_t>(SahBinCount - 1u, static_cast<uint32_t>((Coord - AxisMin) * Scale));
            BinBoxes[Bin].ExpandBox(WorkPrimitives[Idx].Bounds);
            ++BinCounts[Bin];
        }

        std::array<float, SahBinCount - 1u>    LeftAreas {};
        std::array<uint32_t, SahBinCount - 1u> LeftCounts {};
        AxisAlignedBox SweepLeft {};
        uint32_t       AccumLeft = 0u;
        for (uint32_t Split = 0u; Split + 1u < SahBinCount; ++Split)
        {
            SweepLeft.ExpandBox(BinBoxes[Split]);
            AccumLeft         += BinCounts[Split];
            LeftAreas[Split]   = SweepLeft.EvaluateHalfArea();
            LeftCounts[Split]  = AccumLeft;
        }

        AxisAlignedBox SweepRight {};
        uint32_t       AccumRight = 0u;
        for (uint32_t Rev = SahBinCount - 1u; Rev >= 1u; --Rev)
        {
            SweepRight.ExpandBox(BinBoxes[Rev]);
            AccumRight += BinCounts[Rev];
            const uint32_t Split = Rev - 1u;
            if (LeftCounts[Split] == 0u || AccumRight == 0u)
            {
                continue;
            }
            const float Cost = 0.5f + InvOuterArea * (
                static_cast<float>(LeftCounts[Split]) * LeftAreas[Split] +
                static_cast<float>(AccumRight) * SweepRight.EvaluateHalfArea());
            if (Cost < BestSahCost)
            {
                BestSahCost  = Cost;
                BestAxis     = Axis;
                BestSplitBin = Rev;
            }
        }
    }

    const float LeafCost = static_cast<float>(Count);
    uint32_t MidIndex = Start + Count / 2u;

    if (BestSplitBin > 0u && (BestSahCost < LeafCost || Count > 8u))
    {
        const float AxisMin  = ExtractAxisCoordinate(CentroidBounds.Min, BestAxis);
        const float AxisSpan = ExtractAxisCoordinate(CentroidBounds.Max, BestAxis) - AxisMin;
        const float Scale    = static_cast<float>(SahBinCount) / AxisSpan;

        const auto MidIter = std::partition(
            WorkPrimitives.begin() + Start,
            WorkPrimitives.begin() + End,
            [&](const BuildPrimitiveEntry& Candidate) noexcept
            {
                const float Coord = ExtractAxisCoordinate(Candidate.Centroid, BestAxis);
                const uint32_t Bin = std::min<uint32_t>(SahBinCount - 1u, static_cast<uint32_t>((Coord - AxisMin) * Scale));
                return Bin < BestSplitBin;
            });
        MidIndex = static_cast<uint32_t>(MidIter - WorkPrimitives.begin());
        if (MidIndex == Start || MidIndex == End)
        {
            MidIndex = Start + Count / 2u;
            std::sort(
                WorkPrimitives.begin() + Start,
                WorkPrimitives.begin() + End,
                [BestAxis](const BuildPrimitiveEntry& Left, const BuildPrimitiveEntry& Right) noexcept
                {
                    return ExtractAxisCoordinate(Left.Centroid, BestAxis) < ExtractAxisCoordinate(Right.Centroid, BestAxis);
                });
        }
    }
    else if (Count <= 8u)
    {
        OutBinarySlots[SlotIndex].Bounds         = TotalBounds;
        OutBinarySlots[SlotIndex].PrimitiveStart = Start;
        OutBinarySlots[SlotIndex].PrimitiveCount = Count;
        return SlotIndex;
    }

    const uint32_t LeftIdx  = BuildBinarySahSubtree(OutBinarySlots, WorkPrimitives, Start, MidIndex);
    const uint32_t RightIdx = BuildBinarySahSubtree(OutBinarySlots, WorkPrimitives, MidIndex, End);

    OutBinarySlots[SlotIndex].Bounds       = TotalBounds;
    OutBinarySlots[SlotIndex].SubtreeLeft  = LeftIdx;
    OutBinarySlots[SlotIndex].SubtreeRight = RightIdx;
    return SlotIndex;
}

// 📝 Collapses binary SAH tree into 128-byte 4-Wide BVH4 by opening the largest-surface-area internal branch up to 4 slots
uint32_t CollapseBinaryToWide4(
    const std::vector<BinaryBuildSlot>& BinarySlots,
    uint32_t                            BinaryIndex,
    std::vector<WideTreeEntry>&         OutWideEntries,
    uint32_t                            CurrentDepth,
    uint32_t&                           InOutMaxDepth)
{
    InOutMaxDepth = std::max(InOutMaxDepth, CurrentDepth);
    const uint32_t WideIndex = static_cast<uint32_t>(OutWideEntries.size());
    OutWideEntries.push_back(WideTreeEntry{});

    std::array<uint32_t, 4u> CandidateBinarySlots {{ EmptyBranchReference, EmptyBranchReference, EmptyBranchReference, EmptyBranchReference }};
    uint32_t ActiveSlots = 0u;

    const BinaryBuildSlot& RootSlot = BinarySlots[BinaryIndex];
    if (RootSlot.PrimitiveCount > 0u)
    {
        CandidateBinarySlots[0u] = BinaryIndex;
        ActiveSlots              = 1u;
    }
    else
    {
        CandidateBinarySlots[0u] = RootSlot.SubtreeLeft;
        CandidateBinarySlots[1u] = RootSlot.SubtreeRight;
        ActiveSlots              = 2u;

        while (ActiveSlots < 4u)
        {
            int32_t BestExpandPos  = -1;
            float   LargestHalfArea = -1.0f;
            for (uint32_t Pos = 0u; Pos < ActiveSlots; ++Pos)
            {
                const BinaryBuildSlot& Sub = BinarySlots[CandidateBinarySlots[Pos]];
                if (Sub.PrimitiveCount == 0u)
                {
                    const float Area = Sub.Bounds.EvaluateHalfArea();
                    if (Area > LargestHalfArea)
                    {
                        LargestHalfArea = Area;
                        BestExpandPos   = static_cast<int32_t>(Pos);
                    }
                }
            }
            if (BestExpandPos < 0)
            {
                break;
            }
            const BinaryBuildSlot Expanded = BinarySlots[CandidateBinarySlots[static_cast<uint32_t>(BestExpandPos)]];
            CandidateBinarySlots[static_cast<uint32_t>(BestExpandPos)] = Expanded.SubtreeLeft;
            CandidateBinarySlots[ActiveSlots++]                        = Expanded.SubtreeRight;
        }
    }

    WideTreeEntry BuiltEntry {};
    for (uint32_t Pos = 0u; Pos < 4u; ++Pos)
    {
        if (Pos >= ActiveSlots || CandidateBinarySlots[Pos] == EmptyBranchReference)
        {
            BuiltEntry.BranchBoundsMinX[Pos]     = 1.0e30f;
            BuiltEntry.BranchBoundsMaxX[Pos]     = -1.0e30f;
            BuiltEntry.BranchBoundsMinY[Pos]     = 1.0e30f;
            BuiltEntry.BranchBoundsMaxY[Pos]     = -1.0e30f;
            BuiltEntry.BranchBoundsMinZ[Pos]     = 1.0e30f;
            BuiltEntry.BranchBoundsMaxZ[Pos]     = -1.0e30f;
            BuiltEntry.BranchReference[Pos]      = EmptyBranchReference;
            BuiltEntry.BranchPrimitiveCount[Pos] = 0u;
            continue;
        }

        const BinaryBuildSlot& Sub = BinarySlots[CandidateBinarySlots[Pos]];
        BuiltEntry.BranchBoundsMinX[Pos] = Sub.Bounds.Min.X;
        BuiltEntry.BranchBoundsMaxX[Pos] = Sub.Bounds.Max.X;
        BuiltEntry.BranchBoundsMinY[Pos] = Sub.Bounds.Min.Y;
        BuiltEntry.BranchBoundsMaxY[Pos] = Sub.Bounds.Max.Y;
        BuiltEntry.BranchBoundsMinZ[Pos] = Sub.Bounds.Min.Z;
        BuiltEntry.BranchBoundsMaxZ[Pos] = Sub.Bounds.Max.Z;

        if (Sub.PrimitiveCount > 0u)
        {
            BuiltEntry.BranchReference[Pos]      = WideLeafReferenceMask | Sub.PrimitiveStart;
            BuiltEntry.BranchPrimitiveCount[Pos] = Sub.PrimitiveCount;
        }
        else
        {
            const uint32_t NestedWide = CollapseBinaryToWide4(
                BinarySlots,
                CandidateBinarySlots[Pos],
                OutWideEntries,
                CurrentDepth + 1u,
                InOutMaxDepth);
            BuiltEntry.BranchReference[Pos]      = NestedWide;
            BuiltEntry.BranchPrimitiveCount[Pos] = 0u;
        }
    }

    OutWideEntries[WideIndex] = BuiltEntry;
    return WideIndex;
}

[[nodiscard]] bool IntersectSlabAabb(
    Mathematics::Axis3D Origin,
    Mathematics::Axis3D InvDir,
    float               MinT,
    float               MaxT,
    float               BoxMinX,
    float               BoxMaxX,
    float               BoxMinY,
    float               BoxMaxY,
    float               BoxMinZ,
    float               BoxMaxZ,
    float&              OutEntryT) noexcept
{
    if (BoxMinX > BoxMaxX)
    {
        return false;
    }
    const float Tx0 = (BoxMinX - Origin.X) * InvDir.X;
    const float Tx1 = (BoxMaxX - Origin.X) * InvDir.X;
    const float Ty0 = (BoxMinY - Origin.Y) * InvDir.Y;
    const float Ty1 = (BoxMaxY - Origin.Y) * InvDir.Y;
    const float Tz0 = (BoxMinZ - Origin.Z) * InvDir.Z;
    const float Tz1 = (BoxMaxZ - Origin.Z) * InvDir.Z;

    const float TEnter = std::max({ MinT, std::min(Tx0, Tx1), std::min(Ty0, Ty1), std::min(Tz0, Tz1) });
    const float TExit  = std::min({ MaxT, std::max(Tx0, Tx1), std::max(Ty0, Ty1), std::max(Tz0, Tz1) });
    OutEntryT          = TEnter;
    return TEnter <= TExit;
}

[[nodiscard]] bool IntersectTriangleMollerTrumbore(
    const RayTrianglePrimitive& Tri,
    Mathematics::Axis3D         Origin,
    Mathematics::Axis3D         Dir,
    float                       MinT,
    float                       MaxT,
    bool                        CullBackfaces,
    float&                      OutDistanceT,
    float&                      OutBaryU,
    float&                      OutBaryV) noexcept
{
    const Mathematics::Axis3D PVec = Mathematics::EvaluateCross(Dir, Tri.Edge02);
    const float Det = Mathematics::EvaluateDot(Tri.Edge01, PVec);

    if (CullBackfaces)
    {
        if (Det <= 1.0e-9f)
        {
            return false;
        }
    }
    else if (std::fabs(Det) <= 1.0e-9f)
    {
        return false;
    }

    const float InvDet = 1.0f / Det;
    const Mathematics::Axis3D TVec = Origin - Tri.Corner0;
    const float BaryU = Mathematics::EvaluateDot(TVec, PVec) * InvDet;
    if (BaryU < -1.0e-6f || BaryU > 1.0f + 1.0e-6f)
    {
        return false;
    }

    const Mathematics::Axis3D QVec = Mathematics::EvaluateCross(TVec, Tri.Edge01);
    const float BaryV = Mathematics::EvaluateDot(Dir, QVec) * InvDet;
    if (BaryV < -1.0e-6f || (BaryU + BaryV) > 1.0f + 1.0e-6f)
    {
        return false;
    }

    const float HitT = Mathematics::EvaluateDot(Tri.Edge02, QVec) * InvDet;
    if (HitT < MinT || HitT > MaxT)
    {
        return false;
    }

    OutDistanceT = HitT;
    OutBaryU     = std::clamp(BaryU, 0.0f, 1.0f);
    OutBaryV     = std::clamp(BaryV, 0.0f, 1.0f);
    return true;
}

template <typename PodType>
void AppendPodBytes(std::vector<std::byte>& OutBytes, const PodType& SourceEntry)
{
    const std::size_t Offset = OutBytes.size();
    OutBytes.resize(Offset + sizeof(PodType));
    std::memcpy(OutBytes.data() + Offset, &SourceEntry, sizeof(PodType));
}

template <typename PodType>
bool ReadPodBytes(std::span<const std::byte>& Cursor, PodType& OutEntry) noexcept
{
    if (Cursor.size() < sizeof(PodType))
    {
        return false;
    }
    std::memcpy(&OutEntry, Cursor.data(), sizeof(PodType));
    Cursor = Cursor.subspan(sizeof(PodType));
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               4-WIDE BVH4 BUILD & TRACE
//------------------------------------------------------------------------------------------------------------------------

void WideBoundingTree::Reset() noexcept
{
    WideEntries.clear();
    Primitives.clear();
    PrimitiveNormals.clear();
    RootBoundsMin = {};
    RootBoundsMax = {};
    MaxTreeDepth  = 0u;
}

bool WideBoundingTree::BuildFromPolygonNetwork(const GeometrySpace::PolygonNetwork& Network)
{
    Reset();
    if (Network.Vertices.empty() || Network.Faces.empty())
    {
        return false;
    }

    std::vector<BuildPrimitiveEntry> WorkPrimitives;
    WorkPrimitives.reserve(Network.Faces.size() * 2u);

    uint32_t PrimCounter = 0u;
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Network.Faces[FaceIdx];
        if (CurrentFace.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        std::vector<uint32_t> LoopVertices;
        LoopVertices.reserve(CurrentFace.CornerCount);
        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            LoopVertices.push_back(Network.Edges[Walk].OriginVertexIndex);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }

        for (std::size_t Fan = 1u; Fan + 1u < LoopVertices.size(); ++Fan)
        {
            const Mathematics::Axis3D P0 = Network.Vertices[LoopVertices[0u]].Axes;
            const Mathematics::Axis3D P1 = Network.Vertices[LoopVertices[Fan]].Axes;
            const Mathematics::Axis3D P2 = Network.Vertices[LoopVertices[Fan + 1u]].Axes;

            BuildPrimitiveEntry Rec {};
            Rec.Primitive.Corner0          = P0;
            Rec.Primitive.Edge01           = P1 - P0;
            Rec.Primitive.Edge02           = P2 - P0;
            Rec.Primitive.SourceFaceIndex  = FaceIdx;
            Rec.Primitive.MaterialOrdinal  = CurrentFace.PartGroupOrdinal;
            Rec.Primitive.OpaqueMask       = 1u;
            Rec.Primitive.PrimitiveOrdinal = PrimCounter++;

            Rec.Bounds.ExpandPoint(P0);
            Rec.Bounds.ExpandPoint(P1);
            Rec.Bounds.ExpandPoint(P2);
            // Pad zero-thickness axis-aligned triangles by 1e-6 m to guarantee watertight slab tests
            Rec.Bounds.Min = Rec.Bounds.Min - Mathematics::Axis3D{ 1.0e-6f, 1.0e-6f, 1.0e-6f };
            Rec.Bounds.Max = Rec.Bounds.Max + Mathematics::Axis3D{ 1.0e-6f, 1.0e-6f, 1.0e-6f };
            Rec.Centroid   = (P0 + P1 + P2) * (1.0f / 3.0f);
            WorkPrimitives.push_back(Rec);
        }
    }

    if (WorkPrimitives.empty())
    {
        return false;
    }

    std::vector<BinaryBuildSlot> BinarySlots;
    BinarySlots.reserve(WorkPrimitives.size() * 2u);
    const uint32_t BinaryRoot = BuildBinarySahSubtree(
        BinarySlots,
        WorkPrimitives,
        0u,
        static_cast<uint32_t>(WorkPrimitives.size()));

    RootBoundsMin = BinarySlots[BinaryRoot].Bounds.Min;
    RootBoundsMax = BinarySlots[BinaryRoot].Bounds.Max;

    Primitives.resize(WorkPrimitives.size());
    for (std::size_t Idx = 0u; Idx < WorkPrimitives.size(); ++Idx)
    {
        Primitives[Idx] = WorkPrimitives[Idx].Primitive;
    }

    WideEntries.reserve(BinarySlots.size() / 2u + 4u);
    MaxTreeDepth = 0u;
    (void)CollapseBinaryToWide4(BinarySlots, BinaryRoot, WideEntries, 1u, MaxTreeDepth);
    return !WideEntries.empty();
}

bool WideBoundingTree::BuildFromTrianglePrimitives(std::span<const RayTrianglePrimitive> InputPrimitives)
{
    return BuildFromTrianglePrimitivesWithNormals(InputPrimitives, {});
}

bool WideBoundingTree::BuildFromTrianglePrimitivesWithNormals(
    std::span<const RayTrianglePrimitive>  InputPrimitives,
    std::span<const TriangleSmoothNormals> InputNormals)
{
    Reset();
    if (InputPrimitives.empty())
    {
        return false;
    }

    const bool HasNormals = (InputNormals.size() == InputPrimitives.size());

    std::vector<BuildPrimitiveEntry> WorkPrimitives;
    WorkPrimitives.reserve(InputPrimitives.size());

    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(InputPrimitives.size()); ++Idx)
    {
        const RayTrianglePrimitive& Src = InputPrimitives[Idx];
        const Mathematics::Axis3D P0 = Src.Corner0;
        const Mathematics::Axis3D P1 = P0 + Src.Edge01;
        const Mathematics::Axis3D P2 = P0 + Src.Edge02;

        BuildPrimitiveEntry Rec {};
        Rec.Primitive        = Src;
        Rec.SourceInputIndex = Idx;
        Rec.Bounds.ExpandPoint(P0);
        Rec.Bounds.ExpandPoint(P1);
        Rec.Bounds.ExpandPoint(P2);
        Rec.Bounds.Min = Rec.Bounds.Min - Mathematics::Axis3D{ 1.0e-6f, 1.0e-6f, 1.0e-6f };
        Rec.Bounds.Max = Rec.Bounds.Max + Mathematics::Axis3D{ 1.0e-6f, 1.0e-6f, 1.0e-6f };
        Rec.Centroid   = (P0 + P1 + P2) * (1.0f / 3.0f);
        WorkPrimitives.push_back(Rec);
    }

    std::vector<BinaryBuildSlot> BinarySlots;
    BinarySlots.reserve(WorkPrimitives.size() * 2u);
    const uint32_t BinaryRoot = BuildBinarySahSubtree(
        BinarySlots,
        WorkPrimitives,
        0u,
        static_cast<uint32_t>(WorkPrimitives.size()));

    RootBoundsMin = BinarySlots[BinaryRoot].Bounds.Min;
    RootBoundsMax = BinarySlots[BinaryRoot].Bounds.Max;

    Primitives.resize(WorkPrimitives.size());
    if (HasNormals)
    {
        PrimitiveNormals.resize(WorkPrimitives.size());
    }
    for (std::size_t Idx = 0u; Idx < WorkPrimitives.size(); ++Idx)
    {
        Primitives[Idx] = WorkPrimitives[Idx].Primitive;
        if (HasNormals)
        {
            PrimitiveNormals[Idx] = InputNormals[WorkPrimitives[Idx].SourceInputIndex];
        }
    }

    WideEntries.reserve(BinarySlots.size() / 2u + 4u);
    MaxTreeDepth = 0u;
    (void)CollapseBinaryToWide4(BinarySlots, BinaryRoot, WideEntries, 1u, MaxTreeDepth);
    return !WideEntries.empty();
}

RayIntersectionResult WideBoundingTree::TraceClosestRay(
    const RaySegmentQuery& Ray,
    bool                   CullBackfaces) const noexcept
{
    RayIntersectionResult Result {};
    Result.HitDistanceMeters = Ray.MaximumDistance;
    if (WideEntries.empty())
    {
        return Result;
    }

    const auto SafeRecip = [](float Comp) noexcept -> float
    {
        if (std::fabs(Comp) <= 1.0e-12f)
        {
            return (Comp >= 0.0f) ? 1.0e12f : -1.0e12f;
        }
        return 1.0f / Comp;
    };
    const Mathematics::Axis3D InvDir {
        SafeRecip(Ray.UnitDirection.X),
        SafeRecip(Ray.UnitDirection.Y),
        SafeRecip(Ray.UnitDirection.Z),
    };

    struct StackSlot
    {
        uint32_t WideIndex = 0u;
        float    EntryDist = 0.0f;
    };
    std::array<StackSlot, 64u> Stack {};
    uint32_t StackTop = 0u;
    Stack[StackTop++] = { 0u, Ray.MinimumDistance };

    while (StackTop > 0u)
    {
        const StackSlot Current = Stack[--StackTop];
        if (Current.EntryDist > Result.HitDistanceMeters)
        {
            continue;
        }

        ++Result.VisitedWideEntries;
        const WideTreeEntry& Entry = WideEntries[Current.WideIndex];

        struct OrderedHitBranch
        {
            uint32_t Slot      = 0u;
            float    EntryDist = 0.0f;
        };
        std::array<OrderedHitBranch, 4u> HitBranches {};
        uint32_t HitCount = 0u;

        for (uint32_t Slot = 0u; Slot < 4u; ++Slot)
        {
            const uint32_t Ref = Entry.BranchReference[Slot];
            if (Ref == EmptyBranchReference)
            {
                continue;
            }
            float EntryT = 0.0f;
            if (!IntersectSlabAabb(
                    Ray.Origin,
                    InvDir,
                    Ray.MinimumDistance,
                    Result.HitDistanceMeters,
                    Entry.BranchBoundsMinX[Slot],
                    Entry.BranchBoundsMaxX[Slot],
                    Entry.BranchBoundsMinY[Slot],
                    Entry.BranchBoundsMaxY[Slot],
                    Entry.BranchBoundsMinZ[Slot],
                    Entry.BranchBoundsMaxZ[Slot],
                    EntryT))
            {
                continue;
            }

            if ((Ref & WideLeafReferenceMask) != 0u)
            {
                const uint32_t PrimStart = Ref & ~WideLeafReferenceMask;
                const uint32_t PrimCount = Entry.BranchPrimitiveCount[Slot];
                for (uint32_t Offset = 0u; Offset < PrimCount; ++Offset)
                {
                    ++Result.TestedPrimitives;
                    const uint32_t PrimIdx = PrimStart + Offset;
                    const RayTrianglePrimitive& Tri = Primitives[PrimIdx];
                    float CandT = 0.0f, CandU = 0.0f, CandV = 0.0f;
                    if (IntersectTriangleMollerTrumbore(
                            Tri,
                            Ray.Origin,
                            Ray.UnitDirection,
                            Ray.MinimumDistance,
                            Result.HitDistanceMeters,
                            CullBackfaces,
                            CandT,
                            CandU,
                            CandV))
                    {
                        Result.HitDistanceMeters = CandT;
                        Result.BarycentricU      = CandU;
                        Result.BarycentricV      = CandV;
                        Result.GeometricNormal   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02));
                        Result.SourceFaceIndex   = Tri.SourceFaceIndex;
                        Result.PrimitiveIndex    = PrimIdx;
                        Result.MaterialOrdinal   = Tri.MaterialOrdinal;
                        Result.HitConfirmed      = true;
                    }
                }
            }
            else
            {
                HitBranches[HitCount++] = { Ref, EntryT };
            }
        }

        // 📝 Push internal branches in farthest-first order so the closest branch is popped next!
        for (uint32_t Outer = 1u; Outer < HitCount; ++Outer)
        {
            const OrderedHitBranch Key = HitBranches[Outer];
            int32_t Inner = static_cast<int32_t>(Outer) - 1;
            while (Inner >= 0 && HitBranches[static_cast<uint32_t>(Inner)].EntryDist < Key.EntryDist)
            {
                HitBranches[static_cast<uint32_t>(Inner + 1)] = HitBranches[static_cast<uint32_t>(Inner)];
                --Inner;
            }
            HitBranches[static_cast<uint32_t>(Inner + 1)] = Key;
        }

        for (uint32_t Idx = 0u; Idx < HitCount && StackTop < Stack.size(); ++Idx)
        {
            if (HitBranches[Idx].EntryDist <= Result.HitDistanceMeters)
            {
                Stack[StackTop++] = { HitBranches[Idx].Slot, HitBranches[Idx].EntryDist };
            }
        }
    }

    return Result;
}

bool WideBoundingTree::QueryRayOccluded(
    const RaySegmentQuery& Ray,
    uint32_t*              OutVisitedEntries) const noexcept
{
    uint32_t Visited = 0u;
    if (WideEntries.empty())
    {
        if (OutVisitedEntries != nullptr)
        {
            *OutVisitedEntries = 0u;
        }
        return false;
    }

    const auto SafeRecip = [](float Comp) noexcept -> float
    {
        return (std::fabs(Comp) <= 1.0e-12f) ? ((Comp >= 0.0f) ? 1.0e12f : -1.0e12f) : (1.0f / Comp);
    };
    const Mathematics::Axis3D InvDir {
        SafeRecip(Ray.UnitDirection.X),
        SafeRecip(Ray.UnitDirection.Y),
        SafeRecip(Ray.UnitDirection.Z),
    };

    std::array<uint32_t, 64u> Stack {};
    uint32_t StackTop = 0u;
    Stack[StackTop++] = 0u;

    while (StackTop > 0u)
    {
        const uint32_t CurrentIdx = Stack[--StackTop];
        ++Visited;
        const WideTreeEntry& Entry = WideEntries[CurrentIdx];

        for (uint32_t Slot = 0u; Slot < 4u; ++Slot)
        {
            const uint32_t Ref = Entry.BranchReference[Slot];
            if (Ref == EmptyBranchReference)
            {
                continue;
            }
            float EntryT = 0.0f;
            if (!IntersectSlabAabb(
                    Ray.Origin,
                    InvDir,
                    Ray.MinimumDistance,
                    Ray.MaximumDistance,
                    Entry.BranchBoundsMinX[Slot],
                    Entry.BranchBoundsMaxX[Slot],
                    Entry.BranchBoundsMinY[Slot],
                    Entry.BranchBoundsMaxY[Slot],
                    Entry.BranchBoundsMinZ[Slot],
                    Entry.BranchBoundsMaxZ[Slot],
                    EntryT))
            {
                continue;
            }

            if ((Ref & WideLeafReferenceMask) != 0u)
            {
                const uint32_t PrimStart = Ref & ~WideLeafReferenceMask;
                const uint32_t PrimCount = Entry.BranchPrimitiveCount[Slot];
                for (uint32_t Offset = 0u; Offset < PrimCount; ++Offset)
                {
                    const RayTrianglePrimitive& Tri = Primitives[PrimStart + Offset];
                    if (Tri.OpaqueMask == 0u)
                    {
                        continue;
                    }
                    float CandT = 0.0f, CandU = 0.0f, CandV = 0.0f;
                    if (IntersectTriangleMollerTrumbore(
                            Tri,
                            Ray.Origin,
                            Ray.UnitDirection,
                            Ray.MinimumDistance,
                            Ray.MaximumDistance,
                            false,
                            CandT,
                            CandU,
                            CandV))
                    {
                        if (OutVisitedEntries != nullptr)
                        {
                            *OutVisitedEntries = Visited;
                        }
                        return true;
                    }
                }
            }
            else if (StackTop < Stack.size())
            {
                Stack[StackTop++] = Ref;
            }
        }
    }

    if (OutVisitedEntries != nullptr)
    {
        *OutVisitedEntries = Visited;
    }
    return false;
}

std::vector<std::byte> WideBoundingTree::SerializeToBinaryBytes() const
{
    std::vector<std::byte> Bytes;
    Bytes.reserve(48u + WideEntries.size() * sizeof(WideTreeEntry) + Primitives.size() * sizeof(RayTrianglePrimitive));
    AppendPodBytes(Bytes, WideTreeBinaryMagic);
    AppendPodBytes(Bytes, WideTreeBinaryVersion);
    AppendPodBytes(Bytes, RootBoundsMin);
    AppendPodBytes(Bytes, RootBoundsMax);
    AppendPodBytes(Bytes, MaxTreeDepth);
    const auto EntryCount = static_cast<uint32_t>(WideEntries.size());
    const auto PrimCount  = static_cast<uint32_t>(Primitives.size());
    AppendPodBytes(Bytes, EntryCount);
    AppendPodBytes(Bytes, PrimCount);
    for (const auto& Entry : WideEntries)
    {
        AppendPodBytes(Bytes, Entry);
    }
    for (const auto& Prim : Primitives)
    {
        AppendPodBytes(Bytes, Prim);
    }
    return Bytes;
}

bool WideBoundingTree::DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes)
{
    Reset();
    std::span<const std::byte> Cursor = RawBytes;
    uint32_t Magic = 0u, Version = 0u, EntryCount = 0u, PrimCount = 0u;
    if (!ReadPodBytes(Cursor, Magic) || !ReadPodBytes(Cursor, Version) ||
        Magic != WideTreeBinaryMagic || Version != WideTreeBinaryVersion ||
        !ReadPodBytes(Cursor, RootBoundsMin) || !ReadPodBytes(Cursor, RootBoundsMax) ||
        !ReadPodBytes(Cursor, MaxTreeDepth) || !ReadPodBytes(Cursor, EntryCount) || !ReadPodBytes(Cursor, PrimCount))
    {
        return false;
    }

    const std::size_t Expected =
        static_cast<std::size_t>(EntryCount) * sizeof(WideTreeEntry) +
        static_cast<std::size_t>(PrimCount) * sizeof(RayTrianglePrimitive);
    if (Cursor.size() < Expected)
    {
        Reset();
        return false;
    }

    WideEntries.resize(EntryCount);
    Primitives.resize(PrimCount);
    for (uint32_t Idx = 0u; Idx < EntryCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, WideEntries[Idx]);
    }
    for (uint32_t Idx = 0u; Idx < PrimCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, Primitives[Idx]);
    }
    return true;
}

} // namespace Frontier::SpatialAcceleration
