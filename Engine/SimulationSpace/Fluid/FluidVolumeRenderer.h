/*============================================================================================================================================
                                                        FLUIDVOLUMERENDERER.H
============================================================================================================================================*/
// 🧩 Stage D (Slice S17) 1:1 Volumetric raymarcher uniform ABI & bit-accurate CPU reference renderer
//    (Wrenninge 3-octave scattering, 2-lobe HG, 1.35-growth sun shadows, powder darkening, fire-lit smoke/floor, ACES, transparent flipbook)
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js SceneFragment (L1129-L1384) & VolumeRenderer.js L260-L310

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"
#include "Engine/SimulationSpace/Fluid/FluidRegionTypes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::SimulationSpace::Fluid
{

//------------------------------------------------------------------------------------------------------------------------
//                                              VOLUME RENDER SETTINGS & GPU UNIFORMS
//------------------------------------------------------------------------------------------------------------------------

struct FluidVolumeRenderSettings
{
    bool                 Visible        = true;
    bool                 Transparent    = false; // True for flipbook RGBA8 transparent bake
    bool                 VoxelPreview   = false;
    int32_t              SelectedSphere = -1;
    std::array<float, 3> LightDirection { 0.45f, 0.80f, 0.35f };
    std::array<float, 3> LightColour    { 1.35f, 1.22f, 1.05f };
    float                LightIntensity = 1.6f;
    std::array<float, 3> Albedo         { 0.86f, 0.88f, 0.91f };
    std::array<float, 3> AmbientColour  { 0.16f, 0.18f, 0.22f };
    float                DensityScale   = 1.2f;
    float                Absorption     = 2.6f;
    float                Anisotropy     = 0.55f;
    float                FireLight      = 1.0f;
    float                FireIntensity  = 3.0f;
    std::array<float, 3> FireEmber      { 0.90f, 0.14f, 0.02f };
    std::array<float, 3> FireFlame      { 1.00f, 0.55f, 0.08f };
    std::array<float, 3> FireCore       { 1.00f, 0.96f, 0.82f };
    float                FireThreshold  = 1.0f;
    float                FireSoftness   = 3.5f;
    float                FireOpacity    = 0.35f;
    float                Exposure       = 1.05f;
    int32_t              Steps          = 72;
    int32_t              ShadowSteps    = 8;
};

struct FluidRenderCamera
{
    std::array<float, 3>  PositionMeters        { 0.0f, 1.1f, 3.6f };
    std::array<float, 16> ViewProjection        {}; // Column-major 4x4 (matches WebGL / OrbitCamera.js)
    std::array<float, 16> InverseViewProjection {}; // Column-major 4x4 inverse
    float                 FovYRadians           = 0.733f;
    uint32_t              ViewportWidth         = 640u;
    uint32_t              ViewportHeight        = 360u;
};

/// Matches FluidVolumeRaymarchUniformBlock in FluidVolumeRaymarch.slang byte-for-byte (400 bytes).
struct alignas(16) FluidVolumeRaymarchUniformBlock
{
    FluidVolumeUniformBlock Volume                  {}; // 160 B
    std::array<float, 16>   InverseViewProjection   {}; //  64 B
    std::array<float, 4>    CameraPositionFireLight {}; // xyz = cameraPos, w = fireLight
    std::array<float, 4>    ViewportGroundExposure  {}; // xy = viewport, z = groundHeight, w = exposure
    std::array<float, 4>    LightDirectionIntensity {}; // xyz = norm lightDir, w = lightIntensity
    std::array<float, 4>    LightColourAnisotropy   {}; // xyz = lightColour, w = min(0.95, anisotropy)
    std::array<float, 4>    AlbedoDensityScale      {}; // xyz = albedo, w = densityScale
    std::array<float, 4>    AmbientColourAbsorption {}; // xyz = ambientColour, w = absorption
    std::array<float, 4>    FireEmberIntensity      {}; // xyz = fireEmber, w = fireIntensity
    std::array<float, 4>    FireFlameThreshold      {}; // xyz = fireFlame, w = fireThreshold
    std::array<float, 4>    FireCoreSoftness        {}; // xyz = fireCore, w = fireSoftness
    std::array<float, 4>    FireOpacitySteps        {}; // x = fireOpacity, y = steps, z = shadowSteps, w = showVolume
    std::array<int32_t, 4>  FlagsSelection          {}; // x = transparent, y = voxelPreview, z = tickCounter, w = selectedSphere
};
static_assert(sizeof(FluidVolumeRaymarchUniformBlock) == 400u, "FluidVolumeRaymarchUniformBlock must be 400 bytes");

//------------------------------------------------------------------------------------------------------------------------
//                                        UNIFORM BUILDER & CPU REFERENCE RAYMARCHER
//------------------------------------------------------------------------------------------------------------------------

/// Builds a look-at perspective FluidRenderCamera with exact column-major ViewProjection & InverseViewProjection.
[[nodiscard]] FluidRenderCamera BuildFluidPerspectiveCamera(
    const std::array<float, 3>& eyeMeters,
    const std::array<float, 3>& targetMeters,
    float fovYRadians,
    uint32_t width,
    uint32_t height,
    float nearPlane = 0.05f,
    float farPlane  = 100.0f) noexcept;

[[nodiscard]] FluidVolumeRaymarchUniformBlock BuildFluidVolumeRaymarchUniforms(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeRenderSettings& settings,
    const FluidRenderCamera& camera,
    bool isSparse,
    uint32_t tickCounter = 0u) noexcept;

/// Exact CPU mirrors of SceneFragment shading primitives.
[[nodiscard]] std::array<float, 3> EvaluateFireRadianceCpu(
    const FluidVolumeRaymarchUniformBlock& uniforms,
    float temperature) noexcept;

[[nodiscard]] float EvaluateFirePresenceCpu(
    const FluidVolumeRaymarchUniformBlock& uniforms,
    float temperature) noexcept;

[[nodiscard]] float EvaluateSmokePhaseCpu(float cosTheta, float g) noexcept;

[[nodiscard]] std::array<float, 3> EvaluateAcesFilmCpu(const std::array<float, 3>& rgb) noexcept;

[[nodiscard]] float EvaluateSunOpticalDepthCpu(
    const FluidVolumeRaymarchUniformBlock& uniforms,
    const FluidVolumeGridCpu& state,
    const std::array<float, 3>& worldPos,
    const std::array<float, 3>& boxMin,
    const std::array<float, 3>& boxMax) noexcept;

/// Evaluates one pixel (pixelX + 0.5, pixelY + 0.5) using the exact SceneFragment Wrenninge raymarcher.
[[nodiscard]] std::array<float, 4> RenderFluidVolumePixelCpu(
    const FluidVolumeRaymarchUniformBlock& uniforms,
    const FluidVolumeGridCpu& state,
    const FluidVolumeGridCpu& pressure,
    float pixelX,
    float pixelY) noexcept;

/// Renders a full RGBA32F viewport image (row-major, width * height * 4 floats) & optional RGBA8 flipbook buffer.
void RenderFluidVolumeImageCpu(
    const FluidVolumeRaymarchUniformBlock& uniforms,
    const FluidVolumeGridCpu& state,
    const FluidVolumeGridCpu& pressure,
    uint32_t width,
    uint32_t height,
    std::vector<float>& outRgba32f,
    std::vector<uint8_t>* outRgba8 = nullptr);

} // namespace Frontier::SimulationSpace::Fluid
