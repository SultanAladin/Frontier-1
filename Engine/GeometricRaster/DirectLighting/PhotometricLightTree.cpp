/*============================================================================================================================================
                                                       PHOTOMETRICLIGHTTREE.CPP
============================================================================================================================================*/
// 🧩 Adaptive Conty Estevez & Kulla (2018) binary photometric (.ies) importance light tree and unified GPU light register packer

#include "Engine/GeometricRaster/DirectLighting/PhotometricLightTree.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float PiConstant           = 3.14159265358979323846f;
constexpr float CandelaToSceneFactor = 1.0f / 40.0f; // Matches PracticalLighting.js CandelaToScene
constexpr float ParkingRowPitch      = 9.7f;         // [m] - Studio X row spacing (6.22 m vehicle + 3.5 m aisle)
constexpr float ParkingBayPitch      = 3.6f;         // [m] - Studio Z bay spacing (2.5 m vehicle + 1.1 m door gap)
constexpr float LampSplitRatio       = 0.05f;        // [-] - Split cluster while radius^2 > 0.05 * distance^2

struct BoundingAxisCone
{
    std::array<float, 3> Axis   { 0.0f, -1.0f, 0.0f };
    float                Spread = 0.0f;
};

[[nodiscard]] float VecDot(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

[[nodiscard]] std::array<float, 3> VecCross(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

[[nodiscard]] float VecLength(const std::array<float, 3>& V) noexcept
{
    return std::sqrt(VecDot(V, V));
}

[[nodiscard]] std::array<float, 3> VecNormalize(const std::array<float, 3>& V) noexcept
{
    const float L = VecLength(V);
    if (L <= 1.0e-12f)
    {
        return { 0.0f, -1.0f, 0.0f };
    }
    return { V[0] / L, V[1] / L, V[2] / L };
}

[[nodiscard]] std::array<float, 3> RotateRodrigues(
    const std::array<float, 3>& V,
    const std::array<float, 3>& UnitAxis,
    const float                 Angle) noexcept
{
    const float                C = std::cos(Angle);
    const float                S = std::sin(Angle);
    const std::array<float, 3> K = VecCross(UnitAxis, V);
    const float                D = VecDot(UnitAxis, V) * (1.0f - C);
    return {
        V[0] * C + K[0] * S + UnitAxis[0] * D,
        V[1] * C + K[1] * S + UnitAxis[1] * D,
        V[2] * C + K[2] * S + UnitAxis[2] * D,
    };
}

// PBRT-v4 DirectionCone::Union — exact smallest bounding cone around two photometric axis cones
[[nodiscard]] BoundingAxisCone UniteBoundingCones(const BoundingAxisCone& A, const BoundingAxisCone& B) noexcept
{
    if (A.Spread >= PiConstant || B.Spread >= PiConstant)
    {
        return { A.Axis, PiConstant };
    }
    const float ClampedCos = std::clamp(VecDot(A.Axis, B.Axis), -1.0f, 1.0f);
    const float Between    = std::acos(ClampedCos);
    if (std::min(Between + B.Spread, PiConstant) <= A.Spread)
    {
        return A;
    }
    if (std::min(Between + A.Spread, PiConstant) <= B.Spread)
    {
        return B;
    }
    const float MergedSpread = 0.5f * (A.Spread + Between + B.Spread);
    if (MergedSpread >= PiConstant)
    {
        return { A.Axis, PiConstant };
    }
    const std::array<float, 3> Pivot    = VecCross(A.Axis, B.Axis);
    const float                PivotLen = VecLength(Pivot);
    if (PivotLen < 1.0e-9f)
    {
        return { A.Axis, PiConstant };
    }
    const std::array<float, 3> UnitPivot = { Pivot[0] / PivotLen, Pivot[1] / PivotLen, Pivot[2] / PivotLen };
    return {
        VecNormalize(RotateRodrigues(A.Axis, UnitPivot, MergedSpread - A.Spread)),
        MergedSpread,
    };
}

[[nodiscard]] std::array<float, 3> PlaceVectorInBay(
    const VehicleBayPlacement&  Bay,
    const std::array<float, 3>& Local,
    const bool                  DirectionOnly) noexcept
{
    float C = std::cos(Bay.YawRadians);
    float S = std::sin(Bay.YawRadians);
    if (std::abs(Bay.YawRadians) < 1.0e-6f)
    {
        C = 1.0f;
        S = 0.0f;
    }
    else if (std::abs(Bay.YawRadians - PiConstant) < 1.0e-5f)
    {
        C = -1.0f;
        S = 0.0f;
    }
    const float WorldX = C * Local[0] + S * Local[2];
    const float WorldZ = -S * Local[0] + C * Local[2];
    if (DirectionOnly)
    {
        return { WorldX, Local[1], WorldZ };
    }
    return { WorldX + Bay.BayX, Local[1], WorldZ + Bay.BayZ };
}

struct ClusterBuildBranch
{
    LightClusterEntry             Cluster {};
    std::unique_ptr<ClusterBuildBranch> FirstNested {};
    std::unique_ptr<ClusterBuildBranch> SecondNested {};
};

[[nodiscard]] std::unique_ptr<ClusterBuildBranch> BuildRecursiveLightCluster(
    std::span<const PracticalLampPlacement> Lamps,
    std::span<const uint32_t>               Indices)
{
    auto Branch = std::make_unique<ClusterBuildBranch>();
    Branch->Cluster.LowerBounds = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    Branch->Cluster.UpperBounds = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    BoundingAxisCone AccumulatedCone {};
    bool             FirstLamp = true;
    for (const uint32_t LampIdx : Indices)
    {
        const PracticalLampPlacement& Lamp = Lamps[LampIdx];
        for (std::size_t Axis = 0u; Axis < 3u; ++Axis)
        {
            Branch->Cluster.LowerBounds[Axis] = std::min(Branch->Cluster.LowerBounds[Axis], Lamp.Position[Axis]);
            Branch->Cluster.UpperBounds[Axis] = std::max(Branch->Cluster.UpperBounds[Axis], Lamp.Position[Axis]);
        }
        Branch->Cluster.TotalPower += Lamp.FluxPower;
        Branch->Cluster.CutoffHalfAngle = std::max(Branch->Cluster.CutoffHalfAngle, Lamp.CutoffHalfAngle);
        const BoundingAxisCone LampCone { Lamp.Axis, 0.0f };
        AccumulatedCone = FirstLamp ? LampCone : UniteBoundingCones(AccumulatedCone, LampCone);
        FirstLamp       = false;
    }
    Branch->Cluster.ConeAxis   = AccumulatedCone.Axis;
    Branch->Cluster.ConeSpread = AccumulatedCone.Spread;

    if (Indices.size() == 1u)
    {
        Branch->Cluster.LeafLampIndex = static_cast<int32_t>(Indices[0]);
        return Branch;
    }

    const std::array<float, 3> Extents {
        Branch->Cluster.UpperBounds[0] - Branch->Cluster.LowerBounds[0],
        Branch->Cluster.UpperBounds[1] - Branch->Cluster.LowerBounds[1],
        Branch->Cluster.UpperBounds[2] - Branch->Cluster.LowerBounds[2],
    };
    std::size_t SplitAxis = 0u;
    if (Extents[1] > Extents[SplitAxis])
    {
        SplitAxis = 1u;
    }
    if (Extents[2] > Extents[SplitAxis])
    {
        SplitAxis = 2u;
    }

    std::vector<uint32_t> Sorted(Indices.begin(), Indices.end());
    std::sort(Sorted.begin(), Sorted.end(), [&](const uint32_t A, const uint32_t B) {
        const float Diff = Lamps[A].Position[SplitAxis] - Lamps[B].Position[SplitAxis];
        if (Diff != 0.0f)
        {
            return Diff < 0.0f;
        }
        return A < B;
    });

    float       RunningPower  = 0.0f;
    std::size_t BestCut       = 1u;
    float       BestImbalance = std::numeric_limits<float>::infinity();
    const float TotalCountF   = static_cast<float>(Sorted.size());
    for (std::size_t I = 0u; I + 1u < Sorted.size(); ++I)
    {
        RunningPower += Lamps[Sorted[I]].FluxPower;
        const float CountTerm = std::abs(2.0f * static_cast<float>(I + 1u) - TotalCountF) * Branch->Cluster.TotalPower / TotalCountF;
        const float Imbalance = std::abs(2.0f * RunningPower - Branch->Cluster.TotalPower) + CountTerm;
        if (Imbalance < BestImbalance)
        {
            BestImbalance = Imbalance;
            BestCut       = I + 1u;
        }
    }

    const std::span<const uint32_t> LeftSpan(Sorted.data(), BestCut);
    const std::span<const uint32_t> RightSpan(Sorted.data() + BestCut, Sorted.size() - BestCut);
    Branch->FirstNested  = BuildRecursiveLightCluster(Lamps, LeftSpan);
    Branch->SecondNested = BuildRecursiveLightCluster(Lamps, RightSpan);
    return Branch;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<VehicleBayPlacement> ResolveStudioParkingBays(
    const uint32_t VehicleCount,
    const uint16_t FirstFleetPaintSlot,
    const uint16_t FirstFleetRimSlot)
{
    constexpr std::array<int32_t, 5> ParkingRows { 0, -1, 1, -2, 2 };
    constexpr uint32_t               ParkingBaysPerRow = 10u;
    constexpr uint32_t               FleetRimCount     = 4u;

    std::vector<VehicleBayPlacement> Candidates {};
    Candidates.reserve(ParkingRows.size() * ParkingBaysPerRow);
    for (const int32_t Row : ParkingRows)
    {
        for (uint32_t Bay = 0u; Bay < ParkingBaysPerRow; ++Bay)
        {
            VehicleBayPlacement Entry {};
            Entry.BayX       = static_cast<float>(Row) * ParkingRowPitch;
            Entry.BayZ       = -static_cast<float>(Bay) * ParkingBayPitch;
            Entry.YawRadians = (std::abs(Row) % 2 == 1) ? PiConstant : 0.0f;
            Candidates.push_back(Entry);
        }
    }

    std::sort(Candidates.begin(), Candidates.end(), [](const VehicleBayPlacement& Left, const VehicleBayPlacement& Right) {
        const float DistL = std::hypot(Left.BayX, Left.BayZ * 2.0f);
        const float DistR = std::hypot(Right.BayX, Right.BayZ * 2.0f);
        if (std::abs(DistL - DistR) > 1.0e-5f)
        {
            return DistL < DistR;
        }
        if (Left.BayX != Right.BayX)
        {
            return Left.BayX < Right.BayX;
        }
        return Left.BayZ < Right.BayZ;
    });

    const std::size_t ClampedCount = std::clamp<std::size_t>(VehicleCount, 1u, Candidates.size());
    Candidates.resize(ClampedCount);
    for (std::size_t Index = 0u; Index < ClampedCount; ++Index)
    {
        if (Index == 0u)
        {
            Candidates[Index].PaintSlotSwap = 0xFFFFu;
            Candidates[Index].RimSlotSwap   = 0xFFFFu;
        }
        else
        {
            Candidates[Index].PaintSlotSwap = static_cast<uint16_t>(FirstFleetPaintSlot + static_cast<uint16_t>(Index - 1u));
            Candidates[Index].RimSlotSwap   = static_cast<uint16_t>(FirstFleetRimSlot + static_cast<uint16_t>((Index * 3u) % FleetRimCount));
        }
    }
    return Candidates;
}

std::vector<PracticalLampPlacement> ConstructFleetPracticalLamps(
    const PracticalLampMode                              Mode,
    std::span<const VehicleBayPlacement>                 Bays,
    std::span<const Codecs::ResampledPhotometricProfile> Profiles,
    const float                                          IntensityScale)
{
    std::vector<PracticalLampPlacement> Lamps {};
    if (Mode == PracticalLampMode::Disabled || Profiles.size() < 3u)
    {
        return Lamps;
    }

    struct VehicleLampSpec
    {
        uint32_t             ProfileIdx;
        std::array<float, 3> LocalPosition;
        float                PitchDegrees;
        bool                 Rearward;
        std::array<float, 3> LinearColour;
    };

    constexpr std::array<VehicleLampSpec, 4> VehicleMounts {{
        { 0u, {  3.16f, 0.95f, -0.75f }, -1.5f, false, { 1.00f, 0.95f,  0.88f } },
        { 0u, {  3.16f, 0.95f,  0.75f }, -1.5f, false, { 1.00f, 0.95f,  0.88f } },
        { 1u, { -3.16f, 1.09f, -0.85f }, -3.0f, true,  { 1.00f, 0.035f, 0.012f } },
        { 1u, { -3.16f, 1.09f,  0.85f }, -3.0f, true,  { 1.00f, 0.035f, 0.012f } },
    }};

    const auto AppendLamp = [&](
                                const uint32_t             ProfileIdx,
                                const std::array<float, 3> Position,
                                const std::array<float, 3> Axis,
                                const std::array<float, 3> Reference,
                                const std::array<float, 3> Colour) {
        const auto& Profile = Profiles[ProfileIdx];
        const float Peak    = Profile.PeakCandela * CandelaToSceneFactor * IntensityScale;
        PracticalLampPlacement Placement {};
        Placement.Position      = Position;
        Placement.Axis          = Axis;
        Placement.Reference     = Reference;
        Placement.PeakIntensity = { Colour[0] * Peak, Colour[1] * Peak, Colour[2] * Peak };
        Placement.ProfileIndex  = ProfileIdx;
        const float Luminance   = 0.2126f * Placement.PeakIntensity[0] + 0.7152f * Placement.PeakIntensity[1] + 0.0722f * Placement.PeakIntensity[2];
        Placement.FluxPower     = Luminance * Profile.NormalizedFluxSr;
        Placement.CutoffHalfAngle = Profile.CutoffHalfAngleRad;
        Lamps.push_back(Placement);
    };

    for (const VehicleBayPlacement& Bay : Bays)
    {
        for (const VehicleLampSpec& Mount : VehicleMounts)
        {
            const float                PitchRad  = Mount.PitchDegrees * PiConstant / 180.0f;
            const std::array<float, 3> LocalAxis = VecNormalize({
                (Mount.Rearward ? -1.0f : 1.0f) * std::cos(PitchRad),
                std::sin(PitchRad),
                0.0f,
            });
            const std::array<float, 3> WorldDown { 0.0f, -1.0f, 0.0f };
            const float                Proj      = VecDot(WorldDown, LocalAxis);
            const std::array<float, 3> LocalRef  = VecNormalize({
                WorldDown[0] - LocalAxis[0] * Proj,
                WorldDown[1] - LocalAxis[1] * Proj,
                WorldDown[2] - LocalAxis[2] * Proj,
            });
            AppendLamp(
                Mount.ProfileIdx,
                PlaceVectorInBay(Bay, Mount.LocalPosition, false),
                VecNormalize(PlaceVectorInBay(Bay, LocalAxis, true)),
                VecNormalize(PlaceVectorInBay(Bay, LocalRef, true)),
                Mount.LinearColour);
        }
    }

    if (Mode == PracticalLampMode::CarParkLamps)
    {
        constexpr std::array<float, 4> DownlightAisles   { -14.55f, -4.85f, 4.85f, 14.55f };
        constexpr std::array<float, 7> DownlightStations { 2.0f, -4.0f, -10.0f, -16.0f, -22.0f, -28.0f, -34.0f };
        constexpr float                DownlightHeight   = 5.0f;
        constexpr std::array<float, 3> DownlightColour   { 1.00f, 0.90f, 0.78f };

        for (const float X : DownlightAisles)
        {
            for (const float Z : DownlightStations)
            {
                AppendLamp(2u, { X, DownlightHeight, Z }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, DownlightColour);
            }
        }
    }

    return Lamps;
}

std::vector<LightClusterEntry> ConstructPhotometricLightTree(
    std::span<const PracticalLampPlacement> Lamps,
    uint32_t* const                         OutMaximumDepth)
{
    if (OutMaximumDepth != nullptr)
    {
        *OutMaximumDepth = 0u;
    }
    if (Lamps.empty())
    {
        return {};
    }

    std::vector<uint32_t> LampIndices(Lamps.size());
    for (std::size_t I = 0u; I < Lamps.size(); ++I)
    {
        LampIndices[I] = static_cast<uint32_t>(I);
    }

    std::unique_ptr<ClusterBuildBranch> Root = BuildRecursiveLightCluster(Lamps, LampIndices);
    Root->Cluster.TreeLevel               = 0u;

    std::vector<ClusterBuildBranch*> Queue {};
    Queue.reserve(Lamps.size() * 2u);
    Queue.push_back(Root.get());

    uint32_t MaxDepth = 0u;
    for (std::size_t Cursor = 0u; Cursor < Queue.size(); ++Cursor)
    {
        ClusterBuildBranch* Current = Queue[Cursor];
        MaxDepth                 = std::max(MaxDepth, Current->Cluster.TreeLevel);
        if (Current->FirstNested == nullptr || Current->SecondNested == nullptr)
        {
            continue;
        }
        Current->Cluster.FirstNestedSlot     = static_cast<uint32_t>(Queue.size());
        Current->FirstNested->Cluster.TreeLevel  = Current->Cluster.TreeLevel + 1u;
        Current->SecondNested->Cluster.TreeLevel = Current->Cluster.TreeLevel + 1u;
        Queue.push_back(Current->FirstNested.get());
        Queue.push_back(Current->SecondNested.get());
    }

    if (OutMaximumDepth != nullptr)
    {
        *OutMaximumDepth = MaxDepth;
    }

    std::vector<LightClusterEntry> Flattened {};
    Flattened.reserve(Queue.size());
    for (const ClusterBuildBranch* Branch : Queue)
    {
        Flattened.push_back(Branch->Cluster);
    }
    return Flattened;
}

std::vector<float> PackUnifiedLightRegister(
    std::span<const float>                               AreaPackedFloats,
    const uint32_t                                       AreaLightCount,
    std::span<const PracticalLampPlacement>              Lamps,
    std::span<const LightClusterEntry>                   LightTree,
    std::span<const Codecs::ResampledPhotometricProfile> Profiles,
    PackedLightRegisterLayout* const                       OutLayout)
{
    const uint32_t AreaVectors    = AreaLightCount * LightRegisterVectorStride;
    const uint32_t LampSlotStart  = AreaVectors;
    const uint32_t ClusterStart   = LampSlotStart + static_cast<uint32_t>(Lamps.size()) * LightRegisterVectorStride;
    const uint32_t ProfileStart   = ClusterStart + static_cast<uint32_t>(LightTree.size()) * LightClusterVectorStride;
    const uint32_t ProfileVectors = Lamps.empty()
                                        ? 0u
                                        : static_cast<uint32_t>(Profiles.size()) * (Codecs::PhotometricGridStride / 4u);
    const std::size_t TotalFloats = std::max<std::size_t>(4u, static_cast<std::size_t>(ProfileStart + ProfileVectors) * 4u);

    std::vector<float> Packed(TotalFloats, 0.0f);
    const std::size_t  CopyAreaCount = std::min<std::size_t>(AreaPackedFloats.size(), static_cast<std::size_t>(AreaVectors) * 4u);
    if (CopyAreaCount > 0u)
    {
        std::memcpy(Packed.data(), AreaPackedFloats.data(), CopyAreaCount * sizeof(float));
    }

    for (std::size_t LampIdx = 0u; LampIdx < Lamps.size(); ++LampIdx)
    {
        const PracticalLampPlacement& Lamp = Lamps[LampIdx];
        const std::size_t             O    = (static_cast<std::size_t>(LampSlotStart) + LampIdx * LightRegisterVectorStride) * 4u;
        Packed[O + 0u]  = Lamp.Position[0];
        Packed[O + 1u]  = Lamp.Position[1];
        Packed[O + 2u]  = Lamp.Position[2];
        Packed[O + 3u]  = static_cast<float>(Lamp.ProfileIndex);
        Packed[O + 4u]  = Lamp.Axis[0];
        Packed[O + 5u]  = Lamp.Axis[1];
        Packed[O + 6u]  = Lamp.Axis[2];
        Packed[O + 7u]  = 0.0f;
        Packed[O + 8u]  = Lamp.Reference[0];
        Packed[O + 9u]  = Lamp.Reference[1];
        Packed[O + 10u] = Lamp.Reference[2];
        Packed[O + 11u] = 0.0f;
        Packed[O + 12u] = Lamp.PeakIntensity[0];
        Packed[O + 13u] = Lamp.PeakIntensity[1];
        Packed[O + 14u] = Lamp.PeakIntensity[2];
        Packed[O + 15u] = 0.0f;
    }

    uint32_t MaxDepth = 0u;
    for (std::size_t ClusterIdx = 0u; ClusterIdx < LightTree.size(); ++ClusterIdx)
    {
        const LightClusterEntry& Entry = LightTree[ClusterIdx];
        MaxDepth                       = std::max(MaxDepth, Entry.TreeLevel);
        const std::size_t O            = (static_cast<std::size_t>(ClusterStart) + ClusterIdx * LightClusterVectorStride) * 4u;
        Packed[O + 0u]                 = Entry.LowerBounds[0];
        Packed[O + 1u]                 = Entry.LowerBounds[1];
        Packed[O + 2u]                 = Entry.LowerBounds[2];
        Packed[O + 3u]                 = Entry.TotalPower;
        Packed[O + 4u]                 = Entry.UpperBounds[0];
        Packed[O + 5u]                 = Entry.UpperBounds[1];
        Packed[O + 6u]                 = Entry.UpperBounds[2];

        const uint32_t LinkWord = (Entry.LeafLampIndex >= 0)
                                      ? (LightClusterLeafBit | static_cast<uint32_t>(Entry.LeafLampIndex))
                                      : Entry.FirstNestedSlot;
        std::memcpy(&Packed[O + 7u], &LinkWord, sizeof(uint32_t));

        Packed[O + 8u]  = Entry.ConeAxis[0];
        Packed[O + 9u]  = Entry.ConeAxis[1];
        Packed[O + 10u] = Entry.ConeAxis[2];
        Packed[O + 11u] = 0.0f;
        Packed[O + 12u] = std::cos(Entry.ConeSpread);
        Packed[O + 13u] = std::sin(Entry.ConeSpread);
        Packed[O + 14u] = std::cos(Entry.CutoffHalfAngle);
        Packed[O + 15u] = Entry.CutoffHalfAngle;
    }

    if (ProfileVectors > 0u)
    {
        for (std::size_t ProfIdx = 0u; ProfIdx < Profiles.size(); ++ProfIdx)
        {
            const std::size_t O = static_cast<std::size_t>(ProfileStart) * 4u + ProfIdx * Codecs::PhotometricGridStride;
            std::memcpy(&Packed[O], Profiles[ProfIdx].NormalizedCandela.data(), Codecs::PhotometricGridStride * sizeof(float));
        }
    }

    if (OutLayout != nullptr)
    {
        OutLayout->LampCount        = static_cast<uint32_t>(Lamps.size());
        OutLayout->ClusterSlotStart = ClusterStart;
        OutLayout->LampSlotStart    = LampSlotStart;
        OutLayout->ProfileSlotStart = ProfileStart;
        OutLayout->ClusterCount     = static_cast<uint32_t>(LightTree.size());
        OutLayout->MaximumTreeDepth = MaxDepth;
    }

    return Packed;
}

uint32_t ResolveAdaptiveLampCutHost(
    std::span<const LightClusterEntry>            LightTree,
    const std::array<float, 3>&                   ShadingPosition,
    const uint32_t                                CutLimit,
    std::array<uint32_t, AdaptiveLampCutCeiling>& OutClusterCut)
{
    if (LightTree.empty())
    {
        return 0u;
    }
    const uint32_t ClampedLimit = std::clamp(CutLimit, 1u, AdaptiveLampCutCeiling);
    OutClusterCut.fill(0u);
    uint32_t Count = 1u;

    for (uint32_t Round = 1u; Round < ClampedLimit; ++Round)
    {
        uint32_t Chosen = AdaptiveLampCutCeiling;
        float    Best   = 0.0f;
        for (uint32_t Slot = 0u; Slot < Count; ++Slot)
        {
            const LightClusterEntry& Entry = LightTree[OutClusterCut[Slot]];
            if (Entry.LeafLampIndex >= 0)
            {
                continue;
            }
            const std::array<float, 3> Diag {
                Entry.UpperBounds[0] - Entry.LowerBounds[0],
                Entry.UpperBounds[1] - Entry.LowerBounds[1],
                Entry.UpperBounds[2] - Entry.LowerBounds[2],
            };
            const float Radius2 = 0.25f * VecDot(Diag, Diag);
            const std::array<float, 3> Offset {
                ShadingPosition[0] - 0.5f * (Entry.LowerBounds[0] + Entry.UpperBounds[0]),
                ShadingPosition[1] - 0.5f * (Entry.LowerBounds[1] + Entry.UpperBounds[1]),
                ShadingPosition[2] - 0.5f * (Entry.LowerBounds[2] + Entry.UpperBounds[2]),
            };
            const float Distance2 = std::max(VecDot(Offset, Offset), 1.0e-6f);
            const float Score     = Entry.TotalPower * Radius2 / (Distance2 * Distance2);
            if (Radius2 > LampSplitRatio * Distance2 && Score > Best)
            {
                Best   = Score;
                Chosen = Slot;
            }
        }
        if (Chosen == AdaptiveLampCutCeiling)
        {
            break;
        }
        const uint32_t FirstNested = LightTree[OutClusterCut[Chosen]].FirstNestedSlot;
        OutClusterCut[Chosen]    = FirstNested;
        OutClusterCut[Count]     = FirstNested + 1u;
        ++Count;
    }

    return Count;
}

} // namespace Frontier::GeometricRaster
