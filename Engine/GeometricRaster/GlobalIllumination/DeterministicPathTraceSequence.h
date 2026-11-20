/*============================================================================================================================================
                                                  DETERMINISTICPATHTRACESEQUENCE.H
============================================================================================================================================*/
// 🧩 Order 3 unified deterministic Sobol' (0,2) Vulkan 1.3 path tracer shared 1:1 by GameRealtime and SceneFinalRender

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/PhotometricProfile.h"
#include "Engine/GeometricRaster/DirectLighting/PhotometricLightTree.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/SpatialAcceleration/CompressedWideTree.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster
{

struct WorldSceneSpecification;

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DeterministicRenderProfile : uint8_t
{
    SceneFinalRender = 0u, // 1x full pixel trace, Shared (0,m,2) net, Denoiser Off, Restart motion, exact convergence
    GameRealtime     = 1u, // 1/2 or 1x trace, BlueNoise (0,m,2) net, DisplayReconstruction a-trous, Reproject motion (span=8)
    Custom           = 2u, // Inspector-edited custom combination over the identical integrator shader
};

enum class QuadraturePointPattern : uint8_t
{
    Shared    = 0u, // One shared Sobol' (0,2) point set across all pixels (zero grain)
    PerPixel  = 1u, // Fixed per-pixel Owen scramble key (no RNG)
    BlueNoise = 2u, // 64^2 void-and-cluster rank XOR digital shift per pair (even spatial error spectrum)
};

enum class StudioPaintPreset : uint8_t
{
    AuthoredLime = 0u,
    RacingRed    = 1u,
    MidnightBlue = 2u,
    PearlWhite   = 3u,
    Graphite     = 4u,
};

enum class StudioFloorPreset : uint8_t
{
    PolishedEpoxy = 0u,
    MatteConcrete = 1u,
    BlackMirror   = 2u,
    Lambert50     = 3u,
};

enum class StudioEnvironmentPreset : uint8_t
{
    DarkStudio  = 0u,
    OvercastSky = 1u,
    WarmDusk    = 2u,
};

enum class StudioCameraPreset : uint8_t
{
    Hero    = 0u,
    Profile = 1u,
    Front   = 2u,
    Spheres = 3u,
    Rear    = 4u,
    Fleet   = 5u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DeterministicOrbitCamera
{
    float                YawRadians         = 0.78f;                  // [rad] - Azimuth around studio +Y from +Z
    float                PitchRadians       = 0.13f;                  // [rad] - Elevation above skyline
    float                DistanceMeters     = 9.6f;                   // [m]   - Orbit radius to Target
    std::array<float, 3> Target             { 0.10f, 0.70f, 1.00f };  // [m]   - Studio look-at target
    float                FieldOfViewDegrees = 36.0f;                  // [deg] - Vertical field of view
};

struct DeterministicCameraBasis
{
    std::array<float, 3> Origin  {};
    std::array<float, 3> Forward {};
    std::array<float, 3> Right   {}; // Scaled by tan(fov/2) * aspect
    std::array<float, 3> Up      {}; // Scaled by tan(fov/2)
};

struct DeterministicTracerConfiguration
{
    DeterministicRenderProfile Profile                 = DeterministicRenderProfile::SceneFinalRender;
    StudioCameraPreset         CameraPreset            = StudioCameraPreset::Hero;
    DeterministicOrbitCamera   Camera                  {};
    float                      ApertureRadius          = 0.0f;
    float                      FocusDistance           = 0.0f;

    // Look & lighting
    StudioPaintPreset          PaintPreset             = StudioPaintPreset::AuthoredLime;
    float                      PaintRoughness          = 0.0775f;
    StudioFloorPreset          FloorPreset             = StudioFloorPreset::PolishedEpoxy;
    StudioEnvironmentPreset    EnvironmentPreset       = StudioEnvironmentPreset::DarkStudio;
    float                      EnvironmentIntensity    = 1.5f;
    float                      KeyIntensity            = 4.0f;
    float                      RimIntensity            = 12.0f;
    float                      FillIntensity           = 3.0f;
    float                      LampExitance            = 0.2f;
    PracticalLampMode          PracticalMode           = PracticalLampMode::Disabled;
    float                      PracticalIntensity      = 1.0f;
    bool                       NightRigActive          = false;
    bool                       LightsVisibleToCamera   = false;
    float                      ExposureStops           = 0.0f;

    // Transport & SDF Penumbra Mode Selector
    uint32_t                   BounceLimit             = 2u;
    uint32_t                   DepthLimit              = 10u;
    float                      ThroughputCutoff        = 0.002f;
    uint32_t                   SdfPenumbraModeIndex    = 1u; // [0..5] - 0=PointSdf, 1=AnalyticCone, 2=VoxelClipmap, 3=VoxelConeGi, 4=HybridSobolCap, 5=SdfMcRef96Spp
    uint32_t                   SampleBatch             = 1u;
    LampSamplingRule           LampSampling            = LampSamplingRule::SplitEight;
    uint32_t                   TraceScale              = 1u;   // 1 = Full, 2 = 1/2 (2x2 tile), 4 = 1/4 (4x4 tile)
    float                      RenderScale             = 0.75f;
    uint32_t                   TargetSamples           = 1024u;

    // Real-time & fleet
    QuadraturePointPattern     PointPattern            = QuadraturePointPattern::Shared;
    bool                       VisibilityRasterActive  = false;
    bool                       ReprojectionActive      = false;
    uint32_t                   HistoryCapacity         = 32u;
    float                      HistorySpan             = 8.0f;
    float                      HistoryClampGamma       = 0.0f;
    bool                       DenoiseActive           = false;
    float                      DenoiseLuminanceSigma   = 4.0f;
    bool                       VehiclePresent          = true;
    uint32_t                   VehicleCount            = 1u;   // 1, 2, 4, 8, 16, 32, or 50
    bool                       ShowcaseSpheresPresent  = true;
};

struct DeterministicSceneSummary
{
    uint32_t TriangleCount         = 0u;
    uint32_t VehicleTriangleCount  = 0u;
    uint32_t BoundsEntryCount      = 0u;
    uint32_t VehicleWideEntries    = 0u;
    uint32_t InstanceWideEntries   = 0u;
    uint32_t BoundsDepthTier       = 0u;
    uint32_t BoundsByteSize        = 0u;
    uint32_t VehicleCount          = 0u;
    uint32_t SphereCount           = 0u;
    uint32_t AreaLightCount        = 0u;
    uint32_t PracticalLampCount    = 0u;
    uint32_t LightTreeClusterCount = 0u;
    uint32_t LightTreeDepth        = 0u;
    uint32_t MaterialSlotCount     = 0u;
    double   SceneBuildMillis      = 0.0;
    double   LightTreeBuildMillis  = 0.0;
};

struct DeterministicSequenceTimings
{
    double LastDispatchMillis   = 0.0;
    double ReprojectMillis      = 0.0;
    double GuidesMillis         = 0.0;
    double TraceMillis          = 0.0;
    double DenoiseMillis        = 0.0;
    double DisplayMillis        = 0.0;
    double ElapsedSeconds       = 0.0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             DETERMINISTIC PATH TRACE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class DeterministicPathTraceSequence
{
public:
    DeterministicPathTraceSequence() = default;
    ~DeterministicPathTraceSequence();

    DeterministicPathTraceSequence(const DeterministicPathTraceSequence&)            = delete;
    DeterministicPathTraceSequence& operator=(const DeterministicPathTraceSequence&) = delete;

    // Applies canonical GameRealtime or SceneFinalRender configuration presets over the identical integrator
    static void ApplyRenderProfile(
        DeterministicRenderProfile        Profile,
        DeterministicTracerConfiguration& InOutConfig) noexcept;

    [[nodiscard]] static DeterministicOrbitCamera QueryCameraPreset(StudioCameraPreset Preset) noexcept;

    [[nodiscard]] static DeterministicCameraBasis ResolveCameraBasis(
        const DeterministicOrbitCamera& Camera,
        float                           AspectRatio) noexcept;

    // Exact host C++ replica of QuadratureSequence.slang::ScrambledQuadraturePair
    [[nodiscard]] static std::array<float, 2> EvaluateHostQuadraturePair(
        uint32_t                SampleIndex,
        uint32_t                PairIndex,
        uint32_t                PixelKey   = 0u,
        std::array<uint32_t, 2> DigitShift = { 0u, 0u }) noexcept;

    // Exact IEEE-754 FNV-1a 32-bit digest matching TracingDivision.js::DigestFloatBuffer
    [[nodiscard]] static std::string DigestFloatSpan(std::span<const float> Floats);

    // Sample-count log2 smoothstep denoiser fade (1.0 at <= 4 spp -> 0.0 at >= 64 spp)
    [[nodiscard]] static float ResolveDenoiseStrength(float EffectivePixelSamples) noexcept;

    [[nodiscard]] bool Initialize(
        VulkanInterchange&           DeviceInterchange,
        const std::filesystem::path& CompiledShaderDirectory,
        const std::filesystem::path& RepositoryRootDirectory,
        std::string*                 OutDiagnostic = nullptr);

    void Teardown() noexcept;

    [[nodiscard]] bool Ready() const noexcept { return ProgramsReady; }

    [[nodiscard]] bool ConfigureStudioScene(const DeterministicTracerConfiguration& NextConfiguration);

    [[nodiscard]] bool ConfigureCustomWorldScene(
        const WorldSceneSpecification&          WorldScene,
        const DeterministicTracerConfiguration& TemplateConfiguration);

    [[nodiscard]] bool ResizeAccumulation(uint32_t WidthPx, uint32_t HeightPx);

    void ResetAccumulation(bool CameraMoved = false);

    void UpdateCamera(const DeterministicOrbitCamera& NextCamera, bool AllowReprojection = true);

    // Integrates [SampleIndexCursor, SampleIndexCursor + SampleBatch) strictly in index order
    [[nodiscard]] bool AdvanceSamples(uint32_t SampleBatch);

    // Resolves display-ready RGBA8 pixels (running DisplayReconstructionFilter if active, leaving raw sums untouched)
    [[nodiscard]] bool ResolveDisplayRgba8(std::vector<uint8_t>& OutRgba8, float ExposureStopsOverride = 1.0e9f);

    // Reads raw IEEE-754 accumulation region 0 (Width * Height * 4 floats: rgb sum + sample count w)
    [[nodiscard]] bool RetrieveRawAccumulation(std::vector<float>& OutRawAccumulation);

    // Reads linear display HDR radiance (post-reconstruction if active, else exact mean) as Width * Height * 3 floats
    [[nodiscard]] bool RetrieveDisplayLinearRgb(std::vector<float>& OutLinearRgb);

    [[nodiscard]] std::string ComputeAccumulationDigest();

    // Re-renders SampleTotal from 0 with batch=1 vs batch=AlternateBatch and verifies bit-identical FNV-1a digests
    [[nodiscard]] bool VerifyBitExactRepeat(
        uint32_t     SampleTotal,
        uint32_t     AlternateBatch,
        std::string* OutPrimaryDigest = nullptr,
        std::string* OutRepeatDigest  = nullptr);

    // Runs DeterministicQuadratureExtract.comp.spv for GPU-vs-host bit-exactness validation
    [[nodiscard]] std::vector<float> ExtractDeviceQuadrature(uint32_t SampleTotal, uint32_t PairTotal);

    [[nodiscard]] const DeterministicTracerConfiguration& QueryConfiguration() const noexcept { return Configuration; }
    [[nodiscard]] DeterministicTracerConfiguration&       AccessConfiguration() noexcept      { return Configuration; }
    [[nodiscard]] const DeterministicSceneSummary&        QuerySceneSummary() const noexcept  { return Summary; }
    [[nodiscard]] const DeterministicSequenceTimings&        QuerySequenceTimings() const noexcept  { return Timings; }
    [[nodiscard]] uint32_t                                QueryWidth() const noexcept         { return AccumulationWidth; }
    [[nodiscard]] uint32_t                                QueryHeight() const noexcept        { return AccumulationHeight; }
    [[nodiscard]] uint32_t                                QuerySampleIndexCount() const noexcept { return SampleIndexCursor; }
    [[nodiscard]] float                                   QueryEffectivePixelSamples() const noexcept;
    [[nodiscard]] uint32_t                                QueryActiveTraceScale() const noexcept;
    [[nodiscard]] bool                                    QueryReconstructionActive() const noexcept;
    [[nodiscard]] std::string_view                        QueryAdapterName() const noexcept;

private:
    struct PrototypeVehicleGeometry
    {
        SpatialAcceleration::CompressedWideTree WideTree       {};
        std::vector<float>                      PositionFloats {}; // 12 floats per reordered triangle: A, EdgeA, EdgeB + slot/thinBit
        std::vector<float>                      NormalFloats   {}; // 12 floats per reordered triangle: Na, Nb, Nc
        std::array<float, 6>                    LocalBounds    {}; // minX,minY,minZ, maxX,maxY,maxZ
        uint32_t                                SourceTriCount = 0u;
        bool                                    Built          = false;
    };

    struct InstanceTreeBuildOutput
    {
        std::vector<uint32_t> Words              {};
        uint32_t              InstanceWideCount  = 0u;
        uint32_t              MaxInstanceDepth   = 0u;
        uint32_t              InstanceRootEntry  = 0u;
        uint32_t              PlacementStartVec4 = 0u;
        uint32_t              InstanceCount      = 0u;
    };

    [[nodiscard]] bool LoadStaticAssets(const std::filesystem::path& RepositoryRootDirectory, std::string* OutDiagnostic);
    [[nodiscard]] bool ConstructShaderPrograms(const std::filesystem::path& CompiledShaderDirectory, std::string* OutDiagnostic);
    [[nodiscard]] bool RebuildVisibilityFramebuffer();
    [[nodiscard]] bool RebuildDescriptorSets();
    void               WriteTracingUniformSlot(
                      uint32_t                        FirstSample,
                      uint32_t                        SampleBatch,
                      const DeterministicCameraBasis* PreviousCamera,
                      uint32_t                        HistoryReadOffsetPx,
                      uint32_t                        HistoryWriteOffsetPx,
                      uint32_t                        SlotIndex);
    void               EncodePendingReprojection(VkCommandBuffer Cmd);
    void               EncodeSumClear(VkCommandBuffer Cmd);
    void               EncodeVisibilityRaster(VkCommandBuffer Cmd, uint32_t SampleIndex);
    void               EncodeDisplayReconstruction(VkCommandBuffer Cmd);
    [[nodiscard]] bool AwaitDispatchedCommands(VkCommandBuffer Cmd) const;
    [[nodiscard]] bool RasterizedVisibilityActive() const noexcept;

    VulkanInterchange*                                  Vulkan                 = nullptr;
    std::filesystem::path                               RepoRoot               {};
    DeterministicTracerConfiguration                    Configuration          {};
    DeterministicSceneSummary                           Summary                {};
    DeterministicSequenceTimings                           Timings                {};

    PrototypeVehicleGeometry                            PrototypeVehicle       {};
    std::vector<Codecs::ResampledPhotometricProfile>    PhotometryProfiles     {};
    PackedLightRegisterLayout                             LightLayout            {};
    uint32_t                                            InstanceRootEntry      = 0u;
    uint32_t                                            PlacementStartVec4     = 0u;
    uint32_t                                            UploadedVehicleCount   = 0u;
    uint32_t                                            RasterReferenceTris    = 0u;
    uint32_t                                            StudioFloorSlot        = 10u;
    std::array<float, 4>                                ShowcaseSphereBounds   { 0.0f, 0.0f, 0.0f, 0.0f };
    bool                                                CustomSceneActive      = false;

    uint32_t                                            AccumulationWidth      = 0u;
    uint32_t                                            AccumulationHeight     = 0u;
    uint32_t                                            SampleIndexCursor      = 0u;
    uint32_t                                            HistoryRegionIndex     = 1u;
    bool                                                HistoryActive          = false;
    bool                                                ReprojectionPending    = false;
    bool                                                FeaturesStale          = true;
    bool                                                AccumulatedCameraValid   = false;
    DeterministicCameraBasis                            AccumulatedCameraBasis {};

    // Vulkan descriptors, render attachment & shader programs
    VkDescriptorPool                                    DescriptorAllocator    = VK_NULL_HANDLE;
    VkDescriptorSetLayout                               IntegrationSetLayout   = VK_NULL_HANDLE;
    VkDescriptorSetLayout                               ReconstructSetLayout   = VK_NULL_HANDLE;
    VkDescriptorSetLayout                               DisplaySetLayout       = VK_NULL_HANDLE;
    VkDescriptorSetLayout                               RasterSetLayout        = VK_NULL_HANDLE;

    VkPipelineLayout                                    IntegrationPipeLayout  = VK_NULL_HANDLE;
    VkPipelineLayout                                    ReconstructPipeLayout  = VK_NULL_HANDLE;
    VkPipelineLayout                                    DisplayPipeLayout      = VK_NULL_HANDLE;
    VkPipelineLayout                                    RasterPipeLayout       = VK_NULL_HANDLE;

    VkPipeline                                          IntegrateProgram      = VK_NULL_HANDLE;
    VkPipeline                                          ReprojectProgram      = VK_NULL_HANDLE;
    VkPipeline                                          FeaturesProgram       = VK_NULL_HANDLE;
    VkPipeline                                          QuadratureProgram     = VK_NULL_HANDLE;
    VkPipeline                                          GatherProgram         = VK_NULL_HANDLE;
    VkPipeline                                          AtrousProgram         = VK_NULL_HANDLE;
    VkPipeline                                          DisplayProgram        = VK_NULL_HANDLE;
    VkRenderPass                                        VisibilityAttachmentTarget   = VK_NULL_HANDLE;
    VkPipeline                                          VisibilityRasterPipe   = VK_NULL_HANDLE;
    VkFramebuffer                                       VisibilityFramebuffer  = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, 3>                      IntegrationSets        { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    std::array<VkDescriptorSet, 6>                      ReconstructionSets     { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorSet                                     DisplaySet             = VK_NULL_HANDLE;
    VkDescriptorSet                                     RasterSet              = VK_NULL_HANDLE;

    // GPU / host-mapped buffers & textures
    DeviceBufferResource                                TracingUniformBuffer   {};
    DeviceBufferResource                                ReconstructUniformBuf  {};
    DeviceBufferResource                                DisplayUniformBuffer   {};
    DeviceBufferResource                                RasterUniformBuffer    {};
    DeviceBufferResource                                BoundsEntryBuffer      {};
    DeviceBufferResource                                TrianglePosBuffer      {};
    DeviceBufferResource                                TriangleNormBuffer     {};
    DeviceBufferResource                                MaterialRegisterBuffer    {};
    DeviceBufferResource                                LightRegisterBuffer      {};
    DeviceBufferResource                                SphereRegisterBuffer      {};
    DeviceBufferResource                                AccumulationBuffer     {};
    DeviceBufferResource                                ReconstructionBuffer   {};
    DeviceBufferResource                                SurfaceFeatureBuffer   {};
    DeviceBufferResource                                DisplayRgba8Buffer     {};
    DeviceImageResource                                 BlueNoiseTexture       {};
    DeviceImageResource                                 VisibilityIdTexture    {};
    DeviceImageResource                                 VisibilityDepthTexture {};
    bool                                                ProgramsReady         = false;
};

} // namespace Frontier::GeometricRaster
