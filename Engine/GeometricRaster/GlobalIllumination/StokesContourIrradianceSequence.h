/*============================================================================================================================================
                                                     STOKESCONTOURIRRADIANCESEQUENCE.H
============================================================================================================================================*/
// 🧩 Order 2 GI Candidate 1: Exact Sutherland-Hodgman spherical-polygon Stokes contour integral + 2-bounce diffuse patch radiosity

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster::GlobalIllumination
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EmissiveTriangleRadiator
{
    Mathematics::Axis3D    Vertex0         {};
    Mathematics::Axis3D    Vertex1         {};
    Mathematics::Axis3D    Vertex2         {};
    Mathematics::Axis3D    GeometricNormal { 0.0f, 0.0f, -1.0f };
    Mathematics::Axis3D    Centroid        {};
    float                  SurfaceArea     = 0.0f;
    Mathematics::LinearRGB RadianceRgb     {}; // [W/(m^2*sr)]
    uint32_t               ClusterId       = 0u;
    bool                   TwoSidedRadiator = false;
};

struct EmissiveLuminaireCluster
{
    Mathematics::Axis3D    RepresentativePos    {};
    Mathematics::Axis3D    RepresentativeNormal { 0.0f, 0.0f, -1.0f };
    float                  BoundingRadius       = 0.25f;
    float                  TotalArea            = 0.0f;
    Mathematics::LinearRGB MeanRadianceRgb      {};
    bool                   TwoSidedCluster      = false;
};

struct DiffuseBouncePatch
{
    Mathematics::Axis3D    Position   {};
    Mathematics::Axis3D    Normal     { 0.0f, 0.0f, 1.0f };
    Mathematics::LinearRGB AlbedoRgb  {};
    float                  AreaMeters = 0.25f;
    float                  Radius     = 0.28f;
    Mathematics::LinearRGB Radiance1  {}; // 1st-bounce diffuse exitant radiance L = (Albedo/Pi) * E_direct
    Mathematics::LinearRGB Radiance2  {}; // 1st + 2nd bounce relaxed diffuse exitant radiance
};

struct StokesContourTierSettings
{
    bool                   HighEndZeroSdfTier  = false;                    // false = GtxLowEnd (<=4.8 m contour + Hybrid SDF cone); true = HighEnd
    float                  ExactContourCutoffM = 4.8f;                     // [m] - GtxLowEnd switches to cluster Bunnell disk beyond 4.8 m
    uint32_t               ClusterShadowSamples = 2u;                      // [cnt] - HighEnd BVH4 luminaire rays per cluster
    Mathematics::Axis3D    SunDirection        { 0.0f, 0.0f, 1.0f };       // [-] - Unit directional solar vector for diffuse patch bounce
    Mathematics::LinearRGB SunRadiance         { 0.0f, 0.0f, 0.0f };       // [W/m^2] - Directional solar irradiance for diffuse patch bounce
};

//------------------------------------------------------------------------------------------------------------------------
//                                              STOKES CONTOUR IRRADIANCE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

// Exact closed-form Lambertian configuration factor F_{x->tri} in [0, 1] via Sutherland-Hodgman clipping + Stokes contour
[[nodiscard]] float EvaluateClippedTriangleFormFactor(
    Mathematics::Axis3D ReceiverPos,
    Mathematics::Axis3D ReceiverNormal,
    Mathematics::Axis3D TriV0,
    Mathematics::Axis3D TriV1,
    Mathematics::Axis3D TriV2,
    Mathematics::Axis3D TriNormal,
    bool                TwoSidedRadiator = false) noexcept;

class StokesContourIrradianceSequence
{
public:
    std::vector<EmissiveTriangleRadiator>  Radiators {};
    std::vector<EmissiveLuminaireCluster> Clusters {};
    std::vector<DiffuseBouncePatch>       Patches  {};

    void Reset() noexcept;

    // Extracts emissive triangles into clustered emitters and builds 2-bounce diffuse surface patches over non-emissive walls/car/rig
    void BuildFromScene(
        std::span<const SpatialAcceleration::RayTrianglePrimitive> Primitives,
        std::span<const Mathematics::LinearRGB>                    MaterialAlbedoBySlot,
        std::span<const Mathematics::LinearRGB>                    MaterialEmissiveBySlot,
        const DirectLighting::HybridOccluderEnvironment&           Occluders,
        const StokesContourTierSettings&                           Settings);

    // Evaluates 1st-bounce emissive Stokes irradiance + 2nd-bounce diffuse patch irradiance at an arbitrary shading point
    [[nodiscard]] Mathematics::LinearRGB EvaluateTotalIrradiance(
        Mathematics::Axis3D                              ReceiverPos,
        Mathematics::Axis3D                              ReceiverNormal,
        const DirectLighting::HybridOccluderEnvironment& Occluders,
        const StokesContourTierSettings&                 Settings,
        Mathematics::LinearRGB*                          OptionalOutDirectEmissiveE = nullptr,
        Mathematics::LinearRGB*                          OptionalOutBounceIndirectE = nullptr) const noexcept;
};

} // namespace Frontier::GeometricRaster::GlobalIllumination
