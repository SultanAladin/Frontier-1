/*============================================================================================================================================
                                                           COMPRESSEDWIDETREE.CPP
============================================================================================================================================*/
// 🧩 64-byte 8-bit quantized 4-wide SAH BVH4 (Ylitie-Karras-Laine 2017 DP collapse), multi-instance TLAS4+BLAS4, and fast AABB refit

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace Frontier::SpatialAcceleration
{

namespace
{

constexpr uint32_t SahBinCount       = 32u;
constexpr double   WideEntryCostRatio = 3.5;

struct AxisAlignedBox
{
    float Min[3] {  std::numeric_limits<float>::max(),    std::numeric_limits<float>::max(),    std::numeric_limits<float>::max() };
    float Max[3] { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

    void ExpandPoint(Mathematics::Axis3D P) noexcept
    {
        Min[0] = std::min(Min[0], P.X); Max[0] = std::max(Max[0], P.X);
        Min[1] = std::min(Min[1], P.Y); Max[1] = std::max(Max[1], P.Y);
        Min[2] = std::min(Min[2], P.Z); Max[2] = std::max(Max[2], P.Z);
    }

    void ExpandBox(const AxisAlignedBox& Other) noexcept
    {
        for (int A = 0; A < 3; ++A)
        {
            Min[A] = std::min(Min[A], Other.Min[A]);
            Max[A] = std::max(Max[A], Other.Max[A]);
        }
    }

    [[nodiscard]] double SurfaceArea() const noexcept
    {
        const double EX = static_cast<double>(Max[0]) - static_cast<double>(Min[0]);
        const double EY = static_cast<double>(Max[1]) - static_cast<double>(Min[1]);
        const double EZ = static_cast<double>(Max[2]) - static_cast<double>(Min[2]);
        if (EX <= 0.0 || EY <= 0.0 || EZ <= 0.0)
        {
            return 0.0;
        }
        return 2.0 * (EX * EY + EY * EZ + EZ * EX);
    }
};

[[nodiscard]] AxisAlignedBox ComputePrimitiveBox(const RayTrianglePrimitive& Tri) noexcept
{
    AxisAlignedBox Box {};
    Box.ExpandPoint(Tri.Corner0);
    Box.ExpandPoint(Tri.Corner0 + Tri.Edge01);
    Box.ExpandPoint(Tri.Corner0 + Tri.Edge02);
    for (int A = 0; A < 3; ++A)
    {
        if (Box.Max[A] - Box.Min[A] < 1.0e-6f)
        {
            Box.Min[A] -= 5.0e-7f;
            Box.Max[A] += 5.0e-7f;
        }
    }
    return Box;
}

struct WideSlotCandidate
{
    AxisAlignedBox Box   {};
    uint32_t       Count = 0u;
    uint32_t       Link  = CompressedEmptySlotLink;
};

void EncodeQuantizedWideEntry(
    CompressedWideEntry64&               OutEntry,
    std::span<WideSlotCandidate>        Slots) noexcept
{
    OutEntry = {};
    if (Slots.empty())
    {
        return;
    }

    std::sort(Slots.begin(), Slots.end(), [](const WideSlotCandidate& A, const WideSlotCandidate& B)
    {
        const double AreaDiff = B.Box.SurfaceArea() - A.Box.SurfaceArea();
        if (std::fabs(AreaDiff) > 1.0e-12)
        {
            return AreaDiff > 0.0;
        }
        return B.Count > A.Count;
    });

    AxisAlignedBox EnclosingBounds {};
    for (const auto& S : Slots)
    {
        EnclosingBounds.ExpandBox(S.Box);
    }

    OutEntry.OriginX = EnclosingBounds.Min[0];
    OutEntry.OriginY = EnclosingBounds.Min[1];
    OutEntry.OriginZ = EnclosingBounds.Min[2];
    const float Origins[3] { OutEntry.OriginX, OutEntry.OriginY, OutEntry.OriginZ };

    int32_t Exponents[3] { 0, 0, 0 };
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const double Span = std::max(1.0e-30, static_cast<double>(EnclosingBounds.Max[Axis]) - static_cast<double>(Origins[Axis]));
        Exponents[Axis] = std::max(-100, static_cast<int32_t>(std::ceil(std::log2(Span / static_cast<double>(CompressedQuantizationSteps - 2u)))));
    }

    OutEntry.PackedExponents =
        (static_cast<uint32_t>(Exponents[0] + 127) & 255u) |
        ((static_cast<uint32_t>(Exponents[1] + 127) & 255u) << 8u) |
        ((static_cast<uint32_t>(Exponents[2] + 127) & 255u) << 16u) |
        (static_cast<uint32_t>(Slots.size()) << 24u);

    uint8_t Quantized[24] {};
    for (std::size_t SIdx = 0u; SIdx < Slots.size() && SIdx < 4u; ++SIdx)
    {
        const auto& Sub = Slots[SIdx];
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float  Origin = Origins[Axis];
            const double ScaleD = std::ldexp(1.0, Exponents[Axis]);
            const float  ScaleF = static_cast<float>(ScaleD);

            int32_t QMin = std::clamp(
                static_cast<int32_t>(std::floor((static_cast<double>(Sub.Box.Min[Axis]) - static_cast<double>(Origin)) / ScaleD)),
                0,
                static_cast<int32_t>(CompressedQuantizationSteps));
            int32_t QMax = std::clamp(
                static_cast<int32_t>(std::ceil((static_cast<double>(Sub.Box.Max[Axis]) - static_cast<double>(Origin)) / ScaleD)),
                0,
                static_cast<int32_t>(CompressedQuantizationSteps));

            while (QMin > 0 && (Origin + static_cast<float>(QMin) * ScaleF) > Sub.Box.Min[Axis])
            {
                --QMin;
            }
            while (QMax < static_cast<int32_t>(CompressedQuantizationSteps) &&
                   (Origin + static_cast<float>(QMax) * ScaleF) < Sub.Box.Max[Axis])
            {
                ++QMax;
            }
            Quantized[Axis * 8 + static_cast<int>(SIdx)]     = static_cast<uint8_t>(QMin);
            Quantized[Axis * 8 + 4 + static_cast<int>(SIdx)] = static_cast<uint8_t>(QMax);
        }
    }

    for (int Word = 0; Word < 6; ++Word)
    {
        OutEntry.QuantizedBounds[Word] =
            static_cast<uint32_t>(Quantized[Word * 4 + 0]) |
            (static_cast<uint32_t>(Quantized[Word * 4 + 1]) << 8u) |
            (static_cast<uint32_t>(Quantized[Word * 4 + 2]) << 16u) |
            (static_cast<uint32_t>(Quantized[Word * 4 + 3]) << 24u);
    }

    uint32_t Counts[4] { 0u, 0u, 0u, 0u };
    for (std::size_t SIdx = 0u; SIdx < Slots.size() && SIdx < 4u; ++SIdx)
    {
        Counts[SIdx]               = Slots[SIdx].Count;
        OutEntry.SubtreeLinks[SIdx] = Slots[SIdx].Link;
    }
    OutEntry.PackedCounts01 = Counts[0] | (Counts[1] << 16u);
    OutEntry.PackedCounts23 = Counts[2] | (Counts[3] << 16u);
}

void BuildSahBinaryEntries(
    std::span<const AxisAlignedBox>        PrimitiveBoxes,
    std::vector<uint32_t>&                 OutOrder,
    std::vector<RefittableBinaryEntry32>&  OutBinary,
    uint32_t                               MaxLeafPrimitives)
{
    OutBinary.clear();
    const uint32_t PrimitiveCountTotal = static_cast<uint32_t>(PrimitiveBoxes.size());
    OutOrder.resize(PrimitiveCountTotal);
    std::iota(OutOrder.begin(), OutOrder.end(), 0u);
    if (PrimitiveCountTotal == 0u)
    {
        return;
    }

    OutBinary.reserve(PrimitiveCountTotal * 2u);
    OutBinary.push_back({});

    struct BuildRangeTask
    {
        uint32_t EntryIndex;
        uint32_t First;
        uint32_t Count;
    };
    std::vector<BuildRangeTask> Stack;
    Stack.reserve(128u);
    Stack.push_back({ 0u, 0u, PrimitiveCountTotal });

    while (!Stack.empty())
    {
        const BuildRangeTask Task = Stack.back();
        Stack.pop_back();

        AxisAlignedBox Bounds {};
        AxisAlignedBox CentroidBounds {};
        for (uint32_t I = Task.First; I < Task.First + Task.Count; ++I)
        {
            const auto& B = PrimitiveBoxes[OutOrder[I]];
            Bounds.ExpandBox(B);
            CentroidBounds.ExpandPoint({
                0.5f * (B.Min[0] + B.Max[0]),
                0.5f * (B.Min[1] + B.Max[1]),
                0.5f * (B.Min[2] + B.Max[2]),
            });
        }

        auto& Entry = OutBinary[Task.EntryIndex];
        for (int A = 0; A < 3; ++A)
        {
            Entry.BoundsMin[A] = Bounds.Min[A];
            Entry.BoundsMax[A] = Bounds.Max[A];
        }

        if (Task.Count <= MaxLeafPrimitives)
        {
            Entry.LeftOrFirst    = Task.First;
            Entry.PrimitiveCount = Task.Count;
            continue;
        }

        int    BestAxis  = -1;
        uint32_t BestBin = 0u;
        double BestCost  = std::numeric_limits<double>::infinity();
        const double EnclosingArea = std::max(1.0e-20, Bounds.SurfaceArea());

        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float CMin = CentroidBounds.Min[Axis];
            const float CSpan = CentroidBounds.Max[Axis] - CMin;
            if (CSpan <= 1.0e-7f)
            {
                continue;
            }
            struct BinBucket
            {
                AxisAlignedBox Box   {};
                uint32_t       Count = 0u;
            };
            std::array<BinBucket, SahBinCount> Bins {};
            const float Scale = static_cast<float>(SahBinCount) / CSpan;
            for (uint32_t I = Task.First; I < Task.First + Task.Count; ++I)
            {
                const auto& B = PrimitiveBoxes[OutOrder[I]];
                const float C = 0.5f * (B.Min[Axis] + B.Max[Axis]);
                const uint32_t BinIdx = std::min(SahBinCount - 1u, static_cast<uint32_t>(std::max(0.0f, (C - CMin) * Scale)));
                Bins[BinIdx].Box.ExpandBox(B);
                ++Bins[BinIdx].Count;
            }

            std::array<double, SahBinCount - 1u> LeftAreas {};
            std::array<uint32_t, SahBinCount - 1u> LeftCounts {};
            AxisAlignedBox AccumLeft {};
            uint32_t CountLeft = 0u;
            for (uint32_t B = 0u; B + 1u < SahBinCount; ++B)
            {
                if (Bins[B].Count > 0u)
                {
                    AccumLeft.ExpandBox(Bins[B].Box);
                    CountLeft += Bins[B].Count;
                }
                LeftAreas[B]  = (CountLeft > 0u) ? AccumLeft.SurfaceArea() : 0.0;
                LeftCounts[B] = CountLeft;
            }

            AxisAlignedBox AccumRight {};
            uint32_t CountRight = 0u;
            for (int B = static_cast<int>(SahBinCount) - 1; B >= 1; --B)
            {
                const auto BU = static_cast<uint32_t>(B);
                if (Bins[BU].Count > 0u)
                {
                    AccumRight.ExpandBox(Bins[BU].Box);
                    CountRight += Bins[BU].Count;
                }
                if (LeftCounts[BU - 1u] == 0u || CountRight == 0u)
                {
                    continue;
                }
                const double Cost = 0.25 + (LeftAreas[BU - 1u] * LeftCounts[BU - 1u] + AccumRight.SurfaceArea() * CountRight) / EnclosingArea;
                if (Cost < BestCost)
                {
                    BestCost = Cost;
                    BestAxis = Axis;
                    BestBin  = BU;
                }
            }
        }

        uint32_t Mid = Task.First + Task.Count / 2u;
        if (BestAxis >= 0 && (BestCost < static_cast<double>(Task.Count) || Task.Count > CompressedLeafPrimCeiling))
        {
            const float CMin  = CentroidBounds.Min[BestAxis];
            const float CSpan = CentroidBounds.Max[BestAxis] - CMin;
            const float Scale = static_cast<float>(SahBinCount) / CSpan;
            auto BeginIt = OutOrder.begin() + static_cast<std::ptrdiff_t>(Task.First);
            auto EndIt   = BeginIt + static_cast<std::ptrdiff_t>(Task.Count);
            auto SplitIt = std::partition(BeginIt, EndIt, [&](uint32_t Idx)
            {
                const auto& B = PrimitiveBoxes[Idx];
                const float C = 0.5f * (B.Min[BestAxis] + B.Max[BestAxis]);
                const uint32_t BinIdx = std::min(SahBinCount - 1u, static_cast<uint32_t>(std::max(0.0f, (C - CMin) * Scale)));
                return BinIdx < BestBin;
            });
            Mid = static_cast<uint32_t>(std::distance(OutOrder.begin(), SplitIt));
            if (Mid == Task.First || Mid == Task.First + Task.Count)
            {
                Mid = Task.First + Task.Count / 2u;
            }
        }
        else if (Task.Count <= CompressedLeafPrimCeiling)
        {
            Entry.LeftOrFirst    = Task.First;
            Entry.PrimitiveCount = Task.Count;
            continue;
        }

        const uint32_t LeftBranch = static_cast<uint32_t>(OutBinary.size());
        OutBinary.push_back({});
        OutBinary.push_back({});
        OutBinary[Task.EntryIndex].LeftOrFirst    = LeftBranch;
        OutBinary[Task.EntryIndex].PrimitiveCount = 0u;

        const uint32_t LeftCount  = Mid - Task.First;
        const uint32_t RightCount = Task.Count - LeftCount;
        // Push Right first so Left is popped first (preserves contiguous left-before-right leaf ordering)
        Stack.push_back({ LeftBranch + 1u, Mid,        RightCount });
        Stack.push_back({ LeftBranch,      Task.First, LeftCount  });
    }
}

void CollapseBinaryIntoQuantizedWideEntries(
    const std::vector<RefittableBinaryEntry32>& Binary,
    uint32_t                                    EntryStartOffset,
    uint32_t                                    MaxLeafCount,
    std::vector<CompressedWideEntry64>&          OutEntries,
    uint32_t&                                   OutMaxDepth,
    std::vector<uint32_t>*                      OptionalPostOrderInternal = nullptr)
{
    OutEntries.clear();
    OutMaxDepth = 0u;
    if (Binary.empty())
    {
        return;
    }

    const std::size_t EntryCount = Binary.size();
    std::vector<uint32_t> PostOrder;
    PostOrder.reserve(EntryCount);
    {
        std::vector<uint32_t> Walk { 0u };
        while (!Walk.empty())
        {
            const uint32_t Cur = Walk.back();
            Walk.pop_back();
            PostOrder.push_back(Cur);
            if (Binary[Cur].PrimitiveCount == 0u)
            {
                const uint32_t L = Binary[Cur].LeftOrFirst;
                Walk.push_back(L);
                Walk.push_back(L + 1u);
            }
        }
    }

    if (OptionalPostOrderInternal != nullptr)
    {
        OptionalPostOrderInternal->clear();
        for (auto It = PostOrder.rbegin(); It != PostOrder.rend(); ++It)
        {
            if (Binary[*It].PrimitiveCount == 0u)
            {
                OptionalPostOrderInternal->push_back(*It);
            }
        }
    }

    const auto BoxOf = [&](uint32_t E) noexcept -> AxisAlignedBox
    {
        AxisAlignedBox B {};
        for (int A = 0; A < 3; ++A)
        {
            B.Min[A] = Binary[E].BoundsMin[A];
            B.Max[A] = Binary[E].BoundsMax[A];
        }
        return B;
    };

    std::vector<uint32_t> PrimCount(EntryCount, 0u);
    std::vector<uint32_t> PrimFirst(EntryCount, 0u);
    std::vector<uint8_t>  Contiguous(EntryCount, 0u);
    std::vector<double>   SlotCost(EntryCount * 5u, 0.0);
    std::vector<int8_t>   Decision(EntryCount * 5u, 0);

    for (auto It = PostOrder.rbegin(); It != PostOrder.rend(); ++It)
    {
        const uint32_t E        = *It;
        const uint32_t Count    = Binary[E].PrimitiveCount;
        const double   AreaVal  = BoxOf(E).SurfaceArea();
        if (Count > 0u)
        {
            PrimCount[E]  = Count;
            PrimFirst[E]  = Binary[E].LeftOrFirst;
            Contiguous[E] = 1u;
            const double LeafCost = AreaVal * static_cast<double>(Count);
            for (uint32_t S = 1u; S <= 4u; ++S)
            {
                SlotCost[E * 5u + S] = LeafCost;
                Decision[E * 5u + S] = -1;
            }
            continue;
        }

        const uint32_t L = Binary[E].LeftOrFirst;
        const uint32_t R = L + 1u;
        PrimCount[E]  = PrimCount[L] + PrimCount[R];
        PrimFirst[E]  = std::min(PrimFirst[L], PrimFirst[R]);
        Contiguous[E] = (Contiguous[L] != 0u && Contiguous[R] != 0u &&
                         (PrimFirst[L] + PrimCount[L] == PrimFirst[R] ||
                          PrimFirst[R] + PrimCount[R] == PrimFirst[L])) ? 1u : 0u;

        for (uint32_t S = 2u; S <= 4u; ++S)
        {
            double Best = std::numeric_limits<double>::infinity();
            int8_t BestLeft = 1;
            for (uint32_t LS = 1u; LS < S; ++LS)
            {
                const double Cand = SlotCost[L * 5u + LS] + SlotCost[R * 5u + (S - LS)];
                if (Cand < Best)
                {
                    Best     = Cand;
                    BestLeft = static_cast<int8_t>(LS);
                }
            }
            SlotCost[E * 5u + S] = Best;
            Decision[E * 5u + S] = BestLeft;
        }

        double BestSingle = AreaVal * WideEntryCostRatio + SlotCost[E * 5u + 4u];
        int8_t SingleCode = 0;
        if (Contiguous[E] != 0u && PrimCount[E] <= MaxLeafCount)
        {
            const double LeafCost = AreaVal * static_cast<double>(PrimCount[E]);
            if (LeafCost <= BestSingle)
            {
                BestSingle = LeafCost;
                SingleCode = -1;
            }
        }
        SlotCost[E * 5u + 1u] = BestSingle;
        Decision[E * 5u + 1u] = SingleCode;
        for (uint32_t S = 2u; S <= 4u; ++S)
        {
            if (BestSingle < SlotCost[E * 5u + S])
            {
                SlotCost[E * 5u + S] = BestSingle;
                Decision[E * 5u + S] = SingleCode;
            }
        }
    }

    auto CollectSubtrees = [&](auto& Self, uint32_t E, int8_t Slots, std::vector<uint32_t>& OutSelectedSlots) -> void
    {
        const int8_t Code = Decision[E * 5u + static_cast<uint32_t>(Slots)];
        if (Code <= 0 || Binary[E].PrimitiveCount > 0u)
        {
            OutSelectedSlots.push_back(E);
            return;
        }
        const uint32_t L = Binary[E].LeftOrFirst;
        Self(Self, L, Code, OutSelectedSlots);
        Self(Self, L + 1u, static_cast<int8_t>(Slots - Code), OutSelectedSlots);
    };

    OutEntries.push_back({});
    struct CollapseTask
    {
        uint32_t BinaryEntry;
        uint32_t WideIndex;
        uint32_t Depth;
    };
    std::vector<CollapseTask> Pending { { 0u, 0u, 1u } };

    while (!Pending.empty())
    {
        const CollapseTask Cur = Pending.back();
        Pending.pop_back();
        OutMaxDepth = std::max(OutMaxDepth, Cur.Depth);

        std::vector<uint32_t> SelectedSlots;
        SelectedSlots.reserve(4u);
        if (Binary[Cur.BinaryEntry].PrimitiveCount > 0u)
        {
            SelectedSlots.push_back(Cur.BinaryEntry);
        }
        else if (Decision[Cur.BinaryEntry * 5u + 4u] > 0)
        {
            const uint32_t L    = Binary[Cur.BinaryEntry].LeftOrFirst;
            const int8_t   LeftS = Decision[Cur.BinaryEntry * 5u + 4u];
            CollectSubtrees(CollectSubtrees, L, LeftS, SelectedSlots);
            CollectSubtrees(CollectSubtrees, L + 1u, static_cast<int8_t>(4 - LeftS), SelectedSlots);
        }
        else
        {
            const uint32_t L = Binary[Cur.BinaryEntry].LeftOrFirst;
            SelectedSlots.push_back(L);
            SelectedSlots.push_back(L + 1u);
        }

        std::vector<WideSlotCandidate> Candidates;
        Candidates.reserve(SelectedSlots.size());
        for (uint32_t M : SelectedSlots)
        {
            if (Binary[M].PrimitiveCount > 0u || Decision[M * 5u + 1u] == -1)
            {
                Candidates.push_back({ BoxOf(M), PrimCount[M], PrimFirst[M] });
            }
            else
            {
                const uint32_t NextWideIdx = static_cast<uint32_t>(OutEntries.size());
                OutEntries.push_back({});
                Pending.push_back({ M, NextWideIdx, Cur.Depth + 1u });
                Candidates.push_back({ BoxOf(M), 0u, EntryStartOffset + NextWideIdx });
            }
        }
        EncodeQuantizedWideEntry(OutEntries[Cur.WideIndex], Candidates);
    }
}

[[nodiscard]] bool IntersectTriangleMollerTrumbore(
    const RaySegmentQuery&      Query,
    const RayTrianglePrimitive& Prim,
    float                       MaxT,
    float&                      OutT,
    float&                      OutU,
    float&                      OutV) noexcept
{
    const Mathematics::Axis3D PVec = Mathematics::EvaluateCross(Query.UnitDirection, Prim.Edge02);
    const float Det = Mathematics::EvaluateDot(Prim.Edge01, PVec);
    if (std::fabs(Det) <= 1.0e-9f)
    {
        return false;
    }
    const float InvDet = 1.0f / Det;
    const Mathematics::Axis3D TVec = Query.Origin - Prim.Corner0;
    const float U = Mathematics::EvaluateDot(TVec, PVec) * InvDet;
    if (U < 0.0f || U > 1.0f)
    {
        return false;
    }
    const Mathematics::Axis3D QVec = Mathematics::EvaluateCross(TVec, Prim.Edge01);
    const float V = Mathematics::EvaluateDot(Query.UnitDirection, QVec) * InvDet;
    if (V < 0.0f || (U + V) > 1.0f)
    {
        return false;
    }
    const float T = Mathematics::EvaluateDot(Prim.Edge02, QVec) * InvDet;
    if (T < Query.MinimumDistance || T >= MaxT)
    {
        return false;
    }
    OutT = T;
    OutU = U;
    OutV = V;
    return true;
}

void UnpackSubtreeBounds(
    const CompressedWideEntry64& CurEntry,
    uint32_t&                   OutActiveCount,
    float                       OutMinX[4],
    float                       OutMaxX[4],
    float                       OutMinY[4],
    float                       OutMaxY[4],
    float                       OutMinZ[4],
    float                       OutMaxZ[4],
    uint32_t                    OutCounts[4]) noexcept
{
    OutActiveCount = (CurEntry.PackedExponents >> 24u) & 0xFFu;
    const int32_t ExpX = static_cast<int32_t>(CurEntry.PackedExponents & 0xFFu) - 127;
    const int32_t ExpY = static_cast<int32_t>((CurEntry.PackedExponents >> 8u) & 0xFFu) - 127;
    const int32_t ExpZ = static_cast<int32_t>((CurEntry.PackedExponents >> 16u) & 0xFFu) - 127;
    const float ScaleX = std::ldexp(1.0f, ExpX);
    const float ScaleY = std::ldexp(1.0f, ExpY);
    const float ScaleZ = std::ldexp(1.0f, ExpZ);

    for (uint32_t S = 0u; S < 4u; ++S)
    {
        const uint32_t Shift = S * 8u;
        OutMinX[S] = CurEntry.OriginX + static_cast<float>((CurEntry.QuantizedBounds[0] >> Shift) & 0xFFu) * ScaleX;
        OutMaxX[S] = CurEntry.OriginX + static_cast<float>((CurEntry.QuantizedBounds[1] >> Shift) & 0xFFu) * ScaleX;
        OutMinY[S] = CurEntry.OriginY + static_cast<float>((CurEntry.QuantizedBounds[2] >> Shift) & 0xFFu) * ScaleY;
        OutMaxY[S] = CurEntry.OriginY + static_cast<float>((CurEntry.QuantizedBounds[3] >> Shift) & 0xFFu) * ScaleY;
        OutMinZ[S] = CurEntry.OriginZ + static_cast<float>((CurEntry.QuantizedBounds[4] >> Shift) & 0xFFu) * ScaleZ;
        OutMaxZ[S] = CurEntry.OriginZ + static_cast<float>((CurEntry.QuantizedBounds[5] >> Shift) & 0xFFu) * ScaleZ;
    }
    OutCounts[0] = CurEntry.PackedCounts01 & 0xFFFFu;
    OutCounts[1] = (CurEntry.PackedCounts01 >> 16u) & 0xFFFFu;
    OutCounts[2] = CurEntry.PackedCounts23 & 0xFFFFu;
    OutCounts[3] = (CurEntry.PackedCounts23 >> 16u) & 0xFFFFu;
}

[[nodiscard]] bool IntersectBoxSlab(
    Mathematics::Axis3D Origin,
    Mathematics::Axis3D InvDir,
    float               NearClip,
    float               FarClip,
    float               MinX,
    float               MaxX,
    float               MinY,
    float               MaxY,
    float               MinZ,
    float               MaxZ,
    float&              OutEntryT) noexcept
{
    const float TX0 = (MinX - Origin.X) * InvDir.X;
    const float TX1 = (MaxX - Origin.X) * InvDir.X;
    const float TY0 = (MinY - Origin.Y) * InvDir.Y;
    const float TY1 = (MaxY - Origin.Y) * InvDir.Y;
    const float TZ0 = (MinZ - Origin.Z) * InvDir.Z;
    const float TZ1 = (MaxZ - Origin.Z) * InvDir.Z;

    const float TMin = std::max({ NearClip, std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
    const float TMax = std::min({ FarClip,  std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
    OutEntryT        = TMin;
    return TMax >= TMin;
}

[[nodiscard]] float SafeReciprocal(float Component) noexcept
{
    if (std::fabs(Component) < 1.0e-9f)
    {
        return (Component >= 0.0f) ? 1.0e9f : -1.0e9f;
    }
    return 1.0f / Component;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPRESSED WIDE TREE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

void CompressedWideTree::Reset() noexcept
{
    WideEntries.clear();
    Primitives.clear();
    PrimitiveNormals.clear();
    SourceRemapOrder.clear();
    BinaryTreeEntries.clear();
    BinaryBottomUp.clear();
    RootBoundsMin = {};
    RootBoundsMax = {};
    MaxWideDepth  = 0u;
}

bool CompressedWideTree::BuildFromPrimitives(
    std::span<const RayTrianglePrimitive>  InputPrimitives,
    std::span<const TriangleSmoothNormals> InputNormals,
    uint32_t                               EntryStartOffset,
    uint32_t                               MaxLeafCount)
{
    Reset();
    if (InputPrimitives.empty())
    {
        return false;
    }

    std::vector<AxisAlignedBox> Boxes(InputPrimitives.size());
    for (std::size_t I = 0u; I < InputPrimitives.size(); ++I)
    {
        Boxes[I] = ComputePrimitiveBox(InputPrimitives[I]);
    }

    BuildSahBinaryEntries(Boxes, SourceRemapOrder, BinaryTreeEntries, 1u);

    Primitives.resize(SourceRemapOrder.size());
    const bool NormalsPresent = (InputNormals.size() == InputPrimitives.size());
    if (NormalsPresent)
    {
        PrimitiveNormals.resize(SourceRemapOrder.size());
    }
    for (std::size_t I = 0u; I < SourceRemapOrder.size(); ++I)
    {
        const uint32_t SrcIdx = SourceRemapOrder[I];
        Primitives[I]         = InputPrimitives[SrcIdx];
        if (NormalsPresent)
        {
            PrimitiveNormals[I] = InputNormals[SrcIdx];
        }
    }

    RootBoundsMin = { BinaryTreeEntries[0].BoundsMin[0], BinaryTreeEntries[0].BoundsMin[1], BinaryTreeEntries[0].BoundsMin[2] };
    RootBoundsMax = { BinaryTreeEntries[0].BoundsMax[0], BinaryTreeEntries[0].BoundsMax[1], BinaryTreeEntries[0].BoundsMax[2] };

    CollapseBinaryIntoQuantizedWideEntries(
        BinaryTreeEntries,
        EntryStartOffset,
        std::min(MaxLeafCount, CompressedLeafPrimCeiling),
        WideEntries,
        MaxWideDepth,
        &BinaryBottomUp);
    return !WideEntries.empty();
}

bool CompressedWideTree::RefitDeformedPrimitives(
    std::span<const RayTrianglePrimitive>  UpdatedSourcePrimitives,
    std::span<const TriangleSmoothNormals> UpdatedSourceNormals,
    uint32_t                               EntryStartOffset)
{
    if (BinaryTreeEntries.empty() || SourceRemapOrder.empty() || UpdatedSourcePrimitives.size() < SourceRemapOrder.size())
    {
        return false;
    }

    const bool NormalsPresent = (UpdatedSourceNormals.size() == UpdatedSourcePrimitives.size());
    for (std::size_t I = 0u; I < SourceRemapOrder.size(); ++I)
    {
        const uint32_t SrcIdx = SourceRemapOrder[I];
        Primitives[I]         = UpdatedSourcePrimitives[SrcIdx];
        if (NormalsPresent && PrimitiveNormals.size() == Primitives.size())
        {
            PrimitiveNormals[I] = UpdatedSourceNormals[SrcIdx];
        }
    }

    // 1. Update leaf binary AABBs
    for (auto& Bin : BinaryTreeEntries)
    {
        if (Bin.PrimitiveCount == 0u)
        {
            continue;
        }
        AxisAlignedBox LeafBox {};
        for (uint32_t K = 0u; K < Bin.PrimitiveCount; ++K)
        {
            LeafBox.ExpandBox(ComputePrimitiveBox(Primitives[Bin.LeftOrFirst + K]));
        }
        for (int A = 0; A < 3; ++A)
        {
            Bin.BoundsMin[A] = LeafBox.Min[A];
            Bin.BoundsMax[A] = LeafBox.Max[A];
        }
    }

    // 2. Bottom-up internal binary AABB union
    for (uint32_t InternalIdx : BinaryBottomUp)
    {
        auto& CurEntry      = BinaryTreeEntries[InternalIdx];
        const auto& L   = BinaryTreeEntries[CurEntry.LeftOrFirst];
        const auto& R   = BinaryTreeEntries[CurEntry.LeftOrFirst + 1u];
        for (int A = 0; A < 3; ++A)
        {
            CurEntry.BoundsMin[A] = std::min(L.BoundsMin[A], R.BoundsMin[A]);
            CurEntry.BoundsMax[A] = std::max(L.BoundsMax[A], R.BoundsMax[A]);
        }
    }

    RootBoundsMin = { BinaryTreeEntries[0].BoundsMin[0], BinaryTreeEntries[0].BoundsMin[1], BinaryTreeEntries[0].BoundsMin[2] };
    RootBoundsMax = { BinaryTreeEntries[0].BoundsMax[0], BinaryTreeEntries[0].BoundsMax[1], BinaryTreeEntries[0].BoundsMax[2] };

    CollapseBinaryIntoQuantizedWideEntries(
        BinaryTreeEntries,
        EntryStartOffset,
        CompressedLeafPrimCeiling,
        WideEntries,
        MaxWideDepth,
        nullptr);
    return true;
}

RayIntersectionResult CompressedWideTree::TraceClosestRay(
    const RaySegmentQuery& Query,
    bool                   IncludeTransmissive,
    uint32_t               RootEntryIndex) const noexcept
{
    RayIntersectionResult Best {};
    Best.HitDistanceMeters = Query.MaximumDistance;
    if (RootEntryIndex >= WideEntries.size())
    {
        return Best;
    }

    const Mathematics::Axis3D InvDir {
        SafeReciprocal(Query.UnitDirection.X),
        SafeReciprocal(Query.UnitDirection.Y),
        SafeReciprocal(Query.UnitDirection.Z),
    };

    struct PendingSlot
    {
        uint32_t Link;
        uint32_t Count;
        float    EntryT;
    };
    std::array<PendingSlot, 96u> Stack {};
    uint32_t StackTop = 0u;
    Stack[StackTop++] = { RootEntryIndex, 0u, Query.MinimumDistance };

    while (StackTop > 0u)
    {
        const PendingSlot Cur = Stack[--StackTop];
        if (Cur.EntryT >= Best.HitDistanceMeters)
        {
            continue;
        }
        if (Cur.Count > 0u)
        {
            for (uint32_t K = 0u; K < Cur.Count; ++K)
            {
                const uint32_t PIdx = Cur.Link + K;
                if (PIdx >= Primitives.size())
                {
                    break;
                }
                const auto& Prim = Primitives[PIdx];
                if (!IncludeTransmissive && Prim.OpaqueMask == 0u)
                {
                    continue;
                }
                ++Best.TestedPrimitives;
                float HitT = 0.0f, HitU = 0.0f, HitV = 0.0f;
                if (IntersectTriangleMollerTrumbore(Query, Prim, Best.HitDistanceMeters, HitT, HitU, HitV))
                {
                    Best.HitDistanceMeters = HitT;
                    Best.BarycentricU      = HitU;
                    Best.BarycentricV      = HitV;
                    Best.GeometricNormal   = Mathematics::NormalizeAxis(
                        Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02),
                        { 0.0f, 0.0f, 1.0f });
                    Best.SourceFaceIndex   = Prim.SourceFaceIndex;
                    Best.PrimitiveIndex    = PIdx;
                    Best.MaterialOrdinal   = Prim.MaterialOrdinal;
                    Best.HitConfirmed      = true;
                }
            }
            continue;
        }

        if (Cur.Link >= WideEntries.size())
        {
            continue;
        }
        ++Best.VisitedWideEntries;
        const auto& CurEntry = WideEntries[Cur.Link];
        uint32_t Active = 0u, Counts[4] {};
        float MinX[4], MaxX[4], MinY[4], MaxY[4], MinZ[4], MaxZ[4];
        UnpackSubtreeBounds(CurEntry, Active, MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Counts);

        std::array<PendingSlot, 4u> Hits {};
        uint32_t HitCount = 0u;
        for (uint32_t S = 0u; S < Active && S < 4u; ++S)
        {
            float EntryT = 0.0f;
            if (CurEntry.SubtreeLinks[S] != CompressedEmptySlotLink &&
                IntersectBoxSlab(
                    Query.Origin, InvDir, Query.MinimumDistance, Best.HitDistanceMeters,
                    MinX[S], MaxX[S], MinY[S], MaxY[S], MinZ[S], MaxZ[S], EntryT))
            {
                Hits[HitCount++] = { CurEntry.SubtreeLinks[S], Counts[S], EntryT };
            }
        }
        // Push farthest first so nearest is popped next (4-entry insertion sort avoids GCC -O3 introsort warning)
        for (uint32_t I = 1u; I < HitCount; ++I)
        {
            const PendingSlot Key = Hits[I];
            uint32_t J = I;
            while (J > 0u && Hits[J - 1u].EntryT < Key.EntryT)
            {
                Hits[J] = Hits[J - 1u];
                --J;
            }
            Hits[J] = Key;
        }
        for (uint32_t H = 0u; H < HitCount && StackTop < Stack.size(); ++H)
        {
            Stack[StackTop++] = Hits[H];
        }
    }
    return Best;
}

bool CompressedWideTree::QueryRayOccluded(
    const RaySegmentQuery& Query,
    uint32_t               RootEntryIndex) const noexcept
{
    if (RootEntryIndex >= WideEntries.size())
    {
        return false;
    }

    const Mathematics::Axis3D InvDir {
        SafeReciprocal(Query.UnitDirection.X),
        SafeReciprocal(Query.UnitDirection.Y),
        SafeReciprocal(Query.UnitDirection.Z),
    };

    struct PendingSlot
    {
        uint32_t Link;
        uint32_t Count;
    };
    std::array<PendingSlot, 96u> Stack {};
    uint32_t StackTop = 0u;
    Stack[StackTop++] = { RootEntryIndex, 0u };

    while (StackTop > 0u)
    {
        const PendingSlot Cur = Stack[--StackTop];
        if (Cur.Count > 0u)
        {
            for (uint32_t K = 0u; K < Cur.Count; ++K)
            {
                const uint32_t PIdx = Cur.Link + K;
                if (PIdx >= Primitives.size())
                {
                    break;
                }
                const auto& Prim = Primitives[PIdx];
                if (Prim.OpaqueMask == 0u)
                {
                    continue;
                }
                float HitT = 0.0f, HitU = 0.0f, HitV = 0.0f;
                if (IntersectTriangleMollerTrumbore(Query, Prim, Query.MaximumDistance, HitT, HitU, HitV))
                {
                    return true;
                }
            }
            continue;
        }

        if (Cur.Link >= WideEntries.size())
        {
            continue;
        }
        const auto& CurEntry = WideEntries[Cur.Link];
        uint32_t Active = 0u, Counts[4] {};
        float MinX[4], MaxX[4], MinY[4], MaxY[4], MinZ[4], MaxZ[4];
        UnpackSubtreeBounds(CurEntry, Active, MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Counts);

        for (uint32_t S = 0u; S < Active && S < 4u && StackTop < Stack.size(); ++S)
        {
            float EntryT = 0.0f;
            if (CurEntry.SubtreeLinks[S] != CompressedEmptySlotLink &&
                IntersectBoxSlab(
                    Query.Origin, InvDir, Query.MinimumDistance, Query.MaximumDistance,
                    MinX[S], MaxX[S], MinY[S], MaxY[S], MinZ[S], MaxZ[S], EntryT))
            {
                Stack[StackTop++] = { CurEntry.SubtreeLinks[S], Counts[S] };
            }
        }
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         COMPRESSED INSTANCE FOREST IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

void CompressedInstanceForest::Reset() noexcept
{
    SharedEntries.clear();
    Instances.clear();
    SharedPrimitives.clear();
    TlasRootIndex = 0u;
}

bool CompressedInstanceForest::AssembleInstanceForest(
    const CompressedWideTree&              StaticWorldBlas,
    const CompressedWideTree&              PrototypeBlas,
    std::span<const RigidInstancePlacement32> PrototypeInstances)
{
    Reset();

    std::vector<RigidInstancePlacement32> RawInstances;
    std::vector<AxisAlignedBox>        WorldBoxes;

    const auto ComputeRotatedWorldBox = [](
        Mathematics::Axis3D          LocalMin,
        Mathematics::Axis3D          LocalMax,
        const RigidInstancePlacement32& Inst) noexcept -> AxisAlignedBox
    {
        float VertShift = 0.0f;
        std::memcpy(&VertShift, &Inst.VerticalOffsetBits, sizeof(float));
        AxisAlignedBox Out {};
        for (int CX = 0; CX < 2; ++CX)
        {
            for (int CY = 0; CY < 2; ++CY)
            {
                for (int CZ = 0; CZ < 2; ++CZ)
                {
                    const float LX = (CX == 0) ? LocalMin.X : LocalMax.X;
                    const float LY = (CY == 0) ? LocalMin.Y : LocalMax.Y;
                    const float LZ = (CZ == 0) ? LocalMin.Z : LocalMax.Z;
                    if (Inst.AxisConvention == 1u)
                    {
                        const float WX = Inst.CosineYaw * LX - Inst.SineYaw * LZ + Inst.TranslationA;
                        const float WY = LY + VertShift;
                        const float WZ = Inst.SineYaw * LX + Inst.CosineYaw * LZ + Inst.TranslationB;
                        Out.ExpandPoint({ WX, WY, WZ });
                    }
                    else
                    {
                        const float WX = Inst.CosineYaw * LX - Inst.SineYaw * LY + Inst.TranslationA;
                        const float WY = Inst.SineYaw * LX + Inst.CosineYaw * LY + Inst.TranslationB;
                        const float WZ = LZ + VertShift;
                        Out.ExpandPoint({ WX, WY, WZ });
                    }
                }
            }
        }
        return Out;
    };

    // Reserve placeholder BLAS roots (resolved once TLAS entry count is known)
    const bool StaticPresent = !StaticWorldBlas.WideEntries.empty() && !StaticWorldBlas.Primitives.empty();
    const bool PrototypePresent  = !PrototypeBlas.WideEntries.empty() && !PrototypeBlas.Primitives.empty();
    if (!StaticPresent && (!PrototypePresent || PrototypeInstances.empty()))
    {
        return false;
    }

    if (StaticPresent)
    {
        RigidInstancePlacement32 IdentityStatic {};
        IdentityStatic.BlasRootEntryIndex = 0u; // Tag 0 = StaticWorldBlas
        RawInstances.push_back(IdentityStatic);
        AxisAlignedBox SBox {};
        SBox.ExpandPoint(StaticWorldBlas.RootBoundsMin);
        SBox.ExpandPoint(StaticWorldBlas.RootBoundsMax);
        WorldBoxes.push_back(SBox);
    }
    if (PrototypePresent)
    {
        for (auto Inst : PrototypeInstances)
        {
            Inst.BlasRootEntryIndex = 1u; // Tag 1 = PrototypeBlas
            RawInstances.push_back(Inst);
            WorldBoxes.push_back(ComputeRotatedWorldBox(PrototypeBlas.RootBoundsMin, PrototypeBlas.RootBoundsMax, Inst));
        }
    }

    std::vector<uint32_t>                InstOrder;
    std::vector<RefittableBinaryEntry32> TlasBinary;
    BuildSahBinaryEntries(WorldBoxes, InstOrder, TlasBinary, 1u);

    uint32_t TlasDepth = 0u;
    CollapseBinaryIntoQuantizedWideEntries(TlasBinary, 0u, 1u, SharedEntries, TlasDepth, nullptr);
    const uint32_t TlasEntryCount = static_cast<uint32_t>(SharedEntries.size());

    // Append StaticWorldBlas and PrototypeBlas with rebased internal entry links and primitive offsets
    uint32_t StaticRootOffset = CompressedEmptySlotLink;
    if (StaticPresent)
    {
        StaticRootOffset        = static_cast<uint32_t>(SharedEntries.size());
        const uint32_t PrimStart = static_cast<uint32_t>(SharedPrimitives.size());
        SharedPrimitives.insert(SharedPrimitives.end(), StaticWorldBlas.Primitives.begin(), StaticWorldBlas.Primitives.end());

        std::vector<CompressedWideEntry64> RebasedStatic;
        uint32_t DummyDepth = 0u;
        CollapseBinaryIntoQuantizedWideEntries(
            StaticWorldBlas.BinaryTreeEntries, StaticRootOffset, CompressedLeafPrimCeiling, RebasedStatic, DummyDepth, nullptr);
        for (auto& N : RebasedStatic)
        {
            const uint32_t C0 = N.PackedCounts01 & 0xFFFFu;
            const uint32_t C1 = (N.PackedCounts01 >> 16u) & 0xFFFFu;
            const uint32_t C2 = N.PackedCounts23 & 0xFFFFu;
            const uint32_t C3 = (N.PackedCounts23 >> 16u) & 0xFFFFu;
            const uint32_t Counts[4] { C0, C1, C2, C3 };
            for (int S = 0; S < 4; ++S)
            {
                if (Counts[S] > 0u && N.SubtreeLinks[S] != CompressedEmptySlotLink)
                {
                    N.SubtreeLinks[S] += PrimStart;
                }
            }
        }
        SharedEntries.insert(SharedEntries.end(), RebasedStatic.begin(), RebasedStatic.end());
    }

    uint32_t ProtoRootOffset = CompressedEmptySlotLink;
    if (PrototypePresent)
    {
        ProtoRootOffset         = static_cast<uint32_t>(SharedEntries.size());
        const uint32_t PrimStart = static_cast<uint32_t>(SharedPrimitives.size());
        SharedPrimitives.insert(SharedPrimitives.end(), PrototypeBlas.Primitives.begin(), PrototypeBlas.Primitives.end());

        std::vector<CompressedWideEntry64> RebasedProto;
        uint32_t DummyDepth = 0u;
        CollapseBinaryIntoQuantizedWideEntries(
            PrototypeBlas.BinaryTreeEntries, ProtoRootOffset, CompressedLeafPrimCeiling, RebasedProto, DummyDepth, nullptr);
        for (auto& N : RebasedProto)
        {
            const uint32_t C0 = N.PackedCounts01 & 0xFFFFu;
            const uint32_t C1 = (N.PackedCounts01 >> 16u) & 0xFFFFu;
            const uint32_t C2 = N.PackedCounts23 & 0xFFFFu;
            const uint32_t C3 = (N.PackedCounts23 >> 16u) & 0xFFFFu;
            const uint32_t Counts[4] { C0, C1, C2, C3 };
            for (int S = 0; S < 4; ++S)
            {
                if (Counts[S] > 0u && N.SubtreeLinks[S] != CompressedEmptySlotLink)
                {
                    N.SubtreeLinks[S] += PrimStart;
                }
            }
        }
        SharedEntries.insert(SharedEntries.end(), RebasedProto.begin(), RebasedProto.end());
    }

    Instances.resize(InstOrder.size());
    for (std::size_t I = 0u; I < InstOrder.size(); ++I)
    {
        RigidInstancePlacement32 Ordered = RawInstances[InstOrder[I]];
        Ordered.BlasRootEntryIndex     = (Ordered.BlasRootEntryIndex == 0u) ? StaticRootOffset : ProtoRootOffset;
        Instances[I]                  = Ordered;
    }

    (void)TlasEntryCount;
    TlasRootIndex = 0u;
    return true;
}

RayIntersectionResult CompressedInstanceForest::TraceClosestRay(
    const RaySegmentQuery& Query,
    bool                   IncludeTransmissive) const noexcept
{
    RayIntersectionResult Best {};
    Best.HitDistanceMeters = Query.MaximumDistance;
    if (SharedEntries.empty() || Instances.empty())
    {
        return Best;
    }

    const Mathematics::Axis3D WorldInvDir {
        SafeReciprocal(Query.UnitDirection.X),
        SafeReciprocal(Query.UnitDirection.Y),
        SafeReciprocal(Query.UnitDirection.Z),
    };

    struct PendingSlot
    {
        uint32_t Link;
        uint32_t Count;
        float    EntryT;
    };
    std::array<PendingSlot, 64u> TlasStack {};
    uint32_t TlasTop = 0u;
    TlasStack[TlasTop++] = { TlasRootIndex, 0u, Query.MinimumDistance };

    while (TlasTop > 0u)
    {
        const PendingSlot CurTlas = TlasStack[--TlasTop];
        if (CurTlas.EntryT >= Best.HitDistanceMeters)
        {
            continue;
        }

        if (CurTlas.Count > 0u)
        {
            const uint32_t InstIdx = CurTlas.Link;
            if (InstIdx >= Instances.size())
            {
                continue;
            }
            const auto& Inst = Instances[InstIdx];
            float VertShift = 0.0f;
            std::memcpy(&VertShift, &Inst.VerticalOffsetBits, sizeof(float));

            RaySegmentQuery LocalQ = Query;
            LocalQ.MaximumDistance = Best.HitDistanceMeters;
            if (Inst.AxisConvention == 1u)
            {
                const float DX = Query.Origin.X - Inst.TranslationA;
                const float DY = Query.Origin.Y - VertShift;
                const float DZ = Query.Origin.Z - Inst.TranslationB;
                LocalQ.Origin        = { Inst.CosineYaw * DX + Inst.SineYaw * DZ, DY, -Inst.SineYaw * DX + Inst.CosineYaw * DZ };
                LocalQ.UnitDirection = { Inst.CosineYaw * Query.UnitDirection.X + Inst.SineYaw * Query.UnitDirection.Z,
                                         Query.UnitDirection.Y,
                                         -Inst.SineYaw * Query.UnitDirection.X + Inst.CosineYaw * Query.UnitDirection.Z };
            }
            else
            {
                const float DX = Query.Origin.X - Inst.TranslationA;
                const float DY = Query.Origin.Y - Inst.TranslationB;
                const float DZ = Query.Origin.Z - VertShift;
                LocalQ.Origin        = { Inst.CosineYaw * DX + Inst.SineYaw * DY, -Inst.SineYaw * DX + Inst.CosineYaw * DY, DZ };
                LocalQ.UnitDirection = { Inst.CosineYaw * Query.UnitDirection.X + Inst.SineYaw * Query.UnitDirection.Y,
                                         -Inst.SineYaw * Query.UnitDirection.X + Inst.CosineYaw * Query.UnitDirection.Y,
                                         Query.UnitDirection.Z };
            }

            const Mathematics::Axis3D LocalInv {
                SafeReciprocal(LocalQ.UnitDirection.X),
                SafeReciprocal(LocalQ.UnitDirection.Y),
                SafeReciprocal(LocalQ.UnitDirection.Z),
            };

            std::array<PendingSlot, 96u> BlasStack {};
            uint32_t BlasTop = 0u;
            BlasStack[BlasTop++] = { Inst.BlasRootEntryIndex, 0u, LocalQ.MinimumDistance };

            while (BlasTop > 0u)
            {
                const PendingSlot CurBlas = BlasStack[--BlasTop];
                if (CurBlas.EntryT >= Best.HitDistanceMeters)
                {
                    continue;
                }
                if (CurBlas.Count > 0u)
                {
                    for (uint32_t K = 0u; K < CurBlas.Count; ++K)
                    {
                        const uint32_t PIdx = CurBlas.Link + K;
                        if (PIdx >= SharedPrimitives.size()) break;
                        const auto& Prim = SharedPrimitives[PIdx];
                        if (!IncludeTransmissive && Prim.OpaqueMask == 0u) continue;
                        ++Best.TestedPrimitives;
                        float HitT = 0.0f, HitU = 0.0f, HitV = 0.0f;
                        if (IntersectTriangleMollerTrumbore(LocalQ, Prim, Best.HitDistanceMeters, HitT, HitU, HitV))
                        {
                            Best.HitDistanceMeters = HitT;
                            Best.BarycentricU      = HitU;
                            Best.BarycentricV      = HitV;
                            const Mathematics::Axis3D LocalN = Mathematics::NormalizeAxis(
                                Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02),
                                { 0.0f, 0.0f, 1.0f });
                            if (Inst.AxisConvention == 1u)
                            {
                                Best.GeometricNormal = {
                                    Inst.CosineYaw * LocalN.X - Inst.SineYaw * LocalN.Z,
                                    LocalN.Y,
                                    Inst.SineYaw * LocalN.X + Inst.CosineYaw * LocalN.Z,
                                };
                            }
                            else
                            {
                                Best.GeometricNormal = {
                                    Inst.CosineYaw * LocalN.X - Inst.SineYaw * LocalN.Y,
                                    Inst.SineYaw * LocalN.X + Inst.CosineYaw * LocalN.Y,
                                    LocalN.Z,
                                };
                            }
                            Best.SourceFaceIndex = Prim.SourceFaceIndex;
                            Best.PrimitiveIndex  = PIdx;
                            Best.MaterialOrdinal = Prim.MaterialOrdinal;
                            Best.HitConfirmed    = true;
                        }
                    }
                    continue;
                }

                if (CurBlas.Link >= SharedEntries.size()) continue;
                ++Best.VisitedWideEntries;
                const auto& CurEntry = SharedEntries[CurBlas.Link];
                uint32_t Active = 0u, Counts[4] {};
                float MinX[4], MaxX[4], MinY[4], MaxY[4], MinZ[4], MaxZ[4];
                UnpackSubtreeBounds(CurEntry, Active, MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Counts);

                std::array<PendingSlot, 4u> Hits {};
                uint32_t HitCount = 0u;
                for (uint32_t S = 0u; S < Active && S < 4u; ++S)
                {
                    float EntryT = 0.0f;
                    if (CurEntry.SubtreeLinks[S] != CompressedEmptySlotLink &&
                        IntersectBoxSlab(
                            LocalQ.Origin, LocalInv, LocalQ.MinimumDistance, Best.HitDistanceMeters,
                            MinX[S], MaxX[S], MinY[S], MaxY[S], MinZ[S], MaxZ[S], EntryT))
                    {
                        Hits[HitCount++] = { CurEntry.SubtreeLinks[S], Counts[S], EntryT };
                    }
                }
                for (uint32_t I = 1u; I < HitCount; ++I)
                {
                    const PendingSlot Key = Hits[I];
                    uint32_t J = I;
                    while (J > 0u && Hits[J - 1u].EntryT < Key.EntryT)
                    {
                        Hits[J] = Hits[J - 1u];
                        --J;
                    }
                    Hits[J] = Key;
                }
                for (uint32_t H = 0u; H < HitCount && BlasTop < BlasStack.size(); ++H)
                {
                    BlasStack[BlasTop++] = Hits[H];
                }
            }
            continue;
        }

        if (CurTlas.Link >= SharedEntries.size()) continue;
        ++Best.VisitedWideEntries;
        const auto& CurEntry = SharedEntries[CurTlas.Link];
        uint32_t Active = 0u, Counts[4] {};
        float MinX[4], MaxX[4], MinY[4], MaxY[4], MinZ[4], MaxZ[4];
        UnpackSubtreeBounds(CurEntry, Active, MinX, MaxX, MinY, MaxY, MinZ, MaxZ, Counts);

        std::array<PendingSlot, 4u> Hits {};
        uint32_t HitCount = 0u;
        for (uint32_t S = 0u; S < Active && S < 4u; ++S)
        {
            float EntryT = 0.0f;
            if (CurEntry.SubtreeLinks[S] != CompressedEmptySlotLink &&
                IntersectBoxSlab(
                    Query.Origin, WorldInvDir, Query.MinimumDistance, Best.HitDistanceMeters,
                    MinX[S], MaxX[S], MinY[S], MaxY[S], MinZ[S], MaxZ[S], EntryT))
            {
                Hits[HitCount++] = { CurEntry.SubtreeLinks[S], Counts[S], EntryT };
            }
        }
        for (uint32_t I = 1u; I < HitCount; ++I)
        {
            const PendingSlot Key = Hits[I];
            uint32_t J = I;
            while (J > 0u && Hits[J - 1u].EntryT < Key.EntryT)
            {
                Hits[J] = Hits[J - 1u];
                --J;
            }
            Hits[J] = Key;
        }
        for (uint32_t H = 0u; H < HitCount && TlasTop < TlasStack.size(); ++H)
        {
            TlasStack[TlasTop++] = Hits[H];
        }
    }

    return Best;
}

bool CompressedInstanceForest::QueryRayOccluded(const RaySegmentQuery& Query) const noexcept
{
    const auto Hit = TraceClosestRay(Query, false);
    return Hit.HitConfirmed && Hit.HitDistanceMeters < Query.MaximumDistance;
}

} // namespace Frontier::SpatialAcceleration
