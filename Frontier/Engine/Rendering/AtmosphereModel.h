/*==========================================================================================================================================
                                                      ATMOSPHERE MODEL
                                     ------------------------------------------------------------------
                 Rayleigh/Mie/ozone medium, transmittance integration + LUT, phase functions,
                      single + approximated multiple scattering (plan §8.1 atmosphere group).
==========================================================================================================================================*/

// 🧩 Atmosphere model: the portable form of the demo's Atmosphere* units (Medium/Transmittance/
//    Multiple/Scattering). Canonical Earth parameters (Bruneton & Neyret 2008; Hillaire 2020).
//    Parameter parity with the demonstration's AtmosphereScattering.wgsl was verified on
//    2026-10-26 (identical Rayleigh/Mie/ozone coefficients, scale heights, radii, g = 0.8) and is
//    pinned by a check in Exhibits/NightSky. The aerosol phase now uses the demonstration's
//    normalized Cornette–Shanks form; HG remains available for other media. Units are kilometres internally
//    (the LUT-friendly convention of every published model); the public API converts at the
//    boundary where noted. Double precision so validation can hold the numeric integrator to
//    analytic optical depths.

#pragma once

#include "ClusterNesting.h"   // SpatialVector

#include <cstdint>
#include <vector>

namespace Frontier
{

// RGB triplet in linear space (radiance-like quantities; exact spectral meaning is the caller's).
struct SpectrumRgb
{
    double R = 0.0;
    double G = 0.0;
    double B = 0.0;
};

struct AtmosphereParameters
{
    double     GroundRadiusKm    = 6360.0;   // [km]
    double     TopRadiusKm       = 6460.0;   // [km]
    SpectrumRgb RayleighScattering{5.802e-3, 13.558e-3, 33.1e-3};   // [1/km] at sea level
    double     RayleighScaleHeightKm = 8.0;                        // [km]
    double     MieScattering     = 3.996e-3;   // [1/km]
    // 📝 Bruneton/Hillaire Mie extinction is 4.40e-6/m in total, so the absorption remainder is
    //    (4.40 - 3.996)e-3/km and the aerosol single-scattering albedo is 0.908. The demonstration's
    //    AtmosphereScattering.wgsl carries the same split (MieScatter 3.996e-6, MieExtinction 4.40e-6 per metre).
    double     MieAbsorption     = 0.404e-3;   // [1/km] extinction remainder, 4.40e-3 total
    double     MieScaleHeightKm  = 1.2;        // [km]
    double     MiePhaseG         = 0.8;        // [-] Henyey-Greenstein asymmetry
    SpectrumRgb OzoneAbsorption{0.65e-3, 1.881e-3, 0.085e-3};       // [1/km] at layer peak
    double     OzonePeakKm       = 25.0;       // [km]
    double     OzoneWidthKm      = 15.0;       // [km] tent half-width
    double     GroundAlbedo      = 0.1;        // [-] for the multiple-scattering ground term
};

// Densities (dimensionless, 1.0 at sea level for Rayleigh/Mie; ozone is a tent peaking at 1).
[[nodiscard]] double RayleighDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept;
[[nodiscard]] double MieDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept;
[[nodiscard]] double OzoneDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept;

// Phase functions (normalised: integral over the sphere is 1).
[[nodiscard]] double RayleighPhase(double CosTheta) noexcept;
[[nodiscard]] double HenyeyGreensteinPhase(double CosTheta, double G) noexcept;
[[nodiscard]] double CornetteShanksPhase(double CosTheta, double G) noexcept;

// Transmittance between two points (planet-centred coordinates, km), by numeric integration.
// Ground intersection truncates the path (returns 0 beyond the surface via the segment length).
[[nodiscard]] SpectrumRgb TransmittanceBetween(const AtmosphereParameters& P, SpatialVector A, SpatialVector B,
                                               uint32_t Steps = 64) noexcept;

// Transmittance from a point at RadiusKm from the centre toward CosZenith, to the top of the
// atmosphere (or to the ground when looking down). This is exactly what the LUT tabulates.
[[nodiscard]] SpectrumRgb TransmittanceToTop(const AtmosphereParameters& P, double RadiusKm, double CosZenith,
                                             uint32_t Steps = 64) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSMITTANCE LUT
//------------------------------------------------------------------------------------------------------------------------

// Baked transmittance over (altitude, cos zenith) with a horizon-stretching parameterisation
// (u = 0.5 + 0.5*sign(mu)*|mu|^0.35 concentrates texels where transmittance changes fastest).
class TransmittanceLut
{
  public:
    bool Bake(const AtmosphereParameters& P, uint32_t WidthMu = 128, uint32_t HeightAltitude = 32,
              uint32_t IntegrationSteps = 48);

    [[nodiscard]] SpectrumRgb Sample(const AtmosphereParameters& P, double RadiusKm, double CosZenith) const noexcept;
    [[nodiscard]] bool        Baked() const noexcept { return !Values_.empty(); }

  private:
    std::vector<SpectrumRgb> Values_;   // [altitude][mu]
    uint32_t                 WidthMu_        = 0;
    uint32_t                 HeightAltitude_ = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCATTERING
//------------------------------------------------------------------------------------------------------------------------

// Single-scattered sky radiance along a view ray (planet-centred, km; Direction unit), sun treated
// as a directional source of the given irradiance. Sun transmittance is taken from the LUT when
// provided, else integrated directly. Returns spectral radiance (arbitrary consistent units).
[[nodiscard]] SpectrumRgb SingleScattering(const AtmosphereParameters& P, SpatialVector PositionKm,
                                           SpatialVector Direction, SpatialVector SunDirection,
                                           const SpectrumRgb& SunIrradiance, const TransmittanceLut* Lut = nullptr,
                                           uint32_t Steps = 32) noexcept;

// Second-order isotropic multiple-scattering approximation in the spirit of Hillaire's factored
// form: the view-ray's scattering integral scaled by a directional-average factor gathered from
// TransmittanceToTop over the sphere at the ray midpoint. Bounded and non-negative; the SDK phase
// replaces it with the demo's AtmosphereMultiple formulation once that can be read.
[[nodiscard]] SpectrumRgb MultipleScatteringApprox(const AtmosphereParameters& P, SpatialVector PositionKm,
                                                   SpatialVector Direction, SpatialVector SunDirection,
                                                   const SpectrumRgb& SingleScattered,
                                                   const SpectrumRgb& SunIrradiance) noexcept;

} // namespace Frontier
