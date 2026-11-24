/*============================================================================================================================================
                                                        FIREWORKPATTERNSAMPLER.H
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 3D pyrotechnic shell & SkyText pattern sampler (10 firework patterns + 5x7 bitmap font)
// Reference: Experimental/FluidEditor/Engine/FireworkPatterns.js L1-L623

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleCloudTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     DATA STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FireworkShapeMetadata
{
    FireworkPatternOrdinal Pattern = FireworkPatternOrdinal::ClassicBouquet; // [-] - Pattern ordinal (0..9)
    std::string_view       Key     {};                                       // [-] - Short reference key ("bouquet", "text", ...)
    std::string_view       Token   {};                                       // [-] - Canonical TOML token ("ClassicBouquet".."GrandFinale")
    std::string_view       Label   {};                                       // [-] - Reference UI shell display name
};

struct FireworkSamplePoint
{
    double X     = 0.0; // [-] - Unit sky radius X
    double Y     = 0.0; // [-] - Unit sky radius Y
    double Z     = 0.0; // [-] - Unit sky radius Z
    double R     = 1.0; // [-] - Linear burst tint R
    double G     = 1.0; // [-] - Linear burst tint G
    double B     = 1.0; // [-] - Linear burst tint B
    double Role  = 1.0; // [-] - 1 = crisp pattern star, 2 = halo/pistil accent
    double Delay = 0.0; // [s] - Burst ignition stagger delay
};

struct FireworkShellSample
{
    uint32_t                         ActiveMode = 0u;    // [-] - Resolved shell mode (0..8)
    std::string                      ShellName  {};      // [-] - Formatted shell label
    std::vector<FireworkSamplePoint> Points     {};      // [-] - Sampled 3D points
    bool                             IsWillow   = false; // [-] - True for drooping willow/brocade/spiral shells
    bool                             HasStrobe  = true;  // [-] - True when strobe sparkle enabled
};

struct FireworkAnchorSet
{
    uint32_t           Count     = 0u; // [-] - Sample count
    std::vector<float> Positions {};   // [-] - Tightly packed xyz (Count * 3)
    std::vector<float> Normals   {};   // [-] - Tightly packed RGB burst tint (Count * 3)
    std::vector<float> Ridges    {};   // [-] - Role per anchor (Count)
    std::string        ShellName {};   // [-] - Display shell label
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLING & PACKING API
//------------------------------------------------------------------------------------------------------------------------

/// Returns all 10 firework shape descriptors in exact FireworkPatterns.js declaration order.
[[nodiscard]] std::span<const FireworkShapeMetadata> QueryAllFireworkShapes() noexcept;

/// Samples one pyrotechnic shell pattern (0..9, where 9 cycles FINALE_SEQUENCE) (FireworkPatterns.js L529-L546).
[[nodiscard]] FireworkShellSample SampleFireworkShell(
    uint32_t shapeMode,
    std::string_view text,
    uint32_t count,
    int32_t shellIndex = 0);

/// Packs a firework shell into a 20-float-per-particle GPU buffer (FireworkPatterns.js L550-L584).
[[nodiscard]] std::vector<float> BuildFireworkPackedBuffer(
    uint32_t shapeMode,
    std::string_view text,
    uint32_t capacity,
    int32_t shellIndex = 0);

/// Samples a firework pattern into Positions/Normals/Ridges anchor arrays (FireworkPatterns.js L595-L622).
[[nodiscard]] FireworkAnchorSet SampleFireworkAnchors(
    FireworkPatternOrdinal pattern,
    std::string_view text,
    uint32_t count,
    int32_t shellIndex = 0);

} // namespace Frontier::ParticleSpace
