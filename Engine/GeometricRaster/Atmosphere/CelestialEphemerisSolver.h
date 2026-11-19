/*============================================================================================================================================
                                                      CELESTIALEPHEMERISSOLVER.H
============================================================================================================================================*/
// 🧩 Astronomical solar ephemeris solver, Kelvin Planckian tint conversion, and Right-Handed (+Z Up) sky direction mapping

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <cstdint>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SolarTintSource : uint8_t
{
    RgbTint           = 0u, // [-] - Direct manual LinearRGB tint swatch
    KelvinTemperature = 1u, // [-] - Planckian blackbody color temperature curve
};

enum class SolarAppearanceMode : uint8_t
{
    Muted   = 0u, // [-] - Pure limb-darkened photosphere + K/F corona (zero optical glare veil)
    Radiant = 1u, // [-] - Adds physically-bounded optical glare bloom + wide clean-air veil
};

enum class DayCycleSpeedPreset : uint8_t
{
    Multiplier1   = 0u, // [-] - 24.0 h real-time cycle
    Multiplier8   = 1u, // [-] - 3.0 h cycle
    Multiplier30  = 2u, // [-] - 0.8 h cycle
    Multiplier100 = 3u, // [-] - 0.24 h cycle
    Custom        = 4u, // [-] - Custom DayDurationHours
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SolarCelestialConfiguration
{
    bool                SolarEnabled               = true;
    float               AngularDiameterDegrees     = 0.533f;                       // [deg] - Apparent solar disc diameter [0.1..5.0]
    SolarTintSource     TintSource                 = SolarTintSource::RgbTint;
    Mathematics::LinearRGB ManualRgbTint           { 1.0f, 1.0f, 1.0f };           // [-]   - Manual solar tint when TintSource == RgbTint
    float               ColorTemperatureKelvin     = 6500.0f;                      // [K]   - Kelvin tint [1000..40000]
    float               SolarIntensityMultiplier   = 22.0f;                        // [x]   - Atmospheric sky irradiance multiplier [0..60]
    float               DirectGainMultiplier       = 2.50f;                        // [x]   - Direct sunlit surface radiance scale [0..5]
    float               LocalHours                 = 15.50f;                       // [h]   - Observer local solar clock [0..24)
    bool                CycleAnimationEnabled      = false;                        // [-]   - Advances LocalHours over simulation dt
    DayCycleSpeedPreset CycleSpeedPreset           = DayCycleSpeedPreset::Multiplier8;
    float               DayDurationHours           = 3.0f;                         // [h]   - Full 24h day cycle duration [0.01..168]
    float               SiteLatitudeDegrees    = -26.19f;                      // [deg] - Geographic site latitude [-90..+90]
    float               SiteLongitudeDegrees   = 28.32f;                       // [deg] - Geographic site longitude [-180..+180]
    uint32_t            DayOfMonth                 = 10u;                          // [d]   - Calendar day [1..31]
    uint32_t            Month                      = 9u;                           // [mo]  - Calendar month [1..12]
    int32_t             Year                       = 2026;                         // [yr]  - Calendar year
    float               UtcOffsetHours             = 2.0f;                         // [h]   - Observer UTC timezone offset [-12..+14]
    bool                ManualDirectionOverride    = false;                        // [-]   - Overrides ephemeris with ManualAzimuth/Elevation
    float               ManualAzimuthDegrees       = 295.0f;                       // [deg] - Manual compass bearing from North [0..360)
    float               ManualElevationDegrees     = 38.0f;                        // [deg] - Manual altitude angle [-90..+90]
    SolarAppearanceMode AppearanceMode             = SolarAppearanceMode::Radiant; // [-]   - Muted disc or Radiant glare veil
    float               SunGlareStrength           = 0.25f;                        // [x]   - Optical glare veil strength [0..2] (Radiant mode)
};

struct SolarEphemerisOutcome
{
    float                  ElevationDegrees       = 38.0f;                           // [deg] - Geometric solar elevation above ground tangent
    float                  AzimuthDegrees         = 295.0f;                          // [deg] - Compass azimuth clockwise from North (+X)
    float                  DeclinationDegrees     = 0.0f;                            // [deg] - Solar declination angle
    float                  EquationOfTimeMinutes  = 0.0f;                            // [min] - Equation of time offset
    float                  SiderealHours          = 0.0f;                            // [h]   - Local apparent sidereal time
    Mathematics::Axis3D    WorldDirectionToSun    { 0.42f, -0.55f, 0.72f };          // [-]   - Right-handed (+X North, +Y West, +Z Up) unit vector
    Mathematics::LinearRGB EffectiveSolarTint     { 1.0f, 1.0f, 1.0f };              // [-]   - Resolved linear RGB solar tint
};

//------------------------------------------------------------------------------------------------------------------------
//                                           CELESTIAL EPHEMERIS SOLVER
//------------------------------------------------------------------------------------------------------------------------

class CelestialEphemerisSolver
{
public:
    // Converts compass Azimuth (0=North/+X, 90=East/-Y, 180=South/-X, 270=West/+Y) & Elevation to Right-Handed +Z Up unit axis
    [[nodiscard]] static Mathematics::Axis3D ConvertAzimuthElevationToWorldAxis(
        float AzimuthDegrees,
        float ElevationDegrees) noexcept;

    // Evaluates Planckian color temperature [1000..40000 K] in LinearRGB
    [[nodiscard]] static Mathematics::LinearRGB EvaluateKelvinToLinearRgb(float Kelvin) noexcept;

    // Solves astronomical solar position, sidereal time, and effective tint without atmospheric refraction
    [[nodiscard]] static SolarEphemerisOutcome SolveSolarEphemeris(
        const SolarCelestialConfiguration& Config) noexcept;

    // Advances LocalHours monotonically when CycleAnimationEnabled is active
    static void AdvanceDayCycleHours(
        SolarCelestialConfiguration& InOutConfig,
        double                       DeltaSeconds) noexcept;
};

} // namespace Frontier::GeometricRaster
