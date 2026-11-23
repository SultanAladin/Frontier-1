/*==========================================================================================================================================
                                               RADIANTFACETDISTRIBUTION.CPP
==========================================================================================================================================*/
// 🧩 Area/luminance-weighted triangle probabilities with positive 24-bit support.

#include "RadiantFacetDistribution.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

namespace
{

struct FacetProbabilityEntry
{
    uint32_t FacetIndex;
    double   Area;
    double   Mass;
};

double EvaluateFacetArea(const PathReplayFacet& Facet)
{
    const double EdgeAX = double(Facet.B[0]) - Facet.A[0];
    const double EdgeAY = double(Facet.B[1]) - Facet.A[1];
    const double EdgeAZ = double(Facet.B[2]) - Facet.A[2];
    const double EdgeBX = double(Facet.C[0]) - Facet.A[0];
    const double EdgeBY = double(Facet.C[1]) - Facet.A[1];
    const double EdgeBZ = double(Facet.C[2]) - Facet.A[2];
    const double CrossX  = EdgeAY * EdgeBZ - EdgeAZ * EdgeBY;
    const double CrossY  = EdgeAZ * EdgeBX - EdgeAX * EdgeBZ;
    const double CrossZ  = EdgeAX * EdgeBY - EdgeAY * EdgeBX;
    return 0.5 * std::sqrt(CrossX * CrossX + CrossY * CrossY + CrossZ * CrossZ);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<float> CompileRadiantFacetDistribution(const std::vector<PathReplayFacet>& Facets)
{
    if (Facets.size() > RadiantFacetProbabilityTicks)
        throw std::runtime_error("Facet indices exceed float32 integer precision");
    std::vector<FacetProbabilityEntry> Sources;
    double TotalMass = 0.0;
    for (size_t FacetIndex = 0; FacetIndex < Facets.size(); ++FacetIndex)
    {
        const PathReplayFacet& Facet = Facets[FacetIndex];
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!std::isfinite(Facet.A[Axis]) || !std::isfinite(Facet.B[Axis]) ||
                !std::isfinite(Facet.C[Axis]) || !std::isfinite(Facet.Emissive[Axis]) ||
                Facet.Emissive[Axis] < 0.0f)
                throw std::runtime_error("Facet position or radiance is invalid");
        }
        const double Area      = EvaluateFacetArea(Facet);
        const double Luminance = 0.2126 * Facet.Emissive[0] + 0.7152 * Facet.Emissive[1] +
                                 0.0722 * Facet.Emissive[2];
        if (Area > 0.0 && Luminance > 0.0)
        {
            if (Area > std::numeric_limits<float>::max())
                throw std::runtime_error("Radiant facet area is outside float32 range");
            const float EncodedArea = static_cast<float>(Area);
            if (EncodedArea <= 0.0f)
                throw std::runtime_error("Radiant facet area is outside float32 range");
            const double Mass = Area * Luminance;
            Sources.push_back({static_cast<uint32_t>(FacetIndex), Area, Mass});
            TotalMass += Mass;
        }
    }
    if (Sources.size() >= RadiantFacetProbabilityTicks)
        throw std::runtime_error("Radiant facet count exceeds the probability lattice");
    std::vector<float> Distribution;
    Distribution.reserve(1 + Sources.size() * 4);
    Distribution.push_back(static_cast<float>(Sources.size()));
    double CumulativeMass = 0.0;
    float PreviousProbability = 0.0f;
    for (size_t SourceIndex = 0; SourceIndex < Sources.size(); ++SourceIndex)
    {
        const FacetProbabilityEntry& Source = Sources[SourceIndex];
        CumulativeMass += Source.Mass;
        const uint32_t CumulativeTick = SourceIndex + 1 == Sources.size()
            ? RadiantFacetProbabilityTicks
            : static_cast<uint32_t>(SourceIndex + 1) +
              static_cast<uint32_t>(std::floor(CumulativeMass / TotalMass *
                                              (RadiantFacetProbabilityTicks - Sources.size())));
        const float CumulativeProbability = float(CumulativeTick) / RadiantFacetProbabilityTicks;
        if (!(CumulativeProbability > PreviousProbability))
            throw std::runtime_error("Radiant facet probability tick collapsed");
        Distribution.insert(Distribution.end(), {float(Source.FacetIndex), CumulativeProbability - PreviousProbability,
                                                 float(Source.Area), CumulativeProbability});
        PreviousProbability = CumulativeProbability;
    }
    return Distribution;
}

void ValidateRadiantFacetDistribution(const std::vector<float>& Atlas, size_t Offset, size_t FacetCount)
{
    if (Offset >= Atlas.size())
        throw std::runtime_error("Radiant facet distribution offset is outside the atlas");
    const float EncodedCount = Atlas[Offset];
    if (!std::isfinite(EncodedCount) || EncodedCount < 0 || EncodedCount >= RadiantFacetProbabilityTicks ||
        double(EncodedCount) > double(FacetCount) || std::floor(EncodedCount) != EncodedCount)
        throw std::runtime_error("Radiant facet distribution count is invalid");
    const size_t SourceCount = static_cast<size_t>(EncodedCount);
    if (SourceCount > (Atlas.size() - Offset - 1) / 4)
        throw std::runtime_error("Radiant facet distribution span is truncated");
    float PreviousProbability = 0.0f;
    for (size_t SourceIndex = 0; SourceIndex < SourceCount; ++SourceIndex)
    {
        const float* Entry = Atlas.data() + Offset + 1 + SourceIndex * 4;
        if (!std::isfinite(Entry[0]) || Entry[0] < 0 || double(Entry[0]) >= double(FacetCount) ||
            std::floor(Entry[0]) != Entry[0] || !std::isfinite(Entry[1]) || Entry[1] <= 0 ||
            !std::isfinite(Entry[2]) || Entry[2] <= 0 || !std::isfinite(Entry[3]) ||
            Entry[3] <= PreviousProbability || Entry[3] > 1 ||
            std::abs((Entry[3] - PreviousProbability) - Entry[1]) >= 1e-7f)
            throw std::runtime_error("Radiant facet distribution entry is invalid");
        PreviousProbability = Entry[3];
    }
    if (SourceCount != 0 && PreviousProbability != 1.0f)
        throw std::runtime_error("Radiant facet distribution is not normalized");
}

} // namespace Frontier
