/*============================================================================================================================================
                                                        LINEARCOSINELIGHTINTEGRATION.H
============================================================================================================================================*/
// 🧩 Visibility-Gated Clipped-Polygon Linearly Transformed Cosine (LTC) Quad Area Lights + 2-Scale (0.25 m / 2.2 m) Chassis & Specular AO

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/PhotometricProfile.h"
#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PolygonalAreaQuadLight
{
    Mathematics::Axis3D    CenterWorld       { 0.0f, 0.0f, 2.5f };  // [m]     - Quad center in world space
    Mathematics::Axis3D    RightTangent      { 1.0f, 0.0f, 0.0f };  // [-]     - Unit horizontal edge axis
    Mathematics::Axis3D    UpBitangent       { 0.0f, 1.0f, 0.0f };  // [-]     - Unit vertical edge axis
    float                  HalfWidthMeters   = 0.85f;               // [m]     - Half-width along RightTangent
    float                  HalfHeightMeters  = 0.45f;               // [m]     - Half-height along UpBitangent
    Mathematics::LinearRGB EmittedRadiance   { 5.5f, 4.8f, 4.1f };  // [W/sr/m2]- Emitted uniform diffuse area radiance
    bool                   TwoSidedEmission  = false;               // [-]     - Emit from both front & back faces
};

struct AmbientOcclusionSettings
{
    float    ContactRadiusMeters = 0.25f; // [m] - Tight micro-crease / contact cavity scale
    float    ChassisRadiusMeters = 2.20f; // [m] - Broad underbody / architectural chassis occlusion scale
    uint32_t RaysPerPixel        = 8u;    // [cnt]- Stratified cosine-hemisphere AO rays per pixel
};

struct DirectPointLightSpec
{
    Mathematics::Axis3D    PositionWorld {};                 // [m]   - World point light position
    float                  RangeMeters   = 8.0f;             // [m]   - Smooth attenuation cutoff radius
    Mathematics::LinearRGB Color         { 1.0f, 1.0f, 1.0f }; // [-] - Linear RGB tint
    float                  Intensity     = 1.0f;             // [cd]  - Luminous intensity multiplier
    float                  RadiusMeters  = 0.10f;            // [m]   - Source sphere radius
};

struct PhotometricPointRadiator
{
    Mathematics::Axis3D    PositionWorld    { 0.0f, 0.0f, 3.0f };   // [m]    - Emitter apex in world space
    Mathematics::Axis3D    BeamAxis         { 0.0f, 0.0f, -1.0f };  // [-]    - Unit polar theta=0 emission axis
    Mathematics::Axis3D    ZeroAzimuthAxis  { 1.0f, 0.0f, 0.0f };   // [-]    - Unit orthogonal phi=0 reference axis
    Mathematics::LinearRGB PeakIntensityRgb { 1.0f, 1.0f, 1.0f };   // [W/sr] - Radiometric intensity at normalized candela = 1
    float                  RangeMeters      = 24.0f;                // [m]    - Smooth quartic distance cutoff
    float                  RadiusMeters     = 0.06f;                // [m]    - Non-singular denominator clamp
    uint32_t               ProfileOrdinal   = 0u;                   // [idx]  - Index into PhotometricProfiles array
};

struct DirectLightingOutput
{
    uint32_t                            WidthPx                = 0u; // [px]    - Viewport width
    uint32_t                            HeightPx               = 0u; // [px]    - Viewport height
    std::vector<Mathematics::LinearRGB> DirectHdrRadiance      {};   // [cd/m2] - Solar + LTC Area Lights + Chassis-occluded sky fill
    std::vector<uint8_t>                ToneMappedSrgb8        {};   // [rgba8] - ACES filmic + sRGB8 display image
    std::vector<float>                  CombinedDiffuseAo      {};   // [0-1]   - Product of 0.25 m Contact AO & 2.20 m Chassis AO
    std::vector<float>                  LagardeSpecularAo      {};   // [0-1]   - Roughness & grazing-aware specAO
    SolarShadowFieldResult              SolarShadows           {};   // [-]     - Underlying raw/filtered solar shadow & blocker field
};

//------------------------------------------------------------------------------------------------------------------------
//                                           LINEAR COSINE LIGHT INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

class LinearCosineLightIntegration
{
public:
    // Exact Heitz et al. (2016) clipped spherical-polygon clamped-cosine form factor in canonical local Z-up frame [0..1]
    [[nodiscard]] static float IntegrateClippedQuadCosineFormFactor(
        const std::array<Mathematics::Axis3D, 4u>& LocalQuadVertices) noexcept;

    // Evaluates diffuse + GGX-fitted LTC radiance from a PolygonalAreaQuadLight with 4-corner visibility gating against SceneTree
    [[nodiscard]] static Mathematics::LinearRGB EvaluateVisibilityGatedQuadLight(
        const PolygonalAreaQuadLight&                AreaLight,
        const AnalyticResolvedSurfaceSample&         HitSample,
        const MaterialSpace::SurfaceMaterial&        Mat,
        uint32_t                                     ActiveLobeMask,
        const SpatialAcceleration::WideBoundingTree& SceneTree) noexcept;

    // Lagarde & de Rousiers (2014) physically motivated specular ambient occlusion preventing glowing rims in cavities
    [[nodiscard]] static float ComputeLagardeSpecularOcclusion(
        float NdotV,
        float DiffuseAmbientOcclusion,
        float PerceptualRoughness) noexcept;

    // Evaluates full Direct Lighting Frame with explicit Shadow Lab algorithm (BVH4 & Orthographic Cascades)
    [[nodiscard]] static DirectLightingOutput EvaluateDirectLightingFrameWithAlgorithm(
        SolarShadowAlgorithm                           Algorithm,
        const PerspectiveCameraBasis&                  Camera,
        std::span<const RigidClusterInstance>          Instances,
        const VisibilityBufferTargets&                 VisibilityTargets,
        const MaterialSpace::CompactMaterialRegister&  MaterialRegister,
        const SpatialAcceleration::WideBoundingTree&   SceneTree,
        const SolarDirectionalSource&                  Sun,
        std::span<const PolygonalAreaQuadLight>        AreaLights,
        const OrthographicShadowCascade*                        OptionalFineClip    = nullptr,
        const OrthographicShadowCascade*                        OptionalCoarseClip  = nullptr,
        const AmbientOcclusionSettings&                         AoSettings          = {},
        std::span<const DirectPointLightSpec>                   PointLights               = {},
        Mathematics::LinearRGB                                  SkyFillRadiance           = { 0.14f, 0.19f, 0.28f },
        std::span<const PhotometricPointRadiator>                PhotometricRadiators       = {},
        std::span<const Codecs::ResampledPhotometricProfile>    PhotometricProfiles       = {},
        std::span<const float>                                  SolarVisibilityOverride   = {},
        bool                                                    SkipEmissiveTriangleDirect = false);

    // Compatibility wrapper mapping DirectShadowTier -> SolarShadowAlgorithm
    [[nodiscard]] static DirectLightingOutput EvaluateDirectLightingFrame(
        DirectShadowTier                              Tier,
        const PerspectiveCameraBasis&                 Camera,
        std::span<const RigidClusterInstance>         Instances,
        const VisibilityBufferTargets&                VisibilityTargets,
        const MaterialSpace::CompactMaterialRegister& MaterialRegister,
        const SpatialAcceleration::WideBoundingTree&  SceneTree,
        const SolarDirectionalSource&                 Sun,
        std::span<const PolygonalAreaQuadLight>       AreaLights,
        const OrthographicShadowCascade*              OptionalFineClip   = nullptr,
        const OrthographicShadowCascade*              OptionalCoarseClip = nullptr,
        const AmbientOcclusionSettings&               AoSettings         = {});
};

} // namespace Frontier::GeometricRaster
