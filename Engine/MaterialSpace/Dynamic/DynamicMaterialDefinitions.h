/*==========================================================================================================================================
                                                   DYNAMICMATERIALDEFINITIONS.H
==========================================================================================================================================*/
// 🧩 Modular asset-agnostic enumerations and channel bitmasks for Frontier's layer-stack dynamic & procedural OpenPBR architecture

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <string_view>

namespace Frontier::MaterialSpace::Dynamic
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class MaterialLayerCategory : uint8_t
{
    FinishLayer            = 0u, // [-] - Complete 10-lobe OpenPBR 1.1 physical finish foundation
    ProceduralFieldLayer   = 1u, // [-] - Analytical 3D/triplanar/spherical procedural field modulating target channels
    MaskModifierLayer      = 2u, // [-] - Spatial or animated mask with optional luminous leading-edge seam
    SurfaceTransitionLayer = 3u, // [-] - Modular blend from FinishLayer A into FinishLayer B across a spatial sweep mask
};

enum class ProceduralFieldPattern : uint8_t
{
    None                    = 0u,  // [-] - Direct scalar OpenPBR finish without procedural modulation
    CubicVoxelLattice       = 1u,  // [-] - Crisp 3D cubic lattice dissolve & leading-edge cutout field
    DualScaleVoronoiFaults  = 2u,  // [-] - Volcanic pahoehoe basalt fracture plates & recessed convective fissures
    HexagonalBarrierLattice = 3u,  // [-] - Spherical hexagonal energy barrier grid with cell shimmer
    AnisotropicBrushedGrain = 4u,  // [-] - Directional brushed aerospace alloy micro-grooves
    OrthographicScanGrid    = 5u,  // [-] - Lidar elevation scan plane & trailing orthographic telemetry contours
    GerstnerOceanSwells     = 6u,  // [-] - Multi-directional animated ocean swells + capillary ripples + foam crests
    PearlescentMicroFlakes  = 7u,  // [-] - Deep obsidian clearcoat with pearlescent metallic flakes
    RainBeadStreaks         = 8u,  // [-] - Falling beaded glass raindrops with wet glossy vertical streaks
    CrushedAsphaltPuddles   = 9u,  // [-] - Porous aggregate tarmac transitioning into rippled rain puddles
    SastrugiSparkleSnow     = 10u, // [-] - Wind-sculpted snow drifts with view-dependent diamond ice glints
    RefractiveCausticField  = 11u, // [-] - Chromatically dispersed underwater caustic projection over limestone
    CapillaryMercuryWaves   = 12u, // [-] - Intersecting harmonic capillary ripples on liquid mirror metal
    ThinFilmThicknessDrift  = 13u, // [-] - Organic nanometer interference thickness shift over brushed alloy
};

enum class MaskModifierMode : uint8_t
{
    FullSurface             = 0u, // [-] - Uniformly active over entire asset surface
    SweepWaveMask           = 1u, // [-] - Normalized axis sweep front with organic jitter & ionized seam
    FresnelGrazingMask      = 2u, // [-] - View-angle grazing shell pow(1 - NdotV, power)
    GeodesicImpactRipple    = 3u, // [-] - Expanding spherical shockwave rings radiating from live 3D hit origins
    RecessedCreviceMask     = 4u, // [-] - Fracture valley mask concentrating thermal glow in deep cracks
    ElevationBandMask       = 5u, // [-] - Moving horizontal lidar scan plane + trailing wake decay
    PuddleFloodMask         = 6u, // [-] - Climate-driven water pooling & concentric raindrop ripples
    VoxelClockCutoutMask    = 7u, // [-] - Synchronized 3D lattice cutout threshold driven by progress/clock
};

enum class ParameterDriverMode : uint8_t
{
    ConstantScalar          = 0u, // [-] - Direct artist slider control without animation
    PlaybackClock           = 1u, // [-] - Monotonic clock progression (ClockSeconds * AnimationSpeed)
    PingPongOscillation     = 2u, // [-] - Smooth periodic oscillation for showroom turntable previews
    ExternalTimeline        = 3u, // [-] - Driven by sequencer, gameplay, or external controller [0..1]
    WorldImpactPulse        = 4u, // [-] - Triggered by up to 4 live 3D surface impact points & timestamps
    WeatherClimate          = 5u, // [-] - Driven by global rain intensity, wind vector, and snow depth
    ParticleLatticeLink     = 6u, // [-] - Coupled to detached 3D particle fragments sharing normalized asset bounds
};

enum class BlendChannelBitmask : uint32_t
{
    None                 = 0u,
    ChannelAlbedo        = 1u << 0u,
    ChannelRoughness     = 1u << 1u,
    ChannelMetalness     = 1u << 2u,
    ChannelReliefNormal  = 1u << 3u,
    ChannelClearcoat     = 1u << 4u,
    ChannelThinFilm      = 1u << 5u,
    ChannelIncandescence = 1u << 6u,
    ChannelOpacityCutout = 1u << 7u,
    ChannelAllOptical    = 0xFFu,
};

enum class DynamicPresetIdentity : uint8_t
{
    LatticeVoxelDissolve        = 0u,  // [-] - Asset-agnostic cubic lattice dissolve over clearcoat finish
    VolcanicPahoehoeMagma       = 1u,  // [-] - Photorealistic rough volcanic basalt crust + recessed molten veins
    HexagonalEnergyBarrier      = 2u,  // [-] - Translucent Fresnel dome, razor hex wireframe & geodesic shockwaves
    SurfaceMaterialTransition   = 3u,  // [-] - Modular Finish A <-> Finish B sweep across ionized hex seam
    HolographicTelemetryScan    = 4u,  // [-] - Sweeping lidar scan plane + trailing wireframe telemetry contours
    PhysicalOceanWater          = 5u,  // [-] - Animated Gerstner swells, Schlick Fresnel, backlit crests & foam
    LiquidObsidianClearcoat     = 6u,  // [-] - Deep obsidian automotive lacquer with pearlescent metallic flakes
    RainStreakedSmokedGlass     = 7u,  // [-] - Refractive dark glass with live running rain beads & glossy trails
    WetAsphaltRainPuddles       = 8u,  // [-] - Porous crushed-aggregate asphalt flooding into rippled mirror puddles
    HoarfrostSparkleSnow        = 9u,  // [-] - Wind-sculpted snow with cool subsurface bounce & diamond glints
    SubmergedLimestoneCaustics  = 10u, // [-] - Submerged limestone riverbed lit by chromatic refractive caustics
    LiquidChromeMercury         = 11u, // [-] - Mirror liquid conductor with intersecting harmonic capillary waves
    AnodizedThinFilmIridescence = 12u, // [-] - Brushed gunmetal with Belcour-Barla optical thin-film drift
};

inline constexpr uint32_t DynamicPresetTotalCount = 13u;

//------------------------------------------------------------------------------------------------------------------------
//                                                ENUMERATION LABEL QUERIES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr uint32_t CombineChannelBits(BlendChannelBitmask Left, BlendChannelBitmask Right) noexcept
{
    return static_cast<uint32_t>(Left) | static_cast<uint32_t>(Right);
}

[[nodiscard]] constexpr std::string_view QueryLayerCategoryLabel(MaterialLayerCategory Category) noexcept
{
    switch (Category)
    {
        case MaterialLayerCategory::FinishLayer:            return "Finish Layer";
        case MaterialLayerCategory::ProceduralFieldLayer:   return "Procedural Field";
        case MaterialLayerCategory::MaskModifierLayer:      return "Mask Modifier";
        case MaterialLayerCategory::SurfaceTransitionLayer: return "Surface Transition";
    }
    return "Finish Layer";
}

[[nodiscard]] constexpr std::string_view QueryDriverModeLabel(ParameterDriverMode Driver) noexcept
{
    switch (Driver)
    {
        case ParameterDriverMode::ConstantScalar:      return "Constant";
        case ParameterDriverMode::PlaybackClock:       return "Playback Clock";
        case ParameterDriverMode::PingPongOscillation: return "Ping-Pong";
        case ParameterDriverMode::ExternalTimeline:    return "Timeline";
        case ParameterDriverMode::WorldImpactPulse:    return "Impact Pulse";
        case ParameterDriverMode::WeatherClimate:      return "Weather Climate";
        case ParameterDriverMode::ParticleLatticeLink: return "Particle Link";
    }
    return "Constant";
}

} // namespace Frontier::MaterialSpace::Dynamic
