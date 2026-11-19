/*============================================================================================================================================
                                                        PROCEDURALSPRITESYNTHESIZER.H
============================================================================================================================================*/
// 🧩 Deterministic multi-channel procedural sprite, crystal, wing, and dissolve-mask synthesizer shared by ParticleSpace and Pattern Studio

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ProceduralSpriteGroup : uint8_t
{
    SnowCrystal   = 0, // [-] - D6 / D12 dendritic and faceted atmospheric ice crystals
    OpticalFlare  = 1, // [-] - Aperture bokeh, starburst flares, and Gaussian plasma halos
    PyroPlume     = 2, // [-] - Turbulent blackbody flame wisps, smoke puffs, and ember streaks
    LiquidDroplet = 3, // [-] - Surface-tension water drops and thin-film soap bubbles
    BotanicalWing = 4, // [-] - Articulated butterfly wing pairs and blossom petals
    TechShield    = 5, // [-] - Hexagonal energy barriers and Voronoi dissolve masks
};

enum class ProceduralSpriteArchetype : uint8_t
{
    SnowStellarDendrite      = 0,
    SnowFernlikeDendrite     = 1,
    SnowHexPlate             = 2,
    SnowTwelveFoldStar       = 3,
    OpticalHexBokeh          = 4,
    OpticalStarburstFlare    = 5,
    OpticalSoftGlow          = 6,
    PyroFlameWisp            = 7,
    PyroSmokePuff            = 8,
    PyroEmberSpark           = 9,
    LiquidRainDrop           = 10,
    LiquidSoapBubble         = 11,
    BotanicalAzureButterfly = 12,
    BotanicalSakuraPetal     = 13,
    TechHexEnergyShield      = 14,
    TechVoronoiDissolveMask  = 15,
    Count                    = 16,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProceduralSpriteSpec
{
    ProceduralSpriteArchetype Archetype       = ProceduralSpriteArchetype::SnowStellarDendrite;
    uint32_t                  TileResolution  = 128u;  // [px]  - Width and height of each individual sprite tile
    uint32_t                  AtlasColumns    = 1u;    // [cnt] - Grid side count (1 = single sprite, 2 = 2x2, 4 = 4x4, 8 = 8x8)
    uint32_t                  Seed            = 1337u; // [-]   - Deterministic synthesis seed
    float                     BranchDensity   = 0.68f; // [0-1] - Secondary arm / detail richness
    float                     EdgeCrispness   = 0.72f; // [0-1] - Silhouette and ridge sharpness
    float                     TurbulenceGain  = 0.45f; // [0-1] - Organic warp or FBM roughness gain
    float                     NormalRelief    = 2.6f;  // [-]   - Tangent-space Sobel normal slope multiplier
    float                     PrimaryTintR    = 0.88f; // [0-1] - Primary tint red
    float                     PrimaryTintG    = 0.96f; // [0-1] - Primary tint green
    float                     PrimaryTintB    = 1.00f; // [0-1] - Primary tint blue
    float                     SecondaryTintR  = 0.34f; // [0-1] - Secondary / rim tint red
    float                     SecondaryTintG  = 0.80f; // [0-1] - Secondary / rim tint green
    float                     SecondaryTintB  = 1.00f; // [0-1] - Secondary / rim tint blue
};

struct ProceduralSpriteChannelSet
{
    uint32_t             Width          = 0u; // [px] - Full output sheet width (TileResolution * AtlasColumns)
    uint32_t             Height         = 0u; // [px] - Full output sheet height (TileResolution * AtlasColumns)
    uint32_t             TileResolution = 0u; // [px] - Single tile edge length
    uint32_t             AtlasColumns   = 1u; // [cnt]- Tiles per axis
    std::vector<uint8_t> Rgba8;               // [px] - sRGB albedo + silhouette alpha (4 bytes/pixel)
    std::vector<uint8_t> Height8;             // [px] - Linear relief elevation [0..255] (1 byte/pixel)
    std::vector<uint8_t> NormalRgba8;         // [px] - Tangent-space normal encoded 0.5*N+0.5 (4 bytes/pixel)
    std::vector<uint8_t> MaskRgba8;           // [px] - R=GlintSparkle, G=SignedDistanceMask, B=DissolveThreshold, A=255
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ProceduralSpriteGroup QueryProceduralSpriteFamily(ProceduralSpriteArchetype Archetype) noexcept;
[[nodiscard]] std::string_view       QueryProceduralSpriteTitle(ProceduralSpriteArchetype Archetype) noexcept;
[[nodiscard]] ProceduralSpriteSpec   QueryDefaultProceduralSpriteSpec(ProceduralSpriteArchetype Archetype) noexcept;

// Synthesizes all four packed channels (RGBA8, Height8, TangentNormal, Sparkle/SDF/DissolveMask) deterministically.
[[nodiscard]] ProceduralSpriteChannelSet SynthesizeProceduralSpriteChannels(const ProceduralSpriteSpec& Spec);

// Fast point-sample for direct 3D sprite billboard / wing / dissolve rasterization without full atlas allocation.
struct ProceduralSpriteSample
{
    float Red               = 0.0f;
    float Green             = 0.0f;
    float Blue              = 0.0f;
    float Alpha             = 0.0f;
    float Height            = 0.0f;
    float Glint             = 0.0f;
    float SignedDistance01  = 0.0f;
    float DissolveThreshold = 0.0f;
};

[[nodiscard]] ProceduralSpriteSample SampleProceduralSpriteUnit(
    const ProceduralSpriteSpec& Spec,
    float                       UnitX,
    float                       UnitY,
    uint32_t                    TileSeed) noexcept;

} // namespace Frontier
