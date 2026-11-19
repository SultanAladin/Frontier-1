/*============================================================================================================================================
                                                        LUNARSTARFIELDSEQUENCE.H
============================================================================================================================================*/
// 🧩 Multi-instance Lunar discs (6 real 2K surface presets, Lommel-Seeliger regolith, off-limb exospheres) & pinpoint blackbody Starfield

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
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

enum class StarfieldPresetIdentifier : uint8_t
{
    GalacticBulge          = 0u, // [-] - Overhead Sagittarius bulge & Great Rift dust lanes
    ZodiacalLightDusk      = 1u, // [-] - Steep southern-spring evening ecliptic zodiacal light & dusk stars
    SouthernCrossCoalsack  = 2u, // [-] - Crux, Coalsack dark nebula, Alpha/Beta Centauri & Eta Carinae
    WinterHunterBetelgeuse = 3u, // [-] - Orion (Betelgeuse, Rigel, Belt, M42), Sirius, Aldebaran & Pleiades
    MagellanicClouds       = 4u, // [-] - Large & Small Magellanic Clouds, Achernar, Canopus & Eta Carinae
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LunarBodyConfiguration
{
    bool               Visible                  = false;                        // [-]   - Enables lunar disc & halo
    bool               FollowSkyOppositeSun     = false;                        // [-]   - Locks moon anti-solar to SunDirection
    LunarSurfacePreset SurfacePreset            = LunarSurfacePreset::Luna;     // [-]   - Surface texture & mare profile
    float              SynodicPhaseFraction     = 0.62f;                        // [0-1] - 0=New, 0.5=Full, 1=New
    float              BrightnessMultiplier     = 1.60f;                        // [x]   - Disc brightness scale [0..6]
    float              HaloGlowMultiplier       = 0.80f;                        // [x]   - Atmospheric halo glow [0..3]
    float              AngularDiameterDegrees   = 0.52f;                        // [deg] - Apparent disc diameter [0.1..180]
    float              AzimuthDegrees           = 300.0f;                       // [deg] - Sky compass azimuth [0..360)
    float              ElevationDegrees         = 28.0f;                        // [deg] - Sky altitude angle [-90..+90]
    float              SurfaceRollDegrees       = 0.0f;                         // [deg] - Surface tangent roll [0..360)
    float              SurfacePitchDegrees      = 0.0f;                         // [deg] - Surface latitude tilt [-180..+180]
    bool               HasAtmosphere            = false;                        // [-]   - Forces visible off-limb planetary/lunar atmosphere
    float              AtmosphereIntensity      = 0.85f;                        // [x]   - Exosphere scatter brightness scale [0..3]
    float              AtmosphereThicknessScale = 0.22f;                        // [x]   - Outer limb atmosphere extension relative to radius
    Mathematics::LinearRGB AtmosphereTint       { 0.55f, 0.78f, 1.00f };        // [-]   - Exosphere rim/scatter hue (auto-blended with preset)
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
    bool  EnableBrightStarCatalog   = true;                                   // [-]   - Renders J2000 bright navigation stars (Sirius, Rigel, Orion, etc.)
    bool  EnableGalacticBand        = true;                                   // [-]   - Concentrates faint micro-stars along tilted Milky Way plane
    bool  ZodiacalLightEnabled      = true;                                   // [-]   - Enables Leinert (1998) ecliptic zodiacal light & gegenschein
    float ZodiacalRadianceScale     = 1.0f;                                   // [x]   - Zodiacal dust scatter radiance multiplier [0..4]
};

struct StarfieldPresetSpecification
{
    StarfieldPresetIdentifier Preset                 = StarfieldPresetIdentifier::GalacticBulge;
    const char*               Title                  = "";
    const char*               OutputSlug             = "";
    StarfieldConfiguration    Stars                  {};
    float                     SunAzimuthDegrees      = 270.0f;
    float                     SunElevationDegrees    = -40.0f;
    float                     CameraAzimuthDegrees   = 180.0f;
    float                     CameraElevationDegrees = 45.0f;
    float                     HorizontalFovDegrees   = 80.0f;
    float                     RecommendedExposure    = 1.80f;
};

struct StarfieldRepositoryEntry
{
    std::string            Name        {};
    Mathematics::Axis3D    Direction   { 0.0f, 0.0f, 1.0f };
    float                  MagnitudeV  = 2.0f;
    float                  ColorBv     = 0.0f;
    Mathematics::LinearRGB ColorLinear { 1.0f, 1.0f, 1.0f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LUNAR STARFIELD SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class LunarStarfieldSequence
{
public:
    static constexpr uint32_t AtlasTileWidthPx  = 512u;
    static constexpr uint32_t AtlasTileHeightPx = 256u;
    static constexpr uint32_t AtlasPresetCount  = 6u;
    static constexpr uint32_t MaxMoonSlots      = 6u;

    // Returns canonical linear RGB tint for a LunarSurfacePreset (used for atmospheric halo glow & procedural fallback)
    [[nodiscard]] static Mathematics::LinearRGB QueryPresetTint(LunarSurfacePreset Preset) noexcept;

    // Converts Planckian B-V color index [-0.35..+1.8] or stellar temperature K into LinearRGB blackbody starlight
    [[nodiscard]] static Mathematics::LinearRGB EvaluateStarColorFromBv(float BvIndex) noexcept;

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

    // Returns built-in J2000 bright navigation stars (Sirius, Orion, Aldebaran, Capella, Vega, etc.)
    [[nodiscard]] static std::span<const StarfieldRepositoryEntry> QueryJ2000BrightStarRepository() noexcept;

    // Builds named night-sky & twilight starfield framing presets (ZodiacalLightDusk, SouthernCrossCoalsack, WinterHunterBetelgeuse, MagellanicClouds)
    [[nodiscard]] static StarfieldPresetSpecification BuildStarfieldPreset(StarfieldPresetIdentifier Preset) noexcept;

    // Evaluates Leinert et al. (1998) S10 zodiacal light + gegenschein interplanetary dust scatter in equatorial J2000 coordinates
    [[nodiscard]] static Mathematics::LinearRGB EvaluateZodiacalLight(
        const Mathematics::Axis3D& EquatorialDirection,
        const Mathematics::Axis3D& SolarEquatorialDirection) noexcept;

    // Evaluates all active moon slots (up to 6 simultaneously with Lommel-Seeliger regolith & off-limb exospheres) + pinpoint stars
    [[nodiscard]] static Mathematics::LinearRGB EvaluateCelestialField(
        const Mathematics::Axis3D&            RayDirection,
        const Mathematics::Axis3D&            SunDirection,
        std::span<const LunarBodyConfiguration> Moons,
        const StarfieldConfiguration&         Stars,
        float                                 SimulationSeconds,
        const AtmosphericMediumConfiguration* AtmosphereMedium    = nullptr,
        std::span<const StarfieldRepositoryEntry> ExtraRepositoryStars  = {},
        float                                 PixelAngularRad     = (0.85f / 720.0f)) noexcept;
};

} // namespace Frontier::GeometricRaster
