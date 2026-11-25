/*============================================================================================================================================
                                                    FLUIDSIMULATIONPOSTOVERLAY.H
============================================================================================================================================*/
// 🧩 Stage D (Slice S17) 1:1 Screen-space simulation overlay uniform ABI & CPU reference mirror
//    (Heat Shimmer Layer 109, Particle Screen-Space Glitch Layer 217, Distortion / Gravitational Lensing Layer 218)
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js PostOverlayFragment (L1758-L1909) & VolumeRenderer.js L72-L176

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Fluid/FluidVolumeRenderer.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::Fluid
{

//------------------------------------------------------------------------------------------------------------------------
//                                              CONSTANTS & INPUT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kMaximumOverlayEffects = 16u;

enum class DistortionLensModeOrdinal : uint32_t
{
    Off       = 0u,
    BlackHole = 1u,
    Shockwave = 2u,
    Bubble    = 3u,
};

struct FluidShimmerInput
{
    std::array<float, 3> WorldCentreMeters { 0.0f, 0.0f, 0.0f };
    float                WorldRadiusMeters = 1.0f;
    float                Strength          = 0.0f;
    float                Frequency         = 14.0f;
    float                Speed             = 2.2f;
    float                Chromatic         = 0.0f;
};

struct ParticleOverlayEffectInput
{
    bool                      Visible            = true;
    bool                      Enabled            = true;
    std::array<float, 3>      PositionMeters     { 0.0f, 0.0f, 0.0f };
    float                     RadiusMeters       = 1.0f;
    DistortionLensModeOrdinal DistortionMode     = DistortionLensModeOrdinal::Off;
    float                     DistortionStrength = 0.0f;
    float                     DistortionRadius   = 0.28f;
    float                     DistortionHorizon  = 0.0f;
    float                     DistortionChromatic = 0.0f;
    float                     GlitchAmount       = 0.0f;
    float                     GlitchRate         = 14.0f;
    float                     GlitchScanlines    = 0.0f;
    float                     GlitchChromatic    = 0.0f;
};

struct ProjectedScreenSphere
{
    float UvX          = 0.5f;
    float UvY          = 0.5f;
    float ScreenRadius = 0.25f;
    bool  Valid        = false;
};

/// Matches SimulationPostOverlayUniformBlock in SimulationPostOverlay.slang byte-for-byte (1088 bytes).
struct alignas(16) SimulationPostOverlayUniformBlock
{
    std::array<float, 4>                 ViewportTime  { 640.0f, 360.0f, 0.0f, 0.0f };
    std::array<int32_t, 4>               EffectCounts  { 0, 0, 0, 0 }; // x = lensCount, y = glitchCount
    std::array<float, 4>                 ShimmerArea   { 0.5f, 0.5f, 0.35f, 0.0f };
    std::array<float, 4>                 ShimmerParams { 14.0f, 2.2f, 0.0f, 0.0f };
    std::array<std::array<float, 4>, 16> LensA         {};
    std::array<std::array<float, 4>, 16> LensB         {};
    std::array<std::array<float, 4>, 16> GlitchA       {};
    std::array<std::array<float, 4>, 16> GlitchB       {};
};
static_assert(sizeof(SimulationPostOverlayUniformBlock) == 1088u, "SimulationPostOverlayUniformBlock must be 1088 bytes");

//------------------------------------------------------------------------------------------------------------------------
//                                        UNIFORM PACKING & CPU OVERLAY COMPOSITE
//------------------------------------------------------------------------------------------------------------------------

/// Matches VolumeRenderer.js::hasActivePostOverlay (L72-L82).
[[nodiscard]] bool HasActiveSimulationPostOverlay(
    const FluidShimmerInput& shimmer,
    std::span<const ParticleOverlayEffectInput> particleEffects) noexcept;

/// Matches VolumeRenderer.js::projectWorldSphere (L104-L116).
[[nodiscard]] ProjectedScreenSphere ProjectWorldSphereToScreenUv(
    const std::array<float, 3>& worldPos,
    float worldRadius,
    const FluidRenderCamera& camera) noexcept;

/// Packs SimulationPostOverlayUniformBlock (VolumeRenderer.js L118-L176).
[[nodiscard]] SimulationPostOverlayUniformBlock BuildSimulationPostOverlayUniforms(
    const FluidShimmerInput& shimmer,
    std::span<const ParticleOverlayEffectInput> particleEffects,
    const FluidRenderCamera& camera,
    float simulationTimeSeconds) noexcept;

/// Exact CPU mirror of SimulationPostOverlay.slang (bilinear-sampled scene RGBA32F -> output RGBA32F).
void ApplySimulationPostOverlayCpu(
    const SimulationPostOverlayUniformBlock& uniforms,
    uint32_t width,
    uint32_t height,
    std::span<const float> sceneRgba32f,
    std::vector<float>& outRgba32f);

} // namespace Frontier::SimulationSpace::Fluid
