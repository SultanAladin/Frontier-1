/*==========================================================================================================================================
                                                       LENS FLARE MODEL
                                     ------------------------------------------------------------------
                 Ghost placement along the sun-centre axis (plan §8.1: LensFlare/FlareImage/
                      FlareVisibility). Header-only geometry; intensity/visibility supplied by caller.
==========================================================================================================================================*/

// 🧩 Lens flare: the classic aperture-ghost chain. Ghosts lie on the line through the screen centre
//    and the sun's screen position (NDC, centre = 0), at fixed fractional positions along the
//    centre-mirrored axis. Deterministic table; no textures involved at this layer.
// 📝 Radiance layer (2026-10-26): limb-darkened solar disc with a Gaussian-blurred analytic limb,
//    aperture-shaped ghosts with coating tints and rim fringing, diffraction starburst, anamorphic
//    streak, centre halo ring, two-lobe glow and veiling glare. Every term is linear in the sun's
//    disc-integrated irradiance at the lens and in the caller's disc visibility. Screen coordinates
//    are height-normalised (half the frame height = 1), matching the ghost table. The coefficients and
//    profiles are parametric approximations, not a measurement or energy-calibrated lens model.

#pragma once

#include "AtmosphereModel.h"   // SpectrumRgb

#include <array>
#include <cmath>
#include <cstdint>

namespace Frontier
{

struct FlareGhost
{
    double PositionX = 0.0;   // [NDC]
    double PositionY = 0.0;   // [NDC]
    double RadiusNdc = 0.0;   // [NDC] of frame height
    double Strength  = 0.0;   // [-] relative, already includes visibility
};

struct FlareGhostSpec
{
    double AxisFraction;   // [-] position = -SunNdc * AxisFraction (through centre)
    double RadiusNdc;
    double Strength;
};

inline constexpr std::array<FlareGhostSpec, 6> DefaultFlareGhosts{{
    {0.25, 0.06, 0.50},
    {0.55, 0.13, 0.30},
    {0.90, 0.04, 0.65},
    {1.25, 0.20, 0.18},
    {1.70, 0.08, 0.40},
    {2.30, 0.30, 0.10},
}};

// Places the spec ghosts for a sun at (SunX, SunY) NDC with the given visibility [0..1]
// (fraction of the sun disc unoccluded, from the caller's FlareVisibility samples).
[[nodiscard]] inline std::array<FlareGhost, 6> LayoutFlareGhosts(double SunX, double SunY, double Visibility) noexcept
{
    std::array<FlareGhost, 6> Out{};
    const double V = Visibility < 0.0 ? 0.0 : (Visibility > 1.0 ? 1.0 : Visibility);
    for (uint32_t I = 0; I < Out.size(); ++I)
    {
        const FlareGhostSpec& Spec = DefaultFlareGhosts[I];
        Out[I] = {-SunX * Spec.AxisFraction, -SunY * Spec.AxisFraction, Spec.RadiusNdc, Spec.Strength * V};
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SUN DISC
//------------------------------------------------------------------------------------------------------------------------

// 📝 Quadratic limb darkening I(μ)/I(1) = 1 − u₁(1−μ) − u₂(1−μ)² with μ = √(1−ρ²) across the disc
//    (broadband visible coefficients; the limb sits at 0.38 of disc centre). The limb itself is the
//    locally planar approximation to a Gaussian-blurred step edge: ½·erfc((θ−θ_disc)/(√2·σ)), so
//    valid for σ small relative to the disc radius; it is not a flux-preserving 2D PSF convolution.
//    Set σ from the pixel angular size. Unresolved discs require integrated pixel coverage.
struct SunDiscProfile
{
    double AngularRadiusRad = 0.0046524;   // [rad] 0.2666°, mean apparent solar semi-diameter
    double LimbLinear       = 0.38;        // [-] u₁
    double LimbQuadratic    = 0.24;        // [-] u₂
    double EdgeBlurRad      = 0.00030;     // [rad] σ of the point-spread Gaussian at the limb
};

// Disc solid angle Ω = 2π(1 − cos θ) [sr].
[[nodiscard]] inline double SunDiscSolidAngleSr(double AngularRadiusRad) noexcept
{
    return 2.0 * 3.14159265358979323846 * (1.0 - std::cos(AngularRadiusRad));
}

// Disc-mean radiance over disc-centre radiance for the quadratic limb law: 1 − u₁/3 − u₂/6.
[[nodiscard]] inline double SunDiscMeanLimbFactor(const SunDiscProfile& Profile) noexcept
{
    return 1.0 - Profile.LimbLinear / 3.0 - Profile.LimbQuadratic / 6.0;
}

// Sun disc radiance at angular separation AngleRad from disc centre [-], given the disc-centre
// radiance already tinted by atmospheric transmittance toward the sun.
[[nodiscard]] inline SpectrumRgb EvaluateSunDiscRadiance(double                 AngleRad,
                                                         const SunDiscProfile&  Profile,
                                                         const SpectrumRgb&     CentreRadiance) noexcept
{
    constexpr double RootTwo = 1.4142135623730951;
    const double     Rho     = std::sin(AngleRad) / std::sin(Profile.AngularRadiusRad);   // [-] disc-normalised radius
    const double     Mu      = Rho < 1.0 ? std::sqrt(1.0 - Rho * Rho) : 0.0;              // [-] μ = cos emission angle
    const double     Limb    = 1.0 - Profile.LimbLinear * (1.0 - Mu) - Profile.LimbQuadratic * (1.0 - Mu) * (1.0 - Mu);
    const double     Sigma   = Profile.EdgeBlurRad > 0.0 ? Profile.EdgeBlurRad : 1.0e-9;
    const double     Edge    = 0.5 * std::erfc((AngleRad - Profile.AngularRadiusRad) / (RootTwo * Sigma));
    const double     Scale   = Limb * Edge;
    return {CentreRadiance.R * Scale, CentreRadiance.G * Scale, CentreRadiance.B * Scale};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLARE OPTICS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One photographic lens, parameterised: aperture blade count shapes the ghosts and the starburst
//    (an even blade count pairs its diffraction spikes, an odd count doubles them), the anamorphic
//    streak runs along AnamorphicAngleRad (0 = horizontal), and the halo is the faint ring centred
//    on the sun's antipode through the frame centre. Strengths multiply the sun's disc-integrated
//    irradiance at the lens (DiscIrradiance below); 0 disables a term exactly.
struct FlareOpticsProfile
{
    uint32_t ApertureBlades        = 6;        // [-] aperture polygon and starburst construction
    double   ApertureRotationRad   = 0.0;      // [rad] polygon and spike orientation
    double   GhostStrength         = 1.0;      // [-] aperture-ghost chain scale
    double   StarburstStrength     = 1.0;      // [-] diffraction spike scale
    double   StarburstLengthNdc    = 0.55;     // [NDC] spike reach, half-frame-height units
    double   AnamorphicStrength    = 1.0;      // [-] horizontal streak scale
    double   AnamorphicAngleRad    = 0.0;      // [rad] streak direction, 0 = frame horizontal
    double   AnamorphicLengthNdc   = 1.35;     // [NDC] streak exponential reach
    double   AnamorphicThicknessNdc = 0.006;   // [NDC] streak Gaussian σ across its length
    double   StreakStrength        = 0.0;      // [-] independent warm streak; 0 preserves existing presets
    double   StreakAngleRad        = 0.5;      // [rad] separate rotation from the blue anamorphic layer
    double   StreakSpread          = 1.0;      // [-] demo spread, clamped to [0.25, 2]
    double   GlowStrength          = 1.0;      // [-] two-lobe point-spread glow about the sun
    double   HaloStrength          = 1.0;      // [-] antipode-centred ring through frame centre
    double   VeilingStrength       = 1.0;      // [-] frame-wide low-contrast wash
};

namespace LensFlareDetail
{

[[nodiscard]] inline double ClampVisibility(double Visibility) noexcept
{
    return Visibility < 0.0 ? 0.0 : (Visibility > 1.0 ? 1.0 : Visibility);
}

[[nodiscard]] inline SpectrumRgb ScaleRgb(const SpectrumRgb& Colour, double Scale) noexcept
{
    return {Colour.R * Scale, Colour.G * Scale, Colour.B * Scale};
}

[[nodiscard]] inline SpectrumRgb AddRgb(SpectrumRgb A, SpectrumRgb B) noexcept
{
    return {A.R + B.R, A.G + B.G, A.B + B.B};
}

// Aperture polygon boundary radius at angle Phi for a regular Blades-gon of circumradius Radius.
// 📝 std::remainder, not std::fmod: fmod keeps the sign of a negative angle and would leave half the
//    polygon outside its sector; remainder wraps into [-π/N, π/N] about the sector centre exactly.
[[nodiscard]] inline double ApertureEdgeRadius(double Phi, double Radius, uint32_t Blades, double RotationRad) noexcept
{
    constexpr double Pi    = 3.14159265358979323846;
    const double     Count = static_cast<double>(Blades < 3 ? 3 : Blades);
    const double     Local = std::remainder(Phi - RotationRad, 2.0 * Pi / Count);
    return Radius * std::cos(Pi / Count) / std::cos(Local);
}

[[nodiscard]] inline double SoftInside(double Rho, double Edge, double Softness) noexcept
{
    const double T = (Edge - Rho) / (Softness > 0.0 ? Softness : 1.0e-9);
    const double C = T < 0.0 ? 0.0 : (T > 1.0 ? 1.0 : T);
    return C * C * (3.0 - 2.0 * C);
}

} // namespace LensFlareDetail

// Coating tint and construction per DefaultFlareGhosts entry: RingFraction 0 is a filled aperture
// image (rim-weighted, as a defocused aperture photographs), otherwise a hollow ring at that
// fraction of the ghost radius over a faint fill. Rim fringing offsets the edge ±1.2% per channel.
struct FlareGhostAppearance
{
    SpectrumRgb Tint;          // [-] anti-reflection coating colour of the reflecting pair
    double      RingFraction;  // [-] 0 filled, else ring centre as a fraction of RadiusNdc
};

inline constexpr std::array<FlareGhostAppearance, 6> DefaultFlareGhostAppearances{{
    {{1.00, 0.74, 0.48}, 0.0},    // warm amber, filled
    {{0.46, 0.96, 0.62}, 0.82},   // green coating ring
    {{0.56, 0.66, 1.00}, 0.0},    // blue-violet, filled, small and strong
    {{1.00, 0.52, 0.58}, 0.0},    // magenta coating, large and faint
    {{0.86, 0.96, 0.52}, 0.78},   // yellow-green ring
    {{0.52, 0.90, 1.00}, 0.0},    // cyan, largest and faintest
}};

// Aperture-ghost chain radiance at height-normalised screen point (PixelX, PixelY) for a sun at
// (SunX, SunY). DiscIrradiance is the sun's disc-integrated irradiance at the lens, spectrum-shaped
// (top-of-atmosphere irradiance × transmittance toward the sun). OnlyGhost >= 0 isolates one ghost
// of the chain, for measurement; -1 evaluates the whole chain.
[[nodiscard]] inline SpectrumRgb EvaluateFlareGhostRadiance(double                    PixelX,
                                                            double                    PixelY,
                                                            double                    SunX,
                                                            double                    SunY,
                                                            const SpectrumRgb&        DiscIrradiance,
                                                            double                    Visibility,
                                                            const FlareOpticsProfile& Profile,
                                                            int                       OnlyGhost = -1) noexcept
{
    using namespace LensFlareDetail;
    const double V = ClampVisibility(Visibility) * Profile.GhostStrength;
    if (V <= 0.0)
    {
        return {};
    }
    SpectrumRgb Out{};
    const std::array<FlareGhost, 6> Ghosts = LayoutFlareGhosts(SunX, SunY, 1.0);
    for (uint32_t I = 0; I < Ghosts.size(); ++I)
    {
        if (OnlyGhost >= 0 && static_cast<int>(I) != OnlyGhost)
        {
            continue;
        }
        const FlareGhost&           Ghost      = Ghosts[I];
        const FlareGhostAppearance& Appearance = DefaultFlareGhostAppearances[I];
        const double Dx  = PixelX - Ghost.PositionX;
        const double Dy  = PixelY - Ghost.PositionY;
        const double Rho = std::sqrt(Dx * Dx + Dy * Dy);
        if (Rho > Ghost.RadiusNdc * 1.15)
        {
            continue;
        }
        const double Phi      = std::atan2(Dy, Dx);
        const double Softness = 0.10 * Ghost.RadiusNdc;
        // 📝 Per-channel edge radius gives the rim its spectral fringe; the fill is rim-weighted.
        const double ChannelScale[3] = {1.012, 1.0, 0.988};
        const double TintChannel[3]  = {Appearance.Tint.R, Appearance.Tint.G, Appearance.Tint.B};
        const double FluxChannel[3]  = {DiscIrradiance.R, DiscIrradiance.G, DiscIrradiance.B};
        double       Contribution[3] = {0.0, 0.0, 0.0};
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Edge = ApertureEdgeRadius(Phi, Ghost.RadiusNdc * ChannelScale[Channel],
                                                   Profile.ApertureBlades, Profile.ApertureRotationRad);
            const double Inside = SoftInside(Rho, Edge, Softness);
            double       Shape  = 0.0;
            if (Appearance.RingFraction > 0.0)
            {
                const double RingRadius = Appearance.RingFraction * Edge;
                const double RingWidth  = 0.10 * Ghost.RadiusNdc;
                const double RingDelta  = (Rho - RingRadius) / RingWidth;
                Shape = std::exp(-RingDelta * RingDelta) + 0.14 * Inside;
            }
            else
            {
                const double Rim = Rho / (Ghost.RadiusNdc > 0.0 ? Ghost.RadiusNdc : 1.0);
                Shape = Inside * (0.55 + 0.45 * Rim * Rim);
            }
            Contribution[Channel] = 0.16 * Ghost.Strength * TintChannel[Channel] * FluxChannel[Channel] * Shape;
        }
        Out = AddRgb(Out, {Contribution[0], Contribution[1], Contribution[2]});
    }
    return ScaleRgb(Out, V);
}

// Diffraction starburst about the sun: narrow Gaussian spikes along the aperture blade normals with
// a photographic radial falloff. Even blade counts pair opposite spikes (N spikes), odd counts do
// not (2N spikes) — the standard aperture-diffraction result.
[[nodiscard]] inline SpectrumRgb EvaluateDiffractionStarburst(double                    PixelX,
                                                              double                    PixelY,
                                                              double                    SunX,
                                                              double                    SunY,
                                                              const SpectrumRgb&        DiscIrradiance,
                                                              double                    Visibility,
                                                              const FlareOpticsProfile& Profile) noexcept
{
    using namespace LensFlareDetail;
    constexpr double Pi = 3.14159265358979323846;
    const double     V  = ClampVisibility(Visibility) * Profile.StarburstStrength;
    if (V <= 0.0 || Profile.StarburstLengthNdc <= 0.0)
    {
        return {};
    }
    const double   Dx  = PixelX - SunX;
    const double   Dy  = PixelY - SunY;
    const double   Rho = std::sqrt(Dx * Dx + Dy * Dy);
    const uint32_t Spikes = (Profile.ApertureBlades % 2u == 0u) ? Profile.ApertureBlades : 2u * Profile.ApertureBlades;
    const double   Phi = std::atan2(Dy, Dx) - Profile.ApertureRotationRad;
    // 📝 Per-channel angular width spreads each spike into a faint spectrum at its edges.
    const double FluxChannel[3] = {DiscIrradiance.R, DiscIrradiance.G, DiscIrradiance.B};
    double       Contribution[3] = {0.0, 0.0, 0.0};
    const double Radial = std::pow(1.0 / (1.0 + Rho / (0.012 + 0.05 * Profile.StarburstLengthNdc)), 1.7) *
                          std::exp(-Rho / Profile.StarburstLengthNdc);
    for (uint32_t Channel = 0; Channel < 3; ++Channel)
    {
        const double SigmaPhi = 0.020 * (Channel == 0 ? 1.10 : (Channel == 2 ? 0.92 : 1.0));   // [rad]
        double       Angular  = 0.0;
        for (uint32_t Spike = 0; Spike < Spikes; ++Spike)
        {
            // 📝 remainder wraps the angular difference into [-π, π]; fmod would strand the spikes
            //    across the ±π seam at zero (sign-preserving), silently dropping them.
            double Delta = std::remainder(Phi - 2.0 * Pi * static_cast<double>(Spike) / static_cast<double>(Spikes),
                                          2.0 * Pi);
            Delta /= SigmaPhi;
            Angular += std::exp(-Delta * Delta);
        }
        Contribution[Channel] = FluxChannel[Channel] * Angular * Radial;
    }
    return ScaleRgb({Contribution[0], Contribution[1], Contribution[2]}, 0.85 * V);
}

// Anamorphic streak: the elongated horizontal flare of cylindrical anamorphic optics — a thin
// Gaussian across the streak axis, exponential along it, with the characteristic blue cast and a
// brighter narrow core riding the same axis.
[[nodiscard]] inline SpectrumRgb EvaluateAnamorphicStreak(double                    PixelX,
                                                          double                    PixelY,
                                                          double                    SunX,
                                                          double                    SunY,
                                                          const SpectrumRgb&        DiscIrradiance,
                                                          double                    Visibility,
                                                          const FlareOpticsProfile& Profile) noexcept
{
    using namespace LensFlareDetail;
    const double V = ClampVisibility(Visibility) * Profile.AnamorphicStrength;
    if (V <= 0.0 || Profile.AnamorphicLengthNdc <= 0.0 || Profile.AnamorphicThicknessNdc <= 0.0)
    {
        return {};
    }
    const double CosA  = std::cos(Profile.AnamorphicAngleRad);
    const double SinA  = std::sin(Profile.AnamorphicAngleRad);
    const double Dx    = PixelX - SunX;
    const double Dy    = PixelY - SunY;
    const double Along = Dx * CosA + Dy * SinA;    // [NDC] coordinate along the streak
    const double Cross = -Dx * SinA + Dy * CosA;   // [NDC] coordinate across the streak
    const double Wide  = std::exp(-(Cross / Profile.AnamorphicThicknessNdc) * (Cross / Profile.AnamorphicThicknessNdc)) *
                         std::exp(-std::abs(Along) / Profile.AnamorphicLengthNdc);
    const double Core  = std::exp(-(Cross / (0.35 * Profile.AnamorphicThicknessNdc)) *
                                  (Cross / (0.35 * Profile.AnamorphicThicknessNdc))) *
                         std::exp(-std::abs(Along) / (0.42 * Profile.AnamorphicLengthNdc));
    const double Shape = Wide + 0.85 * Core;
    // 📝 The blue cast is the anamorphic coating signature; the sun's own spectrum still shapes it,
    //    so a sunset streak photographs white-blue rather than pure blue.
    return {DiscIrradiance.R * 0.34 * Shape * 0.55 * V, DiscIrradiance.G * 0.54 * Shape * 0.55 * V,
            DiscIrradiance.B * 1.00 * Shape * 0.55 * V};
}

// Warm rotatable streak from VehicleHMI LensFlare.wgsl / NativeLensFlareKernel.js.
// 📝 Demo coordinates use full image height; this API uses half-height, hence the 0.5 conversion.
//    Irradiance multiplication replaces the demo's artistic brightness clamp, preserving exposure linearity.
[[nodiscard]] inline SpectrumRgb EvaluateWarmStreak(double                    PixelX,
                                                   double                    PixelY,
                                                   double                    SunX,
                                                   double                    SunY,
                                                   const SpectrumRgb&        DiscIrradiance,
                                                   double                    Visibility,
                                                   const FlareOpticsProfile& Profile) noexcept
{
    const double Gain = LensFlareDetail::ClampVisibility(Visibility) * Profile.StreakStrength;
    if (Gain <= 0.0) return {};
    const double Spread = std::fmax(0.25, std::fmin(2.0, Profile.StreakSpread));
    const double Cosine = std::cos(Profile.StreakAngleRad);
    const double Sine = std::sin(Profile.StreakAngleRad);
    const double OffsetX = 0.5 * (PixelX - SunX);
    const double OffsetY = 0.5 * (PixelY - SunY);
    const double Along = OffsetX * Cosine + OffsetY * Sine;
    const double Across = -OffsetX * Sine + OffsetY * Cosine;
    const double Shape = 0.65 * Gain * std::exp(-(160.0 * std::abs(Across) + 3.0 * std::abs(Along)) / Spread);
    return {DiscIrradiance.R * Shape, DiscIrradiance.G * Shape * 0.8, DiscIrradiance.B * Shape * 0.55};
}

// Full lens flare at one screen point: ghost chain + starburst + anamorphic streak + two-lobe glow
// + antipode halo ring + veiling glare. Add to the scene radiance before tonemapping; the sun disc
// itself is EvaluateSunDiscRadiance, evaluated on view directions rather than screen points.
[[nodiscard]] inline SpectrumRgb EvaluateLensFlareRadiance(double                    PixelX,
                                                           double                    PixelY,
                                                           double                    SunX,
                                                           double                    SunY,
                                                           const SpectrumRgb&        DiscIrradiance,
                                                           double                    Visibility,
                                                           const FlareOpticsProfile& Profile) noexcept
{
    using namespace LensFlareDetail;
    const double V = ClampVisibility(Visibility);
    if (V <= 0.0)
    {
        return {};
    }
    SpectrumRgb Out = EvaluateFlareGhostRadiance(PixelX, PixelY, SunX, SunY, DiscIrradiance, V, Profile);
    Out = AddRgb(Out, EvaluateDiffractionStarburst(PixelX, PixelY, SunX, SunY, DiscIrradiance, V, Profile));
    Out = AddRgb(Out, EvaluateAnamorphicStreak(PixelX, PixelY, SunX, SunY, DiscIrradiance, V, Profile));
    Out = AddRgb(Out, EvaluateWarmStreak(PixelX, PixelY, SunX, SunY, DiscIrradiance, V, Profile));

    const double Dx  = PixelX - SunX;
    const double Dy  = PixelY - SunY;
    const double Rho = std::sqrt(Dx * Dx + Dy * Dy);
    // Two-lobe glow: tight point-spread core plus the broad scatter lobe of real glass.
    const double Glow = Profile.GlowStrength *
                        (0.50 * std::exp(-(Rho / 0.035) * (Rho / 0.035)) + 0.025 * std::exp(-(Rho / 0.30) * (Rho / 0.30)));
    Out = AddRgb(Out, ScaleRgb(DiscIrradiance, Glow * V));
    // Veiling glare: a near-uniform wash, slightly stronger toward the sun's side of the frame.
    // 📝 This scale is artistic and uncalibrated; stronger values can erase sky contrast wholesale.
    const double Veil = Profile.VeilingStrength * (0.004 + 0.008 * std::exp(-(Rho / 1.2) * (Rho / 1.2)));
    Out = AddRgb(Out, ScaleRgb(DiscIrradiance, Veil * V));
    // Halo: ring centred on the sun's antipode (−SunX, −SunY) with radius |Sun|, passing through the
    // frame centre; per-channel radius offsets give its spectral edge.
    const double SunDistance = std::sqrt(SunX * SunX + SunY * SunY);
    if (Profile.HaloStrength > 0.0 && SunDistance > 1.0e-6)
    {
        const double Hx = PixelX + SunX;
        const double Hy = PixelY + SunY;
        const double HRho = std::sqrt(Hx * Hx + Hy * Hy);
        const double FluxChannel[3]  = {DiscIrradiance.R, DiscIrradiance.G, DiscIrradiance.B};
        const double RadiusOffset[3] = {0.004, 0.0, -0.004};
        double       Halo[3]         = {0.0, 0.0, 0.0};
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Delta = (HRho - SunDistance - RadiusOffset[Channel]) / 0.030;
            Halo[Channel] = FluxChannel[Channel] * std::exp(-Delta * Delta);
        }
        Out = AddRgb(Out, ScaleRgb({Halo[0], Halo[1], Halo[2]}, 0.035 * Profile.HaloStrength * V));
    }
    return Out;
}

} // namespace Frontier
