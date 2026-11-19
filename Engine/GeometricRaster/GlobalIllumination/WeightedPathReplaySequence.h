/*============================================================================================================================================
                                                       RESERVOIRPATHREPLAYSEQUENCE.H
============================================================================================================================================*/
// 🧩 Hybrid ReSTIR Path Replay GI: Candidate RIS, Temporal/Spatial Reservoir Reuse, DynamicProbe 2nd-Bounce & 5-Pass Joint A-Trous SVGF

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/DynamicIrradianceCacheSequence.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ReservoirSampleState
{
    Mathematics::Axis3D    SecondaryHitPos  {};     // [m]    - Secondary surface hit position (or sky direction * 1e4 on miss)
    float                  ContributionW    = 0.0f; // [-]    - Unbiased final RIS contribution weight W
    Mathematics::LinearRGB Radiance         {};     // [W/sr] - Secondary exitant radiance L_o(x2 -> x1)
    uint32_t               SampleCountM     = 0u;   // [cnt]  - Clamped reservoir sample history count M
    float                  WeightSum        = 0.0f; // [-]    - Running RIS weight sum W_sum
    float                  TargetPdf        = 0.0f; // [-]    - Cached target density p_hat at current receiver
    uint16_t               ValidFlag        = 0u;   // [bit]  - 1 when reservoir carries a valid indirect path sample
    uint16_t               SkyMissFlag      = 0u;   // [bit]  - 1 when secondary ray missed geometry into sky environment
};

struct WeightedPathReplaySettings
{
    uint32_t InitialCandidates      = 4u;   // [cnt] - Cosine-hemisphere RIS candidates per primary pixel
    uint32_t SpatialNeighborCount   = 4u;   // [cnt] - Spatial reuse neighbor taps per pixel
    uint32_t TemporalHistoryCap     = 20u;  // [cnt] - Maximum temporal sample history count M
    uint32_t SpatialHistoryCap      = 24u;  // [cnt] - Maximum combined spatial sample history count M
    uint32_t SvfgFilterPasses       = 5u;   // [cnt] - Joint A-Trous SVGF strides (1, 2, 4, 8, 16)
    bool     EnableTemporalReuse    = true; // [-]   - Enable previous-frame reprojected reservoir reuse
    bool     EnableSpatialReuse     = true; // [-]   - Enable visibility-validated spatial reservoir reuse
    bool     EnableSvfgFilter       = true; // [-]   - Enable edge-stopping A-Trous SVGF spatial denoising
};

struct WeightedIndirectOutput
{
    std::vector<Mathematics::LinearRGB> FilteredIndirectOverPi {}; // [W/sr/m2] - Denoised demodulated E(N)/pi indirect GI
    std::vector<Mathematics::LinearRGB> RawIndirectOverPi      {}; // [W/sr/m2] - Pre-filter spatially-reused E(N)/pi indirect GI
    std::vector<ReservoirSampleState>   ResolvedReservoirs     {}; // [-]       - Resolved reservoir state (input for next frame)
};

//------------------------------------------------------------------------------------------------------------------------
//                                           RESERVOIR PATH REPLAY SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class WeightedPathReplaySequence
{
public:
    // Executes Candidate RIS + 1-Bounce BVH Replay + Temporal/Spatial Reservoir Reuse + 5-Pass Joint A-Trous SVGF
    [[nodiscard]] static WeightedIndirectOutput EvaluateWeightedIndirectOutput(
        const PerspectiveCameraBasis&                   CurrentCamera,
        const PerspectiveCameraBasis&                   PreviousCamera,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const DynamicIrradianceCacheSequence&                IrradianceCache,
        const SolarDirectionalSource&                  Sun,
        std::span<const PolygonalAreaQuadLight>         AreaLights,
        Mathematics::LinearRGB                          SkyRadiance,
        const WeightedPathReplaySettings&              Settings,
        uint32_t                                        SequenceIndex,
        std::span<const ReservoirSampleState>           PreviousReservoirs = {});
};

} // namespace Frontier::GeometricRaster
