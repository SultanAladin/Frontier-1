/*============================================================================================================================================
                                                        PARTICLEGPURENDERER.H
============================================================================================================================================*/
// 🧩 ParticleSpace GPU sprite/streak uniform builder, draw-batch planner & bit-accurate CPU mirror of ParticleShadingCommon + 18 cutouts
// Reference: Experimental/FluidEditor/Engine/ParticleShaders.js L753-L966, L1088-L1340 & ParticleSimulation.js L340-L655

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/CarrierSolidRegistry.h"
#include "Engine/ParticleSpace/ParticleCloudTypes.h"
#include "Engine/ParticleSpace/ParticleGpuSimulation.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 GPU ABI DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Matches ParticleShadeUniformBlock in ParticleShadingCommon.slang byte-for-byte (192 bytes, 16-byte aligned).
struct alignas(16) ParticleShadeUniformBlock
{
    std::array<std::array<float, 4>, 8> SwatchStops        {};
    std::array<float, 4>                ModeRampSwatch     { 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 4>                ColourJitterCycle  { 0.0f, 0.0f, 0.5f, 1.0f };
    std::array<float, 4>                LifeRangeFlicker   { 3.0f, 0.6f, 6.0f, 1.0f };
    std::array<float, 4>                TimingRegionHeight { 0.0f, 0.0f, 4.0f, 0.0f };
};
static_assert(sizeof(ParticleShadeUniformBlock) == 192u, "ParticleShadeUniformBlock must be 192 bytes");

/// Matches ParticleSpriteUniformBlock in ParticleSpriteRasterization.slang byte-for-byte (512 bytes, 16-byte aligned).
struct alignas(16) ParticleSpriteUniformBlock
{
    ParticleShapeUniformBlock Shape {};
    ParticleShadeUniformBlock Shade {};
    std::array<float, 16>     ViewProjection         { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    std::array<float, 4>      CameraPositionFocal    { 0.0f, 0.0f, 10.0f, 940.0f };
    std::array<float, 4>      CameraForwardViewportX { 0.0f, 0.0f, -1.0f, 1280.0f };
    std::array<float, 4>      ViewportYSizeParams    { 720.0f, 3.0f, 1.0f, 0.3f };
    std::array<float, 4>      PixelDofParams         { 1.0f, 1.0f, 0.0f, 0.5f };
    std::array<float, 4>      HaloSpriteStyleTrail   { 1.0f, 1.0f, 1.0f, 0.0f };
};
static_assert(sizeof(ParticleSpriteUniformBlock) == 512u, "ParticleSpriteUniformBlock must be 512 bytes");

/// Matches ParticleRibbonUniformBlock in ParticleRibbonRasterization.slang byte-for-byte (512 bytes, 16-byte aligned).
struct alignas(16) ParticleRibbonUniformBlock
{
    ParticleShapeUniformBlock Shape {};
    ParticleShadeUniformBlock Shade {};
    std::array<float, 16>     ViewProjection         { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    std::array<float, 4>      CameraPositionFocal    { 0.0f, 0.0f, 10.0f, 940.0f };
    std::array<float, 4>      CameraForwardViewportX { 0.0f, 0.0f, -1.0f, 1280.0f };
    std::array<float, 4>      ViewportYTrailLayout   { 720.0f, 24.0f, 0.0f, 2.0f };
    std::array<float, 4>      RibbonWidthTaper       { 1.0f, 0.01f, 0.0f, 0.6f };
    std::array<float, 4>      RibbonFadeColourHalo   { 0.7f, 0.0f, 1.0f, 1.0f };
};
static_assert(sizeof(ParticleRibbonUniformBlock) == 512u, "ParticleRibbonUniformBlock must be 512 bytes");

struct ParticleRibbonDrawCall
{
    std::string                NodeId       {};
    ParticleBlendOrdinal       Blend        = ParticleBlendOrdinal::Alpha;
    uint32_t                   VertexCount  = 0u;
    bool                       IsHaloCall   = false;
    ParticleRibbonUniformBlock UniformBlock {};
};

inline constexpr std::array<float, 3> kDefaultBackingColour { 0.62f, 0.86f, 1.0f }; // ParticleSimulation.js L69
inline constexpr uint32_t             kMaximumCarrierVertices = 720000u;             // ParticleSimulation.js L423

/// Matches ParticleCarrierSolidUniformBlock in ParticleCarrierSolidRasterization.slang byte-for-byte (544 bytes, 16-byte aligned).
struct alignas(16) ParticleCarrierSolidUniformBlock
{
    ParticleShapeUniformBlock Shape {};
    ParticleShadeUniformBlock Shade {};
    std::array<float, 16>     ViewProjection          { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    std::array<float, 4>      CameraPositionFocal     { 0.0f, 0.0f, 10.0f, 940.0f };
    std::array<float, 4>      CameraForwardPixelRatio { 0.0f, 0.0f, -1.0f, 1.0f };
    std::array<float, 4>      SolidCountsStride       { 3.0f, 1.0f, 1.0f, 0.0f };
    std::array<float, 4>      BackingParams           { 0.0f, 0.5f, 0.0f, 0.0f };
    std::array<float, 4>      SizeParams              { 3.0f, 1.0f, 0.3f, 1.0f };
    std::array<float, 4>      SpinTiltRoughMetal      { 1.2f, 0.15f, 0.35f, 0.0f };
    std::array<float, 4>      BackingColourGlow       { 0.62f, 0.86f, 1.0f, 0.0f };
};
static_assert(sizeof(ParticleCarrierSolidUniformBlock) == 544u, "ParticleCarrierSolidUniformBlock must be 544 bytes");

enum class ParticleSolidTopologyOrdinal : uint32_t
{
    TriangleList = 0u,
    LineList     = 1u,
};

struct ParticleCarrierSolidDrawCall
{
    std::string                      NodeId          {};
    std::string                      SolidName       {};
    std::string                      CustomSolidId   {};
    ParticleSolidTopologyOrdinal     Topology        = ParticleSolidTopologyOrdinal::TriangleList;
    ParticleBlendOrdinal             Blend           = ParticleBlendOrdinal::Alpha;
    bool                             EnableDepthTest = false;
    bool                             WriteDepth      = false;
    bool                             IsBackingCall   = false;
    uint32_t                         BackingMode     = 0u; // 0=carrier, 1=ObsidianGlossy, 2=BrushedChrome, 3=GhostWireframe
    uint32_t                         CarrierStride   = 1u;
    uint32_t                         InstanceCount   = 0u;
    uint32_t                         VertexCount     = 0u;
    ParticleCarrierSolidUniformBlock UniformBlock    {};
};

struct CarrierSolidVertexCpuResult
{
    std::array<float, 3> WorldPos    { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> WorldNormal { 0.0f, 1.0f, 0.0f };
    std::array<float, 3> LocalPos    { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> Colour      { 1.0f, 1.0f, 1.0f };
    float                Alpha       = 0.0f;
    float                Ridge       = 0.0f;
    bool                 Culled      = false;
};

struct CarrierSolidFragmentCpuResult
{
    std::array<float, 3> Rgb       { 0.0f, 0.0f, 0.0f };
    float                Alpha     = 0.0f;
    bool                 Discarded = false;
};

/// Matches ParticleStrandUniformBlock in ParticleStrandRasterization.slang byte-for-byte (352 bytes, 16-byte aligned).
struct alignas(16) ParticleStrandUniformBlock
{
    std::array<float, 16>               ViewProjection               { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    std::array<std::array<float, 4>, 8> SwatchStops                  {};
    std::array<float, 4>                CameraPositionFocal          { 0.0f, 0.0f, 10.0f, 940.0f };
    std::array<float, 4>                CameraForwardViewportX       { 0.0f, 0.0f, -1.0f, 1280.0f };
    std::array<float, 4>                OriginRadius                 { 0.0f, 0.0f, 0.0f, 0.5f };
    std::array<float, 4>                AxisTime                     { 0.0f, 1.0f, 0.0f, 0.0f };
    std::array<float, 4>                GridCentreHalfX              { 0.0f, 0.0f, 0.0f, 2.0f };
    std::array<float, 4>                GridHalfYZForceGas           { 2.0f, 2.0f, 0.0f, 0.0f };
    std::array<float, 4>                ModeVariantCounts            { 0.0f, 0.0f, 36.0f, 36.0f };
    std::array<float, 4>                SegmentsDetailViewportSwatch { 14.0f, 14.0f, 720.0f, 1.0f };
    std::array<float, 4>                SagThicknessSwaySpan         { 0.32f, 0.032f, 0.85f, 1.8f };
    std::array<float, 4>                GlowBrightnessEnvelope       { 0.65f, 1.0f, 1.0f, 0.0f };
};
static_assert(sizeof(ParticleStrandUniformBlock) == 352u, "ParticleStrandUniformBlock must be 352 bytes");

struct ParticleStrandDrawCall
{
    std::string                  NodeId         {};
    ParticleStrandOrdinal        Mode           = ParticleStrandOrdinal::Off;
    ParticleStrandVariantOrdinal Variant        = ParticleStrandVariantOrdinal::Primary;
    ParticleBlendOrdinal         Blend          = ParticleBlendOrdinal::Alpha;
    uint32_t                     StrandTotal    = 0u;
    uint32_t                     StrandSegments = 0u;
    uint32_t                     VertexCount    = 0u;
    ParticleStrandUniformBlock   UniformBlock   {};
};

struct StrandEndpointsCpuResult
{
    std::array<float, 3> PointA     { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> PointB     { 0.0f, 0.5f, 0.0f };
    float                Sag        = 0.0f;
    float                WidthScale = 1.0f;
    float                PinB       = 0.0f;
    float                StyleTag   = 0.0f;
    float                ColourT    = 0.0f;
    float                Visible    = 1.0f;
};

struct StrandFragmentCpuResult
{
    std::array<float, 3> Rgb       { 0.0f, 0.0f, 0.0f };
    float                Alpha     = 0.0f;
    bool                 Discarded = false;
};

struct ParticleCameraView
{
    std::array<float, 16> ViewProjection    { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    std::array<float, 3>  CameraPosition    { 0.0f, 0.0f, 10.0f };
    std::array<float, 3>  CameraForward     { 0.0f, 0.0f, -1.0f };
    float                 ViewportWidthPx   = 1280.0f;
    float                 ViewportHeightPx  = 720.0f;
    float                 VerticalFovRad    = 0.733f;
    float                 PixelRatio        = 1.0f;
};

enum class ParticleDrawTopologyOrdinal : uint32_t
{
    SpriteQuads = 0u, // 6 vertices per particle (triangle list)
    StreakLines = 1u, // 2 vertices per particle (line list)
};

struct ParticleSpriteDrawCall
{
    std::string                 NodeId       {};
    ParticleDrawTopologyOrdinal Topology     = ParticleDrawTopologyOrdinal::SpriteQuads;
    ParticleBlendOrdinal        Blend        = ParticleBlendOrdinal::Alpha;
    uint32_t                    VertexCount  = 0u;
    bool                        IsHaloCall   = false;
    ParticleSpriteUniformBlock  UniformBlock {};
};

struct ParticleShadeCpuResult
{
    std::array<float, 3> Colour         { 1.0f, 1.0f, 1.0f };
    float                Life           = 0.0f;
    float                Alpha          = 0.0f;
    float                Flicker        = 1.0f;
    float                Linger         = 1.0f;
    float                DerezReleased  = 0.0f;
    float                EffectorWeight = 0.0f;
};

struct SpriteCutoutCpuResult
{
    std::array<float, 3> Rgb       { 0.0f, 0.0f, 0.0f };
    float                Alpha     = 0.0f;
    bool                 Discarded = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                        UNIFORM BUILDING & CPU MIRRORS
//------------------------------------------------------------------------------------------------------------------------

/// Resolves ColourRamp name ("Turbo", "Engineering", "Plasma", "Diverging") to its shader ordinal (0..3).
[[nodiscard]] uint32_t ResolveAnalyticRampOrdinal(std::string_view rampName) noexcept;

/// Populates ParticleShadeUniformBlock from cloud parameters, simulation time, and region height.
[[nodiscard]] ParticleShadeUniformBlock BuildParticleShadeUniforms(
    const ParticleCloudParameters& params,
    float simulationTimeSeconds,
    float regionHeightMeters) noexcept;

/// Populates ParticleSpriteUniformBlock for a specific sprite/streak/halo render call.
[[nodiscard]] ParticleSpriteUniformBlock BuildParticleSpriteUniforms(
    const ParticleCloudParameters& params,
    uint32_t textureSide,
    uint32_t anchorWidth,
    uint32_t anchorCount,
    const ParticleCameraView& camera,
    float simulationTimeSeconds,
    float regionHeightMeters,
    float haloScale = 1.0f,
    float haloAlpha = 1.0f,
    int32_t spriteOverride = -1) noexcept;

/// Bit-accurate CPU mirror of ParticleShadingCommon.slang::EvaluateAnalyticRamp (0..3).
[[nodiscard]] std::array<float, 3> EvaluateAnalyticRampCpu(uint32_t rampMode, float t) noexcept;

/// Bit-accurate CPU mirror of ParticleShadingCommon.slang::EvaluateParticleShade (7 modes, derez glow, flicker).
[[nodiscard]] ParticleShadeCpuResult EvaluateParticleShadeCpu(
    const ParticleShapeUniformBlock& shape,
    const ParticleShadeUniformBlock& shade,
    std::span<const float> anchorTexelRgba,
    int32_t particleIndex,
    const std::array<float, 4>& currentPositionAge,
    const std::array<float, 4>& velocitySeed) noexcept;

/// Bit-accurate CPU mirror of ParticleSpriteRasterization.slang::ParticleSpriteFragmentEntry (all 18 cutouts 0..17).
[[nodiscard]] SpriteCutoutCpuResult EvaluateSpriteCutoutCpu(
    uint32_t spriteOrdinal,
    uint32_t styleOrdinal,
    const std::array<float, 4>& inputColour,
    float soft,
    float across,
    const std::array<float, 4>& extra,
    const std::array<float, 2>& pointCoord) noexcept;

/// Computes exact sprite pixel diameter (1..256px) and DOF/halo alpha multiplier matching ParticleRenderVertex.
void ComputeSpriteDofMetricsCpu(
    const ParticleSpriteUniformBlock& uniforms,
    float baseSize,
    float depthMeters,
    float& outPixelDiameter,
    float& outAlphaMultiplier) noexcept;

/// Populates ParticleRibbonUniformBlock for Catmull-Rom spline ribbon rendering (ParticleSimulation.js L484-L543).
[[nodiscard]] ParticleRibbonUniformBlock BuildParticleRibbonUniforms(
    const ParticleCloudParameters& params,
    const ParticleCloudStateRecord& record,
    const ParticleCameraView& camera,
    float simulationTimeSeconds,
    float regionHeightMeters,
    float haloScale = 1.0f,
    float haloAlpha = 1.0f) noexcept;

/// Evaluates cubic Catmull-Rom spline matching ParticleRibbonRasterization.slang::EvaluateCatmullRom.
[[nodiscard]] std::array<float, 3> EvaluateCatmullRomCpu(
    const std::array<float, 3>& p0,
    const std::array<float, 3>& p1,
    const std::array<float, 3>& p2,
    const std::array<float, 3>& p3,
    float u) noexcept;

/// Reads trail history point k (0 = live position, 1..TrailLength = ring buffer back-trace).
[[nodiscard]] std::array<float, 3> SampleTrailRingPointCpu(
    const ParticleCloudStateRecord& record,
    uint32_t particleIndex,
    int32_t k) noexcept;

/// Populates ParticleCarrierSolidUniformBlock for either a backing surface (mode 1..3) or instanced Object3D carrier (mode 0).
[[nodiscard]] ParticleCarrierSolidUniformBlock BuildParticleCarrierSolidUniforms(
    const ParticleCloudParameters& params,
    const ParticleCloudStateRecord& record,
    uint32_t solidVertexCount,
    uint32_t solidTriangleCount,
    uint32_t carrierStride,
    bool isBacking,
    bool isWire,
    uint32_t backingMode,
    const ParticleCameraView& camera,
    float simulationTimeSeconds,
    float regionHeightMeters,
    const std::array<float, 3>& backingColour = kDefaultBackingColour) noexcept;

/// Bit-accurate CPU mirror of ParticleCarrierSolidRasterization.slang::ParticleCarrierSolidVertexEntry.
[[nodiscard]] CarrierSolidVertexCpuResult EvaluateCarrierSolidVertexCpu(
    const ParticleCarrierSolidUniformBlock& uniforms,
    std::span<const ParticleStateGpuRecord> states,
    std::span<const float> anchorTexelRgba,
    std::span<const float> solidTexelRgba,
    uint32_t vertexId) noexcept;

/// Bit-accurate CPU mirror of ParticleCarrierSolidRasterization.slang::ParticleCarrierSolidFragmentEntry (modes 0..3).
[[nodiscard]] CarrierSolidFragmentCpuResult EvaluateCarrierSolidFragmentCpu(
    const ParticleCarrierSolidUniformBlock& uniforms,
    const CarrierSolidVertexCpuResult& vertex) noexcept;

/// Returns true when any active cloud needs a depth buffer clear (ParticleSimulation.js L566-L567).
[[nodiscard]] bool RequiresParticleDepthBuffer(std::span<const ParticleCloudNode> clouds) noexcept;

/// Populates ParticleStrandUniformBlock (11 modes x 2 variants, ParticleSimulation.js L449-L505).
[[nodiscard]] ParticleStrandUniformBlock BuildParticleStrandUniforms(
    const ParticleCloudParameters& params,
    const ParticleSimulationRegion& region,
    const ParticleCameraView& camera,
    float simulationTimeSeconds,
    uint32_t forceRecordCount = 0u,
    bool hasGasVolume = false) noexcept;

/// Bit-accurate CPU mirror of ParticleStrandRasterization.slang::EvalStrandEndpoints (modes 1..11).
[[nodiscard]] StrandEndpointsCpuResult EvaluateStrandEndpointsCpu(
    const ParticleStrandUniformBlock& uniforms,
    int32_t slot) noexcept;

/// Bit-accurate CPU mirror of ParticleStrandRasterization.slang::EvalStrandPoint (sag, breeze, lightning jitter).
[[nodiscard]] std::array<float, 3> EvaluateStrandPointCpu(
    const ParticleStrandUniformBlock& uniforms,
    int32_t slot,
    const StrandEndpointsCpuResult& endpoints,
    float u,
    const std::array<float, 3>& externalWind = { 0.0f, 0.0f, 0.0f }) noexcept;

/// Bit-accurate CPU mirror of ParticleStrandRasterization.slang::ParticleStrandFragmentEntry (all 8 material tags).
[[nodiscard]] StrandFragmentCpuResult EvaluateStrandFragmentCpu(
    const ParticleStrandUniformBlock& uniforms,
    const std::array<float, 3>& inputColour,
    float along,
    float across,
    float styleTag,
    float seed,
    float inputAlpha) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                PARTICLEGPURENDERER
//------------------------------------------------------------------------------------------------------------------------

/// Plans sprite, streak, ribbon, backing surface, 3D carrier, and strand GPU render calls across all visible particle clouds.
class ParticleGpuRenderer
{
public:
    ParticleGpuRenderer() noexcept = default;
    ~ParticleGpuRenderer() noexcept = default;

    [[nodiscard]] std::vector<ParticleSpriteDrawCall> BuildSpriteRenderCalls(
        std::span<const ParticleCloudNode> clouds,
        const ParticleGpuSimulation& simulation,
        const ParticleCameraView& camera,
        float simulationTimeSeconds,
        float regionHeightMeters) const;

    [[nodiscard]] std::vector<ParticleRibbonDrawCall> BuildRibbonRenderCalls(
        std::span<const ParticleCloudNode> clouds,
        const ParticleGpuSimulation& simulation,
        const ParticleCameraView& camera,
        float simulationTimeSeconds,
        float regionHeightMeters) const;

    [[nodiscard]] std::vector<ParticleCarrierSolidDrawCall> BuildCarrierSolidRenderCalls(
        std::span<const ParticleCloudNode> clouds,
        const ParticleGpuSimulation& simulation,
        const CarrierSolidRegistry& solidRegistry,
        const ParticleCameraView& camera,
        float simulationTimeSeconds,
        float regionHeightMeters,
        const std::array<float, 3>& backingColour = kDefaultBackingColour) const;

    [[nodiscard]] std::vector<ParticleStrandDrawCall> BuildStrandRenderCalls(
        std::span<const ParticleCloudNode> clouds,
        const ParticleSimulationRegion& region,
        const ParticleCameraView& camera,
        float simulationTimeSeconds,
        uint32_t forceRecordCount = 0u,
        bool hasGasVolume = false) const;
};

} // namespace Frontier::ParticleSpace
