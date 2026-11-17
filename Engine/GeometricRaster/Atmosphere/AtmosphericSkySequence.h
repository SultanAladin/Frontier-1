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
    Mathematics::LinearRGB GroundAlbedo               { 0.22f, 0.19f, 0.15f };       // [-] - Lower-hemisphere ground bounce albedo
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 ATMOSPHERIC SKY SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class AtmosphericSkySequence
{
public:
    static constexpr uint32_t SkyViewWidthPx       = 96u;
    static constexpr uint32_t SkyViewHeightPx      = 48u;
    static constexpr uint32_t TransmittanceWidthPx = 128u;
    static constexpr uint32_t TransmittanceHeightPx = 32u;

    // Evaluates optical transmittance from ground observer toward SunDirection
    [[nodiscard]] static Mathematics::LinearRGB ComputeSolarTransmittance(
        const Mathematics::Axis3D&            SunDirection,
        const AtmosphericMediumConfiguration& Medium) noexcept;

    // Rebuilds the 96x48 RGBA32F Bruneton/Hillaire SkyView LUT on CPU for CPU fallback & DDGI IBL parity
    void BuildSkyViewLut(
        const Mathematics::Axis3D&            SunDirection,
        const AtmosphericMediumConfiguration& Medium,
        float                                 SolarIntensityScale = 22.0f);

    // Samples the cached 96x48 SkyView LUT using non-linear elevation mapping
    [[nodiscard]] Mathematics::LinearRGB SampleSkyViewDirection(
        const Mathematics::Axis3D& WorldDirection) const noexcept;

    // Evaluates analytical solar disc + coronal scattering radiance along RayDirection
    [[nodiscard]] static Mathematics::LinearRGB EvaluateSolarCoronaRadiance(
        const Mathematics::Axis3D&         RayDirection,
        const Mathematics::Axis3D&         SunDirection,
        const SolarCelestialConfiguration& SolarConfig,
        const Mathematics::LinearRGB&      SolarTransmittance,
        const Mathematics::LinearRGB&      EffectiveSolarTint) noexcept;

    [[nodiscard]] const std::vector<float>& InspectSkyViewTexelsRgba32f() const noexcept { return SkyViewTexelsRgba; }
    [[nodiscard]] bool                      QueryLutReady() const noexcept { return SkyViewTexelsRgba.size() == SkyViewWidthPx * SkyViewHeightPx * 4u; }

private:
    std::vector<float> SkyViewTexelsRgba {}; // [rgba32f] - 96x48 SkyView spherical LUT
};

} // namespace Frontier::GeometricRaster
