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
    float                        GlobalDistanceFineCenterSpan[4]   {};
    float                        GlobalDistanceCoarseCenterSpan[4] {};
    uint32_t                     GlobalDistanceBakeControl[4]      {};
};

static_assert(sizeof(WorldViewportUniformBlockStd140) == 2960u, "WorldViewportUniformBlockStd140 must equal 2960 bytes.");

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

    constexpr std::array<VkDescriptorType, 31u> SlotTypes {{
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
    ViewportPathTraceProgramHandle = UINT32_MAX; // Lazily registered on first Engine 4/5 Path Tracer dispatch

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

    EnsureBufferCapacity(UniformBuffer, 8192ull * 12ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalDistanceFieldBuffer, 2ull * 262144ull * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalRadiosityFieldBuffer, 299520ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureDeviceBufferCapacity(GlobalSurfaceShellBuffer, 262144ull * sizeof(uint32_t) * 2ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
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
        UploadedPhotometricProfiles   = 0u;
        UploadedPhotometricLamps      = 0u;
        UploadedLocalSdfInstanceCount = 0u;
        CachedLocalSdfAssetSignature  = 0ull;
        CachedLocalSdfOffsets.clear();
    }
    if (UniformBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        std::fprintf(
            stdout,
            "[FrontierWorld] VulkanWorldRenderSequence active on %.*s (Hardware GPU: %s, GPU ShadowCascade: %s)\n",
            static_cast<int>(TargetDevice.QueryAdapterName().size()),
            TargetDevice.QueryAdapterName().data(),
            TargetDevice.QueryHardwareAccelerated() ? "YES" : "NO (Software ICD)",
            ShadowCascadeProgramHandle != UINT32_MAX ? "YES" : "NO");
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
        BoundDevice->ReleaseImage2D(DisplayOutputImage2D);
    }
    DispatchEncoder.TeardownEncoder();
    RadiometricProgramHandle        = UINT32_MAX;
    CloudLayerProgramHandle         = UINT32_MAX;
    DenoiseProgramHandle            = UINT32_MAX;
    TransmittanceLutProgramHandle   = UINT32_MAX;
    MultiScatterLutProgramHandle    = UINT32_MAX;
    SkyViewLutProgramHandle         = UINT32_MAX;
    StellarClearProgramHandle       = UINT32_MAX;
    StellarCullProgramHandle        = UINT32_MAX;
    ShadowCascadeProgramHandle      = UINT32_MAX;
    GlobalDistanceBakeProgramHandle = UINT32_MAX;
    GlobalRadiosityProgramHandle    = UINT32_MAX;
    ViewportPathTraceProgramHandle  = UINT32_MAX;
    UploadedCatalogStarCount        = 0u;
    UploadedPhotometricProfiles     = 0u;
    UploadedPhotometricLamps        = 0u;
    UploadedLocalSdfInstanceCount   = 0u;
    CachedLocalSdfAssetSignature    = 0ull;
    CachedDynamicSdfSignature       = 0ull;
    ActiveClipFocusWorld            = { 0.0f, 0.0f, 0.0f };
    ActiveClipFocusInitialized      = false;
    DistanceFieldStats              = {};
    CachedLocalSdfOffsets.clear();
    FrameSequenceIndex              = 0u;
    BoundDevice                     = nullptr;
    SceneUploadedOnce               = false;
    GlobalDistanceFieldReady        = false;
    SkyViewLutValid                 = false;
    LastSkyViewUniformSnapshot.fill(0u);
    LastRadiosityLightingSignature  = 0ull;
    UploadedBvhCount                = 0u;
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

    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (Inst.LocalSdfVolume == nullptr || Inst.LocalSdfVolume->DistanceSamples.empty())
        {
            continue;
        }
        bool Known = false;
        for (const auto* Existing : UniqueVolumes)
        {
            if (Existing == Inst.LocalSdfVolume)
            {
                Known = true;
                break;
            }
        }
        if (!Known)
        {
            UniqueVolumes.push_back(Inst.LocalSdfVolume);
            MixAssetSig(reinterpret_cast<uintptr_t>(Inst.LocalSdfVolume));
            MixAssetSig(static_cast<uint64_t>(Inst.LocalSdfVolume->DistanceSamples.size()));
            MixAssetSig((static_cast<uint64_t>(Inst.LocalSdfVolume->ResolutionX) << 32u) |
                        (static_cast<uint64_t>(Inst.LocalSdfVolume->ResolutionY) << 16u) |
                         static_cast<uint64_t>(Inst.LocalSdfVolume->ResolutionZ));
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

    // 2. Upload compact per-instance dynamic rigid/scale transform array (< 0.02 ms even during gizmo drag)
    std::vector<GpuDynamicLocalSdfInstance144> GpuInstances;
    GpuInstances.reserve(std::max<std::size_t>(1u, Scene.ClusterInstances.size()));

    for (const RigidClusterInstance& Inst : Scene.ClusterInstances)
    {
        if (Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f)
        {
            continue;
        }
        const bool SignedVolumePresent = (Inst.LocalSdfVolume != nullptr && !Inst.LocalSdfVolume->DistanceSamples.empty());
        const uint32_t ShapeCode = SignedVolumePresent
            ? 0u
            : ((Inst.AnalyticalShapeCode > 0u) ? Inst.AnalyticalShapeCode : 1u);

        uint32_t VoxelOffset = 0u;
        if (SignedVolumePresent)
        {
            for (const auto& Pair : CachedLocalSdfOffsets)
            {
                if (Pair.first == Inst.LocalSdfVolume)
                {
                    VoxelOffset = Pair.second;
                    break;
                }
            }
        }

        const float ScaleU = std::max(1.0e-4f, Inst.UniformScale);
        const float ScaleX = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;
        const float MinWorldScale = std::min({ ScaleX, ScaleY, ScaleZ });
        const float InvScaleX = 1.0f / ScaleX;
        const float InvScaleY = 1.0f / ScaleY;
        const float InvScaleZ = 1.0f / ScaleZ;
        const float YawRad = Inst.RotationZDegrees * Constants::DegToRadFloat;
        const float CosZ   = std::cos(YawRad);
        const float SinZ   = std::sin(YawRad);
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
        Dst.LocalToWorldNormRow1[3] =  SignedVolumePresent ? Inst.LocalSdfVolume->VoxelSpacing.Z : 0.0f;

        Dst.LocalToWorldNormRow2[0] =  0.0f;
        Dst.LocalToWorldNormRow2[1] =  0.0f;
        Dst.LocalToWorldNormRow2[2] =  InvScaleZ;
        Dst.LocalToWorldNormRow2[3] =  0.0f;

        if (SignedVolumePresent)
        {
            const auto& Vol = *Inst.LocalSdfVolume;
            Dst.LocalMinPad[0] = Vol.LocalMin.X;
            Dst.LocalMinPad[1] = Vol.LocalMin.Y;
            Dst.LocalMinPad[2] = Vol.LocalMin.Z;
            Dst.LocalMinPad[3] = Vol.VoxelSpacing.X;
            Dst.LocalMaxPad[0] = Vol.LocalMax.X;
            Dst.LocalMaxPad[1] = Vol.LocalMax.Y;
            Dst.LocalMaxPad[2] = Vol.LocalMax.Z;
            Dst.LocalMaxPad[3] = Vol.VoxelSpacing.Y;
            Dst.ResolutionOffset[0] = (Vol.ResolutionX & 0xFFFFu) | ((Vol.ResolutionY & 0xFFFFu) << 16u);
            Dst.ResolutionOffset[1] = (Vol.ResolutionZ & 0xFFFFu); // ShapeCode = 0 (SignedVolume)
        }
        else
        {
            Mathematics::Axis3D HalfExt = Inst.LocalHalfExtents;
            if (HalfExt.X <= 1.0e-4f || HalfExt.Y <= 1.0e-4f || HalfExt.Z <= 1.0e-4f)
            {
                const float PyrHalf = ComputePyramidLocalHalfExtent(Inst.PyramidAsset);
                HalfExt = { PyrHalf, PyrHalf, PyrHalf };
            }
            Dst.LocalMinPad[0] = -HalfExt.X;
            Dst.LocalMinPad[1] = -HalfExt.Y;
            Dst.LocalMinPad[2] = -HalfExt.Z;
            Dst.LocalMinPad[3] =  0.0f;
            Dst.LocalMaxPad[0] =  HalfExt.X;
            Dst.LocalMaxPad[1] =  HalfExt.Y;
            Dst.LocalMaxPad[2] =  HalfExt.Z;
            Dst.LocalMaxPad[3] =  0.0f;
            Dst.ResolutionOffset[0] = 0u;
            Dst.ResolutionOffset[1] = (ShapeCode & 0xFFFFu) << 16u;
        }
        Dst.ResolutionOffset[2] = VoxelOffset;
        Dst.ResolutionOffset[3] = static_cast<uint32_t>(Inst.MaterialSlot) | (1u << 16u);
        GpuInstances.push_back(Dst);
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
    return ResourcesChanged;
}

Mathematics::Axis3D VulkanWorldRenderSequence::ResolveDynamicClipFocusWorld(
    const PerspectiveCameraBasis*  OptionalCamera,
    const WorldSceneSpecification& Scene) const noexcept
{
    Mathematics::Axis3D FallbackCenter = (Scene.SceneTree.RootBoundsMin + Scene.SceneTree.RootBoundsMax) * 0.5f;

    // Anchor to movable non-floor foreground assets when no live camera is supplied,
    // or blend camera view focus with foreground assets so moving either the camera OR the car
    // shifts the voxel-snapped SDF clipmap smoothly instead of leaving it pinned at (0, 0, 0).
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

    const Mathematics::Axis3D ToTarget = OptionalCamera->TargetPoint - OptionalCamera->CameraOrigin;
    const float TargetDist = std::sqrt(std::max(0.0f, Mathematics::EvaluateDot(ToTarget, ToTarget)));
    const Mathematics::Axis3D ViewDir = (TargetDist > 1.0e-4f)
        ? (ToTarget * (1.0f / TargetDist))
        : Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f };

    // Camera-anchored clipmap look-ahead clamped to scene bounds so flying/panning/dollying moves the Fine + Coarse GDF cascades
    const float LookAheadMeters = std::clamp(TargetDist, 2.2f, 11.5f);
    Mathematics::Axis3D CameraFocus = OptionalCamera->CameraOrigin + ViewDir * LookAheadMeters;

    // When a foreground hero asset sits inside camera reach, anchor strongly toward it while still shifting with camera pan
    if (ForegroundWeight > 0.0f)
    {
        const Mathematics::Axis3D Delta = FallbackCenter - CameraFocus;
        const float DistToForeground = std::sqrt(Mathematics::EvaluateDot(Delta, Delta));
        if (DistToForeground < 14.0f)
        {
            const float Blend = 0.72f * (1.0f - DistToForeground / 14.0f);
            CameraFocus = CameraFocus * (1.0f - Blend) + FallbackCenter * Blend;
        }
    }

    const Mathematics::Axis3D SceneMin = Scene.SceneTree.RootBoundsMin;
    const Mathematics::Axis3D SceneMax = Scene.SceneTree.RootBoundsMax;
    if (SceneMax.X > SceneMin.X + 0.5f)
    {
        CameraFocus.X = std::clamp(CameraFocus.X, SceneMin.X, SceneMax.X);
        CameraFocus.Y = std::clamp(CameraFocus.Y, SceneMin.Y, SceneMax.Y);
        CameraFocus.Z = std::clamp(CameraFocus.Z, SceneMin.Z - 1.0f, SceneMax.Z + 3.5f);
    }
    return CameraFocus;
}

bool VulkanWorldRenderSequence::DispatchGpuScenePrecompute(const WorldSceneSpecification& Scene, bool RebuildGeometryFields)
{
    if (!QueryReady() ||
        ShadowCascadeProgramHandle == UINT32_MAX ||
        UniformBuffer.HostAddress == nullptr)
    {
        return false;
    }

    if (RebuildGeometryFields && !Scene.ClusterInstances.empty())
    {
        (void)SynchronizeLocalSdfResources(Scene);
    }

    const bool AtmoActive = Scene.Atmosphere.Medium.AtmosphereEnabled || Scene.Atmosphere.Medium.OrbitalViewEnabled;
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
    const Mathematics::Axis3D SunDir = AtmoActive
        ? SolarSolved.WorldDirectionToSun
        : Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });
    LastSyncedSunDir = SunDir;

    const Mathematics::Axis3D FocusCenter = ActiveClipFocusInitialized
        ? ActiveClipFocusWorld
        : ResolveDynamicClipFocusWorld(nullptr, Scene);
    const Mathematics::Axis3D BoundsSpan  = Scene.SceneTree.RootBoundsMax - Scene.SceneTree.RootBoundsMin;
    const float MaxHalfExtent = 0.5f * std::max({ std::abs(BoundsSpan.X), std::abs(BoundsSpan.Y), std::abs(BoundsSpan.Z), 5.6f });
    const float FineGdfHalf   = std::clamp(MaxHalfExtent * 1.18f, 3.2f, 4.2f);
    const float CoarseGdfHalf = FineGdfHalf * 2.5f;
    const float FineVoxelM    = (2.0f * FineGdfHalf) / 64.0f;
    const float CoarseVoxelM  = (2.0f * CoarseGdfHalf) / 64.0f;
    CachedFineGdfCenterSpan[0]   = std::round(FocusCenter.X / FineVoxelM) * FineVoxelM;
    CachedFineGdfCenterSpan[1]   = std::round(FocusCenter.Y / FineVoxelM) * FineVoxelM;
    CachedFineGdfCenterSpan[2]   = std::round(FocusCenter.Z / FineVoxelM) * FineVoxelM;
    CachedFineGdfCenterSpan[3]   = FineGdfHalf;
    CachedCoarseGdfCenterSpan[0] = std::round(FocusCenter.X / CoarseVoxelM) * CoarseVoxelM;
    CachedCoarseGdfCenterSpan[1] = std::round(FocusCenter.Y / CoarseVoxelM) * CoarseVoxelM;
    CachedCoarseGdfCenterSpan[2] = std::round(FocusCenter.Z / CoarseVoxelM) * CoarseVoxelM;
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
    InitCascadeBasis(CachedFineSunCascade, 2.85f);
    InitCascadeBasis(CachedSunCascade, 8.5f);
    CoarseShadowOffsetTexels = 1024u * 1024u;

    const uint64_t ShadowDepthBytes = static_cast<uint64_t>(CoarseShadowOffsetTexels) * 2ull * sizeof(float);
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

    std::memcpy(Uniforms.GlobalDistanceFineCenterSpan, CachedFineGdfCenterSpan, sizeof(CachedFineGdfCenterSpan));
    std::memcpy(Uniforms.GlobalDistanceCoarseCenterSpan, CachedCoarseGdfCenterSpan, sizeof(CachedCoarseGdfCenterSpan));
    Uniforms.GlobalDistanceBakeControl[2] = 1u;
    Uniforms.GlobalDistanceBakeControl[3] = (UploadedLocalSdfInstanceCount & 0xFFFFu) << 16u;

    constexpr uint64_t UniformStrideBytes = 8192ull;
    EnsureBufferCapacity(UniformBuffer, UniformStrideBytes * 12ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uint8_t* UniformHostBytes = reinterpret_cast<uint8_t*>(UniformBuffer.HostAddress);
    std::memcpy(UniformHostBytes, &Uniforms, sizeof(Uniforms));

    auto ConstructPrecomputeSlots = [&](uint64_t UniOffs) -> std::array<ComputeSlotDescriptor, 31u>
    {
        return {{
            {  0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle,                   sizeof(WorldViewportUniformBlockStd140), UniOffs },
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
        }};
    };

    DispatchEncoder.ResetTransientDescriptors();
    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

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
        GlobalDistanceFieldReady = false;
    }
    else
    {
        const auto CascadeSlots = ConstructPrecomputeSlots(0ull);
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, ShadowCascadeProgramHandle, CascadeSlots, 128u, 128u, 2u))
        {
            BoundDevice->FreeCommandBuffer(Cmd);
            return false;
        }

        const bool DoBakeGdf =
            (RebuildGeometryFields || !GlobalDistanceFieldReady) &&
            (GlobalDistanceBakeProgramHandle != UINT32_MAX);
        if (DoBakeGdf)
        {
            for (uint32_t CascadeIdx = 0u; CascadeIdx < 2u; ++CascadeIdx)
            {
                const uint64_t UniOffs = static_cast<uint64_t>(1u + CascadeIdx) * UniformStrideBytes;
                WorldViewportUniformBlockStd140 GdfUni = Uniforms;
                GdfUni.GlobalDistanceBakeControl[0] = 0u;
                GdfUni.GlobalDistanceBakeControl[1] = CascadeIdx;
                GdfUni.GlobalDistanceBakeControl[2] = 1u;
                std::memcpy(UniformHostBytes + UniOffs, &GdfUni, sizeof(GdfUni));

                const auto GdfSlots = ConstructPrecomputeSlots(UniOffs);
                if (!DispatchEncoder.EncodeComputeDispatch(Cmd, GlobalDistanceBakeProgramHandle, GdfSlots, 16u, 16u, 16u))
                {
                    BoundDevice->FreeCommandBuffer(Cmd);
                    return false;
                }
            }
            DistanceFieldStats.CascadeBakeCount += 1u;
        }
        InsertPrecomputeBarrier();

        if (GlobalRadiosityProgramHandle != UINT32_MAX)
        {
            constexpr std::array<uint32_t, 8u> PhaseGroupCounts {{ 16u, 8u, 4u, 2u, 16u, 8u, 4u, 2u }};
            for (uint32_t Phase = 0u; Phase < 8u; ++Phase)
            {
                const uint64_t UniOffs = static_cast<uint64_t>(3u + Phase) * UniformStrideBytes;
                WorldViewportUniformBlockStd140 RadUni = Uniforms;
                RadUni.GlobalDistanceBakeControl[0] = Phase;
                RadUni.GlobalDistanceBakeControl[1] = 0u;
                RadUni.GlobalDistanceBakeControl[2] = 1u;
                std::memcpy(UniformHostBytes + UniOffs, &RadUni, sizeof(RadUni));

                const uint32_t Groups   = PhaseGroupCounts[Phase];
                const auto     RadSlots = ConstructPrecomputeSlots(UniOffs);
                if (!DispatchEncoder.EncodeComputeDispatch(Cmd, GlobalRadiosityProgramHandle, RadSlots, Groups, Groups, Groups))
                {
                    BoundDevice->FreeCommandBuffer(Cmd);
                    return false;
                }
                InsertPrecomputeBarrier();
            }
            GlobalDistanceFieldReady = true;
        }
        else if (DoBakeGdf)
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

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    const bool Ok = BoundDevice->AwaitTimelineTick(SyncPoint, 60000000000ull);
    BoundDevice->FreeCommandBuffer(Cmd);
    return Ok;
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
                OutTri.Corner0Pad[3]  = 0.0f; // full-res: LOD level 0
                OutTri.Edge01Pad[3]   = static_cast<float>(Idx / 128u); // full-res pseudo-cluster (128-tri groups)
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
                OutTri.Corner0Pad[3]  = 0.0f; // full-res: LOD level 0
                OutTri.Edge01Pad[3]   = static_cast<float>(Idx / 128u); // full-res pseudo-cluster (128-tri groups)
                ShadedTris[Idx]       = OutTri;
            }
        }
    }
    const uint64_t TriBytes = ShadedTris.size() * sizeof(GpuShadedTriangle96);
    UploadToDeviceBuffer(ShadedTriangleBuffer, ShadedTris.data(), TriBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

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
        UploadSolarCascadeDepth(Scene);
    }
    else if (GlobalDistanceFieldReady)
    {
        (void)DispatchGpuScenePrecompute(Scene, false);
    }
    LastRadiosityLightingSignature = 0ull;

    CachedNaniteCutSignature = 0u;
    NaniteLodBuffersActive   = false;
    SceneUploadedOnce      = true;
    ProgressiveSampleCount = 0u;
    const auto SyncEnd = std::chrono::steady_clock::now();
    LastSceneSyncMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(SyncEnd - SyncStart).count());
}

// Nanite-like viewport LOD: the GPU BVH is rebuilt from the per-camera monotonic cluster cut
// (the same SelectMonotonicCut the CPU renderer uses) whenever the active cluster set changes.
// Per-face material slots ride on every pyramid triangle, so materials survive at every level.
// Glass instances stay out of the BVH (analytic glass spheres shade them, as in the full-res path).
void VulkanWorldRenderSequence::RefreshNaniteLodGeometry(
    const WorldRenderConfiguration& Config,
    const PerspectiveCameraBasis&   Camera,
    const WorldSceneSpecification&  Scene)
{
    NaniteLodStats.FullTriangleCount = static_cast<uint32_t>(Scene.SceneTree.Primitives.size());
    if (!Config.NaniteLodEnabled || Scene.ClusterInstances.empty())
    {
        if (NaniteLodBuffersActive)
        {
            SynchronizeSceneResources(Scene, true);
        }
        NaniteLodStats.Enabled             = false;
        NaniteLodStats.ActiveTriangleCount = NaniteLodStats.FullTriangleCount;
        return;
    }

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    const float PixelsPerRadian = static_cast<float>(std::max(1u, Basis.HeightPx)) /
        std::max(1.0e-5f, 2.0f * Basis.TanHalfFovY);
    const float PixelError = std::clamp(Config.NanitePixelError, 0.10f, 16.0f);

    struct InstanceCut
    {
        uint32_t InstanceIndex = 0u;
        SpatialAcceleration::ClusterCutSelection Selection {};
    };
    std::vector<InstanceCut> Cuts;
    Cuts.reserve(Scene.ClusterInstances.size());

    uint64_t Signature = 1469598103934665603ull;
    const auto MixSignature = [&Signature](uint64_t Value) noexcept
    {
        Signature = (Signature ^ Value) * 1099511628211ull;
    };
    MixSignature(static_cast<uint64_t>(std::lround(PixelError * 100.0f)));

    uint32_t ActiveClusters  = 0u;
    uint32_t ActiveTriangles = 0u;
    for (uint32_t InstIdx = 0u; InstIdx < static_cast<uint32_t>(Scene.ClusterInstances.size()); ++InstIdx)
    {
        const RigidClusterInstance& Inst = Scene.ClusterInstances[InstIdx];
        if (Inst.TransparentGlass || Inst.UniformScale <= 1.0e-4f)
        {
            continue;
        }
        if (Inst.PyramidAsset == nullptr || Inst.PyramidAsset->Clusters.empty())
        {
            // An instance without a baked pyramid has no LOD representation; keep full-res BVH.
            if (NaniteLodBuffersActive)
            {
                SynchronizeSceneResources(Scene, true);
            }
            NaniteLodStats.Enabled             = false;
            NaniteLodStats.ActiveTriangleCount = NaniteLodStats.FullTriangleCount;
            return;
        }
        const float ScaleU = std::max(1.0e-4f, Inst.UniformScale);
        const float ScaleX = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;
        const float YawRad     = Inst.RotationZDegrees * Constants::DegToRadFloat;
        const bool  YawRotated = std::fabs(YawRad) > 1.0e-5f;
        const float CosZ       = YawRotated ? std::cos(YawRad) : 1.0f;
        const float SinZ       = YawRotated ? std::sin(YawRad) : 0.0f;
        const Mathematics::Axis3D RelWorld = Camera.CameraOrigin - Inst.WorldTranslation;
        const Mathematics::Axis3D Unrotated = YawRotated
            ? Mathematics::Axis3D{ RelWorld.X * CosZ + RelWorld.Y * SinZ, -RelWorld.X * SinZ + RelWorld.Y * CosZ, RelWorld.Z }
            : RelWorld;
        const Mathematics::Axis3D LocalCamera { Unrotated.X / ScaleX, Unrotated.Y / ScaleY, Unrotated.Z / ScaleZ };
        InstanceCut Cut {};
        Cut.InstanceIndex = InstIdx;
        Cut.Selection     = Inst.PyramidAsset->SelectMonotonicCut(LocalCamera, PixelsPerRadian, PixelError, false);
        ActiveClusters += static_cast<uint32_t>(Cut.Selection.ActiveClusterIndices.size());
        ActiveTriangles += Cut.Selection.ActiveTriangleCount;
        MixSignature(InstIdx + 1u);
        MixSignature(static_cast<uint64_t>(std::lround(Inst.RotationZDegrees * 100.0f)));
        MixSignature(static_cast<uint64_t>(std::lround(ScaleX * 1000.0f)));
        MixSignature(static_cast<uint64_t>(std::lround(ScaleY * 1000.0f)));
        MixSignature(static_cast<uint64_t>(std::lround(ScaleZ * 1000.0f)));
        MixSignature(static_cast<uint64_t>(std::lround(Inst.WorldTranslation.X * 1000.0f)));
        MixSignature(static_cast<uint64_t>(std::lround(Inst.WorldTranslation.Y * 1000.0f)));
        MixSignature(static_cast<uint64_t>(std::lround(Inst.WorldTranslation.Z * 1000.0f)));
        for (const uint32_t ClusterIdx : Cut.Selection.ActiveClusterIndices)
        {
            MixSignature(ClusterIdx + 1u);
        }
        Cuts.push_back(std::move(Cut));
    }

    NaniteLodStats.ActiveClusterCount  = ActiveClusters;
    NaniteLodStats.ActiveTriangleCount = ActiveTriangles;
    if (Signature == CachedNaniteCutSignature && NaniteLodBuffersActive)
    {
        NaniteLodStats.Enabled = true;
        return;
    }

    // Emit world-space triangles for the active cut only.
    std::vector<SpatialAcceleration::RayTrianglePrimitive>  LodPrims;
    std::vector<SpatialAcceleration::TriangleSmoothNormals> LodNormals;
    std::vector<uint32_t> LodPrimLevel;   // per PrimitiveOrdinal: hierarchy level (debug channel 17)
    std::vector<uint32_t> LodPrimCluster; // per PrimitiveOrdinal: global cluster ordinal (debug channel 18)
    LodPrims.reserve(std::max(1u, ActiveTriangles));
    LodNormals.reserve(std::max(1u, ActiveTriangles));
    LodPrimLevel.reserve(std::max(1u, ActiveTriangles));
    LodPrimCluster.reserve(std::max(1u, ActiveTriangles));
    uint32_t LevelTriangles[8] {};
    uint32_t LevelClusters[8] {};
    uint32_t PrimOrdinal = 0u;
    uint32_t GlobalClusterOrdinal = 0u;
    for (const InstanceCut& Cut : Cuts)
    {
        const RigidClusterInstance& Inst = Scene.ClusterInstances[Cut.InstanceIndex];
        const auto* Pyramid = Inst.PyramidAsset;
        const float ScaleU = std::max(1.0e-4f, Inst.UniformScale);
        const float ScaleX = (Inst.NonUniformScale.X > 1.0e-4f) ? Inst.NonUniformScale.X : ScaleU;
        const float ScaleY = (Inst.NonUniformScale.Y > 1.0e-4f) ? Inst.NonUniformScale.Y : ScaleU;
        const float ScaleZ = (Inst.NonUniformScale.Z > 1.0e-4f) ? Inst.NonUniformScale.Z : ScaleU;
        const float InvScaleX = 1.0f / ScaleX;
        const float InvScaleY = 1.0f / ScaleY;
        const float InvScaleZ = 1.0f / ScaleZ;
        const bool  NonUni = (std::fabs(ScaleX - ScaleY) > 1.0e-4f) || (std::fabs(ScaleX - ScaleZ) > 1.0e-4f);
        const float YawRad     = Inst.RotationZDegrees * Constants::DegToRadFloat;
        const bool  YawRotated = std::fabs(YawRad) > 1.0e-5f;
        const float CosZ       = YawRotated ? std::cos(YawRad) : 1.0f;
        const float SinZ       = YawRotated ? std::sin(YawRad) : 0.0f;
        const auto  RotZ   = [CosZ, SinZ](Mathematics::Axis3D V) noexcept -> Mathematics::Axis3D
        {
            return { V.X * CosZ - V.Y * SinZ, V.X * SinZ + V.Y * CosZ, V.Z };
        };
        const auto ScaleVec = [ScaleX, ScaleY, ScaleZ](Mathematics::Axis3D V) noexcept -> Mathematics::Axis3D
        {
            return { V.X * ScaleX, V.Y * ScaleY, V.Z * ScaleZ };
        };
        const auto XformNormal = [NonUni, YawRotated, InvScaleX, InvScaleY, InvScaleZ, &RotZ](Mathematics::Axis3D N) noexcept -> Mathematics::Axis3D
        {
            Mathematics::Axis3D Sn = NonUni
                ? Mathematics::NormalizeAxis({ N.X * InvScaleX, N.Y * InvScaleY, N.Z * InvScaleZ }, { 0.0f, 0.0f, 1.0f })
                : N;
            return YawRotated ? RotZ(Sn) : Sn;
        };
        for (const uint32_t ClusterIdx : Cut.Selection.ActiveClusterIndices)
        {
            const auto& Cluster = Pyramid->Clusters[ClusterIdx];
            const uint32_t Level = std::min(7u, static_cast<uint32_t>(Cluster.HierarchyLevel));
            LevelClusters[Level] += 1u;
            LevelTriangles[Level] += Cluster.TriangleCount;
            const uint32_t ThisClusterOrdinal = GlobalClusterOrdinal++;
            for (uint32_t TriOff = 0u; TriOff < Cluster.TriangleCount; ++TriOff)
            {
                const auto& Tri = Pyramid->Triangles[Cluster.TriangleStartOffset + TriOff];
                const Mathematics::Axis3D L0 = ScaleVec(Pyramid->Positions[Tri.VertexIndex0]);
                const Mathematics::Axis3D L1 = ScaleVec(Pyramid->Positions[Tri.VertexIndex1]);
                const Mathematics::Axis3D L2 = ScaleVec(Pyramid->Positions[Tri.VertexIndex2]);
                const Mathematics::Axis3D W0 = Inst.WorldTranslation + (YawRotated ? RotZ(L0) : L0);
                const Mathematics::Axis3D W1 = Inst.WorldTranslation + (YawRotated ? RotZ(L1) : L1);
                const Mathematics::Axis3D W2 = Inst.WorldTranslation + (YawRotated ? RotZ(L2) : L2);
                SpatialAcceleration::RayTrianglePrimitive Prim {};
                Prim.Corner0          = W0;
                Prim.Edge01           = W1 - W0;
                Prim.Edge02           = W2 - W0;
                Prim.SourceFaceIndex  = Tri.SourceFaceIndex;
                Prim.MaterialOrdinal  = Tri.MaterialOrdinal; // per-face material slot, preserved by the bake
                Prim.OpaqueMask       = 1u;
                Prim.PrimitiveOrdinal = PrimOrdinal++;
                LodPrimLevel.push_back(Level);
                LodPrimCluster.push_back(ThisClusterOrdinal);
                LodPrims.push_back(Prim);
                SpatialAcceleration::TriangleSmoothNormals Smooth {};
                const Mathematics::Axis3D N0 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal0);
                const Mathematics::Axis3D N1 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal1);
                const Mathematics::Axis3D N2 = GeometrySpace::UnpackOctahedralDirection32(Tri.PackedNormal2);
                Smooth.Normal0 = XformNormal(N0);
                Smooth.Normal1 = XformNormal(N1);
                Smooth.Normal2 = XformNormal(N2);
                LodNormals.push_back(Smooth);
            }
        }
    }
    if (LodPrims.empty())
    {
        NaniteLodStats.Enabled = false;
        return;
    }

    SpatialAcceleration::WideBoundingTree LodTree {};
    if (!LodTree.BuildFromTrianglePrimitivesWithNormals(LodPrims, LodNormals) || LodTree.WideEntries.empty())
    {
        NaniteLodStats.Enabled = false;
        return;
    }

    const uint64_t BvhBytes = LodTree.WideEntries.size() * sizeof(SpatialAcceleration::WideTreeEntry);
    if (!UploadToDeviceBuffer(BvhEntryBuffer, LodTree.WideEntries.data(), BvhBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
    {
        NaniteLodStats.Enabled = false;
        return;
    }
    UploadedBvhCount = static_cast<uint32_t>(LodTree.WideEntries.size());

    const bool HasNormals = (LodTree.PrimitiveNormals.size() == LodTree.Primitives.size());
    std::vector<GpuShadedTriangle96> ShadedTris(LodTree.Primitives.size());
    for (std::size_t Idx = 0u; Idx < LodTree.Primitives.size(); ++Idx)
    {
        const auto& Prim = LodTree.Primitives[Idx];
        Mathematics::Axis3D N0 = HasNormals ? LodTree.PrimitiveNormals[Idx].Normal0 : Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02), { 0.0f, 0.0f, 1.0f });
        Mathematics::Axis3D N1 = HasNormals ? LodTree.PrimitiveNormals[Idx].Normal1 : N0;
        Mathematics::Axis3D N2 = HasNormals ? LodTree.PrimitiveNormals[Idx].Normal2 : N0;
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
        OutTri.Normal0Pad[0]  = N0.X;
        OutTri.Normal0Pad[1]  = N0.Y;
        OutTri.Normal0Pad[2]  = N0.Z;
        OutTri.Normal1Pad[0]  = N1.X;
        OutTri.Normal1Pad[1]  = N1.Y;
        OutTri.Normal1Pad[2]  = N1.Z;
        OutTri.Normal2Slot[0] = N2.X;
        OutTri.Normal2Slot[1] = N2.Y;
        OutTri.Normal2Slot[2] = N2.Z;
        OutTri.Normal2Slot[3] = static_cast<float>(Prim.MaterialOrdinal);
        // Nanite debug packing in spare pad lanes: level in Corner0Pad.w, cluster in Edge01Pad.w
        const uint32_t Ord = Prim.PrimitiveOrdinal;
        OutTri.Corner0Pad[3] = (Ord < LodPrimLevel.size()) ? static_cast<float>(LodPrimLevel[Ord]) : 0.0f;
        OutTri.Edge01Pad[3]  = (Ord < LodPrimCluster.size()) ? static_cast<float>(LodPrimCluster[Ord]) : 0.0f;
        ShadedTris[Idx]       = OutTri;
    }
    const uint64_t TriBytes = ShadedTris.size() * sizeof(GpuShadedTriangle96);
    if (!UploadToDeviceBuffer(ShadedTriangleBuffer, ShadedTris.data(), TriBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
    {
        NaniteLodStats.Enabled = false;
        return;
    }

    const uint32_t PreviousRebuilds = NaniteLodStats.GeometryRebuildCount;
    NaniteLodStats = {};
    NaniteLodStats.Enabled              = true;
    NaniteLodStats.ActiveClusterCount   = ActiveClusters;
    NaniteLodStats.ActiveTriangleCount  = static_cast<uint32_t>(LodPrims.size());
    NaniteLodStats.FullTriangleCount    = static_cast<uint32_t>(Scene.SceneTree.Primitives.size());
    NaniteLodStats.GeometryRebuildCount = PreviousRebuilds + 1u;
    for (uint32_t Level = 0u; Level < 8u; ++Level)
    {
        NaniteLodStats.LevelTriangleCount[Level] = LevelTriangles[Level];
        NaniteLodStats.LevelClusterCount[Level]  = LevelClusters[Level];
    }
    CachedNaniteCutSignature = Signature;
    NaniteLodBuffersActive   = true;
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

    if (!SceneUploadedOnce)
    {
        SynchronizeSceneResources(Scene, true);
    }
    RefreshNaniteLodGeometry(Config, Camera, Scene);

    const auto StartClock = std::chrono::steady_clock::now();
    LastViewportTiming = {};

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
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
    const Mathematics::Axis3D SunDir = AtmoActive
        ? SolarSolved.WorldDirectionToSun
        : Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });

    const bool DynamicInstancesChanged =
        !Scene.ClusterInstances.empty() && SynchronizeLocalSdfResources(Scene);
    const Mathematics::Axis3D DesiredClipFocus = ResolveDynamicClipFocusWorld(&Camera, Scene);
    const float CurrentFineHalf   = std::max(3.2f, CachedFineGdfCenterSpan[3]);
    const float FineVoxelStep     = (2.0f * CurrentFineHalf) / 64.0f;
    const Mathematics::Axis3D FocusDelta {
        DesiredClipFocus.X - ActiveClipFocusWorld.X,
        DesiredClipFocus.Y - ActiveClipFocusWorld.Y,
        DesiredClipFocus.Z - ActiveClipFocusWorld.Z,
    };
    const float FocusShiftSq = Mathematics::EvaluateDot(FocusDelta, FocusDelta);
    const float ScrollThreshold = FineVoxelStep * 2.5f;
    const bool ClipFocusScrolled =
        !ActiveClipFocusInitialized || (FocusShiftSq > ScrollThreshold * ScrollThreshold);
    const bool SunDirectionShifted = Mathematics::EvaluateDot(SunDir, LastSyncedSunDir) < 0.99995f;

    if (ShadowCascadeProgramHandle != UINT32_MAX &&
        (DynamicInstancesChanged || ClipFocusScrolled || SunDirectionShifted))
    {
        ActiveClipFocusWorld       = DesiredClipFocus;
        ActiveClipFocusInitialized = true;
        const auto PreSyncStart = std::chrono::steady_clock::now();
        (void)DispatchGpuScenePrecompute(Scene, true);
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

    Uniforms.FineCascadeCenterSpan[0] = CachedFineSunCascade.CenterWorld.X;
    Uniforms.FineCascadeCenterSpan[1] = CachedFineSunCascade.CenterWorld.Y;
    Uniforms.FineCascadeCenterSpan[2] = CachedFineSunCascade.CenterWorld.Z;
    Uniforms.FineCascadeCenterSpan[3] = CachedFineSunCascade.HalfSpanMeters;

    Uniforms.FineCascadeParams[0] = static_cast<float>(CachedFineSunCascade.ResolutionTexels);
    Uniforms.FineCascadeParams[1] = CachedFineSunCascade.DepthExtentMeters;
    Uniforms.FineCascadeParams[2] = static_cast<float>(CoarseShadowOffsetTexels);

    Uniforms.AtmosphereMaskPad[0] = AtmoActive ? 1u : 0u;
    Uniforms.AtmosphereMaskPad[1] = AtmosphericSkySequence::SkyViewWidthPx;
    Uniforms.AtmosphereMaskPad[2] = AtmosphericSkySequence::SkyViewHeightPx;
    Uniforms.AtmosphereMaskPad[3] =
        (Config.ReflectionsEnabled ? 0u : 2u) |
        (Config.RaytracingEnabled  ? 0u : 8u) |
        ((static_cast<uint32_t>(Config.SdfPenumbraMode) & 0x7u) << 16u) |
        ((static_cast<uint32_t>(Config.GiCandidate) & 0x3u) << 20u);

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
    }

    constexpr uint64_t UniformStrideBytes = 8192ull;
    EnsureBufferCapacity(UniformBuffer, UniformStrideBytes * 12ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

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
            (void)DispatchGpuScenePrecompute(Scene, false);
            const auto RadSyncEnd = std::chrono::steady_clock::now();
            LastViewportTiming.SceneSyncGpuMs += static_cast<float>(
                std::chrono::duration<double, std::milli>(RadSyncEnd - RadSyncStart).count());
        }
        LastRadiosityLightingSignature = LightingSig;
    }

    std::memcpy(Uniforms.GlobalDistanceFineCenterSpan, CachedFineGdfCenterSpan, sizeof(CachedFineGdfCenterSpan));
    std::memcpy(Uniforms.GlobalDistanceCoarseCenterSpan, CachedCoarseGdfCenterSpan, sizeof(CachedCoarseGdfCenterSpan));
    Uniforms.GlobalDistanceBakeControl[0] = 0u;
    Uniforms.GlobalDistanceBakeControl[1] = 0u;
    Uniforms.GlobalDistanceBakeControl[2] = GlobalDistanceFieldReady ? 1u : 0u;
    // w: bit0 = XeGTAO enabled, bits 8..15 = XeGTAO strength x255/2 (0..2 range), bits 16..31 = LocalSdfInstanceCount
    Uniforms.GlobalDistanceBakeControl[3] =
        (Config.XeGtaoEnabled ? 1u : 0u) |
        (static_cast<uint32_t>(std::clamp(Config.XeGtaoStrength, 0.0f, 2.0f) * 127.5f) << 8u) |
        ((UploadedLocalSdfInstanceCount & 0xFFFFu) << 16u);

    uint8_t* UniformBase = reinterpret_cast<uint8_t*>(UniformBuffer.HostAddress);
    std::memcpy(UniformBase, &Uniforms, sizeof(Uniforms));

    const auto UniformDoneClock = std::chrono::steady_clock::now();
    const float CpuUniformMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(UniformDoneClock - StartClock).count());

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

    auto MakePassSlots = [&](uint64_t UniOffset, bool SwapPingPong) -> std::array<ComputeSlotDescriptor, 31u>
    {
        const auto& InShadow  = SwapPingPong ? PongShadowAoBuffer : PingShadowAoBuffer;
        const auto& OutShadow = SwapPingPong ? PingShadowAoBuffer : PongShadowAoBuffer;
        const auto& InIndir   = SwapPingPong ? PongIndirectIrradianceBuffer : PingIndirectIrradianceBuffer;
        const auto& OutIndir  = SwapPingPong ? PingIndirectIrradianceBuffer : PongIndirectIrradianceBuffer;
        return {{
            {  0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle,                   sizeof(WorldViewportUniformBlockStd140), UniOffset },
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
    const auto     Pass0Slots   = MakePassSlots(0ull, false);

    WriteStamp(2u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (AtmoActive && Scene.Atmosphere.Clouds.CloudsEnabled && Config.DebugChannel == 0u)
    {
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, CloudLayerProgramHandle, Pass0Slots, CloudGroupsX, CloudGroupsY, 1u))
        {
            BoundDevice->FreeCommandBuffer(Cmd);
            return false;
        }
        InsertComputeSyncBarrier();
    }
    WriteStamp(3u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    if ((EngIdx == 4u || EngIdx == 5u) && Config.DebugChannel == 0u && ViewportPathTraceProgramHandle == UINT32_MAX)
    {
        std::array<VkDescriptorType, 31u> LazyPtSlots {};
        LazyPtSlots.fill(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        LazyPtSlots[0] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ViewportPathTraceProgramHandle = DispatchEncoder.RegisterProgram(
            CachedCompiledShaderDir / "WorldViewportPathTraceCompute.comp.spv",
            LazyPtSlots,
            "ViewportPathTraceEntry");
    }

    const bool UseDedicatedPathTracer =
        (EngIdx == 4u || EngIdx == 5u) &&
        (Config.DebugChannel == 0u) &&
        (ViewportPathTraceProgramHandle != UINT32_MAX);

    if (UseDedicatedPathTracer)
    {
        WriteStamp(4u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        // Accumulate the whole per-frame batch: each sample re-dispatches with its own sample
        // index in a spare uniform slot (running-mean accumulation in the shader does the rest).
        for (uint32_t BatchIdx = 0u; BatchIdx < ProgressiveBatchSpp; ++BatchIdx)
        {
            auto SampleSlots = Pass0Slots;
            if (BatchIdx > 0u)
            {
                const uint64_t SampleUniOffs = static_cast<uint64_t>(BatchIdx) * UniformStrideBytes;
                WorldViewportUniformBlockStd140 SampleUni = Uniforms;
                SampleUni.EngineShadowReflPad[3] = EffectiveTickOrSample + BatchIdx;
                std::memcpy(UniformBase + SampleUniOffs, &SampleUni, sizeof(SampleUni));
                SampleSlots = MakePassSlots(SampleUniOffs, false);
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
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, RadiometricProgramHandle, Pass0Slots, GroupsX, GroupsY, 1u))
        {
            BoundDevice->FreeCommandBuffer(Cmd);
            return false;
        }
        InsertComputeSyncBarrier();
        WriteStamp(5u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        const bool ShadingDebug =
            (Config.DebugChannel == 0u || (Config.DebugChannel >= 8u && Config.DebugChannel <= 11u) ||
             Config.DebugChannel == 15u);
        const bool MultiPassSpatialDenoise =
            ShadingDebug &&
            ((EngIdx == 3u) || ((EngIdx == 1u || EngIdx == 2u) && PenumbraOrdinal >= 4u));
        const bool SinglePassShadowFilter =
            ShadingDebug &&
            !MultiPassSpatialDenoise &&
            (EngIdx == 1u || EngIdx == 2u);

        WriteStamp(6u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (MultiPassSpatialDenoise)
        {
            constexpr std::array<uint32_t, 3u> Strides {{ 1u, 2u, 4u }};
            for (uint32_t Pass = 0u; Pass < 3u; ++Pass)
            {
                const uint64_t UniOffs = static_cast<uint64_t>(Pass + 1u) * UniformStrideBytes;
                WorldViewportUniformBlockStd140 PassUni = Uniforms;
                PassUni.DenoiseStepParams[0] = Strides[Pass];
                PassUni.DenoiseStepParams[1] = 2u | ((Pass + 1u == 3u) ? 1u : 0u);
                std::memcpy(UniformBase + UniOffs, &PassUni, sizeof(PassUni));

                const auto PassSlots = MakePassSlots(UniOffs, (Pass & 1u) != 0u);
                if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DenoiseProgramHandle, PassSlots, GroupsX, GroupsY, 1u))
                {
                    BoundDevice->FreeCommandBuffer(Cmd);
                    return false;
                }
                if (Pass + 1u < 3u)
                {
                    InsertComputeSyncBarrier();
                }
            }
        }
        else
        {
            const uint64_t UniOffs = UniformStrideBytes;
            WorldViewportUniformBlockStd140 PassUni = Uniforms;
            PassUni.DenoiseStepParams[0] = 1u;
            PassUni.DenoiseStepParams[1] = SinglePassShadowFilter ? 3u : 1u;
            std::memcpy(UniformBase + UniOffs, &PassUni, sizeof(PassUni));

            const auto PassSlots = MakePassSlots(UniOffs, false);
            if (!DispatchEncoder.EncodeComputeDispatch(Cmd, DenoiseProgramHandle, PassSlots, GroupsX, GroupsY, 1u))
            {
                BoundDevice->FreeCommandBuffer(Cmd);
                return false;
            }
        }
        WriteStamp(7u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

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

} // namespace Frontier::GeometricRaster
