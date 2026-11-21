/*============================================================================================================================================
                                                    SURFELRADIOSITYLATTICESEQUENCE.H
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 2: Relocated Surfel Radiosity + Dual-Cascade Octahedral Irradiance Lattice with Dual-Tier Chebyshev Moments

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/StokesContourIrradianceSequence.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster::GlobalIllumination
{

struct RadiositySurfaceElement
{
    Mathematics::Axis3D    Position        {};
    Mathematics::Axis3D    Normal          {};
    Mathematics::LinearRGB DiffuseAlbedo   {};
    Mathematics::LinearRGB EmittedRadiance {}; // [cd/m^2]
    Mathematics::LinearRGB ExitantRadiance {}; // [cd/m^2] L_out = L_emit + (Albedo/Pi) * E_in
    float                  Area            = 0.0f;
    bool                   EmissiveSurface = false;
};

struct IrradianceCascadeGrid
{
    Mathematics::Axis3D               WorldMin           {};
    Mathematics::Axis3D               WorldMax           {};
    Mathematics::Axis3D               CellSpacing        {};
    uint32_t                          GridDimX           = 1u;
    uint32_t                          GridDimY           = 1u;
    uint32_t                          GridDimZ           = 1u;
    uint32_t                          OctaSheetRes       = 6u;  // 6x6 octahedral irradiance texels per lattice entry
    std::vector<Mathematics::LinearRGB> IrradianceSheets {};    // [EntryCount * OctaSheetRes * OctaSheetRes]
    std::vector<float>                ChebyshevMoments   {};    // [EntryCount * OctaSheetRes * OctaSheetRes * 2] (MeanD, MeanD^2)
};

struct SurfelLatticeTierSettings
{
    bool                   HighEndZeroSdfTier  = false; // false = GtxLowEnd (12x10x4 + 8x6x3 + SDF Chebyshev); true = HighEnd (19x15x6 + 9x7x4 + BVH4 Chebyshev)
    uint32_t               TargetSurfelBudget  = 160u;
    uint32_t               RadiosityBounceCount= 2u;
    Mathematics::Axis3D    SunDirection        { 0.38f, -0.46f, 0.80f };
    Mathematics::LinearRGB SunRadiance         { 0.0f, 0.0f, 0.0f };
    std::span<const PolygonalAreaQuadLight> AreaLights {};
};

class SurfelRadiosityLatticeSequence final
{
public:
    void BuildRelaxedLatticeFromScene(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
        std::span<const Mathematics::LinearRGB>                    MaterialDiffuseAlbedos,
        std::span<const Mathematics::LinearRGB>                    MaterialEmissiveRadiance,
        const DirectLighting::HybridOccluderEnvironment&           Occluders,
        const SurfelLatticeTierSettings&                           Settings);

    [[nodiscard]] Mathematics::LinearRGB EvaluateIndirectIrradiance(
        Mathematics::Axis3D ShadingPos,
        Mathematics::Axis3D ShadingNormal) const noexcept;

    [[nodiscard]] Mathematics::LinearRGB EvaluateTotalIrradiance(
        Mathematics::Axis3D                              ShadingPos,
        Mathematics::Axis3D                              ShadingNormal,
        const DirectLighting::HybridOccluderEnvironment& Occluders,
        const SurfelLatticeTierSettings&                 Settings) const noexcept;

    [[nodiscard]] const std::vector<RadiositySurfaceElement>& InspectSurfels() const noexcept      { return Surfels; }
    [[nodiscard]] const IrradianceCascadeGrid&                InspectInnerCascade() const noexcept { return InnerCascade; }
    [[nodiscard]] const IrradianceCascadeGrid&                InspectOuterCascade() const noexcept { return OuterCascade; }

private:
    std::vector<RadiositySurfaceElement> Surfels      {};
    StokesContourIrradianceSequence      NearFieldStokes {};
    IrradianceCascadeGrid                InnerCascade {};
    IrradianceCascadeGrid                OuterCascade {};
    bool                                 BuiltValid   = false;
};

[[nodiscard]] Mathematics::Axis2D EncodeEqualAreaOctahedralUv(Mathematics::Axis3D UnitDir) noexcept;
[[nodiscard]] Mathematics::Axis3D DecodeEqualAreaOctahedralUv(Mathematics::Axis2D UvZeroToOne) noexcept;

} // namespace Frontier::GeometricRaster::GlobalIllumination
