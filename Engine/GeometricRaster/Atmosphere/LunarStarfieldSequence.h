/*============================================================================================================================================
                                                        LUNARSTARFIELDSEQUENCE.H
============================================================================================================================================*/
// 🧩 Multi-instance Lunar discs (6 surface presets, 29.53-day phase, roll/pitch) and deterministic twinkling Starfield evaluation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LunarSurfacePreset : uint8_t
{
    Luna    = 0u, // [-] - Neutral regolith craters
    Ember   = 1u, // [-] - Volcanic basalt/ember glow
    Glacier = 2u, // [-] - Cryo-ice albedo tint
    Sulfur  = 3u, // [-] - Io-like sulfur mantle
    Shroud  = 4u, // [-] - Muted slate eclipse tint
    Shard   = 5u, // [-] - Pale seafoam mineral crust
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LunarBodyConfiguration
{
    bool               Visible                = false;                        // [-]   - Enables lunar disc & halo
    bool               FollowSkyOppositeSun   = false;                        // [-]   - Locks moon anti-solar to SunDirection
    LunarSurfacePreset SurfacePreset          = LunarSurfacePreset::Luna;     // [-]   - Surface tint & mare profile
    float              SynodicPhaseFraction   = 0.62f;                        // [0-1] - 0=New, 0.5=Full, 1=New
    float              BrightnessMultiplier   = 1.60f;                        // [x]   - Disc brightness scale [0..6]
    float              HaloGlowMultiplier     = 0.80f;                        // [x]   - Atmospheric halo glow [0..3]
    float              AngularDiameterDegrees = 0.52f;                        // [deg] - Apparent disc diameter [0.1..180]
    float              AzimuthDegrees         = 300.0f;                       // [deg] - Sky compass azimuth [0..360)
    float              ElevationDegrees       = 28.0f;                        // [deg] - Sky altitude angle [-90..+90]
    float              SurfaceRollDegrees     = 0.0f;                         // [deg] - Surface tangent roll [0..360)
    float              SurfacePitchDegrees    = 0.0f;                         // [deg] - Surface latitude tilt [-180..+180]
};

struct StarfieldConfiguration
{
    bool  StarfieldEnabled          = true;                                   // [-]   - Enables nocturnal procedural starfield
    bool  ScintillationEnabled      = true;                                   // [-]   - Enables atmospheric twinkle modulation
    float LimitingMagnitude         = 5.5f;                                   // [mag] - Apparent magnitude cutoff [0..8]
    float BrightnessMultiplier      = 1.0f;                                   // [x]   - Star intensity scale [0..4]
    float PointSizeScale            = 1.0f;                                   // [x]   - Star point radius scale [0.4..3.0]
    float ScintillationDepthPercent = 30.0f;                                  // [%]   - Twinkle modulation depth [0..100]
    float ScintillationRateHertz    = 1.20f;                                  // [Hz]  - Twinkle frequency [0.2..3.0]
    float CelestialRotationDegrees  = 0.0f;                                   // [deg] - Celestial pole rotation [0..360)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LUNAR STARFIELD SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class LunarStarfieldSequence
{
public:
    static constexpr uint32_t AtlasTileWidthPx  = 256u;
    static constexpr uint32_t AtlasTileHeightPx = 128u;
    static constexpr uint32_t AtlasPresetCount  = 6u;

    // Returns canonical linear RGB tint for a LunarSurfacePreset
    [[nodiscard]] static Mathematics::LinearRGB QueryPresetTint(LunarSurfacePreset Preset) noexcept;

    // Resolves world-space unit direction toward the moon (honoring FollowSkyOppositeSun)
    [[nodiscard]] static Mathematics::Axis3D ResolveMoonWorldDirection(
        const LunarBodyConfiguration& Moon,
        const Mathematics::Axis3D&    SunDirection) noexcept;

    // Synthesizes procedural 6-tier cratered/mare lunar atlas (RGBA8, 256 x 768)
    [[nodiscard]] static std::vector<uint8_t> BuildProceduralLunarSurfaceAtlasRgba8();

    // Evaluates all 4 moon slots + deterministic twinkling starfield along RayDirection
    [[nodiscard]] static Mathematics::LinearRGB EvaluateCelestialField(
        const Mathematics::Axis3D&               RayDirection,
        const Mathematics::Axis3D&               SunDirection,
        std::span<const LunarBodyConfiguration, 4u> Moons,
        const StarfieldConfiguration&            Stars,
        float                                    SimulationSeconds) noexcept;
};

} // namespace Frontier::GeometricRaster
