/*============================================================================================================================================
                                                     SDFEMISSIVEPENUMBRASEQUENCE.H
============================================================================================================================================*/
// 🧩 Unified 6-Mode SDF Shadow, 3D Voxel SDF Clipmap Penumbra, Voxel Cone GI, Sobol Spherical-Cap, & 96-SPP SDF Path-Tracing Sequence

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/ArticulatedCapsuleRig.h"
#include "Engine/DistanceFieldSpace/LocalSignedDistanceVolume.h"
#include "Engine/GeometricRaster/DirectLighting/HybridConeShadowSequence.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster::DirectLighting
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SdfShadowPenumbraMode : uint8_t
{
    PointSampleSdf             = 0u, // 0. Point-Sample SDF (No Penumbra)
    AnalyticSdfConePenumbra    = 1u, // 1. Analytic SDF Cone Penumbra
    VoxelSdfClipmapPenumbra    = 2u, // 2. 3D Voxel SDF Clipmap Penumbra
    VoxelSdfPenumbraPlusConeGi = 3u, // 3. 3D Voxel SDF Penumbra + Cone GI
    HybridSdfSobolSphericalCap = 4u, // 4. Hybrid SDF + Sobol Spherical Cap
    SdfMonteCarloPtRef96Spp    = 5u, // 5. SDF Monte Carlo PT Ref (96 SPP)
    Count                      = 6u,
};

[[nodiscard]] const char* QuerySdfShadowPenumbraModeLabel(SdfShadowPenumbraMode Mode) noexcept;
[[nodiscard]] const char* QuerySdfShadowPenumbraShortLabel(SdfShadowPenumbraMode Mode) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                            ANALYTIC & COMPOSITE SDF PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

struct SdfSurfaceMaterialSpec
{
    Mathematics::LinearRGB BaseColor     { 0.72f, 0.72f, 0.72f };
    float                  Roughness     = 0.48f;
    float                  Metalness     = 0.0f;
    Mathematics::LinearRGB Emission      { 0.0f, 0.0f, 0.0f };
    bool                   IsEmissive    = false;
    bool                   CheckerFloor  = false;
    float                  CheckerScaleM = 0.60f;
};

struct SdfSpherePrimitive
{
    Mathematics::Axis3D Center       { 0.0f, 0.0f, 0.5f };
    float               Radius       = 0.5f;
    uint32_t            MaterialSlot = 0u;
    bool                CastsShadow  = true;
};

struct SdfCappedCylinderPrimitive
{
    Mathematics::Axis3D BaseCenter   { 0.0f, 0.0f, 0.0f }; // Bottom cap center (axis aligned along +Z)
    float               HeightZ      = 1.5f;
    float               Radius       = 0.16f;
    uint32_t            MaterialSlot = 0u;
    bool                CastsShadow  = true;
};

struct SdfTorusPrimitive
{
    Mathematics::Axis3D Center       { 0.0f, 0.0f, 0.9f };
    float               MajorRadius  = 0.30f;
    float               MinorRadius  = 0.11f;
    float               TiltRadiansX = 0.0f;
    uint32_t            MaterialSlot = 0u;
    bool                CastsShadow  = true;
};

struct SdfBoxPrimitive
{
    Mathematics::Axis3D Center       { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D HalfExtent   { 0.5f, 0.5f, 0.5f };
    uint32_t            MaterialSlot = 0u;
    bool                CastsShadow  = true; // False for outer Cornell room slabs so receivers never self-occlude
};

struct EmissiveSphereLuminaire
{
    Mathematics::Axis3D    Center        { -1.38f, 0.45f, 1.02f };
    float                  Radius        = 0.40f;
    Mathematics::LinearRGB Radiance      { 18.5f, 16.4f, 13.4f }; // [W/(m^2*sr)] pure emissive sphere exitance
    uint32_t               MaterialSlot  = 0u;
};

struct SdfShowcaseScene
{
    std::vector<SdfSurfaceMaterialSpec>     Materials        {};
    std::vector<SdfBoxPrimitive>            Boxes            {};
    std::vector<SdfSpherePrimitive>         Spheres          {};
    std::vector<SdfCappedCylinderPrimitive> Cylinders        {};
    std::vector<SdfTorusPrimitive>          Tori             {};
    EmissiveSphereLuminaire                 EmissiveSphere   {};
    bool                                    EmissiveActive   = true;

    // Optional world mesh / dynamic local asset / bone rig attachments
    const SpatialAcceleration::CompressedWideTree* SceneBvh         = nullptr;
    DistanceFieldSpace::RigidLocalVolumePlacement  VehiclePlacement {};
    const DistanceFieldSpace::ArticulatedCapsuleRig* CharacterRig   = nullptr;

    [[nodiscard]] SdfSurfaceMaterialSpec SampleMaterialAt(uint32_t Slot, Mathematics::Axis3D WorldPos) const noexcept;
    [[nodiscard]] float                  EvaluateOccluderSignedDistance(Mathematics::Axis3D WorldPos) const noexcept;
    [[nodiscard]] float                  EvaluateFullSceneSignedDistance(Mathematics::Axis3D WorldPos, uint32_t& OutMaterialSlot) const noexcept;
    [[nodiscard]] Mathematics::Axis3D    EvaluateFullSceneGradientNormal(Mathematics::Axis3D WorldPos) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                              2-CASCADE 3D VOXEL SDF CLIPMAP + MULTI-BOUNCE RADIOSITY CACHE
//------------------------------------------------------------------------------------------------------------------------

struct VoxelSdfClipmapCascade
{
    uint32_t                            DimX          = 0u;
    uint32_t                            DimY          = 0u;
    uint32_t                            DimZ          = 0u;
    Mathematics::Axis3D                 BoundsMin     {};
    Mathematics::Axis3D                 BoundsMax     {};
    Mathematics::Axis3D                 VoxelSpacing  {};
    std::vector<float>                  DistanceGrid  {}; // Signed occluder distance [m]
    std::vector<Mathematics::LinearRGB> RadiosityGrid {}; // 2-bounce diffuse voxel surface exitance
    std::vector<float>                  OpacityGrid   {}; // Voxel surface occupancy [0, 1]

    [[nodiscard]] float                  SampleDistanceHermite(Mathematics::Axis3D WorldPos) const noexcept;
    [[nodiscard]] Mathematics::LinearRGB SampleRadiosityTrilinear(Mathematics::Axis3D WorldPos, float& OutOpacity) const noexcept;
    [[nodiscard]] std::size_t            QueryAllocatedBytes() const noexcept;
};

class SdfVoxelClipmapHierarchy
{
public:
    VoxelSdfClipmapCascade FineCascade   {}; // Cascade 0: 56x56x40 fine interior SDF + radiosity
    VoxelSdfClipmapCascade CoarseCascade {}; // Cascade 1: 40x40x32 room-wide SDF + Cornell wall radiosity

    void BuildFromShowcaseScene(const SdfShowcaseScene& Scene);
    void BuildFromHybridEnvironment(
        const HybridOccluderEnvironment& Env,
        Mathematics::Axis3D              CenterWorld,
        float                            FineHalfExtentM   = 3.4f,
        float                            CoarseHalfExtentM = 6.8f);

    [[nodiscard]] float                  SampleClipmapDistance(Mathematics::Axis3D WorldPos) const noexcept;
    [[nodiscard]] Mathematics::LinearRGB EvaluateVoxelConeGi(Mathematics::Axis3D WorldPos, Mathematics::Axis3D ShadingNormal) const noexcept;
    [[nodiscard]] std::size_t            QueryAllocatedBytes() const noexcept
    {
        return FineCascade.QueryAllocatedBytes() + CoarseCascade.QueryAllocatedBytes();
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                           CAMERA & RENDER OUTPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SdfShowcaseCamera
{
    Mathematics::Axis3D Eye               { 0.0f, -3.55f, 1.32f };
    Mathematics::Axis3D Target            { -0.18f, 0.42f, 0.68f };
    Mathematics::Axis3D Up                { 0.0f, 0.0f, 1.0f };
    float               VerticalFovRad    = 0.785398f; // 45 deg
    uint32_t            WidthPx           = 480u;
    uint32_t            HeightPx          = 270u;
    float               ExposureScale     = 1.05f;
};

struct SdfEmissiveRenderFrame
{
    uint32_t                            WidthPx            = 0u;
    uint32_t                            HeightPx           = 0u;
    SdfShadowPenumbraMode               Mode               = SdfShadowPenumbraMode::AnalyticSdfConePenumbra;
    std::vector<Mathematics::LinearRGB> LinearHdr          {};
    std::vector<uint8_t>                DisplayRgba8       {};
    std::vector<float>                  PrimaryVisibility  {}; // Direct emissive visibility in [0, 1] for penumbra width audit
};

//------------------------------------------------------------------------------------------------------------------------
//                                        EVALUATION & SHOWCASE ENTRY POINTS
//------------------------------------------------------------------------------------------------------------------------

// Builds the canonical PROOF 11 Emissive-Sphere-Only Cornell Showcase (zero sun, zero area, zero spot, zero IES)
void BuildCanonicalProof11EmissiveSphereScene(SdfShowcaseScene& OutScene);

// Owen-scrambled (0, 2) Laine-Karras Sobol 2D quadrature sample in [0, 1)^2
[[nodiscard]] Mathematics::Axis2D EvaluateOwenScrambledSobol02(
    uint32_t SampleIndex,
    uint32_t PixelKey,
    uint32_t DimensionPair = 0u) noexcept;

// Evaluates directional world shadow visibility for any of the 6 SDF modes (used by TieredWorldRenderSequence on GtxLowEnd)
[[nodiscard]] float EvaluateWorldDirectionalSdfShadow(
    SdfShadowPenumbraMode            Mode,
    const HybridOccluderEnvironment& Env,
    const SdfVoxelClipmapHierarchy*  OptionalClipmap,
    Mathematics::Axis3D              ShadingPos,
    Mathematics::Axis3D              ShadingNormal,
    Mathematics::Axis3D              DirectionToLight,
    float                            TanAngularRadius,
    uint32_t                         PixelKey) noexcept;

// Renders a full viewport frame of the Emissive-Sphere-Only showcase using any of the 6 SDF Shadow/Penumbra modes
[[nodiscard]] SdfEmissiveRenderFrame RenderEmissiveSphereSdfFrame(
    SdfShadowPenumbraMode           Mode,
    const SdfShowcaseScene&         Scene,
    const SdfVoxelClipmapHierarchy& Clipmap,
    const SdfShowcaseCamera&        Camera,
    uint32_t                        MonteCarloSamplesOverride = 96u);

// Measures 5%-to-95% floor shadow penumbra width [m] and minimum umbra visibility behind the primary cylinder occluder
void MeasureFloorShadowPenumbraProfile(
    const SdfShowcaseScene&         Scene,
    const SdfVoxelClipmapHierarchy& Clipmap,
    SdfShadowPenumbraMode           Mode,
    float&                          OutPenumbraWidthMeters,
    float&                          OutMinimumUmbraVisibility) noexcept;

} // namespace Frontier::GeometricRaster::DirectLighting
