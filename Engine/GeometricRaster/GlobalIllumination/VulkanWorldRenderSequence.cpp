/*============================================================================================================================================
                                                        VULKANWORLDRENDERSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Implementation of VulkanWorldRenderSequence executing WorldViewportRadiometricCompute.comp.spv on hardware Vulkan GPUs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/Atmosphere/LensFlareSequence.h"

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

struct VolumetricCloudUniformStd140
{
    float                    OriginTimeVector[4]    {};
    float                    SunAxisToggle[4]       {};
    float                    SolarIrradianceExt[4]  {};
    float                    SkyColorWind[4]        {};
    float                    DeckGeometry[4]        {};
    float                    DeckOptics[4]          {};
    float                    WindVectorOcclude[4]   {};
    float                    LocalCenterExt[4]      {};
    float                    LocalExtentFraction[4] {};
    float                    LocalDensityPhase[4]   {};
    float                    FormationOptics[4]     {};
    float                    CloudShadowOptics[4]   {};
    float                    CloudPassControl[4]    {};
    GpuLocalCloudVolumeStd140 LocalClouds[8]        {};
};

static_assert(sizeof(VolumetricCloudUniformStd140) == 848u, "VolumetricCloudUniformStd140 must equal 848 bytes.");

struct GpuLocalFogVolumeStd140
{
    float CenterShape[4]          {};
    float HalfExtentYaw[4]        {};
    float DensityCovScaleSides[4] {};
    float WindVelCurl[4]          {};
    float TintAniso[4]            {};
};

static_assert(sizeof(GpuLocalFogVolumeStd140) == 80u, "GpuLocalFogVolumeStd140 must equal 80 bytes.");

struct GpuFogForceProbeStd140
{
    float PosRadius[4]       {};
    float ForwardStrength[4] {};
    float Params[4]          {};
};

static_assert(sizeof(GpuFogForceProbeStd140) == 48u, "GpuFogForceProbeStd140 must equal 48 bytes.");

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
    GpuLocalFogVolumeStd140 LocalFogs[8]     {};
    GpuFogForceProbeStd140  ForceProbes[8]   {};
};

static_assert(sizeof(VolumetricFogUniformStd140) == 1152u, "VolumetricFogUniformStd140 must equal 1152 bytes.");

struct GpuMoonDescriptorStd140
{
    float DirectionRadiusRad[4] {};
    float TintSynodicPhase[4]       {};
    float OpticsOrientation[4] {};
    float MetaMask[4]          {};
};

struct CelestialFieldUniformStd140
{
    float SunAxisRadius[4]  {};
    float SunTintIntensity[4] {};
    float StarParams[4]          {};
    float StarScintillationRot[4]      {};
    float ClockViewportDims[4]     {};
    GpuMoonDescriptorStd140 Moons[4] {};
};

static_assert(sizeof(CelestialFieldUniformStd140) == 336u, "CelestialFieldUniformStd140 must equal 336 bytes.");

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
};

static_assert(sizeof(AtmosphericSkyUniformStd140) == 96u, "AtmosphericSkyUniformStd140 must equal 96 bytes.");

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
    uint32_t                    AtmosphereMaskPad[4]       {};
    VolumetricCloudUniformStd140 CloudParams                {};
    VolumetricFogUniformStd140   FogParams                  {};
    CelestialFieldUniformStd140  CelestialParams            {};
    LensFlareUniformStd140       LensFlareParams            {};
    FluidRenderUniformStd140     FluidParams                {};
};

static_assert(sizeof(WorldViewportUniformBlockStd140) == 3552u, "WorldViewportUniformBlockStd140 must equal 3552 bytes.");

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

struct GpuWorldMaterial64
{
    float DiffuseAlbedoRoughness[4] {};
    float SpecParams[4]            {};
    float EdgeTintCoatWeight[4] {};
    float CoatRoughnessOptics[4]    {};
};

static_assert(sizeof(GpuWorldMaterial64) == 64u, "GpuWorldMaterial64 must equal 64 bytes.");

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
    if (!DispatchEncoder.InitializeEncoder(BoundDevice, 128u))
    {
        BoundDevice = nullptr;
        return false;
    }

    constexpr std::array<VkDescriptorType, 26u> SlotTypes {{
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
    const std::filesystem::path SkyShaderPath = CompiledShaderDirectory / "SkyViewTransmittanceCompute.comp.spv";
    SkyViewLutProgramHandle = DispatchEncoder.RegisterProgram(
        SkyShaderPath,
        SkySlotTypes,
        "ComputeEntry");

    EnsureBufferCapacity(UniformBuffer, 4096ull * 4ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(SdfUniformBuffer, sizeof(SignedDistanceGridUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    EnsureBufferCapacity(SkyViewUniformBuffer, sizeof(AtmosphericSkyUniformStd140), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    constexpr uint64_t SkyLutBytes = static_cast<uint64_t>(AtmosphericSkySequence::SkyViewWidthPx) *
                                     AtmosphericSkySequence::SkyViewHeightPx * sizeof(float) * 4ull;
    EnsureBufferCapacity(AtmosphericSkyViewLutBuffer, SkyLutBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (AtmosphericSkyViewLutBuffer.HostAddress != nullptr)
    {
        std::memset(AtmosphericSkyViewLutBuffer.HostAddress, 0, static_cast<std::size_t>(SkyLutBytes));
    }
    {
        const auto& MoonAtlasRgba = LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8();
        const uint64_t MoonAtlasBytes = std::max<uint64_t>(4ull, static_cast<uint64_t>(MoonAtlasRgba.size()));
        EnsureBufferCapacity(LunarSurfaceAtlasBuffer, MoonAtlasBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (LunarSurfaceAtlasBuffer.HostAddress != nullptr && !MoonAtlasRgba.empty())
        {
            std::memcpy(LunarSurfaceAtlasBuffer.HostAddress, MoonAtlasRgba.data(), MoonAtlasRgba.size());
        }
    }
    {
        std::vector<uint8_t> FlareAtlasRgba {};
        if (!LensFlareSequence::BakeOpticalAtlasOnGpu(TargetDevice, CompiledShaderDirectory, FlareAtlasRgba))
        {
            FlareAtlasRgba = LensFlareSequence::QueryBakedLensFlareAtlasRgba8();
        }
        const uint64_t FlareAtlasBytes = std::max<uint64_t>(4ull, static_cast<uint64_t>(FlareAtlasRgba.size()));
        EnsureBufferCapacity(BakedLensFlareAtlasBuffer, FlareAtlasBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (BakedLensFlareAtlasBuffer.HostAddress != nullptr && !FlareAtlasRgba.empty())
        {
            std::memcpy(BakedLensFlareAtlasBuffer.HostAddress, FlareAtlasRgba.data(), FlareAtlasRgba.size());
        }
    }
    if (UniformBuffer.BufferHandle != VK_NULL_HANDLE)
    {
        std::fprintf(
            stdout,
            "[FrontierWorld] VulkanWorldRenderSequence active on %.*s (Hardware GPU: %s, GPU SDF Compute: %s)\n",
            static_cast<int>(TargetDevice.QueryAdapterName().size()),
            TargetDevice.QueryAdapterName().data(),
            TargetDevice.QueryHardwareAccelerated() ? "YES" : "NO (Software ICD)",
            SignedDistanceProgramHandle != UINT32_MAX ? "YES" : "NO");
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
        BoundDevice->ReleaseBuffer(UniformBuffer);
        BoundDevice->ReleaseBuffer(SdfUniformBuffer);
        BoundDevice->ReleaseBuffer(SkyViewUniformBuffer);
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
    }
    DispatchEncoder.TeardownEncoder();
    RadiometricProgramHandle    = UINT32_MAX;
    CloudLayerProgramHandle     = UINT32_MAX;
    DenoiseProgramHandle        = UINT32_MAX;
    SignedDistanceProgramHandle = UINT32_MAX;
    SkyViewLutProgramHandle     = UINT32_MAX;
    BoundDevice                 = nullptr;
    SceneUploadedOnce           = false;
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
        EnsureBufferCapacity(IrradianceLatticeBuffer, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (IrradianceLatticeBuffer.HostAddress != nullptr)
        {
            std::memcpy(IrradianceLatticeBuffer.HostAddress, ZeroVec, sizeof(ZeroVec));
        }
        EnsureBufferCapacity(IrradianceChebyshevBuffer, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (IrradianceChebyshevBuffer.HostAddress != nullptr)
        {
            std::memcpy(IrradianceChebyshevBuffer.HostAddress, ZeroVec, sizeof(ZeroVec));
        }
        return;
    }

    const auto& Cfg   = RtxLattice.InspectConfig();
    CachedLatticeMin  = Cfg.GridMinimumWorld;
    CachedLatticeMax  = Cfg.GridMaximumWorld;
    CachedLatticeDimX = Cfg.CountX;
    CachedLatticeDimY = Cfg.CountY;
    CachedLatticeDimZ = Cfg.CountZ;

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
            PackedTexels[TexelOffset + 3u] = 1.0f;
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
    EnsureBufferCapacity(IrradianceLatticeBuffer, LatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (IrradianceLatticeBuffer.HostAddress != nullptr)
    {
        std::memcpy(IrradianceLatticeBuffer.HostAddress, PackedTexels.data(), LatBytes);
    }
    const uint64_t ChebyBytes = PackedCheby.size() * sizeof(float);
    EnsureBufferCapacity(IrradianceChebyshevBuffer, ChebyBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (IrradianceChebyshevBuffer.HostAddress != nullptr)
    {
        std::memcpy(IrradianceChebyshevBuffer.HostAddress, PackedCheby.data(), ChebyBytes);
    }
}

void VulkanWorldRenderSequence::UploadSolarCascadeRadiosity(const WorldSceneSpecification& Scene)
{
    if (!QueryReady())
    {
        return;
    }

    // 1. Build & upload TWO-CASCADE orthographic Sun depth32float clips (FineClip 3.91mm + CoarseClip 15.625mm)
    const Mathematics::Axis3D FocusCenter = (Scene.SceneTree.RootBoundsMin + Scene.SceneTree.RootBoundsMax) * 0.5f;
    SolarShadowSequence::BuildTwoCascadeContactClips(Scene.Sun, FocusCenter, Scene.SceneTree, CachedFineSunCascade, CachedSunCascade);
    if (!CachedFineSunCascade.ClosestLightDepth.empty() && !CachedSunCascade.ClosestLightDepth.empty())
    {
        CoarseShadowOffsetTexels = static_cast<uint32_t>(CachedFineSunCascade.ClosestLightDepth.size());
        const uint64_t FineBytes   = CachedFineSunCascade.ClosestLightDepth.size() * sizeof(float);
        const uint64_t CoarseBytes = CachedSunCascade.ClosestLightDepth.size() * sizeof(float);
        EnsureBufferCapacity(SolarShadowDepthBuffer, FineBytes + CoarseBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (SolarShadowDepthBuffer.HostAddress != nullptr)
        {
            uint8_t* Dst = reinterpret_cast<uint8_t*>(SolarShadowDepthBuffer.HostAddress);
            std::memcpy(Dst, CachedFineSunCascade.ClosestLightDepth.data(), FineBytes);
            std::memcpy(Dst + FineBytes, CachedSunCascade.ClosestLightDepth.data(), CoarseBytes);
        }
    }
    else if (!CachedSunCascade.ClosestLightDepth.empty())
    {
        CoarseShadowOffsetTexels = 0u;
        const uint64_t DepthBytes = CachedSunCascade.ClosestLightDepth.size() * sizeof(float);
        EnsureBufferCapacity(SolarShadowDepthBuffer, DepthBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (SolarShadowDepthBuffer.HostAddress != nullptr)
        {
            std::memcpy(SolarShadowDepthBuffer.HostAddress, CachedSunCascade.ClosestLightDepth.data(), DepthBytes);
        }
    }
    else
    {
        CoarseShadowOffsetTexels = 0u;
        const float FallbackDepth[4]{ 60.0f, 60.0f, 60.0f, 60.0f };
        EnsureBufferCapacity(SolarShadowDepthBuffer, sizeof(FallbackDepth), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (SolarShadowDepthBuffer.HostAddress != nullptr)
        {
            std::memcpy(SolarShadowDepthBuffer.HostAddress, FallbackDepth, sizeof(FallbackDepth));
        }
    }

    // 2. Populate 3D Radiosity SDF Volume matching CachedSdfDims (strictly for SdfGlobalIllumination GI only!)
    const uint64_t TotalVoxels = static_cast<uint64_t>(CachedSdfDimX) * CachedSdfDimY * CachedSdfDimZ;
    std::vector<MaterialSpace::BounceSlabEntry> FallbackSlabs {};
    const std::vector<MaterialSpace::BounceSlabEntry>* ActiveSlabs = &Scene.BounceSummaries;
    if (ActiveSlabs->empty() && Scene.MaterialRegister.QuerySlotCount() > 0u)
    {
        const uint32_t SlotCount = Scene.MaterialRegister.QuerySlotCount();
        FallbackSlabs.resize(SlotCount);
        for (uint32_t Slot = 0u; Slot < SlotCount; ++Slot)
        {
            FallbackSlabs[Slot] = MaterialSpace::CompileBounceSlabEntry(
                Scene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot)));
        }
        ActiveSlabs = &FallbackSlabs;
    }

    if (TotalVoxels >= 8ull && SignedDistanceBuffer.HostAddress != nullptr && !ActiveSlabs->empty())
    {
        const float* SdfSamples = reinterpret_cast<const float*>(SignedDistanceBuffer.HostAddress);
        std::vector<float> RadiosityVoxels(TotalVoxels * 4ull, 0.0f);
        const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
        const float NearBand = CachedSdfSpacing * 1.85f;
        const uint32_t SX    = CachedSdfDimX;
        const uint32_t SXY   = CachedSdfDimX * CachedSdfDimY;

        const auto SampleSdfHost = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> float
        {
            return SdfSamples[static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * SX + X];
        };

        for (uint32_t Z = 0u; Z < CachedSdfDimZ; ++Z)
        {
            for (uint32_t Y = 0u; Y < CachedSdfDimY; ++Y)
            {
                for (uint32_t X = 0u; X < CachedSdfDimX; ++X)
                {
                    const std::size_t Idx = static_cast<std::size_t>(Z) * SXY + static_cast<std::size_t>(Y) * SX + X;
                    if (std::fabs(SdfSamples[Idx]) > NearBand)
                    {
                        continue;
                    }
                    const uint32_t XM = (X > 0u) ? (X - 1u) : 0u;
                    const uint32_t XP = std::min(CachedSdfDimX - 1u, X + 1u);
                    const uint32_t YM = (Y > 0u) ? (Y - 1u) : 0u;
                    const uint32_t YP = std::min(CachedSdfDimY - 1u, Y + 1u);
                    const uint32_t ZM = (Z > 0u) ? (Z - 1u) : 0u;
                    const uint32_t ZP = std::min(CachedSdfDimZ - 1u, Z + 1u);
                    const Mathematics::Axis3D Grad = Mathematics::NormalizeAxis({
                        SampleSdfHost(XP, Y, Z) - SampleSdfHost(XM, Y, Z),
                        SampleSdfHost(X, YP, Z) - SampleSdfHost(X, YM, Z),
                        SampleSdfHost(X, Y, ZP) - SampleSdfHost(X, Y, ZM),
                    }, { 0.0f, 0.0f, 1.0f });

                    const Mathematics::Axis3D VoxelPos {
                        CachedSdfMinCorner.X + static_cast<float>(X) * CachedSdfSpacing,
                        CachedSdfMinCorner.Y + static_cast<float>(Y) * CachedSdfSpacing,
                        CachedSdfMinCorner.Z + static_cast<float>(Z) * CachedSdfSpacing,
                    };

                    SpatialAcceleration::RaySegmentQuery VoxelRay {};
                    VoxelRay.Origin          = VoxelPos + Grad * (CachedSdfSpacing * 0.55f);
                    VoxelRay.UnitDirection   = Grad * -1.0f;
                    VoxelRay.MinimumDistance = 1.0e-3f;
                    VoxelRay.MaximumDistance = NearBand * 1.75f;
                    const auto Hit           = Scene.SceneTree.TraceClosestRay(VoxelRay, false);
                    if (!Hit.HitConfirmed)
                    {
                        continue;
                    }

                    const uint16_t Slot = (Hit.MaterialOrdinal < ActiveSlabs->size()) ? Hit.MaterialOrdinal : 0u;
                    const auto& Slab    = (*ActiveSlabs)[Slot];
                    const Mathematics::Axis3D HitPos = VoxelRay.Origin + VoxelRay.UnitDirection * Hit.HitDistanceMeters;
                    const Mathematics::Axis3D HitNrm = (Mathematics::EvaluateDot(Hit.GeometricNormal, VoxelRay.UnitDirection) < 0.0f)
                        ? Hit.GeometricNormal
                        : (Hit.GeometricNormal * -1.0f);

                    Mathematics::LinearRGB VoxelRad {};
                    // Emissive from BounceSlabEntry
                    if (Hit.MaterialOrdinal < Scene.MaterialRegister.QuerySlotCount())
                    {
                        const auto& Mat = Scene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialOrdinal);
                        VoxelRad = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
                        VoxelRad.R += Scene.SkyRadiance.R * 0.25f * Mat.Diffuse.BaseColor.R * Mat.Diffuse.BaseWeight;
                        VoxelRad.G += Scene.SkyRadiance.G * 0.25f * Mat.Diffuse.BaseColor.G * Mat.Diffuse.BaseWeight;
                        VoxelRad.B += Scene.SkyRadiance.B * 0.25f * Mat.Diffuse.BaseColor.B * Mat.Diffuse.BaseWeight;
                    }
                    if (Mathematics::EvaluateDot(HitNrm, SunDir) > 0.0f)
                    {
                        SpatialAcceleration::RaySegmentQuery SunQ {};
                        SunQ.Origin          = HitPos + HitNrm * 3.0e-3f;
                        SunQ.UnitDirection   = SunDir;
                        SunQ.MinimumDistance = 1.0e-3f;
                        SunQ.MaximumDistance = 100.0f;
                        if (!Scene.SceneTree.QueryRayOccluded(SunQ))
                        {
                            const auto Direct = MaterialSpace::EvaluateBounceSlabRadiance(
                                Slab, HitNrm, Grad, SunDir, Scene.Sun.SolarIrradiance);
                            VoxelRad = VoxelRad + Direct;
                        }
                    }
                    for (const auto& Quad : Scene.AreaLights)
                    {
                        const Mathematics::Axis3D ToL = Quad.CenterWorld - HitPos;
                        const float Dist2 = std::max(0.08f, Mathematics::EvaluateDot(ToL, ToL));
                        const float Dist  = std::sqrt(Dist2);
                        const Mathematics::Axis3D Wi = ToL * (1.0f / Dist);
                        const float NdotL = Mathematics::EvaluateDot(HitNrm, Wi);
                        if (NdotL > 0.01f)
                        {
                            const Mathematics::Axis3D LNorm = Mathematics::NormalizeAxis(
                                Mathematics::EvaluateCross(Quad.RightTangent, Quad.UpBitangent), { 0.0f, 0.0f, -1.0f });
                            const float LCos = Quad.TwoSidedEmission
                                ? std::fabs(Mathematics::EvaluateDot(LNorm, Wi * -1.0f))
                                : std::max(0.0f, Mathematics::EvaluateDot(LNorm, Wi * -1.0f));
                            if (LCos > 0.01f)
                            {
                                SpatialAcceleration::RaySegmentQuery QRay {};
                                QRay.Origin          = HitPos + HitNrm * 4.0e-3f;
                                QRay.UnitDirection   = Wi;
                                QRay.MinimumDistance = 1.0e-3f;
                                QRay.MaximumDistance = std::max(0.02f, Dist - 0.02f);
                                if (!Scene.SceneTree.QueryRayOccluded(QRay))
                                {
                                    const float Area = 4.0f * Quad.HalfWidthMeters * Quad.HalfHeightMeters;
                                    const float Geom = (NdotL * LCos * Area) / (3.14159265f * Dist2);
                                    const auto  QuadIrr = Quad.EmittedRadiance * Geom;
                                    VoxelRad = VoxelRad + MaterialSpace::EvaluateBounceSlabRadiance(
                                        Slab, HitNrm, Grad, Wi, QuadIrr);
                                }
                            }
                        }
                    }

                    RadiosityVoxels[Idx * 4ull + 0ull] = VoxelRad.R;
                    RadiosityVoxels[Idx * 4ull + 1ull] = VoxelRad.G;
                    RadiosityVoxels[Idx * 4ull + 2ull] = VoxelRad.B;
                    RadiosityVoxels[Idx * 4ull + 3ull] = 1.0f;
                }
            }
        }

        const uint64_t RadBytes = RadiosityVoxels.size() * sizeof(float);
        EnsureBufferCapacity(SdfRadiosityVoxelBuffer, RadBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (SdfRadiosityVoxelBuffer.HostAddress != nullptr)
        {
            std::memcpy(SdfRadiosityVoxelBuffer.HostAddress, RadiosityVoxels.data(), RadBytes);
        }
    }
    else
    {
        const uint64_t RadBytes = std::max<uint64_t>(16ull, TotalVoxels * 4ull * sizeof(float));
        EnsureBufferCapacity(SdfRadiosityVoxelBuffer, RadBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (SdfRadiosityVoxelBuffer.HostAddress != nullptr)
        {
            std::memset(SdfRadiosityVoxelBuffer.HostAddress, 0, static_cast<std::size_t>(RadBytes));
        }
    }
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
    EnsureBufferCapacity(SignedDistanceBuffer, SdfBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
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

    // 1. Upload 128-byte 4-Wide BVH4 entries
    if (!Scene.SceneTree.WideEntries.empty())
    {
        const uint64_t BvhBytes = Scene.SceneTree.WideEntries.size() * sizeof(SpatialAcceleration::WideTreeEntry);
        EnsureBufferCapacity(BvhEntryBuffer, BvhBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (BvhEntryBuffer.HostAddress != nullptr)
        {
            std::memcpy(BvhEntryBuffer.HostAddress, Scene.SceneTree.WideEntries.data(), BvhBytes);
        }
        UploadedBvhCount = static_cast<uint32_t>(Scene.SceneTree.WideEntries.size());
    }
    else
    {
        SpatialAcceleration::WideTreeEntry DummyEntry {};
        EnsureBufferCapacity(BvhEntryBuffer, sizeof(DummyEntry), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (BvhEntryBuffer.HostAddress != nullptr)
        {
            std::memcpy(BvhEntryBuffer.HostAddress, &DummyEntry, sizeof(DummyEntry));
        }
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
    EnsureBufferCapacity(ShadedTriangleBuffer, TriBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (ShadedTriangleBuffer.HostAddress != nullptr)
    {
        std::memcpy(ShadedTriangleBuffer.HostAddress, ShadedTris.data(), TriBytes);
    }

    // 3. Upload 64-byte OpenPBR surface material entries
    const uint32_t MatCount = std::max(1u, Scene.MaterialRegister.QuerySlotCount());
    std::vector<GpuWorldMaterial64> GpuMats(MatCount);
    for (uint32_t Slot = 0u; Slot < Scene.MaterialRegister.QuerySlotCount(); ++Slot)
    {
        const auto& Src = Scene.MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Slot));
        GpuWorldMaterial64 Dst {};
        Dst.DiffuseAlbedoRoughness[0] = Src.Diffuse.BaseColor.R * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[1] = Src.Diffuse.BaseColor.G * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[2] = Src.Diffuse.BaseColor.B * Src.Diffuse.BaseWeight;
        Dst.DiffuseAlbedoRoughness[3] = Src.Diffuse.DiffuseRoughness;
        Dst.SpecParams[0]            = Src.Specular.Roughness;
        Dst.SpecParams[1]            = Src.Specular.Metalness;
        Dst.SpecParams[2]            = Src.Specular.DielectricIOR;
        Dst.SpecParams[3]            = Src.Specular.Anisotropy;
        Dst.EdgeTintCoatWeight[0] = Src.Specular.EdgeTintF82.R;
        Dst.EdgeTintCoatWeight[1] = Src.Specular.EdgeTintF82.G;
        Dst.EdgeTintCoatWeight[2] = Src.Specular.EdgeTintF82.B;
        Dst.EdgeTintCoatWeight[3] = Src.Clearcoat.CoatWeight;
        Dst.CoatRoughnessOptics[0]    = Src.Clearcoat.CoatRoughness;
        Dst.CoatRoughnessOptics[1]    = Src.Transmission.TransmissionWeight;
        Dst.CoatRoughnessOptics[2]    = Src.Transmission.AbbeDispersionNumber;
        Dst.CoatRoughnessOptics[3]    = 0.0f;
        GpuMats[Slot]                = Dst;
    }
    const uint64_t MatBytes = GpuMats.size() * sizeof(GpuWorldMaterial64);
    EnsureBufferCapacity(MaterialEntryBuffer, MatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (MaterialEntryBuffer.HostAddress != nullptr)
    {
        std::memcpy(MaterialEntryBuffer.HostAddress, GpuMats.data(), MatBytes);
    }
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
                EnsureBufferCapacity(SignedDistanceBuffer, SdfBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                if (SignedDistanceBuffer.HostAddress != nullptr)
                {
                    std::memcpy(SignedDistanceBuffer.HostAddress, DenseSdf.data(), SdfBytes);
                }
            }
            else
            {
                CachedSdfDimX = CachedSdfDimY = CachedSdfDimZ = 0u;
                const float FallbackDist = 4.0f;
                EnsureBufferCapacity(SignedDistanceBuffer, sizeof(float) * 4u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                if (SignedDistanceBuffer.HostAddress != nullptr)
                {
                    std::memcpy(SignedDistanceBuffer.HostAddress, &FallbackDist, sizeof(float));
                }
            }
        }
    }

    // 5. Upload Tier 2 RTX Irradiance Lattice + Chebyshev Depth independently of RebuildSdfGrid
    if (OptionalRtxLattice != nullptr && !OptionalRtxLattice->InspectCells().empty())
    {
        UploadIrradianceLattice(*OptionalRtxLattice);
    }
    else if (IrradianceLatticeBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        CachedLatticeDimX = CachedLatticeDimY = CachedLatticeDimZ = 0u;
        const float ZeroVec[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        EnsureBufferCapacity(IrradianceLatticeBuffer, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (IrradianceLatticeBuffer.HostAddress != nullptr)
        {
            std::memcpy(IrradianceLatticeBuffer.HostAddress, ZeroVec, sizeof(ZeroVec));
        }
        EnsureBufferCapacity(IrradianceChebyshevBuffer, sizeof(ZeroVec), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (IrradianceChebyshevBuffer.HostAddress != nullptr)
        {
            std::memcpy(IrradianceChebyshevBuffer.HostAddress, ZeroVec, sizeof(ZeroVec));
        }
    }

    // 6. Upload Sun Orthographic Depth Cascade + 3D SDF Radiosity Voxels
    if (RebuildSdfGrid || !SceneUploadedOnce || SolarShadowDepthBuffer.BufferHandle == VK_NULL_HANDLE)
    {
        UploadSolarCascadeRadiosity(Scene);
    }

    SceneUploadedOnce = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU COMPUTE DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool VulkanWorldRenderSequence::RenderWorldViewportRgba8(
    WorldRenderTier                Tier,
    const PerspectiveCameraFrame&  Camera,
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
    if (SkyViewLutProgramHandle == UINT32_MAX || SkyViewUniformBuffer.HostAddress == nullptr ||
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

    std::memcpy(SkyViewUniformBuffer.HostAddress, &SkyUni, sizeof(SkyUni));

    const std::array<ComputeSlotDescriptor, 2u> SkySlots {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SkyViewUniformBuffer.BufferHandle,          sizeof(AtmosphericSkyUniformStd140) },
        { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, AtmosphericSkyViewLutBuffer.BufferHandle, VK_WHOLE_SIZE },
    }};

    const uint32_t GroupsX = (AtmosphericSkySequence::SkyViewWidthPx  + 7u) / 8u;
    const uint32_t GroupsY = (AtmosphericSkySequence::SkyViewHeightPx + 7u) / 8u;
    if (DispatchEncoder.EncodeComputeDispatch(Cmd, SkyViewLutProgramHandle, SkySlots, GroupsX, GroupsY, 1u))
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
    }
}

bool VulkanWorldRenderSequence::RenderWorldViewportWithEngine(
    const WorldRenderConfiguration& Config,
    uint32_t                        PathTraceSampleIndex,
    const PerspectiveCameraFrame&   Camera,
    const WorldSceneSpecification&  Scene,
    std::vector<uint8_t>&           OutDisplayRgba8)
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

    const uint32_t WidthPx  = std::max(1u, Camera.ViewportWidthPx);
    const uint32_t HeightPx = std::max(1u, Camera.ViewportHeightPx);
    const float    CloudScale = std::clamp(Scene.Atmosphere.Clouds.CloudRenderScale, 0.25f, 1.0f);
    const uint32_t CloudW     = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(WidthPx)  * CloudScale)));
    const uint32_t CloudH     = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(HeightPx) * CloudScale)));
    const uint64_t OutBytes   = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    const uint64_t AccBytes   = static_cast<uint64_t>(WidthPx) * HeightPx * 16ull;
    const uint64_t CloudBytes = static_cast<uint64_t>(CloudW) * CloudH * 16ull;
    EnsureBufferCapacity(OutputPixelBuffer, OutBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(AccumulationHdrBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(GBufPosMaskBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(GBufNormAoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(GBufDirectBaseBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(GBufSunContribBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(GBufAlbedoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(PingShadowAoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(PingGiHdrBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(PongShadowAoBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(PongGiHdrBuffer, AccBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EnsureBufferCapacity(CloudRadiometricBuffer, CloudBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    if (OutputPixelBuffer.HostAddress == nullptr || AccumulationHdrBuffer.HostAddress == nullptr ||
        GBufPosMaskBuffer.BufferHandle == VK_NULL_HANDLE || PongGiHdrBuffer.BufferHandle == VK_NULL_HANDLE ||
        CloudRadiometricBuffer.BufferHandle == VK_NULL_HANDLE)
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

    const bool AtmoActive = Scene.Atmosphere.Medium.AtmosphereEnabled;
    const auto SolarSolved = CelestialEphemerisSolver::SolveSolarEphemeris(Scene.Atmosphere.Solar);
    const Mathematics::Axis3D SunDir = AtmoActive
        ? SolarSolved.WorldDirectionToSun
        : Mathematics::NormalizeAxis(Scene.Sun.DirectionToSun, { 0.42f, -0.55f, 0.72f });
    Uniforms.SunDirConeTan[0]       = SunDir.X;
    Uniforms.SunDirConeTan[1]       = SunDir.Y;
    Uniforms.SunDirConeTan[2]       = SunDir.Z;
    Uniforms.SunDirConeTan[3]       = std::tan(std::max(0.004f,
        AtmoActive ? (Scene.Atmosphere.Solar.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat)
                   : Scene.Sun.SunAngularRadiusRadians));

    Mathematics::LinearRGB EffectiveSunIrrad = Scene.Sun.SolarIrradiance;
    Mathematics::LinearRGB SolarTrans { 1.0f, 1.0f, 1.0f };
    if (AtmoActive)
    {
        SolarTrans = AtmosphericSkySequence::ComputeSolarTransmittance(SunDir, Scene.Atmosphere.Medium);
        const float SunVis     = std::clamp((SolarSolved.ElevationDegrees + 4.0f) / 10.0f, 0.0f, 1.0f);
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
    Uniforms.SolarIrradianceTier[3] = (Config.Engine == WorldRenderEngine::DdgiRaytracing && LatticeReady) ? 1.0f : 0.0f;

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
        C.CloudPassControl[3]   = (Atmo.ForceFieldSource != nullptr && Atmo.ForceFieldSource->QueryActiveSourceCount() > 0u) ? 1.0f : 0.0f;

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
        Cel.SunTintIntensity[0] = SolarSolved.EffectiveSolarTint.R * SolarTrans.R;
        Cel.SunTintIntensity[1] = SolarSolved.EffectiveSolarTint.G * SolarTrans.G;
        Cel.SunTintIntensity[2] = SolarSolved.EffectiveSolarTint.B * SolarTrans.B;
        Cel.SunTintIntensity[3] = (Atmo.Solar.SolarEnabled && Atmo.Solar.SolarIntensityMultiplier > 0.0f)
            ? (Atmo.Solar.SolarIntensityMultiplier * Atmo.Medium.SkyBrightnessMultiplier * 0.075f)
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
        Cel.ClockViewportDims[3]     = 4.0f;

        for (uint32_t MIdx = 0u; MIdx < 4u; ++MIdx)
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
            DstM.MetaMask[1]          = static_cast<float>(static_cast<uint32_t>(SrcM.SurfacePreset)) * 3.17f;
            DstM.MetaMask[2]          = static_cast<float>(static_cast<uint32_t>(SrcM.SurfacePreset));
            DstM.MetaMask[3]          = (MIdx == 0u) ? Atmo.Medium.RayleighStrength
                                      : (MIdx == 1u) ? Atmo.Medium.MieStrength
                                      : (MIdx == 2u) ? Atmo.Medium.OzoneStrength
                                                     : (Atmo.Medium.AtmosphereEnabled ? 1.0f : 0.0f);
        }

        const auto& LF = Atmo.LensFlare;
        auto&       LFU = Uniforms.LensFlareParams;
        if (AtmoActive && LF.Enabled && Config.DebugChannel == 0u)
        {
            const ProjectedLensFlareFrame FlareProj = LensFlareSequence::ResolveSolarFlareProjection(
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
            EnsureBufferCapacity(FluidHeroThermoBuffer, 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            EnsureBufferCapacity(FluidWorldThermoBuffer, 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        }
    }

    constexpr uint64_t UniformStrideBytes = 4096ull;
    EnsureBufferCapacity(UniformBuffer, UniformStrideBytes * 4ull, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uint8_t* UniformBase = reinterpret_cast<uint8_t*>(UniformBuffer.HostAddress);
    std::memcpy(UniformBase, &Uniforms, sizeof(Uniforms));

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

    auto MakePassSlots = [&](uint64_t UniOffset, bool SwapPingPong) -> std::array<ComputeSlotDescriptor, 26u>
    {
        const auto& InShadow  = SwapPingPong ? PongShadowAoBuffer : PingShadowAoBuffer;
        const auto& InGi      = SwapPingPong ? PongGiHdrBuffer    : PingGiHdrBuffer;
        const auto& OutShadow = SwapPingPong ? PingShadowAoBuffer : PongShadowAoBuffer;
        const auto& OutGi     = SwapPingPong ? PingGiHdrBuffer    : PongGiHdrBuffer;
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
        }};
    };

    const uint32_t CloudGroupsX = (CloudW   + 7u) / 8u;
    const uint32_t CloudGroupsY = (CloudH   + 7u) / 8u;
    const uint32_t GroupsX      = (WidthPx  + 7u) / 8u;
    const uint32_t GroupsY      = (HeightPx + 7u) / 8u;
    const auto Pass0Slots = MakePassSlots(0ull, false);
    if (AtmoActive && Scene.Atmosphere.Clouds.CloudsEnabled && Config.DebugChannel == 0u)
    {
        if (!DispatchEncoder.EncodeComputeDispatch(Cmd, CloudLayerProgramHandle, Pass0Slots, CloudGroupsX, CloudGroupsY, 1u))
        {
            return false;
        }
    }
    if (!DispatchEncoder.EncodeComputeDispatch(Cmd, RadiometricProgramHandle, Pass0Slots, GroupsX, GroupsY, 1u))
    {
        return false;
    }

    const uint32_t EngIdx = static_cast<uint32_t>(Config.Engine);
    const bool ShadingDebug =
        (Config.DebugChannel == 0u || Config.DebugChannel == 8u ||
         Config.DebugChannel == 12u || Config.DebugChannel == 13u ||
         Config.DebugChannel == 14u || Config.DebugChannel == 15u);
    const bool SpatialDenoise =
        ShadingDebug &&
        ((EngIdx == 1u || EngIdx == 2u || EngIdx == 3u) ||
         (EngIdx == 4u && Config.PathTraceDenoise && PathTraceSampleIndex < 16u));

    if (SpatialDenoise)
    {
        constexpr std::array<uint32_t, 3u> Strides {{ 1u, 2u, 4u }};
        for (uint32_t Pass = 0u; Pass < 3u; ++Pass)
        {
            const uint64_t UniOffs = static_cast<uint64_t>(Pass + 1u) * UniformStrideBytes;
            WorldViewportUniformBlockStd140 PassUni = Uniforms;
            PassUni.DenoiseStepParams[0] = Strides[Pass];
            PassUni.DenoiseStepParams[1] = 2u | ((Pass == 2u) ? 1u : 0u);
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

    VkMemoryBarrier HostReadBarrier {};
    HostReadBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    HostReadBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u,
        &HostReadBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    const uint64_t SyncPoint = BoundDevice->DispatchCommandBufferAsync(Cmd);
    if (!BoundDevice->AwaitTimelineTick(SyncPoint, 60000000000ull))
    {
        std::fprintf(stderr, "[VulkanWorldRenderSequence] AwaitTimelineTick timed out (Engine=%u, %ux%u)\n",
                     static_cast<unsigned>(Config.Engine), WidthPx, HeightPx);
        return false;
    }

    OutDisplayRgba8.resize(static_cast<std::size_t>(OutBytes));
    std::memcpy(OutDisplayRgba8.data(), OutputPixelBuffer.HostAddress, static_cast<std::size_t>(OutBytes));

    const auto EndClock      = std::chrono::steady_clock::now();
    LastDispatchMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(EndClock - StartClock).count());
    return true;
}

} // namespace Frontier::GeometricRaster
