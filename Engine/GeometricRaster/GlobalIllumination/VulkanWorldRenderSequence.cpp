/*============================================================================================================================================
                                                        VULKANWORLDRENDERSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Implementation of VulkanWorldRenderSequence executing WorldViewportRadiometricCompute.comp.spv on hardware Vulkan GPUs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
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

namespace Frontier::GeometricRaster
{

namespace
{

struct GpuAreaQuadLightStd140
{
    float CenterHalfWidth[4] {};
    float RightHalfHeight[4]  {};
    float UpTwoSided[4]  {};
    float RadiancePacked[4] {};
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

struct GpuCloudWindForceSensorStd140
{
    float PosRadius[4]       {};
    float ForwardStrength[4] {};
    float Params[4]          {};
};

static_assert(sizeof(GpuCloudWindForceSensorStd140) == 48u, "GpuCloudWindForceProbeStd140 must equal 48 bytes.");

struct VolumetricCloudUniformStd140
{
    float                         OriginTimeVector[4]    {};
    float                         SunAxisToggle[4]       {};
    float                         SolarIrradianceExt[4]  {};
    float                         SkyColorWind[4]        {};
    float                         DeckGeometry[4]        {};
    float                         DeckOptics[4]          {};
    float                         WindVectorOcclude[4]   {};
    float                         LocalCenterExt[4]      {};
    float                         LocalExtentFraction[4] {};
    float                         LocalDensityPhase[4]   {};
    float                         FormationOptics[4]     {};
    float                         CloudShadowOptics[4]   {};
    float                         CloudPassControl[4]    {};
    float                         WindForceMeta[4]       {};
    GpuLocalCloudVolumeStd140     LocalClouds[8]         {};
    GpuCloudWindForceSensorStd140  WindProbes[8]          {};
};
static_assert(sizeof(VolumetricCloudUniformStd140) == 1248u, "VolumetricCloudUniformStd140 must equal 1248 bytes.");

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
    float                   HeightParams0[4] {};
    float                   HeightParams1[4] {};
    float                   AerialParams[4]  {};
    float                   FogWindClock[4]  {};
    float                   FogMeta[4]       {};
    float                   GodRayParams0[4] {};
    float                   GodRayParams1[4] {};
    float                   GodRayTint[4]    {};
    GpuLocalFogVolumeStd140    LocalFogs[8]     {};
    GpuCloudWindForceSensorStd140 ForceProbes[8] {};
};

static_assert(sizeof(VolumetricFogUniformStd140) == 1152u, "VolumetricFogUniformStd140 must equal 1152 bytes.");

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
    float SourceScreenGain[4]  {}; // x = ScreenU, y = ScreenV, z = EffectiveGain, w = FlareSpread
    float OpticsStyleHalo[4]   {}; // x = StyleIndex (0..2), y = HaloIntensity, z = GhostIntensity, w = GhostSpacing
    float GhostRotationTint[4] {}; // x = GhostCount, y = RotationRad, z = GhostSides, w = Enabled (0/1)
    float SourceTintAspect[4]  {}; // xyz = SourceLinearTint, w = AspectRatio
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
    float CameraForward[4]   {}; // xyz = UnitForward, w = CelestialRotationRad
    float CameraRightTanX[4] {}; // xyz = UnitRight,   w = TanHalfFovX
    float CameraUpTanY[4]    {}; // xyz = UnitUp,      w = TanHalfFovY
    float ViewportCatalog[4] {}; // x = WidthPx, y = HeightPx, z = CatalogCount, w = LimitingMagnitude
    float SkyFlags[4]        {}; // x = StarsEnabled, y = BrightCatalogEnabled, z = OrbitalView, w = BrightnessMultiplier
};

static_assert(sizeof(StellarCullUniformBlockStd140) == 80u, "StellarCullUniformBlockStd140 must equal 80 bytes.");

struct GpuRawStarSpecStd140
{
    float EqDirMag[4]  {}; // xyz = UnitJ2000Equatorial, w = MagnitudeV
    float ColorSeed[4] {}; // rgb = PlanckianRec709,   w = Seed01
};

static_assert(sizeof(GpuRawStarSpecStd140) == 32u, "GpuRawStarSpecStd140 must equal 32 bytes.");

struct FluidEmissiveLightStd140
{
    float PositionRadius[4]  {};
    float ColorIrradiance[4] {};
};

struct FluidRenderUniformStd140
{
    float                  HeroBoxMinFade[4]   {};
    float                  HeroBoxMaxRes[4]    {};
    float                  WorldCenterSpan[4]  {};
    float                  SunDirRaySteps[4]   {};
    float                  SolarIrradiance[4]  {};
    float                  SkyRadiance[4]      {};
    float                  ShadingCore[4]      {};
    float                  ShadingScatter[4]   {};
    float                  ShadingMode[4]      {};
    float                  CrossSectionCfg[4]  {};
    float                  ShockwaveCenter[4]  {};
    FluidEmissiveLightStd140 EmissiveLights[8] {};
};

static_assert(sizeof(FluidRenderUniformStd140) == 432u, "FluidRenderUniformStd140 must equal 432 bytes.");

struct WorldViewportUniformBlockStd140
{
    float                       CameraOriginTanX[4]         {};
    float                       CameraRightTanY[4]          {};
    float                       CameraUpWidth[4]            {};
    float                       CameraForwardHeight[4]      {};
    float                       SunDirConeTan[4]            {};
    float                       SolarIrradianceTier[4]      {};
    float                       SkyRadianceBvhCount[4]      {};
    float                       SdfMinSpacing[4]            {};
    uint32_t                    SdfDimsLightCount[4]        {};
    float                       LatticeMinGlassCount[4]     {};
    float                       LatticeMaxDebugMode[4]      {};
    uint32_t                    LatticeDimsMaterialCount[4] {};
    uint32_t                    EngineShadowReflSample[4]   {};
    float                       SunCascadeCenterSpan[4]     {};
    float                       SunCascadeRightRes[4]       {};
    float                       SunCascadeUpDepth[4]        {};
    float                       SunCascadeForwardPad[4]     {};
    float                       FineCascadeCenterSpan[4]    {};
    float                       FineCascadeParams[4]        {};
    uint32_t                    DenoiseStepParams[4]        {};
    GpuAreaQuadLightStd140      AreaLights[4]               {};
    GpuGlassSphereStd140        GlassSpheres[4]             {};
    uint32_t                    AtmosphereMaskPad[4]        {};
    VolumetricCloudUniformStd140 CloudParams                {};
    VolumetricFogUniformStd140   FogParams                  {};
    CelestialFieldUniformStd140  CelestialParams            {};
    LensFlareUniformStd140       LensFlareParams            {};
    FluidRenderUniformStd140     FluidParams                {};
    float                        PrevCameraOriginTanX[4]    {};
    float                        PrevCameraRightTanY[4]     {};
    float                        PrevCameraUpWidth[4]       {};
    float                        PrevCameraForwardHeight[4] {};
};

static_assert(sizeof(WorldViewportUniformBlockStd140) == 4176u, "WorldViewportUniformBlockStd140 must equal 4176 bytes.");

struct SignedDistanceGridUniformStd140
{
    float    GridMinSpacing[4]   {};
    uint32_t GridDimsBvhCount[4] {};
    float    CutoffConfig[4]     {};
};

static_assert(sizeof(SignedDistanceGridUniformStd140) == 48u, "SignedDistanceGridUniformStd140 must equal 48 bytes.");

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

    constexpr std::array<VkDescriptorType, 32u> SlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
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
            "[FrontierWorld] WARNING: Failed to load GPU compute shader %s — falling back to CPU GI.\n",
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
            "[FrontierWorld] WARNING: Failed to load GPU cloud shader %s — falling back to CPU GI.\n",
            PathText.c_str());
        TeardownSequence();
        return false;
    }

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
            "[FrontierWorld] WARNING: Failed to load GPU denoise shader %s — falling back to CPU GI.\n",
            PathText.c_str());
        TeardownSequence();
        return false;
    }

    constexpr std::array<VkDescriptorType, 4u> SdfSlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const std::filesystem::path SdfShaderPath = CompiledShaderDirectory / "SignedDistanceGridCompute.comp.spv";
    SignedDistanceProgramHandle = DispatchEncoder.RegisterProgram(
        SdfShaderPath,
        SdfSlotTypes,
        "ComputeEntry");

    constexpr std::array<VkDescriptorType, 2u> SkySlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const std::filesystem::path TransShaderPath = CompiledShaderDirectory / "TransmittanceLutCompute.comp.spv";
    TransmittanceLutProgramHandle = DispatchEncoder.RegisterProgram(
        TransShaderPath,
        SkySlotTypes,
        "TransmittanceLutComputeEntry");
    const std::filesystem::path MultiScatterShaderPath = CompiledShaderDirectory / "MultiScatterLutCompute.comp.spv";
    MultiScatterLutProgramHandle = DispatchEncoder.RegisterProgram(
        MultiScatterShaderPath,
        SkySlotTypes,
        "MultiScatterLutComputeEntry");
    const std::filesystem::path SkyShaderPath = CompiledShaderDirectory / "SkyViewTransmittanceCompute.comp.spv";
    SkyViewLutProgramHandle = DispatchEncoder.RegisterProgram(
        SkyShaderPath,
        SkySlotTypes,
        "ComputeEntry");

    constexpr std::array<VkDescriptorType, 4u> StellarCullSlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    const std::filesystem::path StellarClearShaderPath = CompiledShaderDirectory / "StellarTileClearCompute.comp.spv";
    StellarClearProgramHandle = DispatchEncoder.RegisterProgram(
        StellarClearShaderPath,
        StellarCullSlotTypes,
        "ClearStellarTilesEntry");
    const std::filesystem::path StellarCullShaderPath = CompiledShaderDirectory / "StellarTileCullCompute.comp.spv";
    StellarCullProgramHandle = DispatchEncoder.RegisterProgram(
        StellarCullShaderPath,
        StellarCullSlotTypes,
        "ProjectCullStellarTilesEntry");

    const std::filesystem::path ShadowCascadeShaderPath = CompiledShaderDirectory / "WorldViewportShadowCascadeCompute.comp.spv";
    ShadowCascadeProgramHandle = DispatchEncoder.RegisterProgram(
        ShadowCascadeShaderPath,
        SlotTypes,
        "ShadowCascadeComputeEntry");

    const std::filesystem::path SdfRadiosityShaderPath = CompiledShaderDirectory / "WorldViewportSdfRadiosityCompute.comp.spv";
    SdfRadiosityProgramHandle = DispatchEncoder.RegisterProgram(
        SdfRadiosityShaderPath,
        SlotTypes,
        "SdfRadiosityComputeEntry");

    const std::filesystem::path DdgiLatticeShaderPath = CompiledShaderDirectory / "WorldViewportDdgiLatticeCompute.comp.spv";
    DdgiLatticeProgramHandle = DispatchEncoder.RegisterProgram(
        DdgiLatticeShaderPath,
        SlotTypes,
        "DdgiLatticeComputeEntry");

    const std::filesystem::path ImportanceReuseShaderPath = CompiledShaderDirectory / "WorldViewportImportanceReuseCompute.comp.spv";
    ImportanceSpatialProgramHandle = DispatchEncoder.RegisterProgram(
        ImportanceReuseShaderPath,
        SlotTypes,
        "ImportanceSpatialReuseEntry");

    constexpr std::array<VkDescriptorType, 16u> ClipmapSlotTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    }};
    ClipmapComposeProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "VoxelSdfClipmapCompose.comp.spv",
        ClipmapSlotTypes,
        "ComposeDynamicLocalSdfClip0Entry");
    ClipmapRadiosityProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "VoxelSdfClipmapRadiosity.comp.spv",
        ClipmapSlotTypes,
        "PropagateClipmapRadiosityBounceEntry");
    ClipmapConeGiProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "VoxelSdfClipmapConeGi.comp.spv",
        ClipmapSlotTypes,
        "EvaluateClipmapConeGiEntry");

    constexpr std::array<VkDescriptorType, 8u> DistEvalSlotTypes {{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};
    constexpr uint32_t DistEvalPushBytes = 128u;
    DistEvalBakeProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "DistanceEvaluation.BakeLocalDistance.comp.spv",
        DistEvalSlotTypes,
        "main",
        DistEvalPushBytes);
    DistEvalFillClipmapProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "DistanceEvaluation.FillClipmap.comp.spv",
        DistEvalSlotTypes,
        "main",
        DistEvalPushBytes);
    DistEvalProbesProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "DistanceEvaluation.EvaluateDistanceProbes.comp.spv",
        DistEvalSlotTypes,
        "main",
        DistEvalPushBytes);
    DistEvalRelightVehicleProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "DistanceEvaluation.RelightVehicleCache.comp.spv",
        DistEvalSlotTypes,
        "main",
        DistEvalPushBytes);
    DistEvalGatherVehicleProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "DistanceEvaluation.GatherVehicleCache.comp.spv",
        DistEvalSlotTypes,
        "main",
        DistEvalPushBytes);
    DistEvalClearVehicleProgramHandle = DispatchEncoder.RegisterProgram(
        CompiledShaderDirectory / "DistanceEvaluation.ClearVehicleCache.comp.spv",
        DistEvalSlotTypes,
        "main",
        DistEvalPushBytes);

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
    }

    {
        VkSamplerCreateInfo SampSpec {};
        SampSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        SampSpec.magFilter    = VK_FILTER_LINEAR;
        SampSpec.minFilter    = VK_FILTER_LINEAR;
        SampSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        SampSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SampSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SampSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SampSpec.maxLod       = 1.0f;
        if (vkCreateSampler(TargetDevice.AccessDevice(), &SampSpec, nullptr, &SdfLinearSampler) != VK_SUCCESS)
        {
            TeardownSequence();
            return false;
        }

        SdfDummySampledVolume3D = TargetDevice.AllocateImage3D(
            1u,
            1u,
            1u,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        SdfDummyStorageVolume3D = TargetDevice.AllocateImage3D(
            1u,
            1u,
            1u,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        if (SdfDummySampledVolume3D.ViewHandle == VK_NULL_HANDLE ||
            SdfDummyStorageVolume3D.ViewHandle == VK_NULL_HANDLE)
        {
            TeardownSequence();
            return false;
        }

        VkCommandBuffer InitImgCmd = TargetDevice.BeginSingleTimeCommands();
        if (InitImgCmd == VK_NULL_HANDLE)
        {
            TeardownSequence();
            return false;
        }
        VkImageMemoryBarrier SampledToClear {};
        SampledToClear.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        SampledToClear.srcAccessMask               = 0u;
        SampledToClear.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
        SampledToClear.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        SampledToClear.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        SampledToClear.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        SampledToClear.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        SampledToClear.image                       = SdfDummySampledVolume3D.ImageHandle;
        SampledToClear.subresourceRange            = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        vkCmdPipelineBarrier(
            InitImgCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            1u,
            &SampledToClear);

        const VkClearColorValue ClearDefault {{ 0.0f, 0.0f, 0.0f, 4.0f }};
        const VkImageSubresourceRange ClearRange { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        vkCmdClearColorImage(
            InitImgCmd,
            SdfDummySampledVolume3D.ImageHandle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &ClearDefault,
            1u,
            &ClearRange);

        std::array<VkImageMemoryBarrier, 2u> ReadyBarriers {};
        ReadyBarriers[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ReadyBarriers[0].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        ReadyBarriers[0].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        ReadyBarriers[0].oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ReadyBarriers[0].newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ReadyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ReadyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ReadyBarriers[0].image               = SdfDummySampledVolume3D.ImageHandle;
        ReadyBarriers[0].subresourceRange    = ClearRange;

        ReadyBarriers[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ReadyBarriers[1].srcAccessMask       = 0u;
        ReadyBarriers[1].dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        ReadyBarriers[1].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        ReadyBarriers[1].newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        ReadyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ReadyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ReadyBarriers[1].image               = SdfDummyStorageVolume3D.ImageHandle;
        ReadyBarriers[1].subresourceRange    = ClearRange;

        vkCmdPipelineBarrier(
            InitImgCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            static_cast<uint32_t>(ReadyBarriers.size()),
            ReadyBarriers.data());

        const uint64_t InitImgSync = TargetDevice.DispatchCommandBufferAsync(InitImgCmd);
        if (!TargetDevice.AwaitTimelineTick(InitImgSync))
        {
            TeardownSequence();
            return false;
        }
    }

    EnsureBufferCapacity(UniformBuffer, 8192ull * 6ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(SdfUniformBuffer, sizeof(SignedDistanceGridUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(SkyViewUniformBuffer, sizeof(AtmosphericSkyUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(StellarCullUniformBuffer, sizeof(StellarCullUniformBlockStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureDeviceBufferCapacity(WeightedCurrentBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(ReservoirHistoryBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
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
    if (UniformBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        std::fprintf(
            stdout,
            "[FrontierWorld] VulkanWorldRenderSequence active on %.*s (Hardware GPU: %s, GPU SDF/Shadow/DDGI: %s/%s/%s)\n",
            static_cast<int>(TargetDevice.QueryAdapterName().size()),
            TargetDevice.QueryAdapterName().data(),
            TargetDevice.QueryHardwareAccelerated() ? "YES" : "NO (Software ICD)",
            SignedDistanceProgramHandle != UINT32_MAX ? "YES" : "NO",
            ShadowCascadeProgramHandle != UINT32_MAX ? "YES" : "NO",
            DdgiLatticeProgramHandle != UINT32_MAX ? "YES" : "NO");
        std::fflush(stdout);
        return true;
    }
    return false;
}

void VulkanWorldRenderSequence::TeardownSequence() noexcept
{
    if (BoundDevice != nullptr && BoundDevice->Ready())
    {
        vkDeviceWaitIdle(BoundDevice->AccessDevice());
        if (TimestampQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(BoundDevice->AccessDevice(), TimestampQueryPool, nullptr);
            TimestampQueryPool = VK_NULL_HANDLE;
        }
        BoundDevice->ReleaseBuffer(UniformBuffer);
        BoundDevice->ReleaseBuffer(SdfUniformBuffer);
        BoundDevice->ReleaseBuffer(SkyViewUniformBuffer);
        BoundDevice->ReleaseBuffer(StellarCullUniformBuffer);
        BoundDevice->ReleaseBuffer(UploadStagingBuffer);
        BoundDevice->ReleaseBuffer(OutputReadbackStagingBuffer);
        BoundDevice->ReleaseBuffer(BvhEntryBuffer);
        BoundDevice->ReleaseBuffer(ShadedTriangleBuffer);
        BoundDevice->ReleaseBuffer(MaterialEntryBuffer);
        BoundDevice->ReleaseBuffer(SignedDistanceBuffer);
        BoundDevice->ReleaseBuffer(IrradianceLatticeBuffer);
        BoundDevice->ReleaseBuffer(OutputPixelBuffer);
        BoundDevice->ReleaseBuffer(SolarShadowDepthBuffer);
        BoundDevice->ReleaseBuffer(IrradianceChebyshevBuffer);
        BoundDevice->ReleaseBuffer(SdfRadiosityVoxelBuffer);
        BoundDevice->ReleaseBuffer(AccumulationHdrBuffer);
        BoundDevice->ReleaseBuffer(AtmosphericSkyViewLutBuffer);
        BoundDevice->ReleaseBuffer(LunarSurfaceAtlasBuffer);
        BoundDevice->ReleaseBuffer(BakedLensFlareAtlasBuffer);
        BoundDevice->ReleaseBuffer(FluidHeroThermoBuffer);
        BoundDevice->ReleaseBuffer(FluidWorldThermoBuffer);
        BoundDevice->ReleaseBuffer(GBufPosMaskBuffer);
        BoundDevice->ReleaseBuffer(GBufNormAoBuffer);
        BoundDevice->ReleaseBuffer(GBufDirectBaseBuffer);
        BoundDevice->ReleaseBuffer(GBufSunContribBuffer);
        BoundDevice->ReleaseBuffer(GBufAlbedoBuffer);
        BoundDevice->ReleaseBuffer(PingShadowAoBuffer);
        BoundDevice->ReleaseBuffer(PingGiHdrBuffer);
        BoundDevice->ReleaseBuffer(PongShadowAoBuffer);
        BoundDevice->ReleaseBuffer(PongGiHdrBuffer);
        BoundDevice->ReleaseBuffer(CloudRadiometricBuffer);
        BoundDevice->ReleaseBuffer(WeightedCurrentBuffer);
        BoundDevice->ReleaseBuffer(ReservoirHistoryBuffer);
        BoundDevice->ReleaseBuffer(RawCatalogStarBuffer);
        BoundDevice->ReleaseBuffer(ProjectedCatalogStarBuffer);
        BoundDevice->ReleaseBuffer(StellarTileBinsBuffer);
        BoundDevice->ReleaseBuffer(ClipmapUniformBuffer);
        BoundDevice->ReleaseBuffer(ClipmapStaticDist0Buffer);
        BoundDevice->ReleaseBuffer(ClipmapDist1Buffer);
        BoundDevice->ReleaseBuffer(ClipmapActiveDist0Buffer);
        BoundDevice->ReleaseBuffer(ClipmapAlbedoMet0Buffer);
        BoundDevice->ReleaseBuffer(ClipmapNormalRough0Buffer);
        BoundDevice->ReleaseBuffer(ClipmapEmissive0Buffer);
        BoundDevice->ReleaseBuffer(ClipmapRadiosityRead0Buffer);
        BoundDevice->ReleaseBuffer(ClipmapRadiosityWrite0Buffer);
        BoundDevice->ReleaseBuffer(ClipmapRadiosityRead1Buffer);
        BoundDevice->ReleaseBuffer(ClipmapDynObjectsBuffer);
        BoundDevice->ReleaseBuffer(ClipmapLocalSdfVoxelsBuffer);
        BoundDevice->ReleaseBuffer(ClipmapPointLightsBuffer);
        BoundDevice->ReleaseBuffer(ClipmapPrimaryPosBuffer);
        BoundDevice->ReleaseBuffer(ClipmapPrimaryNormBuffer);
        BoundDevice->ReleaseImage2D(ClipmapConeGiOutputImage2D);
        BoundDevice->ReleaseImage2D(SdfBakedVolume3D);
        BoundDevice->ReleaseImage2D(SdfDummySampledVolume3D);
        BoundDevice->ReleaseImage2D(SdfDummyStorageVolume3D);
        BoundDevice->ReleaseImage2D(DisplayOutputImage2D);
        if (SdfLinearSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(BoundDevice->AccessDevice(), SdfLinearSampler, nullptr);
            SdfLinearSampler = VK_NULL_HANDLE;
        }
        SdfVolumeInShaderReadLayout = false;
    }
    DispatchEncoder.TeardownEncoder();
    RadiometricProgramHandle      = UINT32_MAX;
    PathTraceProgramHandle        = UINT32_MAX;
    CloudLayerProgramHandle       = UINT32_MAX;
    DenoiseProgramHandle          = UINT32_MAX;
    SignedDistanceProgramHandle   = UINT32_MAX;
    TransmittanceLutProgramHandle = UINT32_MAX;
    MultiScatterLutProgramHandle  = UINT32_MAX;
    SkyViewLutProgramHandle       = UINT32_MAX;
    StellarClearProgramHandle     = UINT32_MAX;
    StellarCullProgramHandle      = UINT32_MAX;
    ShadowCascadeProgramHandle    = UINT32_MAX;
    SdfRadiosityProgramHandle     = UINT32_MAX;
    DdgiLatticeProgramHandle      = UINT32_MAX;
    ImportanceSpatialProgramHandle = UINT32_MAX;
    ClipmapComposeProgramHandle       = UINT32_MAX;
    ClipmapRadiosityProgramHandle     = UINT32_MAX;
    ClipmapConeGiProgramHandle        = UINT32_MAX;
    DistEvalBakeProgramHandle         = UINT32_MAX;
    DistEvalFillClipmapProgramHandle  = UINT32_MAX;
    DistEvalProbesProgramHandle       = UINT32_MAX;
    DistEvalRelightVehicleProgramHandle = UINT32_MAX;
    DistEvalGatherVehicleProgramHandle  = UINT32_MAX;
    DistEvalClearVehicleProgramHandle   = UINT32_MAX;
    UploadedCatalogStarCount      = 0u;
    ReplayWidthPx                 = 0u;
    ReplayHeightPx                = 0u;
    ReplaySequenceIndex           = 0u;
    BoundDevice                   = nullptr;
    SceneUploadedOnce           = false;
    DdgiManagedByGpu            = false;
    UploadedBvhCount            = 0u;
    UploadedMaterialCount       = 0u;
    CachedSdfDimX               = 0u;
    CachedSdfDimY               = 0u;
    CachedSdfDimZ               = 0u;
    CachedLatticeDimX           = 0u;
    CachedLatticeDimY           = 0u;
    CachedLatticeDimZ           = 0u;
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
    BoundDevice->ReleaseBuffer(TargetBuffer);
    const uint64_t PaddedBytes = std::max<uint64_t>(SafeBytes, (SafeBytes * 5ull) / 4ull);
    TargetBuffer               = DispatchEncoder.AllocateReadbackStagingBuffer(PaddedBytes);
}

void VulkanWorldRenderSequence::EnsureSdfVolumeTexture(uint32_t DimX, uint32_t DimY, uint32_t DimZ)
{
    const uint32_t SafeX = std::max(1u, DimX);
    const uint32_t SafeY = std::max(1u, DimY);
    const uint32_t SafeZ = std::max(1u, DimZ);
    if (SdfBakedVolume3D.ImageHandle != VK_NULL_HANDLE &&
        SdfBakedVolume3D.WidthPx == SafeX &&
        SdfBakedVolume3D.HeightPx == SafeY &&
        SdfBakedVolume3D.DepthPx == SafeZ)
    {
        return;
    }
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return;
    }
    vkDeviceWaitIdle(BoundDevice->AccessDevice());
    BoundDevice->ReleaseImage2D(SdfBakedVolume3D);
    SdfBakedVolume3D = BoundDevice->AllocateImage3D(
        SafeX,
        SafeY,
        SafeZ,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    SdfVolumeInShaderReadLayout = false;
}

void VulkanWorldRenderSequence::EnsureDisplayOutputImage(uint32_t WidthPx, uint32_t HeightPx)
{
    const uint32_t SafeW = std::max(1u, WidthPx);
    const uint32_t SafeH = std::max(1u, HeightPx);
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
    vkDeviceWaitIdle(BoundDevice->AccessDevice());
    BoundDevice->ReleaseImage2D(DisplayOutputImage2D);
    DisplayOutputImage2D = BoundDevice->AllocateImage2D(
        SafeW,
        SafeH,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
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
    return BoundDevice->AwaitTimelineTick(SyncPoint);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SCENE BUFFER UPLOAD
//------------------------------------------------------------------------------------------------------------------------

void VulkanWorldRenderSequence::UploadIrradianceLattice(const IrradianceLatticeSequence& RtxLattice)
{
    if (!QueryReady())
    {
        return;
    }

    const auto Cells = RtxLattice.InspectCells();
    if (Cells.empty())
    {
        CachedLatticeDimX = CachedLatticeDimY = CachedLatticeDimZ = 0u;
        const float ZeroVec[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        UploadToDeviceBuffer(IrradianceLatticeBuffer, ZeroVec, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        UploadToDeviceBuffer(IrradianceChebyshevBuffer, ZeroVec, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        return;
    }

    const auto& Cfg   = RtxLattice.InspectConfig();
    CachedLatticeMin  = Cfg.GridMinimumWorld;
    CachedLatticeMax  = Cfg.GridMaximumWorld;
    CachedLatticeDimX = Cfg.CountX;
    CachedLatticeDimY = Cfg.CountY;
    CachedLatticeDimZ = Cfg.CountZ;
    DdgiManagedByGpu  = false;

    std::vector<float> PackedTexels(Cells.size() * 64u * 4u, 0.0f);
    std::vector<float> PackedCheby(Cells.size() * 256u * 2u, 0.0f);
    for (std::size_t CellIdx = 0u; CellIdx < Cells.size(); ++CellIdx)
    {
        for (std::size_t Tex = 0u; Tex < 64u; ++Tex)
        {
            const std::size_t TexelOffset  = (CellIdx * 64u + Tex) * 4u;
            const auto        Irr          = Cells[CellIdx].IrradianceOverPi[Tex];
            PackedTexels[TexelOffset + 0u] = Irr.R;
            PackedTexels[TexelOffset + 1u] = Irr.G;
            PackedTexels[TexelOffset + 2u] = Irr.B;
            PackedTexels[TexelOffset + 3u] = Cells[CellIdx].BackfaceRatio;
        }
        for (std::size_t DIdx = 0u; DIdx < 256u; ++DIdx)
        {
            const std::size_t ChebyOffset = (CellIdx * 256u + DIdx) * 2u;
            const auto        Mom         = Cells[CellIdx].DepthMoments[DIdx];
            PackedCheby[ChebyOffset + 0u] = Mom.X;
            PackedCheby[ChebyOffset + 1u] = Mom.Y;
        }
    }
    const uint64_t LatBytes = PackedTexels.size() * sizeof(float);
    UploadToDeviceBuffer(IrradianceLatticeBuffer, PackedTexels.data(), LatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const uint64_t ChebyBytes = PackedCheby.size() * sizeof(float);
    UploadToDeviceBuffer(IrradianceChebyshevBuffer, PackedCheby.data(), ChebyBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

bool VulkanWorldRenderSequence::DispatchGpuScenePrecompute(const WorldSceneSpecification& Scene, bool BuildDdgiOnGpu)
{
    if (!QueryReady() ||
        ShadowCascadeProgramHandle == UINT32_MAX ||
        SdfRadiosityProgramHandle == UINT32_MAX ||
        UniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    const bool AtmoActive = Scene.Atmosphere.Medium.AtmosphereEnabled || Scene.Atmosphere.Medium.OrbitalViewEnabled;
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
    const Mathematics::Axis3D SunDir = AtmoActive
        ? SolarSolved.WorldDirectionToSun
        : Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });
    LastSyncedSunDir = SunDir;

    // 1. Lightweight cascade basis calculation (zero triangle rasterization on CPU!)
    const Mathematics::Axis3D FocusCenter = (Scene.SceneTree.RootBoundsMin + Scene.SceneTree.RootBoundsMax) * 0.5f;
    const Mathematics::Axis3D LightFwd    = SunDir * -1.0f;
    Mathematics::Axis3D RightAxis {};
    Mathematics::Axis3D UpAxis    {};
    GeometrySpace::ConstructDuffOrthonormalBasis(LightFwd, RightAxis, UpAxis);

    auto InitCascadeBasis = [&](OrthographicShadowCascade& Cascade, float HalfSpanMeters)
    {
        Cascade.HalfSpanMeters    = HalfSpanMeters;
        Cascade.DepthExtentMeters = 28.0f;
        Cascade.ResolutionTexels  = 1024u;
        Cascade.LightRightAxis    = RightAxis;
        Cascade.LightUpAxis       = UpAxis;
        Cascade.LightForwardAxis  = LightFwd;
        const float TexelSizeM = (2.0f * HalfSpanMeters) / static_cast<float>(Cascade.ResolutionTexels);
        const float ProjU      = std::round(Mathematics::EvaluateDot(FocusCenter, RightAxis) / TexelSizeM) * TexelSizeM;
        const float ProjV      = std::round(Mathematics::EvaluateDot(FocusCenter, UpAxis) / TexelSizeM) * TexelSizeM;
        const float ProjZ      = Mathematics::EvaluateDot(FocusCenter, LightFwd);
        Cascade.CenterWorld    = RightAxis * ProjU + UpAxis * ProjV + LightFwd * ProjZ;
        Cascade.ClosestLightDepth.clear();
    };
    InitCascadeBasis(CachedFineSunCascade, 2.0f);
    InitCascadeBasis(CachedSunCascade, 8.0f);
    CoarseShadowOffsetTexels = 1024u * 1024u;

    const uint64_t ShadowDepthBytes = static_cast<uint64_t>(CoarseShadowOffsetTexels) * 2ull * sizeof(float);
    EnsureDeviceBufferCapacity(SolarShadowDepthBuffer, ShadowDepthBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    const uint64_t TotalVoxels = std::max<uint64_t>(1ull, static_cast<uint64_t>(CachedSdfDimX) * CachedSdfDimY * CachedSdfDimZ);
    const uint64_t RadBytes    = std::max<uint64_t>(64ull, TotalVoxels * 4ull * sizeof(float));
    EnsureDeviceBufferCapacity(SdfRadiosityVoxelBuffer, RadBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    const bool RunDdgi = BuildDdgiOnGpu && (DdgiLatticeProgramHandle != UINT32_MAX);
    if (RunDdgi)
    {
        const auto& LatCfg = Scene.LatticeConfig;
        CachedLatticeMin   = LatCfg.GridMinimumWorld;
        CachedLatticeMax   = LatCfg.GridMaximumWorld;
        CachedLatticeDimX  = std::max(2u, LatCfg.CountX);
        CachedLatticeDimY  = std::max(2u, LatCfg.CountY);
        CachedLatticeDimZ  = std::max(2u, LatCfg.CountZ);
        const uint64_t TotalCells = static_cast<uint64_t>(CachedLatticeDimX) * CachedLatticeDimY * CachedLatticeDimZ;
        EnsureDeviceBufferCapacity(IrradianceLatticeBuffer, TotalCells * 64ull * 4ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        EnsureDeviceBufferCapacity(IrradianceChebyshevBuffer, TotalCells * 256ull * 2ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        EnsureDeviceBufferCapacity(PingGiHdrBuffer, TotalCells * 64ull * 4ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        DdgiManagedByGpu = true;
    }

    // Ensure every remaining descriptor slot has a valid non-null VkBuffer
    EnsureDeviceBufferCapacity(IrradianceLatticeBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(IrradianceChebyshevBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(OutputPixelBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(AccumulationHdrBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(FluidHeroThermoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(FluidWorldThermoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufPosMaskBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufNormAoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufDirectBaseBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufSunContribBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GBufAlbedoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PingShadowAoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PingGiHdrBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongShadowAoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongGiHdrBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(CloudRadiometricBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    Mathematics::LinearRGB EffectiveSunIrrad = Scene.Sun.SolarIrradiance;
    Mathematics::LinearRGB SolarTrans { 1.0f, 1.0f, 1.0f };
    if (AtmoActive)
    {
        const float ObsAlt = Scene.Atmosphere.Medium.ObserverAltitudeMeters;
        SolarTrans = AtmosphericSkySequence::ComputeSolarTransmittance(SunDir, Scene.Atmosphere.Medium, ObsAlt);
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
    Uniforms.SunDirConeTan[0]       = SunDir.X;
    Uniforms.SunDirConeTan[1]       = SunDir.Y;
    Uniforms.SunDirConeTan[2]       = SunDir.Z;
    Uniforms.SunDirConeTan[3]       = std::tan(std::max(0.014f, Scene.Sun.SunAngularRadiusRadians));
    Uniforms.SolarIrradianceTier[0] = EffectiveSunIrrad.R;
    Uniforms.SolarIrradianceTier[1] = EffectiveSunIrrad.G;
    Uniforms.SolarIrradianceTier[2] = EffectiveSunIrrad.B;
    Uniforms.SolarIrradianceTier[3] = 1.0f;
    Uniforms.SkyRadianceBvhCount[0] = Scene.SkyRadiance.R;
    Uniforms.SkyRadianceBvhCount[1] = Scene.SkyRadiance.G;
    Uniforms.SkyRadianceBvhCount[2] = Scene.SkyRadiance.B;
    Uniforms.SkyRadianceBvhCount[3] = static_cast<float>(UploadedBvhCount);
    Uniforms.SdfMinSpacing[0]       = CachedSdfMinCorner.X;
    Uniforms.SdfMinSpacing[1]       = CachedSdfMinCorner.Y;
    Uniforms.SdfMinSpacing[2]       = CachedSdfMinCorner.Z;
    Uniforms.SdfMinSpacing[3]       = CachedSdfSpacing;

    const uint32_t LightCount = static_cast<uint32_t>(std::min<std::size_t>(4u, Scene.AreaLights.size()));
    Uniforms.SdfDimsLightCount[0]   = CachedSdfDimX;
    Uniforms.SdfDimsLightCount[1]   = CachedSdfDimY;
    Uniforms.SdfDimsLightCount[2]   = CachedSdfDimZ;
    Uniforms.SdfDimsLightCount[3]   = LightCount;
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
        Dst.RadiancePacked[3]  = 1.0f;
    }

    Uniforms.LatticeMinGlassCount[0]   = CachedLatticeMin.X;
    Uniforms.LatticeMinGlassCount[1]   = CachedLatticeMin.Y;
    Uniforms.LatticeMinGlassCount[2]   = CachedLatticeMin.Z;
    Uniforms.LatticeMinGlassCount[3]   = 0u;
    Uniforms.LatticeMaxDebugMode[0]    = CachedLatticeMax.X;
    Uniforms.LatticeMaxDebugMode[1]    = CachedLatticeMax.Y;
    Uniforms.LatticeMaxDebugMode[2]    = CachedLatticeMax.Z;
    Uniforms.LatticeMaxDebugMode[3]    = 0u;
    Uniforms.LatticeDimsMaterialCount[0] = CachedLatticeDimX;
    Uniforms.LatticeDimsMaterialCount[1] = CachedLatticeDimY;
    Uniforms.LatticeDimsMaterialCount[2] = CachedLatticeDimZ;
    Uniforms.LatticeDimsMaterialCount[3] = UploadedMaterialCount;

    Uniforms.FineCascadeParams[0] = static_cast<float>(CachedFineSunCascade.ResolutionTexels);
    Uniforms.FineCascadeParams[1] = CachedFineSunCascade.DepthExtentMeters;
    Uniforms.FineCascadeParams[2] = static_cast<float>(CoarseShadowOffsetTexels);
    Uniforms.FineCascadeParams[3] = 1.0f;
    Uniforms.FineCascadeCenterSpan[0] = CachedFineSunCascade.CenterWorld.X;
    Uniforms.FineCascadeCenterSpan[1] = CachedFineSunCascade.CenterWorld.Y;
    Uniforms.FineCascadeCenterSpan[2] = CachedFineSunCascade.CenterWorld.Z;
    Uniforms.FineCascadeCenterSpan[3] = CachedFineSunCascade.HalfSpanMeters;

    Uniforms.AtmosphereMaskPad[0] = AtmoActive ? 1u : 0u;
    Uniforms.AtmosphereMaskPad[1] = AtmosphericSkySequence::SkyViewWidthPx;
    Uniforms.AtmosphereMaskPad[2] = AtmosphericSkySequence::SkyViewHeightPx;
    Uniforms.AtmosphereMaskPad[3] = 0u;
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
    Uniforms.SunCascadeForwardPad[3] = 0.0f;

    constexpr uint64_t UniformStrideBytes = 8192ull;
    EnsureBufferCapacity(UniformBuffer, UniformStrideBytes * 4ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uint8_t* UniformBase = reinterpret_cast<uint8_t*>(UniformBuffer.HostAddress);
    Uniforms.DenoiseStepParams[0] = 0u;
    std::memcpy(UniformBase, &Uniforms, sizeof(Uniforms));
    if (RunDdgi)
    {
        WorldViewportUniformBlockStd140 Pass1Uni = Uniforms;
        Pass1Uni.DenoiseStepParams[0] = 1u;
        std::memcpy(UniformBase + UniformStrideBytes, &Pass1Uni, sizeof(Pass1Uni));
    }

    const uint32_t SdfVolW = std::max(1u, CachedSdfDimX);
    const uint32_t SdfVolH = std::max(1u, CachedSdfDimY);
    const uint32_t SdfVolD = std::max(1u, CachedSdfDimZ);
    EnsureSdfVolumeTexture(SdfVolW, SdfVolH, SdfVolD);

    auto MakePrecomputeSlots = [&](uint64_t UniOffset, bool WritingSdfVolume) -> std::array<ComputeSlotDescriptor, 32u>
    {
        VkImageView ReadVolView  = WritingSdfVolume
            ? SdfDummySampledVolume3D.ViewHandle
            : ((SdfBakedVolume3D.ViewHandle != VK_NULL_HANDLE && SdfVolumeInShaderReadLayout)
                ? SdfBakedVolume3D.ViewHandle
                : SdfDummySampledVolume3D.ViewHandle);
        VkImageView WriteVolView = (WritingSdfVolume && SdfBakedVolume3D.ViewHandle != VK_NULL_HANDLE)
            ? SdfBakedVolume3D.ViewHandle
            : SdfDummyStorageVolume3D.ViewHandle;
        return {{
            {  0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle,              sizeof(WorldViewportUniformBlockStd140), UniOffset },
            {  1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,             VK_WHOLE_SIZE },
            {  2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle,       VK_WHOLE_SIZE },
            {  3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialEntryBuffer.BufferHandle,        VK_WHOLE_SIZE },
            {  4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SignedDistanceBuffer.BufferHandle,       VK_WHOLE_SIZE },
            {  5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IrradianceLatticeBuffer.BufferHandle,    VK_WHOLE_SIZE },
            {  6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputPixelBuffer.BufferHandle,          VK_WHOLE_SIZE },
            {  7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SolarShadowDepthBuffer.BufferHandle,     VK_WHOLE_SIZE },
            {  8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IrradianceChebyshevBuffer.BufferHandle,  VK_WHOLE_SIZE },
            {  9u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SdfRadiosityVoxelBuffer.BufferHandle,    VK_WHOLE_SIZE },
            { 10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AccumulationHdrBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AtmosphericSkyViewLutBuffer.BufferHandle,VK_WHOLE_SIZE },
            { 12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LunarSurfaceAtlasBuffer.BufferHandle,    VK_WHOLE_SIZE },
            { 13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BakedLensFlareAtlasBuffer.BufferHandle,  VK_WHOLE_SIZE },
            { 14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FluidHeroThermoBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 15u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FluidWorldThermoBuffer.BufferHandle,     VK_WHOLE_SIZE },
            { 16u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufPosMaskBuffer.BufferHandle,          VK_WHOLE_SIZE },
            { 17u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufNormAoBuffer.BufferHandle,           VK_WHOLE_SIZE },
            { 18u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufDirectBaseBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 19u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufSunContribBuffer.BufferHandle,       VK_WHOLE_SIZE },
            { 20u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufAlbedoBuffer.BufferHandle,           VK_WHOLE_SIZE },
            { 21u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PingShadowAoBuffer.BufferHandle,         VK_WHOLE_SIZE },
            { 22u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PingGiHdrBuffer.BufferHandle,            VK_WHOLE_SIZE },
            { 23u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PongShadowAoBuffer.BufferHandle,         VK_WHOLE_SIZE },
            { 24u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PongGiHdrBuffer.BufferHandle,            VK_WHOLE_SIZE },
            { 25u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CloudRadiometricBuffer.BufferHandle,     VK_WHOLE_SIZE },
            { 26u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, ReadVolView, SdfLinearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 27u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, WriteVolView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 28u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, WeightedCurrentBuffer.BufferHandle,     VK_WHOLE_SIZE },
            { 29u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ReservoirHistoryBuffer.BufferHandle,     VK_WHOLE_SIZE },
            { 30u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ProjectedCatalogStarBuffer.BufferHandle, VK_WHOLE_SIZE },
            { 31u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StellarTileBinsBuffer.BufferHandle,      VK_WHOLE_SIZE },
        }};
    };

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    if (AtmoActive)
    {
        DispatchGpuSkyViewLut(Cmd, Scene);
    }

    const bool WillBakeSdfVol = (UploadedBvhCount > 0u && TotalVoxels >= 8ull && SdfBakedVolume3D.ImageHandle != VK_NULL_HANDLE);
    if (SdfBakedVolume3D.ImageHandle != VK_NULL_HANDLE)
    {
        VkImageMemoryBarrier SdfPrepare {};
        SdfPrepare.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        SdfPrepare.srcAccessMask       = SdfVolumeInShaderReadLayout ? VK_ACCESS_SHADER_READ_BIT : 0u;
        SdfPrepare.dstAccessMask       = WillBakeSdfVol ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        SdfPrepare.oldLayout           = SdfVolumeInShaderReadLayout ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        SdfPrepare.newLayout           = WillBakeSdfVol ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        SdfPrepare.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        SdfPrepare.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        SdfPrepare.image               = SdfBakedVolume3D.ImageHandle;
        SdfPrepare.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        vkCmdPipelineBarrier(
            Cmd,
            SdfVolumeInShaderReadLayout ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            WillBakeSdfVol ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            1u,
            &SdfPrepare);
        SdfVolumeInShaderReadLayout = false;
    }

    const auto SlotsWrite = MakePrecomputeSlots(0ull, true);
    if (UploadedBvhCount == 0u)
    {
        vkCmdFillBuffer(Cmd, SolarShadowDepthBuffer.BufferHandle, 0ull, ShadowDepthBytes, 0x49742400u); // 1.0e6f
        vkCmdFillBuffer(Cmd, SdfRadiosityVoxelBuffer.BufferHandle, 0ull, RadBytes, 0u);
    }
    else
    {
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ShadowCascadeProgramHandle, SlotsWrite, 128u, 128u, 2u))
        {
            return false;
        }

        if (TotalVoxels >= 8ull)
        {
            const uint32_t SdfGroups = static_cast<uint32_t>((TotalVoxels + 63ull) / 64ull);
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, SdfRadiosityProgramHandle, SlotsWrite, SdfGroups, 1u, 1u))
            {
                return false;
            }
        }
    }

    if (SdfBakedVolume3D.ImageHandle != VK_NULL_HANDLE)
    {
        if (!WillBakeSdfVol)
        {
            const VkClearColorValue ClearDefault {{ 0.0f, 0.0f, 0.0f, 4.0f }};
            const VkImageSubresourceRange ClearRange { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
            vkCmdClearColorImage(
                Cmd,
                SdfBakedVolume3D.ImageHandle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &ClearDefault,
                1u,
                &ClearRange);
        }
        VkImageMemoryBarrier SdfToRead {};
        SdfToRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        SdfToRead.srcAccessMask       = WillBakeSdfVol ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        SdfToRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        SdfToRead.oldLayout           = WillBakeSdfVol ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        SdfToRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        SdfToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        SdfToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        SdfToRead.image               = SdfBakedVolume3D.ImageHandle;
        SdfToRead.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        vkCmdPipelineBarrier(
            Cmd,
            WillBakeSdfVol ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            1u,
            &SdfToRead);
        SdfVolumeInShaderReadLayout = true;
    }

    if (RunDdgi)
    {
        const auto SlotsRead0 = MakePrecomputeSlots(0ull, false);
        const uint32_t TotalCells = CachedLatticeDimX * CachedLatticeDimY * CachedLatticeDimZ;
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DdgiLatticeProgramHandle, SlotsRead0, TotalCells, 1u, 1u))
        {
            return false;
        }
        VkMemoryBarrier BounceBarrier {};
        BounceBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        BounceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        BounceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &BounceBarrier,
            0u,
            nullptr,
            0u,
            nullptr);

        const auto SlotsRead1 = MakePrecomputeSlots(UniformStrideBytes, false);
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DdgiLatticeProgramHandle, SlotsRead1, TotalCells, 1u, 1u))
        {
            return false;
        }
    }

    VkMemoryBarrier DoneBarrier {};
    DoneBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    DoneBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    DoneBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0u,
        1u,
        &DoneBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    return BoundDevice->AwaitTimelineTick(SyncPoint, 60000000000ull);
}

void VulkanWorldRenderSequence::UploadSolarCascadeRadiosity(const WorldSceneSpecification& Scene, bool BuildDdgiOnGpu)
{
    if (!QueryReady())
    {
        return;
    }

    if (DispatchGpuScenePrecompute(Scene, BuildDdgiOnGpu))
    {
        return;
    }

    // Fallback CPU path (used only if precompute .comp.spv shaders were unavailable)
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

    const uint64_t TotalVoxels = std::max<uint64_t>(1ull, static_cast<uint64_t>(CachedSdfDimX) * CachedSdfDimY * CachedSdfDimZ);
    const std::vector<float> ZeroRad(static_cast<std::size_t>(TotalVoxels * 4ull), 0.0f);
    UploadToDeviceBuffer(SdfRadiosityVoxelBuffer, ZeroRad.data(), ZeroRad.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

bool VulkanWorldRenderSequence::DispatchGpuSignedDistanceGrid(const WorldSceneSpecification& Scene)
{
    if (!QueryReady() || SignedDistanceProgramHandle == UINT32_MAX || UploadedBvhCount == 0u ||
        SdfUniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    constexpr float PaddingMeters = 0.45f;
    const Mathematics::Axis3D PadVec { PaddingMeters, PaddingMeters, PaddingMeters };
    const Mathematics::Axis3D BoundsMin = Scene.SceneTree.RootBoundsMin - PadVec;
    const Mathematics::Axis3D BoundsMax = Scene.SceneTree.RootBoundsMax + PadVec;
    const Mathematics::Axis3D Span      = BoundsMax - BoundsMin;
    if (Span.X <= 1.0e-4f || Span.Y <= 1.0e-4f || Span.Z <= 1.0e-4f)
    {
        return false;
    }

    constexpr uint32_t TargetX = 64u;
    constexpr uint32_t TargetY = 56u;
    constexpr uint32_t TargetZ = 32u;
    const float StepX = Span.X / static_cast<float>(TargetX - 1u);
    const float StepY = Span.Y / static_cast<float>(TargetY - 1u);
    const float StepZ = Span.Z / static_cast<float>(TargetZ - 1u);

    CachedSdfSpacing   = std::max({ StepX, StepY, StepZ, 0.05f });
    CachedSdfMinCorner = BoundsMin;
    CachedSdfDimX      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.X / CachedSdfSpacing) + 1.0f));
    CachedSdfDimY      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Y / CachedSdfSpacing) + 1.0f));
    CachedSdfDimZ      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Z / CachedSdfSpacing) + 1.0f));

    const uint64_t TotalVoxels = static_cast<uint64_t>(CachedSdfDimX) * CachedSdfDimY * CachedSdfDimZ;
    const uint64_t SdfBytes    = TotalVoxels * sizeof(float);
    EnsureDeviceBufferCapacity(SignedDistanceBuffer, SdfBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (SignedDistanceBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    SignedDistanceGridUniformStd140 Uniforms {};
    Uniforms.GridMinSpacing[0]   = CachedSdfMinCorner.X;
    Uniforms.GridMinSpacing[1]   = CachedSdfMinCorner.Y;
    Uniforms.GridMinSpacing[2]   = CachedSdfMinCorner.Z;
    Uniforms.GridMinSpacing[3]   = CachedSdfSpacing;
    Uniforms.GridDimsBvhCount[0] = CachedSdfDimX;
    Uniforms.GridDimsBvhCount[1] = CachedSdfDimY;
    Uniforms.GridDimsBvhCount[2] = CachedSdfDimZ;
    Uniforms.GridDimsBvhCount[3] = UploadedBvhCount;
    Uniforms.CutoffConfig[0]     = 1.80f;
    std::memcpy(SdfUniformBuffer.HostAddress, &Uniforms, sizeof(Uniforms));

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const std::array<ComputeSlotDescriptor, 4u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SdfUniformBuffer.BufferHandle,     sizeof(SignedDistanceGridUniformStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,       VK_WHOLE_SIZE },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle, VK_WHOLE_SIZE },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SignedDistanceBuffer.BufferHandle, SdfBytes      },
    }};

    const uint32_t GroupsX = (CachedSdfDimX + 3u) / 4u;
    const uint32_t GroupsY = (CachedSdfDimY + 3u) / 4u;
    const uint32_t GroupsZ = (CachedSdfDimZ + 3u) / 4u;
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, SignedDistanceProgramHandle, Slots, GroupsX, GroupsY, GroupsZ))
    {
        return false;
    }

    VkMemoryBarrier ComputeBarrier {};
    ComputeBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0u,
        1u,
        &ComputeBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    return BoundDevice->AwaitTimelineTick(SyncPoint);
}

void VulkanWorldRenderSequence::SynchronizeSceneResources(
    const WorldSceneSpecification&   Scene,
    const IrradianceLatticeSequence* OptionalRtxLattice,
    bool                             RebuildSdfGrid)
{
    if (!QueryReady())
    {
        return;
    }
    const auto SyncStart = std::chrono::steady_clock::now();

    // 1. Upload 128-byte 4-Wide BVH4 entries into DEVICE_LOCAL VRAM
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

    // 2. Upload 96-byte shaded triangles — O(N) fast path when precomputed per-object smooth normals are present!
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
                ShadedTris[Idx]       = OutTri;
            }
        }
    }
    const uint64_t TriBytes = ShadedTris.size() * sizeof(GpuShadedTriangle96);
    UploadToDeviceBuffer(ShadedTriangleBuffer, ShadedTris.data(), TriBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    // 3. Upload 80-byte OpenPBR surface material entries (including Planckian emissive RGB) into DEVICE_LOCAL VRAM
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

    // 4. Generate dense 3D SDF float grid on GPU compute shader (falling back to CPU SceneSdf only if compute shader unavailable)
    if (RebuildSdfGrid || !SceneUploadedOnce || CachedSdfDimX == 0u)
    {
        const bool BuiltOnGpu = DispatchGpuSignedDistanceGrid(Scene);
        if (!BuiltOnGpu)
        {
            if (Scene.SceneSdf.BrickCountX > 0u && Scene.SceneSdf.BrickCountY > 0u && Scene.SceneSdf.BrickCountZ > 0u)
            {
                const Mathematics::Axis3D Span = Scene.SceneSdf.VolumeBoundsMax - Scene.SceneSdf.VolumeBoundsMin;
                constexpr uint32_t TargetX = 44u;
                constexpr uint32_t TargetY = 40u;
                constexpr uint32_t TargetZ = 24u;
                const float StepX = Span.X / static_cast<float>(TargetX - 1u);
                const float StepY = Span.Y / static_cast<float>(TargetY - 1u);
                const float StepZ = Span.Z / static_cast<float>(TargetZ - 1u);
                CachedSdfSpacing   = std::max({ StepX, StepY, StepZ, 0.05f });
                CachedSdfMinCorner = Scene.SceneSdf.VolumeBoundsMin;
                CachedSdfDimX      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.X / CachedSdfSpacing) + 1.0f));
                CachedSdfDimY      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Y / CachedSdfSpacing) + 1.0f));
                CachedSdfDimZ      = static_cast<uint32_t>(std::max(2.0f, std::floor(Span.Z / CachedSdfSpacing) + 1.0f));

                const std::size_t TotalVoxels = static_cast<std::size_t>(CachedSdfDimX) * CachedSdfDimY * CachedSdfDimZ;
                std::vector<float> DenseSdf(TotalVoxels, 2.5f);
                for (uint32_t IZ = 0u; IZ < CachedSdfDimZ; ++IZ)
                {
                    const float WZ = CachedSdfMinCorner.Z + static_cast<float>(IZ) * CachedSdfSpacing;
                    for (uint32_t IY = 0u; IY < CachedSdfDimY; ++IY)
                    {
                        const float WY = CachedSdfMinCorner.Y + static_cast<float>(IY) * CachedSdfSpacing;
                        const std::size_t RowOffset = (static_cast<std::size_t>(IZ) * CachedSdfDimY + IY) * CachedSdfDimX;
                        for (uint32_t IX = 0u; IX < CachedSdfDimX; ++IX)
                        {
                            const float WX = CachedSdfMinCorner.X + static_cast<float>(IX) * CachedSdfSpacing;
                            DenseSdf[RowOffset + IX] = Scene.SceneSdf.SampleSignedDistance({ WX, WY, WZ });
                        }
                    }
                }
                const uint64_t SdfBytes = DenseSdf.size() * sizeof(float);
                UploadToDeviceBuffer(SignedDistanceBuffer, DenseSdf.data(), SdfBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }
            else
            {
                CachedSdfDimX = CachedSdfDimY = CachedSdfDimZ = 0u;
                const float FallbackDist[4]{ 4.0f, 4.0f, 4.0f, 4.0f };
                UploadToDeviceBuffer(SignedDistanceBuffer, FallbackDist, sizeof(FallbackDist), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            }
        }
    }

    // 5. Upload explicit Tier 2 RTX Irradiance Lattice if supplied; otherwise build DDGI 100% on GPU!
    const bool HasExplicitLattice = (OptionalRtxLattice != nullptr && !OptionalRtxLattice->InspectCells().empty());
    if (HasExplicitLattice)
    {
        UploadIrradianceLattice(*OptionalRtxLattice);
    }

    // 6. Build Two-Cascade Solar Shadow Depth + 3D SDF Radiosity Voxels (+ DDGI Lattice when not explicitly supplied) on GPU
    const bool NeedDdgiOnGpu = !HasExplicitLattice && (RebuildSdfGrid || !SceneUploadedOnce || DdgiManagedByGpu || CachedLatticeDimX == 0u);
    if (RebuildSdfGrid || !SceneUploadedOnce || SolarShadowDepthBuffer.BufferHandle == VK_NULL_HANDLE || NeedDdgiOnGpu)
    {
        UploadSolarCascadeRadiosity(Scene, NeedDdgiOnGpu);
    }

    SceneUploadedOnce = true;
    const auto SyncEnd = std::chrono::steady_clock::now();
    LastSceneSyncMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(SyncEnd - SyncStart).count());
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
    WorldRenderConfiguration Config {};
    Config.DebugChannel = DebugChannel;
    if (Tier == WorldRenderTier::Tier2RtxDeterministic)
    {
        Config.Engine           = WorldRenderEngine::DdgiRaytracing;
        Config.ShadowAlgorithm  = SolarShadowAlgorithm::WideTreeRayQuery;
        Config.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;
    }
    else
    {
        Config.Engine           = WorldRenderEngine::SdfGlobalIllumination;
        Config.ShadowAlgorithm  = SolarShadowAlgorithm::ContactPcss;
        Config.ReflectionSource = SpecularReflectionSource::WideTreeRaytrace;
    }
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

    // Pass 1: 256x64 Spherical-Horizon Transmittance LUT
    const uint32_t TransGroupsX = (AtmosphericSkySequence::TransmittanceWidthPx  + 7u) / 8u;
    const uint32_t TransGroupsY = (AtmosphericSkySequence::TransmittanceHeightPx + 7u) / 8u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, TransmittanceLutProgramHandle, SkySlots, TransGroupsX, TransGroupsY, 1u))
    {
        InsertComputeBarrier();
    }

    // Pass 2: 32x32 Infinite-Order Multi-Scatter LUT
    const uint32_t MsGroupsX = (AtmosphericSkySequence::MultiScatterWidthPx  + 7u) / 8u;
    const uint32_t MsGroupsY = (AtmosphericSkySequence::MultiScatterHeightPx + 7u) / 8u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, MultiScatterLutProgramHandle, SkySlots, MsGroupsX, MsGroupsY, 1u))
    {
        InsertComputeBarrier();
    }

    // Pass 3: 192x108 Geometric-Limb-Centered SkyView LUT
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
    uint32_t                        PathTraceSampleIndex,
    const PerspectiveCameraBasis&   Camera,
    const WorldSceneSpecification&  Scene,
    std::vector<uint8_t>&           OutDisplayRgba8)
{
    return ExecuteWorldRenderDispatch(Config, PathTraceSampleIndex, Camera, Scene, true, false, &OutDisplayRgba8);
}

bool VulkanWorldRenderSequence::RenderWorldViewportToGpuImage(
    const WorldRenderConfiguration& Config,
    uint32_t                        PathTraceSampleIndex,
    const PerspectiveCameraBasis&   Camera,
    const WorldSceneSpecification&  Scene,
    VkImageView&                    OutDisplayImageView,
    uint32_t&                       OutWidthPx,
    uint32_t&                       OutHeightPx)
{
    OutDisplayImageView = VK_NULL_HANDLE;
    OutWidthPx          = 0u;
    OutHeightPx         = 0u;
    if (!ExecuteWorldRenderDispatch(Config, PathTraceSampleIndex, Camera, Scene, false, true, nullptr))
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
    uint32_t                        PathTraceSampleIndex,
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

    if (!SceneUploadedOnce)
    {
        SynchronizeSceneResources(Scene, nullptr, true);
    }

    const auto StartClock = std::chrono::steady_clock::now();
    LastViewportTiming = {};

    const uint32_t WidthPx  = std::max(1u, Camera.ViewportWidthPx);
    const uint32_t HeightPx = std::max(1u, Camera.ViewportHeightPx);
    const float    CloudScale = std::clamp(Scene.Atmosphere.Clouds.CloudRenderScale, 0.25f, 1.0f);
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
    EnsureDeviceBufferCapacity(PingGiHdrBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongShadowAoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(PongGiHdrBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(CloudRadiometricBuffer, CloudBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const uint64_t ResBytes = AccBytes * 2ull;
    EnsureDeviceBufferCapacity(WeightedCurrentBuffer, ResBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(ReservoirHistoryBuffer, ResBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const uint32_t StarTileCols  = (std::max(1u, WidthPx) + 15u) / 16u;
    const uint32_t StarTileRows  = (std::max(1u, HeightPx) + 15u) / 16u;
    const uint64_t StarTileBytes = static_cast<uint64_t>(StarTileCols) * static_cast<uint64_t>(StarTileRows) * 65ull * sizeof(uint32_t);
    EnsureDeviceBufferCapacity(StellarTileBinsBuffer, std::max<uint64_t>(64ull, StarTileBytes), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (OutputPixelBuffer.BufferHandle == VK_NULL_HANDLE ||
        (CopyToReadbackStaging && OutputReadbackStagingBuffer.HostAddress == nullptr) ||
        (CopyToDisplayImage2D && DisplayOutputImage2D.ImageHandle == VK_NULL_HANDLE) ||
        AccumulationHdrBuffer.BufferHandle == VK_NULL_HANDLE ||
        GBufPosMaskBuffer.BufferHandle == VK_NULL_HANDLE ||
        PongGiHdrBuffer.BufferHandle == VK_NULL_HANDLE ||
        CloudRadiometricBuffer.BufferHandle == VK_NULL_HANDLE ||
        WeightedCurrentBuffer.BufferHandle == VK_NULL_HANDLE ||
        ReservoirHistoryBuffer.BufferHandle == VK_NULL_HANDLE ||
        StellarTileBinsBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        return false;
    }

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    const bool HasReplayHistory = (ReplayWidthPx == WidthPx && ReplayHeightPx == HeightPx);
    const OrthonormalCameraBasis PrevBasis = HasReplayHistory
        ? ComputeCameraBasis(PreviousReplayCamera)
        : Basis;

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

    Uniforms.PrevCameraOriginTanX[0]    = HasReplayHistory ? PreviousReplayCamera.CameraOrigin.X : Camera.CameraOrigin.X;
    Uniforms.PrevCameraOriginTanX[1]    = HasReplayHistory ? PreviousReplayCamera.CameraOrigin.Y : Camera.CameraOrigin.Y;
    Uniforms.PrevCameraOriginTanX[2]    = HasReplayHistory ? PreviousReplayCamera.CameraOrigin.Z : Camera.CameraOrigin.Z;
    Uniforms.PrevCameraOriginTanX[3]    = PrevBasis.TanHalfFovX;
    Uniforms.PrevCameraRightTanY[0]     = PrevBasis.RightAxis.X;
    Uniforms.PrevCameraRightTanY[1]     = PrevBasis.RightAxis.Y;
    Uniforms.PrevCameraRightTanY[2]     = PrevBasis.RightAxis.Z;
    Uniforms.PrevCameraRightTanY[3]     = PrevBasis.TanHalfFovY;
    Uniforms.PrevCameraUpWidth[0]       = PrevBasis.UpAxis.X;
    Uniforms.PrevCameraUpWidth[1]       = PrevBasis.UpAxis.Y;
    Uniforms.PrevCameraUpWidth[2]       = PrevBasis.UpAxis.Z;
    Uniforms.PrevCameraUpWidth[3]       = static_cast<float>(WidthPx);
    Uniforms.PrevCameraForwardHeight[0] = PrevBasis.ForwardAxis.X;
    Uniforms.PrevCameraForwardHeight[1] = PrevBasis.ForwardAxis.Y;
    Uniforms.PrevCameraForwardHeight[2] = PrevBasis.ForwardAxis.Z;
    Uniforms.PrevCameraForwardHeight[3] = static_cast<float>(HeightPx);
    Uniforms.DenoiseStepParams[2]       = HasReplayHistory ? 1u : 0u;
    Uniforms.DenoiseStepParams[3]       = ReplaySequenceIndex++;

    const bool AtmoActive = Scene.Atmosphere.Medium.AtmosphereEnabled || Scene.Atmosphere.Medium.OrbitalViewEnabled;
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
    const Mathematics::Axis3D SunDir = AtmoActive
        ? SolarSolved.WorldDirectionToSun
        : Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });
    if (ShadowCascadeProgramHandle != UINT32_MAX &&
        Mathematics::EvaluateDot(SunDir, LastSyncedSunDir) < 0.99995f)
    {
        const auto PreSyncStart = std::chrono::steady_clock::now();
        (void)DispatchGpuScenePrecompute(Scene, DdgiManagedByGpu);
        const auto PreSyncEnd = std::chrono::steady_clock::now();
        LastViewportTiming.SceneSyncGpuMs = static_cast<float>(
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
    Mathematics::LinearRGB SolarTrans { 1.0f, 1.0f, 1.0f };
    if (AtmoActive)
    {
        const float ObsAlt = Scene.Atmosphere.Medium.ObserverAltitudeMeters;
        SolarTrans = AtmosphericSkySequence::ComputeSolarTransmittance(SunDir, Scene.Atmosphere.Medium, ObsAlt);
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

    const bool LatticeReady = (CachedLatticeDimX >= 2u);
    Uniforms.SolarIrradianceTier[0] = EffectiveSunIrrad.R;
    Uniforms.SolarIrradianceTier[1] = EffectiveSunIrrad.G;
    Uniforms.SolarIrradianceTier[2] = EffectiveSunIrrad.B;
    Uniforms.SolarIrradianceTier[3] = ((Config.Engine == WorldRenderEngine::DdgiRaytracing ||
                                        Config.Engine == WorldRenderEngine::SdfGlobalIllumination) &&
                                       LatticeReady) ? 1.0f : 0.0f;

    Uniforms.SkyRadianceBvhCount[0] = Scene.SkyRadiance.R;
    Uniforms.SkyRadianceBvhCount[1] = Scene.SkyRadiance.G;
    Uniforms.SkyRadianceBvhCount[2] = Scene.SkyRadiance.B;
    Uniforms.SkyRadianceBvhCount[3] = static_cast<float>(UploadedBvhCount);

    Uniforms.SdfMinSpacing[0]       = CachedSdfMinCorner.X;
    Uniforms.SdfMinSpacing[1]       = CachedSdfMinCorner.Y;
    Uniforms.SdfMinSpacing[2]       = CachedSdfMinCorner.Z;
    Uniforms.SdfMinSpacing[3]       = CachedSdfSpacing;

    const uint32_t LightCount = static_cast<uint32_t>(std::min<std::size_t>(4u, Scene.AreaLights.size()));
    Uniforms.SdfDimsLightCount[0]   = CachedSdfDimX;
    Uniforms.SdfDimsLightCount[1]   = CachedSdfDimY;
    Uniforms.SdfDimsLightCount[2]   = CachedSdfDimZ;
    Uniforms.SdfDimsLightCount[3]   = LightCount;

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

    Uniforms.LatticeMinGlassCount[0] = CachedLatticeMin.X;
    Uniforms.LatticeMinGlassCount[1] = CachedLatticeMin.Y;
    Uniforms.LatticeMinGlassCount[2] = CachedLatticeMin.Z;
    Uniforms.LatticeMinGlassCount[3] = static_cast<float>(GlassCount);

    Uniforms.LatticeMaxDebugMode[0] = CachedLatticeMax.X;
    Uniforms.LatticeMaxDebugMode[1] = CachedLatticeMax.Y;
    Uniforms.LatticeMaxDebugMode[2] = CachedLatticeMax.Z;
    Uniforms.LatticeMaxDebugMode[3] = static_cast<float>(Config.DebugChannel);

    Uniforms.LatticeDimsMaterialCount[0] = CachedLatticeDimX;
    Uniforms.LatticeDimsMaterialCount[1] = CachedLatticeDimY;
    Uniforms.LatticeDimsMaterialCount[2] = CachedLatticeDimZ;
    Uniforms.LatticeDimsMaterialCount[3] = UploadedMaterialCount;

    Uniforms.EngineShadowReflSample[0] = static_cast<uint32_t>(Config.Engine);
    Uniforms.EngineShadowReflSample[1] = static_cast<uint32_t>(Config.ShadowAlgorithm);
    Uniforms.EngineShadowReflSample[2] = static_cast<uint32_t>(Config.ReflectionSource);
    Uniforms.EngineShadowReflSample[3] = PathTraceSampleIndex;

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
    Uniforms.SunCascadeForwardPad[3] = static_cast<float>(std::clamp(Config.PathTraceMaxBounces, 1u, 6u));

    const uint32_t PathSamplesPerDispatch = std::clamp(Config.PathTraceSamples, 1u, 4u);
    Uniforms.FineCascadeCenterSpan[0] = CachedFineSunCascade.CenterWorld.X;
    Uniforms.FineCascadeCenterSpan[1] = CachedFineSunCascade.CenterWorld.Y;
    Uniforms.FineCascadeCenterSpan[2] = CachedFineSunCascade.CenterWorld.Z;
    Uniforms.FineCascadeCenterSpan[3] = CachedFineSunCascade.HalfSpanMeters;

    Uniforms.FineCascadeParams[0] = static_cast<float>(CachedFineSunCascade.ResolutionTexels);
    Uniforms.FineCascadeParams[1] = CachedFineSunCascade.DepthExtentMeters;
    Uniforms.FineCascadeParams[2] = static_cast<float>(CoarseShadowOffsetTexels);
    Uniforms.FineCascadeParams[3] = static_cast<float>(PathSamplesPerDispatch);

    Uniforms.AtmosphereMaskPad[0] = AtmoActive ? 1u : 0u;
    Uniforms.AtmosphereMaskPad[1] = AtmosphericSkySequence::SkyViewWidthPx;
    Uniforms.AtmosphereMaskPad[2] = AtmosphericSkySequence::SkyViewHeightPx;
    Uniforms.AtmosphereMaskPad[3] =
        (Config.GlobalIlluminationEnabled ? 0u : 1u) |
        (Config.ReflectionsEnabled        ? 0u : 2u) |
        (Config.PathTraceDenoise          ? 0u : 4u) |
        (Config.RaytracingEnabled         ? 0u : 8u);

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
                Atmo.SimulationSeconds,
                Atmo.ForceFieldSource,
                Atmo.WindLatticeCascade);
        }
        const float WindVX = DeckWind.VelocityMetersPerSec.X;
        const float WindVY = DeckWind.VelocityMetersPerSec.Y;

        auto& C = Uniforms.CloudParams;
        C.OriginTimeVector[0]      = Camera.CameraOrigin.X;
        C.OriginTimeVector[1]      = Camera.CameraOrigin.Y;
        C.OriginTimeVector[2]      = Camera.CameraOrigin.Z;
        C.OriginTimeVector[3]      = Atmo.SimulationSeconds;
        C.SunAxisToggle[0]   = SunDir.X;
        C.SunAxisToggle[1]   = SunDir.Y;
        C.SunAxisToggle[2]   = SunDir.Z;
        C.SunAxisToggle[3]   = Deck.CloudsEnabled ? 1.0f : 0.0f;
        C.SolarIrradianceExt[0] = EffectiveSunIrrad.R;
        C.SolarIrradianceExt[1] = EffectiveSunIrrad.G;
        C.SolarIrradianceExt[2] = EffectiveSunIrrad.B;
        C.SolarIrradianceExt[3] = Atmo.Medium.PlanetRadiusMeters;
        C.SkyColorWind[0]        = Scene.SkyRadiance.R;
        C.SkyColorWind[1]        = Scene.SkyRadiance.G;
        C.SkyColorWind[2]        = Scene.SkyRadiance.B;
        C.SkyColorWind[3]        = Deck.FollowWindEnabled ? 1.0f : 0.0f;
        C.DeckGeometry[0]       = std::clamp(Deck.CloudFloorMeters, 100.0f, std::max(200.0f, Deck.DeckCeilingMeters - 100.0f));
        C.DeckGeometry[1]       = Deck.LayerThicknessMeters;
        C.DeckGeometry[2]       = Deck.CloudFraction;
        C.DeckGeometry[3]       = Deck.ExtinctionMultiplier;
        C.DeckOptics[0]         = Deck.FeatureScaleMultiplier;
        C.DeckOptics[1]         = Deck.AnvilTopSpread;
        C.DeckOptics[2]         = Deck.ForwardScatterAnisotropy;
        C.DeckOptics[3]         = Deck.WindShearPerKilometer;
        C.WindVectorOcclude[0]   = WindVX;
        C.WindVectorOcclude[1]   = WindVY;
        C.WindVectorOcclude[2]   = Deck.CloudShadow.ShadowEnabled ? 1.0f : 0.0f;
        C.WindVectorOcclude[3]   = Atmo.LocalCloud.LocalEnabled ? 1.0f : 0.0f;
        C.LocalCenterExt[0]     = Atmo.LocalCloud.VolumeCenterMeters.X;
        C.LocalCenterExt[1]     = Atmo.LocalCloud.VolumeCenterMeters.Y;
        C.LocalCenterExt[2]     = Atmo.LocalCloud.VolumeCenterMeters.Z;
        C.LocalCenterExt[3]     = Atmo.LocalCloud.FeatureScaleMeters;
        C.LocalExtentFraction[0] = Atmo.LocalCloud.HalfExtentMeters.X;
        C.LocalExtentFraction[1] = Atmo.LocalCloud.HalfExtentMeters.Y;
        C.LocalExtentFraction[2] = Atmo.LocalCloud.HalfExtentMeters.Z;
        C.LocalExtentFraction[3] = Atmo.LocalCloud.CloudFraction;
        C.LocalDensityPhase[0]  = Atmo.LocalCloud.ExtinctionMultiplier;
        C.LocalDensityPhase[1]  = Atmo.LocalCloud.ForwardScatterAnisotropy;
        C.LocalDensityPhase[2]  = static_cast<float>(WidthPx);
        C.LocalDensityPhase[3]  = static_cast<float>(HeightPx);
        C.FormationOptics[0]    = static_cast<float>(static_cast<uint32_t>(Deck.Category));
        C.FormationOptics[1]    = Deck.WindVeerDegreesPerKm * Constants::DegToRadFloat;
        C.FormationOptics[2]    = std::max(Deck.WindTurbulence, DeckWind.TurbulenceScalar);
        C.CloudShadowOptics[0]  = Deck.CloudShadow.ShadowFraction;
        C.CloudShadowOptics[1]  = Deck.CloudShadow.ShadowDensityMultiplier;
        C.CloudShadowOptics[2]  = Deck.CloudShadow.ShadowScale;
        C.CloudShadowOptics[3]  = Deck.CloudShadow.ShadowAnvil;
        C.CloudPassControl[0]   = (AtmoActive && Deck.CloudsEnabled) ? static_cast<float>(CloudW) : 0.0f;
        C.CloudPassControl[1]   = (AtmoActive && Deck.CloudsEnabled) ? static_cast<float>(CloudH) : 0.0f;
        C.CloudPassControl[2]   = static_cast<float>(std::clamp(Deck.CloudMarchSteps, 12u, 96u));

        const auto BaseCloudWind = AtmosphericWindSolver::ResolveWindSample(
            Atmo.Wind, Deck.WindSpeedMetersPerSecond, Deck.WindBearingDegrees,
            Deck.WindSource, { 0.0f, 0.0f, Deck.CloudFloorMeters }, Deck.CloudFloorMeters,
            Atmo.SimulationSeconds, nullptr, nullptr);
        uint32_t CloudProbeCount = 0u;
        float CouplingScaleH = 1600.0f;
        if (Atmo.ForceFieldSource != nullptr)
        {
            const auto Sources = Atmo.ForceFieldSource->QuerySources();
            bool HasSolo = false;
            for (const auto& Src : Sources)
            {
                if (Src.Enabled && Src.Solo) { HasSolo = true; break; }
            }
            for (const auto& Src : Sources)
            {
                if (!Src.Enabled || !Src.WindField.DrivesAtmosphereAndClouds ||
                    (HasSolo && !Src.Solo) || CloudProbeCount >= 8u)
                {
                    continue;
                }
                const float YawR   = Src.EulerDegrees.Z * Constants::DegToRadFloat;
                const float PitchR = Src.EulerDegrees.Y * Constants::DegToRadFloat;
                const float FwdX   = std::cos(YawR) * std::cos(PitchR);
                const float FwdY   = std::sin(YawR) * std::cos(PitchR);
                const float FwdZ   = -std::sin(PitchR);
                CouplingScaleH = std::max(CouplingScaleH, Src.WindField.CouplingScaleHeightMeters);
                auto& Pr = C.WindProbes[CloudProbeCount++];
                Pr.PosRadius[0]       = Src.EvaluatedCentreMeters.X;
                Pr.PosRadius[1]       = Src.EvaluatedCentreMeters.Y;
                Pr.PosRadius[2]       = Src.EvaluatedCentreMeters.Z;
                Pr.PosRadius[3]       = Src.RadiusMeters;
                Pr.ForwardStrength[0] = FwdX;
                Pr.ForwardStrength[1] = FwdY;
                Pr.ForwardStrength[2] = FwdZ;
                Pr.ForwardStrength[3] = Src.Strength;
                Pr.Params[0]          = static_cast<float>(static_cast<uint32_t>(Src.Model));
                Pr.Params[1]          = Src.SwirlRatio;
                Pr.Params[2]          = Src.CoreRadiusMeters;
                Pr.Params[3]          = Src.WindField.CloudAdvectionGain;
            }
        }
        C.CloudPassControl[3]   = static_cast<float>(CloudProbeCount);
        C.WindForceMeta[0]      = CouplingScaleH;
        C.WindForceMeta[1]      = BaseCloudWind.VelocityMetersPerSec.X;
        C.WindForceMeta[2]      = BaseCloudWind.VelocityMetersPerSec.Y;
        C.WindForceMeta[3]      = Atmo.Medium.ObserverAltitudeMeters;

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
                      Atmo.SimulationSeconds, Atmo.ForceFieldSource, Atmo.WindLatticeCascade)
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

        // Pack Atmospheric Fog (Height Fog + Aerial Perspective) + Wind-Driven LocalFogVolumes
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
                  Atmo.SimulationSeconds, Atmo.ForceFieldSource, Atmo.WindLatticeCascade)
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
                      Atmo.SimulationSeconds, Atmo.ForceFieldSource, Atmo.WindLatticeCascade)
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
        uint32_t ForceProbeCount = 0u;
        if (Atmo.ForceFieldSource != nullptr)
        {
            const auto Sources = Atmo.ForceFieldSource->QuerySources();
            bool HasSolo = false;
            for (const auto& Src : Sources)
            {
                if (Src.Enabled && Src.Solo) { HasSolo = true; break; }
            }
            for (const auto& Src : Sources)
            {
                if (!Src.Enabled || (HasSolo && !Src.Solo) || ForceProbeCount >= 8u) continue;
                const float YawR   = Src.EulerDegrees.Z * Constants::DegToRadFloat;
                const float PitchR = Src.EulerDegrees.Y * Constants::DegToRadFloat;
                const float FwdX   = std::cos(YawR) * std::cos(PitchR);
                const float FwdY   = std::sin(YawR) * std::cos(PitchR);
                const float FwdZ   = -std::sin(PitchR);
                auto& Pr = F.ForceProbes[ForceProbeCount++];
                Pr.PosRadius[0]       = Src.EvaluatedCentreMeters.X;
                Pr.PosRadius[1]       = Src.EvaluatedCentreMeters.Y;
                Pr.PosRadius[2]       = Src.EvaluatedCentreMeters.Z;
                Pr.PosRadius[3]       = Src.RadiusMeters;
                Pr.ForwardStrength[0] = FwdX;
                Pr.ForwardStrength[1] = FwdY;
                Pr.ForwardStrength[2] = FwdZ;
                Pr.ForwardStrength[3] = Src.Strength;
                Pr.Params[0]          = static_cast<float>(static_cast<uint32_t>(Src.Model));
                Pr.Params[1]          = Src.SwirlRatio;
                Pr.Params[2]          = Src.CoreRadiusMeters;
                Pr.Params[3]          = Src.TurbulenceScaleMeters;
            }
        }
        F.FogMeta[0] = static_cast<float>(LocalFogCount);
        F.FogMeta[1] = Fg.AerialFog.ScaleHeightMeters;
        F.FogMeta[2] = (LocalFogCount > 0u) ? Fg.LocalFogs[0].EdgeSoftness : 0.35f;
        F.FogMeta[3] = static_cast<float>(ForceProbeCount);

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
        Cel.SunAxisRadius[0]  = SunDir.X;
        Cel.SunAxisRadius[1]  = SunDir.Y;
        Cel.SunAxisRadius[2]  = SunDir.Z;
        Cel.SunAxisRadius[3]  = Atmo.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat;
        Cel.SunTintIntensity[0] = SolarSolved.EffectiveSolarTint.R;
        Cel.SunTintIntensity[1] = SolarSolved.EffectiveSolarTint.G;
        Cel.SunTintIntensity[2] = SolarSolved.EffectiveSolarTint.B;
        Cel.SunTintIntensity[3] = (Atmo.Solar.SolarEnabled && Atmo.Solar.SolarIntensityMultiplier > 0.0f)
            ? (Atmo.Solar.SolarIntensityMultiplier * Atmo.Medium.SkyBrightnessMultiplier * 2.1f)
            : 0.0f;
        Cel.StarParams[0]          = Atmo.Stars.StarfieldEnabled ? 1.0f : 0.0f;
        Cel.StarParams[1]          = Atmo.Stars.LimitingMagnitude;
        Cel.StarParams[2]          = Atmo.Stars.BrightnessMultiplier;
        Cel.StarParams[3]          = Atmo.Stars.PointSizeScale;
        Cel.StarScintillationRot[0]      = Atmo.Stars.ScintillationEnabled ? 1.0f : 0.0f;
        Cel.StarScintillationRot[1]      = Atmo.Stars.ScintillationDepthPercent;
        Cel.StarScintillationRot[2]      = Atmo.Stars.ScintillationRateHertz;
        Cel.StarScintillationRot[3]      = Atmo.Stars.CelestialRotationDegrees * Constants::DegToRadFloat;
        Cel.ClockViewportDims[0]     = Atmo.SimulationSeconds;
        Cel.ClockViewportDims[1]     = static_cast<float>(WidthPx);
        Cel.ClockViewportDims[2]     = static_cast<float>(HeightPx);
        Cel.ClockViewportDims[3]     = static_cast<float>(LunarStarfieldSequence::MaxMoonSlots);

        const uint32_t ZodiacalQuant = Atmo.Stars.ZodiacalLightEnabled
            ? static_cast<uint32_t>(std::clamp(std::lround(std::max(0.0f, Atmo.Stars.ZodiacalRadianceScale) * 255.0f), 0l, 4095l))
            : 0u;
        const uint32_t AtmoFlags =
            (Atmo.Medium.AtmosphereEnabled ? 1u : 0u) |
            ((Atmo.Medium.OrbitalViewEnabled || (Atmo.Medium.ObserverAltitudeMeters > Atmo.Medium.AtmosphereThicknessMeters)) ? 2u : 0u) |
            (Atmo.Stars.EnableGalacticBand ? 4u : 0u) |
            (Atmo.Stars.ZodiacalLightEnabled ? 8u : 0u) |
            (ZodiacalQuant << 4u);
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
            DstM.TintSynodicPhase[0]       = MTint.R;
            DstM.TintSynodicPhase[1]       = MTint.G;
            DstM.TintSynodicPhase[2]       = MTint.B;
            DstM.TintSynodicPhase[3]       = SrcM.SynodicPhaseFraction;
            DstM.OpticsOrientation[0] = SrcM.BrightnessMultiplier;
            DstM.OpticsOrientation[1] = SrcM.HaloGlowMultiplier;
            DstM.OpticsOrientation[2] = SrcM.SurfaceRollDegrees * Constants::DegToRadFloat;
            DstM.OpticsOrientation[3] = SrcM.SurfacePitchDegrees * Constants::DegToRadFloat;
            DstM.MetaMask[0]          = SrcM.Visible ? 1.0f : 0.0f;
            DstM.MetaMask[1]          = SrcM.HasAtmosphere ? SrcM.AtmosphereIntensity : 0.0f;
            DstM.MetaMask[2]          = static_cast<float>(static_cast<uint32_t>(SrcM.SurfacePreset));
            DstM.MetaMask[3]          = SrcM.HasAtmosphere ? SrcM.AtmosphereThicknessScale : 0.0f;
        }

        const auto& LF = Atmo.LensFlare;
        auto&       LFU = Uniforms.LensFlareParams;
        if (AtmoActive && LF.Enabled && Config.DebugChannel == 0u)
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

        // Upload Dual-Tier Fluid Volume (Hero + World Drift) + Emissive Fire Lights
        auto& FLU = Uniforms.FluidParams;
        if (Scene.OptionalFluid != nullptr && Config.DebugChannel == 0u)
        {
            const auto& Doc  = Scene.OptionalFluid->InspectDocument();
            const auto& Hero = Scene.OptionalFluid->InspectHeroGrid();
            const auto& Wrld = Scene.OptionalFluid->InspectWorldDriftGrid();
            const auto& Sh   = Doc.Shading;
            const auto& Gi   = Doc.GiCoupling;

            FLU.HeroBoxMinFade[0]  = Hero.BoxMinWorld.X;
            FLU.HeroBoxMinFade[1]  = Hero.BoxMinWorld.Y;
            FLU.HeroBoxMinFade[2]  = Hero.BoxMinWorld.Z;
            FLU.HeroBoxMinFade[3]  = (Hero.Resolution > 0u) ? 1.0f : 0.0f;
            FLU.HeroBoxMaxRes[0]   = Hero.BoxMaxWorld.X;
            FLU.HeroBoxMaxRes[1]   = Hero.BoxMaxWorld.Y;
            FLU.HeroBoxMaxRes[2]   = Hero.BoxMaxWorld.Z;
            FLU.HeroBoxMaxRes[3]   = static_cast<float>(Hero.Resolution);
            FLU.WorldCenterSpan[0] = Wrld.CenterWorld.X;
            FLU.WorldCenterSpan[1] = Wrld.CenterWorld.Y;
            FLU.WorldCenterSpan[2] = Wrld.CenterWorld.Z;
            FLU.WorldCenterSpan[3] = Wrld.SpanMeters;
            FLU.SunDirRaySteps[0]  = SunDir.X;
            FLU.SunDirRaySteps[1]  = SunDir.Y;
            FLU.SunDirRaySteps[2]  = SunDir.Z;
            FLU.SunDirRaySteps[3]  = static_cast<float>(Sh.RaymarchSteps);
            FLU.SolarIrradiance[0] = Scene.Sun.SolarIrradiance.R;
            FLU.SolarIrradiance[1] = Scene.Sun.SolarIrradiance.G;
            FLU.SolarIrradiance[2] = Scene.Sun.SolarIrradiance.B;
            FLU.SolarIrradiance[3] = static_cast<float>(Sh.ShadowSteps);
            FLU.SkyRadiance[0]     = Scene.SkyRadiance.R;
            FLU.SkyRadiance[1]     = Scene.SkyRadiance.G;
            FLU.SkyRadiance[2]     = Scene.SkyRadiance.B;
            FLU.SkyRadiance[3]     = Scene.OptionalFluid->QueryElapsedSeconds();
            FLU.ShadingCore[0]     = Sh.DensityExtinction;
            FLU.ShadingCore[1]     = Sh.SmokeAlbedo;
            FLU.ShadingCore[2]     = Sh.FireIntensity;
            FLU.ShadingCore[3]     = Sh.TemperatureScale;
            FLU.ShadingScatter[0]  = Sh.AmbientIntensity;
            FLU.ShadingScatter[1]  = Sh.PhaseAnisotropy;
            FLU.ShadingScatter[2]  = Sh.ShadowDensity;
            FLU.ShadingScatter[3]  = Sh.InternalScattering;
            FLU.ShadingMode[0]     = static_cast<float>(static_cast<uint32_t>(Sh.ColorPalette));
            FLU.ShadingMode[1]     = static_cast<float>(static_cast<uint32_t>(Sh.RenderChannel));
            FLU.ShadingMode[2]     = (Doc.Domain.WorldGridEnabled && Wrld.Resolution > 0u) ? 1.0f : 0.0f;
            FLU.ShadingMode[3]     = Gi.CastGiShadows ? Gi.GiShadowStrength : 0.0f;
            FLU.CrossSectionCfg[0] = static_cast<float>(Sh.SliceAxis);
            FLU.CrossSectionCfg[1] = Sh.SlicePosition;
            FLU.CrossSectionCfg[2] = Sh.GodRaysIntensity;
            FLU.CrossSectionCfg[3] = Scene.OptionalFluid->QueryShockwaveAge();
            FLU.ShockwaveCenter[0] = Scene.OptionalFluid->QueryShockwaveCenter().X;
            FLU.ShockwaveCenter[1] = Scene.OptionalFluid->QueryShockwaveCenter().Y;
            FLU.ShockwaveCenter[2] = Scene.OptionalFluid->QueryShockwaveCenter().Z;
            FLU.ShockwaveCenter[3] = static_cast<float>(Wrld.Resolution);

            const auto& FLights = Scene.OptionalFluid->InspectEmissiveLights();
            for (uint32_t FI = 0u; FI < 8u; ++FI)
            {
                if (FI < FLights.size())
                {
                    FLU.EmissiveLights[FI].PositionRadius[0]  = FLights[FI].WorldPosition.X;
                    FLU.EmissiveLights[FI].PositionRadius[1]  = FLights[FI].WorldPosition.Y;
                    FLU.EmissiveLights[FI].PositionRadius[2]  = FLights[FI].WorldPosition.Z;
                    FLU.EmissiveLights[FI].PositionRadius[3]  = FLights[FI].RadiusMeters;
                    FLU.EmissiveLights[FI].ColorIrradiance[0] = FLights[FI].ColorIrradiance.R;
                    FLU.EmissiveLights[FI].ColorIrradiance[1] = FLights[FI].ColorIrradiance.G;
                    FLU.EmissiveLights[FI].ColorIrradiance[2] = FLights[FI].ColorIrradiance.B;
                    FLU.EmissiveLights[FI].ColorIrradiance[3] = 1.0f;
                }
            }

            const auto& HeroTh = Hero.Thermo[Hero.ReadIdx()];
            const uint64_t HeroBytes = std::max<uint64_t>(16ull, static_cast<uint64_t>(HeroTh.size() * sizeof(SimulationSpace::FluidSpace::FluidVoxelSample4)));
            EnsureBufferCapacity(FluidHeroThermoBuffer, HeroBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            if (FluidHeroThermoBuffer.HostAddress != nullptr && !HeroTh.empty())
            {
                std::memcpy(FluidHeroThermoBuffer.HostAddress, HeroTh.data(), HeroTh.size() * sizeof(SimulationSpace::FluidSpace::FluidVoxelSample4));
            }

            const uint64_t WorldBytes = std::max<uint64_t>(16ull, static_cast<uint64_t>(Wrld.Thermo.size() * sizeof(SimulationSpace::FluidSpace::FluidVoxelSample4)));
            EnsureBufferCapacity(FluidWorldThermoBuffer, WorldBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            if (FluidWorldThermoBuffer.HostAddress != nullptr && !Wrld.Thermo.empty())
            {
                std::memcpy(FluidWorldThermoBuffer.HostAddress, Wrld.Thermo.data(), Wrld.Thermo.size() * sizeof(SimulationSpace::FluidSpace::FluidVoxelSample4));
            }
        }
        else
        {
            FLU.HeroBoxMinFade[3] = 0.0f;
            EnsureDeviceBufferCapacity(FluidHeroThermoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            EnsureDeviceBufferCapacity(FluidWorldThermoBuffer, 64ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
    }

    constexpr uint64_t UniformStrideBytes = 8192ull;
    EnsureBufferCapacity(UniformBuffer, UniformStrideBytes * 6ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uint8_t* UniformBase = reinterpret_cast<uint8_t*>(UniformBuffer.HostAddress);
    std::memcpy(UniformBase, &Uniforms, sizeof(Uniforms));

    const auto UniformDoneClock = std::chrono::steady_clock::now();
    const float CpuUniformMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(UniformDoneClock - StartClock).count());

    uint32_t ActiveRadiometricProgram = RadiometricProgramHandle;
    if (Config.Engine == WorldRenderEngine::DeterministicPathTrace && Config.DebugChannel == 0u)
    {
        if (PathTraceProgramHandle == UINT32_MAX && !CachedCompiledShaderDir.empty())
        {
            constexpr std::array<VkDescriptorType, 32u> PtSlotTypes {{
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            }};
            PathTraceProgramHandle = DispatchEncoder.RegisterProgram(
                CachedCompiledShaderDir / "WorldViewportPathTraceCompute.comp.spv",
                PtSlotTypes,
                "ComputePathTraceEntry");
        }
        if (PathTraceProgramHandle == UINT32_MAX)
        {
            return false;
        }
        ActiveRadiometricProgram = PathTraceProgramHandle;
    }

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    const bool UseGpuTimers = (TimestampQueryPool != VK_NULL_HANDLE && BoundDevice->QueryTimestampValidBits() > 0u);
    auto WriteStamp = [&](uint32_t Index, VkPipelineStageFlagBits Stage)
    {
        if (UseGpuTimers)
        {
            vkCmdWriteTimestamp(Cmd, Stage, TimestampQueryPool, Index);
        }
    };
    if (UseGpuTimers)
    {
        vkCmdResetQueryPool(Cmd, TimestampQueryPool, 0u, 10u);
    }

    WriteStamp(0u, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    if (AtmoActive)
    {
        DispatchGpuSkyViewLut(Cmd, Scene);
        DispatchGpuStellarTileCull(Cmd, Camera, Scene, WidthPx, HeightPx);
    }
    WriteStamp(1u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    auto MakePassSlots = [&](uint64_t UniOffset, bool SwapPingPong) -> std::array<ComputeSlotDescriptor, 32u>
    {
        const auto& InShadow  = SwapPingPong ? PongShadowAoBuffer : PingShadowAoBuffer;
        const auto& InGi      = SwapPingPong ? PongGiHdrBuffer    : PingGiHdrBuffer;
        const auto& OutShadow = SwapPingPong ? PingShadowAoBuffer : PongShadowAoBuffer;
        const auto& OutGi     = SwapPingPong ? PingGiHdrBuffer    : PongGiHdrBuffer;
        VkImageView ReadVolView = (SdfBakedVolume3D.ViewHandle != VK_NULL_HANDLE && SdfVolumeInShaderReadLayout)
            ? SdfBakedVolume3D.ViewHandle
            : SdfDummySampledVolume3D.ViewHandle;
        return {{
            {  0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle,              sizeof(WorldViewportUniformBlockStd140), UniOffset },
            {  1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BvhEntryBuffer.BufferHandle,             VK_WHOLE_SIZE },
            {  2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadedTriangleBuffer.BufferHandle,       VK_WHOLE_SIZE },
            {  3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialEntryBuffer.BufferHandle,        VK_WHOLE_SIZE },
            {  4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SignedDistanceBuffer.BufferHandle,       VK_WHOLE_SIZE },
            {  5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IrradianceLatticeBuffer.BufferHandle,    VK_WHOLE_SIZE },
            {  6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutputPixelBuffer.BufferHandle,          OutBytes      },
            {  7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SolarShadowDepthBuffer.BufferHandle,     VK_WHOLE_SIZE },
            {  8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IrradianceChebyshevBuffer.BufferHandle,  VK_WHOLE_SIZE },
            {  9u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SdfRadiosityVoxelBuffer.BufferHandle,    VK_WHOLE_SIZE },
            { 10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AccumulationHdrBuffer.BufferHandle,      AccBytes      },
            { 11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AtmosphericSkyViewLutBuffer.BufferHandle,VK_WHOLE_SIZE },
            { 12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LunarSurfaceAtlasBuffer.BufferHandle,    VK_WHOLE_SIZE },
            { 13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BakedLensFlareAtlasBuffer.BufferHandle,  VK_WHOLE_SIZE },
            { 14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FluidHeroThermoBuffer.BufferHandle,      VK_WHOLE_SIZE },
            { 15u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FluidWorldThermoBuffer.BufferHandle,     VK_WHOLE_SIZE },
            { 16u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufPosMaskBuffer.BufferHandle,          AccBytes      },
            { 17u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufNormAoBuffer.BufferHandle,           AccBytes      },
            { 18u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufDirectBaseBuffer.BufferHandle,       AccBytes      },
            { 19u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufSunContribBuffer.BufferHandle,       AccBytes      },
            { 20u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GBufAlbedoBuffer.BufferHandle,           AccBytes      },
            { 21u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InShadow.BufferHandle,                   AccBytes      },
            { 22u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InGi.BufferHandle,                       AccBytes      },
            { 23u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutShadow.BufferHandle,                  AccBytes      },
            { 24u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutGi.BufferHandle,                      AccBytes      },
            { 25u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CloudRadiometricBuffer.BufferHandle,     CloudBytes    },
            { 26u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_NULL_HANDLE, 0ull, 0ull, ReadVolView, SdfLinearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 27u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_NULL_HANDLE, 0ull, 0ull, SdfDummyStorageVolume3D.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
            { 28u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, WeightedCurrentBuffer.BufferHandle,     ResBytes      },
            { 29u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ReservoirHistoryBuffer.BufferHandle,     ResBytes      },
            { 30u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ProjectedCatalogStarBuffer.BufferHandle, VK_WHOLE_SIZE },
            { 31u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, StellarTileBinsBuffer.BufferHandle,      VK_WHOLE_SIZE },
        }};
    };

    const uint32_t CloudGroupsX = (CloudW   + 7u) / 8u;
    const uint32_t CloudGroupsY = (CloudH   + 7u) / 8u;
    const uint32_t GroupsX      = (WidthPx  + 7u) / 8u;
    const uint32_t GroupsY      = (HeightPx + 7u) / 8u;
    const auto Pass0Slots = MakePassSlots(0ull, false);
    WriteStamp(2u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (AtmoActive && Scene.Atmosphere.Clouds.CloudsEnabled && Config.DebugChannel == 0u)
    {
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, CloudLayerProgramHandle, Pass0Slots, CloudGroupsX, CloudGroupsY, 1u))
        {
            return false;
        }
    }
    WriteStamp(3u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    WriteStamp(4u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ActiveRadiometricProgram, Pass0Slots, GroupsX, GroupsY, 1u))
    {
        return false;
    }
    WriteStamp(5u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    const uint32_t EngIdx = static_cast<uint32_t>(Config.Engine);
    const bool ShadingDebug =
        (Config.DebugChannel == 0u || Config.DebugChannel == 8u ||
         Config.DebugChannel == 12u || Config.DebugChannel == 13u ||
         Config.DebugChannel == 14u || Config.DebugChannel == 15u);

    if (EngIdx == 3u && Config.GlobalIlluminationEnabled && ShadingDebug && ImportanceSpatialProgramHandle != UINT32_MAX)
    {
        VkMemoryBarrier ReservoirBarrier {};
        ReservoirBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        ReservoirBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ReservoirBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &ReservoirBarrier,
            0u,
            nullptr,
            0u,
            nullptr);
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ImportanceSpatialProgramHandle, Pass0Slots, GroupsX, GroupsY, 1u))
        {
            return false;
        }
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &ReservoirBarrier,
            0u,
            nullptr,
            0u,
            nullptr);
        PreviousReplayCamera = Camera;
        ReplayWidthPx        = WidthPx;
        ReplayHeightPx       = HeightPx;
    }

    const bool SpatialDenoise =
        ShadingDebug &&
        ((EngIdx == 1u || EngIdx == 2u || EngIdx == 3u) ||
         (EngIdx == 4u && Config.PathTraceDenoise && PathTraceSampleIndex < 16u));

    WriteStamp(6u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (SpatialDenoise)
    {
        constexpr std::array<uint32_t, 5u> Strides {{ 1u, 2u, 4u, 8u, 16u }};
        const uint32_t PassCount = (EngIdx == 3u && Config.SdfGiBilateral) ? 5u : 3u;
        for (uint32_t Pass = 0u; Pass < PassCount; ++Pass)
        {
            const uint64_t UniOffs = static_cast<uint64_t>(Pass + 1u) * UniformStrideBytes;
            WorldViewportUniformBlockStd140 PassUni = Uniforms;
            PassUni.DenoiseStepParams[0] = Strides[Pass];
            PassUni.DenoiseStepParams[1] = 2u | ((Pass + 1u == PassCount) ? 1u : 0u);
            std::memcpy(UniformBase + UniOffs, &PassUni, sizeof(PassUni));

            const auto PassSlots = MakePassSlots(UniOffs, (Pass & 1u) != 0u);
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DenoiseProgramHandle, PassSlots, GroupsX, GroupsY, 1u))
            {
                return false;
            }
        }
    }
    else
    {
        const uint64_t UniOffs = UniformStrideBytes;
        WorldViewportUniformBlockStd140 PassUni = Uniforms;
        PassUni.DenoiseStepParams[0] = 1u;
        PassUni.DenoiseStepParams[1] = 1u;
        std::memcpy(UniformBase + UniOffs, &PassUni, sizeof(PassUni));

        const auto PassSlots = MakePassSlots(UniOffs, false);
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DenoiseProgramHandle, PassSlots, GroupsX, GroupsY, 1u))
        {
            return false;
        }
    }
    WriteStamp(7u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

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
    if (!BoundDevice->AwaitTimelineTick(SyncPoint, 60000000000ull))
    {
        std::fprintf(stderr, "[VulkanWorldRenderSequence] AwaitTimelineTick timed out (Engine=%u, %ux%u)\n",
                     static_cast<unsigned>(Config.Engine), WidthPx, HeightPx);
        BoundDevice->FreeCommandBuffer(Cmd);
        return false;
    }
    BoundDevice->FreeCommandBuffer(Cmd);

    if (UseGpuTimers)
    {
        uint64_t Stamps[10u]{};
        if (vkGetQueryPoolResults(
                BoundDevice->AccessDevice(),
                TimestampQueryPool,
                0u,
                10u,
                sizeof(Stamps),
                Stamps,
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
        {
            const uint32_t Bits = BoundDevice->QueryTimestampValidBits();
            const uint64_t Mask = (Bits >= 64u) ? ~0ull : ((1ull << Bits) - 1ull);
            const double PeriodMs = static_cast<double>(BoundDevice->QueryTimestampPeriodNs()) * 1.0e-6;
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
    LastViewportTiming.GpuHostSubmitWaitMs = std::max(0.0f, (TotalDispatchMs - LastViewportTiming.GpuTotalPassMs)) + CpuUniformMs * 0.0f + CopyMs * 0.0f;
    if (LastViewportTiming.SceneSyncGpuMs <= 0.0f && LastSceneSyncMilliseconds > 0.0f)
    {
        LastViewportTiming.SceneSyncGpuMs = LastSceneSyncMilliseconds;
        LastSceneSyncMilliseconds          = 0.0f;
    }

    LastDispatchMilliseconds = TotalDispatchMs;
    return true;
}

bool VulkanWorldRenderSequence::ExecuteGpuVoxelSdfClipmap(
    const DistanceFieldSpace::VoxelSdfClipmapVolume&             Clipmap,
    const WorldSceneSpecification&                               Scene,
    std::span<const DistanceFieldSpace::DynamicLocalSdfInstance> DynamicObjects,
    std::span<const float>                                       PrimaryPositionsXyzw,
    std::span<const float>                                       PrimaryNormalsXyzw,
    uint32_t                                                     WidthPx,
    uint32_t                                                     HeightPx,
    std::vector<Mathematics::LinearRGB>&                         OutConeGiOverPi,
    float&                                                       OutGpuElapsedMs)
{
    OutGpuElapsedMs = 0.0f;
    if (BoundDevice == nullptr || !BoundDevice->Ready() || !QueryGpuVoxelSdfClipmapReady())
    {
        return false;
    }
    const auto& C0 = Clipmap.ActiveClip0;
    const auto& S0 = Clipmap.StaticClip0;
    const auto& C1 = Clipmap.StaticClip1;
    if (C0.DimX < 2u || C0.Voxels.empty() || C1.DimX < 2u || C1.Voxels.empty() || WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }
    const std::size_t PixelCount = static_cast<std::size_t>(WidthPx) * HeightPx;
    if (PrimaryPositionsXyzw.size() < PixelCount * 4u || PrimaryNormalsXyzw.size() < PixelCount * 4u)
    {
        return false;
    }

    const auto T0 = std::chrono::steady_clock::now();

    struct alignas(16) ClipmapUniformBlockStd140
    {
        uint32_t Clip0Dims[4]          {};
        uint32_t Clip1Dims[4]          {};
        float    Clip0CenterSpacing[4] {};
        float    Clip0HalfExtents[4]   {};
        float    Clip1CenterSpacing[4] {};
        float    Clip1HalfExtents[4]   {};
        uint32_t GridParams[4]         {};
        float    SunDirTanRadius[4]    {};
        float    SunIrradiancePad[4]   {};
        float    SkyRadiancePad[4]     {};
        int32_t  DirtyMinVoxel[4]      {};
        int32_t  DirtyMaxVoxel[4]      {};
    };
    struct alignas(16) DynamicLocalSdfGpuStd430
    {
        float    WorldToLocalRow0[4] {};
        float    WorldToLocalRow1[4] {};
        float    WorldToLocalRow2[4] {};
        float    LocalMinSpacing[4]  {};
        float    LocalMaxPad[4]      {};
        uint32_t LocalDimsOffset[4]  {};
        float    WorldAabbMin[4]     {};
        float    WorldAabbMax[4]     {};
        float    EmissiveRadiosity[4]{};
        float    AlbedoRoughness[4]  {};
    };
    struct alignas(16) PointLightGpuStd430
    {
        float PositionRange[4]  {};
        float ColorIntensity[4] {};
        float RadiusPad[4]      {};
    };

    const uint32_t Total0 = C0.DimX * C0.DimY * C0.DimZ;
    const uint32_t Total1 = C1.DimX * C1.DimY * C1.DimZ;

    std::vector<float> StaticDist0(Total0);
    std::vector<float> ActiveDist0(Total0);
    std::vector<float> AlbMet0(static_cast<std::size_t>(Total0) * 4u);
    std::vector<float> NrmRgh0(static_cast<std::size_t>(Total0) * 4u);
    std::vector<float> Emis0(static_cast<std::size_t>(Total0) * 4u);
    std::vector<float> Rad0(static_cast<std::size_t>(Total0) * 4u);
    for (uint32_t I = 0u; I < Total0; ++I)
    {
        const auto& SV = S0.Voxels[I];
        const auto& AV = C0.Voxels[I];
        StaticDist0[I] = SV.SignedDistanceM;
        ActiveDist0[I] = AV.SignedDistanceM;
        AlbMet0[I * 4u + 0u] = AV.AlbedoRgb.R;
        AlbMet0[I * 4u + 1u] = AV.AlbedoRgb.G;
        AlbMet0[I * 4u + 2u] = AV.AlbedoRgb.B;
        AlbMet0[I * 4u + 3u] = AV.Metalness;
        NrmRgh0[I * 4u + 0u] = AV.SurfaceNormal.X;
        NrmRgh0[I * 4u + 1u] = AV.SurfaceNormal.Y;
        NrmRgh0[I * 4u + 2u] = AV.SurfaceNormal.Z;
        NrmRgh0[I * 4u + 3u] = AV.HasSurface ? AV.Roughness : -1.0f;
        Emis0[I * 4u + 0u]   = AV.EmissiveRgb.R;
        Emis0[I * 4u + 1u]   = AV.EmissiveRgb.G;
        Emis0[I * 4u + 2u]   = AV.EmissiveRgb.B;
        Emis0[I * 4u + 3u]   = 1.0f;
        Rad0[I * 4u + 0u]    = AV.BounceRadiance.R;
        Rad0[I * 4u + 1u]    = AV.BounceRadiance.G;
        Rad0[I * 4u + 2u]    = AV.BounceRadiance.B;
        Rad0[I * 4u + 3u]    = 1.0f;
    }

    std::vector<float> Dist1(Total1);
    std::vector<float> Rad1(static_cast<std::size_t>(Total1) * 4u);
    for (uint32_t I = 0u; I < Total1; ++I)
    {
        const auto& V = C1.Voxels[I];
        Dist1[I] = V.SignedDistanceM;
        Rad1[I * 4u + 0u] = V.BounceRadiance.R;
        Rad1[I * 4u + 1u] = V.BounceRadiance.G;
        Rad1[I * 4u + 2u] = V.BounceRadiance.B;
        Rad1[I * 4u + 3u] = 1.0f;
    }

    std::vector<DynamicLocalSdfGpuStd430> DynEntries;
    std::vector<float> LocalSdfVoxels;
    for (const auto& Obj : DynamicObjects)
    {
        if (!Obj.Enabled || Obj.Asset == nullptr || Obj.Asset->LocalVoxels.empty()) continue;
        DynamicLocalSdfGpuStd430 Entry {};
        const auto& M = Obj.Transform.WorldToLocal;
        Entry.WorldToLocalRow0[0] = M[0];  Entry.WorldToLocalRow0[1] = M[1];  Entry.WorldToLocalRow0[2] = M[2];  Entry.WorldToLocalRow0[3] = M[3];
        Entry.WorldToLocalRow1[0] = M[4];  Entry.WorldToLocalRow1[1] = M[5];  Entry.WorldToLocalRow1[2] = M[6];  Entry.WorldToLocalRow1[3] = M[7];
        Entry.WorldToLocalRow2[0] = M[8];  Entry.WorldToLocalRow2[1] = M[9];  Entry.WorldToLocalRow2[2] = M[10]; Entry.WorldToLocalRow2[3] = M[11];
        Entry.LocalMinSpacing[0]  = Obj.Asset->LocalMin.X;
        Entry.LocalMinSpacing[1]  = Obj.Asset->LocalMin.Y;
        Entry.LocalMinSpacing[2]  = Obj.Asset->LocalMin.Z;
        Entry.LocalMinSpacing[3]  = Obj.Asset->VoxelSpacingM;
        Entry.LocalMaxPad[0]      = Obj.Asset->LocalMax.X;
        Entry.LocalMaxPad[1]      = Obj.Asset->LocalMax.Y;
        Entry.LocalMaxPad[2]      = Obj.Asset->LocalMax.Z;
        Entry.LocalDimsOffset[0]  = Obj.Asset->DimX;
        Entry.LocalDimsOffset[1]  = Obj.Asset->DimY;
        Entry.LocalDimsOffset[2]  = Obj.Asset->DimZ;
        Entry.LocalDimsOffset[3]  = static_cast<uint32_t>(LocalSdfVoxels.size());
        Entry.WorldAabbMin[0]     = Obj.WorldAabbMin.X;
        Entry.WorldAabbMin[1]     = Obj.WorldAabbMin.Y;
        Entry.WorldAabbMin[2]     = Obj.WorldAabbMin.Z;
        Entry.WorldAabbMax[0]     = Obj.WorldAabbMax.X;
        Entry.WorldAabbMax[1]     = Obj.WorldAabbMax.Y;
        Entry.WorldAabbMax[2]     = Obj.WorldAabbMax.Z;
        DynEntries.push_back(Entry);
        for (const auto& LV : Obj.Asset->LocalVoxels)
        {
            LocalSdfVoxels.push_back(LV.SignedDistanceM);
        }
    }
    if (DynEntries.empty()) DynEntries.push_back({});
    if (LocalSdfVoxels.empty()) LocalSdfVoxels.push_back(4.0f);

    std::vector<PointLightGpuStd430> PtEntries;
    for (const auto& P : Scene.PointLights)
    {
        PointLightGpuStd430 E {};
        E.PositionRange[0]  = P.PositionWorld.X;
        E.PositionRange[1]  = P.PositionWorld.Y;
        E.PositionRange[2]  = P.PositionWorld.Z;
        E.PositionRange[3]  = P.RangeMeters;
        E.ColorIntensity[0] = P.Color.R;
        E.ColorIntensity[1] = P.Color.G;
        E.ColorIntensity[2] = P.Color.B;
        E.ColorIntensity[3] = P.Intensity;
        E.RadiusPad[0]      = P.RadiusMeters;
        PtEntries.push_back(E);
    }
    if (PtEntries.empty()) PtEntries.push_back({});

    ClipmapUniformBlockStd140 Uni {};
    Uni.Clip0Dims[0] = C0.DimX; Uni.Clip0Dims[1] = C0.DimY; Uni.Clip0Dims[2] = C0.DimZ; Uni.Clip0Dims[3] = Total0;
    Uni.Clip1Dims[0] = C1.DimX; Uni.Clip1Dims[1] = C1.DimY; Uni.Clip1Dims[2] = C1.DimZ; Uni.Clip1Dims[3] = Total1;
    const Mathematics::Axis3D C0Center = (C0.BoundsMin + C0.BoundsMax) * 0.5f;
    const Mathematics::Axis3D C0Half   = (C0.BoundsMax - C0.BoundsMin) * 0.5f;
    const Mathematics::Axis3D C1Center = (C1.BoundsMin + C1.BoundsMax) * 0.5f;
    const Mathematics::Axis3D C1Half   = (C1.BoundsMax - C1.BoundsMin) * 0.5f;
    Uni.Clip0CenterSpacing[0] = C0Center.X; Uni.Clip0CenterSpacing[1] = C0Center.Y; Uni.Clip0CenterSpacing[2] = C0Center.Z; Uni.Clip0CenterSpacing[3] = C0.VoxelSpacingM;
    Uni.Clip0HalfExtents[0]   = C0Half.X;   Uni.Clip0HalfExtents[1]   = C0Half.Y;   Uni.Clip0HalfExtents[2]   = C0Half.Z;
    Uni.Clip1CenterSpacing[0] = C1Center.X; Uni.Clip1CenterSpacing[1] = C1Center.Y; Uni.Clip1CenterSpacing[2] = C1Center.Z; Uni.Clip1CenterSpacing[3] = C1.VoxelSpacingM;
    Uni.Clip1HalfExtents[0]   = C1Half.X;   Uni.Clip1HalfExtents[1]   = C1Half.Y;   Uni.Clip1HalfExtents[2]   = C1Half.Z;
    Uni.GridParams[0] = static_cast<uint32_t>(DynamicObjects.size());
    Uni.GridParams[1] = static_cast<uint32_t>(Scene.PointLights.size());
    Uni.GridParams[2] = WidthPx;
    Uni.GridParams[3] = HeightPx;
    Uni.SunDirTanRadius[0] = Scene.Sun.DirectionToSun.X;
    Uni.SunDirTanRadius[1] = Scene.Sun.DirectionToSun.Y;
    Uni.SunDirTanRadius[2] = Scene.Sun.DirectionToSun.Z;
    Uni.SunDirTanRadius[3] = std::tan(std::max(0.002f, Scene.Sun.SunAngularRadiusRadians));
    Uni.SunIrradiancePad[0] = Scene.Sun.SolarIrradiance.R;
    Uni.SunIrradiancePad[1] = Scene.Sun.SolarIrradiance.G;
    Uni.SunIrradiancePad[2] = Scene.Sun.SolarIrradiance.B;
    Uni.SkyRadiancePad[0] = Scene.SkyRadiance.R;
    Uni.SkyRadiancePad[1] = Scene.SkyRadiance.G;
    Uni.SkyRadiancePad[2] = Scene.SkyRadiance.B;
    Uni.DirtyMinVoxel[0] = 0; Uni.DirtyMinVoxel[1] = 0; Uni.DirtyMinVoxel[2] = 0;
    Uni.DirtyMaxVoxel[0] = static_cast<int32_t>(C0.DimX) - 1;
    Uni.DirtyMaxVoxel[1] = static_cast<int32_t>(C0.DimY) - 1;
    Uni.DirtyMaxVoxel[2] = static_cast<int32_t>(C0.DimZ) - 1;

    EnsureBufferCapacity(ClipmapUniformBuffer, sizeof(Uni), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    std::memcpy(ClipmapUniformBuffer.HostAddress, &Uni, sizeof(Uni));

    const auto StorageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    UploadToDeviceBuffer(ClipmapStaticDist0Buffer, StaticDist0.data(), StaticDist0.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapDist1Buffer, Dist1.data(), Dist1.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapActiveDist0Buffer, ActiveDist0.data(), ActiveDist0.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapAlbedoMet0Buffer, AlbMet0.data(), AlbMet0.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapNormalRough0Buffer, NrmRgh0.data(), NrmRgh0.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapEmissive0Buffer, Emis0.data(), Emis0.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapRadiosityRead0Buffer, Rad0.data(), Rad0.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapRadiosityWrite0Buffer, Rad0.data(), Rad0.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapRadiosityRead1Buffer, Rad1.data(), Rad1.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapDynObjectsBuffer, DynEntries.data(), DynEntries.size() * sizeof(DynamicLocalSdfGpuStd430), StorageFlags);
    UploadToDeviceBuffer(ClipmapLocalSdfVoxelsBuffer, LocalSdfVoxels.data(), LocalSdfVoxels.size() * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapPointLightsBuffer, PtEntries.data(), PtEntries.size() * sizeof(PointLightGpuStd430), StorageFlags);
    UploadToDeviceBuffer(ClipmapPrimaryPosBuffer, PrimaryPositionsXyzw.data(), PixelCount * 4ull * sizeof(float), StorageFlags);
    UploadToDeviceBuffer(ClipmapPrimaryNormBuffer, PrimaryNormalsXyzw.data(), PixelCount * 4ull * sizeof(float), StorageFlags);

    if (ClipmapConeGiOutputImage2D.ImageHandle == VK_NULL_HANDLE ||
        ClipmapConeGiOutputImage2D.WidthPx != WidthPx ||
        ClipmapConeGiOutputImage2D.HeightPx != HeightPx)
    {
        vkDeviceWaitIdle(BoundDevice->AccessDevice());
        BoundDevice->ReleaseImage2D(ClipmapConeGiOutputImage2D);
        ClipmapConeGiOutputImage2D = BoundDevice->AllocateImage2D(
            WidthPx,
            HeightPx,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }

    const uint64_t ReadbackBytes = static_cast<uint64_t>(PixelCount) * 4ull * sizeof(float);
    EnsureReadbackBufferCapacity(OutputReadbackStagingBuffer, ReadbackBytes);

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE) return false;

    VkImageMemoryBarrier ImgToGen {};
    ImgToGen.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ImgToGen.srcAccessMask       = 0u;
    ImgToGen.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    ImgToGen.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ImgToGen.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ImgToGen.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ImgToGen.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ImgToGen.image               = ClipmapConeGiOutputImage2D.ImageHandle;
    ImgToGen.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    vkCmdPipelineBarrier(
        Cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0u, 0u, nullptr, 0u, nullptr, 1u, &ImgToGen);

    auto MakeClipmapSlots = [&](bool PingPongSwap) -> std::array<ComputeSlotDescriptor, 16u>
    {
        const VkBuffer RadRead  = PingPongSwap ? ClipmapRadiosityWrite0Buffer.BufferHandle : ClipmapRadiosityRead0Buffer.BufferHandle;
        const VkBuffer RadWrite = PingPongSwap ? ClipmapRadiosityRead0Buffer.BufferHandle  : ClipmapRadiosityWrite0Buffer.BufferHandle;
        return {{
            { 0u,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ClipmapUniformBuffer.BufferHandle,        sizeof(Uni), 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 1u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapStaticDist0Buffer.BufferHandle,    VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 2u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapDist1Buffer.BufferHandle,          VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 3u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapActiveDist0Buffer.BufferHandle,    VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 4u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapAlbedoMet0Buffer.BufferHandle,     VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 5u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapNormalRough0Buffer.BufferHandle,   VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 6u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapEmissive0Buffer.BufferHandle,      VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 7u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RadRead,                                  VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 8u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RadWrite,                                 VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 9u,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapRadiosityRead1Buffer.BufferHandle, VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapDynObjectsBuffer.BufferHandle,     VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapLocalSdfVoxelsBuffer.BufferHandle, VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapPointLightsBuffer.BufferHandle,    VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapPrimaryPosBuffer.BufferHandle,     VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipmapPrimaryNormBuffer.BufferHandle,    VK_WHOLE_SIZE, 0ull, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
            { 15u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_NULL_HANDLE,                           0ull, 0ull, ClipmapConeGiOutputImage2D.ViewHandle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL },
        }};
    };

    const uint32_t VoxelGroups = (Total0 + 63u) / 64u;
    const auto SlotsA = MakeClipmapSlots(false);
    const auto SlotsB = MakeClipmapSlots(true);

    VkMemoryBarrier ComputeBarrier {};
    ComputeBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    // 1) Dynamic Local Object SDF Union into ActiveClip0
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ClipmapComposeProgramHandle, SlotsA, VoxelGroups, 1u, 1u))
    {
        return false;
    }
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &ComputeBarrier, 0u, nullptr, 0u, nullptr);

    // 2) Propagate Clipmap Multi-Bounce Surface Cache Radiosity (2 passes)
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ClipmapRadiosityProgramHandle, SlotsA, VoxelGroups, 1u, 1u))
    {
        return false;
    }
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &ComputeBarrier, 0u, nullptr, 0u, nullptr);
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ClipmapRadiosityProgramHandle, SlotsB, VoxelGroups, 1u, 1u))
    {
        return false;
    }
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &ComputeBarrier, 0u, nullptr, 0u, nullptr);

    // 3) Evaluate Deterministic 24-Cone Clipmap GI
    const uint32_t GroupsX = (WidthPx + 7u) / 8u;
    const uint32_t GroupsY = (HeightPx + 7u) / 8u;
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ClipmapConeGiProgramHandle, SlotsA, GroupsX, GroupsY, 1u))
    {
        return false;
    }

    VkImageMemoryBarrier ImgToSrc {};
    ImgToSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ImgToSrc.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    ImgToSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    ImgToSrc.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ImgToSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ImgToSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ImgToSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ImgToSrc.image               = ClipmapConeGiOutputImage2D.ImageHandle;
    ImgToSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
    vkCmdPipelineBarrier(
        Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u, 0u, nullptr, 0u, nullptr, 1u, &ImgToSrc);

    VkBufferImageCopy CopyRegion {};
    CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    CopyRegion.imageExtent      = { WidthPx, HeightPx, 1u };
    vkCmdCopyImageToBuffer(
        Cmd,
        ClipmapConeGiOutputImage2D.ImageHandle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        OutputReadbackStagingBuffer.BufferHandle,
        1u,
        &CopyRegion);

    const uint64_t SyncTick = BoundDevice->DispatchCommandBufferAsync(Cmd);
    if (!BoundDevice->AwaitTimelineTick(SyncTick, 120000000000ull))
    {
        return false;
    }

    OutConeGiOverPi.resize(PixelCount);
    const float* FloatPixels = static_cast<const float*>(OutputReadbackStagingBuffer.HostAddress);
    for (std::size_t P = 0u; P < PixelCount; ++P)
    {
        OutConeGiOverPi[P] = {
            std::max(0.0f, FloatPixels[P * 4u + 0u]),
            std::max(0.0f, FloatPixels[P * 4u + 1u]),
            std::max(0.0f, FloatPixels[P * 4u + 2u]),
        };
    }

    const auto T1 = std::chrono::steady_clock::now();
    OutGpuElapsedMs = static_cast<float>(std::chrono::duration<double, std::milli>(T1 - T0).count());
    return true;
}

bool VulkanWorldRenderSequence::ExecuteGpuDistanceEvaluation(
    const DistanceFieldSpace::FaceBvhRepository&          LocalAtlas,
    const DistanceFieldSpace::BakedFaceDistance&  Baked,
    const DistanceFieldSpace::DistanceClipmap&     Clipmap,
    const DistanceFieldSpace::RigidPose&           Pose,
    std::span<const DistanceFieldSpace::ClosedBox> Boxes,
    std::span<const std::array<float, 12>>         ProbeRays,
    std::vector<float>&                            OutGpuLocalDistances,
    std::vector<DistanceFieldSpace::ClipTexel>&    OutGpuClipTexels,
    std::vector<std::array<float, 4>>&             OutGpuProbeResults,
    float&                                         OutGpuElapsedMs)
{
    OutGpuElapsedMs = 0.0f;
    if (BoundDevice == nullptr || !BoundDevice->Ready() || !QueryGpuDistanceEvaluationReady() ||
        LocalAtlas.Surfaces.empty() || LocalAtlas.Packed.Enclosures.empty())
    {
        return false;
    }

    struct alignas(16) FacetFootprintStd430
    {
        std::array<float, 3> A {};
        uint32_t             Material = 0u;
        std::array<float, 4> B {};
        std::array<float, 4> C {};
    };
    struct alignas(16) BoxFootprintStd430
    {
        std::array<float, 4> Centre {};
        std::array<float, 4> HalfExtent {};
    };
    struct alignas(16) DistanceFootprintPush
    {
        std::array<float, 4>    LocalMinimum {};
        std::array<float, 4>    LocalSpacing {};
        std::array<uint32_t, 4> ExtentAndCount {};
        std::array<float, 4>    PoseCosineSine {};
        std::array<float, 4>    Translation {};
        std::array<int32_t, 4>  ClipOrigin0 {};
        std::array<int32_t, 4>  ClipOrigin1 {};
        std::array<int32_t, 4>  ClipOrigin2 {};
    };
    static_assert(sizeof(DistanceFootprintPush) == 128u);

    const auto T0 = std::chrono::steady_clock::now();

    const uint32_t LocalTexelCount = Baked.Extent[0] * Baked.Extent[1] * Baked.Extent[2];
    const uint32_t ClipLevelTexels = DistanceFieldSpace::DistanceClipmap::Resolution *
                                     DistanceFieldSpace::DistanceClipmap::Resolution *
                                     DistanceFieldSpace::DistanceClipmap::Resolution;
    const uint32_t TotalClipTexels = DistanceFieldSpace::DistanceClipmap::LevelCount * ClipLevelTexels;
    const uint32_t ProbeCount      = std::max<uint32_t>(1u, static_cast<uint32_t>(ProbeRays.size()));

    const auto& Packed = LocalAtlas.Packed;
    std::vector<FacetFootprintStd430> FacetsGpu(Packed.Corners.size());
    for (std::size_t Slot = 0u; Slot < Packed.Corners.size(); ++Slot)
    {
        const auto& PT = Packed.Corners[Slot];
        FacetsGpu[Slot].A        = {{ PT.A[0], PT.A[1], PT.A[2] }};
        FacetsGpu[Slot].Material = LocalAtlas.Surfaces[Packed.OriginalFacets[Slot]].Material;
        FacetsGpu[Slot].B        = PT.B;
        FacetsGpu[Slot].C        = PT.C;
    }
    std::vector<BoxFootprintStd430> BoxesGpu(std::max<std::size_t>(1u, Boxes.size()));
    for (std::size_t I = 0u; I < Boxes.size(); ++I)
    {
        BoxesGpu[I].Centre     = {{ Boxes[I].Centre.X, Boxes[I].Centre.Y, Boxes[I].Centre.Z, 0.0f }};
        BoxesGpu[I].HalfExtent = {{ Boxes[I].HalfExtent.X, Boxes[I].HalfExtent.Y, Boxes[I].HalfExtent.Z, 0.0f }};
    }
    std::vector<std::array<float, 12>> ProbesGpu(ProbeCount);
    for (std::size_t I = 0u; I < ProbeRays.size(); ++I) ProbesGpu[I] = ProbeRays[I];

    const uint64_t EnclBytes   = Packed.Enclosures.size() * sizeof(TriangleVisibilitySpace::FloatEnclosure);
    const uint64_t FacetBytes  = FacetsGpu.size() * sizeof(FacetFootprintStd430);
    const uint64_t LocalBytes  = static_cast<uint64_t>(LocalTexelCount) * sizeof(float);
    const uint64_t ClipBytes   = static_cast<uint64_t>(TotalClipTexels) * sizeof(DistanceFieldSpace::ClipTexel);
    const uint64_t ProbeBytes  = static_cast<uint64_t>(ProbeCount) * sizeof(std::array<float, 12>);
    const uint64_t ResultBytes = static_cast<uint64_t>(ProbeCount) * sizeof(std::array<float, 4>);
    const uint64_t BoxBytes    = BoxesGpu.size() * sizeof(BoxFootprintStd430);
    const uint64_t RadBytes    = std::max<uint64_t>(64ull, static_cast<uint64_t>(LocalAtlas.Surfaces.size()) * 2ull * 16ull);

    DeviceBufferResource EnclBuf  = DispatchEncoder.AllocateHostWritableBuffer(EnclBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    DeviceBufferResource FacetBuf = DispatchEncoder.AllocateHostWritableBuffer(FacetBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    DeviceBufferResource LocalBuf = DispatchEncoder.AllocateHostWritableBuffer(LocalBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    DeviceBufferResource ClipBuf  = DispatchEncoder.AllocateHostWritableBuffer(ClipBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    DeviceBufferResource ProbeBuf = DispatchEncoder.AllocateHostWritableBuffer(ProbeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    DeviceBufferResource ResBuf   = DispatchEncoder.AllocateHostWritableBuffer(ResultBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    DeviceBufferResource BoxBuf   = DispatchEncoder.AllocateHostWritableBuffer(BoxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    DeviceBufferResource RadBuf   = DispatchEncoder.AllocateHostWritableBuffer(RadBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    auto ReleaseAll = [&]()
    {
        BoundDevice->ReleaseBuffer(EnclBuf);
        BoundDevice->ReleaseBuffer(FacetBuf);
        BoundDevice->ReleaseBuffer(LocalBuf);
        BoundDevice->ReleaseBuffer(ClipBuf);
        BoundDevice->ReleaseBuffer(ProbeBuf);
        BoundDevice->ReleaseBuffer(ResBuf);
        BoundDevice->ReleaseBuffer(BoxBuf);
        BoundDevice->ReleaseBuffer(RadBuf);
    };

    if (!EnclBuf.HostAddress || !FacetBuf.HostAddress || !LocalBuf.HostAddress || !ClipBuf.HostAddress ||
        !ProbeBuf.HostAddress || !ResBuf.HostAddress || !BoxBuf.HostAddress || !RadBuf.HostAddress)
    {
        ReleaseAll();
        return false;
    }

    std::memcpy(EnclBuf.HostAddress, Packed.Enclosures.data(), EnclBytes);
    std::memcpy(FacetBuf.HostAddress, FacetsGpu.data(), FacetBytes);
    std::memset(LocalBuf.HostAddress, 0, LocalBytes);
    std::memset(ClipBuf.HostAddress, 0, ClipBytes);
    std::memcpy(ProbeBuf.HostAddress, ProbesGpu.data(), ProbeBytes);
    std::memset(ResBuf.HostAddress, 0, ResultBytes);
    std::memcpy(BoxBuf.HostAddress, BoxesGpu.data(), BoxBytes);
    std::memset(RadBuf.HostAddress, 0, RadBytes);

    const std::array<ComputeSlotDescriptor, 8u> Slots {{
        { 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, EnclBuf.BufferHandle,  EnclBytes },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FacetBuf.BufferHandle, FacetBytes },
        { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, LocalBuf.BufferHandle, LocalBytes },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ClipBuf.BufferHandle,  ClipBytes },
        { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ProbeBuf.BufferHandle, ProbeBytes },
        { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ResBuf.BufferHandle,   ResultBytes },
        { 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BoxBuf.BufferHandle,   BoxBytes },
        { 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RadBuf.BufferHandle,   RadBytes },
    }};

    DistanceFootprintPush Push {};
    Push.LocalMinimum   = {{ Baked.Low.X, Baked.Low.Y, Baked.Low.Z, static_cast<float>(Boxes.size()) }};
    Push.LocalSpacing   = {{ Baked.Spacing.X, Baked.Spacing.Y, Baked.Spacing.Z, 0.0f }};
    Push.ExtentAndCount = {{ Baked.Extent[0], Baked.Extent[1], Baked.Extent[2], static_cast<uint32_t>(ProbeRays.size()) }};
    Push.PoseCosineSine = {{ Pose.Cosine, Pose.Sine, static_cast<float>(LocalAtlas.Surfaces.size()), 0.0f }};
    Push.Translation    = {{ Pose.Translation.X, Pose.Translation.Y, Pose.Translation.Z, 0.0f }};
    Push.ClipOrigin0    = {{ Clipmap.Levels[0].Origin[0], Clipmap.Levels[0].Origin[1], Clipmap.Levels[0].Origin[2], 0 }};
    Push.ClipOrigin1    = {{ Clipmap.Levels[1].Origin[0], Clipmap.Levels[1].Origin[1], Clipmap.Levels[1].Origin[2], 0 }};
    Push.ClipOrigin2    = {{ Clipmap.Levels[2].Origin[0], Clipmap.Levels[2].Origin[1], Clipmap.Levels[2].Origin[2], 0 }};

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        ReleaseAll();
        return false;
    }

    VkMemoryBarrier HostToCompute {};
    HostToCompute.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostToCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    HostToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &HostToCompute, 0u, nullptr, 0u, nullptr);

    const auto DispatchStep = [&](uint32_t ProgramHandle, uint32_t ItemCount) -> bool
    {
        return DispatchEncoder.EncodeComputeDispatch(
            Cmd, ProgramHandle, Slots, (ItemCount + 63u) / 64u, 1u, 1u, &Push, sizeof(Push));
    };

    bool Ok = DispatchStep(DistEvalBakeProgramHandle, LocalTexelCount) &&
              DispatchStep(DistEvalFillClipmapProgramHandle, TotalClipTexels);
    if (Ok && !ProbeRays.empty())
    {
        Ok = DispatchStep(DistEvalProbesProgramHandle, static_cast<uint32_t>(ProbeRays.size()));
    }
    if (!Ok)
    {
        ReleaseAll();
        return false;
    }

    VkMemoryBarrier ComputeToHost {};
    ComputeToHost.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeToHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0u, 1u, &ComputeToHost, 0u, nullptr, 0u, nullptr);

    const uint64_t SyncTick = BoundDevice->DispatchCommandBufferAsync(Cmd);
    if (!BoundDevice->AwaitTimelineTick(SyncTick, 120000000000ull))
    {
        ReleaseAll();
        return false;
    }

    OutGpuLocalDistances.resize(LocalTexelCount);
    std::memcpy(OutGpuLocalDistances.data(), LocalBuf.HostAddress, LocalBytes);
    OutGpuClipTexels.resize(TotalClipTexels);
    std::memcpy(OutGpuClipTexels.data(), ClipBuf.HostAddress, ClipBytes);
    OutGpuProbeResults.resize(ProbeRays.size());
    if (!ProbeRays.empty())
    {
        std::memcpy(OutGpuProbeResults.data(), ResBuf.HostAddress, ResultBytes);
    }

    ReleaseAll();
    const auto T1 = std::chrono::steady_clock::now();
    OutGpuElapsedMs = static_cast<float>(std::chrono::duration<double, std::milli>(T1 - T0).count());
    return true;
}

} // namespace Frontier::GeometricRaster
