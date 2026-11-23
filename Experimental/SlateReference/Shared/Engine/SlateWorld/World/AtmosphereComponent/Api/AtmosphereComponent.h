//============================================================================================================================================
//                                                      ATMOSPHERECOMPONENT.H
//============================================================================================================================================
// 🧩 Host-independent dynamic sun, sky and scattering-medium state with an explicit GPU invalidation graph.

#pragma once

#include <cstdint>

namespace Slate
{

enum class AtmosphereDirty : std::uint32_t
{
    None       = 0u,
    Presentation = 1u << 0u, // intensity, exposure, temperature or solar-disc appearance
    SkyView      = 1u << 1u, // sun direction or camera altitude
    Medium       = 1u << 2u  // transmittance, multiple scattering and sky view
};

constexpr AtmosphereDirty operator|(AtmosphereDirty Left, AtmosphereDirty Right)
{
    return static_cast<AtmosphereDirty>(static_cast<std::uint32_t>(Left) |
                                        static_cast<std::uint32_t>(Right));
}
constexpr bool Includes(AtmosphereDirty Value, AtmosphereDirty Test)
{
    return (static_cast<std::uint32_t>(Value) & static_cast<std::uint32_t>(Test)) != 0u;
}

struct AtmosphereState
{
    double SunElevation = 35.0;
    double SunAzimuth = 120.0;
    double SunIlluminance = 4.8;
    double SunTemperature = 5500.0;
    double SunAngularRadius = 0.266;
    double SunDiscIntensity = 0.95;
    double SkyIntensity = 1.0;
    double ExposureCompensation = 0.0;
    double GroundAlbedo = 0.1;

    double PlanetRadiusKilometres = 6360.0;
    double AtmosphereHeightKilometres = 100.0;
    double RayleighDensity = 1.0;
    double RayleighScaleHeightKilometres = 8.0;
    double MieDensity = 1.0;
    double MieScaleHeightKilometres = 1.2;
    double MieAsymmetry = 0.8;
    double OzoneDensity = 1.0;
    double OzoneCentreKilometres = 25.0;
    double OzoneWidthKilometres = 15.0;
    double CameraAltitudeKilometres = 0.0015;
};

class AtmosphereComponent
{
public:
    /// Classifies and accepts a complete authored state. The first state invalidates every surface.
    AtmosphereDirty Apply(const AtmosphereState& Authored);
    const AtmosphereState& State() const { return Current; }

    void AdvanceTime(double Seconds);
    void SetTimeAnimation(bool Enabled, double DegreesPerSecond);

private:
    AtmosphereState Current = {};
    bool Standing = false;
    bool TimeAnimated = false;
    double SolarDegreesPerSecond = 0.0;
};

} // namespace Slate
