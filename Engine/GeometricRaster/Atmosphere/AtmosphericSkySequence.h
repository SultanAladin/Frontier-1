/*============================================================================================================================================
                                                        ATMOSPHERICSKYSEQUENCE.H
============================================================================================================================================*/
// 🧩 Physical Bruneton/Hillaire Rayleigh + Mie + Ozone atmospheric transmittance, SkyView LUT, and solar disc/corona radiance

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

#include <cstdint>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphericMediumConfiguration
{
    bool                   AtmosphereEnabled          = false;                       // [-] - Enables physical sky dome + solar/lunar/cloud synthesis
    float                  RayleighStrength           = 1.0f;                        // [x] - Molecular scattering multiplier [0..4]
    float                  MieStrength                = 1.0f;                        // [x] - Aerosol haze multiplier [0..6]
    float                  MieAnisotropy              = 0.78f;                       // [-] - Henyey-Greenstein g [0..0.99]
    float                  OzoneStrength              = 1.2f;                        // [x] - Stratospheric ozone absorption [0..4]
    float                  RayleighScaleHeightMeters  = 8000.0f;                     // [m] - Molecular exponential scale height
    float                  MieScaleHeightMeters       = 1200.0f;                     // [m] - Aerosol exponential scale height
    float                  AtmosphereThicknessMeters  = 60000.0f;                    // [m] - Planetary atmosphere shell height
    float                  PlanetRadiusMeters         = 6360000.0f;                  // [m] - Planetary body radius
    float                  LimbGlowIntensity          = 1.0f;                        // [x] - Ground-limb scattering glow [0..3]
    float                  SkyBrightnessMultiplier    = 1.0f;                        // [x] - Sky dome global brightness gain [0..3]
    Mathematics::LinearRGB SkyTint                    { 1.0f, 1.0f, 1.0f };          // [-] - Multiplicative sky hue tint
    Mathematics::LinearRGB GroundAlbedo               { 0.08f, 0.12f, 0.18f };       // [-] - Neutral cool lower-hemisphere bounce albedo
    bool                   OrbitalViewEnabled         = false;                       // [-] - Enables outer-space planetary limb + star + sun rendering
    float                  ObserverAltitudeMeters     = 100.0f;                      // [m] - Observer altitude above sea level (e.g. 800000+ for orbit)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 ATMOSPHERIC SKY SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class AtmosphericSkySequence
{
public:
    static constexpr uint32_t SkyViewWidthPx       = 256u;
    static constexpr uint32_t SkyViewHeightPx      = 128u;
    static constexpr uint32_t TransmittanceWidthPx = 128u;
    static constexpr uint32_t TransmittanceHeightPx = 32u;

    // Evaluates optical transmittance from observer toward SunDirection
    [[nodiscard]] static Mathematics::LinearRGB ComputeSolarTransmittance(
        const Mathematics::Axis3D&            SunDirection,
        const AtmosphericMediumConfiguration& Medium,
        float                                 ObserverAltitudeOverrideM = -1.0f) noexcept;

    // Rebuilds the 256x128 RGBA32F Bruneton/Hillaire SkyView LUT on CPU for CPU fallback & DDGI IBL parity
    void BuildSkyViewLut(
        const Mathematics::Axis3D&            SunDirection,
        const AtmosphericMediumConfiguration& Medium,
        float                                 SolarIntensityScale = 22.0f);

    // Samples the cached 256x128 SkyView LUT using non-linear elevation mapping + Catmull-Rom vertical filtering
    [[nodiscard]] Mathematics::LinearRGB SampleSkyViewDirection(
        const Mathematics::Axis3D& WorldDirection) const noexcept;

    // Evaluates analytical solar disc + coronal scattering radiance along RayDirection
    [[nodiscard]] static Mathematics::LinearRGB EvaluateSolarCoronaRadiance(
        const Mathematics::Axis3D&         RayDirection,
        const Mathematics::Axis3D&         SunDirection,
        const SolarCelestialConfiguration& SolarConfig,
        const Mathematics::LinearRGB&      SolarTransmittance,
        const Mathematics::LinearRGB&      EffectiveSolarTint,
        bool                               OrbitalSpaceView = false) noexcept;

    // Evaluates planetary surface, clouds & curved Rayleigh/Mie atmospheric limb from outer-space orbit
    // OutSpaceTransmittance returns atmospheric transmittance [0..1] for attenuating background stars & sun behind the limb (0 on planet hit)
    [[nodiscard]] static Mathematics::LinearRGB EvaluateOrbitalSpaceRadiance(
        const Mathematics::Axis3D&            RayDirection,
        const Mathematics::Axis3D&            CameraOriginMeters,
        const Mathematics::Axis3D&            SunDirection,
        const AtmosphericMediumConfiguration& Medium,
        float                                 SolarIntensityScale,
        float                                 SimulationSeconds,
        float                                 CloudCoverage01,
        Mathematics::LinearRGB*               OutSpaceTransmittance = nullptr) noexcept;

    [[nodiscard]] const std::vector<float>& InspectSkyViewTexelsRgba32f() const noexcept { return SkyViewTexelsRgba; }
    [[nodiscard]] bool                      QueryLutReady() const noexcept { return SkyViewTexelsRgba.size() == SkyViewWidthPx * SkyViewHeightPx * 4u; }

private:
    std::vector<float> SkyViewTexelsRgba {}; // [rgba32f] - 96x48 SkyView spherical LUT
};

} // namespace Frontier::GeometricRaster
