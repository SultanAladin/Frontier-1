/*============================================================================================================================================
                                                             VISUALCARRIERTYPES.H
============================================================================================================================================*/
// 🧩 Modular visual render-geometry carrier types (all 7 carriers + all 20 procedural sprite cutouts + ColourSpace OpticalBlendEquation)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourSourceTypes.h"

#include <cstdint>
#include <variant>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class VisualCarrierCategory : uint8_t
{
    SpriteBillboard = 0u, // [-] - Camera/velocity-aligned billboards (20 procedural cutouts)
    RibbonStrand    = 1u, // [-] - 20-vertex continuous silk, cable, cloth, grass, kelp, smoke ribbons
    OrientedSolid   = 2u, // [-] - True 3D oriented solids (cubes, coins, shards, MoGraph extrusions)
    WingedCreature  = 3u, // [-] - Articulated flapping 3D winged creatures (butterflies, birds, fish)
    DielectricBeam  = 4u, // [-] - Coherent beams, lightning/Tesla arcs, spiderweb/molecular bonds
    RefractionLens  = 5u, // [-] - Screen-space gravitational lens & thermal shimmer refraction
    BackingSurface  = 6u  // [-] - Companion obsidian/chrome/wireframe backing surface object
};

/// 1:1 parity with all 20 procedural sprite cutout shapes (0..19) in Experimental/ParticleEditor Final.
enum class SpriteCutoutCategory : uint8_t
{
    Streak              = 0u,
    ShadedSphere        = 1u,
    BotanicalLeafPaper  = 2u,
    SoftGlow            = 3u,
    CubeProjection      = 4u,
    MetallicCoin        = 5u,
    IridescentBubble    = 6u,
    DendriticSnowflake  = 7u,
    CrystallineHailstone= 8u,
    BokehDisc           = 9u,
    ShockwaveRing       = 10u,
    PulseDome           = 11u,
    HologramGlitch      = 12u,
    WingedSilhouette    = 13u,
    ViscousDroplet      = 14u,
    RadarSonarSweep     = 15u,
    EnergyBarrierShield = 16u,
    AuroraCurtain       = 17u,
    GlassShardCrystal   = 18u,
    VolumetricWaterMist = 19u,
    Count               = 20u
};

enum class BokehApertureCategory : uint8_t
{
    MixedDepth = 0u,
    SharpCore  = 1u,
    CreamyDisc = 2u,
    RimRing    = 3u
};

enum class BackingSurfaceStyle : uint8_t
{
    ObsidianDark    = 0u,
    BrushedChrome   = 1u,
    GhostWireframe  = 2u,
    HiddenParticles = 3u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpriteBillboardCarrier
{
    SpriteCutoutCategory  Cutout         = SpriteCutoutCategory::BokehDisc;
    BokehApertureCategory BokehAperture  = BokehApertureCategory::MixedDepth;
    float                 BokehHeadScale = 1.0f;  // [x] - Bokeh tip size scale
    float                 StreakThinness = 1.0f;  // [x] - Cross-section ratio (<1 = fine silk)
    bool                  PaperLeafMode  = false; // [-] - True uses rectangular paper sheet cutout
};

struct RibbonStrandCarrier
{
    uint32_t VerticesPerStrand = 20u;   // [count] - History/subdivision vertices per strand
    float    BaseWidthMeters   = 0.03f; // [m]     - Strand root width
    float    TipTaperRatio     = 0.15f; // [0-1]   - Tip-to-root width taper
    bool     SolidWovenSheet   = false; // [-]     - True bridges adjacent strands into solid cloth
    bool     PinnateFrond      = false; // [-]     - True emits fern leaflets along spine
};

struct OrientedSolidCarrier
{
    uint8_t GeometryForm       = 2u;    // [-] - 0 Circle, 1 Box, 2 HeightCube, 3 FlatTile, 4 Tri, 5 Hex, 6 Auto
    float   MinScaleFactor     = 0.10f; // [x] - Idle scale outside effector reach
    float   MaxExtrudeMeters   = 0.95f; // [m] - Peak extrusion height
    bool    FragmentShatter8   = false; // [-] - True splits into 8 tumbling floor-impact shards
};

struct WingedCreatureCarrier
{
    uint8_t SpeciesVariant     = 0u;    // [-]  - 0 Butterfly, 1 Bird, 2 Starling, 3 Bioluminescent Fish
    float   FlapFrequencyHz    = 8.5f;  // [Hz] - Wing beat cycle rate
    float   WingspanMeters     = 0.16f; // [m]  - Full wing tip-to-tip span
};

struct DielectricBeamCarrier
{
    float   CoreRadiusMeters   = 0.048f; // [m]  - Beam or arc filament thickness
    float   PulseSpeedHz       = 6.0f;   // [Hz] - Standing-wave plasma pulse rate
    uint8_t ArcTopology        = 0u;     // [-]  - 0 Laser, 1 TeslaArc, 2 PlasmaCage, 3 WebBond, 4 FibreTube
};

struct RefractionLensCarrier
{
    bool  GravitationalBlackHole = true; // [-] - False = rising thermal heat shimmer
    float DistortionStrength     = 1.0f; // [x] - Screen-space refraction offset scale
};

struct BackingSurfaceCarrier
{
    BackingSurfaceStyle SurfaceStyle = BackingSurfaceStyle::ObsidianDark; // [-] - Backing geometry material style
    bool                ShowVehicle  = false;                              // [-] - True renders figure-8 rally vehicle hull
};

using VisualCarrierSpec = std::variant<
    SpriteBillboardCarrier,
    RibbonStrandCarrier,
    OrientedSolidCarrier,
    WingedCreatureCarrier,
    DielectricBeamCarrier,
    RefractionLensCarrier,
    BackingSurfaceCarrier>;

[[nodiscard]] constexpr VisualCarrierCategory QueryVisualCarrierCategory(const VisualCarrierSpec& Spec) noexcept
{
    return static_cast<VisualCarrierCategory>(Spec.index());
}

} // namespace Frontier::ParticleSpace
