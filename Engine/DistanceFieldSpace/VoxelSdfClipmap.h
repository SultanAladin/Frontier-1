/*============================================================================================================================================
                                                            VOXELSDFCLIPMAP.H
============================================================================================================================================*/
// 🧩 Unreal-Style 2-Cascade Voxel SDF Clipmap + Multi-Bounce Surface Cache + Dynamic Local Object SDF Composition

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/ToroidalDistanceClipmap.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             GPU-MIRRORED STD140 / STD430 BLOCKS
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ClipmapCascadeGpuDesc
{
    float BoundsMinSpacing[4] {}; // xyz = BoundsMinWorld [m], w = VoxelSpacingM [m]
    float BoundsMaxPad[4]     {}; // xyz = BoundsMaxWorld [m], w = Unused
    uint32_t DimsPad[4]       {}; // xyz = DimX, DimY, DimZ,   w = TotalVoxels
    float InvSpanPad[4]       {}; // xyz = 1 / (Max - Min),    w = NarrowBandM
};

static_assert(sizeof(ClipmapCascadeGpuDesc) == 64u, "ClipmapCascadeGpuDesc must match 64-byte std140 layout.");

struct alignas(16) LocalSdfObjectGpuDesc
{
    float    WorldToLocal[16]     {}; // Column-major or row-major 4x4 (consistent with Slang -matrix-layout-row-major)
    float    LocalToWorld[16]     {};
    float    LocalBoundsMinPad[4] {}; // xyz = LocalMin [m], w = VoxelSpacingM
    float    LocalBoundsMaxPad[4] {}; // xyz = LocalMax [m], w = BoundingRadiusM
    uint32_t DimsSlotFlags[4]     {}; // xyz = Dims, w = InstanceFlags
};

static_assert(sizeof(LocalSdfObjectGpuDesc) == 176u, "LocalSdfObjectGpuDesc must be 16-byte aligned.");

struct alignas(16) VoxelPointLightSpec
{
    Mathematics::Axis3D    PositionWorld { 0.0f, 0.0f, 2.0f };  // [m]   - World-space emitter origin
    float                  RadiusMeters  = 0.12f;               // [m]   - Emitter sphere radius for soft SDF penumbra
    Mathematics::LinearRGB Color         { 1.0f, 0.85f, 0.65f };// [rgb] - Linear light color
    float                  Intensity     = 14.0f;               // [W/sr]- Radiant intensity multiplier
    float                  RangeMeters   = 18.0f;               // [m]   - Smooth falloff range
};

struct alignas(16) ClipmapLightingMask
{
    bool EnableEmissive = true;
    bool EnablePoint    = true;
    bool EnableSun      = true;
    bool EnableArea     = true;
    bool EnableSkyFill  = true;
};

struct VoxelSdfClipmapConfiguration
{
    Mathematics::Axis3D Clip0CenterWorld      { 0.0f, 0.0f, 1.40f };
    float               Clip0HalfExtentMeters = 0.0f; // 0 = disabled unless configured
    uint32_t            Clip0Resolution       = 0u;
    Mathematics::Axis3D Clip1CenterWorld      { 0.0f, 0.0f, 2.00f };
    float               Clip1HalfExtentMeters = 0.0f;
    uint32_t            Clip1Resolution       = 0u;
    uint32_t            RadiosityBounces      = 2u;
    uint32_t            ConeSteps             = 24u;
    float               MaxConeTraceMeters    = 9.5f;
    ClipmapLightingMask LightingMask          {};
};

struct DynamicClipmapUpdateStats
{
    uint32_t DirtyVoxelCount = 0u;
    uint32_t TotalClip0Voxels = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             SURFACE CACHE & CLIPMAP CASCADES
//------------------------------------------------------------------------------------------------------------------------

struct VoxelSurfaceCacheEntry
{
    float                  SignedDistanceM = 4.0f;               // [m]     - Narrow-band or far-field SDF
    Mathematics::LinearRGB AlbedoRgb       { 0.0f, 0.0f, 0.0f }; // [-]     - Diffuse reflectance tint
    Mathematics::LinearRGB EmissiveRgb     { 0.0f, 0.0f, 0.0f }; // [cd/m2] - Baked emissive exitance
    Mathematics::Axis3D    SurfaceNormal   { 0.0f, 0.0f, 1.0f }; // [-]     - Outward surface normal
    float                  Metalness       = 0.0f;               // [0..1]  - Metallic blend
    float                  Roughness       = 0.5f;               // [0..1]  - Surface roughness
    Mathematics::LinearRGB DirectRadiance  { 0.0f, 0.0f, 0.0f }; // [cd/m2] - Direct lit outgoing radiance / pi
    Mathematics::LinearRGB BounceRadiance  { 0.0f, 0.0f, 0.0f }; // [cd/m2] - Direct + multi-bounce radiosity / pi
    uint16_t               MaterialSlot    = 0u;                 // [idx]   - Closest surface material slot
    uint8_t                HasSurface      = 0u;                 // [0/1]   - 1 when within active surface cache band
};

struct VoxelClipmapCascade
{
    Mathematics::Axis3D                BoundsMin         {};
    Mathematics::Axis3D                BoundsMax         {};
    Mathematics::Axis3D                InvSpan           {};
    float                              VoxelSpacingM     = 0.05f;
    uint32_t                           DimX              = 0u;
    uint32_t                           DimY              = 0u;
    uint32_t                           DimZ              = 0u;
    std::vector<VoxelSurfaceCacheEntry> Voxels           {};
    // 2x downsampled radiosity mip for wide cone apertures
    uint32_t                           Mip1DimX          = 0u;
    uint32_t                           Mip1DimY          = 0u;
    uint32_t                           Mip1DimZ          = 0u;
    std::vector<Mathematics::LinearRGB> Mip1Radiance     {};
    uint32_t                           Mip2DimX          = 0u;
    uint32_t                           Mip2DimY          = 0u;
    uint32_t                           Mip2DimZ          = 0u;
    std::vector<Mathematics::LinearRGB> Mip2Radiance     {};

    [[nodiscard]] bool ContainsInsideMargin(Mathematics::Axis3D WorldPos, float MarginFrac = 0.015f) const noexcept;
    [[nodiscard]] float SampleDistanceTrilinear(Mathematics::Axis3D WorldPos) const noexcept;
    [[nodiscard]] Mathematics::LinearRGB SampleRadiosityTrilinear(
        Mathematics::Axis3D WorldPos,
        float               ConeRadiusM) const noexcept;
    [[nodiscard]] Mathematics::Axis3D EvaluateNormal(Mathematics::Axis3D WorldPos) const noexcept;
    void RebuildMip1() noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                      BAKED LOCAL OBJECT SDF (FOR MOVING OBJECTS)
//------------------------------------------------------------------------------------------------------------------------

struct LocalGeometrySdfAsset
{
    std::string                        DebugName       {};
    Mathematics::Axis3D                LocalMin        {};
    Mathematics::Axis3D                LocalMax        {};
    Mathematics::Axis3D                InvSpan         {};
    float                              VoxelSpacingM   = 0.035f;
    uint32_t                           DimX            = 0u;
    uint32_t                           DimY            = 0u;
    uint32_t                           DimZ            = 0u;
    std::vector<VoxelSurfaceCacheEntry> LocalVoxels    {};
    FaceBvhRepository                         LocalAtlas      {};
    BakedFaceDistance                 BakedDistance   {};

    // Bakes exact unsigned distance + local surface cache attributes ONCE in object-local space
    // using arena/f2d37e4b-frontier FaceBvhRepository + BakedFaceDistance (80x40x32 default).
    bool BakeFromLocalTree(
        std::string_view                               Name,
        const SpatialAcceleration::WideBoundingTree&   LocalTree,
        const MaterialSpace::CompactMaterialRegister&  Materials,
        float                                          TargetSpacingM = 0.038f,
        float                                          PaddingM       = 0.15f);

    // Conservative Lipschitz lower-bound interval { Raw, Lower } from BakedFaceDistance
    [[nodiscard]] DistanceInterval SampleConservativeLocalInterval(Mathematics::Axis3D LocalPos) const noexcept;

    // Conservative AABB-padded distance (boxDist + max(0, innerDist)) so rays outside never leak
    [[nodiscard]] float SampleConservativeLocalDistance(Mathematics::Axis3D LocalPos) const noexcept;
    [[nodiscard]] bool SampleLocalSurfaceCache(
        Mathematics::Axis3D      LocalPos,
        VoxelSurfaceCacheEntry&  OutEntry) const noexcept;
    [[nodiscard]] Mathematics::Axis3D EvaluateLocalNormal(Mathematics::Axis3D LocalPos) const noexcept;
};

struct RigidTransform3D
{
    // Row-major 4x4 affine matrices (M * vec4(p, 1))
    std::array<float, 16u> LocalToWorld {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::array<float, 16u> WorldToLocal {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static RigidTransform3D FromTranslationYawPitchRoll(
        Mathematics::Axis3D Translation,
        float               YawRad   = 0.0f,
        float               PitchRad = 0.0f,
        float               RollRad  = 0.0f,
        float               Scale    = 1.0f) noexcept;

    [[nodiscard]] Mathematics::Axis3D TransformPoint(const std::array<float, 16u>& M, Mathematics::Axis3D P) const noexcept;
    [[nodiscard]] Mathematics::Axis3D TransformDirection(const std::array<float, 16u>& M, Mathematics::Axis3D D) const noexcept;
};

struct DynamicLocalSdfInstance
{
    const LocalGeometrySdfAsset* Asset     = nullptr;
    RigidTransform3D           Transform {};
    Mathematics::Axis3D        WorldAabbMin {};
    Mathematics::Axis3D        WorldAabbMax {};
    bool                       Enabled   = true;

    void UpdateWorldBounds() noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                  2-CASCADE VOXEL SDF CLIPMAP + SURFACE CACHE ENGINE
//------------------------------------------------------------------------------------------------------------------------

struct ClipmapShadowResult
{
    float Visibility      = 1.0f;
    float BlockerDistM    = 0.0f;
};

class VoxelSdfClipmapVolume
{
public:
    VoxelClipmapCascade StaticClip0 {};
    VoxelClipmapCascade StaticClip1 {};
    VoxelClipmapCascade ActiveClip0 {}; // StaticClip0 + dynamic local object composition (never rebuilds static!)

    // Exact arena/f2d37e4b-frontier 3-tier 48^3 toroidal clipmap + static atlas + two-sided surface cache
    DistanceClipmap                      ToroidalClipmap    {};
    FaceBvhRepository                           StaticAtlas        {};
    std::vector<ClosedBox>               ClosedBoxes        {};
    std::vector<Mathematics::LinearRGB>  VehicleSideCache   {};
    std::vector<Mathematics::LinearRGB>  StaticSideCache    {};
    std::vector<Mathematics::LinearRGB>  GroundSideCache    {};
    static constexpr uint32_t            GroundCacheExtent  = 64u;
    const MaterialSpace::CompactMaterialRegister* BoundMaterials = nullptr;

    Mathematics::Axis3D DirtyPrevMin { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D DirtyPrevMax { 0.0f, 0.0f, 0.0f };
    bool                HasDirtyPrev = false;

    void SetClosedStaticBoxes(std::span<const ClosedBox> Boxes) noexcept;

    // Bakes static Clip0 (fine near focus) and Clip1 (coarse wide) once from static scene BVH + materials
    // and initializes the 3-tier 48^3 toroidal DistanceClipmap.
    void BuildStaticClipmap(
        const SpatialAcceleration::WideBoundingTree&  StaticTree,
        const MaterialSpace::CompactMaterialRegister& Materials,
        Mathematics::Axis3D                           Clip0Min,
        Mathematics::Axis3D                           Clip0Max,
        uint32_t                                      Clip0DimX,
        uint32_t                                      Clip0DimY,
        uint32_t                                      Clip0DimZ,
        Mathematics::Axis3D                           Clip1Min,
        Mathematics::Axis3D                           Clip1Max,
        uint32_t                                      Clip1DimX,
        uint32_t                                      Clip1DimY,
        uint32_t                                      Clip1DimZ);

    // Evaluates direct lighting (Emissive, Point, Sun, Area) into StaticClip0 & StaticClip1 + two-sided surface caches
    void BakeStaticSurfaceCacheLighting(
        const ClipmapLightingMask&                            Mask,
        const GeometricRaster::SolarDirectionalSource&       Sun,
        std::span<const VoxelPointLightSpec>                  PointLights,
        std::span<const GeometricRaster::PolygonalAreaQuadLight> AreaLights,
        Mathematics::LinearRGB                                SkyRadiance,
        uint32_t                                              BouncePasses = 2u);

    // Relocates the 3-tier 48^3 toroidal DistanceClipmap with dirty-region repair + updates ActiveClip0
    void ComposeDynamicObjectsIntoActiveClip0(
        std::span<const DynamicLocalSdfInstance>              DynamicObjects,
        const ClipmapLightingMask&                            Mask,
        const GeometricRaster::SolarDirectionalSource&       Sun,
        std::span<const VoxelPointLightSpec>                  PointLights,
        std::span<const GeometricRaster::PolygonalAreaQuadLight> AreaLights,
        Mathematics::LinearRGB                                SkyRadiance,
        bool                                                  FullRefresh = false);

    // Exact arena/f2d37e4b-frontier Lipschitz lower-bound clipmap interval sample
    [[nodiscard]] DistanceInterval SampleClipmapInterval(
        Mathematics::Axis3D WorldPos,
        unsigned*           OutTier = nullptr) const noexcept;

    // Exact arena/f2d37e4b-frontier Hybrid occlusion: conservative clipmap lower-bound march + exact triangle BVH refinement
    [[nodiscard]] bool TraceHybridOccluded(
        Mathematics::Axis3D                      Origin,
        Mathematics::Axis3D                      Direction,
        float                                    Limit,
        std::span<const DynamicLocalSdfInstance> DynamicObjects = {},
        unsigned*                                OutSteps       = nullptr,
        float*                                   OutHitDistM    = nullptr) const noexcept;

    // Samples ToroidalClipmap / Clip0 -> Clip1 distance
    [[nodiscard]] float SampleClipmapDistance(
        Mathematics::Axis3D                      WorldPos,
        int&                                     OutCascade,
        std::span<const DynamicLocalSdfInstance> DynamicObjects = {}) const noexcept;

    [[nodiscard]] Mathematics::Axis3D EvaluateClipmapNormal(
        Mathematics::Axis3D                      WorldPos,
        int                                      CascadeHint,
        std::span<const DynamicLocalSdfInstance> DynamicObjects = {}) const noexcept;

    [[nodiscard]] Mathematics::LinearRGB SampleClipmapRadiosity(
        Mathematics::Axis3D WorldPos,
        int                 Cascade,
        float               ConeRadiusM) const noexcept;

    [[nodiscard]] Mathematics::LinearRGB SampleClipmapAlbedo(
        Mathematics::Axis3D WorldPos,
        int                 Cascade) const noexcept;

    [[nodiscard]] const DynamicClipmapUpdateStats& InspectLastDynamicUpdateStats() const noexcept { return LastDynamicStats; }

    DynamicClipmapUpdateStats LastDynamicStats {};

    // Traces Voxel SDF Clipmap Cone Penumbra (or single-cascade coarse SDF penumbra when ForceCoarseOnly=true)
    [[nodiscard]] ClipmapShadowResult TraceClipmapSdfPenumbra(
        Mathematics::Axis3D                      WorldPos,
        Mathematics::Axis3D                      SurfaceNormal,
        Mathematics::Axis3D                      ToLightDir,
        float                                    MaxDistM,
        float                                    LightAngularOrRadius,
        bool                                     IsDirectionalLight,
        std::span<const DynamicLocalSdfInstance> DynamicObjects = {},
        bool                                     ForceCoarseOnly = false) const noexcept;

    // Deterministic 16-cone Fibonacci Surface-Cache Cone GI (zero RNG, zero splotches, zero denoiser!)
    [[nodiscard]] Mathematics::LinearRGB EvaluateDeterministicConeGiOverPi(
        Mathematics::Axis3D                      WorldPos,
        Mathematics::Axis3D                      SurfaceNormal,
        Mathematics::LinearRGB                   SkyRadiance,
        uint32_t                                 ConeCount = 16u,
        std::span<const DynamicLocalSdfInstance> DynamicObjects = {}) const noexcept;
};

} // namespace Frontier::DistanceFieldSpace
