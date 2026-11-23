//============================================================================================================================================
//                                                  ENVIRONMENTCONFIGURATION.SLANG.H
//============================================================================================================================================
// 🧩 The atmosphere the artist has dialled in: where the sun is, how bright it is, and what the air is
//    made of.
//
// 🔴 THIS LIVED IN `SlateUI/Interface/SceneDirectoryPanel/`, WHICH MADE IT UNREACHABLE BY THE CODE THAT
//    EVALUATES IT. Every field here is physics — Rayleigh density, Mie asymmetry, ozone, scale heights —
//    and not one of them describes a panel. But `SkyDomeImage` in `SlateCompute` takes one, and
//    `SlateUI`'s link list names `SlateCompute`, so declaring the dependency the other way closed a CYCLE
//    in the unit graph and the build refused. Caught by `make sequence`, not by a syntax check.
//
// 📝 `Shared/` is the one place both an interface panel and a compute unit may read, which is exactly what
//    a settings block written by sliders and consumed by physics needs to be.

#pragma once

#include <cstdint>

namespace Slate
{

/// 🧩 Every parameter the editor's sun, sky and atmosphere present, owned by the host and written through
///    by the inspector's slider cards.
/// note  🔴 Phase 1 presents these as editable ordinates and renders a stylised sky from them. The values are
///        exactly what the atmosphere shaders will read in phase 2, so the property surface does not move
///        when the renderer stops being a placeholder.
/// tag   guarantee, nonallocating, nonthrowing
struct EnvironmentConfiguration
{
    double SunElevation = 35.0;       // [deg] - above the horizon, 0…90
    double SunAzimuth   = 120.0;      // [deg] - clockwise from north, 0…360
    double SunIntensity = 4.8;        // [lx]  - the directional illuminant's illuminance
    double SunTemperature = 5500.0;   // [K]   - the sun's colour temperature, 1000…12000
    double SunDiscRadius = 8.0;       // [-]   - multiplier on the analytic solar angular radius
    double SunDiscIntensity = 0.95;   // [-]   - the direct term's strength, 0…4
    double DayCycleDegreesPerSecond = 0.0; // [deg/s] - zero pauses runtime time-of-day motion
    double SunShadowStrength = 1.0;   // [-]   - directional-light shadow contribution, 0…1
    double SkyIntensity = 1.0;        // [-]   - the sky dome's luminance scale, 0…3
    double SkyTurbidity = 2.0;        // [-]   - artist-facing haze control, 1…10
    double ExposureCompensation = 0.0; // [EV] - presentation-only exposure, -8…8
    double GroundAlbedo = 0.1;        // [-]   - average ground reflectance, 0…1
    double AtmosphereDensity = 1.0;   // [-]   - the Rayleigh density scale, 0…3
    double AtmosphereScaleHeight = 1.0; // [-] - Rayleigh scale-height multiplier, 0.2…3
    double MieDensity = 1.0;          // [-]   - the Mie scattering scale, 0…4
    double MieScaleHeightKilometres = 1.2; // [km] - aerosol scale height, 0.1…8
    double MieAsymmetry = 0.80;       // [-]   - the Mie forward-scattering asymmetry, -0.95…0.95
    double OzoneDensity = 1.0;        // [-]   - Chappuis absorption multiplier, 0…3
    std::uint32_t AtmosphereQuality = 2u; // [-] - 0 Preview, 1 Balanced, 2 High, 3 Ultra
};

}   // namespace Slate
