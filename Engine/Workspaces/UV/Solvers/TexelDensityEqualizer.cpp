/*============================================================================================================================================
                                                      TEXELDENSITYEQUALIZER.CPP
============================================================================================================================================*/
// 🧩 Implementation of physical texel density equalization across seam-bounded PolygonNetwork UV islands

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/Solvers/TexelDensityEqualizer.h"

#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces
{

TexelDensityReport TexelDensityEqualizer::EqualizePhysicalTexelDensity(
    GeometrySpace::PolygonNetwork& Network,
    float                          TargetTexelsPerMeter,
    uint32_t                       ReferenceTilePixels)
{
    TexelDensityReport Report {};
    const std::vector<UVIslandPartition> Islands = SeamCutGenerator::PartitionConnectedUVIslands(Network);
    if (Islands.empty())
    {
        return Report;
    }

    const float SafeRes       = static_cast<float>(std::max(256u, ReferenceTilePixels));
    const float DesiredUVPerM = std::max(16.0f, TargetTexelsPerMeter) / SafeRes;
    float       DensityAccum  = 0.0f;

    for (const auto& Isl : Islands)
    {
        if (Isl.TotalArea3D <= 1.0e-6f || Isl.TotalAreaUV <= 1.0e-8f)
        {
            continue;
        }

        const float CurrentUVPerM = std::sqrt(Isl.TotalAreaUV / Isl.TotalArea3D);
        float       ScaleRatio    = DesiredUVPerM / CurrentUVPerM;

        const float TileFloorU = std::floor(0.5f * (Isl.BoundsMinUV.U + Isl.BoundsMaxUV.U));
        const float TileFloorV = std::floor(0.5f * (Isl.BoundsMinUV.V + Isl.BoundsMaxUV.V));
        const float CentroidU  = 0.5f * (Isl.BoundsMinUV.U + Isl.BoundsMaxUV.U);
        const float CentroidV  = 0.5f * (Isl.BoundsMinUV.V + Isl.BoundsMaxUV.V);
        const float SpanU      = std::max(1.0e-4f, Isl.BoundsMaxUV.U - Isl.BoundsMinUV.U);
        const float SpanV      = std::max(1.0e-4f, Isl.BoundsMaxUV.V - Isl.BoundsMinUV.V);

        // Clamp scale so a single oversized island never spills outside its 0.94 UDIM safe frame
        const float MaxFitScale = std::min(0.92f / SpanU, 0.92f / SpanV);
        ScaleRatio              = std::min(ScaleRatio, MaxFitScale);

        const float NewSpanU = SpanU * ScaleRatio;
        const float NewSpanV = SpanV * ScaleRatio;
        const float ClampedCenterU = std::clamp(CentroidU, TileFloorU + 0.03f + 0.5f * NewSpanU, TileFloorU + 0.97f - 0.5f * NewSpanU);
        const float ClampedCenterV = std::clamp(CentroidV, TileFloorV + 0.03f + 0.5f * NewSpanV, TileFloorV + 0.97f - 0.5f * NewSpanV);

        for (const uint32_t EdgeIdx : Isl.CornerEdgeIndices)
        {
            auto& UV = Network.Edges[EdgeIdx].ParameterUV;
            UV.U = ClampedCenterU + (UV.U - CentroidU) * ScaleRatio;
            UV.V = ClampedCenterV + (UV.V - CentroidV) * ScaleRatio;
        }

        DensityAccum += (CurrentUVPerM * ScaleRatio) * SafeRes;
        ++Report.IslandsEqualized;
    }

    if (Report.IslandsEqualized > 0u)
    {
        Report.AchievedMeanTexelPerM = DensityAccum / static_cast<float>(Report.IslandsEqualized);
    }
    ++Network.UVRevision;
    return Report;
}

} // namespace Frontier::Workspaces
