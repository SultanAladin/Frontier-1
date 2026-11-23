/*============================================================================================================================================
                                                             BOUNCELAYERSUMMARY.H
============================================================================================================================================*/
// 🧩 Pre-integrated 32-byte secondary-bounce surface summary evaluated in 5 ALU operations during GI & multi-bounce raytracing

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <cstdint>

namespace Frontier::MaterialSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact 32-byte (two float4 registers) bindless summary indexed by uint16 MaterialSlot for multi-bounce specular rays
struct alignas(16) BounceSlabEntry
{
    // Bytes 0..15
    uint32_t DiffuseAlbedoRgHalf             = 0u;   // [fp16] - Hemispherical diffuse albedo R (lo16), G (hi16)
    uint32_t DiffuseAlbedoBAndRoughnessHalf  = 0u;   // [fp16] - Hemispherical diffuse albedo B (lo16), EffectiveRoughness (hi16)
    uint32_t SpecularF0RgHalf                = 0u;   // [fp16] - Effective normal-incidence F0 R (lo16), G (hi16)
    uint32_t SpecularF0BAndMetalnessHalf     = 0u;   // [fp16] - Effective normal-incidence F0 B (lo16), MetallicFraction (hi16)

    // Bytes 16..31
    uint32_t EmissiveExitanceRgHalf          = 0u;   // [fp16] - Emitted radiance R (lo16), G (hi16) [W/(sr*m^2) normalized]
    uint32_t EmissiveExitanceBAndTransHalf   = 0u;   // [fp16] - Emitted radiance B (lo16), TransmissionFraction (hi16)
    float    MeanHemisphericalReflectance    = 0.5f; // [-]    - Total scalar hemispherical energy albedo in [0, 1]
    uint32_t SummaryFlags                    = 0u;   // [bits] - Fast flags (bit0 = Transmissive, bit1 = Emissive)
};

static_assert(sizeof(BounceSlabEntry) == 32u, "BounceSlabEntry must occupy exactly 32 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] BounceSlabEntry CompileBounceSlabEntry(const SurfaceMaterial& Material) noexcept;

// 📝 Fast 5-ALU secondary-bounce surface response (1/pi Lambertian + Schlick grazing specular + emissive exitance)
[[nodiscard]] Mathematics::LinearRGB EvaluateBounceSlabRadiance(
    const BounceSlabEntry& Summary,
    Mathematics::Axis3D    SurfaceNormal,
    Mathematics::Axis3D    OutgoingDirection,
    Mathematics::Axis3D    IncidentLightDirection,
    Mathematics::LinearRGB IncidentRadiance) noexcept;

} // namespace Frontier::MaterialSpace
