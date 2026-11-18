/*============================================================================================================================================
                                                        LUNARSTARFIELDSEQUENCE.H
============================================================================================================================================*/
// 🧩 Multi-instance Lunar discs (6 real 2K surface presets, 29.53-day phase, roll/pitch) and deterministic twinkling Starfield evaluation

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

struct AtmosphericMediumConfiguration;

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
    bool               Visible                = false;                        // [-]   - Enables lunar disc & atmospheric shell
    bool               FollowSkyOppositeSun   = false;                        // [-]   - Locks moon anti-solar to SunDirection
    LunarSurfacePreset SurfacePreset          = LunarSurfacePreset::Luna;     // [-]   - Surface texture & mare profile
    float              SynodicPhaseFraction   = 0.62f;                        // [0-1] - 0=New, 0.5=Full, 1=New
    float              BrightnessMultiplier   = 1.60f;                        // [x]   - Disc brightness scale [0..6]
    float              HaloGlowMultiplier     = 0.80f;                        // [x]   - Atmospheric scatter multiplier [0..3]
    float              AngularDiameterDegrees = 0.52f;                        // [deg] - Apparent disc diameter [0.1..180]
    float              AzimuthDegrees         = 300.0f;                       // [deg] - Sky compass azimuth [0..360)
    float              ElevationDegrees       = 28.0f;                        // [deg] - Sky altitude angle [-90..+90]
    float              SurfaceRollDegrees     = 0.0f;                         // [deg] - Surface tangent roll [0..360)
    float              SurfacePitchDegrees    = 0.0f;                         // [deg] - Surface latitude tilt [-180..+180]
    bool               HasAtmosphere          = false;                        // [-]   - Enables explicit physical volumetric moon atmosphere
    float              AtmosphereIntensity    = 0.85f;                        // [x]   - Volumetric off-limb & twilight atmosphere density
    float              AtmosphereThicknessScale = 0.22f;                      // [x]   - Atmospheric shell thickness relative to lunar radius
    Mathematics::LinearRGB AtmosphereTint     { 0.55f, 0.78f, 1.00f };        // [-]   - Physical Rayleigh/Mie atmospheric scattering tint
};

struct BrightCatalogStar
{
    Mathematics::Axis3D DirectionUnit   { 0.0f, 0.0f, 1.0f }; // [-]   - J2000 unit direction vector
    float               ApparentMagnitude = 1.0f;             // [mag] - Visual apparent magnitude V
    float               BvColorIndex    = 0.0f;               // [mag] - Johnson B-V color index [-0.35..+2.0]
};

struct StarfieldConfiguration
{
    bool  StarfieldEnabled          = true;                                   // [-]   - Enables nocturnal pinpoint starfield
    bool  ScintillationEnabled      = true;                                   // [-]   - Enables atmospheric twinkle modulation
    float LimitingMagnitude         = 5.5f;                                   // [mag] - Apparent magnitude cutoff [0..8]
    float BrightnessMultiplier      = 1.0f;                                   // [x]   - Star intensity scale [0..4]
    float PointSizeScale            = 1.0f;                                   // [x]   - Star point radius scale [0.4..3.0]
    float ScintillationDepthPercent = 30.0f;                                  // [%]   - Twinkle modulation depth [0..100]
    float ScintillationRateHertz    = 1.20f;                                  // [Hz]  - Twinkle frequency [0.2..3.0]
    float CelestialRotationDegrees  = 0.0f;                                   // [deg] - Celestial pole rotation [0..360)
    bool  EnableGalacticBand        = true;                                   // [-]   - Enables tilted Milky Way galactic band & dust lanes
    bool  EnableBrightStarCatalog   = true;                                   // [-]   - Enables J2000 bright navigation star catalog
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LUNAR STARFIELD SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class LunarStarfieldSequence
{
public:
    static constexpr uint32_t MaxMoonSlots      = 6u;
    static constexpr uint32_t AtlasTileWidthPx  = 512u;
    static constexpr uint32_t AtlasTileHeightPx = 256u;
    static constexpr uint32_t AtlasPresetCount  = 6u;

    // Returns canonical linear RGB tint for a LunarSurfacePreset (used for atmospheric halo glow & procedural fallback)
    [[nodiscard]] static Mathematics::LinearRGB QueryPresetTint(LunarSurfacePreset Preset) noexcept;

    // Converts Johnson B-V stellar color index [-0.35..+2.0] to linear Planckian RGB color
    [[nodiscard]] static Mathematics::LinearRGB EvaluateStarColorFromBv(float BvIndex) noexcept;

    // Returns built-in J2000 bright navigation star catalog
    [[nodiscard]] static std::span<const BrightCatalogStar> QueryJ2000BrightStarCatalog() noexcept;

    // Resolves world-space unit direction toward the moon (honoring FollowSkyOppositeSun)
    [[nodiscard]] static Mathematics::Axis3D ResolveMoonWorldDirection(
        const LunarBodyConfiguration& Moon,
        const Mathematics::Axis3D&    SunDirection) noexcept;

    // Builds 6-tile stacked lunar surface atlas (RGBA8, 512 x 1536) from the 2K moon textures with procedural fallback
    [[nodiscard]] static std::vector<uint8_t> BuildLunarSurfaceAtlasRgba8();

    // Synthesizes procedural 6-tier cratered/mare lunar atlas fallback (RGBA8, 512 x 1536)
    [[nodiscard]] static std::vector<uint8_t> BuildProceduralLunarSurfaceAtlasRgba8();

    // Returns cached 512 x 1536 RGBA8 atlas built from the real 2K moon textures (thread-safe lazy load)
    [[nodiscard]] static const std::vector<uint8_t>& QueryLunarSurfaceAtlasRgba8();

    // Evaluates up to 6 moon slots (real equirectangular atlas + off-limb volumetric atmosphere) + pinpoint optical starfield
    [[nodiscard]] static Mathematics::LinearRGB EvaluateCelestialField(
        const Mathematics::Axis3D&            RayDirection,
        const Mathematics::Axis3D&            SunDirection,
        std::span<const LunarBodyConfiguration> Moons,
        const StarfieldConfiguration&         Stars,
        float                                 SimulationSeconds,
        const AtmosphericMediumConfiguration* AtmosphereMedium = nullptr,
        std::span<const BrightCatalogStar>    ExtraCatalogStars = {}) noexcept;
};

} // namespace Frontier::GeometricRaster
