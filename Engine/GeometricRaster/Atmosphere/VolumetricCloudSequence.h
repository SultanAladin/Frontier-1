/*============================================================================================================================================
                                                        VOLUMETRICCLOUDSEQUENCE.H
============================================================================================================================================*/
// 🧩 Anti-banded 3D multi-scattering planetary volumetric cloud deck (12 formations), BoundedCloudVolume, and solar cloud shadows

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricFogSequence.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class CloudCategoryType : uint8_t
{
    Stratus         = 0u,  // [-] - Low uniform layered blanket
    Stratocumulus   = 1u,  // [-] - Broken convective rolls
    Cumulus         = 2u,  // [-] - Classic puffy fair-weather towers
    Cumulonimbus    = 3u,  // [-] - Deep vertical anvil storm deck
    Altostratus     = 4u,  // [-] - Mid-altitude sheet veil
    Cirrus          = 5u,  // [-] - High ice-crystal wisps
    Lenticular      = 6u,  // [-] - Stationary stacked orographic laminar lenses
    Mammatus        = 7u,  // [-] - Storm-anvil hanging cellular pouches
    KelvinHelmholtz = 8u,  // [-] - Breaking-wave wind-shear instability curls
    Asperitas       = 9u,  // [-] - Undulating dark-lit gravity-wave undersides
    Nacreous        = 10u, // [-] - Polar stratospheric iridescent mother-of-pearl ribbons
    Pyrocumulus     = 11u, // [-] - Towering convective plume with warm ember under-glow
};

enum class LensFlareStyle : uint8_t
{
    Anamorphic = 0u, // [-] - Horizontal cinema anamorphic blue-cyan flare with lateral wings & halos
    Streaks    = 1u, // [-] - Multi-blade iris starburst diffraction streaks with spectral spokes & halos
    Halos      = 2u, // [-] - Concentric spectral rainbow diffraction halos, aureole & coated aperture ghosts
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CloudShadowConfiguration
{
    bool              ShadowEnabled           = true;
    CloudCategoryType ShadowCategory          = CloudCategoryType::Cumulus;
    float             ShadowFraction          = 1.0f;
    float             ShadowDensityMultiplier = 1.0f;
    float             ShadowScale             = 1.0f;
    float             ShadowAnvil             = 1.0f;
    float             ShadowFloorMeters       = 1.0f;
    float             ShadowThicknessMeters   = 50.0f;
};

struct CloudDeckConfiguration
{
    bool                     CloudsEnabled            = true;
    bool                     FollowWindEnabled        = true;
    WindBindingSource        WindSource               = WindBindingSource::CloudWind;
    CloudCategoryType        Category                 = CloudCategoryType::Cumulus;
    float                    CloudFraction            = 0.34f;   // [0..1]   - Fractional sky cover
    float                    ExtinctionMultiplier     = 2.40f;   // [x]      - Volumetric optical density scale [0..4]
    float                    FeatureScaleMultiplier   = 0.35f;   // [x]      - Noise octave wavelength scale [0.2..3.0]
    float                    CloudFloorMeters         = 1400.0f; // [m]      - Base altitude above sea level [100..15000]
    float                    LayerThicknessMeters     = 1100.0f; // [m]      - Vertical cloud depth [100..6500]
    float                    DeckCeilingMeters        = 16000.0f;// [m]      - Maximum ceiling [4000..22000]
    float                    AnvilTopSpread           = 0.50f;   // [0..1]   - Upper anvil widening factor
    float                    ForwardScatterAnisotropy = 0.45f;   // [-.9..9] - Henyey-Greenstein phase g
    float                    WindSpeedMetersPerSecond = 7.0f;    // [m/s]    - Advection wind speed [0..40]
    float                    WindBearingDegrees       = 250.0f;  // [deg]    - Advection wind bearing [0..360)
    float                    WindShearPerKilometer    = 0.35f;   // [/km]    - Vertical altitude shear
    float                    WindVeerDegreesPerKm     = 12.0f;   // [d/km]   - Ekman wind bearing rotation with altitude [-60..60]
    float                    WindTurbulence           = 0.30f;   // [0..1]   - Wind eddy warp on cloud silhouettes
    CloudShadowConfiguration CloudShadow              {};
};

struct BoundedCloudVolume
{
    bool                   LocalEnabled             = false;
    bool                   FollowWindEnabled        = true;
    WindBindingSource      WindSource               = WindBindingSource::LocalCloudWind;
    AtmosphericVolumeShape Shape                    = AtmosphericVolumeShape::Box;
    uint32_t               PrismSides               = 6u;
    Mathematics::Axis3D    VolumeCenterMeters       { -100.0f, 260.0f, 130.0f };
    Mathematics::Scalar3D  HalfExtentMeters         { 100.0f, 100.0f, 50.0f };
    float                  YawDegrees               = 0.0f;
    float                  EdgeSoftness             = 0.35f;
    float                  ExtinctionMultiplier     = 2.50f;
    float                  CloudFraction            = 0.80f;
    float                  FeatureScaleMeters       = 40.0f;
    float                  ForwardScatterAnisotropy = 0.45f;
};

struct LensFlareConfiguration
{
    bool           Enabled               = false;                      // [-]   - Master physical lens flare toggle
    LensFlareStyle Style                 = LensFlareStyle::Anamorphic; // [-]   - Active baked optical archetype (0..2)
    float          FlareIntensity        = 0.85f;                      // [x]   - Primary baked optical flare gain [0..3]
    float          FlareSpread           = 0.85f;                      // [x]   - Kernel angular footprint scale [0.2..2.0]
    float          HaloIntensity         = 0.65f;                      // [x]   - Concentric spectral halo gain [0..2.5]
    float          GhostIntensity        = 0.50f;                      // [x]   - Multi-coated polygonal aperture ghost gain [0..2.5]
    float          GhostSpacing          = 0.72f;                      // [x]   - Axial ghost reflection spread [0.2..1.8]
    uint32_t       GhostCount            = 7u;                         // [cnt] - Coated aperture ghost count [0..16]
    uint32_t       GhostSides            = 6u;                         // [cnt] - Polygonal aperture blade count (0=round, 6=hex, 8=oct)
    float          StreakRotationDegrees = 10.0f;                      // [deg] - Diffraction streak rotation [0..360)
};

struct WorldAtmosphereConfiguration
{
    AtmosphericMediumConfiguration Medium             {};
    SolarCelestialConfiguration    Solar              {};
    std::array<LunarBodyConfiguration, 4u> Moons      {{
        { true, false, LunarSurfacePreset::Luna,    0.62f, 1.60f, 0.80f, 0.52f, 300.0f, 28.0f, 0.0f, 0.0f },
        { false, false, LunarSurfacePreset::Ember,  0.35f, 1.40f, 0.90f, 0.78f, 115.0f, 22.0f, 12.0f, 0.0f },
        { false, false, LunarSurfacePreset::Glacier,0.80f, 1.25f, 0.65f, 0.44f, 210.0f, 41.0f, 0.0f, -8.0f },
        { false, false, LunarSurfacePreset::Sulfur, 0.50f, 1.80f, 1.00f, 0.60f, 45.0f,  19.0f, 0.0f, 0.0f },
    }};
    StarfieldConfiguration         Stars              {};
    AtmosphericWindParameters      Wind               {};
    CloudDeckConfiguration         Clouds             {};
    BoundedCloudVolume             LocalCloud         {};
    std::vector<BoundedCloudVolume> LocalClouds       {};
    AtmosphericFogConfiguration    Fog                {};
    LensFlareConfiguration         LensFlare          {};
    float                          SimulationSeconds  = 0.0f;
    const SimulationSpace::ForceFields::ForceFieldAccumulator* ForceFieldSource   = nullptr;
    const SimulationSpace::ForceFields::FieldLatticeCascade*   WindLatticeCascade = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              VOLUMETRIC CLOUD SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class VolumetricCloudSequence
{
public:
    static constexpr uint32_t PresetCount             = 12u;
    static constexpr uint32_t MaxGpuLocalCloudVolumes = 8u;

    // Applies canonical physical parameters for any of the 12 CloudCategoryTypes
    static void ApplyCloudCategoryPreset(
        CloudDeckConfiguration& InOutDeck,
        CloudCategoryType       Category) noexcept;

    // Evaluates depth-clipped local BoundedCloudVolumes (for foreground geometry hits and sky rays) with wind & ForceFields advection
    [[nodiscard]] static Mathematics::LinearRGB EvaluateLocalCloudsOverSegment(
        const Mathematics::Axis3D&                               RayOriginWorld,
        const Mathematics::Axis3D&                               RayDirection,
        float                                                    MaxDistanceMeters,
        const Mathematics::Axis3D&                               SunDirection,
        const Mathematics::LinearRGB&                            SolarIrradiance,
        const Mathematics::LinearRGB&                            SkyRadiance,
        const WorldAtmosphereConfiguration&                      Atmosphere,
        float&                                                   InOutTransmittance,
        float                                                    PixelJitter01 = -1.0f) noexcept;

    // Evaluates raymarched global planetary cloud shell + optional BoundedCloudVolume with 3-octave multi-scattering & IGN anti-banding
    [[nodiscard]] static Mathematics::LinearRGB EvaluateVolumetricClouds(
        const Mathematics::Axis3D&            RayOriginWorld,
        const Mathematics::Axis3D&            RayDirection,
        const Mathematics::Axis3D&            SunDirection,
        const Mathematics::LinearRGB&         SolarIrradiance,
        const Mathematics::LinearRGB&         SkyRadiance,
        const CloudDeckConfiguration&         Deck,
        const BoundedCloudVolume&             LocalVolume,
        const AtmosphericMediumConfiguration& Medium,
        float                                 SimulationSeconds,
        float&                                OutTransmittance,
        float                                 PixelJitter01 = -1.0f) noexcept;

    // Overload taking full WorldAtmosphereConfiguration (Global Wind + ForceFields + multi-volume LocalClouds)
    [[nodiscard]] static Mathematics::LinearRGB EvaluateVolumetricCloudsWithWind(
        const Mathematics::Axis3D&            RayOriginWorld,
        const Mathematics::Axis3D&            RayDirection,
        const Mathematics::Axis3D&            SunDirection,
        const Mathematics::LinearRGB&         SolarIrradiance,
        const Mathematics::LinearRGB&         SkyRadiance,
        const WorldAtmosphereConfiguration&   Atmosphere,
        float&                                OutTransmittance,
        float                                 PixelJitter01 = -1.0f) noexcept;

    // Evaluates soft volumetric cloud optical transmittance along a sun ray (deck only)
    [[nodiscard]] static float EvaluateSolarCloudTransmittance(
        const Mathematics::Axis3D&    WorldPos,
        const Mathematics::Axis3D&    SunDirection,
        const CloudDeckConfiguration& Deck,
        float                         PlanetRadiusMeters,
        float                         SimulationSeconds) noexcept;

    // Evaluates combined global CloudDeck + LocalCloud/LocalClouds volumetric transmittance along a sun ray
    [[nodiscard]] static float EvaluateAllSolarCloudTransmittance(
        const Mathematics::Axis3D&          WorldPos,
        const Mathematics::Axis3D&          SunDirection,
        const WorldAtmosphereConfiguration& Atmosphere) noexcept;
};

} // namespace Frontier::GeometricRaster
