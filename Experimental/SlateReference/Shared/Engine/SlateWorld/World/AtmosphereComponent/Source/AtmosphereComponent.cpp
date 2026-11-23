//============================================================================================================================================
//                                                     ATMOSPHERECOMPONENT.CPP
//============================================================================================================================================

#include "SlateWorld/World/AtmosphereComponent/Api/AtmosphereComponent.h"

#include <cmath>

namespace Slate
{
namespace
{
bool Changed(double Left, double Right) { return std::abs(Left - Right) > 1.0e-9; }
}

AtmosphereDirty AtmosphereComponent::Apply(const AtmosphereState& Authored)
{
    if (!Standing)
    {
        Current = Authored;
        Standing = true;
        return AtmosphereDirty::Medium | AtmosphereDirty::SkyView | AtmosphereDirty::Presentation;
    }

    AtmosphereDirty Dirty = AtmosphereDirty::None;

    if (Changed(Current.PlanetRadiusKilometres, Authored.PlanetRadiusKilometres) ||
        Changed(Current.AtmosphereHeightKilometres, Authored.AtmosphereHeightKilometres) ||
        Changed(Current.RayleighDensity, Authored.RayleighDensity) ||
        Changed(Current.RayleighScaleHeightKilometres, Authored.RayleighScaleHeightKilometres) ||
        Changed(Current.MieDensity, Authored.MieDensity) ||
        Changed(Current.MieScaleHeightKilometres, Authored.MieScaleHeightKilometres) ||
        Changed(Current.MieAsymmetry, Authored.MieAsymmetry) ||
        Changed(Current.OzoneDensity, Authored.OzoneDensity) ||
        Changed(Current.OzoneCentreKilometres, Authored.OzoneCentreKilometres) ||
        Changed(Current.OzoneWidthKilometres, Authored.OzoneWidthKilometres) ||
        Changed(Current.GroundAlbedo, Authored.GroundAlbedo))
        Dirty = Dirty | AtmosphereDirty::Medium;

    if (Changed(Current.SunElevation, Authored.SunElevation) ||
        Changed(Current.SunAzimuth, Authored.SunAzimuth) ||
        Changed(Current.CameraAltitudeKilometres, Authored.CameraAltitudeKilometres))
        Dirty = Dirty | AtmosphereDirty::SkyView;

    if (Changed(Current.SunIlluminance, Authored.SunIlluminance) ||
        Changed(Current.SunTemperature, Authored.SunTemperature) ||
        Changed(Current.SunAngularRadius, Authored.SunAngularRadius) ||
        Changed(Current.SunDiscIntensity, Authored.SunDiscIntensity) ||
        Changed(Current.SkyIntensity, Authored.SkyIntensity) ||
        Changed(Current.ExposureCompensation, Authored.ExposureCompensation))
        Dirty = Dirty | AtmosphereDirty::Presentation;

    Current = Authored;
    return Dirty;
}

void AtmosphereComponent::SetTimeAnimation(bool Enabled, double DegreesPerSecond)
{
    TimeAnimated = Enabled;
    SolarDegreesPerSecond = DegreesPerSecond;
}

void AtmosphereComponent::AdvanceTime(double Seconds)
{
    if (!TimeAnimated || Seconds <= 0.0)
        return;
    Current.SunAzimuth = std::fmod(Current.SunAzimuth + SolarDegreesPerSecond * Seconds, 360.0);
    if (Current.SunAzimuth < 0.0)
        Current.SunAzimuth += 360.0;
}

} // namespace Slate
