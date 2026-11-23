/*============================================================================================================================================
                                                          FACEPARTITIONREGISTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of FacePartitionRegister single-pass O(F) reference indexing and zero-re-extraction layer selection queries

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  LAYER LIFECYCLE & ASSIGNMENT
//------------------------------------------------------------------------------------------------------------------------

uint16_t FacePartitionRegister::ConstructLayer(
    std::string_view      Title,
    Mathematics::Scalar3D LinearTintRGB,
    float                 TargetSharePercent,
    uint32_t              TargetUDIMNumber)
{
    const uint16_t AssignedOrdinal = NextLayerOrdinal++;
    PriorityAllocationLayer Entry {};
    Entry.LayerOrdinal       = AssignedOrdinal;
    Entry.LayerTitle         = Title.empty() ? ("Layer " + std::to_string(AssignedOrdinal)) : std::string(Title);
    Entry.LinearTintRGB      = LinearTintRGB;
    Entry.TargetSharePercent = std::clamp(TargetSharePercent, 0.0f, 100.0f);
    Entry.TargetUDIMNumber   = TargetUDIMNumber;
    Layers.push_back(std::move(Entry));

    SynchronizedRevision = 0ull;
    return AssignedOrdinal;
}

bool FacePartitionRegister::RemoveLayer(PolygonNetwork& Network, uint16_t LayerOrdinal)
{
    const auto Found = std::find_if(
        Layers.begin(),
        Layers.end(),
        [LayerOrdinal](const PriorityAllocationLayer& Candidate) noexcept
        {
            return Candidate.LayerOrdinal == LayerOrdinal;
        });

    if (Found == Layers.end())
    {
        return false;
    }

    for (Face& CurrentFace : Network.Faces)
    {
        if (CurrentFace.PriorityLayerOrdinal == LayerOrdinal)
        {
            CurrentFace.PriorityLayerOrdinal = 0u;
        }
    }

    Layers.erase(Found);
    ++Network.UVRevision;
    SynchronizedRevision = 0ull;
    return true;
}

void FacePartitionRegister::AssignFacesToLayer(
    PolygonNetwork&           Network,
    std::span<const uint32_t> TargetFaceIndices,
    uint16_t                  LayerOrdinal) noexcept
{
    bool AnyChanged = false;
    for (const uint32_t FaceIndex : TargetFaceIndices)
    {
        if (FaceIndex < Network.Faces.size() && Network.Faces[FaceIndex].PriorityLayerOrdinal != LayerOrdinal)
        {
            Network.Faces[FaceIndex].PriorityLayerOrdinal = LayerOrdinal;
            AnyChanged = true;
        }
    }
    if (AnyChanged)
    {
        ++Network.UVRevision;
        SynchronizedRevision = 0ull;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SINGLE-PASS REFERENCE INDEXING
//------------------------------------------------------------------------------------------------------------------------

void FacePartitionRegister::SynchronizePartitions(
    const PolygonNetwork& Network,
    GeometryInstanceToken OwnerInstance)
{
    if (SynchronizedRevision == Network.UVRevision && !IndexedPartitions.empty())
    {
        return;
    }

    const std::size_t SlotCapacity = static_cast<std::size_t>(NextLayerOrdinal) + 1u;
    IndexedPartitions.assign(SlotCapacity, FacePartitionSpan{});

    for (std::size_t Ord = 0u; Ord < SlotCapacity; ++Ord)
    {
        FacePartitionSpan& Ref = IndexedPartitions[Ord];
        Ref.OwnerInstance       = OwnerInstance;
        Ref.LayerOrdinal        = static_cast<uint16_t>(Ord);
        Ref.CapturedUVRevision  = Network.UVRevision;
        Ref.UVBoundsMin         = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        Ref.UVBoundsMax         = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    }

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        const Face&    CurrentFace = Network.Faces[FaceIdx];
        const uint16_t RawOrd      = CurrentFace.PriorityLayerOrdinal;
        const bool     KnownLayer  = RawOrd > 0u && RawOrd < SlotCapacity && ResolveLayer(RawOrd) != nullptr;
        const uint16_t EffectiveOrd = KnownLayer ? RawOrd : 0u;

        FacePartitionSpan& Ref = IndexedPartitions[EffectiveOrd];
        Ref.FaceIndices.push_back(FaceIdx);
        Ref.TotalArea3D += Network.EvaluateFaceArea3D(FaceIdx);
        Ref.TotalAreaUV += std::abs(Network.EvaluateFaceSignedAreaUV(FaceIdx));

        const uint32_t UDIMCode = Network.DeriveFaceUDIMNumber(FaceIdx);
        if (std::find(Ref.UDIMNumbers.begin(), Ref.UDIMNumbers.end(), UDIMCode) == Ref.UDIMNumbers.end())
        {
            Ref.UDIMNumbers.push_back(UDIMCode);
        }

        uint32_t EdgeCursor = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Corner = 0u; Corner < CurrentFace.CornerCount && EdgeCursor < Network.Edges.size(); ++Corner)
        {
            const Mathematics::UVAxes UV = Network.Edges[EdgeCursor].ParameterUV;
            Ref.UVBoundsMin.U = std::min(Ref.UVBoundsMin.U, UV.U);
            Ref.UVBoundsMin.V = std::min(Ref.UVBoundsMin.V, UV.V);
            Ref.UVBoundsMax.U = std::max(Ref.UVBoundsMax.U, UV.U);
            Ref.UVBoundsMax.V = std::max(Ref.UVBoundsMax.V, UV.V);
            EdgeCursor        = Network.Edges[EdgeCursor].NextEdgeIndex;
        }
    }

    for (FacePartitionSpan& Ref : IndexedPartitions)
    {
        if (Ref.FaceIndices.empty())
        {
            Ref.UVBoundsMin = { 0.0f, 0.0f };
            Ref.UVBoundsMax = { 0.0f, 0.0f };
        }
        std::sort(Ref.UDIMNumbers.begin(), Ref.UDIMNumbers.end());
    }

    SynchronizedRevision = Network.UVRevision;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

const FacePartitionSpan& FacePartitionRegister::InspectPartitionSpan(
    const PolygonNetwork& Network,
    uint16_t              LayerOrdinal,
    GeometryInstanceToken OwnerInstance)
{
    SynchronizePartitions(Network, OwnerInstance);
    if (LayerOrdinal < IndexedPartitions.size())
    {
        return IndexedPartitions[LayerOrdinal];
    }
    return IndexedPartitions[0u];
}

PriorityAllocationLayer* FacePartitionRegister::ResolveLayer(uint16_t LayerOrdinal) noexcept
{
    for (PriorityAllocationLayer& Entry : Layers)
    {
        if (Entry.LayerOrdinal == LayerOrdinal)
        {
            return &Entry;
        }
    }
    return nullptr;
}

const PriorityAllocationLayer* FacePartitionRegister::InspectLayer(uint16_t LayerOrdinal) const noexcept
{
    for (const PriorityAllocationLayer& Entry : Layers)
    {
        if (Entry.LayerOrdinal == LayerOrdinal)
        {
            return &Entry;
        }
    }
    return nullptr;
}

std::span<const PriorityAllocationLayer> FacePartitionRegister::InspectLayers() const noexcept
{
    return Layers;
}

void FacePartitionRegister::RestoreLayers(
    std::span<const PriorityAllocationLayer> SavedLayers,
    uint16_t                                 SavedNextOrdinal)
{
    Layers.assign(SavedLayers.begin(), SavedLayers.end());
    uint16_t HighestOrdinal = 0u;
    for (const PriorityAllocationLayer& Entry : Layers)
    {
        HighestOrdinal = std::max(HighestOrdinal, Entry.LayerOrdinal);
    }
    NextLayerOrdinal     = std::max<uint16_t>(SavedNextOrdinal, static_cast<uint16_t>(HighestOrdinal + 1u));
    SynchronizedRevision = 0ull;
}

float FacePartitionRegister::DeriveExplicitShareSum() const noexcept
{
    float Total = 0.0f;
    for (const PriorityAllocationLayer& Entry : Layers)
    {
        Total += Entry.TargetSharePercent;
    }
    return Total;
}

float FacePartitionRegister::DeriveRemainderSharePercent() const noexcept
{
    return std::max(0.0f, 100.0f - DeriveExplicitShareSum());
}

} // namespace Frontier::GeometrySpace
