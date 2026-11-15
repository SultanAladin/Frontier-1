/*============================================================================================================================================
                                                          COMPACTMATERIALREGISTER.H
============================================================================================================================================*/
// 🧩 128-byte cache-aligned bindless GPU material register and wave-uniform ActiveLobeMask table (Zero G-Buffer Bloat)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum ActiveMaterialLobeBits : uint32_t
{
    LobeMaskNone                   = 0u,          // [-] - Unlit / empty mask
    LobeDiffuseEON                 = 1u << 0u,    // [-] - L1: Portsmouth 2025 Energy-Preserving Oren-Nayar
    LobeSpecularMetalGGX           = 1u << 1u,    // [-] - L2: Anisotropic VNDF GGX + F0/F82 + Turquin MS
    LobeClearcoat                  = 1u << 2u,    // [-] - L3: Clearcoat reflection + Beer-Lambert + TIR
    LobeThinFilmAiry               = 1u << 3u,    // [-] - L4: Nanometer Airy wave-optics iridescence
    LobeGlintMicroflakes           = 1u << 4u,    // [-] - L5: Constant-time 4D multiscale glinty microflakes
    LobeDielectricTransmission     = 1u << 5u,    // [-] - L6: Rough GGX BTDF + Abbe dispersion
    LobeSubsurfaceDiffusion        = 1u << 6u,    // [-] - L7: Burley normalized diffusion + volumetric walk
    LobeClothSheenLTC              = 1u << 7u,    // [-] - L8: Zeltner LTC multiple-scattering cloth sheen
    LobeFiberBCSDF                 = 1u << 8u,    // [-] - L9: Chiang/Marschner R + TT + TRT fiber BCSDF
    LobePorosityWetness            = 1u << 9u,    // [-] - L10a: Relative-IOR porous water film darkening
    LobeBlackbodyEmission          = 1u << 10u,   // [-] - L10b: Planckian Kelvin & explicit luminous exitance
    FlagThinWalledSlab             = 1u << 16u,   // [-] - Thin-walled glazing pane without ray refraction offset
    FlagAnisotropicFrame           = 1u << 17u,   // [-] - Non-zero tangent anisotropy active
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact 128-byte (two 64B cache lines, 8 x float4) GPU bindless material register indexed by uint16 MaterialSlot
struct alignas(16) CompactMaterialEntry
{
    // Row 0 (Bytes 0..15): Header & substrate reflectance
    uint32_t ActiveLobeMask                  = 0u;   // [bits] - Bitfield of active optical lobes for wave-uniform branching
    uint32_t BaseColorRgHalf                 = 0u;   // [fp16] - BaseColor.R (lo16) | BaseColor.G (hi16)
    uint32_t BaseColorBAndWeightHalf         = 0u;   // [fp16] - BaseColor.B (lo16) | BaseWeight (hi16)
    uint32_t DiffuseSpecRoughnessHalf     = 0u;   // [fp16] - DiffuseRoughness (lo16) | SpecularRoughness (hi16)

    // Row 1 (Bytes 16..31): L2 Specular, Metalness, Anisotropy & F82 Tint
    uint32_t MetalnessSpecWeightHalf      = 0u;   // [fp16] - Metalness (lo16) | SpecularWeight (hi16)
    uint32_t AnisotropyRotationHalf       = 0u;   // [fp16] - Anisotropy (lo16) | RotationRadians (hi16)
    uint32_t DielectricIorEdgeTintRHalf   = 0u;   // [fp16] - DielectricIOR (lo16) | EdgeTintF82.R (hi16)
    uint32_t EdgeTintGbHalf                  = 0u;   // [fp16] - EdgeTintF82.G (lo16) | EdgeTintF82.B (hi16)

    // Row 2 (Bytes 32..47): L3 Clearcoat & L4 Thin-Film Iridescence
    uint32_t CoatWeightRoughnessHalf      = 0u;   // [fp16] - CoatWeight (lo16) | CoatRoughness (hi16)
    uint32_t CoatAnisoIorHalf             = 0u;   // [fp16] - CoatAnisotropy (lo16) | CoatIOR (hi16)
    uint32_t CoatTintRgHalf                  = 0u;   // [fp16] - CoatAbsorptionTint.R (lo16) | CoatAbsorptionTint.G (hi16)
    uint32_t CoatTintBAndThicknessHalf       = 0u;   // [fp16] - CoatAbsorptionTint.B (lo16) | CoatThicknessMillimeter (hi16)

    // Row 3 (Bytes 48..63): L4 Thin-Film + L5 Constant-Time Glinty Microflakes
    float    ThinFilmThicknessNanometers     = 0.0f; // [nm]   - Full FP32 Airy optical thickness [0, 2000]
    uint32_t ThinFilmWeightIorHalf        = 0u;   // [fp16] - ThinFilmWeight (lo16) | ThinFilmIOR (hi16)
    uint32_t FlakeCoverageRoughHalf            = 0u;   // [fp16] - FlakeCoverage (lo16) | FlakeIntrinsicRoughness (hi16)
    uint32_t FlakeSpreadLogDensityHalf    = 0u;   // [fp16] - FlakeOrientationSpread (lo16) | FlakeLogDensityPerSqM (hi16)

    // Row 4 (Bytes 64..79): L5 Glinty Tints + L6 Dielectric Transmission
    uint32_t FlakePrimaryRgHalf              = 0u;   // [fp16] - FlakePrimaryTint.R (lo16) | FlakePrimaryTint.G (hi16)
    uint32_t FlakePrimaryBAndBurialHalf      = 0u;   // [fp16] - FlakePrimaryTint.B (lo16) | FlakeBurialDepth (hi16)
    uint32_t FlakeHorizonRgHalf              = 0u;   // [fp16] - FlakeHorizonTint.R (lo16) | FlakeHorizonTint.G (hi16)
    uint32_t FlakeHorizonBAndTransWeightHalf = 0u;   // [fp16] - FlakeHorizonTint.B (lo16) | TransmissionWeight (hi16)

    // Row 5 (Bytes 80..95): L6 Abbe Dispersion & Beer-Lambert + L7 Subsurface Diffusion
    uint32_t AbbeVolumeAnisoHalf          = 0u;   // [fp16] - AbbeDispersionNumber (lo16) | VolumeAnisotropyG (hi16)
    uint32_t AttenuationRgHalf               = 0u;   // [fp16] - AttenuationColor.R (lo16) | AttenuationColor.G (hi16)
    uint32_t AttenuationBAndDistHalf         = 0u;   // [fp16] - AttenuationColor.B (lo16) | AttenuationDistanceMeter (hi16)
    uint32_t SubsurfaceWeightAnisoHalf    = 0u;   // [fp16] - SubsurfaceWeight (lo16) | SubsurfacePhaseG (hi16)

    // Row 6 (Bytes 96..111): L7 Subsurface Radii + L8 Sheen + L9 Fiber BCSDF
    uint32_t SubsurfaceColorRgHalf           = 0u;   // [fp16] - SubsurfaceColor.R (lo16) | SubsurfaceColor.G (hi16)
    uint32_t SubsurfaceColorBAndMfpXHalf     = 0u;   // [fp16] - SubsurfaceColor.B (lo16) | MeanFreePathMillimeters.X (hi16)
    uint32_t SubsurfaceMfpYzHalf             = 0u;   // [fp16] - MeanFreePathMillimeters.Y (lo16) | MeanFreePathMillimeters.Z (hi16)
    uint32_t SheenWeightRoughRgHalf       = 0u;   // [pack] - UNORM8: SheenWeight, SheenRoughness, SheenColor.R, SheenColor.G

    // Row 7 (Bytes 112..127): L8 Sheen B + L9 Fiber + L10 Porosity/Blackbody + Bindless Channels
    uint32_t SheenBAndFiberParamsPacked      = 0u;   // [pack] - UNORM8: SheenColor.B, FiberWeight, LongRough, AzimRough
    uint32_t FiberPigmentPorosityPacked   = 0u;   // [pack] - SNORM8 CuticleTilt, UNORM8 Melanin, Pheomelanin, Wetness|Porosity
    uint32_t PorosityKelvinHalf           = 0u;   // [fp16] - PorosityFraction_WetnessPack (lo16) | BlackbodyKelvin (hi16)
    uint32_t EmissionNitsChannelMask  = 0u;   // [pack] - EmissionLuminanceNits FP16 (lo16) | BaseColorChannelOrdinal (hi16)
};

static_assert(sizeof(CompactMaterialEntry) == 128u, "CompactMaterialEntry must occupy exactly 128 bytes (two 64B cache lines).");

//------------------------------------------------------------------------------------------------------------------------
//                                              IEEE-754 FP16 PACKING UTILITIES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint16_t ConvertFloat32ToHalf16(float Value) noexcept;
[[nodiscard]] float    ConvertHalf16ToFloat32(uint16_t HalfBits) noexcept;
[[nodiscard]] uint32_t PackHalf2x16(float LowValue, float HighValue) noexcept;
void                   UnpackHalf2x16(uint32_t PackedWord, float& OutLowValue, float& OutHighValue) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                        COMPACT REGISTER COMPILER & TABLE HOST
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t             DetermineActiveLobeMask(const SurfaceMaterial& Material) noexcept;
[[nodiscard]] CompactMaterialEntry CompileCompactMaterialEntry(const SurfaceMaterial& Material) noexcept;
[[nodiscard]] SurfaceMaterial      UnpackCompactMaterialEntry(const CompactMaterialEntry& Entry);

class CompactMaterialRegister
{
public:
    CompactMaterialRegister();

    void                  ClearRegister();
    [[nodiscard]] uint16_t RegisterMaterial(const SurfaceMaterial& Material);
    bool                  UpdateMaterialSlot(uint16_t MaterialOrdinal, const SurfaceMaterial& Material);

    [[nodiscard]] uint32_t                         QuerySlotCount() const noexcept;
    [[nodiscard]] const CompactMaterialEntry&      InspectCompactEntry(uint16_t MaterialOrdinal) const noexcept;
    [[nodiscard]] const SurfaceMaterial&           InspectAuthoredMaterial(uint16_t MaterialOrdinal) const noexcept;
    [[nodiscard]] std::span<const CompactMaterialEntry> InspectGpuBufferSpan() const noexcept;

    // 📝 Emulate GPU WaveActiveBitOr(ActiveLobeMask) over a tile/wavefront list of visible material slots
    [[nodiscard]] uint32_t QueryCombinedWavefrontLobeMask(std::span<const uint16_t> VisibleMaterialSlots) const noexcept;

private:
    std::vector<CompactMaterialEntry> CompactEntries   {}; // [128 B] - Contiguous GPU-uploadable bindless buffer
    std::vector<SurfaceMaterial>      AuthoredEntries  {}; // [-]     - Matching CPU authored materials (slot 0 = default clay)
};

} // namespace Frontier::MaterialSpace
