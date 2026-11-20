/*============================================================================================================================================
                                                            SHEENCOSINEINTEGRAL.H
============================================================================================================================================*/
// 🧩 32x32 RGBA16F (8 KB) Zeltner, Burley & Chiang (SIGGRAPH 2022) Linearly Transformed Cosine (LTC) cloth sheen lookup table

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t    SheenTableDimension  = 32u;                                                   // [texels] - 32 x 32 (CosTheta x Roughness)
inline constexpr std::size_t SheenTableEntryCount = SheenTableDimension * SheenTableDimension;             // [count]  - 1,024 texels
inline constexpr std::size_t SheenTableByteSize   = SheenTableEntryCount * sizeof(uint64_t);               // [B]      - Exact 8,192 B (8 KB RGBA16F)

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SheenLinearCosineCoefficients
{
    float InverseScaleA        = 1.0f; // [-] - Diagonal stretch M^-1_00 = M^-1_11 of grazing Charlie fiber lobe
    float InverseShearB        = 0.0f; // [-] - Off-diagonal backward/forward shear M^-1_02
    float DirectionalSheenR    = 0.0f; // [-] - Pre-integrated directional sheen reflectance R(mu, r) in [0, 1]
};

struct SheenLinearCosineSheet
{
    // 📝 Each uint64_t packs 4 IEEE-754 FP16 channels (RGBA16F): A_inv, B_inv, R_sheen, Reserved -> exact 8,192 bytes!
    std::array<uint64_t, SheenTableEntryCount> PackedRgba16fTexels {}; // [RGBA16F] - 32x32 8 KB GPU LUT payload
};

static_assert(sizeof(SheenLinearCosineSheet) == 8192u, "SheenLinearCosineSheet must occupy exactly 8,192 bytes (8 KB).");

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SheenLinearCosineSheet BuildSheenLinearCosineSheet() noexcept;

[[nodiscard]] const SheenLinearCosineSheet& InspectGlobalSheenLinearCosineSheet() noexcept;

[[nodiscard]] SheenLinearCosineCoefficients SampleSheenLinearCosineCoefficients(
    const SheenLinearCosineSheet& Sheet,
    float                         CosTheta,
    float                         SheenRoughness) noexcept;

} // namespace Frontier::MaterialSpace
