/*============================================================================================================================================
                                                        VULKANWORLDRENDERSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Implementation of VulkanWorldRenderSequence executing WorldViewportRadiometricCompute.comp.spv on hardware Vulkan GPUs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/Lifecycle/ThoughtLog.h"
#include "Engine/GeometricRaster/Atmosphere/CloudNoiseVolume.h"
#include "Engine/GeometricRaster/Atmosphere/J2000BrightStarRepository.h"
#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace Frontier::GeometricRaster
{

namespace
{

struct GpuAreaQuadLightStd140
{
    float CenterHalfWidth[4] {};
    float RightHalfHeight[4] {};
    float UpTwoSided[4]      {};
    float RadiancePacked[4]  {};
};

struct GpuGlassSphereStd140
{
    float CenterRadiusPacked[4] {};
    float OpticsSlotPacked[4]   {};
};

struct GpuLocalCloudVolumeStd140
{
    float CenterShape[4]          {};
    float HalfExtentYaw[4]        {};
    float DensityCovScaleSides[4] {};
    float WindVelCurl[4]          {};
    float SoftAnisoPad[4]         {};
};

static_assert(sizeof(GpuLocalCloudVolumeStd140) == 80u, "GpuLocalCloudVolumeStd140 must equal 80 bytes.");

struct VolumetricCloudUniformStd140
{
    float                         OriginTimeVector[4]      {};
    float                         SunAxisToggle[4]         {};
    float                         SolarIrradianceExt[4]    {};
    float                         SkyColorWind[4]          {};
    float                         DeckGeometry[4]          {};
    float                         DeckOptics[4]            {};
    float                         WindVectorOcclude[4]     {};
    float                         LocalCenterExt[4]        {};
    float                         LocalExtentFraction[4]   {};
    float                         LocalDensityPhase[4]     {};
    float                         FormationOptics[4]       {};
    float                         CloudShadowOptics[4]     {};
    float                         CloudPassControl[4]      {};
    float                         WindForceMeta[4]         {};
    float                         CloudToaSolarIrrad[4]    {};
    float                         CloudSkyAmbient[4]       {};
    float                         CloudAtmoStrengths[4]    {};
    float                         CloudAirScalesCyclone[4] {};
    GpuLocalCloudVolumeStd140     LocalClouds[8]           {};
};
static_assert(sizeof(VolumetricCloudUniformStd140) == 928u, "VolumetricCloudUniformStd140 must equal 928 bytes.");

struct GpuLocalFogVolumeStd140
{
    float CenterShape[4]          {};
    float HalfExtentYaw[4]        {};
    float DensityCovScaleSides[4] {};
    float WindVelCurl[4]          {};
    float TintAniso[4]            {};
};

static_assert(sizeof(GpuLocalFogVolumeStd140) == 80u, "GpuLocalFogVolumeStd140 must equal 80 bytes.");

struct VolumetricFogUniformStd140
{
    float                         HeightParams0[4] {};
    float                         HeightParams1[4] {};
    float                         AerialParams[4]  {};
    float                         FogWindClock[4]  {};
    float                         FogMeta[4]       {};
    float                         GodRayParams0[4] {};
    float                         GodRayParams1[4] {};
    float                         GodRayTint[4]    {};
    GpuLocalFogVolumeStd140       LocalFogs[8]     {};
};

static_assert(sizeof(VolumetricFogUniformStd140) == 768u, "VolumetricFogUniformStd140 must equal 768 bytes.");

struct GpuMoonDescriptorStd140
{
    float DirectionRadiusRad[4] {};
    float TintSynodicPhase[4]   {};
    float OpticsOrientation[4]  {};
    float MetaMask[4]           {};
};

struct CelestialFieldUniformStd140
{
    float SunAxisRadius[4]         {};
    float SunTintIntensity[4]      {};
    float StarParams[4]            {};
    float StarScintillationRot[4]  {};
    float ClockViewportDims[4]     {};
    float AtmoMediumPack[4]        {};
    float SolarOpticsParams[4]     {};
    GpuMoonDescriptorStd140 Moons[6] {};
};

static_assert(sizeof(CelestialFieldUniformStd140) == 496u, "CelestialFieldUniformStd140 must equal 496 bytes.");

struct LensFlareUniformStd140
{
    float SourceScreenGain[4]  {};
    float OpticsStyleHalo[4]   {};
    float GhostRotationTint[4] {};
    float SourceTintAspect[4]  {};
};

static_assert(sizeof(LensFlareUniformStd140) == 64u, "LensFlareUniformStd140 must equal 64 bytes.");

struct AtmosphericSkyUniformStd140
{
    float    SunDirectionIntensity[4] {};
    float    ScatteringStrengths[4]   {};
    float    PlanetDimensions[4]      {};
    float    SkyTintBrightness[4]     {};
    float    GroundAlbedoLimbGlow[4]  {};
    uint32_t LutDimensions[4]         {};
    float    ObserverParams[4]        {};
};

static_assert(sizeof(AtmosphericSkyUniformStd140) == 112u, "AtmosphericSkyUniformStd140 must equal 112 bytes.");

struct StellarCullUniformBlockStd140
{
    float CameraForward[4]   {};
    float CameraRightTanX[4] {};
    float CameraUpTanY[4]    {};
    float ViewportCatalog[4] {};
    float SkyFlags[4]        {};
};

static_assert(sizeof(StellarCullUniformBlockStd140) == 80u, "StellarCullUniformBlockStd140 must equal 80 bytes.");

struct GpuRawStarSpecStd140
{
    float EqDirMag[4]  {};
    float ColorSeed[4] {};
};

static_assert(sizeof(GpuRawStarSpecStd140) == 32u, "GpuRawStarSpecStd140 must equal 32 bytes.");

struct WorldViewportUniformBlockStd140
{
    float                        CameraOriginTanX[4]        {};
    float                        CameraRightTanY[4]         {};
    float                        CameraUpWidth[4]           {};
    float                        CameraForwardHeight[4]     {};
    float                        SunDirConeTan[4]           {};
    float                        SolarIrradiancePad[4]      {};
    float                        SkyRadianceBvhCount[4]     {};
    uint32_t                     LightGlassMaterialDebug[4] {}; // x=AreaLights, y=GlassSpheres, z=Materials, w=DebugChannel
    uint32_t                     EngineShadowReflPad[4]     {}; // x=EngineMode(0..1), y=ShadowAlg(0..4), z=ReflSource(0..1), w=FrameIdx
    float                        SunCascadeCenterSpan[4]    {};
    float                        SunCascadeRightRes[4]      {};
    float                        SunCascadeUpDepth[4]       {};
    float                        SunCascadeForwardPad[4]    {};
    float                        FineCascadeCenterSpan[4]   {};
    float                        FineCascadeParams[4]       {};
    uint32_t                     DenoiseStepParams[4]       {};
    GpuAreaQuadLightStd140       AreaLights[4]              {};
    GpuGlassSphereStd140         GlassSpheres[4]            {};
    uint32_t                     AtmosphereMaskPad[4]       {};
    VolumetricCloudUniformStd140 CloudParams                {};
    VolumetricFogUniformStd140   FogParams                        {};
    CelestialFieldUniformStd140  CelestialParams                  {};
    LensFlareUniformStd140       LensFlareParams                  {};
    float                        GlobalDistanceFineCenterSpan[4]     {};
    float                        GlobalDistanceCoarseCenterSpan[4]   {};
    uint32_t                     GlobalDistanceBakeControl[4]        {};
    int32_t                      GlobalDistanceFineToroidalCell[4]   {};
    int32_t                      GlobalDistanceCoarseToroidalCell[4] {};
    uint32_t                     GlobalDistanceSlabBounds[4]         {};
    uint32_t                     SelectionOutlineSpan[4]             {}; // x=FirstClusterOrd, y=ClusterCount, z/w=Reserved
    float                        SelectionOutlineColorWidth[4]       {}; // xyz=LinearOutlineRGB, w=WidthPx
    float                        PostProcessParams[4]                {}; // x=ExposureGain, y=Saturation, z=Contrast, w=Vignette
    float                        MotionBlurParams0[4]                {}; // x=Enabled(0/1), y=ShutterScale*Intensity, z=SampleCount, w=MaxBlurRadiusPx
    float                        MotionBlurParams1[4]                {}; // x=VelocityOverrideU, y=VelocityOverrideV, z=Style, w=DirAngleOrSpin
    float                        PrevCameraOriginTanX[4]             {}; // xyz=PrevOrigin, w=PrevTanHalfFovX
    float                        PrevCameraForwardValid[4]           {}; // xyz=PrevForward, w=PrevValid
    float                        PrevCameraRightTanY[4]              {}; // xyz=PrevRight, w=PrevTanHalfFovY
    float                        PrevCameraUpPad[4]                  {}; // xyz=PrevUp, w=Reserved
};
static_assert(sizeof(WorldViewportUniformBlockStd140) == 3152u, "WorldViewportUniformBlockStd140 must equal 3152 bytes.");

struct GpuShadedTriangle96
{
    float Corner0Pad[4]  {};
    float Edge01Pad[4]   {};
    float Edge02Pad[4]   {};
    float Normal0Pad[4]  {};
    float Normal1Pad[4]  {};
    float Normal2Slot[4] {};
};

static_assert(sizeof(GpuShadedTriangle96) == 96u, "GpuShadedTriangle96 must equal 96 bytes.");

struct GpuWorldMaterial80
{
    float DiffuseAlbedoRoughness[4] {};
    float SpecParams[4]             {};
    float EdgeTintCoatWeight[4]     {};
    float CoatRoughnessOptics[4]    {};
    float EmissiveRadiancePad[4]    {};
};

static_assert(sizeof(GpuWorldMaterial80) == 80u, "GpuWorldMaterial80 must equal 80 bytes.");

struct GpuPhotometricLamp64
{
    float PositionRange[4]      {}; // xyz = PositionWorld, w = RangeMeters
    float BeamAxisRadius[4]     {}; // xyz = BeamAxis,      w = RadiusMeters
    float ZeroAzimuthProfile[4] {}; // xyz = ZeroAzimuth,   w = float(ProfileOrdinal)
    float PeakIntensityPad[4]   {}; // xyz = PeakIntensity, w = Reserved
};

static_assert(sizeof(GpuPhotometricLamp64) == 64u, "GpuPhotometricLamp64 must equal 64 bytes.");

struct GpuDynamicLocalSdfInstance144
{
    float    WorldToLocalRow0[4]     {}; // xyz = R^T row 0 / scale.x, w = -dot(row0, T)
    float    WorldToLocalRow1[4]     {}; // xyz = R^T row 1 / scale.y, w = -dot(row1, T)
    float    WorldToLocalRow2[4]     {}; // xyz = R^T row 2 / scale.z, w = -dot(row2, T)
    float    LocalToWorldNormRow0[4] {}; // xyz = R row 0,             w = UniformWorldScale
    float    LocalToWorldNormRow1[4] {}; // xyz = R row 1,             w = VoxelSpacing.z
    float    LocalToWorldNormRow2[4] {}; // xyz = R row 2,             w = Reserved
    float    LocalMinPad[4]          {}; // xyz = LocalMin,            w = VoxelSpacing.x
    float    LocalMaxPad[4]          {}; // xyz = LocalMax / HalfExt,  w = VoxelSpacing.y
    uint32_t ResolutionOffset[4]     {}; // x = ResX|(ResY<<16), y = ResZ|(ShapeCode<<16), z = VoxelFloatOffset, w = MaterialSlot|(Active<<16)
};

static_assert(sizeof(GpuDynamicLocalSdfInstance144) == 144u, "GpuDynamicLocalSdfInstance144 must equal 144 bytes.");

struct QuantizedPositionKey
{
    int32_t  QX          = 0;
    int32_t  QY          = 0;
    int32_t  QZ          = 0;
    uint16_t MaterialOrd = 0u;

    [[nodiscard]] bool operator==(const QuantizedPositionKey& Rhs) const noexcept
    {
        return QX == Rhs.QX && QY == Rhs.QY && QZ == Rhs.QZ && MaterialOrd == Rhs.MaterialOrd;
    }

    [[nodiscard]] bool operator<(const QuantizedPositionKey& Rhs) const noexcept
    {
        if (QX != Rhs.QX) return QX < Rhs.QX;
        if (QY != Rhs.QY) return QY < Rhs.QY;
        if (QZ != Rhs.QZ) return QZ < Rhs.QZ;
        return MaterialOrd < Rhs.MaterialOrd;
    }
};

struct QuantizedCornerTriangleEntry
{
    QuantizedPositionKey Key      {};
    uint32_t             TriIndex = 0u;

    [[nodiscard]] bool operator<(const QuantizedCornerTriangleEntry& Rhs) const noexcept
    {
        if (Key == Rhs.Key)
        {
            return TriIndex < Rhs.TriIndex;
        }
        return Key < Rhs.Key;
    }
};

[[nodiscard]] QuantizedPositionKey QuantizeVertexPosition(Mathematics::Axis3D Pos, uint16_t MatOrd) noexcept
{
    return {
        static_cast<int32_t>(std::lround(Pos.X * 2048.0f)),
        static_cast<int32_t>(std::lround(Pos.Y * 2048.0f)),
        static_cast<int32_t>(std::lround(Pos.Z * 2048.0f)),
        MatOrd,
    };
}

[[nodiscard]] float ComputePyramidLocalHalfExtent(const SpatialAcceleration::ClusterPyramid* Pyramid) noexcept
{
    if (Pyramid == nullptr || Pyramid->Positions.empty())
    {
        return 0.40f;
    }
    float MaxRadius = 0.05f;
    for (const Mathematics::Axis3D& Pos : Pyramid->Positions)
    {
        const float Len = std::sqrt(Pos.X * Pos.X + Pos.Y * Pos.Y + Pos.Z * Pos.Z);
        MaxRadius       = std::max(MaxRadius, Len);
    }
    return MaxRadius;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        UDF CLIPMAP & BILATERAL DENOISE CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr float    FallbackLocalHalfExtentMeters = 0.40f;                  // [m]     - Default half-extent when pyramid empty
constexpr float    MinimumValidScaleFloor        = 1.0e-4f;                // [-]     - Minimum positive world scale threshold
constexpr float    DatumChangeToleranceMeters    = 1.0e-4f;                // [m]     - Minimum ground datum shift triggering sync
constexpr float    AutoPyramidMinSpanMeters      = 0.08f;                  // [m]     - Minimum axis span for on-demand UDF bake
constexpr float    AutoPyramidTargetCells        = 40.0f;                  // [count] - Target longest-axis cell count per pyramid
constexpr float    AutoPyramidMinPitchMeters     = 0.035f;                 // [m]     - Finest voxel pitch for on-demand pyramid UDF
constexpr uint32_t AutoPyramidAlignmentQuantum   = 4u;                     // [count] - Brick alignment multiple for pyramid UDF
constexpr uint32_t AutoPyramidMinAxisCells       = 12u;                    // [count] - Minimum axis cells for on-demand pyramid UDF
constexpr uint32_t AutoPyramidMaxAxisCells       = 44u;                    // [count] - Maximum axis cells for on-demand pyramid UDF
constexpr float    AutoPyramidMarginFraction     = 0.08f;                  // [-]     - Exterior AABB padding fraction of max span
constexpr float    AutoPyramidMinMarginMeters    = 0.06f;                  // [m]     - Minimum exterior AABB padding around pyramid
constexpr float    AutoPyramidMaxMarginMeters    = 0.28f;                  // [m]     - Maximum exterior AABB padding around pyramid
constexpr float    MinimumGroundDeckSpanMeters   = 16.0f;                  // [m]     - Minimum XY span classifying analytic ground slab
constexpr float    MaximumGroundDeckHeightMeters = 1.30f;                  // [m]     - Maximum Z thickness classifying ground slab
constexpr float    MaximumGroundDeckTiltDegrees  = 0.5f;                   // [deg]   - Maximum yaw tolerance for horizontal ground slab
constexpr uint32_t PackedResolutionMask16        = 0xFFFFu;                // [mask]  - 16-bit mask packing XYZ grid resolutions
constexpr uint32_t PackedUpperWordShift16        = 16u;                    // [idx]   - Upper 16-bit shift inside 32-bit word
constexpr uint32_t PackedUpperWordShift32        = 32u;                    // [idx]   - Upper 32-bit shift inside 64-bit signature
constexpr uint32_t DebugChannelShadedLit         = 0u;                     // [id]    - Full shaded radiometric viewport output
constexpr uint32_t DebugChannelSunShadow         = 8u;                     // [id]    - Direct solar shadow diagnostic view
constexpr uint32_t DebugChannelFilteredAo        = 9u;                     // [id]    - Bilaterally denoised UDF ambient occlusion
constexpr uint32_t DebugChannelSpecular          = 11u;                    // [id]    - Specular indirect lighting diagnostic view
constexpr uint32_t DebugChannelDistanceHeat      = 12u;                    // [id]    - UDF distance heatmap diagnostic view
constexpr uint32_t DebugChannelRawUdfAo          = 15u;                    // [id]    - Unfiltered 12-ray UDF ambient occlusion
constexpr uint32_t DebugChannelCascadeTint       = 16u;                    // [id]    - UDF cascade residency diagnostic view
constexpr uint32_t EnableFinalToneStackBit       = 1u << 0u;               // [mask]  - Final step tone-stack & display output bit
constexpr uint32_t EnableSpatialAoDenoiseBit     = 1u << 1u;               // [mask]  - Joint-bilateral UDF AO spatial filter bit
constexpr uint32_t EnableSpatialSunDenoiseBit    = 1u << 2u;               // [mask]  - Joint-bilateral solar shadow filter bit
constexpr uint32_t PenumbraMultiStepThreshold    = 4u;                     // [id]    - Penumbra preset ordinal enabling 3-step sun blur
constexpr uint32_t BilateralDenoiseStepCount     = 3u;                     // [count] - À-trous bilateral spatial filter step count
constexpr std::array<uint32_t, BilateralDenoiseStepCount> BilateralDenoiseStrides {{
    1u, 2u, 4u
}};

[[nodiscard]] bool ComputePyramidLocalBounds(
    const SpatialAcceleration::ClusterPyramid* PyramidAsset,
    Mathematics::Axis3D&                       OutMinimum,
    Mathematics::Axis3D&                       OutMaximum) noexcept
{
    if (PyramidAsset == nullptr || PyramidAsset->Positions.empty())
    {
        OutMinimum = { -FallbackLocalHalfExtentMeters, -FallbackLocalHalfExtentMeters, -FallbackLocalHalfExtentMeters };
        OutMaximum = {  FallbackLocalHalfExtentMeters,  FallbackLocalHalfExtentMeters,  FallbackLocalHalfExtentMeters };
        return false;
    }
    OutMinimum = PyramidAsset->Positions.front();
    OutMaximum = PyramidAsset->Positions.front();
    for (const Mathematics::Axis3D& Position : PyramidAsset->Positions)
    {
        OutMinimum.X = std::min(OutMinimum.X, Position.X);
        OutMinimum.Y = std::min(OutMinimum.Y, Position.Y);
        OutMinimum.Z = std::min(OutMinimum.Z, Position.Z);
        OutMaximum.X = std::max(OutMaximum.X, Position.X);
        OutMaximum.Y = std::max(OutMaximum.Y, Position.Y);
        OutMaximum.Z = std::max(OutMaximum.Z, Position.Z);
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VulkanWorldRenderSequence::~VulkanWorldRenderSequence()
{
    TeardownSequence();
}

bool VulkanWorldRenderSequence::InitializeSequence(
    VulkanInterchange&           TargetDevice,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownSequence();
    if (!TargetDevice.Ready())
    {
        return false;
    }

    BoundDevice = &TargetDevice;
    CachedCompiledShaderDir = CompiledShaderDirectory;
    if (!DispatchEncoder.InitializeEncoder(BoundDevice, 128u))
    {
        BoundDevice = nullptr;
        return false;
    }

    constexpr std::array<VkDescriptorType, 39u> SlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // 0:  WorldUniforms
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 1:  SceneBvhEntries
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 2:  SceneTriangles
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 3:  MaterialEntries
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 4:  OutDisplayRgba8
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 5:  SolarShadowDepthBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 6:  AccumulationHdrBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 7:  AtmosphericSkyViewLut
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 8:  LunarSurfaceAtlasTexels
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 9:  BakedLensFlareAtlasTexels
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 10: GBufPosMask
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 11: GBufNormAo
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 12: GBufDirectBase
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 13: GBufSunContrib
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 14: GBufAlbedo
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 15: PingShadowAo
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 16: PongShadowAo
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 17: CloudRadiometricBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 18: ProjectedCatalogStars
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 19: StellarTileBins
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 20: PlanetaryCloudNoise
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 21: RawCatalogStars
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 22: PhotometricProfileTables
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 23: PhotometricLampEntries
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 24: GlobalDistanceFieldBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 25: GlobalRadiosityFieldBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 26: PingIndirectIrradiance
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 27: PongIndirectIrradiance
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 28: GlobalSurfaceShellBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 29: LocalAssetSdfVoxelBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 30: DynamicLocalSdfInstanceBuffer
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 31: ActiveClusterBitmask
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 32: GdfFineTex3D
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 33: GdfCoarseTex3D
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 34: RadiosityMip0Tex3D
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 35: RadiosityCoarseTex3D
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 36: GdfBakeTargetImg3D
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 37: RadiosityBakeDstImg3D
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // 38: PrimaryVisImg2D
    }};

    const std::filesystem::path ShaderBinaryPath = CompiledShaderDirectory / "WorldViewportRadiometricCompute.comp.spv";
    RadiometricProgramHandle = DispatchEncoder.RegisterProgram(
        ShaderBinaryPath,
        SlotTypes,
        "ComputeEntry");
    if (RadiometricProgramHandle == UINT32_MAX)
    {
        const std::string PathText = ShaderBinaryPath.generic_string();
        std::fprintf(
            stderr,
            "[FrontierWorld] WARNING: Failed to load GPU compute shader %s — falling back to CPU renderer.\n",
            PathText.c_str());
        TeardownSequence();
        return false;
    }

    const std::filesystem::path CloudShaderPath = CompiledShaderDirectory / "WorldViewportCloudCompute.comp.spv";
    CloudLayerProgramHandle = DispatchEncoder.RegisterProgram(
        CloudShaderPath,
        SlotTypes,
        "CloudComputeEntry");
    if (CloudLayerProgramHandle == UINT32_MAX)
    {
        const std::string PathText = CloudShaderPath.generic_string();
        std::fprintf(
            stderr,
            "[FrontierWorld] WARNING: Failed to load GPU cloud shader %s — falling back to CPU renderer.\n",
            PathText.c_str());
        TeardownSequence();
        return false;
    }

    const std::filesystem::path HalfResShaderPath = CompiledShaderDirectory / "WorldViewportHalfResIndirectCompute.comp.spv";
    HalfResIndirectProgramHandle = DispatchEncoder.RegisterProgram(
        HalfResShaderPath,
        SlotTypes,
        "HalfResIndirectEntry");

    const std::filesystem::path DenoiseShaderPath = CompiledShaderDirectory / "WorldViewportDenoiseCompute.comp.spv";
    DenoiseProgramHandle = DispatchEncoder.RegisterProgram(
        DenoiseShaderPath,
        SlotTypes,
        "DenoiseCompositeEntry");
    if (DenoiseProgramHandle == UINT32_MAX)
    {
        const std::string PathText = DenoiseShaderPath.generic_string();
        std::fprintf(
            stderr,
            "[FrontierWorld] WARNING: Failed to load GPU denoise shader %s — falling back to CPU renderer.\n",
            PathText.c_str());
        TeardownSequence();
        return false;
    }

    const std::filesystem::path MotionBlurShaderPath = CompiledShaderDirectory / "WorldViewportMotionBlurCompute.comp.spv";
    MotionBlurProgramHandle = DispatchEncoder.RegisterProgram(
        MotionBlurShaderPath,
        SlotTypes,
        "MotionBlurPostEntry");

    constexpr std::array<VkDescriptorType, 2u> SkySlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    TransmittanceLutProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "TransmittanceLutCompute.comp.spv",
        SkySlotTypes,
        "TransmittanceLutComputeEntry");
    MultiScatterLutProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "MultiScatterLutCompute.comp.spv",
        SkySlotTypes,
        "MultiScatterLutComputeEntry");
    SkyViewLutProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "SkyViewTransmittanceCompute.comp.spv",
        SkySlotTypes,
        "ComputeEntry");

    constexpr std::array<VkDescriptorType, 4u> StellarCullSlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    StellarClearProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "StellarTileClearCompute.comp.spv",
        StellarCullSlotTypes,
        "ClearStellarTilesEntry");
    StellarCullProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "StellarTileCullCompute.comp.spv",
        StellarCullSlotTypes,
        "ProjectCullStellarTilesEntry");

    ShadowCascadeProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "WorldViewportShadowCascadeCompute.comp.spv",
        SlotTypes,
        "ShadowCascadeComputeEntry");
    GlobalDistanceBakeProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "WorldViewportGlobalDistanceBake.comp.spv",
        SlotTypes,
        "GlobalDistanceBakeEntry");
    GlobalRadiosityProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "WorldViewportGlobalRadiosityBake.comp.spv",
        SlotTypes,
        "GlobalRadiosityBakeEntry");
    HighEndBvhProgramHandle           = UINT32_MAX;
    ViewportPathTraceProgramHandle    = UINT32_MAX;
    InstanceTransformBvhProgramHandle = UINT32_MAX;
    InstanceTransformTriProgramHandle = UINT32_MAX;
    {
        constexpr std::array<VkDescriptorType, 5u> TransformSlotTypes {{
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // 0: InstanceTransform
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 1: LocalBvhEntries
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 2: WorldBvhEntries
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 3: LocalTriangles
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // 4: WorldTriangles
        }};
        const auto XformBvhPath = CompiledShaderDirectory / "WorldInstanceTransformBvh.comp.spv";
        const auto XformTriPath = CompiledShaderDirectory / "WorldInstanceTransformTri.comp.spv";
        if (std::filesystem::exists(XformBvhPath) && std::filesystem::exists(XformTriPath))
        {
            InstanceTransformBvhProgramHandle = DispatchEncoder.RegisterProgram(
                XformBvhPath,
                TransformSlotTypes,
                "TransformBvhNodesEntry");
            InstanceTransformTriProgramHandle = DispatchEncoder.RegisterProgram(
                XformTriPath,
                TransformSlotTypes,
                "TransformTrianglesEntry");
        }
    }
    if (TargetDevice.QueryHardwareAccelerated())
    {
        HighEndBvhProgramHandle = DispatchEncoder.RegisterProgram(
            CompiledShaderDirectory / "WorldViewportHighEndCompute.comp.spv",
            SlotTypes,
            "HighEndBvhComputeEntry");
        ViewportPathTraceProgramHandle = DispatchEncoder.RegisterProgram(
            CompiledShaderDirectory / "WorldViewportPathTraceCompute.comp.spv",
            SlotTypes,
            "ViewportPathTraceEntry");
        DispatchEncoder.PersistProgramCache();
    }

    if (TargetDevice.QueryTimestampValidBits() > 0u)
    {
        VkQueryPoolCreateInfo PoolInfo {};
        PoolInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        PoolInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        PoolInfo.queryCount = 16u;
        if (vkCreateQueryPool(TargetDevice.AccessDevice(), &PoolInfo, nullptr, &TimestampQueryPool) != VK_SUCCESS)
        {
            TimestampQueryPool = VK_NULL_HANDLE;
        }
        for (AsyncLiveFrameSlot& Slot : LiveFrameSlots)
        {
            if (vkCreateQueryPool(TargetDevice.AccessDevice(), &PoolInfo, nullptr, &Slot.TimestampQueryPool) != VK_SUCCESS)
            {
                Slot.TimestampQueryPool = VK_NULL_HANDLE;
            }
        }
    }

    EnsureBufferCapacity(UniformBuffer, 8192ull * 12ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(PrecomputeUniformBuffer, 8192ull * 24ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(InstanceTransformUniformBuffer, 256ull * 64ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(TopNodeStagingBuffer, 64ull * sizeof(SpatialAcceleration::WideTreeEntry), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    for (AsyncLiveFrameSlot& Slot : LiveFrameSlots)
    {
        EnsureBufferCapacity(Slot.UniformBuffer, 8192ull * 12ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
    EnsureDeviceBufferCapacity(GlobalDistanceFieldBuffer, 2ull * 262144ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalRadiosityFieldBuffer, 299520ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalSurfaceShellBuffer, 262144ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (!EnsureVolume3DResources())
    {
        TeardownSequence();
        return false;
    }
    EnsureBufferCapacity(SkyViewUniformBuffer, sizeof(AtmosphericSkyUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(StellarCullUniformBuffer, sizeof(StellarCullUniformBlockStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    {
        std::vector<GpuRawStarSpecStd140> RawStars;
        RawStars.reserve(std::size(J2000BrightStars));
        for (std::size_t I = 0u; I < std::size(J2000BrightStars); ++I)
        {
            const auto& Src  = J2000BrightStars[I];
            const float RaR  = Src.RightAscensionHours * (Constants::TauFloat / 24.0f);
            const float DecR = Src.DeclinationDegrees * Constants::DegToRadFloat;
            const float CosD = std::cos(DecR);
            const auto  Tint = LunarStarfieldSequence::EvaluateStarColorFromBv(Src.ColorBv);
            GpuRawStarSpecStd140 Dst {};
            Dst.EqDirMag[0]  = CosD * std::cos(RaR);
            Dst.EqDirMag[1]  = CosD * std::sin(RaR);
            Dst.EqDirMag[2]  = std::sin(DecR);
            Dst.EqDirMag[3]  = Src.VisualMagnitude;
            Dst.ColorSeed[0] = Tint.R;
            Dst.ColorSeed[1] = Tint.G;
            Dst.ColorSeed[2] = Tint.B;
            Dst.ColorSeed[3] = std::fmod(static_cast<float>(I) * 0.61803398875f, 1.0f);
            RawStars.push_back(Dst);
        }
        std::sort(RawStars.begin(), RawStars.end(), [](const GpuRawStarSpecStd140& A, const GpuRawStarSpecStd140& B)
        {
            return A.EqDirMag[3] < B.EqDirMag[3];
        });
        UploadedCatalogStarCount = static_cast<uint32_t>(RawStars.size());
        const uint64_t RawBytes  = std::max<uint64_t>(32ull, static_cast<uint64_t>(RawStars.size()) * sizeof(GpuRawStarSpecStd140));
        if (!RawStars.empty())
        {
            UploadToDeviceBuffer(RawCatalogStarBuffer, RawStars.data(), RawBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
        else
        {
            const GpuRawStarSpecStd140 ZeroStar {};
            UploadToDeviceBuffer(RawCatalogStarBuffer, &ZeroStar, sizeof(ZeroStar), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
        EnsureDeviceBufferCapacity(ProjectedCatalogStarBuffer, RawBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        EnsureDeviceBufferCapacity(StellarTileBinsBuffer, 4096ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    {
        const auto& NoiseTexels = CondensateNoise::QueryTexels();
        const uint64_t NoiseBytes = std::max<uint64_t>(4ull, static_cast<uint64_t>(NoiseTexels.size()) * sizeof(uint32_t));
        if (!NoiseTexels.empty())
        {
            UploadToDeviceBuffer(PlanetaryCloudNoiseBuffer, NoiseTexels.data(), NoiseBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
        else
        {
            const uint32_t Zero = 0u;
            UploadToDeviceBuffer(PlanetaryCloudNoiseBuffer, &Zero, sizeof(Zero), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
    }
    constexpr uint64_t SkyLutBytes = static_cast<uint64_t>(AtmosphericSkySequence::TotalPackedLutTexels) * sizeof(float) * 4ull;
    EnsureDeviceBufferCapacity(AtmosphericSkyViewLutBuffer, SkyLutBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
        const std::vector<uint8_t> ZeroLut(static_cast<std::size_t>(SkyLutBytes), 0u);
        UploadToDeviceBuffer(AtmosphericSkyViewLutBuffer, ZeroLut.data(), SkyLutBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    {
        const auto& MoonAtlasRgba = LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8();
        const uint64_t MoonAtlasBytes = std::max<uint64_t>(4ull, static_cast<uint64_t>(MoonAtlasRgba.size()));
        if (!MoonAtlasRgba.empty())
        {
            UploadToDeviceBuffer(LunarSurfaceAtlasBuffer, MoonAtlasRgba.data(), MoonAtlasBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
        else
        {
            const uint32_t Zero = 0u;
            UploadToDeviceBuffer(LunarSurfaceAtlasBuffer, &Zero, sizeof(Zero), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
    }
    {
        std::vector<uint8_t> FlareAtlasRgba {};
        if (!LensFlareSequence::BakeOpticalAtlasOnGpu(TargetDevice, CompiledShaderDirectory, FlareAtlasRgba))
        {
            FlareAtlasRgba = LensFlareSequence::QueryBakedLensFlareAtlasRgba8();
        }
        const uint64_t FlareAtlasBytes = std::max<uint64_t>(4ull, static_cast<uint64_t>(FlareAtlasRgba.size()));
        if (!FlareAtlasRgba.empty())
        {
            UploadToDeviceBuffer(BakedLensFlareAtlasBuffer, FlareAtlasRgba.data(), FlareAtlasBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
        else
        {
            const uint32_t Zero = 0u;
            UploadToDeviceBuffer(BakedLensFlareAtlasBuffer, &Zero, sizeof(Zero), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
    }
    {
        const float ZeroFloat = 0.0f;
        UploadToDeviceBuffer(PhotometricProfileTableBuffer, &ZeroFloat, sizeof(ZeroFloat), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const GpuPhotometricLamp64 ZeroLamp {};
        UploadToDeviceBuffer(PhotometricLampEntryBuffer, &ZeroLamp, sizeof(ZeroLamp), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        UploadToDeviceBuffer(LocalAssetSdfVoxelBuffer, &ZeroFloat, sizeof(ZeroFloat), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        const GpuDynamicLocalSdfInstance144 ZeroInst {};
        UploadToDeviceBuffer(DynamicLocalSdfInstanceBuffer, &ZeroInst, sizeof(ZeroInst), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        MaskSliceOffsetBytes = 256ull;
        EnsureBufferCapacity(ActiveClusterMaskBuffer, MaskSliceOffsetBytes * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (ActiveClusterMaskBuffer.HostAddress != nullptr)
        {
            std::memset(ActiveClusterMaskBuffer.HostAddress, 0xFF, static_cast<std::size_t>(MaskSliceOffsetBytes * 2ull));
        }
        UploadedPhotometricProfiles   = 0u;
        UploadedPhotometricLamps      = 0u;
        UploadedLocalSdfInstanceCount = 0u;
        CachedLocalSdfAssetSignature  = 0ull;
        CachedLocalSdfOffsets.clear();
    }
    (void)InitializePrimaryVisRasterPipeline();
    if (UniformBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        std::fprintf(
            stdout,
            "[FrontierWorld] VulkanWorldRenderSequence active on %.*s (Hardware GPU: %s, GPU ShadowCascade: %s, HW VisBuffer: %s, HalfRes AO/GI: %s)\n",
            static_cast<int>(TargetDevice.QueryAdapterName().size()),
            TargetDevice.QueryAdapterName().data(),
            TargetDevice.QueryHardwareAccelerated() ? "YES" : "NO (Software ICD)",
            ShadowCascadeProgramHandle != UINT32_MAX ? "YES" : "NO",
            PrimaryVisPipeline != VK_NULL_HANDLE ? "YES" : "NO",
            HalfResIndirectProgramHandle != UINT32_MAX ? "YES" : "NO");
        std::fflush(stdout);
        return true;
    }
    return false;
}

void VulkanWorldRenderSequence::TeardownSequence() noexcept
{
    if (BoundDevice != nullptr && BoundDevice->Ready())
    {
        FlushAllAsyncDispatches();
        vkDeviceWaitIdle(BoundDevice->AccessDevice());
        ReapRetiredDisplayImages(true);
        ReleasePrimaryVisResources();
        ReleaseVolume3DResources();
        if (TimestampQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(BoundDevice->AccessDevice(), TimestampQueryPool, nullptr);
            TimestampQueryPool = VK_NULL_HANDLE;
        }
        for (AsyncLiveFrameSlot& Slot : LiveFrameSlots)
        {
            if (Slot.TimestampQueryPool != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(BoundDevice->AccessDevice(), Slot.TimestampQueryPool, nullptr);
                Slot.TimestampQueryPool = VK_NULL_HANDLE;
            }
            BoundDevice->ReleaseBuffer(Slot.UniformBuffer);
            Slot.CommandBuffer    = VK_NULL_HANDLE;
            Slot.TimelineSignal   = 0ull;
            Slot.HasGpuTimestamps = false;
        }
        BoundDevice->ReleaseBuffer(UniformBuffer);
        BoundDevice->ReleaseBuffer(PrecomputeUniformBuffer);
        BoundDevice->ReleaseBuffer(InstanceTransformUniformBuffer);
        BoundDevice->ReleaseBuffer(TopNodeStagingBuffer);
        BoundDevice->ReleaseBuffer(ResidentLocalBvhBuffer);
        BoundDevice->ReleaseBuffer(ResidentLocalTriBuffer);
        BoundDevice->ReleaseBuffer(SkyViewUniformBuffer);
        BoundDevice->ReleaseBuffer(StellarCullUniformBuffer);
        BoundDevice->ReleaseBuffer(UploadStagingBuffer);
        BoundDevice->ReleaseBuffer(OutputReadbackStagingBuffer);
        BoundDevice->ReleaseBuffer(BvhEntryBuffer);
        BoundDevice->ReleaseBuffer(ShadedTriangleBuffer);
        BoundDevice->ReleaseBuffer(MaterialEntryBuffer);
        BoundDevice->ReleaseBuffer(OutputPixelBuffer);
        BoundDevice->ReleaseBuffer(SolarShadowDepthBuffer);
        BoundDevice->ReleaseBuffer(AccumulationHdrBuffer);
        BoundDevice->ReleaseBuffer(AtmosphericSkyViewLutBuffer);
        BoundDevice->ReleaseBuffer(LunarSurfaceAtlasBuffer);
        BoundDevice->ReleaseBuffer(BakedLensFlareAtlasBuffer);
        BoundDevice->ReleaseBuffer(GBufPosMaskBuffer);
        BoundDevice->ReleaseBuffer(GBufNormAoBuffer);
        BoundDevice->ReleaseBuffer(GBufDirectBaseBuffer);
        BoundDevice->ReleaseBuffer(GBufSunContribBuffer);
        BoundDevice->ReleaseBuffer(GBufAlbedoBuffer);
        BoundDevice->ReleaseBuffer(PingShadowAoBuffer);
        BoundDevice->ReleaseBuffer(PongShadowAoBuffer);
        BoundDevice->ReleaseBuffer(CloudRadiometricBuffer);
        BoundDevice->ReleaseBuffer(PlanetaryCloudNoiseBuffer);
        BoundDevice->ReleaseBuffer(RawCatalogStarBuffer);
        BoundDevice->ReleaseBuffer(ProjectedCatalogStarBuffer);
        BoundDevice->ReleaseBuffer(StellarTileBinsBuffer);
        BoundDevice->ReleaseBuffer(PhotometricProfileTableBuffer);
        BoundDevice->ReleaseBuffer(PhotometricLampEntryBuffer);
        BoundDevice->ReleaseBuffer(GlobalDistanceFieldBuffer);
        BoundDevice->ReleaseBuffer(GlobalRadiosityFieldBuffer);
        BoundDevice->ReleaseBuffer(PingIndirectIrradianceBuffer);
        BoundDevice->ReleaseBuffer(PongIndirectIrradianceBuffer);
        BoundDevice->ReleaseBuffer(GlobalSurfaceShellBuffer);
        BoundDevice->ReleaseBuffer(LocalAssetSdfVoxelBuffer);
        BoundDevice->ReleaseBuffer(DynamicLocalSdfInstanceBuffer);
        BoundDevice->ReleaseBuffer(ActiveClusterMaskBuffer);
        BoundDevice->ReleaseImage2D(DisplayOutputImage2D);
    }
    RetiredDisplayImages.clear();
    AssetClusterTreeCache.clear();
    PreparedPyramidVolumes.clear();
    ResidentClusterInstances.clear();
    TopBranchGateRules.clear();
    FinestClusterMaskWords.clear();
    ViewportClusterMaskWords.clear();
    DispatchEncoder.TeardownEncoder();
    RadiometricProgramHandle          = UINT32_MAX;
    CloudLayerProgramHandle           = UINT32_MAX;
    HalfResIndirectProgramHandle      = UINT32_MAX;
    DenoiseProgramHandle              = UINT32_MAX;
    MotionBlurProgramHandle           = UINT32_MAX;
    TransmittanceLutProgramHandle     = UINT32_MAX;
    MultiScatterLutProgramHandle      = UINT32_MAX;
    SkyViewLutProgramHandle           = UINT32_MAX;
    StellarClearProgramHandle         = UINT32_MAX;
    StellarCullProgramHandle          = UINT32_MAX;
    ShadowCascadeProgramHandle        = UINT32_MAX;
    GlobalDistanceBakeProgramHandle   = UINT32_MAX;
    GlobalRadiosityProgramHandle      = UINT32_MAX;
    HighEndBvhProgramHandle           = UINT32_MAX;
    ViewportPathTraceProgramHandle    = UINT32_MAX;
    InstanceTransformBvhProgramHandle = UINT32_MAX;
    InstanceTransformTriProgramHandle = UINT32_MAX;
    LiveFrameCursor                   = 0u;
    PendingPrecomputeCmd              = VK_NULL_HANDLE;
    PendingPrecomputeSignal           = 0ull;
    PendingTransformCmd               = VK_NULL_HANDLE;
    PendingTransformSignal            = 0ull;
    ReservedTopNodeCount              = 0u;
    CachedResidentTopologySig         = 0ull;
    UploadedCatalogStarCount        = 0u;
    UploadedPhotometricProfiles     = 0u;
    UploadedPhotometricLamps        = 0u;
    UploadedLocalSdfInstanceCount   = 0u;
    CachedLocalSdfAssetSignature    = 0ull;
    CachedDynamicSdfSignature       = 0ull;
    ActiveClipFocusWorld            = { 0.0f, 0.0f, 0.0f };
    LastFineShadowFocusWorld        = { 0.0f, 0.0f, 0.0f };
    LastCoarseShadowFocusWorld      = { 0.0f, 0.0f, 0.0f };
    LastFineGdfFocusWorld           = { 0.0f, 0.0f, 0.0f };
    LastCoarseGdfFocusWorld         = { 0.0f, 0.0f, 0.0f };
    ActiveClipFocusInitialized      = false;
    CascadeFocusInitialized         = false;
    FineToroidalValid               = false;
    CoarseToroidalValid             = false;
    PendingFineGeometryReset        = true;
    PendingCoarseGeometryReset      = true;
    DistanceFieldStats              = {};
    ActiveAnalyticGroundPlaneZ       = UnsetAnalyticGroundPlaneZ;
    ActiveAnalyticGroundMaterialSlot = 0u;
    CachedLocalSdfOffsets.clear();
    MaskSliceOffsetBytes            = 256ull;
    CachedNaniteCutSignature        = 0u;
    CachedResidentTransformSig      = 0u;
    ResidentFinestTriangleCount     = 0u;
    ResidentClusterBvhActive        = false;
    ViewportMaskHoldsFinestOnly     = true;
    FrameSequenceIndex              = 0u;
    BoundDevice                     = nullptr;
    SceneUploadedOnce               = false;
    GlobalDistanceFieldReady        = false;
    SkyViewLutValid                 = false;
    LastSkyViewUniformSnapshot.fill(0u);
    LastRadiosityLightingSignature  = 0ull;
    HasPrevMotionBlurCameraBasis    = false;
    UploadedBvhCount                = 0u;
    UploadedTriangleCount           = 0u;
    UploadedClusterSlotCount        = 0u;
    UploadedMaterialCount           = 0u;
}

bool VulkanWorldRenderSequence::QueryReady() const noexcept
{
    return BoundDevice != nullptr && BoundDevice->Ready() &&
           RadiometricProgramHandle != UINT32_MAX &&
           CloudLayerProgramHandle != UINT32_MAX &&
           DenoiseProgramHandle != UINT32_MAX;
}

bool VulkanWorldRenderSequence::QueryHardwareGpuActive() const noexcept
{
    return BoundDevice != nullptr && BoundDevice->QueryHardwareAccelerated();
}

std::string_view VulkanWorldRenderSequence::QueryAdapterTitle() const noexcept
{
    return BoundDevice != nullptr ? BoundDevice->QueryAdapterName() : std::string_view{};
}

void VulkanWorldRenderSequence::EnsureBufferCapacity(
    DeviceBufferResource& TargetBuffer,
    uint64_t              MinimumBytes,
    VkBufferUsageFlags    UsageMask)
{
    const uint64_t SafeBytes = std::max<uint64_t>(64ull, MinimumBytes);
    if (TargetBuffer.BufferHandle != VK_NULL_HANDLE && TargetBuffer.ByteCapacity >= SafeBytes)
    {
        return;
    }
    if (BoundDevice == nullptr)
    {
        return;
    }
    if (TargetBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        FlushAllAsyncDispatches();
    }
    BoundDevice->ReleaseBuffer(TargetBuffer);
    const uint64_t PaddedBytes = std::max<uint64_t>(SafeBytes, (SafeBytes * 5ull) / 4ull);
    TargetBuffer               = DispatchEncoder.AllocateHostWritableBuffer(PaddedBytes, UsageMask);
}

void VulkanWorldRenderSequence::EnsureDeviceBufferCapacity(
    DeviceBufferResource& TargetBuffer,
    uint64_t              MinimumBytes,
    VkBufferUsageFlags    UsageMask)
{
    const uint64_t SafeBytes = std::max<uint64_t>(64ull, MinimumBytes);
    if (TargetBuffer.BufferHandle != VK_NULL_HANDLE && TargetBuffer.ByteCapacity >= SafeBytes)
    {
        return;
    }
    if (BoundDevice == nullptr)
    {
        return;
    }
    if (TargetBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        FlushAllAsyncDispatches();
    }
    BoundDevice->ReleaseBuffer(TargetBuffer);
    const uint64_t PaddedBytes = std::max<uint64_t>(SafeBytes, (SafeBytes * 5ull) / 4ull);
    TargetBuffer               = DispatchEncoder.AllocateDeviceLocalBuffer(
        PaddedBytes, UsageMask | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}

void VulkanWorldRenderSequence::EnsureReadbackBufferCapacity(
    DeviceBufferResource& TargetBuffer,
    uint64_t              MinimumBytes)
{
    const uint64_t SafeBytes = std::max<uint64_t>(64ull, MinimumBytes);
    if (TargetBuffer.BufferHandle != VK_NULL_HANDLE && TargetBuffer.ByteCapacity >= SafeBytes)
    {
        return;
    }
    if (BoundDevice == nullptr)
    {
        return;
    }
    if (TargetBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        FlushAllAsyncDispatches();
    }
    BoundDevice->ReleaseBuffer(TargetBuffer);
    const uint64_t PaddedBytes = std::max<uint64_t>(SafeBytes, (SafeBytes * 5ull) / 4ull);
    TargetBuffer               = DispatchEncoder.AllocateReadbackStagingBuffer(PaddedBytes);
}

uint64_t VulkanWorldRenderSequence::QueryLatestSubmittedTimelineSignal() const noexcept
{
    uint64_t Latest = std::max(PendingPrecomputeSignal, PendingTransformSignal);
    for (const AsyncLiveFrameSlot& Slot : LiveFrameSlots)
    {
        if (Slot.TimelineSignal > Latest)
        {
            Latest = Slot.TimelineSignal;
        }
    }
    return Latest;
}

void VulkanWorldRenderSequence::RetireLiveFrameSlot(uint32_t SlotIndex, bool BlockIfBusy) noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready() || SlotIndex >= kLiveFrameSlots)
    {
        return;
    }
    AsyncLiveFrameSlot& Slot = LiveFrameSlots[SlotIndex];
    if (Slot.CommandBuffer == VK_NULL_HANDLE)
    {
        return;
    }
    if (Slot.TimelineSignal != 0ull && !BoundDevice->QueryTimelineReached(Slot.TimelineSignal))
    {
        if (!BlockIfBusy)
        {
            return;
        }
        (void)BoundDevice->AwaitTimelineTick(Slot.TimelineSignal, 60000000000ull);
    }

    if (Slot.HasGpuTimestamps && Slot.TimestampQueryPool != VK_NULL_HANDLE)
    {
        uint64_t Stamps[10u]{};
        if (vkGetQueryPoolResults(
                BoundDevice->AccessDevice(),
                Slot.TimestampQueryPool,
                0u,
                10u,
                sizeof(Stamps),
                Stamps,
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            const uint32_t Bits     = BoundDevice->QueryTimestampValidBits();
            const uint64_t Mask     = (Bits >= 64u) ? ~0ull : ((1ull << Bits) - 1ull);
            const double   PeriodMs = static_cast<double>(BoundDevice->QueryTimestampPeriodNs()) * 1.0e-6;
            auto DeltaMs = [&](uint32_t A, uint32_t B) -> float
            {
                const uint64_t DA = Stamps[A] & Mask;
                const uint64_t DB = Stamps[B] & Mask;
                return (DB >= DA) ? static_cast<float>(static_cast<double>(DB - DA) * PeriodMs) : 0.0f;
            };
            LastViewportTiming.GpuSkyLutMs        = DeltaMs(0u, 1u);
            LastViewportTiming.GpuCloudsMs        = DeltaMs(2u, 3u);
            LastViewportTiming.GpuPrimaryTraceMs  = DeltaMs(4u, 5u);
            LastViewportTiming.GpuDenoiseMs       = DeltaMs(6u, 7u);
            LastViewportTiming.GpuReadbackCopyMs  = DeltaMs(8u, 9u);
            LastViewportTiming.GpuTotalPassMs     = DeltaMs(0u, 9u);
            LastViewportTiming.GpuTimestampsValid = true;
        }
    }

    BoundDevice->FreeCommandBuffer(Slot.CommandBuffer);
    Slot.CommandBuffer    = VK_NULL_HANDLE;
    Slot.TimelineSignal   = 0ull;
    Slot.HasGpuTimestamps = false;
}

void VulkanWorldRenderSequence::RetirePendingPrecompute(bool BlockIfBusy) noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready() || PendingPrecomputeCmd == VK_NULL_HANDLE)
    {
        return;
    }
    if (PendingPrecomputeSignal != 0ull && !BoundDevice->QueryTimelineReached(PendingPrecomputeSignal))
    {
        if (!BlockIfBusy)
        {
            return;
        }
        (void)BoundDevice->AwaitTimelineTick(PendingPrecomputeSignal, 60000000000ull);
    }
    BoundDevice->FreeCommandBuffer(PendingPrecomputeCmd);
    PendingPrecomputeCmd    = VK_NULL_HANDLE;
    PendingPrecomputeSignal = 0ull;
}

void VulkanWorldRenderSequence::RetirePendingTransform(bool BlockIfBusy) noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready() || PendingTransformCmd == VK_NULL_HANDLE)
    {
        return;
    }
    if (PendingTransformSignal != 0ull && !BoundDevice->QueryTimelineReached(PendingTransformSignal))
    {
        if (!BlockIfBusy)
        {
            return;
        }
        (void)BoundDevice->AwaitTimelineTick(PendingTransformSignal, 60000000000ull);
    }
    BoundDevice->FreeCommandBuffer(PendingTransformCmd);
    PendingTransformCmd    = VK_NULL_HANDLE;
    PendingTransformSignal = 0ull;
}

void VulkanWorldRenderSequence::FlushAllAsyncDispatches() noexcept
{
    for (uint32_t Idx = 0u; Idx < kLiveFrameSlots; ++Idx)
    {
        RetireLiveFrameSlot(Idx, true);
    }
    RetirePendingPrecompute(true);
    RetirePendingTransform(true);
}

void VulkanWorldRenderSequence::ReapRetiredDisplayImages(bool ForceDrainAll) noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready() || RetiredDisplayImages.empty())
    {
        return;
    }
    std::size_t WriteIdx = 0u;
    for (std::size_t ReadIdx = 0u; ReadIdx < RetiredDisplayImages.size(); ++ReadIdx)
    {
        RetiredDisplayImageEntry& Entry = RetiredDisplayImages[ReadIdx];
        const bool TimelineDone =
            (Entry.TimelineSignal == 0ull) ||
            BoundDevice->QueryTimelineReached(Entry.TimelineSignal);
        const bool GracePassed = (FrameSequenceIndex >= Entry.RetireAfterFrame);
        if (ForceDrainAll || (TimelineDone && GracePassed))
        {
            BoundDevice->ReleaseImage2D(Entry.Image);
        }
        else
        {
            if (WriteIdx != ReadIdx)
            {
                RetiredDisplayImages[WriteIdx] = Entry;
            }
            ++WriteIdx;
        }
    }
    RetiredDisplayImages.resize(WriteIdx);
}

void VulkanWorldRenderSequence::ReleaseVolume3DResources() noexcept
{
    if (BoundDevice != nullptr && BoundDevice->Ready())
    {
        VkDevice DeviceHandle = BoundDevice->AccessDevice();
        for (VkImageView& MipView : RadiosityCoarseMipViews)
        {
            if (MipView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(DeviceHandle, MipView, nullptr);
                MipView = VK_NULL_HANDLE;
            }
        }
        if (LinearRepeatSampler3D != VK_NULL_HANDLE)
        {
            vkDestroySampler(DeviceHandle, LinearRepeatSampler3D, nullptr);
            LinearRepeatSampler3D = VK_NULL_HANDLE;
        }
        if (LinearClampSampler3D != VK_NULL_HANDLE)
        {
            vkDestroySampler(DeviceHandle, LinearClampSampler3D, nullptr);
            LinearClampSampler3D = VK_NULL_HANDLE;
        }
        BoundDevice->ReleaseImage2D(GdfFineImage3D);
        BoundDevice->ReleaseImage2D(GdfCoarseImage3D);
        BoundDevice->ReleaseImage2D(RadiosityMip0Image3D);
        BoundDevice->ReleaseImage2D(RadiosityMip0ScratchImage3D);
        BoundDevice->ReleaseImage2D(RadiosityCoarseImage3D);
        BoundDevice->ReleaseImage2D(DummyFloat1x1x1Image3D);
        BoundDevice->ReleaseImage2D(DummyRgba1x1x1Image3D);
    }
    Volume3DResourcesInitialized = false;
}

bool VulkanWorldRenderSequence::EnsureVolume3DResources()
{
    if (Volume3DResourcesInitialized)
    {
        return true;
    }
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }

    const VkImageUsageFlags VolUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    GdfFineImage3D = BoundDevice->AllocateImage3D(64u, 64u, 64u, VK_FORMAT_R16_SFLOAT, VolUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1u);
    GdfCoarseImage3D = BoundDevice->AllocateImage3D(64u, 64u, 64u, VK_FORMAT_R16_SFLOAT, VolUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1u);
    RadiosityMip0Image3D = BoundDevice->AllocateImage3D(64u, 64u, 64u, VK_FORMAT_R16G16B16A16_SFLOAT, VolUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1u);
    RadiosityMip0ScratchImage3D = BoundDevice->AllocateImage3D(64u, 64u, 64u, VK_FORMAT_R16G16B16A16_SFLOAT, VolUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1u);
    RadiosityCoarseImage3D = BoundDevice->AllocateImage3D(32u, 32u, 32u, VK_FORMAT_R16G16B16A16_SFLOAT, VolUsage, VK_IMAGE_ASPECT_COLOR_BIT, 3u);
    DummyFloat1x1x1Image3D = BoundDevice->AllocateImage3D(1u, 1u, 1u, VK_FORMAT_R16_SFLOAT, VolUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1u);
    DummyRgba1x1x1Image3D = BoundDevice->AllocateImage3D(1u, 1u, 1u, VK_FORMAT_R16G16B16A16_SFLOAT, VolUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1u);

    if (GdfFineImage3D.ViewHandle == VK_NULL_HANDLE ||
        GdfCoarseImage3D.ViewHandle == VK_NULL_HANDLE ||
        RadiosityMip0Image3D.ViewHandle == VK_NULL_HANDLE ||
        RadiosityMip0ScratchImage3D.ViewHandle == VK_NULL_HANDLE ||
        RadiosityCoarseImage3D.ViewHandle == VK_NULL_HANDLE ||
        DummyFloat1x1x1Image3D.ViewHandle == VK_NULL_HANDLE ||
        DummyRgba1x1x1Image3D.ViewHandle == VK_NULL_HANDLE)
    {
        ReleaseVolume3DResources();
        return false;
    }

    for (uint32_t Mip = 0u; Mip < 3u; ++Mip)
    {
        RadiosityCoarseMipViews[Mip] = BoundDevice->CreateImageView3DMip(
            RadiosityCoarseImage3D.ImageHandle,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            Mip,
            1u);
        if (RadiosityCoarseMipViews[Mip] == VK_NULL_HANDLE)
        {
            ReleaseVolume3DResources();
            return false;
        }
    }

    VkDevice DeviceHandle = BoundDevice->AccessDevice();
    VkSamplerCreateInfo SampSpec {};
    SampSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SampSpec.magFilter    = VK_FILTER_LINEAR;
    SampSpec.minFilter    = VK_FILTER_LINEAR;
    SampSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    SampSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    SampSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    SampSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    SampSpec.minLod       = 0.0f;
    SampSpec.maxLod       = 0.0f;
    if (vkCreateSampler(DeviceHandle, &SampSpec, nullptr, &LinearRepeatSampler3D) != VK_SUCCESS)
    {
        ReleaseVolume3DResources();
        return false;
    }

    SampSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampSpec.maxLod       = 3.0f;
    if (vkCreateSampler(DeviceHandle, &SampSpec, nullptr, &LinearClampSampler3D) != VK_SUCCESS)
    {
        ReleaseVolume3DResources();
        return false;
    }

    // One-time initial layout transition + clear to safe defaults
    VkCommandBuffer InitCmd = BoundDevice->BeginSingleTimeCommands();
    if (InitCmd == VK_NULL_HANDLE)
    {
        ReleaseVolume3DResources();
        return false;
    }

    auto TransitionInit = [&](VkImage Img, uint32_t Mips, VkImageLayout OldL, VkImageLayout NewL,
                              VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage,
                              VkAccessFlags SrcAccess, VkAccessFlags DstAccess)
    {
        VkImageMemoryBarrier B {};
        B.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        B.oldLayout           = OldL;
        B.newLayout           = NewL;
        B.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.image               = Img;
        B.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, Mips, 0u, 1u };
        B.srcAccessMask       = SrcAccess;
        B.dstAccessMask       = DstAccess;
        vkCmdPipelineBarrier(InitCmd, SrcStage, DstStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &B);
    };

    const VkImageSubresourceRange Range1 { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    const VkImageSubresourceRange Range3 { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 3u, 0u, 1u };
    const VkClearColorValue ClearGdf   {{ 10000.0f, 10000.0f, 10000.0f, 10000.0f }};
    const VkClearColorValue ClearZero  {{ 0.0f, 0.0f, 0.0f, 0.0f }};

    TransitionInit(GdfFineImage3D.ImageHandle,              1u, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_ACCESS_TRANSFER_WRITE_BIT);
    TransitionInit(GdfCoarseImage3D.ImageHandle,            1u, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_ACCESS_TRANSFER_WRITE_BIT);
    TransitionInit(RadiosityMip0Image3D.ImageHandle,        1u, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_ACCESS_TRANSFER_WRITE_BIT);
    TransitionInit(RadiosityMip0ScratchImage3D.ImageHandle, 1u, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_ACCESS_TRANSFER_WRITE_BIT);
    TransitionInit(RadiosityCoarseImage3D.ImageHandle,      3u, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_ACCESS_TRANSFER_WRITE_BIT);
    TransitionInit(DummyFloat1x1x1Image3D.ImageHandle,      1u, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_ACCESS_TRANSFER_WRITE_BIT);
    TransitionInit(DummyRgba1x1x1Image3D.ImageHandle,       1u, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_ACCESS_TRANSFER_WRITE_BIT);

    vkCmdClearColorImage(InitCmd, GdfFineImage3D.ImageHandle,              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearGdf,  1u, &Range1);
    vkCmdClearColorImage(InitCmd, GdfCoarseImage3D.ImageHandle,            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearGdf,  1u, &Range1);
    vkCmdClearColorImage(InitCmd, RadiosityMip0Image3D.ImageHandle,        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearZero, 1u, &Range1);
    vkCmdClearColorImage(InitCmd, RadiosityMip0ScratchImage3D.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearZero, 1u, &Range1);
    vkCmdClearColorImage(InitCmd, RadiosityCoarseImage3D.ImageHandle,      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearZero, 1u, &Range3);
    vkCmdClearColorImage(InitCmd, DummyFloat1x1x1Image3D.ImageHandle,      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearGdf,  1u, &Range1);
    vkCmdClearColorImage(InitCmd, DummyRgba1x1x1Image3D.ImageHandle,       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearZero, 1u, &Range1);

    TransitionInit(GdfFineImage3D.ImageHandle,              1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionInit(GdfCoarseImage3D.ImageHandle,            1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionInit(RadiosityMip0Image3D.ImageHandle,        1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionInit(RadiosityMip0ScratchImage3D.ImageHandle, 1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    TransitionInit(RadiosityCoarseImage3D.ImageHandle,      3u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    TransitionInit(DummyFloat1x1x1Image3D.ImageHandle,      1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    TransitionInit(DummyRgba1x1x1Image3D.ImageHandle,       1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(InitCmd);
    (void)BoundDevice->AwaitTimelineTick(SyncPoint, 60000000000ull);
    BoundDevice->FreeCommandBuffer(InitCmd);

    Volume3DResourcesInitialized = true;
    return true;
}

void VulkanWorldRenderSequence::EnsureDisplayOutputImage(uint32_t WidthPx, uint32_t HeightPx)
{
    const uint32_t SafeW = std::max(1u, WidthPx);
    const uint32_t SafeH = std::max(1u, HeightPx);
    ReapRetiredDisplayImages(false);
    if (DisplayOutputImage2D.ImageHandle != VK_NULL_HANDLE &&
        DisplayOutputImage2D.WidthPx == SafeW &&
        DisplayOutputImage2D.HeightPx == SafeH)
    {
        return;
    }
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return;
    }
    // Defer releasing previous DisplayOutputImage2D until in-flight GPU work & ImGui finish (zero vkDeviceWaitIdle!)
    if (DisplayOutputImage2D.ImageHandle != VK_NULL_HANDLE)
    {
        RetiredDisplayImageEntry Retired {};
        Retired.Image            = DisplayOutputImage2D;
        Retired.TimelineSignal   = QueryLatestSubmittedTimelineSignal();
        Retired.RetireAfterFrame = FrameSequenceIndex + 4u;
        RetiredDisplayImages.push_back(Retired);
        DisplayOutputImage2D = {};
    }
    DisplayOutputImage2D = BoundDevice->AllocateImage2D(
        SafeW,
        SafeH,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanWorldRenderSequence::ReleasePrimaryVisFramebuffer() noexcept
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return;
    }
    VkDevice Dev = BoundDevice->AccessDevice();
    if (PrimaryVisFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(Dev, PrimaryVisFramebuffer, nullptr);
        PrimaryVisFramebuffer = VK_NULL_HANDLE;
    }
    BoundDevice->ReleaseImage2D(PrimaryVisImage2D);
    BoundDevice->ReleaseImage2D(PrimaryVisDepthImage2D);
}

void VulkanWorldRenderSequence::ReleasePrimaryVisResources() noexcept
{
    ReleasePrimaryVisFramebuffer();
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return;
    }
    VkDevice Dev = BoundDevice->AccessDevice();
    if (PrimaryVisPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Dev, PrimaryVisPipeline, nullptr);
        PrimaryVisPipeline = VK_NULL_HANDLE;
    }
    if (PrimaryVisPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Dev, PrimaryVisPipelineLayout, nullptr);
        PrimaryVisPipelineLayout = VK_NULL_HANDLE;
    }
    if (PrimaryVisRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(Dev, PrimaryVisRenderPass, nullptr);
        PrimaryVisRenderPass = VK_NULL_HANDLE;
    }
    if (PrimaryVisDescPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Dev, PrimaryVisDescPool, nullptr);
        PrimaryVisDescPool = VK_NULL_HANDLE;
    }
    PrimaryVisDescSets.fill(VK_NULL_HANDLE);
    if (PrimaryVisDescLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Dev, PrimaryVisDescLayout, nullptr);
        PrimaryVisDescLayout = VK_NULL_HANDLE;
    }
}

bool VulkanWorldRenderSequence::EnsurePrimaryVisTargets(uint32_t WidthPx, uint32_t HeightPx)
{
    const uint32_t SafeW = std::max(1u, WidthPx);
    const uint32_t SafeH = std::max(1u, HeightPx);
    if (PrimaryVisImage2D.ImageHandle != VK_NULL_HANDLE &&
        PrimaryVisImage2D.WidthPx == SafeW &&
        PrimaryVisImage2D.HeightPx == SafeH &&
        PrimaryVisFramebuffer != VK_NULL_HANDLE)
    {
        return true;
    }
    if (BoundDevice == nullptr || !BoundDevice->Ready() || PrimaryVisRenderPass == VK_NULL_HANDLE)
    {
        return false;
    }
    if (PrimaryVisImage2D.ImageHandle != VK_NULL_HANDLE)
    {
        FlushAllAsyncDispatches();
        ReleasePrimaryVisFramebuffer();
    }

    PrimaryVisImage2D = BoundDevice->AllocateImage2D(
        SafeW,
        SafeH,
        VK_FORMAT_R32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    PrimaryVisDepthImage2D = BoundDevice->AllocateImage2D(
        SafeW,
        SafeH,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    if (PrimaryVisImage2D.ViewHandle == VK_NULL_HANDLE || PrimaryVisDepthImage2D.ViewHandle == VK_NULL_HANDLE)
    {
        ReleasePrimaryVisFramebuffer();
        return false;
    }

    const std::array<VkImageView, 2u> Attachments {{
        PrimaryVisImage2D.ViewHandle,
        PrimaryVisDepthImage2D.ViewHandle,
    }};
    VkFramebufferCreateInfo FbInfo {};
    FbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbInfo.renderPass      = PrimaryVisRenderPass;
    FbInfo.attachmentCount = static_cast<uint32_t>(Attachments.size());
    FbInfo.pAttachments    = Attachments.data();
    FbInfo.width           = SafeW;
    FbInfo.height          = SafeH;
    FbInfo.layers          = 1u;
    if (vkCreateFramebuffer(BoundDevice->AccessDevice(), &FbInfo, nullptr, &PrimaryVisFramebuffer) != VK_SUCCESS)
    {
        ReleasePrimaryVisFramebuffer();
        return false;
    }

    // Transition freshly allocated PrimaryVisImage2D into VK_IMAGE_LAYOUT_GENERAL so descriptor slot 38 is valid immediately
    VkCommandBuffer InitCmd = BoundDevice->BeginSingleTimeCommands();
    if (InitCmd != VK_NULL_HANDLE)
    {
        VkImageMemoryBarrier Barrier {};
        Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        Barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image                           = PrimaryVisImage2D.ImageHandle;
        Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.baseMipLevel   = 0u;
        Barrier.subresourceRange.levelCount     = 1u;
        Barrier.subresourceRange.baseArrayLayer = 0u;
        Barrier.subresourceRange.layerCount     = 1u;
        Barrier.srcAccessMask                   = 0u;
        Barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(
            InitCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
        const uint64_t InitSig = BoundDevice->DispatchCommandBufferAsync(InitCmd);
        if (InitSig != 0ull)
        {
            (void)BoundDevice->AwaitTimelineTick(InitSig);
        }
        BoundDevice->FreeCommandBuffer(InitCmd);
    }
    return true;
}

bool VulkanWorldRenderSequence::InitializePrimaryVisRasterPipeline()
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }
    VkDevice Dev = BoundDevice->AccessDevice();

    const std::filesystem::path VertPath = CachedCompiledShaderDir / "WorldPrimaryVisRaster.vert.spv";
    const std::filesystem::path FragPath = CachedCompiledShaderDir / "WorldPrimaryVisRaster.frag.spv";
    VkShaderModule VertMod = BoundDevice->LoadShaderBinary(VertPath);
    VkShaderModule FragMod = BoundDevice->LoadShaderBinary(FragPath);
    if (VertMod == VK_NULL_HANDLE || FragMod == VK_NULL_HANDLE)
    {
        if (VertMod != VK_NULL_HANDLE) vkDestroyShaderModule(Dev, VertMod, nullptr);
        if (FragMod != VK_NULL_HANDLE) vkDestroyShaderModule(Dev, FragMod, nullptr);
        return false;
    }

    // 1. RenderPass: R32_UINT TriIdPlus1 color (UNDEFINED -> GENERAL) + D32_SFLOAT Reversed-Z depth
    VkAttachmentDescription ColorAtt {};
    ColorAtt.format         = VK_FORMAT_R32_UINT;
    ColorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    ColorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ColorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ColorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    ColorAtt.finalLayout    = VK_IMAGE_LAYOUT_GENERAL;

    VkAttachmentDescription DepthAtt {};
    DepthAtt.format         = VK_FORMAT_D32_SFLOAT;
    DepthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    DepthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    DepthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    DepthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    DepthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    DepthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ColorRef { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference DepthRef { 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription Subpass {};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = 1u;
    Subpass.pColorAttachments       = &ColorRef;
    Subpass.pDepthStencilAttachment = &DepthRef;

    std::array<VkSubpassDependency, 2u> Deps {};
    Deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    Deps[0].dstSubpass    = 0u;
    Deps[0].srcStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    Deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    Deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    Deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    Deps[1].srcSubpass    = 0u;
    Deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    Deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    Deps[1].dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    Deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    Deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    const std::array<VkAttachmentDescription, 2u> Atts {{ ColorAtt, DepthAtt }};
    VkRenderPassCreateInfo RpInfo {};
    RpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    RpInfo.attachmentCount = static_cast<uint32_t>(Atts.size());
    RpInfo.pAttachments    = Atts.data();
    RpInfo.subpassCount    = 1u;
    RpInfo.pSubpasses      = &Subpass;
    RpInfo.dependencyCount = static_cast<uint32_t>(Deps.size());
    RpInfo.pDependencies   = Deps.data();

    if (vkCreateRenderPass(Dev, &RpInfo, nullptr, &PrimaryVisRenderPass) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Dev, VertMod, nullptr);
        vkDestroyShaderModule(Dev, FragMod, nullptr);
        return false;
    }

    // 2. Descriptor set layout & per-slot ring descriptor sets (bindings 0: SceneTriangles, 1: ActiveClusterBitmask)
    std::array<VkDescriptorSetLayoutBinding, 2u> Bindings {};
    Bindings[0].binding         = 0u;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[0].descriptorCount = 1u;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    Bindings[1].binding         = 1u;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1u;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo DslInfo {};
    DslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    DslInfo.bindingCount = static_cast<uint32_t>(Bindings.size());
    DslInfo.pBindings    = Bindings.data();
    if (vkCreateDescriptorSetLayout(Dev, &DslInfo, nullptr, &PrimaryVisDescLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Dev, VertMod, nullptr);
        vkDestroyShaderModule(Dev, FragMod, nullptr);
        ReleasePrimaryVisResources();
        return false;
    }

    VkDescriptorPoolSize PoolSize { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kLiveFrameSlots * 2u };
    VkDescriptorPoolCreateInfo PoolInfo {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = kLiveFrameSlots;
    PoolInfo.poolSizeCount = 1u;
    PoolInfo.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Dev, &PoolInfo, nullptr, &PrimaryVisDescPool) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Dev, VertMod, nullptr);
        vkDestroyShaderModule(Dev, FragMod, nullptr);
        ReleasePrimaryVisResources();
        return false;
    }

    std::array<VkDescriptorSetLayout, kLiveFrameSlots> Layouts {};
    Layouts.fill(PrimaryVisDescLayout);
    VkDescriptorSetAllocateInfo AllocInfo {};
    AllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocInfo.descriptorPool     = PrimaryVisDescPool;
    AllocInfo.descriptorSetCount = kLiveFrameSlots;
    AllocInfo.pSetLayouts        = Layouts.data();
    if (vkAllocateDescriptorSets(Dev, &AllocInfo, PrimaryVisDescSets.data()) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Dev, VertMod, nullptr);
        vkDestroyShaderModule(Dev, FragMod, nullptr);
        ReleasePrimaryVisResources();
        return false;
    }

    // 3. Pipeline layout with 80-byte VisRasterPushConstants
    VkPushConstantRange PushRange {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = 80u;

    VkPipelineLayoutCreateInfo PlInfo {};
    PlInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PlInfo.setLayoutCount         = 1u;
    PlInfo.pSetLayouts            = &PrimaryVisDescLayout;
    PlInfo.pushConstantRangeCount = 1u;
    PlInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Dev, &PlInfo, nullptr, &PrimaryVisPipelineLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Dev, VertMod, nullptr);
        vkDestroyShaderModule(Dev, FragMod, nullptr);
        ReleasePrimaryVisResources();
        return false;
    }

    // 4. Graphics pipeline: vertex-pulling (no vertex buffers), two-sided (CULL_MODE_NONE), Reversed-Z (COMPARE_OP_GREATER)
    std::array<VkPipelineShaderStageCreateInfo, 2u> Stages {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = VertMod;
    Stages[0].pName  = "PrimaryVisVertexEntry";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = FragMod;
    Stages[1].pName  = "PrimaryVisFragmentEntry";

    VkPipelineVertexInputStateCreateInfo ViState {};
    ViState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo IaState {};
    IaState.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    IaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo VpState {};
    VpState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    VpState.viewportCount = 1u;
    VpState.scissorCount  = 1u;

    const std::array<VkDynamicState, 2u> DynStates {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
    VkPipelineDynamicStateCreateInfo DynState {};
    DynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynState.dynamicStateCount = static_cast<uint32_t>(DynStates.size());
    DynState.pDynamicStates    = DynStates.data();

    VkPipelineRasterizationStateCreateInfo RastState {};
    RastState.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RastState.polygonMode = VK_POLYGON_MODE_FILL;
    RastState.cullMode    = VK_CULL_MODE_NONE;
    RastState.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RastState.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo MsState {};
    MsState.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MsState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DsState {};
    DsState.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DsState.depthTestEnable  = VK_TRUE;
    DsState.depthWriteEnable = VK_TRUE;
    DsState.depthCompareOp   = VK_COMPARE_OP_GREATER; // Reversed-Z [1 at ZNear, 0 at ZFar]

    VkPipelineColorBlendAttachmentState BlendAtt {};
    BlendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo BlendState {};
    BlendState.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    BlendState.attachmentCount = 1u;
    BlendState.pAttachments    = &BlendAtt;

    VkGraphicsPipelineCreateInfo PipeInfo {};
    PipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeInfo.stageCount          = static_cast<uint32_t>(Stages.size());
    PipeInfo.pStages             = Stages.data();
    PipeInfo.pVertexInputState   = &ViState;
    PipeInfo.pInputAssemblyState = &IaState;
    PipeInfo.pViewportState      = &VpState;
    PipeInfo.pRasterizationState = &RastState;
    PipeInfo.pMultisampleState   = &MsState;
    PipeInfo.pDepthStencilState  = &DsState;
    PipeInfo.pColorBlendState    = &BlendState;
    PipeInfo.pDynamicState       = &DynState;
    PipeInfo.layout              = PrimaryVisPipelineLayout;
    PipeInfo.renderPass          = PrimaryVisRenderPass;
    PipeInfo.subpass             = 0u;

    const VkResult PipeRes = vkCreateGraphicsPipelines(Dev, VK_NULL_HANDLE, 1u, &PipeInfo, nullptr, &PrimaryVisPipeline);
    vkDestroyShaderModule(Dev, VertMod, nullptr);
    vkDestroyShaderModule(Dev, FragMod, nullptr);
    if (PipeRes != VK_SUCCESS)
    {
        ReleasePrimaryVisResources();
        return false;
    }

    // Allocate initial 1x1 dummy target in VK_IMAGE_LAYOUT_GENERAL so descriptor slot 38 is always valid
    return EnsurePrimaryVisTargets(1u, 1u);
}

void VulkanWorldRenderSequence::RecordPrimaryVisRasterPass(
    VkCommandBuffer Cmd,
    uint32_t        SlotIdx,
    uint32_t        WidthPx,
    uint32_t        HeightPx,
    const float     CamOriginTanX[4],
    const float     CamRightTanY[4],
    const float     CamUpNearZ[4],
    const float     CamForwardFarZ[4])
{
    if (PrimaryVisPipeline == VK_NULL_HANDLE || PrimaryVisFramebuffer == VK_NULL_HANDLE || UploadedTriangleCount == 0u)
    {
        return;
    }
    VkDescriptorSet DescSet = PrimaryVisDescSets[SlotIdx % kLiveFrameSlots];

    VkDescriptorBufferInfo TriInfo {};
    TriInfo.buffer = ShadedTriangleBuffer.BufferHandle;
    TriInfo.offset = 0ull;
    TriInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo MaskInfo {};
    MaskInfo.buffer = ActiveClusterMaskBuffer.BufferHandle;
    MaskInfo.offset = MaskSliceOffsetBytes;
    MaskInfo.range  = MaskSliceOffsetBytes;

    std::array<VkWriteDescriptorSet, 2u> Writes {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = DescSet;
    Writes[0].dstBinding      = 0u;
    Writes[0].descriptorCount = 1u;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[0].pBufferInfo     = &TriInfo;

    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = DescSet;
    Writes[1].dstBinding      = 1u;
    Writes[1].descriptorCount = 1u;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[1].pBufferInfo     = &MaskInfo;

    vkUpdateDescriptorSets(BoundDevice->AccessDevice(), static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    std::array<VkClearValue, 2u> Clears {};
    Clears[0].color.uint32[0] = 0u;
    Clears[0].color.uint32[1] = 0u;
    Clears[0].color.uint32[2] = 0u;
    Clears[0].color.uint32[3] = 0u;
    Clears[1].depthStencil    = { 0.0f, 0u }; // Reversed-Z far plane clear

    VkRenderPassBeginInfo RpBegin {};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = PrimaryVisRenderPass;
    RpBegin.framebuffer       = PrimaryVisFramebuffer;
    RpBegin.renderArea.offset = { 0, 0 };
    RpBegin.renderArea.extent = { WidthPx, HeightPx };
    RpBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    RpBegin.pClearValues      = Clears.data();

    vkCmdBeginRenderPass(Cmd, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PrimaryVisPipeline);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PrimaryVisPipelineLayout, 0u, 1u, &DescSet, 0u, nullptr);

    VkViewport Vp {};
    Vp.x        = 0.0f;
    Vp.y        = 0.0f;
    Vp.width    = static_cast<float>(WidthPx);
    Vp.height   = static_cast<float>(HeightPx);
    Vp.minDepth = 0.0f;
    Vp.maxDepth = 1.0f;
    vkCmdSetViewport(Cmd, 0u, 1u, &Vp);

    VkRect2D Scissor { { 0, 0 }, { WidthPx, HeightPx } };
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);

    struct alignas(16) VisPushConstants80
    {
        float    CamOriginTanX[4];
        float    CamRightTanY[4];
        float    CamUpNearZ[4];
        float    CamForwardFarZ[4];
        uint32_t TriangleMeta[4];
    };
    static_assert(sizeof(VisPushConstants80) == 80u);
    VisPushConstants80 Push {};
    std::memcpy(Push.CamOriginTanX,  CamOriginTanX,  sizeof(Push.CamOriginTanX));
    std::memcpy(Push.CamRightTanY,   CamRightTanY,   sizeof(Push.CamRightTanY));
    std::memcpy(Push.CamUpNearZ,     CamUpNearZ,     sizeof(Push.CamUpNearZ));
    std::memcpy(Push.CamForwardFarZ, CamForwardFarZ, sizeof(Push.CamForwardFarZ));
    Push.TriangleMeta[0] = UploadedTriangleCount;
    Push.TriangleMeta[1] = UploadedClusterSlotCount;
    Push.TriangleMeta[2] = 0u;
    Push.TriangleMeta[3] = 0u;

    vkCmdPushConstants(Cmd, PrimaryVisPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0u, sizeof(Push), &Push);
    vkCmdDraw(Cmd, UploadedTriangleCount * 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(Cmd);
}

bool VulkanWorldRenderSequence::UploadToDeviceBuffer(
    DeviceBufferResource& TargetBuffer,
    const void*           SourceData,
    uint64_t              ByteCount,
    VkBufferUsageFlags    UsageMask)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }
    const uint64_t SafeBytes = std::max<uint64_t>(4ull, ByteCount);
    EnsureDeviceBufferCapacity(TargetBuffer, SafeBytes, UsageMask);
    if (TargetBuffer.BufferHandle == VK_NULL_HANDLE || SourceData == nullptr)
    {
        return false;
    }
    if (TargetBuffer.HostAddress != nullptr)
    {
        std::memcpy(TargetBuffer.HostAddress, SourceData, static_cast<std::size_t>(ByteCount));
        return true;
    }
    if (UploadStagingBuffer.BufferHandle == VK_NULL_HANDLE || UploadStagingBuffer.ByteCapacity < SafeBytes)
    {
        BoundDevice->ReleaseBuffer(UploadStagingBuffer);
        const uint64_t PaddedBytes = std::max<uint64_t>(SafeBytes, (SafeBytes * 5ull) / 4ull);
        UploadStagingBuffer        = DispatchEncoder.AllocateUploadStagingBuffer(PaddedBytes);
    }
    if (UploadStagingBuffer.HostAddress == nullptr)
    {
        return false;
    }
    std::memcpy(UploadStagingBuffer.HostAddress, SourceData, static_cast<std::size_t>(ByteCount));
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }
    VkBufferCopy CopyRegion {};
    CopyRegion.size = ByteCount;
    vkCmdCopyBuffer(Cmd, UploadStagingBuffer.BufferHandle, TargetBuffer.BufferHandle, 1u, &CopyRegion);
    VkMemoryBarrier CopyBarrier {};
    CopyBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    CopyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    CopyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0u,
        1u,
        &CopyBarrier,
        0u,
        nullptr,
        0u,
        nullptr);
    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    const bool Ok = BoundDevice->AwaitTimelineTick(SyncPoint);
    BoundDevice->FreeCommandBuffer(Cmd);
    return Ok;
}

bool VulkanWorldRenderSequence::UploadDualDeviceBuffers(
    DeviceBufferResource& TargetBufferA,
    const void*           SourceDataA,
    uint64_t              ByteCountA,
    VkBufferUsageFlags    UsageMaskA,
    DeviceBufferResource& TargetBufferB,
    const void*           SourceDataB,
    uint64_t              ByteCountB,
    VkBufferUsageFlags    UsageMaskB)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }
    const uint64_t SafeBytesA = std::max<uint64_t>(4ull, ByteCountA);
    const uint64_t SafeBytesB = std::max<uint64_t>(4ull, ByteCountB);
    EnsureDeviceBufferCapacity(TargetBufferA, SafeBytesA, UsageMaskA);
    EnsureDeviceBufferCapacity(TargetBufferB, SafeBytesB, UsageMaskB);
    if (TargetBufferA.BufferHandle == VK_NULL_HANDLE || SourceDataA == nullptr ||
        TargetBufferB.BufferHandle == VK_NULL_HANDLE || SourceDataB == nullptr)
    {
        return false;
    }
    if (TargetBufferA.HostAddress != nullptr && TargetBufferB.HostAddress != nullptr)
    {
        std::memcpy(TargetBufferA.HostAddress, SourceDataA, static_cast<std::size_t>(ByteCountA));
        std::memcpy(TargetBufferB.HostAddress, SourceDataB, static_cast<std::size_t>(ByteCountB));
        return true;
    }

    const uint64_t OffsetB    = (SafeBytesA + 255ull) & ~255ull;
    const uint64_t TotalBytes = OffsetB + SafeBytesB;
    if (UploadStagingBuffer.BufferHandle == VK_NULL_HANDLE || UploadStagingBuffer.ByteCapacity < TotalBytes)
    {
        BoundDevice->ReleaseBuffer(UploadStagingBuffer);
        const uint64_t PaddedBytes = std::max<uint64_t>(TotalBytes, (TotalBytes * 5ull) / 4ull);
        UploadStagingBuffer        = DispatchEncoder.AllocateUploadStagingBuffer(PaddedBytes);
    }
    if (UploadStagingBuffer.HostAddress == nullptr)
    {
        return false;
    }

    uint8_t* StagingBytes = static_cast<uint8_t*>(UploadStagingBuffer.HostAddress);
    std::memcpy(StagingBytes, SourceDataA, static_cast<std::size_t>(ByteCountA));
    std::memcpy(StagingBytes + OffsetB, SourceDataB, static_cast<std::size_t>(ByteCountB));

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }
    VkBufferCopy CopyA {};
    CopyA.srcOffset = 0ull;
    CopyA.dstOffset = 0ull;
    CopyA.size      = ByteCountA;
    vkCmdCopyBuffer(Cmd, UploadStagingBuffer.BufferHandle, TargetBufferA.BufferHandle, 1u, &CopyA);

    VkBufferCopy CopyB {};
    CopyB.srcOffset = OffsetB;
    CopyB.dstOffset = 0ull;
    CopyB.size      = ByteCountB;
    vkCmdCopyBuffer(Cmd, UploadStagingBuffer.BufferHandle, TargetBufferB.BufferHandle, 1u, &CopyB);

    VkMemoryBarrier CopyBarrier {};
    CopyBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    CopyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    CopyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0u,
        1u,
        &CopyBarrier,
        0u,
        nullptr,
        0u,
        nullptr);
    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    const bool Ok = BoundDevice->AwaitTimelineTick(SyncPoint);
    BoundDevice->FreeCommandBuffer(Cmd);
    return Ok;
}

[[nodiscard]] float PackSafeBits30ToFloat(uint32_t Word30) noexcept
{
    const uint32_t SafeWord = Word30 & 0x3FFFFFFFu;
    float          OutFloat = 0.0f;
    std::memcpy(&OutFloat, &SafeWord, sizeof(float));
    return OutFloat;
}

[[nodiscard]] uint32_t UnpackSafeBits30FromFloat(float InFloat) noexcept
{
    uint32_t Word = 0u;
    std::memcpy(&Word, &InFloat, sizeof(uint32_t));
    return Word & 0x3FFFFFFFu;
}

[[nodiscard]] float PackNaniteTriangleMetaWord(
    uint32_t LodLevel,
    uint32_t MaxLodLevel,
    uint32_t SeamEdgeMask,
    uint32_t GroupOrdinal) noexcept
{
    const uint32_t Word =
        (LodLevel & 0xFu) |
        ((MaxLodLevel & 0xFu) << 4u) |
        ((SeamEdgeMask & 0x7u) << 8u) |
        ((GroupOrdinal & 0x7FFFFu) << 11u);
    return PackSafeBits30ToFloat(Word);
}

void PackNaniteTriangleUvsToFloats(
    Mathematics::UVAxes Uv0,
    Mathematics::UVAxes Uv1,
    Mathematics::UVAxes Uv2,
    float&              OutWordA,
    float&              OutWordB) noexcept
{
    auto QuantizeUv10 = [](float Coord) noexcept -> uint32_t
    {
        const float   Wrapped = Coord - std::floor(Coord * 0.25f) * 4.0f;
        const int32_t Quant   = static_cast<int32_t>(std::lround(std::clamp(Wrapped * 0.25f, 0.0f, 1.0f) * 1023.0f));
        return static_cast<uint32_t>(std::clamp(Quant, 0, 1023));
    };
    const uint32_t U0 = QuantizeUv10(Uv0.U);
    const uint32_t V0 = QuantizeUv10(Uv0.V);
    const uint32_t U1 = QuantizeUv10(Uv1.U);
    const uint32_t V1 = QuantizeUv10(Uv1.V);
    const uint32_t U2 = QuantizeUv10(Uv2.U);
    const uint32_t V2 = QuantizeUv10(Uv2.V);
    OutWordA = PackSafeBits30ToFloat(U0 | (V0 << 10u) | (U1 << 20u));
    OutWordB = PackSafeBits30ToFloat(V1 | (U2 << 10u) | (V2 << 20u));
}

const VulkanWorldRenderSequence::CachedAssetClusterHierarchy&
VulkanWorldRenderSequence::AcquireAssetClusterHierarchy(
    const SpatialAcceleration::ClusterPyramid* PyramidAsset)
{
    for (const auto& Cached : AssetClusterTreeCache)
    {
        if (Cached.PyramidAsset == PyramidAsset &&
            Cached.SourceClusterCount == PyramidAsset->Clusters.size() &&
            Cached.SourceTriangleCount == PyramidAsset->Triangles.size())
        {
            return Cached;
        }
    }

    CachedAssetClusterHierarchy Built {};
    Built.PyramidAsset        = PyramidAsset;
    Built.SourceClusterCount  = PyramidAsset->Clusters.size();
    Built.SourceTriangleCount = PyramidAsset->Triangles.size();

    uint32_t LevelCount = std::clamp<uint32_t>(std::max<uint32_t>(1u, PyramidAsset->HierarchyDepth), 1u, 8u);
    for (const auto& Cluster : PyramidAsset->Clusters)
    {
        const uint32_t ClampedLevel = std::min(7u, static_cast<uint32_t>(Cluster.HierarchyLevel));
        LevelCount = std::max(LevelCount, ClampedLevel + 1u);
    }
    Built.LevelSubtrees.resize(LevelCount);

    const uint32_t MaxLevelIndex = (LevelCount > 0u) ? (LevelCount - 1u) : 0u;
    struct LocalTriangleNaniteAux
    {
        uint32_t GroupOrdinal    = 0u;
        uint32_t SeamEdgeMask    = 0u;
        float    SelfErrorMeters = 0.0f;
        float    PackedUvWordA   = 0.0f;
        float    PackedUvWordB   = 0.0f;
    };

    std::vector<SpatialAcceleration::RayTrianglePrimitive>  LocalPrims {};
    std::vector<SpatialAcceleration::TriangleSmoothNormals> LocalNormals {};
    std::vector<LocalTriangleNaniteAux>                     LocalAux {};

    for (uint32_t Level = 0u; Level < LevelCount; ++Level)
    {
        auto& Subtree          = Built.LevelSubtrees[Level];
        Subtree.HierarchyLevel = Level;

        LocalPrims.clear();
        LocalNormals.clear();
        LocalAux.clear();
        Mathematics::Axis3D BMin {  1.0e30f,  1.0e30f,  1.0e30f };
        Mathematics::Axis3D BMax { -1.0e30f, -1.0e30f, -1.0e30f };

        struct LevelEdgeEntry
        {
            uint32_t            UseCount     = 0u;
            uint16_t            FirstMat     = 0u;
            Mathematics::UVAxes FirstUvLo    {};
            Mathematics::UVAxes FirstUvHi    {};
            bool                SeamDetected = false;
        };
        std::unordered_map<uint64_t, LevelEdgeEntry> LevelEdges;
        auto EncodeEdgeKey = [](uint32_t A, uint32_t B) noexcept -> uint64_t
        {
            const uint32_t Lo = std::min(A, B);
            const uint32_t Hi = std::max(A, B);
            return (static_cast<uint64_t>(Lo) << 32u) | static_cast<uint64_t>(Hi);
        };

        for (std::size_t ClusterIdx = 0u; ClusterIdx < PyramidAsset->Clusters.size(); ++ClusterIdx)
        {
            const auto&    Cluster      = PyramidAsset->Clusters[ClusterIdx];
            const uint32_t ClampedLevel = std::min(7u, static_cast<uint32_t>(Cluster.HierarchyLevel));
            if (ClampedLevel != Level)
            {
                continue;
            }
            for (uint32_t TriOff = 0u; TriOff < Cluster.TriangleCount; ++TriOff)
            {
                const uint32_t TriGlobalIdx = Cluster.TriangleStartOffset + TriOff;
                if (TriGlobalIdx >= PyramidAsset->Triangles.size())
                {
                    break;
                }
                const auto& Tri = PyramidAsset->Triangles[TriGlobalIdx];
                const uint32_t V[3] = { Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 };
                const Mathematics::UVAxes Uv[3] = { Tri.CornerUV0, Tri.CornerUV1, Tri.CornerUV2 };
                for (uint32_t K = 0u; K < 3u; ++K)
                {
                    const uint32_t NextK = (K + 1u) % 3u;
                    const bool     Swap  = (V[K] > V[NextK]);
                    const Mathematics::UVAxes UvLo = Swap ? Uv[NextK] : Uv[K];
                    const Mathematics::UVAxes UvHi = Swap ? Uv[K]     : Uv[NextK];
                    auto [It, Inserted] = LevelEdges.try_emplace(EncodeEdgeKey(V[K], V[NextK]));
                    LevelEdgeEntry& EdgeEntry = It->second;
                    ++EdgeEntry.UseCount;
                    if (Inserted)
                    {
                        EdgeEntry.FirstMat  = Tri.MaterialOrdinal;
                        EdgeEntry.FirstUvLo = UvLo;
                        EdgeEntry.FirstUvHi = UvHi;
                    }
                    else
                    {
                        const float JumpLo = std::fabs(EdgeEntry.FirstUvLo.U - UvLo.U) + std::fabs(EdgeEntry.FirstUvLo.V - UvLo.V);
                        const float JumpHi = std::fabs(EdgeEntry.FirstUvHi.U - UvHi.U) + std::fabs(EdgeEntry.FirstUvHi.V - UvHi.V);
                        if (EdgeEntry.FirstMat != Tri.MaterialOrdinal || std::max(JumpLo, JumpHi) > 2.0e-3f)
                        {
                            EdgeEntry.SeamDetected = true;
                        }
                    }
                }
            }
        }

        for (std::size_t ClusterIdx = 0u; ClusterIdx < PyramidAsset->Clusters.size(); ++ClusterIdx)
        {
            const auto&    Cluster      = PyramidAsset->Clusters[ClusterIdx];
            const uint32_t ClampedLevel = std::min(7u, static_cast<uint32_t>(Cluster.HierarchyLevel));
            if (ClampedLevel != Level)
            {
                continue;
            }

            for (uint32_t TriOff = 0u; TriOff < Cluster.TriangleCount; ++TriOff)
            {
                const uint32_t TriGlobalIdx = Cluster.TriangleStartOffset + TriOff;
                if (TriGlobalIdx >= PyramidAsset->Triangles.size())
                {
                    break;
                }
                const auto& Tri = PyramidAsset->Triangles[TriGlobalIdx];
                if (Tri.VertexIndex0 >= PyramidAsset->Positions.size() ||
                    Tri.VertexIndex1 >= PyramidAsset->Positions.size() ||
                    Tri.VertexIndex2 >= PyramidAsset->Positions.size())
                {
                    continue;
                }
                const Mathematics::Axis3D P0 = PyramidAsset->Positions[Tri.VertexIndex0];
                const Mathematics::Axis3D P1 = PyramidAsset->Positions[Tri.VertexIndex1];
                const Mathematics::Axis3D P2 = PyramidAsset->Positions[Tri.VertexIndex2];

                BMin.X = std::min({ BMin.X, P0.X, P1.X, P2.X });
                BMin.Y = std::min({ BMin.Y, P0.Y, P1.Y, P2.Y });
                BMin.Z = std::min({ BMin.Z, P0.Z, P1.Z, P2.Z });
                BMax.X = std::max({ BMax.X, P0.X, P1.X, P2.X });
                BMax.Y = std::max({ BMax.Y, P0.Y, P1.Y, P2.Y });
                BMax.Z = std::max({ BMax.Z, P0.Z, P1.Z, P2.Z });

                uint32_t SeamMask = 0u;
                const uint32_t V[3] = { Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 };
                for (uint32_t K = 0u; K < 3u; ++K)
                {
                    const auto EdgeIt = LevelEdges.find(EncodeEdgeKey(V[K], V[(K + 1u) % 3u]));
                    if (EdgeIt != LevelEdges.end() && (EdgeIt->second.UseCount == 1u || EdgeIt->second.SeamDetected))
                    {
                        SeamMask |= (1u << K);
                    }
                }

                LocalTriangleNaniteAux Aux {};
                Aux.GroupOrdinal    = Cluster.EnclosingGroupOrdinal;
                Aux.SeamEdgeMask    = SeamMask;
                Aux.SelfErrorMeters = std::max(0.0f, Cluster.FinerSelfErrorMeters);
                PackNaniteTriangleUvsToFloats(Tri.CornerUV0, Tri.CornerUV1, Tri.CornerUV2, Aux.PackedUvWordA, Aux.PackedUvWordB);

                SpatialAcceleration::RayTrianglePrimitive Prim {};
                Prim.Corner0          = P0;
                Prim.Edge01           = P1 - P0;
                Prim.Edge02           = P2 - P0;
                Prim.SourceFaceIndex  = static_cast<uint32_t>(LocalAux.size());
                Prim.MaterialOrdinal  = Tri.MaterialOrdinal;
                Prim.OpaqueMask       = 1u;
                Prim.PrimitiveOrdinal = static_cast<uint32_t>(ClusterIdx);
                LocalPrims.push_back(Prim);
                LocalAux.push_back(Aux);

                SpatialAcceleration::TriangleSmoothNormals Smooth {};
                Smooth.Normal0 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal0);
                Smooth.Normal1 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal1);
                Smooth.Normal2 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal2);
                LocalNormals.push_back(Smooth);
            }
        }

        if (LocalPrims.empty())
        {
            continue;
        }

        Subtree.LocalBoundsMin = { BMin.X - 1.0e-5f, BMin.Y - 1.0e-5f, BMin.Z - 1.0e-5f };
        Subtree.LocalBoundsMax = { BMax.X + 1.0e-5f, BMax.Y + 1.0e-5f, BMax.Z + 1.0e-5f };
        if (!Subtree.LocalBvh.BuildFromTrianglePrimitivesWithNormals(LocalPrims, LocalNormals) ||
            Subtree.LocalBvh.WideEntries.empty())
        {
            continue;
        }

        const std::size_t TriCount   = Subtree.LocalBvh.Primitives.size();
        const bool        HasNormals = (Subtree.LocalBvh.PrimitiveNormals.size() == TriCount);
        Subtree.TriangleCount = static_cast<uint32_t>(TriCount);
        Subtree.TriLocalClusterIndices.resize(TriCount);
        Subtree.LocalShadedTriBytes.resize(TriCount * sizeof(GpuShadedTriangle96));
        auto* OutTris = reinterpret_cast<GpuShadedTriangle96*>(Subtree.LocalShadedTriBytes.data());

        for (std::size_t Idx = 0u; Idx < TriCount; ++Idx)
        {
            const auto& Prim = Subtree.LocalBvh.Primitives[Idx];
            Subtree.TriLocalClusterIndices[Idx] = Prim.PrimitiveOrdinal;
            const LocalTriangleNaniteAux Aux = (Prim.SourceFaceIndex < LocalAux.size())
                ? LocalAux[Prim.SourceFaceIndex]
                : LocalTriangleNaniteAux{};

            const Mathematics::Axis3D N0 = HasNormals
                ? Subtree.LocalBvh.PrimitiveNormals[Idx].Normal0
                : Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02), { 0.0f, 0.0f, 1.0f });
            const Mathematics::Axis3D N1 = HasNormals ? Subtree.LocalBvh.PrimitiveNormals[Idx].Normal1 : N0;
            const Mathematics::Axis3D N2 = HasNormals ? Subtree.LocalBvh.PrimitiveNormals[Idx].Normal2 : N0;

            GpuShadedTriangle96 OutTri {};
            OutTri.Corner0Pad[0]  = Prim.Corner0.X;
            OutTri.Corner0Pad[1]  = Prim.Corner0.Y;
            OutTri.Corner0Pad[2]  = Prim.Corner0.Z;
            OutTri.Corner0Pad[3]  = PackNaniteTriangleMetaWord(Level, MaxLevelIndex, Aux.SeamEdgeMask, Aux.GroupOrdinal);
            OutTri.Edge01Pad[0]   = Prim.Edge01.X;
            OutTri.Edge01Pad[1]   = Prim.Edge01.Y;
            OutTri.Edge01Pad[2]   = Prim.Edge01.Z;
            OutTri.Edge01Pad[3]   = static_cast<float>(Prim.PrimitiveOrdinal);
            OutTri.Edge02Pad[0]   = Prim.Edge02.X;
            OutTri.Edge02Pad[1]   = Prim.Edge02.Y;
            OutTri.Edge02Pad[2]   = Prim.Edge02.Z;
            OutTri.Edge02Pad[3]   = Aux.SelfErrorMeters;
            OutTri.Normal0Pad[0]  = N0.X;
            OutTri.Normal0Pad[1]  = N0.Y;
            OutTri.Normal0Pad[2]  = N0.Z;
            OutTri.Normal0Pad[3]  = Aux.PackedUvWordA;
            OutTri.Normal1Pad[0]  = N1.X;
            OutTri.Normal1Pad[1]  = N1.Y;
            OutTri.Normal1Pad[2]  = N1.Z;
            OutTri.Normal1Pad[3]  = Aux.PackedUvWordB;
            OutTri.Normal2Slot[0] = N2.X;
            OutTri.Normal2Slot[1] = N2.Y;
            OutTri.Normal2Slot[2] = N2.Z;
            OutTri.Normal2Slot[3] = static_cast<float>(Prim.MaterialOrdinal);
            OutTris[Idx]          = OutTri;
        }

        // Sub-split any mixed-cluster leaf (2..4 tris straddling a cluster boundary) into 1-tri lanes
        // inside a child WideTreeEntry so 100% of leaves are cluster-pure with zero per-triangle bit checks.
        const std::size_t OrigNodeCount = Subtree.LocalBvh.WideEntries.size();
        for (std::size_t NodeIdx = 0u; NodeIdx < OrigNodeCount; ++NodeIdx)
        {
            for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
            {
                const uint32_t Ref = Subtree.LocalBvh.WideEntries[NodeIdx].BranchReference[Lane];
                const uint32_t Cnt = Subtree.LocalBvh.WideEntries[NodeIdx].BranchPrimitiveCount[Lane];
                if (Ref == SpatialAcceleration::EmptyBranchReference || Cnt <= 1u || Cnt > 4u)
                {
                    continue;
                }
                const uint32_t Start = Ref & 0x7FFFFFFFu;
                if (Start + Cnt > TriCount)
                {
                    continue;
                }
                const uint32_t FirstClust = Subtree.TriLocalClusterIndices[Start];
                bool MixedLeaf = false;
                for (uint32_t P = 1u; P < Cnt; ++P)
                {
                    if (Subtree.TriLocalClusterIndices[Start + P] != FirstClust)
                    {
                        MixedLeaf = true;
                        break;
                    }
                }
                if (!MixedLeaf)
                {
                    continue;
                }

                SpatialAcceleration::WideTreeEntry SplitChild {};
                for (uint32_t SubLane = 0u; SubLane < 4u; ++SubLane)
                {
                    if (SubLane < Cnt)
                    {
                        const uint32_t TriIdx = Start + SubLane;
                        const auto&    Prim   = Subtree.LocalBvh.Primitives[TriIdx];
                        const Mathematics::Axis3D P0 = Prim.Corner0;
                        const Mathematics::Axis3D P1 = P0 + Prim.Edge01;
                        const Mathematics::Axis3D P2 = P0 + Prim.Edge02;
                        SplitChild.BranchBoundsMinX[SubLane]     = std::min({ P0.X, P1.X, P2.X }) - 1.0e-6f;
                        SplitChild.BranchBoundsMaxX[SubLane]     = std::max({ P0.X, P1.X, P2.X }) + 1.0e-6f;
                        SplitChild.BranchBoundsMinY[SubLane]     = std::min({ P0.Y, P1.Y, P2.Y }) - 1.0e-6f;
                        SplitChild.BranchBoundsMaxY[SubLane]     = std::max({ P0.Y, P1.Y, P2.Y }) + 1.0e-6f;
                        SplitChild.BranchBoundsMinZ[SubLane]     = std::min({ P0.Z, P1.Z, P2.Z }) - 1.0e-6f;
                        SplitChild.BranchBoundsMaxZ[SubLane]     = std::max({ P0.Z, P1.Z, P2.Z }) + 1.0e-6f;
                        SplitChild.BranchReference[SubLane]      = SpatialAcceleration::WideLeafReferenceMask | TriIdx;
                        SplitChild.BranchPrimitiveCount[SubLane] = 1u;
                    }
                    else
                    {
                        SplitChild.BranchBoundsMinX[SubLane]     =  1.0e30f;
                        SplitChild.BranchBoundsMaxX[SubLane]     = -1.0e30f;
                        SplitChild.BranchBoundsMinY[SubLane]     =  1.0e30f;
                        SplitChild.BranchBoundsMaxY[SubLane]     = -1.0e30f;
                        SplitChild.BranchBoundsMinZ[SubLane]     =  1.0e30f;
                        SplitChild.BranchBoundsMaxZ[SubLane]     = -1.0e30f;
                        SplitChild.BranchReference[SubLane]      = SpatialAcceleration::EmptyBranchReference;
                        SplitChild.BranchPrimitiveCount[SubLane] = 0u;
                    }
                }
                const uint32_t ChildIdx = static_cast<uint32_t>(Subtree.LocalBvh.WideEntries.size());
                Subtree.LocalBvh.WideEntries.push_back(SplitChild);
                Subtree.LocalBvh.WideEntries[NodeIdx].BranchReference[Lane]      = ChildIdx;
                Subtree.LocalBvh.WideEntries[NodeIdx].BranchPrimitiveCount[Lane] = 0u;
            }
        }

        // Annotate ONLY the highest transition lane entering each single-cluster subtree (internal or leaf)
        // so rays test ActiveClusterBitmask at most once per homogeneous branch and zero times below it.
        const std::size_t NodeCount = Subtree.LocalBvh.WideEntries.size();
        Subtree.TransitionLaneClusterGate.assign(
            NodeCount,
            std::array<uint32_t, 4u>{ UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX });
        constexpr uint32_t kEmptySubtree = UINT32_MAX - 1u;
        constexpr uint32_t kMixedSubtree = UINT32_MAX;
        std::vector<uint8_t>                   EvalState(NodeCount, 0u);
        std::vector<uint32_t>                  NodeMemo(NodeCount, kMixedSubtree);
        std::vector<std::array<uint32_t, 4u>>  LaneMemo(
            NodeCount,
            std::array<uint32_t, 4u>{ kEmptySubtree, kEmptySubtree, kEmptySubtree, kEmptySubtree });

        auto EvalHomogeneousNode = [&](auto& Self, uint32_t NodeIdx) -> uint32_t
        {
            if (NodeIdx >= NodeCount)
            {
                return kEmptySubtree;
            }
            if (EvalState[NodeIdx] == 2u)
            {
                return NodeMemo[NodeIdx];
            }
            if (EvalState[NodeIdx] == 1u)
            {
                return kMixedSubtree;
            }
            EvalState[NodeIdx] = 1u;

            const auto& Entry       = Subtree.LocalBvh.WideEntries[NodeIdx];
            uint32_t    CommonClust = kEmptySubtree;
            for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
            {
                const uint32_t Ref = Entry.BranchReference[Lane];
                const uint32_t Cnt = Entry.BranchPrimitiveCount[Lane];
                if (Ref == SpatialAcceleration::EmptyBranchReference)
                {
                    continue;
                }

                uint32_t LaneClust = kEmptySubtree;
                if (Cnt > 0u)
                {
                    const uint32_t Start = Ref & 0x7FFFFFFFu;
                    if (Start < TriCount)
                    {
                        LaneClust = Subtree.TriLocalClusterIndices[Start];
                    }
                }
                else
                {
                    LaneClust = Self(Self, Ref);
                }
                LaneMemo[NodeIdx][Lane] = LaneClust;

                if (LaneClust == kEmptySubtree)
                {
                    continue;
                }
                if (CommonClust == kEmptySubtree)
                {
                    CommonClust = LaneClust;
                }
                else if (CommonClust != LaneClust)
                {
                    CommonClust = kMixedSubtree;
                }
            }

            EvalState[NodeIdx] = 2u;
            NodeMemo[NodeIdx]  = CommonClust;
            return CommonClust;
        };
        (void)EvalHomogeneousNode(EvalHomogeneousNode, 0u);

        for (std::size_t NodeIdx = 0u; NodeIdx < NodeCount; ++NodeIdx)
        {
            if (NodeMemo[NodeIdx] != kMixedSubtree)
            {
                continue;
            }
            for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
            {
                const uint32_t LaneClust = LaneMemo[NodeIdx][Lane];
                if (LaneClust < kEmptySubtree)
                {
                    Subtree.TransitionLaneClusterGate[NodeIdx][Lane] = LaneClust;
                }
            }
        }
    }

    for (auto& Existing : AssetClusterTreeCache)
    {
        if (Existing.PyramidAsset == PyramidAsset)
        {
            Existing = std::move(Built);
            return Existing;
        }
    }
    AssetClusterTreeCache.push_back(std::move(Built));
    return AssetClusterTreeCache.back();
}

const DistanceFieldSpace::LocalSignedDistanceVolume*
VulkanWorldRenderSequence::AcquirePyramidDistanceVolume(
    const SpatialAcceleration::ClusterPyramid* PyramidAsset)
{
    if (PyramidAsset == nullptr || PyramidAsset->Triangles.empty() || PyramidAsset->Positions.empty())
    {
        return nullptr;
    }

    for (const PreparedPyramidDistanceVolume& ExistingEntry : PreparedPyramidVolumes)
    {
        if (ExistingEntry.PyramidAsset == PyramidAsset &&
            ExistingEntry.SourceTriangleCount == PyramidAsset->Triangles.size())
        {
            return !ExistingEntry.Volume.DistanceSamples.empty()
                ? &ExistingEntry.Volume
                : nullptr;
        }
    }

    std::vector<SpatialAcceleration::RayTrianglePrimitive> LocalPrimitives;
    LocalPrimitives.reserve(PyramidAsset->Triangles.size());
    for (const auto& SourceTriangle : PyramidAsset->Triangles)
    {
        if (SourceTriangle.VertexIndex0 >= PyramidAsset->Positions.size() ||
            SourceTriangle.VertexIndex1 >= PyramidAsset->Positions.size() ||
            SourceTriangle.VertexIndex2 >= PyramidAsset->Positions.size())
        {
            continue;
        }
        const Mathematics::Axis3D Corner0 = PyramidAsset->Positions[SourceTriangle.VertexIndex0];
        const Mathematics::Axis3D Corner1 = PyramidAsset->Positions[SourceTriangle.VertexIndex1];
        const Mathematics::Axis3D Corner2 = PyramidAsset->Positions[SourceTriangle.VertexIndex2];
        SpatialAcceleration::RayTrianglePrimitive Primitive {};
        Primitive.Corner0          = Corner0;
        Primitive.Edge01           = Corner1 - Corner0;
        Primitive.Edge02           = Corner2 - Corner0;
        Primitive.SourceFaceIndex  = static_cast<uint32_t>(LocalPrimitives.size());
        Primitive.PrimitiveOrdinal = static_cast<uint32_t>(LocalPrimitives.size());
        Primitive.MaterialOrdinal  = SourceTriangle.MaterialOrdinal;
        Primitive.OpaqueMask       = 1u;
        LocalPrimitives.push_back(Primitive);
    }

    // 💡 std::deque owns LocalSignedDistanceVolume by value while keeping addresses stable across subsequent insertions
    PreparedPyramidDistanceVolume PreparedEntry {};
    PreparedEntry.PyramidAsset        = PyramidAsset;
    PreparedEntry.SourceTriangleCount = PyramidAsset->Triangles.size();

    if (!LocalPrimitives.empty())
    {
        Mathematics::Axis3D BoundsMinimum {};
        Mathematics::Axis3D BoundsMaximum {};
        (void)ComputePyramidLocalBounds(PyramidAsset, BoundsMinimum, BoundsMaximum);
        const float SpanX             = std::max(AutoPyramidMinSpanMeters, BoundsMaximum.X - BoundsMinimum.X);
        const float SpanY             = std::max(AutoPyramidMinSpanMeters, BoundsMaximum.Y - BoundsMinimum.Y);
        const float SpanZ             = std::max(AutoPyramidMinSpanMeters, BoundsMaximum.Z - BoundsMinimum.Z);
        const float MaximumSpanMeters = std::max({ SpanX, SpanY, SpanZ });
        const float VoxelPitchMeters  = std::max(
            AutoPyramidMinPitchMeters,
            MaximumSpanMeters / AutoPyramidTargetCells);

        const auto ResolveAxisResolution = [VoxelPitchMeters](float SpanMeters) noexcept -> uint32_t
        {
            const uint32_t UnalignedCells = static_cast<uint32_t>(std::lround(SpanMeters / VoxelPitchMeters));
            const uint32_t AlignedCells   =
                ((UnalignedCells + (AutoPyramidAlignmentQuantum - 1u)) / AutoPyramidAlignmentQuantum) *
                AutoPyramidAlignmentQuantum;
            return std::clamp(AlignedCells, AutoPyramidMinAxisCells, AutoPyramidMaxAxisCells);
        };

        const float MarginMeters = std::clamp(
            MaximumSpanMeters * AutoPyramidMarginFraction,
            AutoPyramidMinMarginMeters,
            AutoPyramidMaxMarginMeters);
        (void)PreparedEntry.Volume.BakeFromTrianglesAspect(
            LocalPrimitives,
            ResolveAxisResolution(SpanX),
            ResolveAxisResolution(SpanY),
            ResolveAxisResolution(SpanZ),
            MarginMeters);
    }

    for (PreparedPyramidDistanceVolume& ExistingEntry : PreparedPyramidVolumes)
    {
        if (ExistingEntry.PyramidAsset == PyramidAsset)
        {
            ExistingEntry = std::move(PreparedEntry);
            return !ExistingEntry.Volume.DistanceSamples.empty()
                ? &ExistingEntry.Volume
                : nullptr;
        }
    }

    PreparedPyramidVolumes.push_back(std::move(PreparedEntry));
    const PreparedPyramidDistanceVolume& StoredEntry = PreparedPyramidVolumes.back();
    return !StoredEntry.Volume.DistanceSamples.empty()
        ? &StoredEntry.Volume
        : nullptr;
}

void VulkanWorldRenderSequence::PropagateTopBranchGateBits(
    std::vector<uint32_t>& InOutMaskWords) const noexcept
{
    for (const TopBranchGateRule& Rule : TopBranchGateRules)
    {
        bool AnyChildActive = false;
        for (uint32_t K = 0u; K < 4u; ++K)
        {
            const uint32_t ChildOrd = Rule.ChildOrdinals[K];
            if (ChildOrd == UINT32_MAX)
            {
                continue;
            }
            const std::size_t WordIdx = static_cast<std::size_t>(ChildOrd >> 5u);
            if (WordIdx < InOutMaskWords.size() &&
                ((InOutMaskWords[WordIdx] >> (ChildOrd & 31u)) & 1u) != 0u)
            {
                AnyChildActive = true;
                break;
            }
        }
        if (AnyChildActive)
        {
            const std::size_t DstWord = static_cast<std::size_t>(Rule.GateOrdinal >> 5u);
            if (DstWord < InOutMaskWords.size())
            {
                InOutMaskWords[DstWord] |= (1u << (Rule.GateOrdinal & 31u));
            }
        }
    }
}

uint64_t VulkanWorldRenderSequence::ComputeResidentClusterTransformSignature(
    const WorldSceneSpecification& Scene) const noexcept
{
    uint64_t Sig = 14695981039346656037ull;
    auto Mix = [&Sig](uint64_t Value) noexcept
    {
        Sig = (Sig ^ Value) * 1099511628211ull;
    };
    Mix(static_cast<uint64_t>(Scene.ClusterInstances.size()));
    for (uint32_t InstIdx = 0u; InstIdx < static_cast<uint32_t>(Scene.ClusterInstances.size()); ++InstIdx)
    {
        const RigidClusterInstance& Inst = Scene.ClusterInstances[InstIdx];
        if (Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f)
        {
            Mix(0x9E3779B97F4A7C15ull ^ InstIdx);
            continue;
        }
        const float ScaleU = std::max(1.0e-4f, Inst.UniformScale);
        const float ScaleX = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;
        Mix(InstIdx + 1u);
        Mix(reinterpret_cast<uintptr_t>(Inst.PyramidAsset));
        Mix(Inst.PyramidAsset != nullptr ? static_cast<uint64_t>(Inst.PyramidAsset->Clusters.size()) : 0ull);
        Mix(static_cast<uint64_t>(Inst.MaterialSlot));
        Mix(static_cast<uint64_t>(std::lround(Inst.RotationZDegrees * 100.0f)));
        Mix(static_cast<uint64_t>(std::lround(ScaleX * 1000.0f)));
        Mix(static_cast<uint64_t>(std::lround(ScaleY * 1000.0f)));
        Mix(static_cast<uint64_t>(std::lround(ScaleZ * 1000.0f)));
        Mix(static_cast<uint64_t>(std::lround(Inst.WorldTranslation.X * 1000.0f)));
        Mix(static_cast<uint64_t>(std::lround(Inst.WorldTranslation.Y * 1000.0f)));
        Mix(static_cast<uint64_t>(std::lround(Inst.WorldTranslation.Z * 1000.0f)));
    }
    return Sig;
}

uint64_t VulkanWorldRenderSequence::ComputeResidentClusterTopologySignature(
    const WorldSceneSpecification& Scene) const noexcept
{
    uint64_t Sig = 14695981039346656037ull;
    auto Mix = [&](uint64_t V) noexcept
    {
        Sig ^= V;
        Sig *= 1099511628211ull;
    };
    Mix(static_cast<uint64_t>(Scene.ClusterInstances.size()));
    for (uint32_t InstIdx = 0u; InstIdx < static_cast<uint32_t>(Scene.ClusterInstances.size()); ++InstIdx)
    {
        const RigidClusterInstance& Inst = Scene.ClusterInstances[InstIdx];
        if (Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f)
        {
            continue;
        }
        Mix(InstIdx + 1u);
        Mix(reinterpret_cast<uintptr_t>(Inst.PyramidAsset));
        Mix(Inst.PyramidAsset != nullptr ? static_cast<uint64_t>(Inst.PyramidAsset->Clusters.size()) : 0ull);
        Mix(Inst.PyramidAsset != nullptr ? static_cast<uint64_t>(Inst.PyramidAsset->Triangles.size()) : 0ull);
    }
    return Sig;
}

bool VulkanWorldRenderSequence::AssembleResidentClusterSceneBvh(
    const WorldSceneSpecification& Scene,
    bool                           ForceReassemble)
{
    if (Scene.ClusterInstances.empty())
    {
        return false;
    }

    uint32_t ActiveOpaqueInstanceCount = 0u;
    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f)
        {
            continue;
        }
        if (Inst.PyramidAsset == nullptr || Inst.PyramidAsset->Clusters.empty())
        {
            return false;
        }
        ++ActiveOpaqueInstanceCount;
    }

    const uint64_t TransformSig = ComputeResidentClusterTransformSignature(Scene);
    const uint64_t TopologySig  = ComputeResidentClusterTopologySignature(Scene);
    if (!ForceReassemble && ResidentClusterBvhActive && TransformSig == CachedResidentTransformSig)
    {
        return true;
    }

    const bool CanAttemptGpuFastTransform =
        ResidentClusterBvhActive &&
        (TopologySig == CachedResidentTopologySig) &&
        (ReservedTopNodeCount > 0u) &&
        (InstanceTransformBvhProgramHandle != UINT32_MAX) &&
        (InstanceTransformTriProgramHandle != UINT32_MAX) &&
        (ResidentLocalBvhBuffer.BufferHandle != VK_NULL_HANDLE) &&
        (ResidentLocalTriBuffer.BufferHandle != VK_NULL_HANDLE) &&
        (BvhEntryBuffer.BufferHandle != VK_NULL_HANDLE) &&
        (ShadedTriangleBuffer.BufferHandle != VK_NULL_HANDLE) &&
        (ActiveOpaqueInstanceCount == static_cast<uint32_t>(ResidentClusterInstances.size()));

    // Pre-warm per-asset local cluster sub-BVH4 cache (built once per asset across the entire session).
    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (!Inst.TransparentGlass && Inst.UniformScale > 1.0e-4f && Inst.PyramidAsset != nullptr)
        {
            (void)AcquireAssetClusterHierarchy(Inst.PyramidAsset);
        }
    }

    if (ActiveOpaqueInstanceCount == 0u)
    {
        SpatialAcceleration::WideTreeEntry DummyEntry {};
        for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
        {
            DummyEntry.BranchBoundsMinX[Lane]     =  1.0e30f;
            DummyEntry.BranchBoundsMaxX[Lane]     = -1.0e30f;
            DummyEntry.BranchBoundsMinY[Lane]     =  1.0e30f;
            DummyEntry.BranchBoundsMaxY[Lane]     = -1.0e30f;
            DummyEntry.BranchBoundsMinZ[Lane]     =  1.0e30f;
            DummyEntry.BranchBoundsMaxZ[Lane]     = -1.0e30f;
            DummyEntry.BranchReference[Lane]      = SpatialAcceleration::EmptyBranchReference;
            DummyEntry.BranchPrimitiveCount[Lane] = 0u;
        }
        GpuShadedTriangle96 DummyTri {};
        UploadDualDeviceBuffers(
            BvhEntryBuffer, &DummyEntry, sizeof(DummyEntry), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            ShadedTriangleBuffer, &DummyTri, sizeof(DummyTri), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        UploadedBvhCount         = 0u;
        UploadedTriangleCount    = 0u;
        UploadedClusterSlotCount = 0u;
        ResidentClusterInstances.clear();
        TopBranchGateRules.clear();
        FinestClusterMaskWords.assign(1u, 0u);
        ViewportClusterMaskWords.assign(1u, 0u);
        MaskSliceOffsetBytes = 256ull;
        EnsureBufferCapacity(ActiveClusterMaskBuffer, MaskSliceOffsetBytes * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (ActiveClusterMaskBuffer.HostAddress != nullptr)
        {
            std::memset(ActiveClusterMaskBuffer.HostAddress, 0, static_cast<std::size_t>(MaskSliceOffsetBytes * 2ull));
        }
        ResidentFinestTriangleCount = 0u;
        CachedResidentTransformSig  = TransformSig;
        CachedNaniteCutSignature    = 0u;
        ResidentClusterBvhActive    = true;
        ViewportMaskHoldsFinestOnly = true;
        NaniteLodStats.FullTriangleCount = 0u;
        return true;
    }

    struct ClusterWorldBoxRecord
    {
        Mathematics::Axis3D WorldMin              {};
        Mathematics::Axis3D WorldMax              {};
        Mathematics::Axis3D Centroid              {};
        float               HalfArea              = 0.0f;
        uint32_t            HierarchyBucket       = 0u;
        uint32_t            GlobalClusterOrdinal  = 0u;
        uint32_t            SubBvhRelativeNodeOff = 0u;
    };

    auto TransformAabbWorld = [](
        const Mathematics::Axis3D& LocalMin,
        const Mathematics::Axis3D& LocalMax,
        float                      ScaleX,
        float                      ScaleY,
        float                      ScaleZ,
        float                      CosZ,
        float                      SinZ,
        const Mathematics::Axis3D& Translation,
        Mathematics::Axis3D&       OutMin,
        Mathematics::Axis3D&       OutMax) noexcept
    {
        const float SMinX = std::min(LocalMin.X * ScaleX, LocalMax.X * ScaleX);
        const float SMaxX = std::max(LocalMin.X * ScaleX, LocalMax.X * ScaleX);
        const float SMinY = std::min(LocalMin.Y * ScaleY, LocalMax.Y * ScaleY);
        const float SMaxY = std::max(LocalMin.Y * ScaleY, LocalMax.Y * ScaleY);
        const float SMinZ = std::min(LocalMin.Z * ScaleZ, LocalMax.Z * ScaleZ);
        const float SMaxZ = std::max(LocalMin.Z * ScaleZ, LocalMax.Z * ScaleZ);

        const float CX0 = CosZ * SMinX;
        const float CX1 = CosZ * SMaxX;
        const float SX0 = -SinZ * SMinY;
        const float SX1 = -SinZ * SMaxY;

        const float SY0 = SinZ * SMinX;
        const float SY1 = SinZ * SMaxX;
        const float CY0 = CosZ * SMinY;
        const float CY1 = CosZ * SMaxY;

        OutMin.X = Translation.X + std::min(CX0, CX1) + std::min(SX0, SX1);
        OutMax.X = Translation.X + std::max(CX0, CX1) + std::max(SX0, SX1);
        OutMin.Y = Translation.Y + std::min(SY0, SY1) + std::min(CY0, CY1);
        OutMax.Y = Translation.Y + std::max(SY0, SY1) + std::max(CY0, CY1);
        OutMin.Z = Translation.Z + SMinZ;
        OutMax.Z = Translation.Z + SMaxZ;
    };

    const std::vector<ResidentClusterInstanceSpan> PreviousSpans =
        CanAttemptGpuFastTransform ? ResidentClusterInstances : std::vector<ResidentClusterInstanceSpan>{};

    ResidentClusterInstances.clear();
    ResidentClusterInstances.reserve(ActiveOpaqueInstanceCount);

    std::vector<const CachedAssetClusterHierarchy*> InstanceHierarchies(Scene.ClusterInstances.size(), nullptr);
    uint32_t TotalClusterSlots = 0u;
    uint32_t TotalSubBvhNodes  = 0u;
    uint32_t TotalTriangles    = 0u;
    uint32_t FinestTriSum      = 0u;

    for (uint32_t InstIdx = 0u; InstIdx < static_cast<uint32_t>(Scene.ClusterInstances.size()); ++InstIdx)
    {
        const RigidClusterInstance& Inst = Scene.ClusterInstances[InstIdx];
        if (Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f)
        {
            continue;
        }
        const auto& Hierarchy        = AcquireAssetClusterHierarchy(Inst.PyramidAsset);
        InstanceHierarchies[InstIdx] = &Hierarchy;
        const uint32_t ClusterCnt    = static_cast<uint32_t>(Inst.PyramidAsset->Clusters.size());

        ResidentClusterInstanceSpan Span {};
        Span.InstanceIndex             = InstIdx;
        Span.FirstGlobalClusterOrdinal = TotalClusterSlots;
        Span.ClusterCount              = ClusterCnt;
        ResidentClusterInstances.push_back(Span);
        TotalClusterSlots += ClusterCnt;

        for (const auto& Sub : Hierarchy.LevelSubtrees)
        {
            TotalSubBvhNodes += static_cast<uint32_t>(Sub.LocalBvh.WideEntries.size());
            TotalTriangles   += Sub.TriangleCount;
            if (Sub.HierarchyLevel == 0u)
            {
                FinestTriSum += Sub.TriangleCount;
            }
        }
    }

    uint32_t NextLevelGateOrd = TotalClusterSlots;
    for (ResidentClusterInstanceSpan& Span : ResidentClusterInstances)
    {
        const auto& Hierarchy = *InstanceHierarchies[Span.InstanceIndex];
        for (uint32_t Level = 0u; Level < static_cast<uint32_t>(Hierarchy.LevelSubtrees.size()) && Level < 8u; ++Level)
        {
            const auto& Sub = Hierarchy.LevelSubtrees[Level];
            if (Sub.TriangleCount > 0u && !Sub.LocalBvh.WideEntries.empty())
            {
                Span.LevelGateOrdinals[Level] = NextLevelGateOrd++;
            }
        }
    }
    const uint32_t TotalClusterAndLevelSlots = NextLevelGateOrd;

    const std::size_t BaseMaskWords = std::max<std::size_t>(
        1u,
        (static_cast<std::size_t>(TotalClusterAndLevelSlots) + 31u) / 32u);
    FinestClusterMaskWords.assign(BaseMaskWords, 0u);

    std::vector<ClusterWorldBoxRecord> ClusterBoxes {};
    ClusterBoxes.reserve(std::max<uint32_t>(4u, TotalClusterAndLevelSlots - TotalClusterSlots));

    uint32_t RunningSubNodeOff = 0u;
    for (const ResidentClusterInstanceSpan& Span : ResidentClusterInstances)
    {
        const RigidClusterInstance& Inst      = Scene.ClusterInstances[Span.InstanceIndex];
        const auto&                 Hierarchy = *InstanceHierarchies[Span.InstanceIndex];
        const float ScaleU = std::max(1.0e-4f, Inst.UniformScale);
        const float ScaleX = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;
        const float YawRad = Inst.RotationZDegrees * Constants::DegToRadFloat;
        const bool  HasYaw = std::fabs(YawRad) > 1.0e-5f;
        const float CosZ   = HasYaw ? std::cos(YawRad) : 1.0f;
        const float SinZ   = HasYaw ? std::sin(YawRad) : 0.0f;

        for (uint32_t LocalClusterIdx = 0u; LocalClusterIdx < Span.ClusterCount; ++LocalClusterIdx)
        {
            if (Inst.PyramidAsset->Clusters[LocalClusterIdx].HierarchyLevel == 0u)
            {
                const uint32_t GlobalOrd = Span.FirstGlobalClusterOrdinal + LocalClusterIdx;
                FinestClusterMaskWords[GlobalOrd >> 5u] |= (1u << (GlobalOrd & 31u));
            }
        }
        if (Span.LevelGateOrdinals[0u] != UINT32_MAX)
        {
            const uint32_t Gate0 = Span.LevelGateOrdinals[0u];
            FinestClusterMaskWords[Gate0 >> 5u] |= (1u << (Gate0 & 31u));
        }

        for (uint32_t Level = 0u; Level < static_cast<uint32_t>(Hierarchy.LevelSubtrees.size()) && Level < 8u; ++Level)
        {
            const auto&    Sub          = Hierarchy.LevelSubtrees[Level];
            const uint32_t LevelGateOrd = Span.LevelGateOrdinals[Level];
            if (LevelGateOrd == UINT32_MAX || Sub.TriangleCount == 0u || Sub.LocalBvh.WideEntries.empty())
            {
                continue;
            }
            ClusterWorldBoxRecord Rec {};
            TransformAabbWorld(
                Sub.LocalBoundsMin, Sub.LocalBoundsMax,
                ScaleX, ScaleY, ScaleZ, CosZ, SinZ, Inst.WorldTranslation,
                Rec.WorldMin, Rec.WorldMax);
            const float DX = std::max(0.0f, Rec.WorldMax.X - Rec.WorldMin.X);
            const float DY = std::max(0.0f, Rec.WorldMax.Y - Rec.WorldMin.Y);
            const float DZ = std::max(0.0f, Rec.WorldMax.Z - Rec.WorldMin.Z);
            Rec.Centroid              = (Rec.WorldMin + Rec.WorldMax) * 0.5f;
            Rec.HalfArea              = DX * DY + DY * DZ + DZ * DX;
            Rec.HierarchyBucket       = std::min(3u, Level);
            Rec.GlobalClusterOrdinal  = LevelGateOrd;
            Rec.SubBvhRelativeNodeOff = RunningSubNodeOff;
            ClusterBoxes.push_back(Rec);
            RunningSubNodeOff += static_cast<uint32_t>(Sub.LocalBvh.WideEntries.size());
        }
    }

    auto MakeEmptyWideNode = []() noexcept -> SpatialAcceleration::WideTreeEntry
    {
        SpatialAcceleration::WideTreeEntry Node {};
        for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
        {
            Node.BranchBoundsMinX[Lane]     =  1.0e30f;
            Node.BranchBoundsMaxX[Lane]     = -1.0e30f;
            Node.BranchBoundsMinY[Lane]     =  1.0e30f;
            Node.BranchBoundsMaxY[Lane]     = -1.0e30f;
            Node.BranchBoundsMinZ[Lane]     =  1.0e30f;
            Node.BranchBoundsMaxZ[Lane]     = -1.0e30f;
            Node.BranchReference[Lane]      = SpatialAcceleration::EmptyBranchReference;
            Node.BranchPrimitiveCount[Lane] = 0u;
        }
        return Node;
    };

    TopBranchGateRules.clear();
    TopBranchGateRules.reserve(std::max<std::size_t>(4u, (ClusterBoxes.size() + 2u) / 3u));

    std::vector<SpatialAcceleration::WideTreeEntry> TopNodes {};
    TopNodes.reserve(std::max<std::size_t>(1u, (ClusterBoxes.size() + 2u) / 3u));
    uint32_t NextGateOrdinal = TotalClusterAndLevelSlots;

    auto BoxHalfArea = [](const Mathematics::Axis3D& BMin, const Mathematics::Axis3D& BMax) noexcept -> float
    {
        const float DX = std::max(0.0f, BMax.X - BMin.X);
        const float DY = std::max(0.0f, BMax.Y - BMin.Y);
        const float DZ = std::max(0.0f, BMax.Z - BMin.Z);
        return DX * DY + DY * DZ + DZ * DX;
    };

    auto PartitionRangeSah = [&](std::size_t Begin, std::size_t End) -> std::size_t
    {
        const std::size_t Count = End - Begin;
        if (Count <= 1u)
        {
            return Begin;
        }
        Mathematics::Axis3D CMin = ClusterBoxes[Begin].Centroid;
        Mathematics::Axis3D CMax = ClusterBoxes[Begin].Centroid;
        for (std::size_t I = Begin + 1u; I < End; ++I)
        {
            CMin.X = std::min(CMin.X, ClusterBoxes[I].Centroid.X);
            CMin.Y = std::min(CMin.Y, ClusterBoxes[I].Centroid.Y);
            CMin.Z = std::min(CMin.Z, ClusterBoxes[I].Centroid.Z);
            CMax.X = std::max(CMax.X, ClusterBoxes[I].Centroid.X);
            CMax.Y = std::max(CMax.Y, ClusterBoxes[I].Centroid.Y);
            CMax.Z = std::max(CMax.Z, ClusterBoxes[I].Centroid.Z);
        }

        constexpr uint32_t kNumBins = 12u;
        int   BestAxis     = -1;
        float BestSplitPos = 0.0f;
        float BestCost     = 1.0e30f;

        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float AxisMin  = (Axis == 0) ? CMin.X : ((Axis == 1) ? CMin.Y : CMin.Z);
            const float AxisMax  = (Axis == 0) ? CMax.X : ((Axis == 1) ? CMax.Y : CMax.Z);
            const float AxisSpan = AxisMax - AxisMin;
            if (AxisSpan < 1.0e-5f)
            {
                continue;
            }

            struct SahBin
            {
                Mathematics::Axis3D BMin  {  1.0e30f,  1.0e30f,  1.0e30f };
                Mathematics::Axis3D BMax  { -1.0e30f, -1.0e30f, -1.0e30f };
                uint32_t            Count = 0u;
            };
            std::array<SahBin, kNumBins> Bins {};
            const float Scale = static_cast<float>(kNumBins) / AxisSpan;

            for (std::size_t I = Begin; I < End; ++I)
            {
                const float CVal = (Axis == 0) ? ClusterBoxes[I].Centroid.X
                                 : ((Axis == 1) ? ClusterBoxes[I].Centroid.Y : ClusterBoxes[I].Centroid.Z);
                const uint32_t BinIdx = std::min<uint32_t>(
                    kNumBins - 1u,
                    static_cast<uint32_t>(std::max(0.0f, (CVal - AxisMin) * Scale)));
                auto& Bin = Bins[BinIdx];
                Bin.BMin.X = std::min(Bin.BMin.X, ClusterBoxes[I].WorldMin.X);
                Bin.BMin.Y = std::min(Bin.BMin.Y, ClusterBoxes[I].WorldMin.Y);
                Bin.BMin.Z = std::min(Bin.BMin.Z, ClusterBoxes[I].WorldMin.Z);
                Bin.BMax.X = std::max(Bin.BMax.X, ClusterBoxes[I].WorldMax.X);
                Bin.BMax.Y = std::max(Bin.BMax.Y, ClusterBoxes[I].WorldMax.Y);
                Bin.BMax.Z = std::max(Bin.BMax.Z, ClusterBoxes[I].WorldMax.Z);
                Bin.Count += 1u;
            }

            std::array<float, kNumBins - 1u>    RightArea {};
            std::array<uint32_t, kNumBins - 1u> RightCount {};
            Mathematics::Axis3D RMin {  1.0e30f,  1.0e30f,  1.0e30f };
            Mathematics::Axis3D RMax { -1.0e30f, -1.0e30f, -1.0e30f };
            uint32_t RCnt = 0u;
            for (int B = static_cast<int>(kNumBins) - 1; B >= 1; --B)
            {
                if (Bins[static_cast<std::size_t>(B)].Count > 0u)
                {
                    const auto& Src = Bins[static_cast<std::size_t>(B)];
                    RMin.X = std::min(RMin.X, Src.BMin.X);
                    RMin.Y = std::min(RMin.Y, Src.BMin.Y);
                    RMin.Z = std::min(RMin.Z, Src.BMin.Z);
                    RMax.X = std::max(RMax.X, Src.BMax.X);
                    RMax.Y = std::max(RMax.Y, Src.BMax.Y);
                    RMax.Z = std::max(RMax.Z, Src.BMax.Z);
                    RCnt  += Src.Count;
                }
                RightArea[static_cast<std::size_t>(B - 1)]  = (RCnt > 0u) ? BoxHalfArea(RMin, RMax) : 0.0f;
                RightCount[static_cast<std::size_t>(B - 1)] = RCnt;
            }

            Mathematics::Axis3D LMin {  1.0e30f,  1.0e30f,  1.0e30f };
            Mathematics::Axis3D LMax { -1.0e30f, -1.0e30f, -1.0e30f };
            uint32_t LCnt = 0u;
            for (uint32_t B = 0u; B + 1u < kNumBins; ++B)
            {
                if (Bins[B].Count > 0u)
                {
                    LMin.X = std::min(LMin.X, Bins[B].BMin.X);
                    LMin.Y = std::min(LMin.Y, Bins[B].BMin.Y);
                    LMin.Z = std::min(LMin.Z, Bins[B].BMin.Z);
                    LMax.X = std::max(LMax.X, Bins[B].BMax.X);
                    LMax.Y = std::max(LMax.Y, Bins[B].BMax.Y);
                    LMax.Z = std::max(LMax.Z, Bins[B].BMax.Z);
                    LCnt  += Bins[B].Count;
                }
                if (LCnt == 0u || RightCount[B] == 0u)
                {
                    continue;
                }
                const float Cost = static_cast<float>(LCnt) * BoxHalfArea(LMin, LMax) +
                                   static_cast<float>(RightCount[B]) * RightArea[B];
                if (Cost < BestCost)
                {
                    BestCost     = Cost;
                    BestAxis     = Axis;
                    BestSplitPos = AxisMin + AxisSpan * (static_cast<float>(B + 1u) / static_cast<float>(kNumBins));
                }
            }
        }

        if (BestAxis >= 0)
        {
            auto SplitIter = std::partition(
                ClusterBoxes.begin() + static_cast<std::ptrdiff_t>(Begin),
                ClusterBoxes.begin() + static_cast<std::ptrdiff_t>(End),
                [BestAxis, BestSplitPos](const ClusterWorldBoxRecord& Rec) noexcept
                {
                    const float CVal = (BestAxis == 0) ? Rec.Centroid.X
                                     : ((BestAxis == 1) ? Rec.Centroid.Y : Rec.Centroid.Z);
                    return CVal < BestSplitPos;
                });
            const std::size_t Mid = static_cast<std::size_t>(SplitIter - ClusterBoxes.begin());
            if (Mid > Begin && Mid < End)
            {
                return Mid;
            }
        }

        const std::size_t Mid = Begin + Count / 2u;
        std::nth_element(
            ClusterBoxes.begin() + static_cast<std::ptrdiff_t>(Begin),
            ClusterBoxes.begin() + static_cast<std::ptrdiff_t>(Mid),
            ClusterBoxes.begin() + static_cast<std::ptrdiff_t>(End),
            [](const ClusterWorldBoxRecord& A, const ClusterWorldBoxRecord& B) noexcept
            {
                return A.GlobalClusterOrdinal < B.GlobalClusterOrdinal;
            });
        return Mid;
    };

    auto BuildTopNode = [&](auto& Self, std::size_t Begin, std::size_t End) -> std::pair<uint32_t, uint32_t>
    {
        const uint32_t NodeIdx   = static_cast<uint32_t>(TopNodes.size());
        const uint32_t MyGateOrd = NextGateOrdinal++;
        TopNodes.push_back(MakeEmptyWideNode());
        TopBranchGateRule MyRule {};
        MyRule.GateOrdinal = MyGateOrd;

        const std::size_t Count = End - Begin;
        if (Count == 0u)
        {
            TopBranchGateRules.push_back(MyRule);
            return { NodeIdx, MyGateOrd };
        }
        if (Count <= 4u)
        {
            for (std::size_t Lane = 0u; Lane < Count; ++Lane)
            {
                const auto& Rec = ClusterBoxes[Begin + Lane];
                auto&       Dst = TopNodes[NodeIdx];
                Dst.BranchBoundsMinX[Lane]     = Rec.WorldMin.X;
                Dst.BranchBoundsMaxX[Lane]     = Rec.WorldMax.X;
                Dst.BranchBoundsMinY[Lane]     = Rec.WorldMin.Y;
                Dst.BranchBoundsMaxY[Lane]     = Rec.WorldMax.Y;
                Dst.BranchBoundsMinZ[Lane]     = Rec.WorldMin.Z;
                Dst.BranchBoundsMaxZ[Lane]     = Rec.WorldMax.Z;
                Dst.BranchReference[Lane]      = Rec.SubBvhRelativeNodeOff;
                Dst.BranchPrimitiveCount[Lane] = (Rec.GlobalClusterOrdinal + 1u) << 4u;
                MyRule.ChildOrdinals[Lane]     = Rec.GlobalClusterOrdinal;
            }
            TopBranchGateRules.push_back(MyRule);
            return { NodeIdx, MyGateOrd };
        }

        // Isolate extreme surface-area outliers (e.g. a 24 m ground plane sharing centroid (0,0,0) with foreground meshes)
        // into their own dedicated wide lane so foreground cluster AABBs stay razor-tight.
        std::size_t MaxAreaIdx = Begin;
        double      SumArea    = 0.0;
        for (std::size_t I = Begin; I < End; ++I)
        {
            SumArea += static_cast<double>(ClusterBoxes[I].HalfArea);
            if (ClusterBoxes[I].HalfArea > ClusterBoxes[MaxAreaIdx].HalfArea)
            {
                MaxAreaIdx = I;
            }
        }
        const double OtherMeanArea = (SumArea - static_cast<double>(ClusterBoxes[MaxAreaIdx].HalfArea)) /
                                     static_cast<double>(Count - 1u);

        std::array<std::pair<std::size_t, std::size_t>, 4u> SubRanges {};
        uint32_t SubCount = 0u;
        auto PushRange = [&](std::size_t R0, std::size_t R1)
        {
            if (R1 > R0 && SubCount < 4u)
            {
                SubRanges[SubCount++] = { R0, R1 };
            }
        };

        if (static_cast<double>(ClusterBoxes[MaxAreaIdx].HalfArea) > std::max(1.0e-3, OtherMeanArea * 12.0))
        {
            if (MaxAreaIdx != Begin)
            {
                std::swap(ClusterBoxes[Begin], ClusterBoxes[MaxAreaIdx]);
            }
            PushRange(Begin, Begin + 1u);
            const std::size_t RemBegin = Begin + 1u;
            const std::size_t Split1   = PartitionRangeSah(RemBegin, End);
            if ((Split1 - RemBegin) >= (End - Split1) && (Split1 - RemBegin) > 1u)
            {
                const std::size_t Split2 = PartitionRangeSah(RemBegin, Split1);
                PushRange(RemBegin, Split2);
                PushRange(Split2, Split1);
                PushRange(Split1, End);
            }
            else if ((End - Split1) > 1u)
            {
                const std::size_t Split2 = PartitionRangeSah(Split1, End);
                PushRange(RemBegin, Split1);
                PushRange(Split1, Split2);
                PushRange(Split2, End);
            }
            else
            {
                PushRange(RemBegin, Split1);
                PushRange(Split1, End);
            }
        }
        else
        {
            const std::size_t Mid = PartitionRangeSah(Begin, End);
            const std::size_t Q1  = (Mid - Begin > 1u) ? PartitionRangeSah(Begin, Mid) : Begin;
            const std::size_t Q3  = (End - Mid > 1u) ? PartitionRangeSah(Mid, End) : Mid;
            if (Mid - Begin > 1u)
            {
                PushRange(Begin, Q1);
                PushRange(Q1, Mid);
            }
            else
            {
                PushRange(Begin, Mid);
            }
            if (End - Mid > 1u)
            {
                PushRange(Mid, Q3);
                PushRange(Q3, End);
            }
            else
            {
                PushRange(Mid, End);
            }
        }

        for (uint32_t Lane = 0u; Lane < SubCount; ++Lane)
        {
            const auto [R0, R1] = SubRanges[Lane];
            if (R1 - R0 == 1u)
            {
                const auto& Rec = ClusterBoxes[R0];
                auto&       Dst = TopNodes[NodeIdx];
                Dst.BranchBoundsMinX[Lane]     = Rec.WorldMin.X;
                Dst.BranchBoundsMaxX[Lane]     = Rec.WorldMax.X;
                Dst.BranchBoundsMinY[Lane]     = Rec.WorldMin.Y;
                Dst.BranchBoundsMaxY[Lane]     = Rec.WorldMax.Y;
                Dst.BranchBoundsMinZ[Lane]     = Rec.WorldMin.Z;
                Dst.BranchBoundsMaxZ[Lane]     = Rec.WorldMax.Z;
                Dst.BranchReference[Lane]      = Rec.SubBvhRelativeNodeOff;
                Dst.BranchPrimitiveCount[Lane] = (Rec.GlobalClusterOrdinal + 1u) << 4u;
                MyRule.ChildOrdinals[Lane]     = Rec.GlobalClusterOrdinal;
            }
            else
            {
                Mathematics::Axis3D UMin = ClusterBoxes[R0].WorldMin;
                Mathematics::Axis3D UMax = ClusterBoxes[R0].WorldMax;
                for (std::size_t I = R0 + 1u; I < R1; ++I)
                {
                    UMin.X = std::min(UMin.X, ClusterBoxes[I].WorldMin.X);
                    UMin.Y = std::min(UMin.Y, ClusterBoxes[I].WorldMin.Y);
                    UMin.Z = std::min(UMin.Z, ClusterBoxes[I].WorldMin.Z);
                    UMax.X = std::max(UMax.X, ClusterBoxes[I].WorldMax.X);
                    UMax.Y = std::max(UMax.Y, ClusterBoxes[I].WorldMax.Y);
                    UMax.Z = std::max(UMax.Z, ClusterBoxes[I].WorldMax.Z);
                }
                const auto [ChildNodeIdx, ChildGateOrd] = Self(Self, R0, R1);
                auto& Dst = TopNodes[NodeIdx];
                Dst.BranchBoundsMinX[Lane]     = UMin.X;
                Dst.BranchBoundsMaxX[Lane]     = UMax.X;
                Dst.BranchBoundsMinY[Lane]     = UMin.Y;
                Dst.BranchBoundsMaxY[Lane]     = UMax.Y;
                Dst.BranchBoundsMinZ[Lane]     = UMin.Z;
                Dst.BranchBoundsMaxZ[Lane]     = UMax.Z;
                Dst.BranchReference[Lane]      = ChildNodeIdx;
                Dst.BranchPrimitiveCount[Lane] = (ChildGateOrd + 1u) << 4u;
                MyRule.ChildOrdinals[Lane]     = ChildGateOrd;
            }
        }

        TopBranchGateRules.push_back(MyRule);
        return { NodeIdx, MyGateOrd };
    };

    // Group clusters at Root Node 0 by LOD level bucket (0 = L0, 1 = L1, 2 = L2, 3 = L3+)
    // so inactive LOD levels (e.g. L1/L2 during shadow/GDF bake or LOD-off) skip in ONE bit test at depth 0.
    std::stable_sort(
        ClusterBoxes.begin(),
        ClusterBoxes.end(),
        [](const ClusterWorldBoxRecord& A, const ClusterWorldBoxRecord& B) noexcept
        {
            return A.HierarchyBucket < B.HierarchyBucket;
        });

    std::array<std::pair<std::size_t, std::size_t>, 4u> LevelRanges {};
    uint32_t LevelRangeCount = 0u;
    {
        std::size_t Cursor = 0u;
        while (Cursor < ClusterBoxes.size() && LevelRangeCount < 4u)
        {
            const uint32_t Bucket = ClusterBoxes[Cursor].HierarchyBucket;
            std::size_t    Next   = Cursor + 1u;
            while (Next < ClusterBoxes.size() && ClusterBoxes[Next].HierarchyBucket == Bucket)
            {
                ++Next;
            }
            LevelRanges[LevelRangeCount++] = { Cursor, Next };
            Cursor = Next;
        }
    }

    if (ClusterBoxes.size() <= 4u || LevelRangeCount <= 1u)
    {
        (void)BuildTopNode(BuildTopNode, 0u, ClusterBoxes.size());
    }
    else
    {
        TopNodes.push_back(MakeEmptyWideNode()); // Root Node 0
        for (uint32_t Lane = 0u; Lane < LevelRangeCount; ++Lane)
        {
            const auto [R0, R1] = LevelRanges[Lane];
            if (R1 - R0 == 1u)
            {
                const auto& Rec = ClusterBoxes[R0];
                auto&       Dst = TopNodes[0u];
                Dst.BranchBoundsMinX[Lane]     = Rec.WorldMin.X;
                Dst.BranchBoundsMaxX[Lane]     = Rec.WorldMax.X;
                Dst.BranchBoundsMinY[Lane]     = Rec.WorldMin.Y;
                Dst.BranchBoundsMaxY[Lane]     = Rec.WorldMax.Y;
                Dst.BranchBoundsMinZ[Lane]     = Rec.WorldMin.Z;
                Dst.BranchBoundsMaxZ[Lane]     = Rec.WorldMax.Z;
                Dst.BranchReference[Lane]      = Rec.SubBvhRelativeNodeOff;
                Dst.BranchPrimitiveCount[Lane] = (Rec.GlobalClusterOrdinal + 1u) << 4u;
            }
            else
            {
                Mathematics::Axis3D UMin = ClusterBoxes[R0].WorldMin;
                Mathematics::Axis3D UMax = ClusterBoxes[R0].WorldMax;
                for (std::size_t I = R0 + 1u; I < R1; ++I)
                {
                    UMin.X = std::min(UMin.X, ClusterBoxes[I].WorldMin.X);
                    UMin.Y = std::min(UMin.Y, ClusterBoxes[I].WorldMin.Y);
                    UMin.Z = std::min(UMin.Z, ClusterBoxes[I].WorldMin.Z);
                    UMax.X = std::max(UMax.X, ClusterBoxes[I].WorldMax.X);
                    UMax.Y = std::max(UMax.Y, ClusterBoxes[I].WorldMax.Y);
                    UMax.Z = std::max(UMax.Z, ClusterBoxes[I].WorldMax.Z);
                }
                const auto [ChildNodeIdx, ChildGateOrd] = BuildTopNode(BuildTopNode, R0, R1);
                auto& Dst = TopNodes[0u];
                Dst.BranchBoundsMinX[Lane]     = UMin.X;
                Dst.BranchBoundsMaxX[Lane]     = UMax.X;
                Dst.BranchBoundsMinY[Lane]     = UMin.Y;
                Dst.BranchBoundsMaxY[Lane]     = UMax.Y;
                Dst.BranchBoundsMinZ[Lane]     = UMin.Z;
                Dst.BranchBoundsMaxZ[Lane]     = UMax.Z;
                Dst.BranchReference[Lane]      = ChildNodeIdx;
                Dst.BranchPrimitiveCount[Lane] = (ChildGateOrd + 1u) << 4u;
            }
        }
    }

    const uint32_t TopNodeCount = static_cast<uint32_t>(TopNodes.size());
    const bool UseGpuFastTransform =
        CanAttemptGpuFastTransform &&
        (TopNodeCount <= ReservedTopNodeCount);
    const uint32_t EffectiveTopReserve = UseGpuFastTransform
        ? ReservedTopNodeCount
        : std::max(TopNodeCount, std::max<uint32_t>(4u, static_cast<uint32_t>(ClusterBoxes.size()) * 2u));

    for (auto& Node : TopNodes)
    {
        for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
        {
            const uint32_t GatePlusOne = Node.BranchPrimitiveCount[Lane] >> 4u;
            if (GatePlusOne != 0u && (GatePlusOne - 1u) < TotalClusterAndLevelSlots)
            {
                Node.BranchReference[Lane] += EffectiveTopReserve;
            }
        }
    }

    const std::size_t FullMaskWordCount = std::max<std::size_t>(
        1u,
        (static_cast<std::size_t>(NextGateOrdinal) + 31u) / 32u);
    FinestClusterMaskWords.resize(FullMaskWordCount, 0u);
    PropagateTopBranchGateBits(FinestClusterMaskWords);

    if (UseGpuFastTransform)
    {
        struct alignas(16) GpuInstanceTransformUniformStd140
        {
            float    TranslationMaxScale[4] {};
            float    ScaleCosYaw[4]         {};
            float    InvScaleSinYaw[4]      {};
            uint32_t RangeMaterialFlags[4]  {};
        };

        std::vector<uint32_t> DirtySpanIndices {};
        DirtySpanIndices.reserve(ResidentClusterInstances.size());
        uint32_t RunningFirstNode = EffectiveTopReserve;
        uint32_t RunningFirstTri  = 0u;

        for (uint32_t SpanIdx = 0u; SpanIdx < static_cast<uint32_t>(ResidentClusterInstances.size()); ++SpanIdx)
        {
            ResidentClusterInstanceSpan& Span = ResidentClusterInstances[SpanIdx];
            const RigidClusterInstance&  Inst = Scene.ClusterInstances[Span.InstanceIndex];
            const auto& Hierarchy             = *InstanceHierarchies[Span.InstanceIndex];

            uint32_t InstNodes = 0u;
            uint32_t InstTris  = 0u;
            for (uint32_t Level = 0u; Level < static_cast<uint32_t>(Hierarchy.LevelSubtrees.size()) && Level < 8u; ++Level)
            {
                const auto& Sub = Hierarchy.LevelSubtrees[Level];
                if (Span.LevelGateOrdinals[Level] == UINT32_MAX || Sub.TriangleCount == 0u || Sub.LocalBvh.WideEntries.empty())
                {
                    continue;
                }
                InstNodes += static_cast<uint32_t>(Sub.LocalBvh.WideEntries.size());
                InstTris  += Sub.TriangleCount;
            }
            Span.FirstBvhNode  = RunningFirstNode;
            Span.BvhNodeCount  = InstNodes;
            Span.FirstTriangle = RunningFirstTri;
            Span.TriangleCount = InstTris;
            RunningFirstNode  += InstNodes;
            RunningFirstTri   += InstTris;

            const float ScaleU = std::max(1.0e-4f, Inst.UniformScale);
            const float ScaleX = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
            const float ScaleY = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
            const float ScaleZ = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;

            const ResidentClusterInstanceSpan& PrevSpan = PreviousSpans[SpanIdx];
            const bool Dirty =
                (std::fabs(ScaleX - PrevSpan.CachedScaleX) > 1.0e-5f) ||
                (std::fabs(ScaleY - PrevSpan.CachedScaleY) > 1.0e-5f) ||
                (std::fabs(ScaleZ - PrevSpan.CachedScaleZ) > 1.0e-5f) ||
                (std::fabs(Inst.RotationZDegrees - PrevSpan.CachedYawDeg) > 1.0e-4f) ||
                (std::fabs(Inst.WorldTranslation.X - PrevSpan.CachedTranslation.X) > 1.0e-5f) ||
                (std::fabs(Inst.WorldTranslation.Y - PrevSpan.CachedTranslation.Y) > 1.0e-5f) ||
                (std::fabs(Inst.WorldTranslation.Z - PrevSpan.CachedTranslation.Z) > 1.0e-5f) ||
                (static_cast<uint32_t>(Inst.MaterialSlot) != PrevSpan.CachedMaterialSlot);

            Span.CachedScaleX       = ScaleX;
            Span.CachedScaleY       = ScaleY;
            Span.CachedScaleZ       = ScaleZ;
            Span.CachedYawDeg       = Inst.RotationZDegrees;
            Span.CachedTranslation  = Inst.WorldTranslation;
            Span.CachedMaterialSlot = static_cast<uint32_t>(Inst.MaterialSlot);
            if (Dirty)
            {
                DirtySpanIndices.push_back(SpanIdx);
            }
        }

        const uint64_t TopBytes = static_cast<uint64_t>(TopNodeCount) * sizeof(SpatialAcceleration::WideTreeEntry);
        const uint64_t UniStride = 256ull;
        const uint64_t NeededUniBytes = std::max<uint64_t>(1u, static_cast<uint64_t>(DirtySpanIndices.size()) * 2ull) * UniStride;
        RetirePendingTransform(true);
        EnsureBufferCapacity(TopNodeStagingBuffer, TopBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        EnsureBufferCapacity(InstanceTransformUniformBuffer, NeededUniBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

        if (TopNodeStagingBuffer.HostAddress != nullptr && InstanceTransformUniformBuffer.HostAddress != nullptr)
        {
            std::memcpy(TopNodeStagingBuffer.HostAddress, TopNodes.data(), static_cast<std::size_t>(TopBytes));

            DispatchEncoder.SelectDescriptorRingSlot(3u);
            DispatchEncoder.ResetTransientDescriptors();
            VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
            if (Cmd != VK_NULL_HANDLE)
            {
                VkBufferCopy TopCopy {};
                TopCopy.srcOffset = 0ull;
                TopCopy.dstOffset = 0ull;
                TopCopy.size      = TopBytes;
                vkCmdCopyBuffer(Cmd, TopNodeStagingBuffer.BufferHandle, BvhEntryBuffer.BufferHandle, 1u, &TopCopy);

                auto* UniHost = reinterpret_cast<uint8_t*>(InstanceTransformUniformBuffer.HostAddress);
                uint64_t UniSlot = 0ull;
                for (uint32_t SpanIdx : DirtySpanIndices)
                {
                    const ResidentClusterInstanceSpan& Span = ResidentClusterInstances[SpanIdx];
                    const float ScaleX    = Span.CachedScaleX;
                    const float ScaleY    = Span.CachedScaleY;
                    const float ScaleZ    = Span.CachedScaleZ;
                    const float MaxScale  = std::max({ ScaleX, ScaleY, ScaleZ });
                    const bool  NonUni    = (std::fabs(ScaleX - ScaleY) > 1.0e-4f) || (std::fabs(ScaleX - ScaleZ) > 1.0e-4f);
                    const float YawRad    = Span.CachedYawDeg * Constants::DegToRadFloat;
                    const bool  HasYaw    = std::fabs(YawRad) > 1.0e-5f;
                    const float CosZ      = HasYaw ? std::cos(YawRad) : 1.0f;
                    const float SinZ      = HasYaw ? std::sin(YawRad) : 0.0f;
                    const uint32_t Flags  = (HasYaw ? 1u : 0u) | (NonUni ? 2u : 0u);

                    GpuInstanceTransformUniformStd140 BaseUni {};
                    BaseUni.TranslationMaxScale[0] = Span.CachedTranslation.X;
                    BaseUni.TranslationMaxScale[1] = Span.CachedTranslation.Y;
                    BaseUni.TranslationMaxScale[2] = Span.CachedTranslation.Z;
                    BaseUni.TranslationMaxScale[3] = MaxScale;
                    BaseUni.ScaleCosYaw[0]         = ScaleX;
                    BaseUni.ScaleCosYaw[1]         = ScaleY;
                    BaseUni.ScaleCosYaw[2]         = ScaleZ;
                    BaseUni.ScaleCosYaw[3]         = CosZ;
                    BaseUni.InvScaleSinYaw[0]      = 1.0f / ScaleX;
                    BaseUni.InvScaleSinYaw[1]      = 1.0f / ScaleY;
                    BaseUni.InvScaleSinYaw[2]      = 1.0f / ScaleZ;
                    BaseUni.InvScaleSinYaw[3]      = SinZ;
                    BaseUni.RangeMaterialFlags[2]  = Span.CachedMaterialSlot;
                    BaseUni.RangeMaterialFlags[3]  = Flags;

                    if (Span.BvhNodeCount > 0u)
                    {
                        const uint64_t OffBvh = (UniSlot++) * UniStride;
                        GpuInstanceTransformUniformStd140 BvhUni = BaseUni;
                        BvhUni.RangeMaterialFlags[0] = Span.FirstBvhNode;
                        BvhUni.RangeMaterialFlags[1] = Span.BvhNodeCount;
                        std::memcpy(UniHost + OffBvh, &BvhUni, sizeof(BvhUni));
                        const std::array<ComputeSlotDescriptor, 5u> Slots {{
                            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, InstanceTransformUniformBuffer.BufferHandle, sizeof(BvhUni), OffBvh },
                            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ResidentLocalBvhBuffer.BufferHandle,         VK_WHOLE_SIZE },
                            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,                 VK_WHOLE_SIZE },
                            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ResidentLocalTriBuffer.BufferHandle,         VK_WHOLE_SIZE },
                            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle,           VK_WHOLE_SIZE },
                        }};
                        (void)DispatchEncoder.EncodeComputeDispatch(
                            Cmd,
                            InstanceTransformBvhProgramHandle,
                            Slots,
                            (Span.BvhNodeCount + 63u) / 64u,
                            1u,
                            1u);
                    }

                    if (Span.TriangleCount > 0u)
                    {
                        const uint64_t OffTri = (UniSlot++) * UniStride;
                        GpuInstanceTransformUniformStd140 TriUni = BaseUni;
                        TriUni.RangeMaterialFlags[0] = Span.FirstTriangle;
                        TriUni.RangeMaterialFlags[1] = Span.TriangleCount;
                        std::memcpy(UniHost + OffTri, &TriUni, sizeof(TriUni));
                        const std::array<ComputeSlotDescriptor, 5u> Slots {{
                            { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, InstanceTransformUniformBuffer.BufferHandle, sizeof(TriUni), OffTri },
                            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ResidentLocalBvhBuffer.BufferHandle,         VK_WHOLE_SIZE },
                            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,                 VK_WHOLE_SIZE },
                            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ResidentLocalTriBuffer.BufferHandle,         VK_WHOLE_SIZE },
                            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle,           VK_WHOLE_SIZE },
                        }};
                        (void)DispatchEncoder.EncodeComputeDispatch(
                            Cmd,
                            InstanceTransformTriProgramHandle,
                            Slots,
                            (Span.TriangleCount + 63u) / 64u,
                            1u,
                            1u);
                    }
                }

                VkMemoryBarrier DoneBarrier {};
                DoneBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                DoneBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                DoneBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(
                    Cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0u,
                    1u,
                    &DoneBarrier,
                    0u,
                    nullptr,
                    0u,
                    nullptr);

                PendingTransformSignal = BoundDevice->DispatchCommandBufferAsync(Cmd);
                PendingTransformCmd    = Cmd;

                ViewportClusterMaskWords = FinestClusterMaskWords;
                const uint64_t RawWordBytes = static_cast<uint64_t>(FinestClusterMaskWords.size()) * sizeof(uint32_t);
                MaskSliceOffsetBytes = std::max<uint64_t>(256ull, (RawWordBytes + 255ull) & ~255ull);
                EnsureBufferCapacity(ActiveClusterMaskBuffer, MaskSliceOffsetBytes * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                if (ActiveClusterMaskBuffer.HostAddress != nullptr)
                {
                    auto* HostBytes = reinterpret_cast<uint8_t*>(ActiveClusterMaskBuffer.HostAddress);
                    std::memset(HostBytes, 0, static_cast<std::size_t>(MaskSliceOffsetBytes * 2ull));
                    std::memcpy(HostBytes, FinestClusterMaskWords.data(), static_cast<std::size_t>(RawWordBytes));
                    std::memcpy(HostBytes + MaskSliceOffsetBytes, ViewportClusterMaskWords.data(), static_cast<std::size_t>(RawWordBytes));
                }

                CachedResidentTransformSig  = TransformSig;
                CachedNaniteCutSignature    = 0u;
                ViewportMaskHoldsFinestOnly = true;
                return true;
            }
        }
    }

    std::vector<SpatialAcceleration::WideTreeEntry> AllBvhNodes(EffectiveTopReserve + RunningSubNodeOff);
    std::vector<SpatialAcceleration::WideTreeEntry> LocalBvhNodes(EffectiveTopReserve + RunningSubNodeOff);
    std::memcpy(
        AllBvhNodes.data(),
        TopNodes.data(),
        static_cast<std::size_t>(TopNodeCount) * sizeof(SpatialAcceleration::WideTreeEntry));
    std::memcpy(
        LocalBvhNodes.data(),
        TopNodes.data(),
        static_cast<std::size_t>(TopNodeCount) * sizeof(SpatialAcceleration::WideTreeEntry));

    std::vector<GpuShadedTriangle96> AllShadedTris(std::max<uint32_t>(1u, TotalTriangles));
    std::vector<GpuShadedTriangle96> LocalShadedTris(std::max<uint32_t>(1u, TotalTriangles));
    uint32_t CurNodeBase = EffectiveTopReserve;
    uint32_t CurTriBase  = 0u;

    for (ResidentClusterInstanceSpan& Span : ResidentClusterInstances)
    {
        const RigidClusterInstance& Inst      = Scene.ClusterInstances[Span.InstanceIndex];
        const auto&                 Hierarchy = *InstanceHierarchies[Span.InstanceIndex];
        const float ScaleU    = std::max(1.0e-4f, Inst.UniformScale);
        const float ScaleX    = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY    = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ    = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;
        const float InvScaleX = 1.0f / ScaleX;
        const float InvScaleY = 1.0f / ScaleY;
        const float InvScaleZ = 1.0f / ScaleZ;
        const bool  NonUni    = (std::fabs(ScaleX - ScaleY) > 1.0e-4f) || (std::fabs(ScaleX - ScaleZ) > 1.0e-4f);
        const float YawRad    = Inst.RotationZDegrees * Constants::DegToRadFloat;
        const bool  HasYaw    = std::fabs(YawRad) > 1.0e-5f;
        const float CosZ      = HasYaw ? std::cos(YawRad) : 1.0f;
        const float SinZ      = HasYaw ? std::sin(YawRad) : 0.0f;

        Span.FirstBvhNode       = CurNodeBase;
        Span.FirstTriangle      = CurTriBase;
        Span.CachedScaleX       = ScaleX;
        Span.CachedScaleY       = ScaleY;
        Span.CachedScaleZ       = ScaleZ;
        Span.CachedYawDeg       = Inst.RotationZDegrees;
        Span.CachedTranslation  = Inst.WorldTranslation;
        Span.CachedMaterialSlot = static_cast<uint32_t>(Inst.MaterialSlot);

        auto RotZ = [CosZ, SinZ](Mathematics::Axis3D V) noexcept -> Mathematics::Axis3D
        {
            return { V.X * CosZ - V.Y * SinZ, V.X * SinZ + V.Y * CosZ, V.Z };
        };
        auto ScaleVec = [ScaleX, ScaleY, ScaleZ](Mathematics::Axis3D V) noexcept -> Mathematics::Axis3D
        {
            return { V.X * ScaleX, V.Y * ScaleY, V.Z * ScaleZ };
        };
        auto XformNormal = [NonUni, HasYaw, InvScaleX, InvScaleY, InvScaleZ, &RotZ](Mathematics::Axis3D N) noexcept -> Mathematics::Axis3D
        {
            const Mathematics::Axis3D Sn = NonUni
                ? Mathematics::NormalizeAxis({ N.X * InvScaleX, N.Y * InvScaleY, N.Z * InvScaleZ }, { 0.0f, 0.0f, 1.0f })
                : N;
            return HasYaw ? RotZ(Sn) : Sn;
        };

        for (uint32_t Level = 0u; Level < static_cast<uint32_t>(Hierarchy.LevelSubtrees.size()) && Level < 8u; ++Level)
        {
            const auto& Sub = Hierarchy.LevelSubtrees[Level];
            if (Span.LevelGateOrdinals[Level] == UINT32_MAX || Sub.TriangleCount == 0u || Sub.LocalBvh.WideEntries.empty())
            {
                continue;
            }

            const std::size_t SubNodeCnt   = Sub.LocalBvh.WideEntries.size();
            const bool        HasTransGate = (Sub.TransitionLaneClusterGate.size() == SubNodeCnt);
            for (std::size_t NodeOff = 0u; NodeOff < SubNodeCnt; ++NodeOff)
            {
                SpatialAcceleration::WideTreeEntry LocalNode = Sub.LocalBvh.WideEntries[NodeOff];
                SpatialAcceleration::WideTreeEntry DstNode   = LocalNode;
                for (uint32_t Lane = 0u; Lane < 4u; ++Lane)
                {
                    if (DstNode.BranchReference[Lane] == SpatialAcceleration::EmptyBranchReference)
                    {
                        continue;
                    }
                    Mathematics::Axis3D LMin {
                        DstNode.BranchBoundsMinX[Lane],
                        DstNode.BranchBoundsMinY[Lane],
                        DstNode.BranchBoundsMinZ[Lane],
                    };
                    Mathematics::Axis3D LMax {
                        DstNode.BranchBoundsMaxX[Lane],
                        DstNode.BranchBoundsMaxY[Lane],
                        DstNode.BranchBoundsMaxZ[Lane],
                    };
                    Mathematics::Axis3D WMin {}, WMax {};
                    TransformAabbWorld(LMin, LMax, ScaleX, ScaleY, ScaleZ, CosZ, SinZ, Inst.WorldTranslation, WMin, WMax);
                    DstNode.BranchBoundsMinX[Lane] = WMin.X;
                    DstNode.BranchBoundsMaxX[Lane] = WMax.X;
                    DstNode.BranchBoundsMinY[Lane] = WMin.Y;
                    DstNode.BranchBoundsMaxY[Lane] = WMax.Y;
                    DstNode.BranchBoundsMinZ[Lane] = WMin.Z;
                    DstNode.BranchBoundsMaxZ[Lane] = WMax.Z;

                    const uint32_t TriCnt = DstNode.BranchPrimitiveCount[Lane] & 0xFu;
                    if (TriCnt > 0u)
                    {
                        const uint32_t LocalTriStart = DstNode.BranchReference[Lane] & 0x7FFFFFFFu;
                        DstNode.BranchReference[Lane] = SpatialAcceleration::WideLeafReferenceMask | (CurTriBase + LocalTriStart);
                    }
                    else
                    {
                        DstNode.BranchReference[Lane] = CurNodeBase + DstNode.BranchReference[Lane];
                    }

                    uint32_t PackedCnt = TriCnt;
                    if (HasTransGate)
                    {
                        const uint32_t TransClust = Sub.TransitionLaneClusterGate[NodeOff][Lane];
                        if (TransClust < Span.ClusterCount)
                        {
                            const uint32_t GlobalClust = Span.FirstGlobalClusterOrdinal + TransClust;
                            PackedCnt |= ((GlobalClust + 1u) << 4u);
                        }
                    }
                    DstNode.BranchPrimitiveCount[Lane]   = PackedCnt;
                    LocalNode.BranchReference[Lane]      = DstNode.BranchReference[Lane];
                    LocalNode.BranchPrimitiveCount[Lane] = PackedCnt;
                }
                AllBvhNodes[CurNodeBase + NodeOff]   = DstNode;
                LocalBvhNodes[CurNodeBase + NodeOff] = LocalNode;
            }

            const auto* SrcTris = reinterpret_cast<const GpuShadedTriangle96*>(Sub.LocalShadedTriBytes.data());
            for (uint32_t TriIdx = 0u; TriIdx < Sub.TriangleCount; ++TriIdx)
            {
                GpuShadedTriangle96 LocalTri = SrcTris[TriIdx];
                GpuShadedTriangle96 DstTri   = LocalTri;
                const Mathematics::Axis3D LC0  = ScaleVec({ DstTri.Corner0Pad[0], DstTri.Corner0Pad[1], DstTri.Corner0Pad[2] });
                const Mathematics::Axis3D LE01 = ScaleVec({ DstTri.Edge01Pad[0],  DstTri.Edge01Pad[1],  DstTri.Edge01Pad[2] });
                const Mathematics::Axis3D LE02 = ScaleVec({ DstTri.Edge02Pad[0],  DstTri.Edge02Pad[1],  DstTri.Edge02Pad[2] });
                const Mathematics::Axis3D WC0  = Inst.WorldTranslation + (HasYaw ? RotZ(LC0)  : LC0);
                const Mathematics::Axis3D WE01 = HasYaw ? RotZ(LE01) : LE01;
                const Mathematics::Axis3D WE02 = HasYaw ? RotZ(LE02) : LE02;
                const Mathematics::Axis3D WN0  = XformNormal({ DstTri.Normal0Pad[0], DstTri.Normal0Pad[1], DstTri.Normal0Pad[2] });
                const Mathematics::Axis3D WN1  = XformNormal({ DstTri.Normal1Pad[0], DstTri.Normal1Pad[1], DstTri.Normal1Pad[2] });
                const Mathematics::Axis3D WN2  = XformNormal({ DstTri.Normal2Slot[0], DstTri.Normal2Slot[1], DstTri.Normal2Slot[2] });

                const uint32_t LocalMat = static_cast<uint32_t>(DstTri.Normal2Slot[3] + 0.5f);
                const uint32_t FinalMat = (LocalMat != 0u) ? LocalMat : static_cast<uint32_t>(Inst.MaterialSlot);
                const uint32_t LocalClust = (TriIdx < Sub.TriLocalClusterIndices.size())
                    ? Sub.TriLocalClusterIndices[TriIdx]
                    : 0u;
                const uint32_t GlobalOrd   = Span.FirstGlobalClusterOrdinal + LocalClust;
                const float    MaxScaleAbs = std::max({ ScaleX, ScaleY, ScaleZ });
                const uint32_t SrcMetaWord = UnpackSafeBits30FromFloat(DstTri.Corner0Pad[3]);
                const uint32_t LocalGroup  = (SrcMetaWord >> 11u) & 0x7FFFFu;
                const uint32_t GlobalGroup = Span.FirstGlobalClusterOrdinal + LocalGroup;
                const float    PackedMeta  = PackNaniteTriangleMetaWord(
                    SrcMetaWord & 0xFu,
                    (SrcMetaWord >> 4u) & 0xFu,
                    (SrcMetaWord >> 8u) & 0x7u,
                    GlobalGroup);

                LocalTri.Corner0Pad[3] = PackedMeta;
                LocalTri.Edge01Pad[3]  = static_cast<float>(GlobalOrd);
                LocalShadedTris[CurTriBase + TriIdx] = LocalTri;

                DstTri.Corner0Pad[0]  = WC0.X;
                DstTri.Corner0Pad[1]  = WC0.Y;
                DstTri.Corner0Pad[2]  = WC0.Z;
                DstTri.Corner0Pad[3]  = PackedMeta;
                DstTri.Edge01Pad[0]   = WE01.X;
                DstTri.Edge01Pad[1]   = WE01.Y;
                DstTri.Edge01Pad[2]   = WE01.Z;
                DstTri.Edge01Pad[3]   = static_cast<float>(GlobalOrd);
                DstTri.Edge02Pad[0]   = WE02.X;
                DstTri.Edge02Pad[1]   = WE02.Y;
                DstTri.Edge02Pad[2]   = WE02.Z;
                DstTri.Edge02Pad[3]   = DstTri.Edge02Pad[3] * MaxScaleAbs;
                DstTri.Normal0Pad[0]  = WN0.X;
                DstTri.Normal0Pad[1]  = WN0.Y;
                DstTri.Normal0Pad[2]  = WN0.Z;
                DstTri.Normal1Pad[0]  = WN1.X;
                DstTri.Normal1Pad[1]  = WN1.Y;
                DstTri.Normal1Pad[2]  = WN1.Z;
                DstTri.Normal2Slot[0] = WN2.X;
                DstTri.Normal2Slot[1] = WN2.Y;
                DstTri.Normal2Slot[2] = WN2.Z;
                DstTri.Normal2Slot[3] = static_cast<float>(FinalMat);
                AllShadedTris[CurTriBase + TriIdx] = DstTri;
            }

            CurNodeBase += static_cast<uint32_t>(SubNodeCnt);
            CurTriBase  += Sub.TriangleCount;
        }
        Span.BvhNodeCount  = CurNodeBase - Span.FirstBvhNode;
        Span.TriangleCount = CurTriBase  - Span.FirstTriangle;
    }

    if (!UploadDualDeviceBuffers(
            BvhEntryBuffer,
            AllBvhNodes.data(),
            static_cast<uint64_t>(AllBvhNodes.size()) * sizeof(SpatialAcceleration::WideTreeEntry),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            ShadedTriangleBuffer,
            AllShadedTris.data(),
            static_cast<uint64_t>(AllShadedTris.size()) * sizeof(GpuShadedTriangle96),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
    {
        return false;
    }

    (void)UploadDualDeviceBuffers(
        ResidentLocalBvhBuffer,
        LocalBvhNodes.data(),
        static_cast<uint64_t>(LocalBvhNodes.size()) * sizeof(SpatialAcceleration::WideTreeEntry),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        ResidentLocalTriBuffer,
        LocalShadedTris.data(),
        static_cast<uint64_t>(LocalShadedTris.size()) * sizeof(GpuShadedTriangle96),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    ViewportClusterMaskWords = FinestClusterMaskWords;
    const uint64_t RawWordBytes = static_cast<uint64_t>(FinestClusterMaskWords.size()) * sizeof(uint32_t);
    MaskSliceOffsetBytes = std::max<uint64_t>(256ull, (RawWordBytes + 255ull) & ~255ull);
    EnsureBufferCapacity(ActiveClusterMaskBuffer, MaskSliceOffsetBytes * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (ActiveClusterMaskBuffer.HostAddress != nullptr)
    {
        auto* HostBytes = reinterpret_cast<uint8_t*>(ActiveClusterMaskBuffer.HostAddress);
        std::memset(HostBytes, 0, static_cast<std::size_t>(MaskSliceOffsetBytes * 2ull));
        std::memcpy(HostBytes, FinestClusterMaskWords.data(), static_cast<std::size_t>(RawWordBytes));
        std::memcpy(HostBytes + MaskSliceOffsetBytes, ViewportClusterMaskWords.data(), static_cast<std::size_t>(RawWordBytes));
    }

    UploadedBvhCount                     = static_cast<uint32_t>(AllBvhNodes.size());
    UploadedTriangleCount                = static_cast<uint32_t>(AllShadedTris.size());
    UploadedClusterSlotCount             = static_cast<uint32_t>(FinestClusterMaskWords.size() * 32u);
    ReservedTopNodeCount                 = EffectiveTopReserve;
    ResidentFinestTriangleCount          = FinestTriSum;
    CachedResidentTransformSig           = TransformSig;
    CachedResidentTopologySig            = TopologySig;
    CachedNaniteCutSignature             = 0u;
    ResidentClusterBvhActive             = true;
    ViewportMaskHoldsFinestOnly          = true;
    NaniteLodStats.FullTriangleCount     = FinestTriSum;
    NaniteLodStats.GeometryRebuildCount += 1u;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SCENE BUFFER UPLOAD
//------------------------------------------------------------------------------------------------------------------------

bool VulkanWorldRenderSequence::SynchronizeLocalSdfResources(const WorldSceneSpecification& Scene)
{
    if (!QueryReady())
    {
        return false;
    }

    bool ResourcesChanged = false;

    // 1. Collect unique baked LocalSignedDistanceVolume assets across active opaque instances (uploaded once per asset set)
    std::vector<const DistanceFieldSpace::LocalSignedDistanceVolume*> UniqueVolumes;
    UniqueVolumes.reserve(Scene.ClusterInstances.size());
    uint64_t AssetSig = 14695981039346656037ull;
    const auto MixAssetSig = [&AssetSig](uint64_t Val) noexcept
    {
        AssetSig = (AssetSig ^ Val) * 1099511628211ull;
    };

    auto ResolveInstanceVolume = [&](const RigidClusterInstance& Instance) -> const DistanceFieldSpace::LocalSignedDistanceVolume*
    {
        if (Instance.TransparentGlass ||
            Instance.UniformScale <= MinimumValidScaleFloor ||
            Instance.AnalyticalShapeCode == DistanceShapeExcluded)
        {
            return nullptr;
        }
        if (Instance.LocalSdfVolume != nullptr && !Instance.LocalSdfVolume->DistanceSamples.empty())
        {
            return Instance.LocalSdfVolume;
        }
        if (Instance.AnalyticalShapeCode == DistanceShapeBakedVolume &&
            Instance.PyramidAsset != nullptr &&
            !Instance.PyramidAsset->Triangles.empty() &&
            !Instance.PyramidAsset->Positions.empty())
        {
            const float UniformScale = std::max(MinimumValidScaleFloor, Instance.UniformScale);
            const float ScaleX       = (Instance.NonUniformScale.X > MinimumValidScaleFloor) ? Instance.NonUniformScale.X : UniformScale;
            const float ScaleY       = (Instance.NonUniformScale.Y > MinimumValidScaleFloor) ? Instance.NonUniformScale.Y : UniformScale;
            const float ScaleZ       = (Instance.NonUniformScale.Z > MinimumValidScaleFloor) ? Instance.NonUniformScale.Z : UniformScale;
            Mathematics::Axis3D BoundsMinimum {};
            Mathematics::Axis3D BoundsMaximum {};
            (void)ComputePyramidLocalBounds(Instance.PyramidAsset, BoundsMinimum, BoundsMaximum);
            const bool WideGroundDeck =
                (std::fabs(Instance.RotationZDegrees) < MaximumGroundDeckTiltDegrees) &&
                ((BoundsMaximum.X - BoundsMinimum.X) * ScaleX >= MinimumGroundDeckSpanMeters) &&
                ((BoundsMaximum.Y - BoundsMinimum.Y) * ScaleY >= MinimumGroundDeckSpanMeters) &&
                ((BoundsMaximum.Z - BoundsMinimum.Z) * ScaleZ <= MaximumGroundDeckHeightMeters);
            if (!WideGroundDeck)
            {
                return AcquirePyramidDistanceVolume(Instance.PyramidAsset);
            }
        }
        return nullptr;
    };

    for (const RigidClusterInstance& Instance : Scene.ClusterInstances)
    {
        const auto* ResolvedVolume = ResolveInstanceVolume(Instance);
        if (ResolvedVolume == nullptr || ResolvedVolume->DistanceSamples.empty())
        {
            continue;
        }
        bool VolumeAlreadyQueued = false;
        for (const auto* ExistingVolume : UniqueVolumes)
        {
            if (ExistingVolume == ResolvedVolume)
            {
                VolumeAlreadyQueued = true;
                break;
            }
        }
        if (!VolumeAlreadyQueued)
        {
            UniqueVolumes.push_back(ResolvedVolume);
            MixAssetSig(reinterpret_cast<uintptr_t>(ResolvedVolume));
            MixAssetSig(static_cast<uint64_t>(ResolvedVolume->DistanceSamples.size()));
            MixAssetSig((static_cast<uint64_t>(ResolvedVolume->ResolutionX) << PackedUpperWordShift32) |
                        (static_cast<uint64_t>(ResolvedVolume->ResolutionY) << PackedUpperWordShift16) |
                         static_cast<uint64_t>(ResolvedVolume->ResolutionZ));
        }
    }

    if (AssetSig != CachedLocalSdfAssetSignature || LocalAssetSdfVoxelBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        CachedLocalSdfOffsets.clear();
        std::vector<float> PackedVoxels;
        std::size_t TotalFloats = 0u;
        for (const auto* Vol : UniqueVolumes)
        {
            TotalFloats += Vol->DistanceSamples.size();
        }
        PackedVoxels.reserve(std::max<std::size_t>(1u, TotalFloats));
        for (const auto* Vol : UniqueVolumes)
        {
            const uint32_t StartOffset = static_cast<uint32_t>(PackedVoxels.size());
            CachedLocalSdfOffsets.emplace_back(Vol, StartOffset);
            PackedVoxels.insert(PackedVoxels.end(), Vol->DistanceSamples.begin(), Vol->DistanceSamples.end());
        }
        if (PackedVoxels.empty())
        {
            PackedVoxels.push_back(10.0f);
        }
        UploadToDeviceBuffer(
            LocalAssetSdfVoxelBuffer,
            PackedVoxels.data(),
            static_cast<uint64_t>(PackedVoxels.size()) * sizeof(float),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        CachedLocalSdfAssetSignature = AssetSig;
        DistanceFieldStats.LocalAssetVoxelUploadCount += 1u;
        ResourcesChanged = true;
    }

    // ② Upload compact per-instance dynamic rigid/scale transform array (< 0.02 ms even during gizmo drag)
    std::vector<GpuDynamicLocalSdfInstance144> GpuInstances;
    GpuInstances.reserve(std::max<std::size_t>(1u, Scene.ClusterInstances.size()));
    float    NextAnalyticGroundZ            = UnsetAnalyticGroundPlaneZ;
    uint32_t NextAnalyticGroundMaterialSlot = 0u;

    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (Inst.TransparentGlass ||
            Inst.UniformScale <= MinimumValidScaleFloor ||
            Inst.AnalyticalShapeCode == DistanceShapeExcluded)
        {
            continue;
        }
        const auto*    ResolvedVolume = ResolveInstanceVolume(Inst);
        const bool     VolumePresent  = (ResolvedVolume != nullptr && !ResolvedVolume->DistanceSamples.empty());
        const uint32_t ShapeCode      = VolumePresent
            ? DistanceShapeBakedVolume
            : ((Inst.AnalyticalShapeCode > DistanceShapeBakedVolume) ? Inst.AnalyticalShapeCode : DistanceShapeBox);

        uint32_t VoxelOffset = 0u;
        if (VolumePresent)
        {
            for (const auto& OffsetPair : CachedLocalSdfOffsets)
            {
                if (OffsetPair.first == ResolvedVolume)
                {
                    VoxelOffset = OffsetPair.second;
                    break;
                }
            }
        }

        const float ScaleU        = std::max(MinimumValidScaleFloor, Inst.UniformScale);
        const float ScaleX        = (Inst.NonUniformScale.X > MinimumValidScaleFloor) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY        = (Inst.NonUniformScale.Y > MinimumValidScaleFloor) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ        = (Inst.NonUniformScale.Z > MinimumValidScaleFloor) ? Inst.NonUniformScale.Z : ScaleU;
        const float MinWorldScale = std::min({ ScaleX, ScaleY, ScaleZ });
        const float InvScaleX     = 1.0f / ScaleX;
        const float InvScaleY     = 1.0f / ScaleY;
        const float InvScaleZ     = 1.0f / ScaleZ;
        const float YawRad        = Inst.RotationZDegrees * Constants::DegToRadFloat;
        const float CosZ          = std::cos(YawRad);
        const float SinZ          = std::sin(YawRad);
        const Mathematics::Axis3D T = Inst.WorldTranslation;

        // World = T + Rz * diag(ScaleX, ScaleY, ScaleZ) * Local
        // => Local = diag(1/ScaleX, 1/ScaleY, 1/ScaleZ) * Rz^T * (World - T)
        GpuDynamicLocalSdfInstance144 Dst {};
        Dst.WorldToLocalRow0[0] =  CosZ * InvScaleX;
        Dst.WorldToLocalRow0[1] =  SinZ * InvScaleX;
        Dst.WorldToLocalRow0[2] =  0.0f;
        Dst.WorldToLocalRow0[3] = -(Dst.WorldToLocalRow0[0] * T.X + Dst.WorldToLocalRow0[1] * T.Y);

        Dst.WorldToLocalRow1[0] = -SinZ * InvScaleY;
        Dst.WorldToLocalRow1[1] =  CosZ * InvScaleY;
        Dst.WorldToLocalRow1[2] =  0.0f;
        Dst.WorldToLocalRow1[3] = -(Dst.WorldToLocalRow1[0] * T.X + Dst.WorldToLocalRow1[1] * T.Y);

        Dst.WorldToLocalRow2[0] =  0.0f;
        Dst.WorldToLocalRow2[1] =  0.0f;
        Dst.WorldToLocalRow2[2] =  InvScaleZ;
        Dst.WorldToLocalRow2[3] = -T.Z * InvScaleZ;

        Dst.LocalToWorldNormRow0[0] =  CosZ * InvScaleX;
        Dst.LocalToWorldNormRow0[1] = -SinZ * InvScaleY;
        Dst.LocalToWorldNormRow0[2] =  0.0f;
        Dst.LocalToWorldNormRow0[3] =  MinWorldScale;

        Dst.LocalToWorldNormRow1[0] =  SinZ * InvScaleX;
        Dst.LocalToWorldNormRow1[1] =  CosZ * InvScaleY;
        Dst.LocalToWorldNormRow1[2] =  0.0f;
        Dst.LocalToWorldNormRow1[3] =  VolumePresent ? ResolvedVolume->VoxelSpacing.Z : 0.0f;

        Dst.LocalToWorldNormRow2[0] =  0.0f;
        Dst.LocalToWorldNormRow2[1] =  0.0f;
        Dst.LocalToWorldNormRow2[2] =  InvScaleZ;
        Dst.LocalToWorldNormRow2[3] =  0.0f;

        bool AnalyticGroundSlab = false;
        if (VolumePresent)
        {
            const auto& Volume = *ResolvedVolume;
            Dst.LocalMinPad[0]      = Volume.LocalMin.X;
            Dst.LocalMinPad[1]      = Volume.LocalMin.Y;
            Dst.LocalMinPad[2]      = Volume.LocalMin.Z;
            Dst.LocalMinPad[3]      = Volume.VoxelSpacing.X;
            Dst.LocalMaxPad[0]      = Volume.LocalMax.X;
            Dst.LocalMaxPad[1]      = Volume.LocalMax.Y;
            Dst.LocalMaxPad[2]      = Volume.LocalMax.Z;
            Dst.LocalMaxPad[3]      = Volume.VoxelSpacing.Y;
            Dst.ResolutionOffset[0] =
                (Volume.ResolutionX & PackedResolutionMask16) |
                ((Volume.ResolutionY & PackedResolutionMask16) << PackedUpperWordShift16);
            Dst.ResolutionOffset[1] = (Volume.ResolutionZ & PackedResolutionMask16);
        }
        else
        {
            Mathematics::Axis3D BoxMin {};
            Mathematics::Axis3D BoxMax {};
            if (Inst.AnalyticalShapeCode > DistanceShapeBakedVolume &&
                Inst.LocalHalfExtents.X > MinimumValidScaleFloor &&
                Inst.LocalHalfExtents.Y > MinimumValidScaleFloor &&
                Inst.LocalHalfExtents.Z > MinimumValidScaleFloor)
            {
                BoxMin = { -Inst.LocalHalfExtents.X, -Inst.LocalHalfExtents.Y, -Inst.LocalHalfExtents.Z };
                BoxMax = Inst.LocalHalfExtents;
            }
            else if (!ComputePyramidLocalBounds(Inst.PyramidAsset, BoxMin, BoxMax))
            {
                BoxMin = { -FallbackLocalHalfExtentMeters, -FallbackLocalHalfExtentMeters, -FallbackLocalHalfExtentMeters };
                BoxMax = {  FallbackLocalHalfExtentMeters,  FallbackLocalHalfExtentMeters,  FallbackLocalHalfExtentMeters };
            }
            Dst.LocalMinPad[0]      = BoxMin.X;
            Dst.LocalMinPad[1]      = BoxMin.Y;
            Dst.LocalMinPad[2]      = BoxMin.Z;
            Dst.LocalMinPad[3]      = 0.0f;
            Dst.LocalMaxPad[0]      = BoxMax.X;
            Dst.LocalMaxPad[1]      = BoxMax.Y;
            Dst.LocalMaxPad[2]      = BoxMax.Z;
            Dst.LocalMaxPad[3]      = 0.0f;
            Dst.ResolutionOffset[0] = 0u;
            Dst.ResolutionOffset[1] = (ShapeCode & PackedResolutionMask16) << PackedUpperWordShift16;

            // 💡 Separate horizontal ground slabs from non-ground clipmap voxels so open floors have zero self-darkening
            AnalyticGroundSlab =
                (ShapeCode == DistanceShapeBox) &&
                (std::fabs(Inst.RotationZDegrees) < MaximumGroundDeckTiltDegrees) &&
                ((BoxMax.X - BoxMin.X) * ScaleX >= MinimumGroundDeckSpanMeters) &&
                ((BoxMax.Y - BoxMin.Y) * ScaleY >= MinimumGroundDeckSpanMeters) &&
                ((BoxMax.Z - BoxMin.Z) * ScaleZ <= MaximumGroundDeckHeightMeters);
            if (AnalyticGroundSlab)
            {
                const float SlabTopZ = T.Z + BoxMax.Z * ScaleZ;
                if (NextAnalyticGroundZ < ValidAnalyticGroundFloorZ || SlabTopZ > NextAnalyticGroundZ)
                {
                    NextAnalyticGroundZ            = SlabTopZ;
                    NextAnalyticGroundMaterialSlot = static_cast<uint32_t>(Inst.MaterialSlot);
                }
            }
        }
        Dst.ResolutionOffset[2] = VoxelOffset;
        Dst.ResolutionOffset[3] =
            (static_cast<uint32_t>(Inst.MaterialSlot) & InstanceMaterialSlotMask) |
            (1u << InstanceActiveBitShift) |
            (AnalyticGroundSlab ? (1u << InstanceGroundSlabBitShift) : 0u);
        GpuInstances.push_back(Dst);
    }

    if (std::fabs(NextAnalyticGroundZ - ActiveAnalyticGroundPlaneZ) > DatumChangeToleranceMeters ||
        NextAnalyticGroundMaterialSlot != ActiveAnalyticGroundMaterialSlot)
    {
        ActiveAnalyticGroundPlaneZ       = NextAnalyticGroundZ;
        ActiveAnalyticGroundMaterialSlot = NextAnalyticGroundMaterialSlot;
        ResourcesChanged                 = true;
    }

    uint64_t DynSig = 14695981039346656037ull;
    {
        const auto* RawBytes = reinterpret_cast<const uint8_t*>(GpuInstances.data());
        const std::size_t TotalBytes = GpuInstances.size() * sizeof(GpuDynamicLocalSdfInstance144);
        for (std::size_t ByteIdx = 0u; ByteIdx < TotalBytes; ++ByteIdx)
        {
            DynSig ^= static_cast<uint64_t>(RawBytes[ByteIdx]);
            DynSig *= 1099511628211ull;
        }
    }

    UploadedLocalSdfInstanceCount = static_cast<uint32_t>(GpuInstances.size());
    if (DynSig != CachedDynamicSdfSignature || DynamicLocalSdfInstanceBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        if (GpuInstances.empty())
        {
            GpuInstances.emplace_back();
        }
        UploadToDeviceBuffer(
            DynamicLocalSdfInstanceBuffer,
            GpuInstances.data(),
            static_cast<uint64_t>(GpuInstances.size()) * sizeof(GpuDynamicLocalSdfInstance144),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        CachedDynamicSdfSignature = DynSig;
        DistanceFieldStats.DynamicInstanceUploadCount += 1u;
        ResourcesChanged = true;
    }
    if (ResourcesChanged)
    {
        PendingFineGeometryReset   = true;
        PendingCoarseGeometryReset = true;
    }
    return ResourcesChanged;
}

Mathematics::Axis3D VulkanWorldRenderSequence::ResolveDynamicClipFocusWorld(
    const PerspectiveCameraBasis*  OptionalCamera,
    const WorldSceneSpecification& Scene) const noexcept
{
    Mathematics::Axis3D FallbackCenter = (Scene.SceneTree.RootBoundsMin + Scene.SceneTree.RootBoundsMax) * 0.5f;

    // Anchor to movable non-floor foreground assets when no live camera is supplied,
    // or blend surface-projected camera focus with foreground assets so moving either the camera
    // OR a hero asset shifts the voxel-snapped SDF clipmap smoothly without detaching into mid-air
    // during forward/backward dolly or yaw/pitch camera-angle changes.
    Mathematics::Axis3D ForegroundSum { 0.0f, 0.0f, 0.0f };
    float               ForegroundWeight = 0.0f;
    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f)
        {
            continue;
        }
        const float MaxHalf = std::max({ Inst.LocalHalfExtents.X, Inst.LocalHalfExtents.Y, Inst.LocalHalfExtents.Z }) * Inst.UniformScale;
        if (MaxHalf > 8.0f)
        {
            continue; // Exclude 24 m ground slab from pinning the fine clipmap center to world origin
        }
        const float Weight = (Inst.LocalSdfVolume != nullptr) ? 4.0f : 1.0f;
        ForegroundSum      = ForegroundSum + Inst.WorldTranslation * Weight;
        ForegroundWeight  += Weight;
    }
    if (ForegroundWeight > 0.0f)
    {
        FallbackCenter = ForegroundSum * (1.0f / ForegroundWeight);
    }

    if (OptionalCamera == nullptr)
    {
        return FallbackCenter;
    }

    const Mathematics::Axis3D SceneMin   = Scene.SceneTree.RootBoundsMin;
    const Mathematics::Axis3D SceneMax   = Scene.SceneTree.RootBoundsMax;
    const Mathematics::Axis3D BoundsSpan = SceneMax - SceneMin;
    const float SceneMaxSpan = std::max({ std::abs(BoundsSpan.X), std::abs(BoundsSpan.Y), std::abs(BoundsSpan.Z), 12.0f });
    const float AnchorPlaneZ = (ForegroundWeight > 0.0f) ? FallbackCenter.Z : SceneMin.Z;

    const Mathematics::Axis3D ToTarget = OptionalCamera->TargetPoint - OptionalCamera->CameraOrigin;
    const float TargetDist = std::sqrt(std::max(0.0f, Mathematics::EvaluateDot(ToTarget, ToTarget)));
    const Mathematics::Axis3D ViewDir = (TargetDist > 1.0e-4f)
        ? (ToTarget * (1.0f / TargetDist))
        : Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f };

    // Project the camera view ray onto the active scene/foreground datum plane instead of clamping
    // a fixed look-ahead distance in mid-air: dollying forward/back keeps the fine clipmap locked
    // on the viewed surface, and pitching upward never launches the fine cascade into empty sky.
    const float MaxFocusDist = std::clamp(SceneMaxSpan * 0.85f, 14.0f, 40.0f);
    Mathematics::Axis3D CameraFocus = OptionalCamera->TargetPoint;
    if (ViewDir.Z < -0.05f && OptionalCamera->CameraOrigin.Z > AnchorPlaneZ + 0.05f)
    {
        const float PlaneHitDist = std::clamp(
            (AnchorPlaneZ - OptionalCamera->CameraOrigin.Z) / ViewDir.Z,
            1.5f,
            MaxFocusDist);
        CameraFocus = OptionalCamera->CameraOrigin + ViewDir * PlaneHitDist;
    }
    else if (ForegroundWeight > 0.0f)
    {
        const Mathematics::Axis3D ToForeground = FallbackCenter - OptionalCamera->CameraOrigin;
        const float AlongForeground = Mathematics::EvaluateDot(ToForeground, ViewDir);
        const float FocusDist = (AlongForeground > -3.0f && AlongForeground < MaxFocusDist)
            ? std::clamp(AlongForeground, 1.5f, 16.0f)
            : std::clamp(TargetDist, 2.2f, 11.5f);
        CameraFocus   = OptionalCamera->CameraOrigin + ViewDir * FocusDist;
        CameraFocus.Z = AnchorPlaneZ;
    }
    else
    {
        CameraFocus   = OptionalCamera->CameraOrigin + ViewDir * std::clamp(TargetDist, 2.2f, 11.5f);
        CameraFocus.Z = AnchorPlaneZ;
    }

    // When a foreground hero asset sits near the camera focus or inside the near view cone,
    // keep the fine clipmap anchored across camera-angle rotations while still responding to camera pan.
    if (ForegroundWeight > 0.0f)
    {
        const Mathematics::Axis3D Delta = FallbackCenter - CameraFocus;
        const float DistToForeground = std::sqrt(std::max(0.0f, Mathematics::EvaluateDot(Delta, Delta)));
        float Blend = (DistToForeground < 14.0f) ? (0.55f * (1.0f - DistToForeground / 14.0f)) : 0.0f;

        const Mathematics::Axis3D ToFg = FallbackCenter - OptionalCamera->CameraOrigin;
        const float CamToFgDist = std::sqrt(std::max(0.0f, Mathematics::EvaluateDot(ToFg, ToFg)));
        const float AlongFg     = Mathematics::EvaluateDot(ToFg, ViewDir);
        if (CamToFgDist < 12.0f && AlongFg > -2.5f)
        {
            const Mathematics::Axis3D PerpVec = ToFg - ViewDir * std::max(0.0f, AlongFg);
            const float RayPerpDist = std::sqrt(std::max(0.0f, Mathematics::EvaluateDot(PerpVec, PerpVec)));
            if (RayPerpDist < 5.5f)
            {
                const float AngleStableBlend = 0.72f * (1.0f - RayPerpDist / 5.5f);
                Blend = std::max(Blend, AngleStableBlend);
            }
        }
        if (Blend > 0.0f)
        {
            CameraFocus = CameraFocus * (1.0f - Blend) + FallbackCenter * Blend;
        }
    }

    if (SceneMax.X > SceneMin.X + 0.5f)
    {
        CameraFocus.X = std::clamp(CameraFocus.X, SceneMin.X, SceneMax.X);
        CameraFocus.Y = std::clamp(CameraFocus.Y, SceneMin.Y, SceneMax.Y);
        CameraFocus.Z = std::clamp(CameraFocus.Z, SceneMin.Z - 1.0f, SceneMax.Z + 3.5f);
    }
    return CameraFocus;
}

bool VulkanWorldRenderSequence::DispatchGpuScenePrecompute(
    const WorldSceneSpecification& Scene,
    bool                           RebuildGeometryFields,
    uint32_t                       UpdateMask)
{
    if (!QueryReady() ||
        ShadowCascadeProgramHandle == UINT32_MAX ||
        PrecomputeUniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    if (RebuildGeometryFields && !Scene.ClusterInstances.empty())
    {
        (void)SynchronizeLocalSdfResources(Scene);
    }

    uint32_t EffectiveMask = UpdateMask;
    if (!CascadeFocusInitialized || !GlobalDistanceFieldReady)
    {
        EffectiveMask = kPrecomputeFullMask;
    }
    if (!RebuildGeometryFields && GlobalDistanceFieldReady)
    {
        EffectiveMask &= ~(kPrecomputeFineGdf | kPrecomputeCoarseGdf);
    }
    if (EffectiveMask == 0u)
    {
        return true;
    }
    if ((EffectiveMask & kPrecomputeRadiosity) != 0u)
    {
        PendingRadiosityRefresh = false;
    }

    const bool AtmoActive = Scene.Atmosphere.Medium.AtmosphereEnabled || Scene.Atmosphere.Medium.OrbitalViewEnabled;
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
    const Mathematics::Axis3D SunDir = AtmoActive
        ? SolarSolved.WorldDirectionToSun
        : Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });
    LastSyncedSunDir = SunDir;

    const Mathematics::Axis3D BoundsSpan  = Scene.SceneTree.RootBoundsMax - Scene.SceneTree.RootBoundsMin;
    const Mathematics::Axis3D SceneCenter = (Scene.SceneTree.RootBoundsMin + Scene.SceneTree.RootBoundsMax) * 0.5f;
    const float MaxHalfExtent = 0.5f * std::max({ std::abs(BoundsSpan.X), std::abs(BoundsSpan.Y), std::abs(BoundsSpan.Z), 5.6f });
    const float FineGdfHalf   = std::round(std::clamp(MaxHalfExtent * 1.18f, 3.25f, 4.25f) * 8.0f) * 0.125f;
    const float CoarseGdfHalf = std::round(std::clamp(MaxHalfExtent * 1.15f, FineGdfHalf * 2.5f, 24.0f) * 8.0f) * 0.125f;
    const float FineVoxelM    = (2.0f * FineGdfHalf) / 64.0f;
    const float CoarseVoxelM  = (2.0f * CoarseGdfHalf) / 64.0f;

    const Mathematics::Axis3D SceneObjectAnchor = ResolveDynamicClipFocusWorld(nullptr, Scene);
    const Mathematics::Axis3D FocusCenter = ActiveClipFocusInitialized
        ? ActiveClipFocusWorld
        : SceneObjectAnchor;
    constexpr float FineSunCascadeHalfSpanMeters = 6.00f;
    const float FocusFromObjectAnchor = std::max({
        std::abs(FocusCenter.X - SceneObjectAnchor.X),
        std::abs(FocusCenter.Y - SceneObjectAnchor.Y),
        std::abs(FocusCenter.Z - SceneObjectAnchor.Z),
    });
    const Mathematics::Axis3D FineShadowAnchor =
        (FocusFromObjectAnchor <= FineSunCascadeHalfSpanMeters * 0.55f) ? SceneObjectAnchor : FocusCenter;
    const Mathematics::Axis3D CoarseGdfAnchor = (MaxHalfExtent <= CoarseGdfHalf) ? SceneCenter : FocusCenter;
    if (!CascadeFocusInitialized)
    {
        LastFineShadowFocusWorld   = FineShadowAnchor;
        LastCoarseShadowFocusWorld = SceneCenter;
        LastFineGdfFocusWorld      = FocusCenter;
        LastCoarseGdfFocusWorld    = CoarseGdfAnchor;
        CascadeFocusInitialized    = true;
    }
    else
    {
        if ((EffectiveMask & kPrecomputeFineShadow) != 0u)
        {
            LastFineShadowFocusWorld = FineShadowAnchor;
        }
        if ((EffectiveMask & kPrecomputeCoarseShadow) != 0u)
        {
            LastCoarseShadowFocusWorld = SceneCenter;
        }
        if ((EffectiveMask & kPrecomputeFineGdf) != 0u)
        {
            LastFineGdfFocusWorld = FocusCenter;
        }
        if ((EffectiveMask & kPrecomputeCoarseGdf) != 0u)
        {
            LastCoarseGdfFocusWorld = CoarseGdfAnchor;
        }
    }

    struct ToroidalSlabBox
    {
        uint32_t MinCoord[3] { 0u, 0u, 0u };
        uint32_t MaxCoord[3] { 64u, 64u, 64u };
    };

    auto SnapMinCellStep4 = [](
        float   FocusMeters,
        float   VoxelMeters,
        int32_t PrevMinCell,
        bool    ForceSnap) noexcept -> int32_t
    {
        const float PrevCenterMeters = (static_cast<float>(PrevMinCell) + 32.0f) * VoxelMeters;
        if (!ForceSnap && std::abs(FocusMeters - PrevCenterMeters) < VoxelMeters * 4.35f)
        {
            return PrevMinCell;
        }
        const float   StepMeters = VoxelMeters * 4.0f;
        const int32_t CenterCell = static_cast<int32_t>(std::lround(FocusMeters / StepMeters)) * 4;
        return CenterCell - 32;
    };

    auto ConstructToroidalDirtySlabs = [](
        const int32_t                  PrevMin[3],
        const int32_t                  NextMin[3],
        bool                           ForceFullBox,
        std::array<ToroidalSlabBox, 3>& OutSlabs,
        uint32_t&                      OutVoxelCount) noexcept -> uint32_t
    {
        if (ForceFullBox)
        {
            OutSlabs[0]   = ToroidalSlabBox{ { 0u, 0u, 0u }, { 64u, 64u, 64u } };
            OutVoxelCount = 262144u;
            return 1u;
        }

        const int32_t Delta[3] {
            NextMin[0] - PrevMin[0],
            NextMin[1] - PrevMin[1],
            NextMin[2] - PrevMin[2],
        };
        if (std::abs(Delta[0]) >= 64 || std::abs(Delta[1]) >= 64 || std::abs(Delta[2]) >= 64)
        {
            OutSlabs[0]   = ToroidalSlabBox{ { 0u, 0u, 0u }, { 64u, 64u, 64u } };
            OutVoxelCount = 262144u;
            return 1u;
        }

        uint32_t KeepMin[3]  { 0u, 0u, 0u };
        uint32_t KeepMax[3]  { 64u, 64u, 64u };
        uint32_t DirtyMin[3] { 0u, 0u, 0u };
        uint32_t DirtyMax[3] { 0u, 0u, 0u };
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            if (Delta[Axis] > 0)
            {
                const uint32_t Shift = static_cast<uint32_t>(Delta[Axis]);
                KeepMin[Axis]  = 0u;
                KeepMax[Axis]  = 64u - Shift;
                DirtyMin[Axis] = 64u - Shift;
                DirtyMax[Axis] = 64u;
            }
            else if (Delta[Axis] < 0)
            {
                const uint32_t Shift = static_cast<uint32_t>(-Delta[Axis]);
                KeepMin[Axis]  = Shift;
                KeepMax[Axis]  = 64u;
                DirtyMin[Axis] = 0u;
                DirtyMax[Axis] = Shift;
            }
        }

        uint32_t SlabCount = 0u;
        OutVoxelCount      = 0u;
        auto AppendSlab = [&](uint32_t X0, uint32_t X1, uint32_t Y0, uint32_t Y1, uint32_t Z0, uint32_t Z1)
        {
            if (X1 > X0 && Y1 > Y0 && Z1 > Z0)
            {
                OutSlabs[SlabCount++] = ToroidalSlabBox{ { X0, Y0, Z0 }, { X1, Y1, Z1 } };
                OutVoxelCount += (X1 - X0) * (Y1 - Y0) * (Z1 - Z0);
            }
        };

        if (Delta[0] != 0)
        {
            AppendSlab(DirtyMin[0], DirtyMax[0], 0u, 64u, 0u, 64u);
        }
        if (Delta[1] != 0)
        {
            AppendSlab(KeepMin[0], KeepMax[0], DirtyMin[1], DirtyMax[1], 0u, 64u);
        }
        if (Delta[2] != 0)
        {
            AppendSlab(KeepMin[0], KeepMax[0], KeepMin[1], KeepMax[1], DirtyMin[2], DirtyMax[2]);
        }
        return SlabCount;
    };

    const bool DoFineGdfUpdate   = ((EffectiveMask & kPrecomputeFineGdf) != 0u) && (GlobalDistanceBakeProgramHandle != UINT32_MAX);
    const bool DoCoarseGdfUpdate = ((EffectiveMask & kPrecomputeCoarseGdf) != 0u) && (GlobalDistanceBakeProgramHandle != UINT32_MAX);
    const bool FullFineReset =
        PendingFineGeometryReset ||
        !FineToroidalValid ||
        !GlobalDistanceFieldReady ||
        (std::abs(FineGdfHalf - CachedFineGdfCenterSpan[3]) > 1.0e-4f);
    const bool FullCoarseReset =
        PendingCoarseGeometryReset ||
        !CoarseToroidalValid ||
        !GlobalDistanceFieldReady ||
        (std::abs(CoarseGdfHalf - CachedCoarseGdfCenterSpan[3]) > 1.0e-4f);

    std::array<ToroidalSlabBox, 3u> FineSlabs {};
    std::array<ToroidalSlabBox, 3u> CoarseSlabs {};
    uint32_t FineSlabCount    = 0u;
    uint32_t CoarseSlabCount  = 0u;
    uint32_t FineVoxWritten   = 0u;
    uint32_t CoarseVoxWritten = 0u;

    if (DoFineGdfUpdate)
    {
        const int32_t NextFineMin[3] {
            SnapMinCellStep4(LastFineGdfFocusWorld.X, FineVoxelM, CachedFineToroidalMinCell[0], FullFineReset),
            SnapMinCellStep4(LastFineGdfFocusWorld.Y, FineVoxelM, CachedFineToroidalMinCell[1], FullFineReset),
            SnapMinCellStep4(LastFineGdfFocusWorld.Z, FineVoxelM, CachedFineToroidalMinCell[2], FullFineReset),
        };
        FineSlabCount = ConstructToroidalDirtySlabs(
            CachedFineToroidalMinCell,
            NextFineMin,
            FullFineReset,
            FineSlabs,
            FineVoxWritten);
        CachedFineToroidalMinCell[0] = NextFineMin[0];
        CachedFineToroidalMinCell[1] = NextFineMin[1];
        CachedFineToroidalMinCell[2] = NextFineMin[2];
        CachedFineToroidalMinCell[3] = 0;
        FineToroidalValid            = true;
        PendingFineGeometryReset     = false;
    }

    if (DoCoarseGdfUpdate)
    {
        const int32_t NextCoarseMin[3] {
            SnapMinCellStep4(LastCoarseGdfFocusWorld.X, CoarseVoxelM, CachedCoarseToroidalMinCell[0], FullCoarseReset),
            SnapMinCellStep4(LastCoarseGdfFocusWorld.Y, CoarseVoxelM, CachedCoarseToroidalMinCell[1], FullCoarseReset),
            SnapMinCellStep4(LastCoarseGdfFocusWorld.Z, CoarseVoxelM, CachedCoarseToroidalMinCell[2], FullCoarseReset),
        };
        CoarseSlabCount = ConstructToroidalDirtySlabs(
            CachedCoarseToroidalMinCell,
            NextCoarseMin,
            FullCoarseReset,
            CoarseSlabs,
            CoarseVoxWritten);
        CachedCoarseToroidalMinCell[0] = NextCoarseMin[0];
        CachedCoarseToroidalMinCell[1] = NextCoarseMin[1];
        CachedCoarseToroidalMinCell[2] = NextCoarseMin[2];
        CachedCoarseToroidalMinCell[3] = 0;
        CoarseToroidalValid            = true;
        PendingCoarseGeometryReset     = false;
    }

    CachedFineGdfCenterSpan[0]   = (static_cast<float>(CachedFineToroidalMinCell[0]) + 32.0f) * FineVoxelM;
    CachedFineGdfCenterSpan[1]   = (static_cast<float>(CachedFineToroidalMinCell[1]) + 32.0f) * FineVoxelM;
    CachedFineGdfCenterSpan[2]   = (static_cast<float>(CachedFineToroidalMinCell[2]) + 32.0f) * FineVoxelM;
    CachedFineGdfCenterSpan[3]   = FineGdfHalf;
    CachedCoarseGdfCenterSpan[0] = (static_cast<float>(CachedCoarseToroidalMinCell[0]) + 32.0f) * CoarseVoxelM;
    CachedCoarseGdfCenterSpan[1] = (static_cast<float>(CachedCoarseToroidalMinCell[1]) + 32.0f) * CoarseVoxelM;
    CachedCoarseGdfCenterSpan[2] = (static_cast<float>(CachedCoarseToroidalMinCell[2]) + 32.0f) * CoarseVoxelM;
    CachedCoarseGdfCenterSpan[3] = CoarseGdfHalf;

    DistanceFieldStats.FineCascadeCenterWorld[0]   = CachedFineGdfCenterSpan[0];
    DistanceFieldStats.FineCascadeCenterWorld[1]   = CachedFineGdfCenterSpan[1];
    DistanceFieldStats.FineCascadeCenterWorld[2]   = CachedFineGdfCenterSpan[2];
    DistanceFieldStats.FineCascadeHalfSpanMeters   = FineGdfHalf;
    DistanceFieldStats.CoarseCascadeCenterWorld[0] = CachedCoarseGdfCenterSpan[0];
    DistanceFieldStats.CoarseCascadeCenterWorld[1] = CachedCoarseGdfCenterSpan[1];
    DistanceFieldStats.CoarseCascadeCenterWorld[2] = CachedCoarseGdfCenterSpan[2];
    DistanceFieldStats.CoarseCascadeHalfSpanMeters = CoarseGdfHalf;

    const Mathematics::Axis3D LightFwd    = SunDir * -1.0f;
    Mathematics::Axis3D RightAxis {};
    Mathematics::Axis3D UpAxis    {};
    GeometrySpace::ConstructDuffOrthonormalBasis(LightFwd, RightAxis, UpAxis);

    auto InitCascadeBasis = [&](
        OrthographicShadowCascade& Cascade,
        const Mathematics::Axis3D& AnchorWorld,
        float                      HalfSpanMeters,
        uint32_t                   ResolutionTexels)
    {
        Cascade.HalfSpanMeters    = HalfSpanMeters;
        Cascade.DepthExtentMeters = 28.0f;
        Cascade.ResolutionTexels  = ResolutionTexels;
        Cascade.LightRightAxis    = RightAxis;
        Cascade.LightUpAxis       = UpAxis;
        Cascade.LightForwardAxis  = LightFwd;
        const float TexelSizeM = (2.0f * HalfSpanMeters) / static_cast<float>(Cascade.ResolutionTexels);
        const float ProjU      = std::round(Mathematics::EvaluateDot(AnchorWorld, RightAxis) / TexelSizeM) * TexelSizeM;
        const float ProjV      = std::round(Mathematics::EvaluateDot(AnchorWorld, UpAxis) / TexelSizeM) * TexelSizeM;
        const float ProjZ      = Mathematics::EvaluateDot(AnchorWorld, LightFwd);
        Cascade.CenterWorld    = RightAxis * ProjU + UpAxis * ProjV + LightFwd * ProjZ;
        Cascade.ClosestLightDepth.clear();
    };
    const uint32_t FineShadowRes = QueryHardwareGpuActive() ? 2048u : 1024u;
    InitCascadeBasis(CachedFineSunCascade, LastFineShadowFocusWorld, FineSunCascadeHalfSpanMeters, FineShadowRes);
    InitCascadeBasis(CachedSunCascade, LastCoarseShadowFocusWorld, std::clamp(MaxHalfExtent * 1.15f, 12.5f, 36.0f), 1024u);
    CoarseShadowOffsetTexels = CachedFineSunCascade.ResolutionTexels * CachedFineSunCascade.ResolutionTexels;

    const uint64_t ShadowDepthTexels =
        static_cast<uint64_t>(CoarseShadowOffsetTexels) +
        static_cast<uint64_t>(CachedSunCascade.ResolutionTexels) * static_cast<uint64_t>(CachedSunCascade.ResolutionTexels);
    const uint64_t ShadowDepthBytes = ShadowDepthTexels * sizeof(float);
    EnsureDeviceBufferCapacity(SolarShadowDepthBuffer, ShadowDepthBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalDistanceFieldBuffer, 2ull * 262144ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalRadiosityFieldBuffer, 299520ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalSurfaceShellBuffer, 262144ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    EnsureDeviceBufferCapacity(OutputPixelBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(AccumulationHdrBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufPosMaskBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufNormAoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufDirectBaseBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufSunContribBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufAlbedoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PingShadowAoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongShadowAoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PingIndirectIrradianceBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongIndirectIrradianceBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(CloudRadiometricBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    Mathematics::LinearRGB EffectiveSunIrrad = Scene.Sun.SolarIrradiance;
    if (AtmoActive)
    {
        const float ObsAlt = Scene.Atmosphere.Medium.ObserverAltitudeMeters;
        const auto SolarTrans = AtmosphericSkySequence::ComputeSolarTransmittance(SunDir, Scene.Atmosphere.Medium, ObsAlt);
        const bool IsOrbital = Scene.Atmosphere.Medium.OrbitalViewEnabled ||
                               (ObsAlt > Scene.Atmosphere.Medium.AtmosphereThicknessMeters);
        const float SunVis     = IsOrbital ? 1.0f : std::clamp((SolarSolved.ElevationDegrees + 4.0f) / 10.0f, 0.0f, 1.0f);
        const float IrradScale = Scene.Atmosphere.Solar.SolarIntensityMultiplier *
                                 Scene.Atmosphere.Solar.DirectGainMultiplier * 0.044f * SunVis;
        EffectiveSunIrrad = Mathematics::LinearRGB{
            SolarSolved.EffectiveSolarTint.R * SolarTrans.R * IrradScale,
            SolarSolved.EffectiveSolarTint.G * SolarTrans.G * IrradScale,
            SolarSolved.EffectiveSolarTint.B * SolarTrans.B * IrradScale,
        };
    }

    WorldViewportUniformBlockStd140 Uniforms {};
    Uniforms.SunDirConeTan[0]           = SunDir.X;
    Uniforms.SunDirConeTan[1]           = SunDir.Y;
    Uniforms.SunDirConeTan[2]           = SunDir.Z;
    Uniforms.SunDirConeTan[3]           = std::tan(std::max(0.014f, Scene.Sun.SunAngularRadiusRadians));
    Uniforms.SolarIrradiancePad[0]      = EffectiveSunIrrad.R;
    Uniforms.SolarIrradiancePad[1]      = EffectiveSunIrrad.G;
    Uniforms.SolarIrradiancePad[2]      = EffectiveSunIrrad.B;
    Uniforms.SolarIrradiancePad[3]      = 5000.0f;
    Uniforms.SkyRadianceBvhCount[0]     = Scene.SkyRadiance.R;
    Uniforms.SkyRadianceBvhCount[1]     = Scene.SkyRadiance.G;
    Uniforms.SkyRadianceBvhCount[2]     = Scene.SkyRadiance.B;
    Uniforms.SkyRadianceBvhCount[3]     = static_cast<float>(UploadedBvhCount);

    const uint32_t LightCount = static_cast<uint32_t>(std::min<std::size_t>(4u, Scene.AreaLights.size()));
    for (uint32_t Idx = 0u; Idx < LightCount; ++Idx)
    {
        const auto& Src = Scene.AreaLights[Idx];
        auto&       Dst = Uniforms.AreaLights[Idx];
        Dst.CenterHalfWidth[0] = Src.CenterWorld.X;
        Dst.CenterHalfWidth[1] = Src.CenterWorld.Y;
        Dst.CenterHalfWidth[2] = Src.CenterWorld.Z;
        Dst.CenterHalfWidth[3] = Src.HalfWidthMeters;
        Dst.RightHalfHeight[0] = Src.RightTangent.X;
        Dst.RightHalfHeight[1] = Src.RightTangent.Y;
        Dst.RightHalfHeight[2] = Src.RightTangent.Z;
        Dst.RightHalfHeight[3] = Src.HalfHeightMeters;
        Dst.UpTwoSided[0]      = Src.UpBitangent.X;
        Dst.UpTwoSided[1]      = Src.UpBitangent.Y;
        Dst.UpTwoSided[2]      = Src.UpBitangent.Z;
        Dst.UpTwoSided[3]      = Src.TwoSidedEmission ? 1.0f : 0.0f;
        Dst.RadiancePacked[0]  = Src.EmittedRadiance.R;
        Dst.RadiancePacked[1]  = Src.EmittedRadiance.G;
        Dst.RadiancePacked[2]  = Src.EmittedRadiance.B;
    }

    Uniforms.LightGlassMaterialDebug[0] = LightCount;
    Uniforms.LightGlassMaterialDebug[2] = UploadedMaterialCount;
    Uniforms.DenoiseStepParams[2]       =
        ((UploadedPhotometricProfiles & 0xFFFFu) << 16u) |
        (UploadedPhotometricLamps & 0xFFFFu);

    Uniforms.FineCascadeParams[0]     = static_cast<float>(CachedFineSunCascade.ResolutionTexels);
    Uniforms.FineCascadeParams[1]     = CachedFineSunCascade.DepthExtentMeters;
    Uniforms.FineCascadeParams[2]     = static_cast<float>(CoarseShadowOffsetTexels);
    Uniforms.FineCascadeCenterSpan[0] = CachedFineSunCascade.CenterWorld.X;
    Uniforms.FineCascadeCenterSpan[1] = CachedFineSunCascade.CenterWorld.Y;
    Uniforms.FineCascadeCenterSpan[2] = CachedFineSunCascade.CenterWorld.Z;
    Uniforms.FineCascadeCenterSpan[3] = CachedFineSunCascade.HalfSpanMeters;

    Uniforms.AtmosphereMaskPad[0] = AtmoActive ? 1u : 0u;
    Uniforms.AtmosphereMaskPad[1] = AtmosphericSkySequence::SkyViewWidthPx;
    Uniforms.AtmosphereMaskPad[2] = AtmosphericSkySequence::SkyViewHeightPx;
    Uniforms.CelestialParams.SolarOpticsParams[2] = Scene.Atmosphere.Medium.ObserverAltitudeMeters;
    Uniforms.CelestialParams.SolarOpticsParams[3] = Scene.Atmosphere.Medium.PlanetRadiusMeters;

    Uniforms.SunCascadeCenterSpan[0] = CachedSunCascade.CenterWorld.X;
    Uniforms.SunCascadeCenterSpan[1] = CachedSunCascade.CenterWorld.Y;
    Uniforms.SunCascadeCenterSpan[2] = CachedSunCascade.CenterWorld.Z;
    Uniforms.SunCascadeCenterSpan[3] = CachedSunCascade.HalfSpanMeters;
    Uniforms.SunCascadeRightRes[0]   = CachedSunCascade.LightRightAxis.X;
    Uniforms.SunCascadeRightRes[1]   = CachedSunCascade.LightRightAxis.Y;
    Uniforms.SunCascadeRightRes[2]   = CachedSunCascade.LightRightAxis.Z;
    Uniforms.SunCascadeRightRes[3]   = static_cast<float>(CachedSunCascade.ResolutionTexels);
    Uniforms.SunCascadeUpDepth[0]    = CachedSunCascade.LightUpAxis.X;
    Uniforms.SunCascadeUpDepth[1]    = CachedSunCascade.LightUpAxis.Y;
    Uniforms.SunCascadeUpDepth[2]    = CachedSunCascade.LightUpAxis.Z;
    Uniforms.SunCascadeUpDepth[3]    = CachedSunCascade.DepthExtentMeters;
    Uniforms.SunCascadeForwardPad[0] = CachedSunCascade.LightForwardAxis.X;
    Uniforms.SunCascadeForwardPad[1] = CachedSunCascade.LightForwardAxis.Y;
    Uniforms.SunCascadeForwardPad[2] = CachedSunCascade.LightForwardAxis.Z;

    std::memcpy(&CachedFineToroidalMinCell[3], &ActiveAnalyticGroundPlaneZ, sizeof(float));
    std::memcpy(&CachedCoarseToroidalMinCell[3], &ActiveAnalyticGroundMaterialSlot, sizeof(uint32_t));
    std::memcpy(Uniforms.GlobalDistanceFineCenterSpan, CachedFineGdfCenterSpan, sizeof(CachedFineGdfCenterSpan));
    std::memcpy(Uniforms.GlobalDistanceCoarseCenterSpan, CachedCoarseGdfCenterSpan, sizeof(CachedCoarseGdfCenterSpan));
    std::memcpy(Uniforms.GlobalDistanceFineToroidalCell, CachedFineToroidalMinCell, sizeof(CachedFineToroidalMinCell));
    std::memcpy(Uniforms.GlobalDistanceCoarseToroidalCell, CachedCoarseToroidalMinCell, sizeof(CachedCoarseToroidalMinCell));
    Uniforms.GlobalDistanceBakeControl[2] = 1u;
    Uniforms.GlobalDistanceBakeControl[3] = (UploadedLocalSdfInstanceCount & 0xFFFFu) << 16u;

    if (!EnsureVolume3DResources())
    {
        return false;
    }

    RetirePendingPrecompute(true);

    constexpr uint64_t UniformStrideBytes = 8192ull;
    EnsureBufferCapacity(PrecomputeUniformBuffer, UniformStrideBytes * 24ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uint8_t* UniformHostBytes = reinterpret_cast<uint8_t*>(PrecomputeUniformBuffer.HostAddress);
    std::memcpy(UniformHostBytes, &Uniforms, sizeof(Uniforms));

    EnsureBufferCapacity(ActiveClusterMaskBuffer, MaskSliceOffsetBytes * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto ConstructPrecomputeSlots = [&](
        uint64_t      UniOffs,
        VkImageView   GdfFineReadView,
        VkImageLayout GdfFineReadLayout,
        VkImageView   GdfCoarseReadView,
        VkImageLayout GdfCoarseReadLayout,
        VkImageView   RadMip0ReadView,
        VkImageLayout RadMip0ReadLayout,
        VkImageView   RadCoarseReadView,
        VkImageLayout RadCoarseReadLayout,
        VkImageView   GdfWriteView,
        VkImageView   RadWriteView) -> std::array<ComputeSlotDescriptor, 39u>
    {
        return {{
            {  0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, PrecomputeUniformBuffer.BufferHandle,         sizeof(WorldViewportUniformBlockStd140), UniOffs },
            {  1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,                  VK_WHOLE_SIZE },
            {  2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle,            VK_WHOLE_SIZE },
            {  3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialEntryBuffer.BufferHandle,             VK_WHOLE_SIZE },
            {  4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputPixelBuffer.BufferHandle,               VK_WHOLE_SIZE },
            {  5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SolarShadowDepthBuffer.BufferHandle,          VK_WHOLE_SIZE },
            {  6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AccumulationHdrBuffer.BufferHandle,           VK_WHOLE_SIZE },
            {  7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AtmosphericSkyViewLutBuffer.BufferHandle,     VK_WHOLE_SIZE },
            {  8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LunarSurfaceAtlasBuffer.BufferHandle,         VK_WHOLE_SIZE },
            {  9u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BakedLensFlareAtlasBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufPosMaskBuffer.BufferHandle,               VK_WHOLE_SIZE },
            { 11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufNormAoBuffer.BufferHandle,                VK_WHOLE_SIZE },
            { 12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufDirectBaseBuffer.BufferHandle,            VK_WHOLE_SIZE },
            { 13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufSunContribBuffer.BufferHandle,            VK_WHOLE_SIZE },
            { 14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufAlbedoBuffer.BufferHandle,                VK_WHOLE_SIZE },
            { 15u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PingShadowAoBuffer.BufferHandle,              VK_WHOLE_SIZE },
            { 16u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PongShadowAoBuffer.BufferHandle,              VK_WHOLE_SIZE },
            { 17u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CloudRadiometricBuffer.BufferHandle,          VK_WHOLE_SIZE },
            { 18u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ProjectedCatalogStarBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 19u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StellarTileBinsBuffer.BufferHandle,           VK_WHOLE_SIZE },
            { 20u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PlanetaryCloudNoiseBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 21u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RawCatalogStarBuffer.BufferHandle,            VK_WHOLE_SIZE },
            { 22u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PhotometricProfileTableBuffer.BufferHandle,   VK_WHOLE_SIZE },
            { 23u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PhotometricLampEntryBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 24u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GlobalDistanceFieldBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 25u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GlobalRadiosityFieldBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 26u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PingIndirectIrradianceBuffer.BufferHandle,    VK_WHOLE_SIZE },
            { 27u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PongIndirectIrradianceBuffer.BufferHandle,    VK_WHOLE_SIZE },
            { 28u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GlobalSurfaceShellBuffer.BufferHandle,        VK_WHOLE_SIZE },
            { 29u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LocalAssetSdfVoxelBuffer.BufferHandle,        VK_WHOLE_SIZE },
            { 30u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DynamicLocalSdfInstanceBuffer.BufferHandle,   VK_WHOLE_SIZE },
            { 31u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ActiveClusterMaskBuffer.BufferHandle,         MaskSliceOffsetBytes, 0ull },
            { 32u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, GdfFineReadView,   LinearRepeatSampler3D, GdfFineReadLayout },
            { 33u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, GdfCoarseReadView, LinearRepeatSampler3D, GdfCoarseReadLayout },
            { 34u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, RadMip0ReadView,   LinearRepeatSampler3D, RadMip0ReadLayout },
            { 35u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, RadCoarseReadView, LinearClampSampler3D,  RadCoarseReadLayout },
            { 36u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, GdfWriteView,      VK_NULL_HANDLE,        VK_IMAGE_LAYOUT_GENERAL },
            { 37u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, RadWriteView,      VK_NULL_HANDLE,        VK_IMAGE_LAYOUT_GENERAL },
            { 38u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, PrimaryVisImage2D.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
        }};
    };

    DispatchEncoder.SelectDescriptorRingSlot(2u);
    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    auto TransitionVol3D = [&](
        VkImage              Img,
        uint32_t             BaseMip,
        uint32_t             LevelCount,
        VkImageLayout        OldLayout,
        VkImageLayout        NewLayout,
        VkPipelineStageFlags SrcStage,
        VkPipelineStageFlags DstStage,
        VkAccessFlags        SrcAccess,
        VkAccessFlags        DstAccess)
    {
        VkImageMemoryBarrier B {};
        B.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        B.oldLayout           = OldLayout;
        B.newLayout           = NewLayout;
        B.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        B.image               = Img;
        B.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, BaseMip, LevelCount, 0u, 1u };
        B.srcAccessMask       = SrcAccess;
        B.dstAccessMask       = DstAccess;
        vkCmdPipelineBarrier(Cmd, SrcStage, DstStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &B);
    };

    auto InsertPrecomputeBarrier = [&]()
    {
        VkMemoryBarrier Barrier {};
        Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &Barrier,
            0u,
            nullptr,
            0u,
            nullptr);
    };

    if (AtmoActive)
    {
        DispatchGpuSkyViewLut(Cmd, Scene);
    }

    if (UploadedBvhCount == 0u)
    {
        vkCmdFillBuffer(Cmd, SolarShadowDepthBuffer.BufferHandle, 0ull, ShadowDepthBytes, 0x49742400u); // 1.0e6f
        vkCmdFillBuffer(Cmd, GlobalRadiosityFieldBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE, 0u);
        vkCmdFillBuffer(Cmd, GlobalSurfaceShellBuffer.BufferHandle, 0ull, VK_WHOLE_SIZE, 0u);
        const VkImageSubresourceRange Range1 { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        const VkImageSubresourceRange Range3 { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 3u, 0u, 1u };
        const VkClearColorValue ClearGdf  {{ 10000.0f, 10000.0f, 10000.0f, 10000.0f }};
        const VkClearColorValue ClearZero {{ 0.0f, 0.0f, 0.0f, 0.0f }};
        TransitionVol3D(GdfFineImage3D.ImageHandle,         0u, 1u, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        TransitionVol3D(GdfCoarseImage3D.ImageHandle,       0u, 1u, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        TransitionVol3D(RadiosityMip0Image3D.ImageHandle,   0u, 1u, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        TransitionVol3D(RadiosityCoarseImage3D.ImageHandle, 0u, 3u, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        vkCmdClearColorImage(Cmd, GdfFineImage3D.ImageHandle,         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearGdf,  1u, &Range1);
        vkCmdClearColorImage(Cmd, GdfCoarseImage3D.ImageHandle,       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearGdf,  1u, &Range1);
        vkCmdClearColorImage(Cmd, RadiosityMip0Image3D.ImageHandle,   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearZero, 1u, &Range1);
        vkCmdClearColorImage(Cmd, RadiosityCoarseImage3D.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearZero, 1u, &Range3);
        TransitionVol3D(GdfFineImage3D.ImageHandle,         0u, 1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionVol3D(GdfCoarseImage3D.ImageHandle,       0u, 1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionVol3D(RadiosityMip0Image3D.ImageHandle,   0u, 1u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        TransitionVol3D(RadiosityCoarseImage3D.ImageHandle, 0u, 3u, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        GlobalDistanceFieldReady = false;
        FineToroidalValid        = false;
        CoarseToroidalValid      = false;
    }
    else
    {
        uint32_t NextUniSlot = 0u;

        const bool DoFineShadow   = (EffectiveMask & kPrecomputeFineShadow) != 0u;
        const bool DoCoarseShadow = (EffectiveMask & kPrecomputeCoarseShadow) != 0u;
        if (DoFineShadow || DoCoarseShadow)
        {
            const uint64_t UniOffs = static_cast<uint64_t>(NextUniSlot++) * UniformStrideBytes;
            WorldViewportUniformBlockStd140 ShadowUni = Uniforms;
            ShadowUni.GlobalDistanceBakeControl[0] = 0u;
            ShadowUni.GlobalDistanceBakeControl[1] = (!DoFineShadow && DoCoarseShadow) ? 1u : 0u;
            ShadowUni.GlobalDistanceBakeControl[2] = 1u;
            std::memcpy(UniformHostBytes + UniOffs, &ShadowUni, sizeof(ShadowUni));

            const uint32_t CascadeCount = (DoFineShadow && DoCoarseShadow) ? 2u : 1u;
            const uint32_t MaxActiveRes = DoFineShadow
                ? std::max(CachedFineSunCascade.ResolutionTexels, DoCoarseShadow ? CachedSunCascade.ResolutionTexels : 0u)
                : CachedSunCascade.ResolutionTexels;
            const uint32_t ShadowGroups = (MaxActiveRes + 7u) / 8u;
            const auto CascadeSlots = ConstructPrecomputeSlots(
                UniOffs,
                GdfFineImage3D.ViewHandle,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                GdfCoarseImage3D.ViewHandle,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                RadiosityMip0Image3D.ViewHandle,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                RadiosityCoarseImage3D.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                DummyFloat1x1x1Image3D.ViewHandle,
                DummyRgba1x1x1Image3D.ViewHandle);
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ShadowCascadeProgramHandle, CascadeSlots, ShadowGroups, ShadowGroups, CascadeCount))
            {
                BoundDevice->FreeCommandBuffer(Cmd);
                return false;
            }
            DistanceFieldStats.SolarShadowBakeCount += 1u;
        }

        auto PackSlabUniform = [](WorldViewportUniformBlockStd140& TargetUni, const ToroidalSlabBox& Box) noexcept
        {
            TargetUni.GlobalDistanceSlabBounds[0] = Box.MinCoord[0] | (Box.MaxCoord[0] << 16u);
            TargetUni.GlobalDistanceSlabBounds[1] = Box.MinCoord[1] | (Box.MaxCoord[1] << 16u);
            TargetUni.GlobalDistanceSlabBounds[2] = Box.MinCoord[2] | (Box.MaxCoord[2] << 16u);
            TargetUni.GlobalDistanceSlabBounds[3] = 1u;
        };

        if (DoFineGdfUpdate || DoCoarseGdfUpdate)
        {
            bool DispatchedAnySlab  = false;
            bool IncrementalScrolled = false;
            for (uint32_t CascadeIdx = 0u; CascadeIdx < 2u; ++CascadeIdx)
            {
                const bool      ActiveCascade = (CascadeIdx == 0u) ? DoFineGdfUpdate : DoCoarseGdfUpdate;
                const uint32_t  SlabCnt       = (CascadeIdx == 0u) ? FineSlabCount   : CoarseSlabCount;
                const auto&     SlabArray     = (CascadeIdx == 0u) ? FineSlabs       : CoarseSlabs;
                if (!ActiveCascade || SlabCnt == 0u)
                {
                    continue;
                }

                DeviceImageResource& DstGdfImg = (CascadeIdx == 0u) ? GdfFineImage3D : GdfCoarseImage3D;
                TransitionVol3D(
                    DstGdfImg.ImageHandle, 0u, 1u,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

                for (uint32_t SlabIdx = 0u; SlabIdx < SlabCnt; ++SlabIdx)
                {
                    const ToroidalSlabBox& Box = SlabArray[SlabIdx];
                    const uint32_t ExtX    = Box.MaxCoord[0] - Box.MinCoord[0];
                    const uint32_t ExtY    = Box.MaxCoord[1] - Box.MinCoord[1];
                    const uint32_t ExtZ    = Box.MaxCoord[2] - Box.MinCoord[2];
                    const uint32_t GroupsX = (ExtX + 3u) / 4u;
                    const uint32_t GroupsY = (ExtY + 3u) / 4u;
                    const uint32_t GroupsZ = (ExtZ + 3u) / 4u;

                    const uint64_t UniOffs = static_cast<uint64_t>(NextUniSlot++) * UniformStrideBytes;
                    WorldViewportUniformBlockStd140 GdfUni = Uniforms;
                    GdfUni.GlobalDistanceBakeControl[0] = 0u;
                    GdfUni.GlobalDistanceBakeControl[1] = CascadeIdx;
                    GdfUni.GlobalDistanceBakeControl[2] = 1u;
                    PackSlabUniform(GdfUni, Box);
                    std::memcpy(UniformHostBytes + UniOffs, &GdfUni, sizeof(GdfUni));

                    const auto GdfSlots = ConstructPrecomputeSlots(
                        UniOffs,
                        (CascadeIdx == 0u) ? DummyFloat1x1x1Image3D.ViewHandle : GdfFineImage3D.ViewHandle,
                        (CascadeIdx == 0u) ? VK_IMAGE_LAYOUT_GENERAL          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        (CascadeIdx == 1u) ? DummyFloat1x1x1Image3D.ViewHandle : GdfCoarseImage3D.ViewHandle,
                        (CascadeIdx == 1u) ? VK_IMAGE_LAYOUT_GENERAL          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        RadiosityMip0Image3D.ViewHandle,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        RadiosityCoarseImage3D.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        DstGdfImg.ViewHandle,
                        DummyRgba1x1x1Image3D.ViewHandle);
                    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, GlobalDistanceBakeProgramHandle, GdfSlots, GroupsX, GroupsY, GroupsZ))
                    {
                        BoundDevice->FreeCommandBuffer(Cmd);
                        return false;
                    }
                    DispatchedAnySlab = true;
                }

                TransitionVol3D(
                    DstGdfImg.ImageHandle, 0u, 1u,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

                if (CascadeIdx == 0u)
                {
                    DistanceFieldStats.LastFineUpdatedVoxelCount = FineVoxWritten;
                    if (!FullFineReset && FineVoxWritten < 262144u)
                    {
                        IncrementalScrolled = true;
                    }
                }
                else
                {
                    DistanceFieldStats.LastCoarseUpdatedVoxelCount = CoarseVoxWritten;
                    DistanceFieldStats.CoarseCascadeBakeCount     += 1u;
                    if (!FullCoarseReset && CoarseVoxWritten < 262144u)
                    {
                        IncrementalScrolled = true;
                    }
                }
            }
            if (DispatchedAnySlab)
            {
                DistanceFieldStats.CascadeBakeCount += 1u;
            }
            if (IncrementalScrolled)
            {
                DistanceFieldStats.ToroidalSlabScrollCount += 1u;
            }
        }
        InsertPrecomputeBarrier();

        const bool DoRadiosity = ((EffectiveMask & kPrecomputeRadiosity) != 0u) && (GlobalRadiosityProgramHandle != UINT32_MAX);
        if (DoRadiosity)
        {
            const bool IncrementalRadiositySlabs =
                DoFineGdfUpdate &&
                !FullFineReset &&
                ((EffectiveMask & kPrecomputeRadiosityFull) == 0u);
            const std::array<ToroidalSlabBox, 3u> FullVolumeSlabs {{
                ToroidalSlabBox{ { 0u, 0u, 0u }, { 64u, 64u, 64u } },
                ToroidalSlabBox{},
                ToroidalSlabBox{},
            }};
            const auto&    RadSlabArray = IncrementalRadiositySlabs ? FineSlabs     : FullVolumeSlabs;
            const uint32_t RadSlabCount = IncrementalRadiositySlabs ? FineSlabCount : 1u;

            if (RadSlabCount > 0u)
            {
                const uint32_t PhaseCount = ((EffectiveMask & kPrecomputeSecondBounce) != 0u) ? 8u : 4u;
                constexpr std::array<uint32_t, 8u> PhaseGroupCounts {{ 16u, 8u, 4u, 2u, 16u, 8u, 4u, 2u }};
                for (uint32_t Phase = 0u; Phase < PhaseCount; ++Phase)
                {
                    if (Phase == 0u)
                    {
                        TransitionVol3D(
                            RadiosityMip0Image3D.ImageHandle, 0u, 1u,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

                        for (uint32_t SlabIdx = 0u; SlabIdx < RadSlabCount; ++SlabIdx)
                        {
                            const ToroidalSlabBox& Box = RadSlabArray[SlabIdx];
                            const uint32_t GroupsX = (Box.MaxCoord[0] - Box.MinCoord[0] + 3u) / 4u;
                            const uint32_t GroupsY = (Box.MaxCoord[1] - Box.MinCoord[1] + 3u) / 4u;
                            const uint32_t GroupsZ = (Box.MaxCoord[2] - Box.MinCoord[2] + 3u) / 4u;

                            const uint64_t UniOffs = static_cast<uint64_t>(NextUniSlot++) * UniformStrideBytes;
                            WorldViewportUniformBlockStd140 RadUni = Uniforms;
                            RadUni.GlobalDistanceBakeControl[0] = Phase;
                            RadUni.GlobalDistanceBakeControl[1] = 0u;
                            RadUni.GlobalDistanceBakeControl[2] = 1u;
                            PackSlabUniform(RadUni, Box);
                            std::memcpy(UniformHostBytes + UniOffs, &RadUni, sizeof(RadUni));

                            const auto RadSlots = ConstructPrecomputeSlots(
                                UniOffs,
                                GdfFineImage3D.ViewHandle,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                GdfCoarseImage3D.ViewHandle,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                DummyRgba1x1x1Image3D.ViewHandle,  VK_IMAGE_LAYOUT_GENERAL,
                                RadiosityCoarseImage3D.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                DummyFloat1x1x1Image3D.ViewHandle,
                                RadiosityMip0Image3D.ViewHandle);
                            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, GlobalRadiosityProgramHandle, RadSlots, GroupsX, GroupsY, GroupsZ))
                            {
                                BoundDevice->FreeCommandBuffer(Cmd);
                                return false;
                            }
                        }

                        TransitionVol3D(
                            RadiosityMip0Image3D.ImageHandle, 0u, 1u,
                            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
                    }
                    else if (Phase == 4u)
                    {
                        if (IncrementalRadiositySlabs)
                        {
                            TransitionVol3D(
                                RadiosityMip0Image3D.ImageHandle, 0u, 1u,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                            TransitionVol3D(
                                RadiosityMip0ScratchImage3D.ImageHandle, 0u, 1u,
                                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                            VkImageCopy SeedCopy {};
                            SeedCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
                            SeedCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
                            SeedCopy.extent         = { 64u, 64u, 64u };
                            vkCmdCopyImage(
                                Cmd,
                                RadiosityMip0Image3D.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                RadiosityMip0ScratchImage3D.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                1u, &SeedCopy);
                            TransitionVol3D(
                                RadiosityMip0Image3D.ImageHandle, 0u, 1u,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
                            TransitionVol3D(
                                RadiosityMip0ScratchImage3D.ImageHandle, 0u, 1u,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
                        }

                        for (uint32_t SlabIdx = 0u; SlabIdx < RadSlabCount; ++SlabIdx)
                        {
                            const ToroidalSlabBox& Box = RadSlabArray[SlabIdx];
                            const uint32_t GroupsX = (Box.MaxCoord[0] - Box.MinCoord[0] + 3u) / 4u;
                            const uint32_t GroupsY = (Box.MaxCoord[1] - Box.MinCoord[1] + 3u) / 4u;
                            const uint32_t GroupsZ = (Box.MaxCoord[2] - Box.MinCoord[2] + 3u) / 4u;

                            const uint64_t UniOffs = static_cast<uint64_t>(NextUniSlot++) * UniformStrideBytes;
                            WorldViewportUniformBlockStd140 RadUni = Uniforms;
                            RadUni.GlobalDistanceBakeControl[0] = Phase;
                            RadUni.GlobalDistanceBakeControl[1] = 0u;
                            RadUni.GlobalDistanceBakeControl[2] = 1u;
                            PackSlabUniform(RadUni, Box);
                            std::memcpy(UniformHostBytes + UniOffs, &RadUni, sizeof(RadUni));

                            const auto RadSlots = ConstructPrecomputeSlots(
                                UniOffs,
                                GdfFineImage3D.ViewHandle,              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                GdfCoarseImage3D.ViewHandle,            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                RadiosityMip0Image3D.ViewHandle,        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                RadiosityCoarseImage3D.ViewHandle,      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                DummyFloat1x1x1Image3D.ViewHandle,
                                RadiosityMip0ScratchImage3D.ViewHandle);
                            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, GlobalRadiosityProgramHandle, RadSlots, GroupsX, GroupsY, GroupsZ))
                            {
                                BoundDevice->FreeCommandBuffer(Cmd);
                                return false;
                            }
                        }

                        // Copy updated 2nd-bounce Mip 0 from Scratch -> Mip 0
                        TransitionVol3D(
                            RadiosityMip0ScratchImage3D.ImageHandle, 0u, 1u,
                            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                        TransitionVol3D(
                            RadiosityMip0Image3D.ImageHandle, 0u, 1u,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                        VkImageCopy Copy2nd {};
                        Copy2nd.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
                        Copy2nd.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
                        Copy2nd.extent         = { 64u, 64u, 64u };
                        vkCmdCopyImage(
                            Cmd,
                            RadiosityMip0ScratchImage3D.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            RadiosityMip0Image3D.ImageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1u, &Copy2nd);
                        TransitionVol3D(
                            RadiosityMip0Image3D.ImageHandle, 0u, 1u,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
                        TransitionVol3D(
                            RadiosityMip0ScratchImage3D.ImageHandle, 0u, 1u,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
                    }
                    else
                    {
                        // Downsample phases: 1/5 -> Coarse[0], 2/6 -> Coarse[1], 3/7 -> Coarse[2]
                        const uint32_t DstCoarseMip = (Phase == 1u || Phase == 5u) ? 0u : ((Phase == 2u || Phase == 6u) ? 1u : 2u);
                        TransitionVol3D(
                            RadiosityCoarseImage3D.ImageHandle, DstCoarseMip, 1u,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

                        const uint64_t UniOffs = static_cast<uint64_t>(NextUniSlot++) * UniformStrideBytes;
                        WorldViewportUniformBlockStd140 RadUni = Uniforms;
                        RadUni.GlobalDistanceBakeControl[0] = Phase;
                        RadUni.GlobalDistanceBakeControl[1] = 0u;
                        RadUni.GlobalDistanceBakeControl[2] = 1u;
                        RadUni.GlobalDistanceSlabBounds[3]  = 0u;
                        std::memcpy(UniformHostBytes + UniOffs, &RadUni, sizeof(RadUni));

                        const VkImageView   SrcCoarseView   = (DstCoarseMip == 0u) ? DummyRgba1x1x1Image3D.ViewHandle : RadiosityCoarseMipViews[DstCoarseMip - 1u];
                        const VkImageLayout SrcCoarseLayout = (DstCoarseMip == 0u) ? VK_IMAGE_LAYOUT_GENERAL          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        const uint32_t Groups = PhaseGroupCounts[Phase];
                        const auto RadSlots = ConstructPrecomputeSlots(
                            UniOffs,
                            GdfFineImage3D.ViewHandle,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            GdfCoarseImage3D.ViewHandle,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            RadiosityMip0Image3D.ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            SrcCoarseView,                   SrcCoarseLayout,
                            DummyFloat1x1x1Image3D.ViewHandle,
                            RadiosityCoarseMipViews[DstCoarseMip]);
                        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, GlobalRadiosityProgramHandle, RadSlots, Groups, Groups, Groups))
                        {
                            BoundDevice->FreeCommandBuffer(Cmd);
                            return false;
                        }

                        TransitionVol3D(
                            RadiosityCoarseImage3D.ImageHandle, DstCoarseMip, 1u,
                            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
                    }
                    InsertPrecomputeBarrier();
                }
            }
            GlobalDistanceFieldReady = true;

            auto HashMixBytes = [](uint64_t SeedVal, const void* BytesPtr, std::size_t ByteLen) -> uint64_t
            {
                const auto* Raw = static_cast<const uint8_t*>(BytesPtr);
                for (std::size_t I = 0u; I < ByteLen; ++I)
                {
                    SeedVal ^= static_cast<uint64_t>(Raw[I]);
                    SeedVal *= 1099511628211ull;
                }
                return SeedVal;
            };
            uint64_t LightingSig = 14695981039346656037ull;
            LightingSig = HashMixBytes(LightingSig, Uniforms.SolarIrradiancePad, sizeof(Uniforms.SolarIrradiancePad));
            LightingSig = HashMixBytes(LightingSig, Uniforms.SkyRadianceBvhCount, sizeof(float) * 3u);
            LightingSig = HashMixBytes(LightingSig, Uniforms.AreaLights, sizeof(Uniforms.AreaLights));
            LightingSig = HashMixBytes(LightingSig, &LightCount, sizeof(LightCount));
            LastRadiosityLightingSignature = LightingSig;
        }
        else if (DoFineGdfUpdate || DoCoarseGdfUpdate)
        {
            GlobalDistanceFieldReady = true;
        }
    }

    VkMemoryBarrier DoneBarrier {};
    DoneBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    DoneBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    DoneBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0u,
        1u,
        &DoneBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    // Submit asynchronously on the Vulkan timeline (zero CPU AwaitTimelineTick stall!)
    PendingPrecomputeSignal = BoundDevice->DispatchCommandBufferAsync(Cmd);
    PendingPrecomputeCmd    = Cmd;
    return true;
}

void VulkanWorldRenderSequence::UploadSolarCascadeDepth(const WorldSceneSpecification& Scene)
{
    if (!QueryReady())
    {
        return;
    }

    if (DispatchGpuScenePrecompute(Scene))
    {
        return;
    }

    const Mathematics::Axis3D FocusCenter = (Scene.SceneTree.RootBoundsMin + Scene.SceneTree.RootBoundsMax) * 0.5f;
    SolarShadowSequence::BuildTwoCascadeContactClips(Scene.Sun, FocusCenter, Scene.SceneTree, CachedFineSunCascade, CachedSunCascade);
    if (!CachedFineSunCascade.ClosestLightDepth.empty() && !CachedSunCascade.ClosestLightDepth.empty())
    {
        CoarseShadowOffsetTexels = static_cast<uint32_t>(CachedFineSunCascade.ClosestLightDepth.size());
        std::vector<float> Combined(CachedFineSunCascade.ClosestLightDepth.size() + CachedSunCascade.ClosestLightDepth.size());
        std::memcpy(Combined.data(), CachedFineSunCascade.ClosestLightDepth.data(), CachedFineSunCascade.ClosestLightDepth.size() * sizeof(float));
        std::memcpy(Combined.data() + CachedFineSunCascade.ClosestLightDepth.size(), CachedSunCascade.ClosestLightDepth.data(), CachedSunCascade.ClosestLightDepth.size() * sizeof(float));
        UploadToDeviceBuffer(SolarShadowDepthBuffer, Combined.data(), Combined.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    else
    {
        CoarseShadowOffsetTexels = 0u;
        const float FallbackDepth[4]{ 60.0f, 60.0f, 60.0f, 60.0f };
        UploadToDeviceBuffer(SolarShadowDepthBuffer, FallbackDepth, sizeof(FallbackDepth), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
}

void VulkanWorldRenderSequence::SynchronizeSceneResources(
    const WorldSceneSpecification& Scene,
    bool                           RebuildShadows)
{
    if (!QueryReady())
    {
        return;
    }
    const auto SyncStart = std::chrono::steady_clock::now();

    // 1 & 2. Assemble Resident Multi-LOD Cluster BVH4 + Shaded Triangles once (or fall back to raw SceneTree)
    if (!AssembleResidentClusterSceneBvh(Scene, RebuildShadows))
    {
        ResidentClusterBvhActive = false;
        MaskSliceOffsetBytes     = 256ull;
        EnsureBufferCapacity(ActiveClusterMaskBuffer, MaskSliceOffsetBytes * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (ActiveClusterMaskBuffer.HostAddress != nullptr)
        {
            std::memset(ActiveClusterMaskBuffer.HostAddress, 0xFF, static_cast<std::size_t>(MaskSliceOffsetBytes * 2ull));
        }
        if (!Scene.SceneTree.WideEntries.empty())
        {
            const uint64_t BvhBytes = Scene.SceneTree.WideEntries.size() * sizeof(SpatialAcceleration::WideTreeEntry);
            UploadToDeviceBuffer(BvhEntryBuffer, Scene.SceneTree.WideEntries.data(), BvhBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            UploadedBvhCount = static_cast<uint32_t>(Scene.SceneTree.WideEntries.size());
        }
        else
        {
            SpatialAcceleration::WideTreeEntry DummyEntry {};
            UploadToDeviceBuffer(BvhEntryBuffer, &DummyEntry, sizeof(DummyEntry), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            UploadedBvhCount = 0u;
        }

        // 2. Upload 96-byte shaded triangles
        const auto& SrcPrims   = Scene.SceneTree.Primitives;
    const auto& SrcNormals = Scene.SceneTree.PrimitiveNormals;
    std::vector<GpuShadedTriangle96> ShadedTris(std::max<std::size_t>(1u, SrcPrims.size()));
    if (!SrcPrims.empty())
    {
        if (SrcNormals.size() == SrcPrims.size())
        {
            for (std::size_t Idx = 0u; Idx < SrcPrims.size(); ++Idx)
            {
                const auto& Prim   = SrcPrims[Idx];
                const auto& Smooth = SrcNormals[Idx];

                GpuShadedTriangle96 OutTri {};
                OutTri.Corner0Pad[0]  = Prim.Corner0.X;
                OutTri.Corner0Pad[1]  = Prim.Corner0.Y;
                OutTri.Corner0Pad[2]  = Prim.Corner0.Z;
                OutTri.Edge01Pad[0]   = Prim.Edge01.X;
                OutTri.Edge01Pad[1]   = Prim.Edge01.Y;
                OutTri.Edge01Pad[2]   = Prim.Edge01.Z;
                OutTri.Edge02Pad[0]   = Prim.Edge02.X;
                OutTri.Edge02Pad[1]   = Prim.Edge02.Y;
                OutTri.Edge02Pad[2]   = Prim.Edge02.Z;
                OutTri.Normal0Pad[0]  = Smooth.Normal0.X;
                OutTri.Normal0Pad[1]  = Smooth.Normal0.Y;
                OutTri.Normal0Pad[2]  = Smooth.Normal0.Z;
                OutTri.Normal1Pad[0]  = Smooth.Normal1.X;
                OutTri.Normal1Pad[1]  = Smooth.Normal1.Y;
                OutTri.Normal1Pad[2]  = Smooth.Normal1.Z;
                OutTri.Normal2Slot[0] = Smooth.Normal2.X;
                OutTri.Normal2Slot[1] = Smooth.Normal2.Y;
                OutTri.Normal2Slot[2] = Smooth.Normal2.Z;
                OutTri.Normal2Slot[3] = static_cast<float>(Prim.MaterialOrdinal);
                OutTri.Corner0Pad[3]  = PackNaniteTriangleMetaWord(0u, 0u, 0u, static_cast<uint32_t>(Idx / 512u));
                OutTri.Edge01Pad[3]   = static_cast<float>(Idx / 128u);
                OutTri.Edge02Pad[3]   = 0.0f;
                {
                    const Mathematics::Axis3D C0 = Prim.Corner0;
                    const Mathematics::Axis3D C1 = Prim.Corner0 + Prim.Edge01;
                    const Mathematics::Axis3D C2 = Prim.Corner0 + Prim.Edge02;
                    const Mathematics::Axis3D Fn = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
                    const bool UseXY = (std::fabs(Fn.Z) >= std::fabs(Fn.X) && std::fabs(Fn.Z) >= std::fabs(Fn.Y));
                    const bool UseXZ = (!UseXY && std::fabs(Fn.Y) >= std::fabs(Fn.X));
                    auto Proj = [&](Mathematics::Axis3D P) noexcept -> Mathematics::UVAxes
                    {
                        return UseXY ? Mathematics::UVAxes { P.X * 0.25f, P.Y * 0.25f }
                             : UseXZ ? Mathematics::UVAxes { P.X * 0.25f, P.Z * 0.25f }
                                     : Mathematics::UVAxes { P.Y * 0.25f, P.Z * 0.25f };
                    };
                    PackNaniteTriangleUvsToFloats(Proj(C0), Proj(C1), Proj(C2), OutTri.Normal0Pad[3], OutTri.Normal1Pad[3]);
                }
                ShadedTris[Idx]       = OutTri;
            }
        }
        else
        {
            std::vector<Mathematics::Axis3D>          AreaWeightedNormals(SrcPrims.size());
            std::vector<QuantizedCornerTriangleEntry> SharedCornerEntries;
            SharedCornerEntries.reserve(SrcPrims.size() * 3u);

            for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(SrcPrims.size()); ++Idx)
            {
                const auto& Prim            = SrcPrims[Idx];
                AreaWeightedNormals[Idx]    = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
                const Mathematics::Axis3D C0 = Prim.Corner0;
                const Mathematics::Axis3D C1 = Prim.Corner0 + Prim.Edge01;
                const Mathematics::Axis3D C2 = Prim.Corner0 + Prim.Edge02;
                SharedCornerEntries.push_back({ QuantizeVertexPosition(C0, Prim.MaterialOrdinal), Idx });
                SharedCornerEntries.push_back({ QuantizeVertexPosition(C1, Prim.MaterialOrdinal), Idx });
                SharedCornerEntries.push_back({ QuantizeVertexPosition(C2, Prim.MaterialOrdinal), Idx });
            }
            std::sort(SharedCornerEntries.begin(), SharedCornerEntries.end());

            for (std::size_t Idx = 0u; Idx < SrcPrims.size(); ++Idx)
            {
                const auto& Prim = SrcPrims[Idx];
                const Mathematics::Axis3D UnitFaceN = Mathematics::NormalizeAxis(AreaWeightedNormals[Idx], { 0.0f, 0.0f, 1.0f });
                const std::array<Mathematics::Axis3D, 3u> Corners {{
                    Prim.Corner0,
                    Prim.Corner0 + Prim.Edge01,
                    Prim.Corner0 + Prim.Edge02,
                }};

                std::array<Mathematics::Axis3D, 3u> SmoothCorners {};
                for (std::size_t CornerIdx = 0u; CornerIdx < 3u; ++CornerIdx)
                {
                    Mathematics::Axis3D SumNormal { 0.0f, 0.0f, 0.0f };
                    const QuantizedCornerTriangleEntry SearchCorner { QuantizeVertexPosition(Corners[CornerIdx], Prim.MaterialOrdinal), 0u };
                    auto Iter = std::lower_bound(
                        SharedCornerEntries.begin(),
                        SharedCornerEntries.end(),
                        SearchCorner,
                        [](const QuantizedCornerTriangleEntry& Lhs, const QuantizedCornerTriangleEntry& Rhs) noexcept
                        {
                            return Lhs.Key < Rhs.Key;
                        });
                    while (Iter != SharedCornerEntries.end() && Iter->Key == SearchCorner.Key)
                    {
                        const uint32_t PeerTriIdx = Iter->TriIndex;
                        const Mathematics::Axis3D PeerUnit = Mathematics::NormalizeAxis(AreaWeightedNormals[PeerTriIdx], UnitFaceN);
                        if (Mathematics::EvaluateDot(UnitFaceN, PeerUnit) > 0.65f)
                        {
                            SumNormal = SumNormal + AreaWeightedNormals[PeerTriIdx];
                        }
                        ++Iter;
                    }
                    SmoothCorners[CornerIdx] = Mathematics::NormalizeAxis(SumNormal, UnitFaceN);
                }

                GpuShadedTriangle96 OutTri {};
                OutTri.Corner0Pad[0]  = Prim.Corner0.X;
                OutTri.Corner0Pad[1]  = Prim.Corner0.Y;
                OutTri.Corner0Pad[2]  = Prim.Corner0.Z;
                OutTri.Edge01Pad[0]   = Prim.Edge01.X;
                OutTri.Edge01Pad[1]   = Prim.Edge01.Y;
                OutTri.Edge01Pad[2]   = Prim.Edge01.Z;
                OutTri.Edge02Pad[0]   = Prim.Edge02.X;
                OutTri.Edge02Pad[1]   = Prim.Edge02.Y;
                OutTri.Edge02Pad[2]   = Prim.Edge02.Z;
                OutTri.Normal0Pad[0]  = SmoothCorners[0u].X;
                OutTri.Normal0Pad[1]  = SmoothCorners[0u].Y;
                OutTri.Normal0Pad[2]  = SmoothCorners[0u].Z;
                OutTri.Normal1Pad[0]  = SmoothCorners[1u].X;
                OutTri.Normal1Pad[1]  = SmoothCorners[1u].Y;
                OutTri.Normal1Pad[2]  = SmoothCorners[1u].Z;
                OutTri.Normal2Slot[0] = SmoothCorners[2u].X;
                OutTri.Normal2Slot[1] = SmoothCorners[2u].Y;
                OutTri.Normal2Slot[2] = SmoothCorners[2u].Z;
                OutTri.Normal2Slot[3] = static_cast<float>(Prim.MaterialOrdinal);
                OutTri.Corner0Pad[3]  = PackNaniteTriangleMetaWord(0u, 0u, 0u, static_cast<uint32_t>(Idx / 512u));
                OutTri.Edge01Pad[3]   = static_cast<float>(Idx / 128u);
                OutTri.Edge02Pad[3]   = 0.0f;
                {
                    const Mathematics::Axis3D C0 = Prim.Corner0;
                    const Mathematics::Axis3D C1 = Prim.Corner0 + Prim.Edge01;
                    const Mathematics::Axis3D C2 = Prim.Corner0 + Prim.Edge02;
                    const bool UseXY = (std::fabs(UnitFaceN.Z) >= std::fabs(UnitFaceN.X) && std::fabs(UnitFaceN.Z) >= std::fabs(UnitFaceN.Y));
                    const bool UseXZ = (!UseXY && std::fabs(UnitFaceN.Y) >= std::fabs(UnitFaceN.X));
                    auto Proj = [&](Mathematics::Axis3D P) noexcept -> Mathematics::UVAxes
                    {
                        return UseXY ? Mathematics::UVAxes { P.X * 0.25f, P.Y * 0.25f }
                             : UseXZ ? Mathematics::UVAxes { P.X * 0.25f, P.Z * 0.25f }
                                     : Mathematics::UVAxes { P.Y * 0.25f, P.Z * 0.25f };
                    };
                    PackNaniteTriangleUvsToFloats(Proj(C0), Proj(C1), Proj(C2), OutTri.Normal0Pad[3], OutTri.Normal1Pad[3]);
                }
                ShadedTris[Idx]       = OutTri;
            }
        }
    }
        const uint64_t TriBytes = ShadedTris.size() * sizeof(GpuShadedTriangle96);
        UploadToDeviceBuffer(ShadedTriangleBuffer, ShadedTris.data(), TriBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        UploadedTriangleCount                = static_cast<uint32_t>(ShadedTris.size());
        UploadedClusterSlotCount             = 0u;
        NaniteLodStats.FullTriangleCount     = static_cast<uint32_t>(SrcPrims.size());
        NaniteLodStats.GeometryRebuildCount += 1u;
    }

    // 3. Upload 80-byte OpenPBR surface material entries
    const uint32_t MatCount = std::max(1u, Scene.MaterialRegister.QuerySlotCount());
    std::vector<GpuWorldMaterial80> GpuMats(MatCount);
    for (uint32_t Slot = 0u; Slot < Scene.MaterialRegister.QuerySlotCount(); ++Slot)
    {
        const auto& Src = Scene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot));
        const Mathematics::LinearRGB Emis = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Src.PorosityLight);
        GpuWorldMaterial80 Dst {};
        Dst.DiffuseAlbedoRoughness[0] = Src.Diffuse.BaseColor.R * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[1] = Src.Diffuse.BaseColor.G * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[2] = Src.Diffuse.BaseColor.B * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[3] = Src.Diffuse.DiffuseRoughness;
        Dst.SpecParams[0]             = Src.Specular.Roughness;
        Dst.SpecParams[1]             = Src.Specular.Metalness;
        Dst.SpecParams[2]             = Src.Specular.DielectricIOR;
        Dst.SpecParams[3]             = Src.Specular.Anisotropy;
        Dst.EdgeTintCoatWeight[0]     = Src.Specular.EdgeTintF82.R;
        Dst.EdgeTintCoatWeight[1]     = Src.Specular.EdgeTintF82.G;
        Dst.EdgeTintCoatWeight[2]     = Src.Specular.EdgeTintF82.B;
        Dst.EdgeTintCoatWeight[3]     = Src.Clearcoat.CoatWeight;
        Dst.CoatRoughnessOptics[0]    = Src.Clearcoat.CoatRoughness;
        Dst.CoatRoughnessOptics[1]    = Src.Transmission.TransmissionWeight;
        Dst.CoatRoughnessOptics[2]    = Src.Transmission.AbbeDispersionNumber;
        Dst.CoatRoughnessOptics[3]    = 0.0f;
        Dst.EmissiveRadiancePad[0]    = Emis.R;
        Dst.EmissiveRadiancePad[1]    = Emis.G;
        Dst.EmissiveRadiancePad[2]    = Emis.B;
        Dst.EmissiveRadiancePad[3]    = 0.0f;
        GpuMats[Slot]                 = Dst;
    }
    const uint64_t MatBytes = GpuMats.size() * sizeof(GpuWorldMaterial80);
    UploadToDeviceBuffer(MaterialEntryBuffer, GpuMats.data(), MatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    UploadedMaterialCount = MatCount;

    // 3b. Upload Resampled 128x32 LM-63 Photometric Tables & Point Emitters
    {
        if (!Scene.PhotometricProfiles.empty())
        {
            std::vector<float> FlatTables;
            FlatTables.reserve(Scene.PhotometricProfiles.size() * Codecs::PhotometricGridStride);
            for (const auto& Profile : Scene.PhotometricProfiles)
            {
                FlatTables.insert(
                    FlatTables.end(),
                    Profile.NormalizedCandela.begin(),
                    Profile.NormalizedCandela.end());
            }
            UploadToDeviceBuffer(
                PhotometricProfileTableBuffer,
                FlatTables.data(),
                static_cast<uint64_t>(FlatTables.size()) * sizeof(float),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            UploadedPhotometricProfiles = static_cast<uint32_t>(Scene.PhotometricProfiles.size());
        }
        else
        {
            UploadedPhotometricProfiles = 0u;
        }

        if (!Scene.PhotometricRadiators.empty())
        {
            std::vector<GpuPhotometricLamp64> GpuLamps(Scene.PhotometricRadiators.size());
            for (std::size_t L = 0u; L < Scene.PhotometricRadiators.size(); ++L)
            {
                const auto& Src = Scene.PhotometricRadiators[L];
                auto&       Dst = GpuLamps[L];
                Dst.PositionRange[0]      = Src.PositionWorld.X;
                Dst.PositionRange[1]      = Src.PositionWorld.Y;
                Dst.PositionRange[2]      = Src.PositionWorld.Z;
                Dst.PositionRange[3]      = Src.RangeMeters;
                Dst.BeamAxisRadius[0]     = Src.BeamAxis.X;
                Dst.BeamAxisRadius[1]     = Src.BeamAxis.Y;
                Dst.BeamAxisRadius[2]     = Src.BeamAxis.Z;
                Dst.BeamAxisRadius[3]     = Src.RadiusMeters;
                Dst.ZeroAzimuthProfile[0] = Src.ZeroAzimuthAxis.X;
                Dst.ZeroAzimuthProfile[1] = Src.ZeroAzimuthAxis.Y;
                Dst.ZeroAzimuthProfile[2] = Src.ZeroAzimuthAxis.Z;
                Dst.ZeroAzimuthProfile[3] = static_cast<float>(Src.ProfileOrdinal);
                Dst.PeakIntensityPad[0]   = Src.PeakIntensityRgb.R;
                Dst.PeakIntensityPad[1]   = Src.PeakIntensityRgb.G;
                Dst.PeakIntensityPad[2]   = Src.PeakIntensityRgb.B;
                Dst.PeakIntensityPad[3]   = 0.0f;
            }
            UploadToDeviceBuffer(
                PhotometricLampEntryBuffer,
                GpuLamps.data(),
                static_cast<uint64_t>(GpuLamps.size()) * sizeof(GpuPhotometricLamp64),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            UploadedPhotometricLamps = static_cast<uint32_t>(GpuLamps.size());
        }
        else
        {
            UploadedPhotometricLamps = 0u;
        }
    }

    // 3c. Upload Per-Asset Signed Local SDF Voxels (once per asset set) + Dynamic Rigid Instance Buffer
    (void)SynchronizeLocalSdfResources(Scene);

    // 4. Build Two-Cascade Solar Shadow Depth + Global Distance Field + 2-Bounce Radiosity Volume on GPU
    if (RebuildShadows || !SceneUploadedOnce || SolarShadowDepthBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        ActiveClipFocusWorld       = ResolveDynamicClipFocusWorld(nullptr, Scene);
        ActiveClipFocusInitialized = true;
        CascadeFocusInitialized    = false;
        FineToroidalValid          = false;
        CoarseToroidalValid        = false;
        PendingFineGeometryReset   = true;
        PendingCoarseGeometryReset = true;
        UploadSolarCascadeDepth(Scene);
    }
    else if (GlobalDistanceFieldReady)
    {
        (void)DispatchGpuScenePrecompute(
            Scene,
            false,
            kPrecomputeFineShadow | kPrecomputeCoarseShadow | kPrecomputeRadiosity | kPrecomputeRadiosityFull | kPrecomputeSecondBounce);
    }

    CachedNaniteCutSignature = 0u;
    SceneUploadedOnce        = true;
    ProgressiveSampleCount   = 0u;
    const auto SyncEnd = std::chrono::steady_clock::now();
    LastSceneSyncMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(SyncEnd - SyncStart).count());
}

// Nanite-like viewport LOD: all multi-LOD cluster sub-BVH4s live permanently in DEVICE_LOCAL VRAM.
// Camera movement and LOD toggles NEVER rebuild or re-upload the BVH or triangle buffers; only the
// compact ActiveClusterMaskBuffer (~64 B host-coherent bitmask) updates when the monotonic cut shifts.
void VulkanWorldRenderSequence::RefreshNaniteLodGeometry(
    const WorldRenderConfiguration& Config,
    const PerspectiveCameraBasis&   Camera,
    const WorldSceneSpecification&  Scene,
    bool                            AllowAsyncHysteresis)
{
    if (Scene.ClusterInstances.empty())
    {
        NaniteLodStats.FullTriangleCount   = static_cast<uint32_t>(Scene.SceneTree.Primitives.size());
        NaniteLodStats.Enabled             = false;
        NaniteLodStats.ActiveTriangleCount = NaniteLodStats.FullTriangleCount;
        return;
    }

    const uint64_t TransformSig = ComputeResidentClusterTransformSignature(Scene);
    if (!ResidentClusterBvhActive || TransformSig != CachedResidentTransformSig)
    {
        if (!AssembleResidentClusterSceneBvh(Scene, true))
        {
            NaniteLodStats.FullTriangleCount   = static_cast<uint32_t>(Scene.SceneTree.Primitives.size());
            NaniteLodStats.Enabled             = false;
            NaniteLodStats.ActiveTriangleCount = NaniteLodStats.FullTriangleCount;
            return;
        }
    }

    NaniteLodStats.FullTriangleCount = (ResidentFinestTriangleCount > 0u)
        ? ResidentFinestTriangleCount
        : static_cast<uint32_t>(Scene.SceneTree.Primitives.size());

    if (!Config.NaniteLodEnabled)
    {
        if (!ViewportMaskHoldsFinestOnly && !FinestClusterMaskWords.empty())
        {
            ViewportClusterMaskWords = FinestClusterMaskWords;
            if (ActiveClusterMaskBuffer.HostAddress != nullptr)
            {
                const std::size_t CopyBytes = ViewportClusterMaskWords.size() * sizeof(uint32_t);
                std::memcpy(
                    reinterpret_cast<uint8_t*>(ActiveClusterMaskBuffer.HostAddress) + MaskSliceOffsetBytes,
                    ViewportClusterMaskWords.data(),
                    CopyBytes);
            }
            ViewportMaskHoldsFinestOnly          = true;
            CachedNaniteCutSignature             = 0u;
            NaniteLodStats.ClusterMaskUpdateCount += 1u;
        }
        NaniteLodStats.Enabled             = false;
        NaniteLodStats.ActiveTriangleCount = NaniteLodStats.FullTriangleCount;
        return;
    }

    if (ResidentClusterInstances.empty())
    {
        NaniteLodStats.Enabled             = false;
        NaniteLodStats.ActiveTriangleCount = 0u;
        NaniteLodStats.ActiveClusterCount  = 0u;
        return;
    }

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    const float PixelsPerRadian = static_cast<float>(std::max(1u, Basis.HeightPx)) /
        std::max(1.0e-5f, 2.0f * Basis.TanHalfFovY);
    const float PixelError = std::clamp(Config.NanitePixelError, 0.10f, 16.0f);

    struct InstanceCut
    {
        uint32_t SpanOrdinal = 0u;
        SpatialAcceleration::ClusterCutSelection Selection {};
    };
    std::vector<InstanceCut> Cuts {};
    Cuts.reserve(ResidentClusterInstances.size());

    uint64_t Signature = 1469598103934665603ull;
    const auto MixSignature = [&Signature](uint64_t Value) noexcept
    {
        Signature = (Signature ^ Value) * 1099511628211ull;
    };
    MixSignature(TransformSig);
    MixSignature(static_cast<uint64_t>(std::lround(PixelError * 100.0f)));
    MixSignature(static_cast<uint64_t>(static_cast<int64_t>(Config.NaniteForcedLodLevel) + 16));

    uint32_t ActiveClusters  = 0u;
    uint32_t ActiveTriangles = 0u;
    for (uint32_t SpanOrd = 0u; SpanOrd < static_cast<uint32_t>(ResidentClusterInstances.size()); ++SpanOrd)
    {
        const ResidentClusterInstanceSpan& Span = ResidentClusterInstances[SpanOrd];
        const RigidClusterInstance&        Inst = Scene.ClusterInstances[Span.InstanceIndex];
        const float ScaleU = std::max(1.0e-4f, Inst.UniformScale);
        const float ScaleX = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;
        const float YawRad     = Inst.RotationZDegrees * Constants::DegToRadFloat;
        const bool  YawRotated = std::fabs(YawRad) > 1.0e-5f;
        const float CosZ       = YawRotated ? std::cos(YawRad) : 1.0f;
        const float SinZ       = YawRotated ? std::sin(YawRad) : 0.0f;
        Mathematics::Axis3D RelWorld = Camera.CameraOrigin - Inst.WorldTranslation;
        if (AllowAsyncHysteresis && !ViewportMaskHoldsFinestOnly)
        {
            const float RelDist = std::sqrt(std::max(1.0e-4f, Mathematics::EvaluateDot(RelWorld, RelWorld)));
            const float StepM   = std::clamp(RelDist * 0.025f, 0.05f, 0.45f);
            RelWorld.X = std::round(RelWorld.X / StepM) * StepM;
            RelWorld.Y = std::round(RelWorld.Y / StepM) * StepM;
            RelWorld.Z = std::round(RelWorld.Z / StepM) * StepM;
        }
        const Mathematics::Axis3D Unrotated = YawRotated
            ? Mathematics::Axis3D{ RelWorld.X * CosZ + RelWorld.Y * SinZ, -RelWorld.X * SinZ + RelWorld.Y * CosZ, RelWorld.Z }
            : RelWorld;
        const Mathematics::Axis3D LocalCamera { Unrotated.X / ScaleX, Unrotated.Y / ScaleY, Unrotated.Z / ScaleZ };

        InstanceCut Cut {};
        Cut.SpanOrdinal = SpanOrd;
        Cut.Selection   = Inst.PyramidAsset->SelectMonotonicCut(
            LocalCamera,
            PixelsPerRadian,
            PixelError,
            false,
            Config.NaniteForcedLodLevel);
        ActiveClusters  += static_cast<uint32_t>(Cut.Selection.ActiveClusterIndices.size());
        ActiveTriangles += Cut.Selection.ActiveTriangleCount;

        MixSignature(Span.InstanceIndex + 1u);
        for (const uint32_t ClusterIdx : Cut.Selection.ActiveClusterIndices)
        {
            MixSignature(ClusterIdx + 1u);
        }
        Cuts.push_back(std::move(Cut));
    }

    if (!ViewportMaskHoldsFinestOnly && Signature == CachedNaniteCutSignature)
    {
        NaniteLodStats.Enabled = true;
        return;
    }

    ViewportClusterMaskWords.assign(FinestClusterMaskWords.size(), 0u);
    std::array<uint32_t, 8u> LevelTriangles {};
    std::array<uint32_t, 8u> LevelClusters  {};

    for (const InstanceCut& Cut : Cuts)
    {
        const ResidentClusterInstanceSpan& Span    = ResidentClusterInstances[Cut.SpanOrdinal];
        const auto*                        Pyramid = Scene.ClusterInstances[Span.InstanceIndex].PyramidAsset;
        for (const uint32_t ClusterIdx : Cut.Selection.ActiveClusterIndices)
        {
            if (ClusterIdx >= Span.ClusterCount || ClusterIdx >= Pyramid->Clusters.size())
            {
                continue;
            }
            const uint32_t GlobalOrd = Span.FirstGlobalClusterOrdinal + ClusterIdx;
            ViewportClusterMaskWords[GlobalOrd >> 5u] |= (1u << (GlobalOrd & 31u));
            const auto&    Cluster   = Pyramid->Clusters[ClusterIdx];
            const uint32_t Level     = std::min(7u, static_cast<uint32_t>(Cluster.HierarchyLevel));
            const uint32_t LevelGate = Span.LevelGateOrdinals[Level];
            if (LevelGate != UINT32_MAX && (LevelGate >> 5u) < ViewportClusterMaskWords.size())
            {
                ViewportClusterMaskWords[LevelGate >> 5u] |= (1u << (LevelGate & 31u));
            }
            LevelClusters[Level]  += 1u;
            LevelTriangles[Level] += Cluster.TriangleCount;
        }
    }
    PropagateTopBranchGateBits(ViewportClusterMaskWords);

    if (ActiveClusterMaskBuffer.HostAddress != nullptr && !ViewportClusterMaskWords.empty())
    {
        const std::size_t CopyBytes = ViewportClusterMaskWords.size() * sizeof(uint32_t);
        std::memcpy(
            reinterpret_cast<uint8_t*>(ActiveClusterMaskBuffer.HostAddress) + MaskSliceOffsetBytes,
            ViewportClusterMaskWords.data(),
            CopyBytes);
    }

    const uint32_t PrevRebuilds = NaniteLodStats.GeometryRebuildCount;
    const uint32_t PrevMaskUpd  = NaniteLodStats.ClusterMaskUpdateCount;
    const uint32_t FullTris     = NaniteLodStats.FullTriangleCount;
    NaniteLodStats = {};
    NaniteLodStats.Enabled                = true;
    NaniteLodStats.ActiveClusterCount     = ActiveClusters;
    NaniteLodStats.ActiveTriangleCount    = ActiveTriangles;
    NaniteLodStats.FullTriangleCount      = FullTris;
    NaniteLodStats.GeometryRebuildCount   = PrevRebuilds;
    NaniteLodStats.ClusterMaskUpdateCount = PrevMaskUpd + 1u;
    for (uint32_t Level = 0u; Level < 8u; ++Level)
    {
        NaniteLodStats.LevelTriangleCount[Level] = LevelTriangles[Level];
        NaniteLodStats.LevelClusterCount[Level]  = LevelClusters[Level];
    }

    CachedNaniteCutSignature    = Signature;
    ViewportMaskHoldsFinestOnly = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU COMPUTE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool VulkanWorldRenderSequence::RenderWorldViewportRgba8(
    WorldRenderTier                Tier,
    const PerspectiveCameraBasis&  Camera,
    const WorldSceneSpecification& Scene,
    uint32_t                       DebugChannel,
    std::vector<uint8_t>&          OutDisplayRgba8)
{
    FRONTIER_THOUGHT_SCOPE("Render.WorldViewportRgba8");
    WorldRenderConfiguration Config {};
    Config.DebugChannel     = DebugChannel;
    Config.Engine           = WorldRenderEngine::OpenPbrDirect;
    Config.ShadowAlgorithm  = (Tier == WorldRenderTier::Tier2RtxDeterministic)
        ? SolarShadowAlgorithm::WideTreeRayQuery
        : SolarShadowAlgorithm::ContactPcss;
    Config.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;
    return RenderWorldViewportWithEngine(Config, 0u, Camera, Scene, OutDisplayRgba8);
}

void VulkanWorldRenderSequence::DispatchGpuSkyViewLut(VkCommandBuffer Cmd, const WorldSceneSpecification& Scene)
{
    if (TransmittanceLutProgramHandle == UINT32_MAX ||
        MultiScatterLutProgramHandle == UINT32_MAX ||
        SkyViewLutProgramHandle == UINT32_MAX ||
        SkyViewUniformBuffer.HostAddress == nullptr ||
        AtmosphericSkyViewLutBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return;
    }

    const auto& Atmo = Scene.Atmosphere;
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);

    AtmosphericSkyUniformStd140 SkyUni {};
    SkyUni.SunDirectionIntensity[0] = SolarSolved.WorldDirectionToSun.X;
    SkyUni.SunDirectionIntensity[1] = SolarSolved.WorldDirectionToSun.Y;
    SkyUni.SunDirectionIntensity[2] = SolarSolved.WorldDirectionToSun.Z;
    SkyUni.SunDirectionIntensity[3] = Atmo.Solar.SolarIntensityMultiplier;

    SkyUni.ScatteringStrengths[0]   = Atmo.Medium.RayleighStrength;
    SkyUni.ScatteringStrengths[1]   = Atmo.Medium.MieStrength;
    SkyUni.ScatteringStrengths[2]   = Atmo.Medium.MieAnisotropy;
    SkyUni.ScatteringStrengths[3]   = Atmo.Medium.OzoneStrength;

    SkyUni.PlanetDimensions[0]      = Atmo.Medium.PlanetRadiusMeters;
    SkyUni.PlanetDimensions[1]      = Atmo.Medium.AtmosphereThicknessMeters;
    SkyUni.PlanetDimensions[2]      = Atmo.Medium.RayleighScaleHeightMeters;
    SkyUni.PlanetDimensions[3]      = Atmo.Medium.MieScaleHeightMeters;

    SkyUni.SkyTintBrightness[0]     = Atmo.Medium.SkyTint.R;
    SkyUni.SkyTintBrightness[1]     = Atmo.Medium.SkyTint.G;
    SkyUni.SkyTintBrightness[2]     = Atmo.Medium.SkyTint.B;
    SkyUni.SkyTintBrightness[3]     = Atmo.Medium.SkyBrightnessMultiplier;

    SkyUni.GroundAlbedoLimbGlow[0]  = Atmo.Medium.GroundAlbedo.R;
    SkyUni.GroundAlbedoLimbGlow[1]  = Atmo.Medium.GroundAlbedo.G;
    SkyUni.GroundAlbedoLimbGlow[2]  = Atmo.Medium.GroundAlbedo.B;
    SkyUni.GroundAlbedoLimbGlow[3]  = Atmo.Medium.LimbGlowIntensity;

    SkyUni.LutDimensions[0]         = AtmosphericSkySequence::SkyViewWidthPx;
    SkyUni.LutDimensions[1]         = AtmosphericSkySequence::SkyViewHeightPx;
    SkyUni.LutDimensions[2]         = (Atmo.Medium.ObserverAltitudeMeters > 0.0f)
        ? std::max(5u, static_cast<uint32_t>(std::lround(Atmo.Medium.ObserverAltitudeMeters)))
        : 100u;
    SkyUni.LutDimensions[3]         = (Atmo.Medium.OrbitalViewEnabled ||
                                       Atmo.Medium.ObserverAltitudeMeters > Atmo.Medium.AtmosphereThicknessMeters) ? 1u : 0u;

    static_assert(sizeof(SkyUni) <= 128u, "AtmosphericSkyUniformStd140 snapshot capacity check.");
    if (SkyViewLutValid && std::memcmp(&SkyUni, LastSkyViewUniformSnapshot.data(), sizeof(SkyUni)) == 0)
    {
        return;
    }
    std::memcpy(LastSkyViewUniformSnapshot.data(), &SkyUni, sizeof(SkyUni));
    SkyViewLutValid = true;

    std::memcpy(SkyViewUniformBuffer.HostAddress, &SkyUni, sizeof(SkyUni));

    const std::array<ComputeSlotDescriptor, 2u> SkySlots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SkyViewUniformBuffer.BufferHandle,          sizeof(AtmosphericSkyUniformStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AtmosphericSkyViewLutBuffer.BufferHandle, VK_WHOLE_SIZE },
    }};

    auto InsertComputeBarrier = [&]()
    {
        VkMemoryBarrier ComputeToCompute {};
        ComputeToCompute.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        ComputeToCompute.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ComputeToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &ComputeToCompute,
            0u,
            nullptr,
            0u,
            nullptr);
    };

    const uint32_t TransGroupsX = (AtmosphericSkySequence::TransmittanceWidthPx  + 7u) / 8u;
    const uint32_t TransGroupsY = (AtmosphericSkySequence::TransmittanceHeightPx + 7u) / 8u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, TransmittanceLutProgramHandle, SkySlots, TransGroupsX, TransGroupsY, 1u))
    {
        InsertComputeBarrier();
    }

    const uint32_t MsGroupsX = (AtmosphericSkySequence::MultiScatterWidthPx  + 7u) / 8u;
    const uint32_t MsGroupsY = (AtmosphericSkySequence::MultiScatterHeightPx + 7u) / 8u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, MultiScatterLutProgramHandle, SkySlots, MsGroupsX, MsGroupsY, 1u))
    {
        InsertComputeBarrier();
    }

    const uint32_t SkyGroupsX = (AtmosphericSkySequence::SkyViewWidthPx  + 7u) / 8u;
    const uint32_t SkyGroupsY = (AtmosphericSkySequence::SkyViewHeightPx + 7u) / 8u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, SkyViewLutProgramHandle, SkySlots, SkyGroupsX, SkyGroupsY, 1u))
    {
        InsertComputeBarrier();
    }
}

void VulkanWorldRenderSequence::DispatchGpuStellarTileCull(
    VkCommandBuffer                Cmd,
    const PerspectiveCameraBasis&  Camera,
    const WorldSceneSpecification& Scene,
    uint32_t                       WidthPx,
    uint32_t                       HeightPx)
{
    if (StellarCullUniformBuffer.HostAddress == nullptr ||
        RawCatalogStarBuffer.BufferHandle == VK_NULL_HANDLE ||
        ProjectedCatalogStarBuffer.BufferHandle == VK_NULL_HANDLE ||
        StellarTileBinsBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return;
    }

    const uint32_t TileCols  = (std::max(1u, WidthPx) + 15u) / 16u;
    const uint32_t TileRows  = (std::max(1u, HeightPx) + 15u) / 16u;
    const uint32_t TileCount = TileCols * TileRows;
    const uint64_t TileBytes = static_cast<uint64_t>(TileCount) * 65ull * sizeof(uint32_t);

    if (StellarClearProgramHandle == UINT32_MAX || StellarCullProgramHandle == UINT32_MAX)
    {
        vkCmdFillBuffer(Cmd, StellarTileBinsBuffer.BufferHandle, 0ull, TileBytes, 0u);
        VkMemoryBarrier FillBarrier {};
        FillBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        FillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        FillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &FillBarrier,
            0u,
            nullptr,
            0u,
            nullptr);
        return;
    }

    const auto Basis = ComputeCameraBasis(Camera);
    const auto& Atmo = Scene.Atmosphere;
    const bool Orbital = Atmo.Medium.OrbitalViewEnabled ||
                         (Atmo.Medium.ObserverAltitudeMeters > Atmo.Medium.AtmosphereThicknessMeters);

    StellarCullUniformBlockStd140 CullUni {};
    CullUni.CameraForward[0]   = Basis.ForwardAxis.X;
    CullUni.CameraForward[1]   = Basis.ForwardAxis.Y;
    CullUni.CameraForward[2]   = Basis.ForwardAxis.Z;
    CullUni.CameraForward[3]   = Atmo.Stars.CelestialRotationDegrees * Constants::DegToRadFloat;
    CullUni.CameraRightTanX[0] = Basis.RightAxis.X;
    CullUni.CameraRightTanX[1] = Basis.RightAxis.Y;
    CullUni.CameraRightTanX[2] = Basis.RightAxis.Z;
    CullUni.CameraRightTanX[3] = Basis.TanHalfFovX;
    CullUni.CameraUpTanY[0]    = Basis.UpAxis.X;
    CullUni.CameraUpTanY[1]    = Basis.UpAxis.Y;
    CullUni.CameraUpTanY[2]    = Basis.UpAxis.Z;
    CullUni.CameraUpTanY[3]    = Basis.TanHalfFovY;
    CullUni.ViewportCatalog[0] = static_cast<float>(WidthPx);
    CullUni.ViewportCatalog[1] = static_cast<float>(HeightPx);
    CullUni.ViewportCatalog[2] = static_cast<float>(UploadedCatalogStarCount);
    CullUni.ViewportCatalog[3] = Atmo.Stars.LimitingMagnitude;
    CullUni.SkyFlags[0]        = Atmo.Stars.StarfieldEnabled ? 1.0f : 0.0f;
    CullUni.SkyFlags[1]        = Atmo.Stars.EnableBrightStarCatalog ? 1.0f : 0.0f;
    CullUni.SkyFlags[2]        = Orbital ? 1.0f : 0.0f;
    CullUni.SkyFlags[3]        = Atmo.Stars.BrightnessMultiplier;
    std::memcpy(StellarCullUniformBuffer.HostAddress, &CullUni, sizeof(CullUni));

    const std::array<ComputeSlotDescriptor, 4u> CullSlots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, StellarCullUniformBuffer.BufferHandle,   sizeof(StellarCullUniformBlockStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RawCatalogStarBuffer.BufferHandle,       VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ProjectedCatalogStarBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StellarTileBinsBuffer.BufferHandle,      VK_WHOLE_SIZE },
    }};

    auto InsertBarrier = [&]()
    {
        VkMemoryBarrier ComputeToCompute {};
        ComputeToCompute.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        ComputeToCompute.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ComputeToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &ComputeToCompute,
            0u,
            nullptr,
            0u,
            nullptr);
    };

    const uint32_t ClearGroups = (TileCount + 63u) / 64u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, StellarClearProgramHandle, CullSlots, ClearGroups, 1u, 1u))
    {
        InsertBarrier();
    }

    const uint32_t StarGroups = (std::max(1u, UploadedCatalogStarCount) + 63u) / 64u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, StellarCullProgramHandle, CullSlots, StarGroups, 1u, 1u))
    {
        InsertBarrier();
    }
}

bool VulkanWorldRenderSequence::RenderWorldViewportWithEngine(
    const WorldRenderConfiguration& Config,
    uint32_t                        FrameIndex,
    const PerspectiveCameraBasis&   Camera,
    const WorldSceneSpecification&  Scene,
    std::vector<uint8_t>&           OutDisplayRgba8)
{
    FRONTIER_THOUGHT_SCOPE("Render.WorldViewportWithEngine");
    return ExecuteWorldRenderDispatch(Config, FrameIndex, Camera, Scene, true, false, &OutDisplayRgba8);
}

bool VulkanWorldRenderSequence::RenderWorldViewportToGpuImage(
    const WorldRenderConfiguration& Config,
    uint32_t                        FrameIndex,
    const PerspectiveCameraBasis&   Camera,
    const WorldSceneSpecification&  Scene,
    VkImageView&                    OutDisplayImageView,
    uint32_t&                       OutWidthPx,
    uint32_t&                       OutHeightPx)
{
    OutDisplayImageView = VK_NULL_HANDLE;
    OutWidthPx          = 0u;
    OutHeightPx         = 0u;
    if (!ExecuteWorldRenderDispatch(Config, FrameIndex, Camera, Scene, false, true, nullptr))
    {
        return false;
    }
    if (DisplayOutputImage2D.ViewHandle == VK_NULL_HANDLE)
    {
        return false;
    }
    OutDisplayImageView = DisplayOutputImage2D.ViewHandle;
    OutWidthPx          = DisplayOutputImage2D.WidthPx;
    OutHeightPx         = DisplayOutputImage2D.HeightPx;
    return true;
}

bool VulkanWorldRenderSequence::ExecuteWorldRenderDispatch(
    const WorldRenderConfiguration& Config,
    uint32_t                        FrameIndex,
    const PerspectiveCameraBasis&   Camera,
    const WorldSceneSpecification&  Scene,
    bool                            CopyToReadbackStaging,
    bool                            CopyToDisplayImage2D,
    std::vector<uint8_t>*           OutDisplayRgba8)
{
    if (!QueryReady())
    {
        return false;
    }

    const auto StartClock = std::chrono::steady_clock::now();
    ReapRetiredDisplayImages(false);
    RetirePendingPrecompute(false);
    RetirePendingTransform(false);
    LastViewportTiming = {};

    if (!EnsureVolume3DResources())
    {
        return false;
    }

    if (!SceneUploadedOnce)
    {
        SynchronizeSceneResources(Scene, true);
    }
    if (LastSceneSyncMilliseconds > 0.0f)
    {
        LastViewportTiming.SceneSyncGpuMs += LastSceneSyncMilliseconds;
        LastSceneSyncMilliseconds = 0.0f;
    }

    const auto LodSyncStart = std::chrono::steady_clock::now();
    RefreshNaniteLodGeometry(Config, Camera, Scene, !CopyToReadbackStaging);
    const auto LodSyncEnd = std::chrono::steady_clock::now();
    LastViewportTiming.SceneSyncGpuMs += static_cast<float>(
        std::chrono::duration<double, std::milli>(LodSyncEnd - LodSyncStart).count());

    const uint32_t WidthPx    = std::max(1u, Camera.ViewportWidthPx);
    const uint32_t HeightPx   = std::max(1u, Camera.ViewportHeightPx);
    const bool     IsHwGpu    = QueryHardwareGpuActive();
    const float    MaxScale   = IsHwGpu ? 1.0f : 0.50f;
    const float    CloudScale = std::clamp(Scene.Atmosphere.Clouds.CloudRenderScale, 0.25f, MaxScale);
    const uint32_t CloudW     = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(WidthPx)  * CloudScale)));
    const uint32_t CloudH     = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(HeightPx) * CloudScale)));
    const uint64_t OutBytes   = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    const uint64_t AccBytes   = static_cast<uint64_t>(WidthPx) * HeightPx * 16ull;
    const uint64_t CloudBytes = static_cast<uint64_t>(CloudW) * CloudH * 16ull;
    EnsureDeviceBufferCapacity(OutputPixelBuffer, OutBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (CopyToReadbackStaging)
    {
        EnsureReadbackBufferCapacity(OutputReadbackStagingBuffer, OutBytes);
    }
    if (CopyToDisplayImage2D)
    {
        EnsureDisplayOutputImage(WidthPx, HeightPx);
    }
    EnsureDeviceBufferCapacity(AccumulationHdrBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufPosMaskBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufNormAoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufDirectBaseBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufSunContribBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufAlbedoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PingShadowAoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongShadowAoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PingIndirectIrradianceBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongIndirectIrradianceBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalDistanceFieldBuffer, 2ull * 262144ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalRadiosityFieldBuffer, 299520ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalSurfaceShellBuffer, 262144ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(CloudRadiometricBuffer, CloudBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const uint32_t StarTileCols  = (std::max(1u, WidthPx) + 15u) / 16u;
    const uint32_t StarTileRows  = (std::max(1u, HeightPx) + 15u) / 16u;
    const uint64_t StarTileBytes = static_cast<uint64_t>(StarTileCols) * static_cast<uint64_t>(StarTileRows) * 65ull * sizeof(uint32_t);
    EnsureDeviceBufferCapacity(StellarTileBinsBuffer, std::max<uint64_t>(64ull, StarTileBytes), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (OutputPixelBuffer.BufferHandle == VK_NULL_HANDLE ||
        (CopyToReadbackStaging && OutputReadbackStagingBuffer.HostAddress == nullptr) ||
        (CopyToDisplayImage2D && DisplayOutputImage2D.ImageHandle == VK_NULL_HANDLE) ||
        AccumulationHdrBuffer.BufferHandle == VK_NULL_HANDLE ||
        GBufPosMaskBuffer.BufferHandle == VK_NULL_HANDLE ||
        PongShadowAoBuffer.BufferHandle == VK_NULL_HANDLE ||
        PongIndirectIrradianceBuffer.BufferHandle == VK_NULL_HANDLE ||
        GlobalDistanceFieldBuffer.BufferHandle == VK_NULL_HANDLE ||
        GlobalRadiosityFieldBuffer.BufferHandle == VK_NULL_HANDLE ||
        GlobalSurfaceShellBuffer.BufferHandle == VK_NULL_HANDLE ||
        CloudRadiometricBuffer.BufferHandle == VK_NULL_HANDLE ||
        StellarTileBinsBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);

    WorldViewportUniformBlockStd140 Uniforms {};
    Uniforms.CameraOriginTanX[0]    = Camera.CameraOrigin.X;
    Uniforms.CameraOriginTanX[1]    = Camera.CameraOrigin.Y;
    Uniforms.CameraOriginTanX[2]    = Camera.CameraOrigin.Z;
    Uniforms.CameraOriginTanX[3]    = Basis.TanHalfFovX;

    Uniforms.CameraRightTanY[0]     = Basis.RightAxis.X;
    Uniforms.CameraRightTanY[1]     = Basis.RightAxis.Y;
    Uniforms.CameraRightTanY[2]     = Basis.RightAxis.Z;
    Uniforms.CameraRightTanY[3]     = Basis.TanHalfFovY;

    Uniforms.CameraUpWidth[0]       = Basis.UpAxis.X;
    Uniforms.CameraUpWidth[1]       = Basis.UpAxis.Y;
    Uniforms.CameraUpWidth[2]       = Basis.UpAxis.Z;
    Uniforms.CameraUpWidth[3]       = static_cast<float>(WidthPx);

    Uniforms.CameraForwardHeight[0] = Basis.ForwardAxis.X;
    Uniforms.CameraForwardHeight[1] = Basis.ForwardAxis.Y;
    Uniforms.CameraForwardHeight[2] = Basis.ForwardAxis.Z;
    Uniforms.CameraForwardHeight[3] = static_cast<float>(HeightPx);
    Uniforms.DenoiseStepParams[2]   =
        ((UploadedPhotometricProfiles & 0xFFFFu) << 16u) |
        (UploadedPhotometricLamps & 0xFFFFu);
    Uniforms.DenoiseStepParams[3]   = FrameSequenceIndex++;

    const bool AtmoActive = Scene.Atmosphere.Medium.AtmosphereEnabled || Scene.Atmosphere.Medium.OrbitalViewEnabled;
    bool       AnyMoonVisible = false;
    for (const auto& Moon : Scene.Atmosphere.Moons)
    {
        if (Moon.Visible) { AnyMoonVisible = true; break; }
    }
    const bool CelestialOrCloudsActive =
        Scene.Atmosphere.Solar.SolarEnabled ||
        Scene.Atmosphere.Clouds.CloudsEnabled ||
        Scene.Atmosphere.Stars.StarfieldEnabled ||
        AnyMoonVisible;
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
    const Mathematics::Axis3D SunDir = (AtmoActive || Scene.Atmosphere.Solar.SolarEnabled)
        ? SolarSolved.WorldDirectionToSun
        : Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });

    const bool DynamicInstancesChanged =
        !Scene.ClusterInstances.empty() && SynchronizeLocalSdfResources(Scene);
    const Mathematics::Axis3D DesiredClipFocus = ResolveDynamicClipFocusWorld(&Camera, Scene);
    const bool SunDirectionShifted = Mathematics::EvaluateDot(SunDir, LastSyncedSunDir) < 0.99995f;

    uint32_t PrecomputeMask = 0u;
    if (!ActiveClipFocusInitialized || !CascadeFocusInitialized || DynamicInstancesChanged)
    {
        PrecomputeMask = kPrecomputeFullMask;
    }
    else
    {
        if (SunDirectionShifted)
        {
            PrecomputeMask |= (kPrecomputeFineShadow | kPrecomputeCoarseShadow | kPrecomputeRadiosity | kPrecomputeRadiosityFull | kPrecomputeSecondBounce);
        }
        const bool UsesGdfDebug =
            (Config.DebugChannel == DebugChannelFilteredAo) ||
            (Config.DebugChannel >= DebugChannelDistanceHeat && Config.DebugChannel <= DebugChannelCascadeTint);
        const bool UsesUnshadedDebug =
            (Config.DebugChannel != 0u) && !UsesGdfDebug &&
            !(Config.DebugChannel >= 8u && Config.DebugChannel <= 11u);
        const bool DistanceRadiosityActive =
            UsesGdfDebug ||
            (!UsesUnshadedDebug &&
             (Config.Engine == WorldRenderEngine::OpenPbrDirect ||
              Config.Engine == WorldRenderEngine::RealTimeGiGtxLowEnd));
        if (DistanceRadiosityActive)
        {
            const Mathematics::Axis3D BoundsSpan    = Scene.SceneTree.RootBoundsMax - Scene.SceneTree.RootBoundsMin;
            const float               MaxHalfExtent = 0.5f * std::max({ std::abs(BoundsSpan.X), std::abs(BoundsSpan.Y), std::abs(BoundsSpan.Z), 5.6f });
            const float               FineGdfHalf   = std::round(std::clamp(MaxHalfExtent * 1.18f, 3.25f, 4.25f) * 8.0f) * 0.125f;
            const float               CoarseGdfHalf = std::round(std::clamp(MaxHalfExtent * 1.15f, FineGdfHalf * 2.5f, 24.0f) * 8.0f) * 0.125f;
            const float               FineVoxelM    = (2.0f * FineGdfHalf) / 64.0f;
            const float               CoarseVoxelM  = (2.0f * CoarseGdfHalf) / 64.0f;

            const bool UsesRadiosityField =
                (Config.DebugChannel == 14u || Config.DebugChannel == 15u) ||
                (!UsesUnshadedDebug && Config.Engine == WorldRenderEngine::RealTimeGiGtxLowEnd);

            const float FineAxisDrift = std::max({
                std::abs(DesiredClipFocus.X - CachedFineGdfCenterSpan[0]),
                std::abs(DesiredClipFocus.Y - CachedFineGdfCenterSpan[1]),
                std::abs(DesiredClipFocus.Z - CachedFineGdfCenterSpan[2]),
            });
            if (!FineToroidalValid || FineAxisDrift >= FineVoxelM * 4.35f)
            {
                PrecomputeMask |= kPrecomputeFineGdf;
                if (UsesRadiosityField)
                {
                    PrecomputeMask |= kPrecomputeRadiosity;
                    if (PendingRadiosityRefresh)
                    {
                        PrecomputeMask |= kPrecomputeRadiosityFull;
                    }
                    if (CopyToReadbackStaging)
                    {
                        PrecomputeMask |= kPrecomputeSecondBounce;
                    }
                }
                else
                {
                    PendingRadiosityRefresh = true;
                }
            }
            else if (UsesRadiosityField && PendingRadiosityRefresh)
            {
                PrecomputeMask |= (kPrecomputeRadiosity | kPrecomputeRadiosityFull);
                if (CopyToReadbackStaging)
                {
                    PrecomputeMask |= kPrecomputeSecondBounce;
                }
            }

            const float CoarseAxisDrift = std::max({
                std::abs(DesiredClipFocus.X - CachedCoarseGdfCenterSpan[0]),
                std::abs(DesiredClipFocus.Y - CachedCoarseGdfCenterSpan[1]),
                std::abs(DesiredClipFocus.Z - CachedCoarseGdfCenterSpan[2]),
            });
            if (!CoarseToroidalValid ||
                (MaxHalfExtent > CoarseGdfHalf && CoarseAxisDrift >= CoarseVoxelM * 4.35f))
            {
                PrecomputeMask |= kPrecomputeCoarseGdf;
            }
        }

        // Scroll the Fine Sun Shadow Cascade when the camera focus navigates outside its active core,
        // while keeping it anchored with wide hysteresis near foreground subjects (zero shimmer or redundant bakes).
        constexpr float FineSunHalfMeters = 6.00f;
        const Mathematics::Axis3D SceneObjectAnchor = ResolveDynamicClipFocusWorld(nullptr, Scene);
        const float FocusFromObject = std::max({
            std::abs(DesiredClipFocus.X - SceneObjectAnchor.X),
            std::abs(DesiredClipFocus.Y - SceneObjectAnchor.Y),
            std::abs(DesiredClipFocus.Z - SceneObjectAnchor.Z),
        });
        const Mathematics::Axis3D DesiredFineShadowAnchor =
            (FocusFromObject <= FineSunHalfMeters * 0.55f) ? SceneObjectAnchor : DesiredClipFocus;
        const float FineShadowDrift = std::max({
            std::abs(DesiredFineShadowAnchor.X - LastFineShadowFocusWorld.X),
            std::abs(DesiredFineShadowAnchor.Y - LastFineShadowFocusWorld.Y),
            std::abs(DesiredFineShadowAnchor.Z - LastFineShadowFocusWorld.Z),
        });
        if (FineShadowDrift >= FineSunHalfMeters * 0.55f)
        {
            PrecomputeMask |= kPrecomputeFineShadow;
        }
    }

    if (ShadowCascadeProgramHandle != UINT32_MAX && PrecomputeMask != 0u)
    {
        ActiveClipFocusWorld       = DesiredClipFocus;
        ActiveClipFocusInitialized = true;
        const auto PreSyncStart = std::chrono::steady_clock::now();
        const bool RebuildGdf   = ((PrecomputeMask & (kPrecomputeFineGdf | kPrecomputeCoarseGdf)) != 0u);
        (void)DispatchGpuScenePrecompute(Scene, RebuildGdf, PrecomputeMask);
        const auto PreSyncEnd = std::chrono::steady_clock::now();
        LastViewportTiming.SceneSyncGpuMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(PreSyncEnd - PreSyncStart).count());
    }
    Uniforms.SunDirConeTan[0]       = SunDir.X;
    Uniforms.SunDirConeTan[1]       = SunDir.Y;
    Uniforms.SunDirConeTan[2]       = SunDir.Z;
    const float AuthoredConeRad     = std::max(0.014f, Scene.Sun.SunAngularRadiusRadians);
    const float AtmoConeRad         = Scene.Atmosphere.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat;
    Uniforms.SunDirConeTan[3]       = std::tan(AtmoActive ? std::max(AuthoredConeRad, AtmoConeRad)
                                                          : std::max(0.004f, Scene.Sun.SunAngularRadiusRadians));

    Mathematics::LinearRGB EffectiveSunIrrad = Scene.Sun.SolarIrradiance;
    if (AtmoActive)
    {
        const float ObsAlt = Scene.Atmosphere.Medium.ObserverAltitudeMeters;
        const auto SolarTrans = AtmosphericSkySequence::ComputeSolarTransmittance(SunDir, Scene.Atmosphere.Medium, ObsAlt);
        const bool IsOrbital = Scene.Atmosphere.Medium.OrbitalViewEnabled ||
                               (ObsAlt > Scene.Atmosphere.Medium.AtmosphereThicknessMeters);
        const float SunVis     = IsOrbital ? 1.0f : std::clamp((SolarSolved.ElevationDegrees + 4.0f) / 10.0f, 0.0f, 1.0f);
        const float IrradScale = Scene.Atmosphere.Solar.SolarIntensityMultiplier *
                                 Scene.Atmosphere.Solar.DirectGainMultiplier * 0.044f * SunVis;
        EffectiveSunIrrad = Mathematics::LinearRGB{
            SolarSolved.EffectiveSolarTint.R * SolarTrans.R * IrradScale,
            SolarSolved.EffectiveSolarTint.G * SolarTrans.G * IrradScale,
            SolarSolved.EffectiveSolarTint.B * SolarTrans.B * IrradScale,
        };
    }

    Uniforms.SolarIrradiancePad[0]  = EffectiveSunIrrad.R;
    Uniforms.SolarIrradiancePad[1]  = EffectiveSunIrrad.G;
    Uniforms.SolarIrradiancePad[2]  = EffectiveSunIrrad.B;
    Uniforms.SolarIrradiancePad[3]  = std::max(120.0f, Camera.FarClipMeters);
    Uniforms.SkyRadianceBvhCount[0] = Scene.SkyRadiance.R;
    Uniforms.SkyRadianceBvhCount[1] = Scene.SkyRadiance.G;
    Uniforms.SkyRadianceBvhCount[2] = Scene.SkyRadiance.B;
    Uniforms.SkyRadianceBvhCount[3] = static_cast<float>(UploadedBvhCount);

    const uint32_t LightCount = static_cast<uint32_t>(std::min<std::size_t>(4u, Scene.AreaLights.size()));
    for (uint32_t Idx = 0u; Idx < LightCount; ++Idx)
    {
        const auto& Src = Scene.AreaLights[Idx];
        auto&       Dst = Uniforms.AreaLights[Idx];
        Dst.CenterHalfWidth[0] = Src.CenterWorld.X;
        Dst.CenterHalfWidth[1] = Src.CenterWorld.Y;
        Dst.CenterHalfWidth[2] = Src.CenterWorld.Z;
        Dst.CenterHalfWidth[3] = Src.HalfWidthMeters;
        Dst.RightHalfHeight[0] = Src.RightTangent.X;
        Dst.RightHalfHeight[1] = Src.RightTangent.Y;
        Dst.RightHalfHeight[2] = Src.RightTangent.Z;
        Dst.RightHalfHeight[3] = Src.HalfHeightMeters;
        Dst.UpTwoSided[0]      = Src.UpBitangent.X;
        Dst.UpTwoSided[1]      = Src.UpBitangent.Y;
        Dst.UpTwoSided[2]      = Src.UpBitangent.Z;
        Dst.UpTwoSided[3]      = Src.TwoSidedEmission ? 1.0f : 0.0f;
        Dst.RadiancePacked[0]  = Src.EmittedRadiance.R;
        Dst.RadiancePacked[1]  = Src.EmittedRadiance.G;
        Dst.RadiancePacked[2]  = Src.EmittedRadiance.B;
    }

    uint32_t GlassCount = 0u;
    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (!Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f || GlassCount >= 4u)
        {
            continue;
        }
        const float LocalRadius = ComputePyramidLocalHalfExtent(Inst.PyramidAsset);
        const auto& GlassMat    = Scene.MaterialRegister.InspectAuthoredMaterial(Inst.MaterialSlot);
        auto&       Dst         = Uniforms.GlassSpheres[GlassCount++];
        Dst.CenterRadiusPacked[0] = Inst.WorldTranslation.X;
        Dst.CenterRadiusPacked[1] = Inst.WorldTranslation.Y;
        Dst.CenterRadiusPacked[2] = Inst.WorldTranslation.Z;
        Dst.CenterRadiusPacked[3] = std::max(0.04f, LocalRadius * Inst.UniformScale);
        Dst.OpticsSlotPacked[0]   = GlassMat.Specular.DielectricIOR;
        Dst.OpticsSlotPacked[1]   = GlassMat.Transmission.AbbeDispersionNumber;
        Dst.OpticsSlotPacked[2]   = static_cast<float>(Inst.MaterialSlot);
        Dst.OpticsSlotPacked[3]   = GlassMat.Specular.Roughness;
    }

    Uniforms.LightGlassMaterialDebug[0] = LightCount;
    Uniforms.LightGlassMaterialDebug[1] = GlassCount;
    Uniforms.LightGlassMaterialDebug[2] = UploadedMaterialCount;
    Uniforms.LightGlassMaterialDebug[3] = Config.DebugChannel;

    const uint32_t EngineOrdinal   = static_cast<uint32_t>(Config.Engine);
    const uint32_t PenumbraOrdinal = static_cast<uint32_t>(Config.SdfPenumbraMode);
    const bool CameraOrModeShifted =
        EngineOrdinal != LastEngineOrdinal ||
        PenumbraOrdinal != LastPenumbraOrdinal ||
        Mathematics::EvaluateLengthSquared(Camera.CameraOrigin - LastCameraOrigin) > 1.0e-7f ||
        Mathematics::EvaluateLengthSquared(Basis.ForwardAxis - LastCameraForward) > 1.0e-7f;
    if (CameraOrModeShifted || EngineOrdinal < static_cast<uint32_t>(WorldRenderEngine::PathTracerGameRealtime))
    {
        ProgressiveSampleCount = 0u;
    }
    LastEngineOrdinal   = EngineOrdinal;
    LastPenumbraOrdinal = PenumbraOrdinal;
    LastCameraOrigin    = Camera.CameraOrigin;
    LastCameraForward   = Basis.ForwardAxis;

    // Progressive PT: accumulate PathTraceSamples spp per dispatch (clamped to the convergence
    // target when one is set); the editor keeps dispatching until the target is reached.
    const bool     ProgressivePtActive =
        EngineOrdinal >= static_cast<uint32_t>(WorldRenderEngine::PathTracerGameRealtime);
    uint32_t       ProgressiveBatchSpp = 1u;
    if (ProgressivePtActive)
    {
        ProgressiveBatchSpp = std::clamp(Config.PathTraceSamples, 1u, 8u);
        if (Config.PathTraceTargetSpp > 0u)
        {
            const uint32_t RemainingSpp = (Config.PathTraceTargetSpp > ProgressiveSampleCount)
                ? (Config.PathTraceTargetSpp - ProgressiveSampleCount)
                : 0u;
            ProgressiveBatchSpp = std::min(ProgressiveBatchSpp, std::max(1u, RemainingSpp));
        }
    }
    const uint32_t EffectiveTickOrSample = ProgressivePtActive ? ProgressiveSampleCount : FrameIndex;
    if (ProgressivePtActive)
    {
        ProgressiveSampleCount += ProgressiveBatchSpp;
    }

    Uniforms.EngineShadowReflPad[0] = EngineOrdinal;
    Uniforms.EngineShadowReflPad[1] = static_cast<uint32_t>(Config.ShadowAlgorithm);
    Uniforms.EngineShadowReflPad[2] = static_cast<uint32_t>(Config.ReflectionSource);
    Uniforms.EngineShadowReflPad[3] = EffectiveTickOrSample;

    Uniforms.SunCascadeCenterSpan[0] = CachedSunCascade.CenterWorld.X;
    Uniforms.SunCascadeCenterSpan[1] = CachedSunCascade.CenterWorld.Y;
    Uniforms.SunCascadeCenterSpan[2] = CachedSunCascade.CenterWorld.Z;
    Uniforms.SunCascadeCenterSpan[3] = CachedSunCascade.HalfSpanMeters;

    Uniforms.SunCascadeRightRes[0]   = CachedSunCascade.LightRightAxis.X;
    Uniforms.SunCascadeRightRes[1]   = CachedSunCascade.LightRightAxis.Y;
    Uniforms.SunCascadeRightRes[2]   = CachedSunCascade.LightRightAxis.Z;
    Uniforms.SunCascadeRightRes[3]   = static_cast<float>(CachedSunCascade.ResolutionTexels);

    Uniforms.SunCascadeUpDepth[0]    = CachedSunCascade.LightUpAxis.X;
    Uniforms.SunCascadeUpDepth[1]    = CachedSunCascade.LightUpAxis.Y;
    Uniforms.SunCascadeUpDepth[2]    = CachedSunCascade.LightUpAxis.Z;
    Uniforms.SunCascadeUpDepth[3]    = CachedSunCascade.DepthExtentMeters;

    Uniforms.SunCascadeForwardPad[0] = CachedSunCascade.LightForwardAxis.X;
    Uniforms.SunCascadeForwardPad[1] = CachedSunCascade.LightForwardAxis.Y;
    Uniforms.SunCascadeForwardPad[2] = CachedSunCascade.LightForwardAxis.Z;
    Uniforms.SunCascadeForwardPad[3] = std::clamp(Config.NanitePixelError, 0.10f, 16.0f);

    Uniforms.FineCascadeCenterSpan[0] = CachedFineSunCascade.CenterWorld.X;
    Uniforms.FineCascadeCenterSpan[1] = CachedFineSunCascade.CenterWorld.Y;
    Uniforms.FineCascadeCenterSpan[2] = CachedFineSunCascade.CenterWorld.Z;
    Uniforms.FineCascadeCenterSpan[3] = CachedFineSunCascade.HalfSpanMeters;

    Uniforms.FineCascadeParams[0] = static_cast<float>(CachedFineSunCascade.ResolutionTexels);
    Uniforms.FineCascadeParams[1] = CachedFineSunCascade.DepthExtentMeters;
    Uniforms.FineCascadeParams[2] = static_cast<float>(CoarseShadowOffsetTexels);

    Uniforms.AtmosphereMaskPad[0] = (AtmoActive ? 1u : 0u) | (CelestialOrCloudsActive ? 2u : 0u);
    Uniforms.AtmosphereMaskPad[1] = AtmosphericSkySequence::SkyViewWidthPx;
    Uniforms.AtmosphereMaskPad[2] = AtmosphericSkySequence::SkyViewHeightPx;
    Uniforms.AtmosphereMaskPad[3] =
        (Config.ReflectionsEnabled     ? 0u : 2u) |
        (Config.RaytracingEnabled      ? 0u : 8u) |
        ((static_cast<uint32_t>(Config.SdfPenumbraMode) & 0x7u) << 16u) |
        ((static_cast<uint32_t>(Config.GiCandidate) & 0x3u) << 20u) |
        (Config.NaniteWireframeOverlay ? (1u << 22u) : 0u);

    {
        const auto& Atmo = Scene.Atmosphere;
        const auto& Deck = Atmo.Clouds;
        ResolvedWindSample DeckWind {};
        if (Deck.FollowWindEnabled)
        {
            DeckWind = AtmosphericWindSolver::ResolveWindSample(
                Atmo.Wind,
                Deck.WindSpeedMetersPerSecond,
                Deck.WindBearingDegrees,
                Deck.WindSource,
                { 0.0f, 0.0f, Deck.CloudFloorMeters },
                Deck.CloudFloorMeters,
                Atmo.SimulationSeconds);
        }

        auto& C = Uniforms.CloudParams;
        C.OriginTimeVector[0]    = Camera.CameraOrigin.X;
        C.OriginTimeVector[1]    = Camera.CameraOrigin.Y;
        C.OriginTimeVector[2]    = Camera.CameraOrigin.Z;
        C.OriginTimeVector[3]    = Atmo.SimulationSeconds;
        C.SunAxisToggle[0]       = SunDir.X;
        C.SunAxisToggle[1]       = SunDir.Y;
        C.SunAxisToggle[2]       = SunDir.Z;
        C.SunAxisToggle[3]       = Deck.CloudsEnabled ? 1.0f : 0.0f;
        C.SolarIrradianceExt[0]  = EffectiveSunIrrad.R;
        C.SolarIrradianceExt[1]  = EffectiveSunIrrad.G;
        C.SolarIrradianceExt[2]  = EffectiveSunIrrad.B;
        C.SolarIrradianceExt[3]  = Atmo.Medium.PlanetRadiusMeters;
        C.SkyColorWind[0]        = Scene.SkyRadiance.R;
        C.SkyColorWind[1]        = Scene.SkyRadiance.G;
        C.SkyColorWind[2]        = Scene.SkyRadiance.B;
        C.SkyColorWind[3]        = Deck.FollowWindEnabled ? 1.0f : 0.0f;
        C.DeckGeometry[0]        = std::clamp(Deck.CloudFloorMeters, 100.0f, std::max(200.0f, Deck.DeckCeilingMeters - 100.0f));
        C.DeckGeometry[1]        = Deck.LayerThicknessMeters;
        C.DeckGeometry[2]        = Deck.CloudFraction;
        C.DeckGeometry[3]        = Deck.ExtinctionMultiplier;
        C.DeckOptics[0]          = Deck.FeatureScaleMultiplier;
        C.DeckOptics[1]          = Deck.AnvilTopSpread;
        C.DeckOptics[2]          = Deck.ForwardScatterAnisotropy;
        C.DeckOptics[3]          = Deck.WindShearPerKilometer;
        C.WindVectorOcclude[0]   = DeckWind.VelocityMetersPerSec.X;
        C.WindVectorOcclude[1]   = DeckWind.VelocityMetersPerSec.Y;
        C.WindVectorOcclude[2]   = Deck.CloudShadow.ShadowEnabled ? 1.0f : 0.0f;
        C.WindVectorOcclude[3]   = Atmo.LocalCloud.LocalEnabled ? 1.0f : 0.0f;
        C.LocalCenterExt[0]      = Atmo.LocalCloud.VolumeCenterMeters.X;
        C.LocalCenterExt[1]      = Atmo.LocalCloud.VolumeCenterMeters.Y;
        C.LocalCenterExt[2]      = Atmo.LocalCloud.VolumeCenterMeters.Z;
        C.LocalCenterExt[3]      = Atmo.LocalCloud.FeatureScaleMeters;
        C.LocalExtentFraction[0] = Atmo.LocalCloud.HalfExtentMeters.X;
        C.LocalExtentFraction[1] = Atmo.LocalCloud.HalfExtentMeters.Y;
        C.LocalExtentFraction[2] = Atmo.LocalCloud.HalfExtentMeters.Z;
        C.LocalExtentFraction[3] = Atmo.LocalCloud.CloudFraction;
        C.LocalDensityPhase[0]   = Atmo.LocalCloud.ExtinctionMultiplier;
        C.LocalDensityPhase[1]   = Atmo.LocalCloud.ForwardScatterAnisotropy;
        C.LocalDensityPhase[2]   = static_cast<float>(WidthPx);
        C.LocalDensityPhase[3]   = static_cast<float>(HeightPx);
        C.FormationOptics[0]     = static_cast<float>(static_cast<uint32_t>(Deck.Category));
        C.FormationOptics[1]     = Deck.WindVeerDegreesPerKm * Constants::DegToRadFloat;
        C.FormationOptics[2]     = std::max(Deck.WindTurbulence, DeckWind.TurbulenceScalar);
        C.CloudShadowOptics[0]   = Deck.CloudShadow.ShadowFraction;
        C.CloudShadowOptics[1]   = Deck.CloudShadow.ShadowDensityMultiplier;
        C.CloudShadowOptics[2]   = Deck.CloudShadow.ShadowScale;
        C.CloudShadowOptics[3]   = Deck.CloudShadow.ShadowAnvil;
        C.CloudPassControl[0]    = (AtmoActive && Deck.CloudsEnabled) ? static_cast<float>(CloudW) : 0.0f;
        C.CloudPassControl[1]    = (AtmoActive && Deck.CloudsEnabled) ? static_cast<float>(CloudH) : 0.0f;
        const uint32_t EffSteps  = IsHwGpu
            ? std::clamp(std::max(Deck.CloudMarchSteps, 64u), 48u, 128u)
            : std::clamp(Deck.CloudMarchSteps, 24u, 36u);
        C.CloudPassControl[2]    = static_cast<float>(EffSteps);

        const auto BaseCloudWind = Deck.FollowWindEnabled
            ? AtmosphericWindSolver::ResolveWindSample(
                  Atmo.Wind, Deck.WindSpeedMetersPerSecond, Deck.WindBearingDegrees,
                  Deck.WindSource, { 0.0f, 0.0f, Deck.CloudFloorMeters }, Deck.CloudFloorMeters,
                  Atmo.SimulationSeconds)
            : ResolvedWindSample {};
        C.CloudPassControl[3] = Basis.TanHalfFovY;
        C.WindForceMeta[0]    = 1600.0f;
        C.WindForceMeta[1]    = BaseCloudWind.VelocityMetersPerSec.X;
        C.WindForceMeta[2]    = BaseCloudWind.VelocityMetersPerSec.Y;
        C.WindForceMeta[3]    = Atmo.Medium.ObserverAltitudeMeters;

        const float CloudSolarScale = std::max(0.0f, Atmo.Solar.SolarIntensityMultiplier * Atmo.Medium.SkyBrightnessMultiplier * 0.72f);
        C.CloudToaSolarIrrad[0] = SolarSolved.EffectiveSolarTint.R * CloudSolarScale;
        C.CloudToaSolarIrrad[1] = SolarSolved.EffectiveSolarTint.G * CloudSolarScale;
        C.CloudToaSolarIrrad[2] = SolarSolved.EffectiveSolarTint.B * CloudSolarScale;
        C.CloudToaSolarIrrad[3] = 0.04f;
        const float AmbGain     = CloudSolarScale / 16.0f;
        C.CloudSkyAmbient[0]    = 0.37f * AmbGain * Atmo.Medium.SkyTint.R;
        C.CloudSkyAmbient[1]    = 0.46f * AmbGain * Atmo.Medium.SkyTint.G;
        C.CloudSkyAmbient[2]    = 0.62f * AmbGain * Atmo.Medium.SkyTint.B;
        C.CloudSkyAmbient[3]    = IsHwGpu ? 8.0f : 4.0f;
        C.CloudAtmoStrengths[0] = Atmo.Medium.RayleighStrength;
        C.CloudAtmoStrengths[1] = Atmo.Medium.MieStrength;
        C.CloudAtmoStrengths[2] = Atmo.Medium.OzoneStrength;
        C.CloudAtmoStrengths[3] = Atmo.Medium.AtmosphereThicknessMeters;
        C.CloudAirScalesCyclone[0] = Atmo.Medium.RayleighScaleHeightMeters;
        C.CloudAirScalesCyclone[1] = Atmo.Medium.MieScaleHeightMeters;
        C.CloudAirScalesCyclone[2] = 28.0f;
        C.CloudAirScalesCyclone[3] = 22000.0f;

        uint32_t LocalCloudCount = 0u;
        auto PackLocalCloud = [&](const BoundedCloudVolume& Vol)
        {
            if (LocalCloudCount >= 8u || !Vol.LocalEnabled || Vol.CloudFraction <= 0.01f || Vol.ExtinctionMultiplier <= 0.01f)
            {
                return;
            }
            const ResolvedWindSample W = Vol.FollowWindEnabled
                ? AtmosphericWindSolver::ResolveWindSample(
                      Atmo.Wind, Deck.WindSpeedMetersPerSecond, Deck.WindBearingDegrees,
                      Vol.WindSource, Vol.VolumeCenterMeters, Vol.VolumeCenterMeters.Z,
                      Atmo.SimulationSeconds)
                : ResolvedWindSample {};
            auto& Dst = C.LocalClouds[LocalCloudCount++];
            Dst.CenterShape[0]          = Vol.VolumeCenterMeters.X;
            Dst.CenterShape[1]          = Vol.VolumeCenterMeters.Y;
            Dst.CenterShape[2]          = Vol.VolumeCenterMeters.Z;
            Dst.CenterShape[3]          = static_cast<float>(static_cast<uint32_t>(Vol.Shape));
            Dst.HalfExtentYaw[0]        = Vol.HalfExtentMeters.X;
            Dst.HalfExtentYaw[1]        = Vol.HalfExtentMeters.Y;
            Dst.HalfExtentYaw[2]        = Vol.HalfExtentMeters.Z;
            Dst.HalfExtentYaw[3]        = Vol.YawDegrees * Constants::DegToRadFloat;
            Dst.DensityCovScaleSides[0] = Vol.ExtinctionMultiplier;
            Dst.DensityCovScaleSides[1] = Vol.CloudFraction;
            Dst.DensityCovScaleSides[2] = 1.0f / std::max(2.0f, Vol.FeatureScaleMeters);
            Dst.DensityCovScaleSides[3] = static_cast<float>(std::clamp(Vol.PrismSides, 3u, 16u));
            Dst.WindVelCurl[0]          = W.VelocityMetersPerSec.X;
            Dst.WindVelCurl[1]          = W.VelocityMetersPerSec.Y;
            Dst.WindVelCurl[2]          = W.VelocityMetersPerSec.Z;
            Dst.WindVelCurl[3]          = W.CurlScalar;
            Dst.SoftAnisoPad[0]         = Vol.EdgeSoftness;
            Dst.SoftAnisoPad[1]         = Vol.ForwardScatterAnisotropy;
            Dst.SoftAnisoPad[2]         = Vol.FollowWindEnabled ? 1.0f : 0.0f;
            Dst.SoftAnisoPad[3]         = 0.0f;
        };
        if (Atmo.LocalCloud.LocalEnabled)
        {
            PackLocalCloud(Atmo.LocalCloud);
        }
        for (const auto& Vol : Atmo.LocalClouds)
        {
            PackLocalCloud(Vol);
        }
        C.FormationOptics[3] = static_cast<float>(LocalCloudCount);

        const auto& Fg = Atmo.Fog;
        auto& F        = Uniforms.FogParams;
        F.HeightParams0[0] = Fg.HeightFog.Enabled ? 1.0f : 0.0f;
        F.HeightParams0[1] = Fg.HeightFog.Density;
        F.HeightParams0[2] = Fg.HeightFog.FalloffHeightMeters;
        F.HeightParams0[3] = Fg.HeightFog.BaseAltitudeMeters;
        F.HeightParams1[0] = Fg.HeightFog.HeightColour.R;
        F.HeightParams1[1] = Fg.HeightFog.HeightColour.G;
        F.HeightParams1[2] = Fg.HeightFog.HeightColour.B;
        F.HeightParams1[3] = Fg.HeightFog.SunScatterIntensity;
        F.AerialParams[0]  = Fg.AerialFog.Enabled ? 1.0f : 0.0f;
        F.AerialParams[1]  = Fg.AerialFog.DensityMultiplier;
        F.AerialParams[2]  = Fg.AerialFog.StartDistanceMeters;
        F.AerialParams[3]  = Fg.AerialFog.MieBlend;

        const ResolvedWindSample HeightWind = Fg.HeightFog.FollowWind
            ? AtmosphericWindSolver::ResolveWindSample(
                  Atmo.Wind, Deck.WindSpeedMetersPerSecond, Deck.WindBearingDegrees,
                  Fg.HeightFog.WindSource, Camera.CameraOrigin, Fg.HeightFog.BaseAltitudeMeters,
                  Atmo.SimulationSeconds)
            : ResolvedWindSample {};
        F.FogWindClock[0] = Fg.HeightFog.FollowWind ? 1.0f : 0.0f;
        F.FogWindClock[1] = HeightWind.VelocityMetersPerSec.X;
        F.FogWindClock[2] = HeightWind.VelocityMetersPerSec.Y;
        F.FogWindClock[3] = Atmo.SimulationSeconds;

        uint32_t LocalFogCount = 0u;
        for (const auto& Vol : Fg.LocalFogs)
        {
            if (LocalFogCount >= 8u || !Vol.Enabled || Vol.Density <= 1.0e-4f || Vol.Coverage <= 1.0e-4f)
            {
                continue;
            }
            const ResolvedWindSample FW = Vol.FollowWind
                ? AtmosphericWindSolver::ResolveWindSample(
                      Atmo.Wind, Deck.WindSpeedMetersPerSecond, Deck.WindBearingDegrees,
                      Vol.WindSource, Vol.VolumeCenterMeters, Vol.VolumeCenterMeters.Z,
                      Atmo.SimulationSeconds)
                : ResolvedWindSample {};
            auto& Dst = F.LocalFogs[LocalFogCount++];
            Dst.CenterShape[0]          = Vol.VolumeCenterMeters.X;
            Dst.CenterShape[1]          = Vol.VolumeCenterMeters.Y;
            Dst.CenterShape[2]          = Vol.VolumeCenterMeters.Z;
            Dst.CenterShape[3]          = static_cast<float>(static_cast<uint32_t>(Vol.Shape));
            Dst.HalfExtentYaw[0]        = Vol.HalfExtentMeters.X;
            Dst.HalfExtentYaw[1]        = Vol.HalfExtentMeters.Y;
            Dst.HalfExtentYaw[2]        = Vol.HalfExtentMeters.Z;
            Dst.HalfExtentYaw[3]        = Vol.YawDegrees * Constants::DegToRadFloat;
            Dst.DensityCovScaleSides[0] = Vol.Density;
            Dst.DensityCovScaleSides[1] = Vol.Coverage;
            Dst.DensityCovScaleSides[2] = 1.0f / std::max(1.5f, Vol.FeatureScaleMeters);
            Dst.DensityCovScaleSides[3] = static_cast<float>(std::clamp(Vol.PrismSides, 3u, 16u));
            Dst.WindVelCurl[0]          = FW.VelocityMetersPerSec.X;
            Dst.WindVelCurl[1]          = FW.VelocityMetersPerSec.Y;
            Dst.WindVelCurl[2]          = FW.VelocityMetersPerSec.Z;
            Dst.WindVelCurl[3]          = FW.CurlScalar;
            Dst.TintAniso[0]            = Vol.FogTint.R;
            Dst.TintAniso[1]            = Vol.FogTint.G;
            Dst.TintAniso[2]            = Vol.FogTint.B;
            Dst.TintAniso[3]            = Vol.AnisotropyG;
        }
        F.FogMeta[0] = static_cast<float>(LocalFogCount);
        F.FogMeta[1] = Fg.AerialFog.ScaleHeightMeters;
        F.FogMeta[2] = (LocalFogCount > 0u) ? Fg.LocalFogs[0].EdgeSoftness : 0.35f;
        F.FogMeta[3] = 0.0f;

        F.GodRayParams0[0] = Fg.GodRays.Enabled ? 1.0f : 0.0f;
        F.GodRayParams0[1] = Fg.GodRays.Intensity;
        F.GodRayParams0[2] = Fg.GodRays.AnisotropyG;
        F.GodRayParams0[3] = Fg.GodRays.Density;
        F.GodRayParams1[0] = Fg.GodRays.MaxDistanceMeters;
        F.GodRayParams1[1] = static_cast<float>(std::clamp(Fg.GodRays.SampleSteps, 8u, 48u));
        F.GodRayParams1[2] = Fg.GodRays.InteractWithClouds ? 1.0f : 0.0f;
        F.GodRayParams1[3] = Fg.GodRays.InteractWithFog ? 1.0f : 0.0f;
        F.GodRayTint[0]    = Fg.GodRays.ShaftTint.R;
        F.GodRayTint[1]    = Fg.GodRays.ShaftTint.G;
        F.GodRayTint[2]    = Fg.GodRays.ShaftTint.B;
        F.GodRayTint[3]    = Fg.GodRays.Decay;

        auto& Cel = Uniforms.CelestialParams;
        Cel.SunAxisRadius[0]    = SunDir.X;
        Cel.SunAxisRadius[1]    = SunDir.Y;
        Cel.SunAxisRadius[2]    = SunDir.Z;
        Cel.SunAxisRadius[3]    = Atmo.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat;
        Cel.SunTintIntensity[0] = SolarSolved.EffectiveSolarTint.R;
        Cel.SunTintIntensity[1] = SolarSolved.EffectiveSolarTint.G;
        Cel.SunTintIntensity[2] = SolarSolved.EffectiveSolarTint.B;
        Cel.SunTintIntensity[3] = (Atmo.Solar.SolarEnabled && Atmo.Solar.SolarIntensityMultiplier > 0.0f)
            ? (Atmo.Solar.SolarIntensityMultiplier * Atmo.Medium.SkyBrightnessMultiplier * 2.1f)
            : 0.0f;
        Cel.StarParams[0]            = Atmo.Stars.StarfieldEnabled ? 1.0f : 0.0f;
        Cel.StarParams[1]            = Atmo.Stars.LimitingMagnitude;
        Cel.StarParams[2]            = Atmo.Stars.BrightnessMultiplier;
        Cel.StarParams[3]            = Atmo.Stars.PointSizeScale;
        Cel.StarScintillationRot[0]  = Atmo.Stars.ScintillationEnabled ? 1.0f : 0.0f;
        Cel.StarScintillationRot[1]  = Atmo.Stars.ScintillationDepthPercent;
        Cel.StarScintillationRot[2]  = Atmo.Stars.ScintillationRateHertz;
        Cel.StarScintillationRot[3]  = Atmo.Stars.CelestialRotationDegrees * Constants::DegToRadFloat;
        Cel.ClockViewportDims[0]     = Atmo.SimulationSeconds;
        Cel.ClockViewportDims[1]     = static_cast<float>(WidthPx);
        Cel.ClockViewportDims[2]     = static_cast<float>(HeightPx);
        Cel.ClockViewportDims[3]     = static_cast<float>(LunarStarfieldSequence::MaxMoonSlots);

        const uint32_t ZodiacalQuant = Atmo.Stars.ZodiacalLightEnabled
            ? static_cast<uint32_t>(std::clamp(std::lround(std::max(0.0f, Atmo.Stars.ZodiacalRadianceScale) * 255.0f), 0l, 4095l))
            : 0u;
        const uint32_t FallbackBrightCnt = Atmo.Stars.EnableBrightStarCatalog
            ? std::min<uint32_t>(240u, UploadedCatalogStarCount)
            : 0u;
        const uint32_t AtmoFlags =
            (Atmo.Medium.AtmosphereEnabled ? 1u : 0u) |
            ((Atmo.Medium.OrbitalViewEnabled || (Atmo.Medium.ObserverAltitudeMeters > Atmo.Medium.AtmosphereThicknessMeters)) ? 2u : 0u) |
            (Atmo.Stars.EnableGalacticBand ? 4u : 0u) |
            (Atmo.Stars.ZodiacalLightEnabled ? 8u : 0u) |
            (ZodiacalQuant << 4u) |
            ((FallbackBrightCnt & 0xFFu) << 16u);
        Cel.AtmoMediumPack[0] = Atmo.Medium.RayleighStrength;
        Cel.AtmoMediumPack[1] = Atmo.Medium.MieStrength;
        Cel.AtmoMediumPack[2] = Atmo.Medium.OzoneStrength;
        Cel.AtmoMediumPack[3] = static_cast<float>(AtmoFlags);

        Cel.SolarOpticsParams[0] = (Atmo.Solar.SolarEnabled && Atmo.Solar.AppearanceMode == SolarAppearanceMode::Radiant)
            ? std::max(0.0f, Atmo.Solar.SunGlareStrength)
            : 0.0f;
        Cel.SolarOpticsParams[1] = (2.0f * Basis.TanHalfFovY) / static_cast<float>(std::max(1u, HeightPx));
        Cel.SolarOpticsParams[2] = Atmo.Medium.ObserverAltitudeMeters;
        Cel.SolarOpticsParams[3] = Atmo.Medium.PlanetRadiusMeters;

        for (uint32_t MIdx = 0u; MIdx < LunarStarfieldSequence::MaxMoonSlots; ++MIdx)
        {
            const auto& SrcM = Atmo.Moons[MIdx];
            auto&       DstM = Cel.Moons[MIdx];
            const auto  MDir = LunarStarfieldSequence::ResolveMoonWorldDirection(SrcM, SunDir);
            const auto  MTint = LunarStarfieldSequence::QueryPresetTint(SrcM.SurfacePreset);
            DstM.DirectionRadiusRad[0] = MDir.X;
            DstM.DirectionRadiusRad[1] = MDir.Y;
            DstM.DirectionRadiusRad[2] = MDir.Z;
            DstM.DirectionRadiusRad[3] = SrcM.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat;
            DstM.TintSynodicPhase[0]   = MTint.R;
            DstM.TintSynodicPhase[1]   = MTint.G;
            DstM.TintSynodicPhase[2]   = MTint.B;
            DstM.TintSynodicPhase[3]   = SrcM.SynodicPhaseFraction;
            DstM.OpticsOrientation[0]  = SrcM.BrightnessMultiplier;
            DstM.OpticsOrientation[1]  = SrcM.HaloGlowMultiplier;
            DstM.OpticsOrientation[2]  = SrcM.SurfaceRollDegrees * Constants::DegToRadFloat;
            DstM.OpticsOrientation[3]  = SrcM.SurfacePitchDegrees * Constants::DegToRadFloat;
            DstM.MetaMask[0]           = SrcM.Visible ? 1.0f : 0.0f;
            DstM.MetaMask[1]           = SrcM.HasAtmosphere ? SrcM.AtmosphereIntensity : 0.0f;
            DstM.MetaMask[2]           = static_cast<float>(static_cast<uint32_t>(SrcM.SurfacePreset));
            DstM.MetaMask[3]           = SrcM.HasAtmosphere ? SrcM.AtmosphereThicknessScale : 0.0f;
        }

        const auto& LF = Atmo.LensFlare;
        auto&       LFU = Uniforms.LensFlareParams;
        if (LF.Enabled && Config.DebugChannel == 0u)
        {
            const ProjectedLensFlareSlice FlareProj = LensFlareSequence::ResolveSolarFlareProjection(
                Basis.CameraOrigin,
                Basis.ForwardAxis,
                Basis.RightAxis,
                Basis.UpAxis,
                Basis.TanHalfFovX,
                Basis.TanHalfFovY,
                Atmo,
                &Scene.SceneTree);
            LFU.SourceScreenGain[0]  = FlareProj.ScreenU;
            LFU.SourceScreenGain[1]  = FlareProj.ScreenV;
            LFU.SourceScreenGain[2]  = FlareProj.EffectiveGain * std::clamp(LF.FlareIntensity, 0.0f, 3.0f);
            LFU.SourceScreenGain[3]  = std::clamp(LF.FlareSpread, 0.2f, 2.2f);
            LFU.OpticsStyleHalo[0]   = static_cast<float>(static_cast<uint32_t>(LF.Style));
            LFU.OpticsStyleHalo[1]   = std::clamp(LF.HaloIntensity, 0.0f, 2.5f);
            LFU.OpticsStyleHalo[2]   = std::clamp(LF.GhostIntensity, 0.0f, 2.5f);
            LFU.OpticsStyleHalo[3]   = std::clamp(LF.GhostSpacing, 0.2f, 2.0f);
            LFU.GhostRotationTint[0] = static_cast<float>(std::min(16u, LF.GhostCount));
            LFU.GhostRotationTint[1] = LF.StreakRotationDegrees * Constants::DegToRadFloat;
            LFU.GhostRotationTint[2] = static_cast<float>(std::min(16u, LF.GhostSides));
            LFU.GhostRotationTint[3] = (FlareProj.EffectiveGain > 1.0e-4f) ? 1.0f : 0.0f;
            LFU.SourceTintAspect[0]  = FlareProj.SourceTint.R;
            LFU.SourceTintAspect[1]  = FlareProj.SourceTint.G;
            LFU.SourceTintAspect[2]  = FlareProj.SourceTint.B;
            LFU.SourceTintAspect[3]  = static_cast<float>(WidthPx) / static_cast<float>(std::max(1u, HeightPx));
        }
    }

    constexpr uint64_t UniformStrideBytes = 8192ull;

    // Refresh the 2-bounce 3D Radiosity Pyramid (~0.2 ms, zero triangle traversal) if dynamic luminaire/solar radiance shifted
    {
        auto HashMixBytes = [](uint64_t SeedVal, const void* BytesPtr, std::size_t ByteLen) -> uint64_t
        {
            const auto* Raw = static_cast<const uint8_t*>(BytesPtr);
            for (std::size_t I = 0u; I < ByteLen; ++I)
            {
                SeedVal ^= static_cast<uint64_t>(Raw[I]);
                SeedVal *= 1099511628211ull;
            }
            return SeedVal;
        };
        uint64_t LightingSig = 14695981039346656037ull;
        LightingSig = HashMixBytes(LightingSig, Uniforms.SolarIrradiancePad, sizeof(Uniforms.SolarIrradiancePad));
        LightingSig = HashMixBytes(LightingSig, Uniforms.SkyRadianceBvhCount, sizeof(float) * 3u);
        LightingSig = HashMixBytes(LightingSig, Uniforms.AreaLights, sizeof(Uniforms.AreaLights));
        LightingSig = HashMixBytes(LightingSig, &LightCount, sizeof(LightCount));

        if (Config.Engine != WorldRenderEngine::MatCap &&
            GlobalDistanceFieldReady &&
            UploadedBvhCount > 0u &&
            LastRadiosityLightingSignature != 0ull &&
            LightingSig != LastRadiosityLightingSignature)
        {
            const auto RadSyncStart = std::chrono::steady_clock::now();
            (void)DispatchGpuScenePrecompute(Scene, false, kPrecomputeRadiosity | kPrecomputeRadiosityFull | kPrecomputeSecondBounce);
            const auto RadSyncEnd = std::chrono::steady_clock::now();
            LastViewportTiming.SceneSyncGpuMs += static_cast<float>(
                std::chrono::duration<double, std::milli>(RadSyncEnd - RadSyncStart).count());
        }
        LastRadiosityLightingSignature = LightingSig;
    }

    const bool     SyncReadbackMode = CopyToReadbackStaging || (OutDisplayRgba8 != nullptr);
    const uint32_t SlotIdx          = SyncReadbackMode ? 0u : LiveFrameCursor;
    if (!SyncReadbackMode)
    {
        LiveFrameCursor = (LiveFrameCursor + 1u) % kLiveFrameSlots;
    }
    RetireLiveFrameSlot(SlotIdx, true);
    AsyncLiveFrameSlot& ActiveSlot = LiveFrameSlots[SlotIdx];
    EnsureBufferCapacity(ActiveSlot.UniformBuffer, UniformStrideBytes * 12ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    if (ActiveSlot.UniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    std::memcpy(&CachedFineToroidalMinCell[3], &ActiveAnalyticGroundPlaneZ, sizeof(float));
    std::memcpy(&CachedCoarseToroidalMinCell[3], &ActiveAnalyticGroundMaterialSlot, sizeof(uint32_t));
    std::memcpy(Uniforms.GlobalDistanceFineCenterSpan, CachedFineGdfCenterSpan, sizeof(CachedFineGdfCenterSpan));
    std::memcpy(Uniforms.GlobalDistanceCoarseCenterSpan, CachedCoarseGdfCenterSpan, sizeof(CachedCoarseGdfCenterSpan));
    std::memcpy(Uniforms.GlobalDistanceFineToroidalCell, CachedFineToroidalMinCell, sizeof(CachedFineToroidalMinCell));
    std::memcpy(Uniforms.GlobalDistanceCoarseToroidalCell, CachedCoarseToroidalMinCell, sizeof(CachedCoarseToroidalMinCell));
    Uniforms.GlobalDistanceSlabBounds[3] = 0u;
    Uniforms.GlobalDistanceBakeControl[0] = 0u;
    Uniforms.GlobalDistanceBakeControl[1] = 0u;
    Uniforms.GlobalDistanceBakeControl[2] = GlobalDistanceFieldReady ? 1u : 0u;
    // w: bit0 = XeGTAO enabled, bits 8..15 = XeGTAO strength x255/2 (0..2 range), bits 16..31 = LocalSdfInstanceCount
    Uniforms.GlobalDistanceBakeControl[3] =
        (Config.XeGtaoEnabled ? 1u : 0u) |
        (static_cast<uint32_t>(std::clamp(Config.XeGtaoStrength, 0.0f, 2.0f) * 127.5f) << 8u) |
        ((UploadedLocalSdfInstanceCount & 0xFFFFu) << 16u);

    Uniforms.SelectionOutlineSpan[0]       = 0u;
    Uniforms.SelectionOutlineSpan[1]       = 0u;
    Uniforms.SelectionOutlineSpan[2]       = 0u;
    Uniforms.SelectionOutlineSpan[3]       = 0u;
    Uniforms.SelectionOutlineColorWidth[0] = std::pow(std::clamp(Config.SelectionOutlineColor.X, 0.0f, 1.0f), 2.2f);
    Uniforms.SelectionOutlineColorWidth[1] = std::pow(std::clamp(Config.SelectionOutlineColor.Y, 0.0f, 1.0f), 2.2f);
    Uniforms.SelectionOutlineColorWidth[2] = std::pow(std::clamp(Config.SelectionOutlineColor.Z, 0.0f, 1.0f), 2.2f);
    Uniforms.SelectionOutlineColorWidth[3] = std::clamp(Config.SelectionOutlineWidthPx, 1.0f, 3.0f);

    // Exposure & Post-Process grading parameters (neutral defaults {1, 1, 1, 0} when inactive)
    Uniforms.PostProcessParams[0] = std::clamp(Config.ExposureGain, 0.03125f, 16.0f);
    Uniforms.PostProcessParams[1] = std::clamp(Config.PostProcessSaturation, 0.0f, 2.0f);
    Uniforms.PostProcessParams[2] = std::clamp(Config.PostProcessContrast, 0.25f, 2.5f);
    Uniforms.PostProcessParams[3] = std::clamp(Config.PostProcessVignette, 0.0f, 1.0f);

    // Motion Blur post-pass parameters (0.000 ms overhead when disabled)
    const bool MotionBlurActive =
        Config.MotionBlurEnabled &&
        Config.DebugChannel == 0u &&
        MotionBlurProgramHandle != UINT32_MAX &&
        Config.MotionBlurIntensity > 1.0e-4f &&
        Config.MotionBlurShutterAngleDeg > 0.5f;
    const float ShutterNorm = std::clamp(Config.MotionBlurShutterAngleDeg / 360.0f, 0.01f, 1.0f);
    Uniforms.MotionBlurParams0[0] = MotionBlurActive ? 1.0f : 0.0f;
    Uniforms.MotionBlurParams0[1] = ShutterNorm * std::clamp(Config.MotionBlurIntensity, 0.0f, 3.0f);
    Uniforms.MotionBlurParams0[2] = static_cast<float>(std::clamp(Config.MotionBlurSampleCount, 4u, 16u));
    Uniforms.MotionBlurParams0[3] = std::clamp(Config.MotionBlurMaxRadiusPx, 1.0f, 48.0f);
    Uniforms.MotionBlurParams1[0] = Config.MotionBlurVelocityUvOverrideX;
    Uniforms.MotionBlurParams1[1] = Config.MotionBlurVelocityUvOverrideY;
    Uniforms.MotionBlurParams1[2] = static_cast<float>(std::min(2u, Config.MotionBlurStyleIndex));
    Uniforms.MotionBlurParams1[3] = Config.MotionBlurDirectionalAngleRad;
    Uniforms.PrevCameraOriginTanX[0]   = PrevMotionBlurCameraBasis.CameraOrigin.X;
    Uniforms.PrevCameraOriginTanX[1]   = PrevMotionBlurCameraBasis.CameraOrigin.Y;
    Uniforms.PrevCameraOriginTanX[2]   = PrevMotionBlurCameraBasis.CameraOrigin.Z;
    Uniforms.PrevCameraOriginTanX[3]   = PrevMotionBlurCameraBasis.TanHalfFovX;
    Uniforms.PrevCameraForwardValid[0] = PrevMotionBlurCameraBasis.ForwardAxis.X;
    Uniforms.PrevCameraForwardValid[1] = PrevMotionBlurCameraBasis.ForwardAxis.Y;
    Uniforms.PrevCameraForwardValid[2] = PrevMotionBlurCameraBasis.ForwardAxis.Z;
    Uniforms.PrevCameraForwardValid[3] = HasPrevMotionBlurCameraBasis ? 1.0f : 0.0f;
    Uniforms.PrevCameraRightTanY[0]    = PrevMotionBlurCameraBasis.RightAxis.X;
    Uniforms.PrevCameraRightTanY[1]    = PrevMotionBlurCameraBasis.RightAxis.Y;
    Uniforms.PrevCameraRightTanY[2]    = PrevMotionBlurCameraBasis.RightAxis.Z;
    Uniforms.PrevCameraRightTanY[3]    = PrevMotionBlurCameraBasis.TanHalfFovY;
    Uniforms.PrevCameraUpPad[0]        = PrevMotionBlurCameraBasis.UpAxis.X;
    Uniforms.PrevCameraUpPad[1]        = PrevMotionBlurCameraBasis.UpAxis.Y;
    Uniforms.PrevCameraUpPad[2]        = PrevMotionBlurCameraBasis.UpAxis.Z;
    Uniforms.PrevCameraUpPad[3]        = 0.0f;
    if (Config.SelectedClusterInstanceIndex != UINT32_MAX)
    {
        for (const ResidentClusterInstanceSpan& Span : ResidentClusterInstances)
        {
            if (Span.InstanceIndex == Config.SelectedClusterInstanceIndex && Span.ClusterCount > 0u)
            {
                Uniforms.SelectionOutlineSpan[0] = Span.FirstGlobalClusterOrdinal;
                Uniforms.SelectionOutlineSpan[1] = Span.ClusterCount;
                break;
            }
        }
    }

    const uint32_t EngIdxPre             = static_cast<uint32_t>(Config.Engine);
    const uint32_t DirectPbrOrdinalPre   = static_cast<uint32_t>(WorldRenderEngine::OpenPbrDirect);
    const uint32_t LowEndGtxOrdinalPre   = static_cast<uint32_t>(WorldRenderEngine::RealTimeGiGtxLowEnd);
    const bool     PathTracePre          = (EngIdxPre == 4u || EngIdxPre == 5u) && (Config.DebugChannel == 0u);
    const bool     UseHardwarePrimaryVis =
        !PathTracePre &&
        (EngIdxPre != 3u) &&
        (PrimaryVisPipeline != VK_NULL_HANDLE) &&
        (UploadedTriangleCount > 0u) &&
        EnsurePrimaryVisTargets(WidthPx, HeightPx);
    const bool     UseHalfResIndirect    =
        !PathTracePre &&
        (HalfResIndirectProgramHandle != UINT32_MAX) &&
        (EngIdxPre == DirectPbrOrdinalPre || EngIdxPre == LowEndGtxOrdinalPre) &&
        (Config.DebugChannel == DebugChannelShadedLit ||
         (Config.DebugChannel >= DebugChannelSunShadow && Config.DebugChannel <= DebugChannelSpecular)) &&
        (Config.DebugChannel != DebugChannelFilteredAo);

    if (UseHardwarePrimaryVis)
    {
        Uniforms.AtmosphereMaskPad[3] |= (1u << 24u);
    }
    if (UseHalfResIndirect)
    {
        Uniforms.AtmosphereMaskPad[3] |= (1u << 25u);
    }

    uint8_t* UniformBytes = reinterpret_cast<uint8_t*>(ActiveSlot.UniformBuffer.HostAddress);
    std::memcpy(UniformBytes, &Uniforms, sizeof(Uniforms));

    const auto UniformDoneClock = std::chrono::steady_clock::now();
    const float CpuUniformMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(UniformDoneClock - StartClock).count());

    DispatchEncoder.SelectDescriptorRingSlot(SlotIdx);
    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const VkQueryPool ActiveQueryPool = ActiveSlot.TimestampQueryPool;
    const bool UseGpuTimers = (ActiveQueryPool != VK_NULL_HANDLE && BoundDevice->QueryTimestampValidBits() > 0u);
    auto WriteStamp = [&](uint32_t Index, VkPipelineStageFlagBits Stage)
    {
        if (UseGpuTimers)
        {
            vkCmdWriteTimestamp(Cmd, Stage, ActiveQueryPool, Index);
        }
    };
    if (UseGpuTimers)
    {
        vkCmdResetQueryPool(Cmd, ActiveQueryPool, 0u, 10u);
    }

    WriteStamp(0u, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    if (AtmoActive)
    {
        DispatchGpuSkyViewLut(Cmd, Scene);
    }
    if (AtmoActive || CelestialOrCloudsActive)
    {
        const bool IsOrbitalSky = Scene.Atmosphere.Medium.OrbitalViewEnabled ||
                                  (Scene.Atmosphere.Medium.ObserverAltitudeMeters > Scene.Atmosphere.Medium.AtmosphereThicknessMeters);
        const bool StarsVisible = Scene.Atmosphere.Stars.StarfieldEnabled &&
                                  (!Scene.Atmosphere.Medium.AtmosphereEnabled || IsOrbitalSky || SolarSolved.ElevationDegrees < 5.0f);
        if (StarsVisible)
        {
            DispatchGpuStellarTileCull(Cmd, Camera, Scene, WidthPx, HeightPx);
        }
    }
    WriteStamp(1u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    EnsureBufferCapacity(ActiveClusterMaskBuffer, MaskSliceOffsetBytes * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto ConstructStepSlots = [&](uint64_t UniOffset, bool SwapPingPong) -> std::array<ComputeSlotDescriptor, 39u>
    {
        const auto& InShadow  = SwapPingPong ? PongShadowAoBuffer : PingShadowAoBuffer;
        const auto& OutShadow = SwapPingPong ? PingShadowAoBuffer : PongShadowAoBuffer;
        const auto& InIndir   = SwapPingPong ? PongIndirectIrradianceBuffer : PingIndirectIrradianceBuffer;
        const auto& OutIndir  = SwapPingPong ? PingIndirectIrradianceBuffer : PongIndirectIrradianceBuffer;
        return {{
            {  0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ActiveSlot.UniformBuffer.BufferHandle,        sizeof(WorldViewportUniformBlockStd140), UniOffset },
            {  1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,                  VK_WHOLE_SIZE },
            {  2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle,            VK_WHOLE_SIZE },
            {  3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialEntryBuffer.BufferHandle,             VK_WHOLE_SIZE },
            {  4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputPixelBuffer.BufferHandle,               OutBytes      },
            {  5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SolarShadowDepthBuffer.BufferHandle,          VK_WHOLE_SIZE },
            {  6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AccumulationHdrBuffer.BufferHandle,           AccBytes      },
            {  7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AtmosphericSkyViewLutBuffer.BufferHandle,     VK_WHOLE_SIZE },
            {  8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LunarSurfaceAtlasBuffer.BufferHandle,         VK_WHOLE_SIZE },
            {  9u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BakedLensFlareAtlasBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufPosMaskBuffer.BufferHandle,               AccBytes      },
            { 11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufNormAoBuffer.BufferHandle,                AccBytes      },
            { 12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufDirectBaseBuffer.BufferHandle,            AccBytes      },
            { 13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufSunContribBuffer.BufferHandle,            AccBytes      },
            { 14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufAlbedoBuffer.BufferHandle,                AccBytes      },
            { 15u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InShadow.BufferHandle,                        AccBytes      },
            { 16u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutShadow.BufferHandle,                       AccBytes      },
            { 17u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CloudRadiometricBuffer.BufferHandle,          CloudBytes    },
            { 18u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ProjectedCatalogStarBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 19u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StellarTileBinsBuffer.BufferHandle,           VK_WHOLE_SIZE },
            { 20u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PlanetaryCloudNoiseBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 21u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RawCatalogStarBuffer.BufferHandle,            VK_WHOLE_SIZE },
            { 22u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PhotometricProfileTableBuffer.BufferHandle,   VK_WHOLE_SIZE },
            { 23u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PhotometricLampEntryBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 24u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GlobalDistanceFieldBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 25u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GlobalRadiosityFieldBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 26u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InIndir.BufferHandle,                         AccBytes      },
            { 27u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutIndir.BufferHandle,                        AccBytes      },
            { 28u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GlobalSurfaceShellBuffer.BufferHandle,        VK_WHOLE_SIZE },
            { 29u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LocalAssetSdfVoxelBuffer.BufferHandle,        VK_WHOLE_SIZE },
            { 30u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DynamicLocalSdfInstanceBuffer.BufferHandle,   VK_WHOLE_SIZE },
            { 31u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ActiveClusterMaskBuffer.BufferHandle,         MaskSliceOffsetBytes, MaskSliceOffsetBytes },
            { 32u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, GdfFineImage3D.ViewHandle,         LinearRepeatSampler3D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 33u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, GdfCoarseImage3D.ViewHandle,       LinearRepeatSampler3D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 34u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, RadiosityMip0Image3D.ViewHandle,   LinearRepeatSampler3D, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 35u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, RadiosityCoarseImage3D.ViewHandle, LinearClampSampler3D,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 36u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, DummyFloat1x1x1Image3D.ViewHandle, VK_NULL_HANDLE,        VK_IMAGE_LAYOUT_GENERAL },
            { 37u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, DummyRgba1x1x1Image3D.ViewHandle,  VK_NULL_HANDLE,        VK_IMAGE_LAYOUT_GENERAL },
            { 38u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, PrimaryVisImage2D.ViewHandle,      VK_NULL_HANDLE,        VK_IMAGE_LAYOUT_GENERAL },
        }};
    };

    auto InsertComputeSyncBarrier = [&]()
    {
        VkMemoryBarrier Barrier {};
        Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &Barrier,
            0u,
            nullptr,
            0u,
            nullptr);
    };

    const uint32_t CloudGroupsX = (CloudW   + 7u) / 8u;
    const uint32_t CloudGroupsY = (CloudH   + 7u) / 8u;
    const uint32_t GroupsX      = (WidthPx  + 7u) / 8u;
    const uint32_t GroupsY      = (HeightPx + 7u) / 8u;
    const uint32_t EngIdx       = static_cast<uint32_t>(Config.Engine);
    const auto     InitialSlots = ConstructStepSlots(0ull, false);

    WriteStamp(2u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (AtmoActive && Scene.Atmosphere.Clouds.CloudsEnabled && Config.DebugChannel == 0u)
    {
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, CloudLayerProgramHandle, InitialSlots, CloudGroupsX, CloudGroupsY, 1u))
        {
            BoundDevice->FreeCommandBuffer(Cmd);
            return false;
        }
        InsertComputeSyncBarrier();
    }
    WriteStamp(3u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    auto MakeLazyViewportSlots = []() -> std::array<VkDescriptorType, 39u>
    {
        std::array<VkDescriptorType, 39u> LazySlots {};
        LazySlots.fill(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        LazySlots[0]  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        LazySlots[32] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        LazySlots[33] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        LazySlots[34] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        LazySlots[35] = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        LazySlots[36] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        LazySlots[37] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        LazySlots[38] = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        return LazySlots;
    };
    if ((EngIdx == 4u || EngIdx == 5u) && Config.DebugChannel == 0u && ViewportPathTraceProgramHandle == UINT32_MAX)
    {
        const auto LazyPtSlots = MakeLazyViewportSlots();
        ViewportPathTraceProgramHandle = DispatchEncoder.RegisterProgram(
            CachedCompiledShaderDir / "WorldViewportPathTraceCompute.comp.spv",
            LazyPtSlots,
            "ViewportPathTraceEntry");
        DispatchEncoder.PersistProgramCache();
    }
    if (EngIdx == 3u && HighEndBvhProgramHandle == UINT32_MAX)
    {
        const auto LazyHighEndSlots = MakeLazyViewportSlots();
        HighEndBvhProgramHandle = DispatchEncoder.RegisterProgram(
            CachedCompiledShaderDir / "WorldViewportHighEndCompute.comp.spv",
            LazyHighEndSlots,
            "HighEndBvhComputeEntry");
        DispatchEncoder.PersistProgramCache();
    }

    const bool UseDedicatedPathTracer =
        (EngIdx == 4u || EngIdx == 5u) &&
        (Config.DebugChannel == 0u) &&
        (ViewportPathTraceProgramHandle != UINT32_MAX);

    bool MotionBlurSourceSwapPingPong = false;

    if (UseDedicatedPathTracer)
    {
        WriteStamp(4u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        // Accumulate the whole per-frame batch: each sample re-dispatches with its own sample
        // index in a spare uniform slot (running-mean accumulation in the shader does the rest).
        for (uint32_t BatchIdx = 0u; BatchIdx < ProgressiveBatchSpp; ++BatchIdx)
        {
            auto SampleSlots = InitialSlots;
            if (BatchIdx > 0u)
            {
                const uint64_t SampleUniOffs = static_cast<uint64_t>(BatchIdx) * UniformStrideBytes;
                WorldViewportUniformBlockStd140 SampleUni = Uniforms;
                SampleUni.EngineShadowReflPad[3] = EffectiveTickOrSample + BatchIdx;
                std::memcpy(UniformBytes + SampleUniOffs, &SampleUni, sizeof(SampleUni));
                SampleSlots = ConstructStepSlots(SampleUniOffs, false);
            }
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ViewportPathTraceProgramHandle, SampleSlots, GroupsX, GroupsY, 1u))
            {
                BoundDevice->FreeCommandBuffer(Cmd);
                return false;
            }
            if (BatchIdx + 1u < ProgressiveBatchSpp)
            {
                InsertComputeSyncBarrier();
            }
        }
        WriteStamp(5u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        WriteStamp(6u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        WriteStamp(7u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    else
    {
        WriteStamp(4u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (UseHardwarePrimaryVis)
        {
            const float PrimaryFarZ = std::max(120.0f, Uniforms.SolarIrradiancePad[3]);
            const float CamUpNearZ[4]     { Uniforms.CameraUpWidth[0],       Uniforms.CameraUpWidth[1],       Uniforms.CameraUpWidth[2],       0.08f       };
            const float CamForwardFarZ[4] { Uniforms.CameraForwardHeight[0], Uniforms.CameraForwardHeight[1], Uniforms.CameraForwardHeight[2], PrimaryFarZ };
            RecordPrimaryVisRasterPass(
                Cmd,
                SlotIdx,
                WidthPx,
                HeightPx,
                Uniforms.CameraOriginTanX,
                Uniforms.CameraRightTanY,
                CamUpNearZ,
                CamForwardFarZ);
        }

        const uint32_t PrimaryProgramHandle =
            (EngIdx == 3u && HighEndBvhProgramHandle != UINT32_MAX)
                ? HighEndBvhProgramHandle
                : RadiometricProgramHandle;
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, PrimaryProgramHandle, InitialSlots, GroupsX, GroupsY, 1u))
        {
            BoundDevice->FreeCommandBuffer(Cmd);
            return false;
        }
        InsertComputeSyncBarrier();

        constexpr uint32_t EnableHalfResIndirectBit = 8u;
        const uint32_t HalfW       = (WidthPx  + 1u) >> 1u;
        const uint32_t HalfH       = (HeightPx + 1u) >> 1u;
        const uint32_t HalfGroupsX = (HalfW + 7u) / 8u;
        const uint32_t HalfGroupsY = (HalfH + 7u) / 8u;
        auto DispatchHalfResIndirect = [&](bool SwapPingPongForFinalStep) -> bool
        {
            const auto HalfSlots = ConstructStepSlots(0ull, SwapPingPongForFinalStep);
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, HalfResIndirectProgramHandle, HalfSlots, HalfGroupsX, HalfGroupsY, 1u))
            {
                return false;
            }
            InsertComputeSyncBarrier();
            return true;
        };

        const uint32_t DirectPbrOrdinal  = static_cast<uint32_t>(WorldRenderEngine::OpenPbrDirect);
        const uint32_t LowEndGtxOrdinal  = static_cast<uint32_t>(WorldRenderEngine::RealTimeGiGtxLowEnd);
        const uint32_t HighEndBvhOrdinal = static_cast<uint32_t>(WorldRenderEngine::RealTimeGiHighEnd);
        const bool ShadingDebug =
            (Config.DebugChannel == DebugChannelShadedLit) ||
            (Config.DebugChannel >= DebugChannelSunShadow && Config.DebugChannel <= DebugChannelSpecular) ||
            (Config.DebugChannel == DebugChannelRawUdfAo);
        const bool MultiStepBilateralDenoise =
            ShadingDebug &&
            (Config.DebugChannel != DebugChannelRawUdfAo) &&
            ((EngIdx >= DirectPbrOrdinal && EngIdx <= HighEndBvhOrdinal) ||
             Config.DebugChannel == DebugChannelFilteredAo);
        const bool MultiStepSunFilter =
            (EngIdx == HighEndBvhOrdinal) ||
            ((EngIdx == DirectPbrOrdinal || EngIdx == LowEndGtxOrdinal) &&
             PenumbraOrdinal >= PenumbraMultiStepThreshold);
        const bool FirstStepSunFilter =
            MultiStepSunFilter ||
            (EngIdx == DirectPbrOrdinal || EngIdx == LowEndGtxOrdinal);

        // Collapse 3-step sequential Denoise -> 1 fused pass on DirectPBR / GTX GDF GI when multi-step sun denoise isn't required.
        const bool UseSingleFusedDenoisePass =
            MultiStepBilateralDenoise &&
            !MultiStepSunFilter &&
            (Config.DebugChannel != DebugChannelFilteredAo) &&
            (EngIdx == DirectPbrOrdinal || EngIdx == LowEndGtxOrdinal);

        if (UseHalfResIndirect && (UseSingleFusedDenoisePass || !MultiStepBilateralDenoise))
        {
            if (!DispatchHalfResIndirect(false))
            {
                BoundDevice->FreeCommandBuffer(Cmd);
                return false;
            }
        }
        WriteStamp(5u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        WriteStamp(6u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (UseSingleFusedDenoisePass)
        {
            const uint64_t UniformOffset = UniformStrideBytes;
            WorldViewportUniformBlockStd140 StepUniforms = Uniforms;
            StepUniforms.DenoiseStepParams[0] = 2u;
            StepUniforms.DenoiseStepParams[1] =
                EnableFinalToneStackBit |
                (UseHalfResIndirect ? EnableHalfResIndirectBit : EnableSpatialAoDenoiseBit) |
                (FirstStepSunFilter ? EnableSpatialSunDenoiseBit : 0u);
            std::memcpy(UniformBytes + UniformOffset, &StepUniforms, sizeof(StepUniforms));

            const auto StepSlots = ConstructStepSlots(UniformOffset, false);
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DenoiseProgramHandle, StepSlots, GroupsX, GroupsY, 1u))
            {
                BoundDevice->FreeCommandBuffer(Cmd);
                return false;
            }
        }
        else if (MultiStepBilateralDenoise)
        {
            for (uint32_t StepIndex = 0u; StepIndex < BilateralDenoiseStepCount; ++StepIndex)
            {
                const bool IsFinalStep = (StepIndex + 1u == BilateralDenoiseStepCount);
                const bool SwapPingPong = (StepIndex & 1u) != 0u;
                if (IsFinalStep)
                {
                    MotionBlurSourceSwapPingPong = SwapPingPong;
                }
                if (IsFinalStep && UseHalfResIndirect)
                {
                    if (!DispatchHalfResIndirect(SwapPingPong))
                    {
                        BoundDevice->FreeCommandBuffer(Cmd);
                        return false;
                    }
                }
                const uint64_t UniformOffset = static_cast<uint64_t>(StepIndex + 1u) * UniformStrideBytes;
                WorldViewportUniformBlockStd140 StepUniforms = Uniforms;
                const bool FilterSunThisStep = (StepIndex == 0u) ? FirstStepSunFilter : MultiStepSunFilter;
                StepUniforms.DenoiseStepParams[0] = BilateralDenoiseStrides[StepIndex];
                StepUniforms.DenoiseStepParams[1] =
                    (IsFinalStep ? EnableFinalToneStackBit : 0u) |
                    (UseHalfResIndirect ? (IsFinalStep ? EnableHalfResIndirectBit : 0u) : EnableSpatialAoDenoiseBit) |
                    (FilterSunThisStep ? EnableSpatialSunDenoiseBit : 0u);
                std::memcpy(UniformBytes + UniformOffset, &StepUniforms, sizeof(StepUniforms));

                const auto StepSlots = ConstructStepSlots(UniformOffset, SwapPingPong);
                if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DenoiseProgramHandle, StepSlots, GroupsX, GroupsY, 1u))
                {
                    BoundDevice->FreeCommandBuffer(Cmd);
                    return false;
                }
                if (!IsFinalStep)
                {
                    InsertComputeSyncBarrier();
                }
            }
        }
        else
        {
            const uint64_t UniformOffset = UniformStrideBytes;
            WorldViewportUniformBlockStd140 StepUniforms = Uniforms;
            StepUniforms.DenoiseStepParams[0] = BilateralDenoiseStrides[0u];
            StepUniforms.DenoiseStepParams[1] =
                EnableFinalToneStackBit |
                (UseHalfResIndirect ? EnableHalfResIndirectBit : 0u);
            std::memcpy(UniformBytes + UniformOffset, &StepUniforms, sizeof(StepUniforms));

            const auto StepSlots = ConstructStepSlots(UniformOffset, false);
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DenoiseProgramHandle, StepSlots, GroupsX, GroupsY, 1u))
            {
                BoundDevice->FreeCommandBuffer(Cmd);
                return false;
            }
        }
        WriteStamp(7u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    if (MotionBlurActive)
    {
        VkMemoryBarrier CompositeToBlur {};
        CompositeToBlur.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        CompositeToBlur.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        CompositeToBlur.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &CompositeToBlur,
            0u,
            nullptr,
            0u,
            nullptr);

        const auto BlurSlots = ConstructStepSlots(0ull, MotionBlurSourceSwapPingPong);
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, MotionBlurProgramHandle, BlurSlots, GroupsX, GroupsY, 1u))
        {
            BoundDevice->FreeCommandBuffer(Cmd);
            return false;
        }
    }
    PrevMotionBlurCameraBasis    = Basis;
    HasPrevMotionBlurCameraBasis = true;

    WriteStamp(8u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    {
        VkMemoryBarrier ComputeToTransfer {};
        ComputeToTransfer.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        ComputeToTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ComputeToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u,
            1u,
            &ComputeToTransfer,
            0u,
            nullptr,
            0u,
            nullptr);

        if (CopyToDisplayImage2D && DisplayOutputImage2D.ImageHandle != VK_NULL_HANDLE)
        {
            VkImageMemoryBarrier ImgToDst {};
            ImgToDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ImgToDst.srcAccessMask       = 0u;
            ImgToDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            ImgToDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            ImgToDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ImgToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ImgToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ImgToDst.image               = DisplayOutputImage2D.ImageHandle;
            ImgToDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
            vkCmdPipelineBarrier(
                Cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0u,
                0u,
                nullptr,
                0u,
                nullptr,
                1u,
                &ImgToDst);

            VkBufferImageCopy ImgRegion {};
            ImgRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ImgRegion.imageSubresource.layerCount = 1u;
            ImgRegion.imageExtent                 = { WidthPx, HeightPx, 1u };
            vkCmdCopyBufferToImage(
                Cmd,
                OutputPixelBuffer.BufferHandle,
                DisplayOutputImage2D.ImageHandle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1u,
                &ImgRegion);

            VkImageMemoryBarrier ImgToSampled {};
            ImgToSampled.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ImgToSampled.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            ImgToSampled.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            ImgToSampled.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ImgToSampled.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ImgToSampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ImgToSampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ImgToSampled.image               = DisplayOutputImage2D.ImageHandle;
            ImgToSampled.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
            vkCmdPipelineBarrier(
                Cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,
                0u,
                nullptr,
                0u,
                nullptr,
                1u,
                &ImgToSampled);
        }

        if (CopyToReadbackStaging && OutputPixelBuffer.BufferHandle != OutputReadbackStagingBuffer.BufferHandle)
        {
            VkBufferCopy ReadbackCopy {};
            ReadbackCopy.size = OutBytes;
            vkCmdCopyBuffer(Cmd, OutputPixelBuffer.BufferHandle, OutputReadbackStagingBuffer.BufferHandle, 1u, &ReadbackCopy);

            VkMemoryBarrier TransferToHost {};
            TransferToHost.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            TransferToHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            TransferToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(
                Cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                0u,
                1u,
                &TransferToHost,
                0u,
                nullptr,
                0u,
                nullptr);
        }
    }
    WriteStamp(9u, VK_PIPELINE_STAGE_TRANSFER_BIT);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    ActiveSlot.CommandBuffer    = Cmd;
    ActiveSlot.TimelineSignal   = SyncPoint;
    ActiveSlot.HasGpuTimestamps = UseGpuTimers;

    if (SyncReadbackMode)
    {
        if (!BoundDevice->AwaitTimelineTick(SyncPoint, 60000000000ull))
        {
            std::fprintf(stderr, "[VulkanWorldRenderSequence] AwaitTimelineTick timed out (Engine=%u, %ux%u)\n",
                         static_cast<unsigned>(Config.Engine), WidthPx, HeightPx);
            BoundDevice->FreeCommandBuffer(Cmd);
            ActiveSlot.CommandBuffer    = VK_NULL_HANDLE;
            ActiveSlot.TimelineSignal   = 0ull;
            ActiveSlot.HasGpuTimestamps = false;
            return false;
        }
        RetireLiveFrameSlot(SlotIdx, false);
    }

    const auto CopyStart = std::chrono::steady_clock::now();
    if (CopyToReadbackStaging && OutDisplayRgba8 != nullptr)
    {
        OutDisplayRgba8->resize(static_cast<std::size_t>(OutBytes));
        std::memcpy(OutDisplayRgba8->data(), OutputReadbackStagingBuffer.HostAddress, static_cast<std::size_t>(OutBytes));
    }
    const auto EndClock = std::chrono::steady_clock::now();
    const float CopyMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(EndClock - CopyStart).count());
    const float TotalDispatchMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(EndClock - StartClock).count());

    LastViewportTiming.RenderWidthPx       = WidthPx;
    LastViewportTiming.RenderHeightPx      = HeightPx;
    LastViewportTiming.EngineMode          = EngIdx;
    LastViewportTiming.ShadowAlgorithm     = static_cast<uint32_t>(Config.ShadowAlgorithm);
    LastViewportTiming.QualityTier         = Config.QualityTier;
    LastViewportTiming.HardwareGpuActive   = QueryHardwareGpuActive();
    LastViewportTiming.WorldDispatched     = true;
    LastViewportTiming.AdapterName         = std::string(QueryAdapterTitle());
    LastViewportTiming.GpuHostSubmitWaitMs = SyncReadbackMode
        ? std::max(0.0f, TotalDispatchMs - LastViewportTiming.GpuTotalPassMs - LastViewportTiming.SceneSyncGpuMs) + CpuUniformMs * 0.0f + CopyMs * 0.0f
        : std::max(0.0f, TotalDispatchMs - LastViewportTiming.SceneSyncGpuMs) + CpuUniformMs * 0.0f + CopyMs * 0.0f;
    if (LastViewportTiming.SceneSyncGpuMs <= 0.0f && LastSceneSyncMilliseconds > 0.0f)
    {
        LastViewportTiming.SceneSyncGpuMs = LastSceneSyncMilliseconds;
        LastSceneSyncMilliseconds          = 0.0f;
    }

    LastDispatchMilliseconds = TotalDispatchMs;
    return true;
}

} // namespace Frontier::GeometricRaster
