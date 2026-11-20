/*============================================================================================================================================
                                                         DIRECTIONALALBEDOINTEGRAL.H
============================================================================================================================================*/
// 🧩 32x32 RG16F (4 KB) pre-integrated GGX directional & hemispherical albedo sheet for Turquin (2019) multi-scattering compensation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/LinearColors.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t    AlbedoTableDimension = 32u;                                                   // [texels] - 32 x 32 (CosTheta x Roughness)
inline constexpr std::size_t AlbedoTableEntryCount = AlbedoTableDimension * AlbedoTableDimension;          // [count]  - 1,024 texels
inline constexpr std::size_t AlbedoTableByteSize   = AlbedoTableEntryCount * sizeof(uint32_t);             // [B]      - Exact 4,096 B (4 KB RG16F)

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DirectionalAlbedoLookup
{
    float DirectionalAlbedoE = 1.0f; // [-] - E(mu, roughness) single-scatter directional reflectance in [0, 1]
    float AverageAlbedoEavg  = 1.0f; // [-] - E_avg(roughness) = 2 * integral_0^1 E(mu) * mu dmu in [0, 1]
};

struct DirectionalAlbedoSheet
{
    // 📝 Each uint32_t packs two IEEE-754 FP16 values: lo16 = E(mu, r), hi16 = E_avg(r) -> exact 4,096 bytes!
    std::array<uint32_t, AlbedoTableEntryCount> PackedRg16fTexels {}; // [RG16F] - 32x32 4 KB GPU LUT payload
};

static_assert(sizeof(DirectionalAlbedoSheet) == 4096u, "DirectionalAlbedoSheet must occupy exactly 4,096 bytes (4 KB).");

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Deterministically integrate the 32x32 RG16F GGX directional & average albedo table via VNDF Quasi-Monte-Carlo
[[nodiscard]] DirectionalAlbedoSheet BuildDirectionalAlbedoSheet() noexcept;

// 📝 Inspect the process-wide immutable 4 KB DirectionalAlbedoSheet
[[nodiscard]] const DirectionalAlbedoSheet& InspectGlobalDirectionalAlbedoSheet() noexcept;

// 📝 Bilinearly sample E(mu, roughness) and E_avg(roughness) from a 4 KB sheet
[[nodiscard]] DirectionalAlbedoLookup SampleDirectionalAlbedo(
    const DirectionalAlbedoSheet& Sheet,
    float                         CosTheta,
    float                         PerceptualRoughness) noexcept;

// 📝 Evaluate Turquin (2019) / OpenPBR 1.1 reciprocal multi-scattering energy compensation lobe f_ms(wo, wi)
[[nodiscard]] Mathematics::LinearRGB EvaluateTurquinMultiScatterLobe(
    const DirectionalAlbedoSheet& Sheet,
    float                         CosThetaOut,
    float                         CosThetaIn,
    float                         PerceptualRoughness,
    Mathematics::LinearRGB        AverageFresnelFavg) noexcept;

} // namespace Frontier::MaterialSpace
