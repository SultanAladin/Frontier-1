/*============================================================================================================================================
                                                         UVSURFACE.CPP
============================================================================================================================================*/
// 🧩 Native UV surface invariants, packed material assignment, selection transforms and seam-chart extraction

#include "Engine/UVSpace/UVSurface.h"

#include <algorithm>
#include <cmath>

namespace Frontier::UVSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   SURFACE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool UVSurface::Validate() const noexcept
{
    const auto FiniteCoordinate = [](UVCoordinate Coordinate)
    {
        return std::isfinite(Coordinate.U) && std::isfinite(Coordinate.V);
    };
    if (!std::all_of(Coordinates.begin(), Coordinates.end(), FiniteCoordinate))
    {
        return false;
    }

    uint32_t PackedLayerCount = 0u;
    for (const SurfaceFace& Face : Faces)
    {
        if (Face.Corners.size() < 3u || Face.MaterialFirst != PackedLayerCount)
        {
            return false;
        }
        if (Face.MaterialCount > MaterialLayers.size() - std::min(Face.MaterialFirst, static_cast<uint32_t>(MaterialLayers.size())))
        {
            return false;
        }
        for (const SurfaceCorner& Corner : Face.Corners)
        {
            if (Corner.PositionIndex >= Positions.size() || Corner.UVIndex >= Coordinates.size())
            {
                return false;
            }
        }
        PackedLayerCount += Face.MaterialCount;
    }
    return PackedLayerCount == MaterialLayers.size();
}

bool UVSurface::AssignFaceLayers(uint32_t FaceIndex, std::span<const GeometricRaster::MatcapLayer> Layers)
{
    if (FaceIndex >= Faces.size())
    {
        return false;
    }
    SurfaceFace& Face = Faces[FaceIndex];
    // 📝 Copy first so a caller can safely reuse a span into the existing packed layer array.
    const std::vector<GeometricRaster::MatcapLayer> ReplacementLayers(Layers.begin(), Layers.end());
    const auto First = MaterialLayers.begin() + Face.MaterialFirst;
    const auto Last = First + Face.MaterialCount;
    const int64_t LayerDelta = static_cast<int64_t>(ReplacementLayers.size()) - static_cast<int64_t>(Face.MaterialCount);
    MaterialLayers.erase(First, Last);
    MaterialLayers.insert(MaterialLayers.begin() + Face.MaterialFirst, ReplacementLayers.begin(), ReplacementLayers.end());
    Face.MaterialCount = static_cast<uint32_t>(ReplacementLayers.size());

    for (uint32_t FollowingFace = FaceIndex + 1u; FollowingFace < Faces.size(); ++FollowingFace)
    {
        Faces[FollowingFace].MaterialFirst = static_cast<uint32_t>(
            static_cast<int64_t>(Faces[FollowingFace].MaterialFirst) + LayerDelta);
    }
    return Validate();
}

bool UVSurface::TransformUVSelection(
    std::span<const uint32_t> UVIndices,
    UVCoordinate              Pivot,
    UVCoordinate              Translation,
    UVCoordinate              Scale,
    float                     RotationRadians) noexcept
{
    if (!std::isfinite(RotationRadians) || !std::isfinite(Scale.U) || !std::isfinite(Scale.V) ||
        !std::isfinite(Pivot.U) || !std::isfinite(Pivot.V) || !std::isfinite(Translation.U) || !std::isfinite(Translation.V))
    {
        return false;
    }
    const auto InvalidIndex = [this](uint32_t CoordinateIndex) { return CoordinateIndex >= Coordinates.size(); };
    if (std::any_of(UVIndices.begin(), UVIndices.end(), InvalidIndex))
    {
        return false;
    }
    const float Cosine = std::cos(RotationRadians);
    const float Sine = std::sin(RotationRadians);
    for (std::size_t SelectionIndex = 0u; SelectionIndex < UVIndices.size(); ++SelectionIndex)
    {
        const uint32_t CoordinateIndex = UVIndices[SelectionIndex];
        bool PreviouslyTransformed = false;
        for (std::size_t Earlier = 0u; Earlier < SelectionIndex; ++Earlier)
        {
            PreviouslyTransformed |= UVIndices[Earlier] == CoordinateIndex;
        }
        if (PreviouslyTransformed)
        {
            continue;
        }

        UVCoordinate& Coordinate = Coordinates[CoordinateIndex];
        const float ScaledU = (Coordinate.U - Pivot.U) * Scale.U;
        const float ScaledV = (Coordinate.V - Pivot.V) * Scale.V;
        Coordinate.U = Pivot.U + ScaledU * Cosine - ScaledV * Sine + Translation.U;
        Coordinate.V = Pivot.V + ScaledU * Sine + ScaledV * Cosine + Translation.V;
    }
    return true;
}

bool UVSurface::ConfigureSeam(SurfaceEdge Edge, bool SeamEnabled)
{
    Edge = ConstructSurfaceEdge(Edge.PositionA, Edge.PositionB);
    const auto Seam = std::lower_bound(Seams.begin(), Seams.end(), Edge);
    const bool Present = Seam != Seams.end() && *Seam == Edge;
    if (SeamEnabled && !Present)
    {
        Seams.insert(Seam, Edge);
    }
    else if (!SeamEnabled && Present)
    {
        Seams.erase(Seam);
    }
    return Present != SeamEnabled;
}

bool UVSurface::QuerySeam(SurfaceEdge Edge) const noexcept
{
    Edge = ConstructSurfaceEdge(Edge.PositionA, Edge.PositionB);
    return std::binary_search(Seams.begin(), Seams.end(), Edge);
}

std::vector<uint32_t> UVSurface::ExtractFaceChart(uint32_t SeedFaceIndex) const
{
    std::vector<uint32_t> Chart{};
    if (SeedFaceIndex >= Faces.size())
    {
        return Chart;
    }
    std::vector<bool> Visited(Faces.size(), false);
    std::vector<uint32_t> Pending{SeedFaceIndex};
    Visited[SeedFaceIndex] = true;

    while (!Pending.empty())
    {
        const uint32_t FaceIndex = Pending.back();
        Pending.pop_back();
        Chart.push_back(FaceIndex);
        const SurfaceFace& Face = Faces[FaceIndex];
        for (uint32_t CornerIndex = 0u; CornerIndex < Face.Corners.size(); ++CornerIndex)
        {
            const SurfaceCorner& CornerA = Face.Corners[CornerIndex];
            const SurfaceCorner& CornerB = Face.Corners[(CornerIndex + 1u) % Face.Corners.size()];
            const SurfaceEdge Edge = ConstructSurfaceEdge(CornerA.PositionIndex, CornerB.PositionIndex);
            if (QuerySeam(Edge))
            {
                continue;
            }
            for (uint32_t CandidateIndex = 0u; CandidateIndex < Faces.size(); ++CandidateIndex)
            {
                if (Visited[CandidateIndex])
                {
                    continue;
                }
                const SurfaceFace& Candidate = Faces[CandidateIndex];
                for (uint32_t CandidateCorner = 0u; CandidateCorner < Candidate.Corners.size(); ++CandidateCorner)
                {
                    const SurfaceCorner& CandidateA = Candidate.Corners[CandidateCorner];
                    const SurfaceCorner& CandidateB = Candidate.Corners[(CandidateCorner + 1u) % Candidate.Corners.size()];
                    if (ConstructSurfaceEdge(CandidateA.PositionIndex, CandidateB.PositionIndex) == Edge)
                    {
                        Visited[CandidateIndex] = true;
                        Pending.push_back(CandidateIndex);
                        break;
                    }
                }
            }
        }
    }
    std::sort(Chart.begin(), Chart.end());
    return Chart;
}

} // namespace Frontier::UVSpace
