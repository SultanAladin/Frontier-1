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
    Mathematics::LinearRGB GroundAlbedo               { 0.045f, 0.088f, 0.165f };    // [-] - Lower-hemisphere planetary aerial haze albedo
    bool                   OrbitalViewEnabled         = false;                       // [-] - Enables true 3D outer-space planetary limb, continents & cloud shell
    float                  ObserverAltitudeMeters     = 100.0f;                      // [m] - Camera altitude above planet datum (> AtmoThickness engages orbital space)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 ATMOSPHERIC SKY SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class AtmosphericSkySequence
{
public:
    static constexpr uint32_t SkyViewWidthPx         = 192u;
    static constexpr uint32_t SkyViewHeightPx        = 108u;
    static constexpr uint32_t TransmittanceWidthPx   = 256u;
    static constexpr uint32_t TransmittanceHeightPx  = 64u;
    static constexpr uint32_t MultiScatterWidthPx    = 32u;
    static constexpr uint32_t MultiScatterHeightPx   = 32u;
    static constexpr uint32_t SkyViewTexelCount      = SkyViewWidthPx * SkyViewHeightPx;             // 20,736
    static constexpr uint32_t TransmittanceTexelCount = TransmittanceWidthPx * TransmittanceHeightPx; // 16,384
    static constexpr uint32_t MultiScatterTexelCount = MultiScatterWidthPx * MultiScatterHeightPx;   //  1,024
    static constexpr uint32_t TotalPackedLutTexels   = SkyViewTexelCount + TransmittanceTexelCount + MultiScatterTexelCount; // 38,144

    // Evaluates optical transmittance from ground/altitude observer toward SunDirection
    [[nodiscard]] static Mathematics::LinearRGB ComputeSolarTransmittance(
        const Mathematics::Axis3D&            SunDirection,
        const AtmosphericMediumConfiguration& Medium,
        float                                 ObserverAltitudeMeters = 100.0f) noexcept;

    // Rebuilds 256x64 Transmittance LUT + 32x32 Multi-Scatter LUT (when medium changed) and 192x108 geometric-limb SkyView LUT
    void BuildSkyViewLut(
        const Mathematics::Axis3D&            SunDirection,
        const AtmosphericMediumConfiguration& Medium,
        float                                 SolarIntensityScale = 22.0f,
        float                                 ObserverAltitudeMeters = 100.0f);

    // Samples the cached 256x64 spherical-horizon Transmittance LUT at AltitudeMeters and PolarCosine
    [[nodiscard]] Mathematics::LinearRGB SampleTransmittanceLutDirection(
        const AtmosphericMediumConfiguration& Medium,
        float                                 AltitudeMeters,
        float                                 PolarCosine) const noexcept;

    // Samples the cached 32x32 isotropic infinite-order Multi-Scatter LUT at AltitudeMeters and SolarCosine
    [[nodiscard]] Mathematics::LinearRGB SampleMultiScatterLut(
        const AtmosphericMediumConfiguration& Medium,
        float                                 AltitudeMeters,
        float                                 SolarCosine) const noexcept;

    // Samples the cached 192x108 SkyView LUT using geometric-limb split + vertical Catmull-Rom filtering (zero horizon bleed)
    [[nodiscard]] Mathematics::LinearRGB SampleSkyViewDirection(
        const Mathematics::Axis3D& WorldDirection) const noexcept;

    // Evaluates Abramowitz & Stegun error function erf(Argument) for sub-pixel optical integration & C-infinity edge roll-off
    [[nodiscard]] static float EvaluateSolarErf(float Argument) noexcept;

    // Evaluates Neckel & Labs (1994) 5th-order wavelength-dependent solar photospheric limb darkening at 650/550/450 nm
    [[nodiscard]] static Mathematics::LinearRGB EvaluateNeckelLabsLimbDarkening(float MuPhotosphere) noexcept;

    // Evaluates radial optical glare bloom + clean-air aureole profile bounded within 30 deg solar reach
    [[nodiscard]] static float EvaluateSolarGlareSpread(
        float SeparationRadians,
        float DiscRadiusRadians) noexcept;

    // Evaluates analytical solar disc (Neckel & Labs limb darkening + erfc PSF + <3 px SolarErf fallback), K+F corona & Radiant/Muted glare
    [[nodiscard]] static Mathematics::LinearRGB EvaluateSolarCoronaRadiance(
        const Mathematics::Axis3D&            RayDirection,
        const Mathematics::Axis3D&            SunDirection,
        const SolarCelestialConfiguration&    SolarConfig,
        const Mathematics::LinearRGB&         SolarTransmittance,
        const Mathematics::LinearRGB&         EffectiveSolarTint,
        bool                                  AllowBelowHorizon   = false,
        const AtmosphericMediumConfiguration* OptionalMedium      = nullptr,
        float                                 PixelAngularSizeRad = 0.0f,
        const Mathematics::LinearRGB&         ForegroundOcclusion = { 1.0f, 1.0f, 1.0f },
        float                                 GlareEclipseVis     = 1.0f) noexcept;

    // Evaluates outer-space planetary sphere, atmospheric Rayleigh/Mie limb, terminator, continents/oceans & 3D planetary clouds
    [[nodiscard]] static Mathematics::LinearRGB EvaluateOrbitalSpaceRadiance(
        const Mathematics::Axis3D&            CameraLocalOriginMeters,
        const Mathematics::Axis3D&            RayDirection,
        const Mathematics::Axis3D&            SunDirection,
        const SolarCelestialConfiguration&    SolarConfig,
        const AtmosphericMediumConfiguration& Medium,
        bool                                  IncludeOrbitalClouds,
        float                                 CloudCoverage01,
        float                                 SimulationSeconds,
        const struct CloudDeckConfiguration*  OptionalDeck         = nullptr,
        Mathematics::LinearRGB*               OutSpaceTransmittance = nullptr) noexcept;

    [[nodiscard]] const std::vector<float>& InspectSkyViewTexelsRgba32f()   const noexcept { return SkyViewTexelsRgba; }
    [[nodiscard]] const std::vector<float>& InspectTransmittanceTexelsRgb() const noexcept { return TransmittanceTexelsRgb; }
    [[nodiscard]] const std::vector<float>& InspectMultiScatterTexelsRgb()  const noexcept { return MultiScatterTexelsRgb; }
    [[nodiscard]] bool                      QueryLutReady()                 const noexcept { return SkyViewTexelsRgba.size() == SkyViewTexelCount * 4u; }
    [[nodiscard]] bool                      QueryFoundationLutsReady()      const noexcept
    {
        return TransmittanceTexelsRgb.size() == TransmittanceTexelCount * 3u &&
               MultiScatterTexelsRgb.size()  == MultiScatterTexelCount  * 3u;
    }

private:
    std::vector<float> SkyViewTexelsRgba      {}; // [rgba32f] - 192x108 geometric-limb-centered SkyView LUT
    std::vector<float> TransmittanceTexelsRgb {}; // [rgb32f]  - 256x64 spherical-horizon Transmittance LUT
    std::vector<float> MultiScatterTexelsRgb  {}; // [rgb32f]  - 32x32 isotropic infinite-order Multi-Scatter LUT
    float              BakedBottomRadiusKm    = 6360.0f;
    float              BakedTopRadiusKm       = 6420.0f;
    float              BakedStartRadiusKm     = 6360.1f;
    float              BakedRayStrength       = -1.0f;
    float              BakedMieStrength       = -1.0f;
    float              BakedOzoneStrength     = -1.0f;
    float              BakedRayScaleH         = -1.0f;
    float              BakedMieScaleH         = -1.0f;
    float              BakedGroundAlbedoSum   = -1.0f;
};

} // namespace Frontier::GeometricRaster
