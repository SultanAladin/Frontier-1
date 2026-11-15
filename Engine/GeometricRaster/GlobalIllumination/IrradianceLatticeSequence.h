/*============================================================================================================================================
                                                         IRRADIANCELATTICESEQUENCE.H
============================================================================================================================================*/
// 🧩 Tier 2 RTX 1/pi-Normalized Octahedral Irradiance Lattice (16x16 Chebyshev Depth) + Tier 1 GTX 6-Cone SignedDistanceVolume Diffuse GI

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
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

inline constexpr uint32_t IrradianceOctaWidth  = 8u;  // [tx] - 8x8 octahedral 1/pi-normalized diffuse irradiance per cell
inline constexpr uint32_t ChebyshevDepthOctaW  = 16u; // [tx] - 16x16 octahedral radial distance & squared-distance moments

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OctahedralIrradianceCell
{
    Mathematics::Axis3D                                                  WorldPosition   {}; // [m]      - Cell world center
    std::array<Mathematics::LinearRGB, IrradianceOctaWidth * IrradianceOctaWidth> IrradianceOverPi {}; // [W/sr/m2]- E(n) / pi!
    std::array<Mathematics::Axis2D,    ChebyshevDepthOctaW * ChebyshevDepthOctaW> DepthMoments    {}; // [m, m^2] - (MeanR, MeanR^2)
};

struct IrradianceLatticeConfiguration
{
    Mathematics::Axis3D GridMinimumWorld  { -3.2f, -2.8f, -0.75f }; // [m]   - Lower world corner of 3D lattice
    Mathematics::Axis3D GridMaximumWorld  {  3.2f,  3.2f,  2.25f };       // [m]   - Upper world corner of 3D lattice
    uint32_t            CountX            = 7u;                           // [cnt] - Lattice resolution along X
    uint32_t            CountY            = 6u;                           // [cnt] - Lattice resolution along Y
    uint32_t            CountZ            = 4u;                           // [cnt] - Lattice resolution along Z
    uint32_t            FibonacciRayCount = 64u;                          // [cnt] - Spherical Fibonacci rays per cell update
    uint32_t            BouncePasses      = 2u;                           // [cnt] - Multi-bounce propagation iterations
};

//------------------------------------------------------------------------------------------------------------------------
//                                              IRRADIANCE LATTICE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class IrradianceLatticeSequence
{
public:
    // Updates the 3D OctahedralIrradianceCell lattice via 64-ray Spherical Fibonacci BVH4 raytracing + 32 B BounceSlabEntry multi-bounce!
    void PropagateRtxIrradianceLattice(
        const IrradianceLatticeConfiguration&            Config,
        const SpatialAcceleration::WideBoundingTree&     SceneTree,
        std::span<const MaterialSpace::BounceSlabEntry>  BounceSummaries,
        const SolarDirectionalEmitter&                   Sun,
        Mathematics::LinearRGB                           SkyRadiance = { 0.16f, 0.22f, 0.34f });

    // Queries Chebyshev-visibility-weighted 1/pi diffuse exitant radiance L_d = E(N) / pi at (WorldPosition, SurfaceNormal) — zero wall leak!
    [[nodiscard]] Mathematics::LinearRGB SampleChebyshevIrradianceOverPi(
        Mathematics::Axis3D WorldPosition,
        Mathematics::Axis3D SurfaceNormal,
        Mathematics::Axis3D ViewDirection) const noexcept;

    // Evaluates Tier 1 GTX 6-Cone Directional SignedDistanceVolume Diffuse GI + Surface Bounce Color Bleed without hardware RT
    [[nodiscard]] Mathematics::LinearRGB EvaluateGtxSignedDistanceConeGi(
        Mathematics::Axis3D                              WorldPosition,
        Mathematics::Axis3D                              SurfaceNormal,
        const DistanceFieldSpace::SignedDistanceVolume&  SdfVolume,
        const SpatialAcceleration::WideBoundingTree&     SceneTree,
        std::span<const MaterialSpace::BounceSlabEntry>  BounceSummaries,
        const SolarDirectionalEmitter&                   Sun,
        Mathematics::LinearRGB                           SkyRadiance = { 0.16f, 0.22f, 0.34f }) const noexcept;

    [[nodiscard]] std::span<const OctahedralIrradianceCell> InspectCells() const noexcept { return Cells; }
    [[nodiscard]] const IrradianceLatticeConfiguration&     InspectConfig() const noexcept { return ActiveConfig; }

private:
    IrradianceLatticeConfiguration        ActiveConfig {};
    Mathematics::Axis3D                   CellSpacing  { 1.0f, 1.0f, 1.0f };
    std::vector<OctahedralIrradianceCell> Cells        {};
};

} // namespace Frontier::GeometricRaster
