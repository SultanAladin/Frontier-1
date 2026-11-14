/*============================================================================================================================================
                                                           SURFACEANCHORSAMPLER.H
============================================================================================================================================*/
// 🧩 All 17 procedural 3D surface presets (0..16) + custom triangle-network area-weighted sampler for ParticleSpace origins

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceTriangleInput
{
    float VertexA[3] = { 0.0f, 0.0f, 0.0f }; // [m] - First triangle corner
    float VertexB[3] = { 0.0f, 0.0f, 0.0f }; // [m] - Second triangle corner
    float VertexC[3] = { 0.0f, 0.0f, 0.0f }; // [m] - Third triangle corner
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string_view QuerySurfacePresetLabel(SurfacePresetCategory Preset) noexcept;

/// Samples deterministic surface/volume/ridge anchors for any of the 17 procedural SurfacePresetCategory targets (0..16).
[[nodiscard]] std::vector<ParticleAnchorSample> SampleProceduralSurfaceAnchors(
    const SurfaceAnchorOrigin& Origin,
    uint32_t                   SampleCount,
    uint32_t                   Seed = 1337u);

/// Area-weighted surface sampler over arbitrary imported triangles (e.g. from GeometrySpace / OBJ / STL).
[[nodiscard]] std::vector<ParticleAnchorSample> SampleTriangleSurfaceAnchors(
    std::span<const SurfaceTriangleInput> Triangles,
    const SurfaceAnchorOrigin&            TransformOrigin,
    uint32_t                              SampleCount,
    uint32_t                              Seed = 1337u);

} // namespace Frontier::ParticleSpace
