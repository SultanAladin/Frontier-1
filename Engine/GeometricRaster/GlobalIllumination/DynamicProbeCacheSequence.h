/*============================================================================================================================================
                                                        DYNAMICPROBECACHESEQUENCE.H
============================================================================================================================================*/
// 🧩 GPU-Parity 8x8 Octahedral 1/pi Irradiance Probe Cache + 16x16 Chebyshev Radial Moments with Backface Validity Gating

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t ProbeIrradianceOctaWidth = 8u;  // [tx] - 8x8 octahedral 1/pi-normalized diffuse irradiance
inline constexpr uint32_t ProbeChebyshevOctaWidth  = 16u; // [tx] - 16x16 octahedral radial distance & squared-distance moments

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DynamicProbeCell
{
    Mathematics::Axis3D                                                            WorldPosition   {}; // [m]      - Probe world center
    std::array<Mathematics::LinearRGB, ProbeIrradianceOctaWidth * ProbeIrradianceOctaWidth> IrradianceOverPi {}; // [W/sr/m2]- E(n) / pi
    std::array<Mathematics::Axis2D,    ProbeChebyshevOctaWidth * ProbeChebyshevOctaWidth>   DepthMoments    {}; // [m, m^2] - (MeanR, MeanR^2)
    float                                                                          BackfaceRatio   = 0.0f; // [0..1] - Reverse-hit ratio (>0.25 attenuated)
};

struct DynamicProbeCacheConfiguration
{
    Mathematics::Axis3D GridMinimumWorld  { -3.2f, -2.8f, -0.75f }; // [m]   - Lower world corner of 3D probe grid
    Mathematics::Axis3D GridMaximumWorld  {  3.2f,  3.2f,  2.25f }; // [m]   - Upper world corner of 3D probe grid
    uint32_t            CountX            = 7u;                      // [cnt] - Probe count along X
    uint32_t            CountY            = 6u;                      // [cnt] - Probe count along Y
    uint32_t            CountZ            = 4u;                      // [cnt] - Probe count along Z
    uint32_t            FibonacciRayCount = 64u;                     // [cnt] - Spherical Fibonacci rays per probe update
    uint32_t            BouncePasses      = 2u;                      // [cnt] - Multi-bounce probe propagation passes
    float               TemporalBlend     = 0.0f;                    // [0..1]- Optional temporal hysteresis blend factor
};

//------------------------------------------------------------------------------------------------------------------------
//                                             DYNAMIC PROBE CACHE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class DynamicProbeCacheSequence
{
public:
    // Traces 64 Spherical Fibonacci rays per probe over BVH4, evaluates Direct + Area + Multi-Bounce, & encodes 8x8/16x16 octahedral atlases
    void UpdateProbeCache(
        const DynamicProbeCacheConfiguration&           Config,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
        const SolarDirectionalEmitter&                  Sun,
        std::span<const PolygonalAreaQuadLight>         AreaLights,
        Mathematics::LinearRGB                          SkyRadiance = { 0.16f, 0.22f, 0.34f });

    // Queries Chebyshev-cubed + wrapped-cosine^2 + backface-validity weighted 1/pi irradiance E(N)/pi at (WorldPosition, SurfaceNormal)
    [[nodiscard]] Mathematics::LinearRGB SampleIrradianceOverPi(
        Mathematics::Axis3D WorldPosition,
        Mathematics::Axis3D SurfaceNormal,
        Mathematics::Axis3D ViewDirection) const noexcept;

    [[nodiscard]] std::span<const DynamicProbeCell>     InspectCells()       const noexcept { return Cells; }
    [[nodiscard]] const DynamicProbeCacheConfiguration& InspectConfig()      const noexcept { return ActiveConfig; }
    [[nodiscard]] Mathematics::Axis3D                   InspectCellSpacing() const noexcept { return CellSpacing; }

    void AssignExternalCells(
        const DynamicProbeCacheConfiguration& Config,
        std::vector<DynamicProbeCell>&&       ExternalCells) noexcept;

private:
    DynamicProbeCacheConfiguration ActiveConfig {};
    Mathematics::Axis3D            CellSpacing  { 1.0f, 1.0f, 1.0f };
    std::vector<DynamicProbeCell>  Cells        {};
};

} // namespace Frontier::GeometricRaster
