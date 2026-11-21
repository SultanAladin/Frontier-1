/*============================================================================================================================================
                                                    STOKESCONTOURIRRADIANCESEQUENCE.H
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 1: Closed-Form Stokes Contour Emissive Irradiance + Stratified Multi-Bounce Virtual Diffuse Patches

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster::GlobalIllumination
{

struct EmissiveTriangleEmitter
{
    Mathematics::Axis3D    CornerA       {};
    Mathematics::Axis3D    CornerB       {};
    Mathematics::Axis3D    CornerC       {};
    Mathematics::Axis3D    Centroid      {};
    Mathematics::Axis3D    OutwardNormal {};
    Mathematics::LinearRGB Radiance      {}; // [cd/m^2]
    float                  SurfaceArea   = 0.0f;
    uint32_t               ClusterId     = 0u;
};

struct EmissiveLuminaireCluster
{
    Mathematics::Axis3D    RepresentativePos    {};
    Mathematics::Axis3D    SamplePosA           {};
    Mathematics::Axis3D    SamplePosB           {};
    Mathematics::Axis3D    SamplePosC           {};
    Mathematics::Axis3D    RepresentativeNormal {};
    float                  BoundingRadius       = 0.35f;
    float                  TotalArea            = 0.0f;
};

struct DiffuseBouncePatch
{
    Mathematics::Axis3D    Position    {};
    Mathematics::Axis3D    Normal      {};
    Mathematics::LinearRGB Albedo      {};
    Mathematics::LinearRGB ExitantFlux {}; // [W/m^2] Lambertian exitance B = Albedo * E_in
    float                  Area        = 0.0f;
};

struct StokesContourTierSettings
{
    bool                    HighEndZeroSdfTier = false; // false = GtxLowEnd (hybrid SDF+contact); true = HighEnd (0-SDF BVH4)
    uint32_t                TargetPatchBudget  = 112u;
    Mathematics::Axis3D     SunDirection       { 0.38f, -0.46f, 0.80f };
    Mathematics::LinearRGB  SunRadiance        { 0.0f, 0.0f, 0.0f };
    std::span<const PolygonalAreaQuadLight> AreaLights {};
};

class StokesContourIrradianceSequence final
{
public:
    void BuildFromScene(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
        std::span<const Mathematics::LinearRGB>                    MaterialDiffuseAlbedos,
        std::span<const Mathematics::LinearRGB>                    MaterialEmissiveRadiance,
        const DirectLighting::HybridOccluderEnvironment&           Occluders,
        const StokesContourTierSettings&                           Settings);

    [[nodiscard]] Mathematics::LinearRGB EvaluateDirectEmissiveIrradiance(
        Mathematics::Axis3D                              ShadingPos,
        Mathematics::Axis3D                              ShadingNormal,
        const DirectLighting::HybridOccluderEnvironment& Occluders,
        const StokesContourTierSettings&                 Settings) const noexcept;

    [[nodiscard]] Mathematics::LinearRGB EvaluateIndirectBounceIrradiance(
        Mathematics::Axis3D                              ShadingPos,
        Mathematics::Axis3D                              ShadingNormal,
        const DirectLighting::HybridOccluderEnvironment& Occluders,
        const StokesContourTierSettings&                 Settings) const noexcept;

    [[nodiscard]] Mathematics::LinearRGB EvaluateTotalIrradiance(
        Mathematics::Axis3D                              ShadingPos,
        Mathematics::Axis3D                              ShadingNormal,
        const DirectLighting::HybridOccluderEnvironment& Occluders,
        const StokesContourTierSettings&                 Settings) const noexcept;

    [[nodiscard]] const std::vector<EmissiveTriangleEmitter>&  InspectEmissiveTriangles() const noexcept { return EmissiveTriangles; }
    [[nodiscard]] const std::vector<EmissiveLuminaireCluster>& InspectClusters() const noexcept          { return Clusters; }
    [[nodiscard]] const std::vector<DiffuseBouncePatch>&       InspectBouncePatches() const noexcept     { return BouncePatches; }

private:
    std::vector<EmissiveTriangleEmitter>  EmissiveTriangles {};
    std::vector<EmissiveLuminaireCluster> Clusters          {};
    std::vector<DiffuseBouncePatch>       BouncePatches     {};
};

// Exact Lambert (1760) / Stokes boundary vector irradiance integral for a clipped convex triangle
[[nodiscard]] float EvaluateClippedStokesTriangleFormFactor(
    Mathematics::Axis3D ShadingPos,
    Mathematics::Axis3D ShadingNormal,
    Mathematics::Axis3D CornerA,
    Mathematics::Axis3D CornerB,
    Mathematics::Axis3D CornerC,
    Mathematics::Axis3D EmitterNormal) noexcept;

} // namespace Frontier::GeometricRaster::GlobalIllumination
