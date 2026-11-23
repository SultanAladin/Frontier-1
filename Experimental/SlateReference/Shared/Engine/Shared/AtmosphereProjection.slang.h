//============================================================================================================================================
//                                                       ATMOSPHEREPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 The scattering medium, the phase functions and the three surface parameterisations — one source, both toolchains.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 🔴 A bake and the lookup that reads it are the same mapping written twice, and that is the defect this file
//    exists to make impossible. A transmittance surface baked through one arrangement of its axes and sampled
//    through another is not wrong by a visible amount — it is wrong by half a texel, which reaches the artist as
//    a horizon that sits slightly askew at one altitude and nowhere else, and which no reader attributes to a
//    parameterisation. `28` §2's three surfaces are built on the host and sampled on the device, so the mapping
//    crosses the toolchain seam in both directions and is declared here rather than in either.
//
// ⚠️ Lengths are **metres** throughout, and coefficients reciprocal metres, so every exponent is dimensionless
//    without a conversion. The donor formulation these mappings come from marches in kilometres and carries a
//    factor of a thousand at each exponentiation; that factor is absent here by construction, and it is the first
//    thing to look for if an atmosphere ported from that formulation comes out either opaque or absent.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE MEDIUM PROFILE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The medium as the device reads it — extents, profiles and the coefficients already resolved into the working space.
/// note  🔴 The coefficients arrive **resolved**, never derived here. `28` §3 derives Rayleigh from the refractive
///        index and fits ozone to the Chappuis band, both against `02` §5's spectral projection over four hundred
///        and seventy nanometres of wavelength domain — which is a host computation performed once per medium
///        amendment, not a per-texel one. Deriving it on the device would be deriving it a million times.
/// note  📝 Held at 64 bits like every other quantity in `Shared/`. The three resident surfaces are half precision
///        and that is where the width drops, at the store, which is `28` §1's Tier D declaration and not this
///        profile's business.
/// tag   shared, nonallocating, nonthrowing
struct MediumProfile
{
    Real64  PlanetRadius          ;   // [m]   - to the surface
    Real64  AtmosphereThickness   ;   // [m]   - above it
    Real64  RayleighScaleHeight   ;   // [m]   - exponential
    Real64  MieScaleHeight        ;   // [m]   - exponential
    Real64  OzoneCentreAltitude   ;   // [m]   - the tent's centre
    Real64  OzoneHalfWidth        ;   // [m]   - the tent falls to nothing at this departure
    Real64  MieAsymmetry          ;   // [-]   - forward-biased; zero is isotropic
    Real64  MieScattering         ;   // [1/m] - wavelength-neutral, at sea level
    Real64  MieExtinction         ;   // [1/m] - scattering plus absorption
    Real64  RayleighScatteringRed ;   // [1/m] - working space, at sea level
    Real64  RayleighScatteringGreen;  // [1/m]
    Real64  RayleighScatteringBlue;   // [1/m]
    Real64  OzoneAbsorptionRed    ;   // [1/m] - working space, at the tent's centre
    Real64  OzoneAbsorptionGreen  ;   // [1/m]
    Real64  OzoneAbsorptionBlue   ;   // [1/m]
    Real64  SunDirectionX         ;   // [-]   - toward the sun, unit, local frame
    Real64  SunDirectionY         ;   // [-]   - the local zenith is the second axis
    Real64  SunDirectionZ         ;   // [-]
    Real64  CameraAltitude        ;   // [m]   - above the surface, where ③ is baked from
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The distance from a radius along a zenith cosine to a sphere of a declared radius.
/// in    Radius        [m]  from the planet centre
/// in    ZenithCosine  [-]  of the ray against the local up
/// in    SphereRadius  [m]  the sphere reached for
/// out   Distance      [m]  negative where the ray never reaches it
/// note  📐 The discriminant is written as r²(μ²−1) + R² rather than as the expanded quadratic, because the
///        expanded form subtracts two nearly equal magnitudes at grazing angles and loses every significant digit
///        exactly where the horizon is — which is the one place in the whole surface where the answer matters.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectSphereDistance(Real64 Radius, Real64 ZenithCosine, Real64 SphereRadius)
{
    const Real64 Discriminant = Radius * Radius * (ZenithCosine * ZenithCosine - 1.0) + SphereRadius * SphereRadius;

    if (Discriminant < 0.0)
    {
        return -1.0;
    }

    const Real64 Root = SquareRoot(Discriminant);

    // 📝 The nearer intersection where the ray descends, the further where it climbs. A ray that climbs has no
    //    nearer intersection with the planet at all, which is what the negative branch below tests for.
    const Real64 Nearer  = -Radius * ZenithCosine - Root;
    const Real64 Further = -Radius * ZenithCosine + Root;

    if (Nearer >= 0.0)
    {
        return Nearer;
    }

    return Further >= 0.0 ? Further : -1.0;
}

/// 🧩 The radius reached after advancing a declared distance along a ray.
/// in    Distance  [m]  along the ray
/// out   Radius    [m]  from the planet centre at the arrival
/// note  📐 From the cosine rule on the triangle centre–origin–arrival: r'² = r² + d² + 2rμd. Advancing a position
///        and taking its length instead would carry three coordinates through the march to recover one number that
///        depends on none of them separately.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 AdvanceRadius(Real64 Radius, Real64 ZenithCosine, Real64 Distance)
{
    const Real64 Squared = Radius * Radius + Distance * Distance + 2.0 * Radius * ZenithCosine * Distance;

    return SquareRoot(Squared > 0.0 ? Squared : 0.0);
}

/// 🧩 Whether a ray from a declared radius meets the planet before the atmosphere boundary.
/// out   GroundReached  [-]  a ray that climbs never reaches it, whatever its discriminant says
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED bool ClassifyGroundReach(Real64 Radius, Real64 ZenithCosine, Real64 PlanetRadius)
{
    if (ZenithCosine >= 0.0)
    {
        return false;
    }

    return Radius * Radius * (ZenithCosine * ZenithCosine - 1.0) + PlanetRadius * PlanetRadius >= 0.0;
}

/// 🧩 The distance a ray runs before it leaves the medium — the ground where it descends, the boundary where it climbs.
/// out   Distance  [m]  never positive where the ray is outside the atmosphere altogether
/// note  📝 Declared once here because ② and ③ both march it and `28` §2 orders them one after the other. Two
///        marches that disagree about where a ray ends disagree about the horizon, and ② would carry that
///        disagreement into ③ as an ambient term rather than as a visible seam.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectMarchDistance(Real64 Radius, Real64 ZenithCosine,
                                         Real64 PlanetRadius, Real64 AtmosphereRadius)
{
    if (ClassifyGroundReach(Radius, ZenithCosine, PlanetRadius))
    {
        return ProjectSphereDistance(Radius, ZenithCosine, PlanetRadius);
    }

    return ProjectSphereDistance(Radius, ZenithCosine, AtmosphereRadius);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE THREE PROFILES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The ozone tent's density at an altitude — unity at its centre, nothing beyond the declared half width.
/// note  📐 A tent rather than an exponential because ozone is a layer with a maximum aloft, not a gas that
///        settles. That is exactly why it colours twilight and why the other two components do not: at a low sun
///        the path crosses the tent nearly horizontally, and the Chappuis absorption it accumulates there is what
///        leaves the zenith blue instead of grey.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectOzoneDensity(MediumProfile Medium, Real64 Altitude)
{
    if (Medium.OzoneHalfWidth <= 0.0)
    {
        return 0.0;
    }

    const Real64 Departure = Magnitude(Altitude - Medium.OzoneCentreAltitude);

    if (Departure >= Medium.OzoneHalfWidth)
    {
        return 0.0;
    }

    return 1.0 - Departure / Medium.OzoneHalfWidth;
}

/// 🧩 The medium's total extinction at an altitude, per component.
/// out   Red/Green/Blue  [1/m]  Rayleigh scattering, Mie extinction and ozone absorption together
/// note  🔴 Mie contributes its **extinction** and not its scattering. The difference between the two is the
///        fraction Mie absorbs, and dropping it makes a hazy atmosphere brighten rather than dim with depth —
///        which reads as fog that glows.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ResolveMediumExtinction(MediumProfile Medium, Real64 Altitude,
                                          SLATE_OUT(Real64) Red,
                                          SLATE_OUT(Real64) Green,
                                          SLATE_OUT(Real64) Blue)
{
    const Real64 RayleighDensity = Exponential(-Altitude / Medium.RayleighScaleHeight);
    const Real64 MieDensity      = Exponential(-Altitude / Medium.MieScaleHeight);
    const Real64 OzoneDensity    = ProjectOzoneDensity(Medium, Altitude);
    const Real64 MieTerm         = Medium.MieExtinction * MieDensity;

    Red   = Medium.RayleighScatteringRed   * RayleighDensity + Medium.OzoneAbsorptionRed   * OzoneDensity + MieTerm;
    Green = Medium.RayleighScatteringGreen * RayleighDensity + Medium.OzoneAbsorptionGreen * OzoneDensity + MieTerm;
    Blue  = Medium.RayleighScatteringBlue  * RayleighDensity + Medium.OzoneAbsorptionBlue  * OzoneDensity + MieTerm;
}

/// 🧩 The medium's scattering at an altitude, Rayleigh per component and Mie as one neutral magnitude.
/// out   Mie  [1/m]  wavelength-neutral by declaration, which is why it is one magnitude and not three
/// note  🔴 Ozone contributes nothing here. `28` §3 and §7: ozone absorbs **without scattering**, and a component
///        that appeared in this routine would be one that brightens the sky it is meant to tint.
/// note  📝 The two are handed back apart rather than summed, because ③ weights them by different phase functions
///        and a caller given only their sum cannot recover either.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ResolveMediumScattering(MediumProfile Medium, Real64 Altitude,
                                          SLATE_OUT(Real64) RayleighRed,
                                          SLATE_OUT(Real64) RayleighGreen,
                                          SLATE_OUT(Real64) RayleighBlue,
                                          SLATE_OUT(Real64) Mie)
{
    const Real64 RayleighDensity = Exponential(-Altitude / Medium.RayleighScaleHeight);
    const Real64 MieDensity      = Exponential(-Altitude / Medium.MieScaleHeight);

    RayleighRed   = Medium.RayleighScatteringRed   * RayleighDensity;
    RayleighGreen = Medium.RayleighScatteringGreen * RayleighDensity;
    RayleighBlue  = Medium.RayleighScatteringBlue  * RayleighDensity;
    Mie           = Medium.MieScattering * MieDensity;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE PHASE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Rayleigh's phase, 3(1+cos²θ)/16π.
/// note  📐 Near-isotropic with a shallow pair of lobes forward and back, which is why the daytime sky is bright in
///        every direction rather than only around the sun.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectRayleighPhase(Real64 ScatterCosine)
{
    return 3.0 * (1.0 + ScatterCosine * ScatterCosine) / (16.0 * Pi);
}

/// 🧩 Cornette–Shanks, forward-biased by the declared asymmetry.
/// in    Asymmetry  [-]  the open interval about the origin; unity collapses the lobe onto no solid extent at all
/// note  📐 Cornette–Shanks rather than Henyey–Greenstein, which is the donor formulation's own choice and is the
///        better fit to Mie's forward lobe at the asymmetries an atmosphere uses. The two differ by the (1+cos²θ)
///        factor and a normalisation; the visible consequence is the softness of the halo immediately around the
///        sun, where Henyey–Greenstein is the blunter of the two.
/// note  🔴 The denominator's base is held above zero before the three-halves power. At an asymmetry approaching
///        unity and a scatter cosine approaching it too, the base is a difference of two nearly equal magnitudes
///        and reaches zero before the physics does; the power of a zero base is not a large magnitude, it is a
///        non-finite one, and it propagates through the whole march rather than through one step.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectMiePhase(Real64 ScatterCosine, Real64 Asymmetry)
{
    const Real64 Squared     = Asymmetry * Asymmetry;
    const Real64 Numerator   = 3.0 * (1.0 - Squared) * (1.0 + ScatterCosine * ScatterCosine);
    const Real64 Base        = 1.0 + Squared - 2.0 * Asymmetry * ScatterCosine;
    const Real64 Held        = Base > 1.0e-4 ? Base : 1.0e-4;
    const Real64 Denominator = 8.0 * Pi * (2.0 + Squared) * Held * SquareRoot(Held);

    return Numerator / Denominator;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE STEP INTEGRAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One march step's ∫₀ᵈ S·e^{−σₑs} ds, which is S(1 − e^{−σₑd})/σₑ.
/// in    ScatteringMagnitude  [1/m]  the in-scatter the step contributes, before attenuation
/// in    ExtinctionMagnitude  [1/m]  the extinction attenuating it across the step
/// in    StepTransmittance    [-]    e^{−σₑd}, already formed by the caller for its own throughput
/// in    StepSize             [m]    the step's length
/// note  🔴 The vanishing-extinction limit S·d is spelled out rather than guarded by a floor under the divisor. An
///        extinction coefficient at the top of the atmosphere is itself of the order any such floor would be, so a
///        floor does not protect the division — it replaces the answer with a different one wherever the air is
///        thin, which is most of the ray.
/// note  📐 Analytic across the step rather than S·T·d evaluated at its midpoint. The two agree only where the
///        step is optically thin, and the steps that are not are exactly the ones near the ground; the midpoint
///        form's disagreement there is the banding a sunset shows across a smooth gradient.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 IntegrateStepInScatter(Real64 ScatteringMagnitude, Real64 ExtinctionMagnitude,
                                           Real64 StepTransmittance,   Real64 StepSize)
{
    if (ExtinctionMagnitude <= 0.0)
    {
        return ScatteringMagnitude * StepSize;
    }

    return ScatteringMagnitude * (1.0 - StepTransmittance) / ExtinctionMagnitude;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ① THE TRANSMITTANCE PARAMETERISATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where a declared radius and zenith cosine sit on ①.
/// out   CoordinateX   [-]  the zenith cosine, spanning the wider axis linearly
/// out   CoordinateY  [-]  the altitude, over the declared thickness
/// note  📐 The zenith cosine takes the wider of the two axes precisely because the transmittance gradient across
///        the horizon is the steep one, and the altitude the narrower because its gradient is not. Two hundred and
///        fifty-six against sixty-four is that observation as a number — `Foundation/` holds both.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectTransmittanceCoordinate(MediumProfile Medium, Real64 Radius, Real64 ZenithCosine,
                                                 SLATE_OUT(Real64) CoordinateX,
                                                 SLATE_OUT(Real64) CoordinateY)
{
    const Real64 Altitude = Radius - Medium.PlanetRadius;

    CoordinateX  = BoundedMagnitude(0.5 * (ZenithCosine + 1.0), 0.0, 1.0);
    CoordinateY = Medium.AtmosphereThickness > 0.0
                     ? BoundedMagnitude(Altitude / Medium.AtmosphereThickness, 0.0, 1.0)
                     : 0.0;
}

/// 🧩 The inverse — what one texel centre of ① stands for.
/// note  📝 Declared beside the forward mapping so that an amendment to either is made with the other in view.
///        They are a pair and neither is correct alone.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectTransmittanceParameter(MediumProfile Medium,
                                                Real64 CoordinateX, Real64 CoordinateY,
                                                SLATE_OUT(Real64) Radius,
                                                SLATE_OUT(Real64) ZenithCosine)
{
    Radius       = Medium.PlanetRadius + CoordinateY * Medium.AtmosphereThickness;
    ZenithCosine = 2.0 * CoordinateX - 1.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        ② THE MULTIPLE-SCATTERING PARAMETERISATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where a declared radius and sun zenith cosine sit on ②.
/// note  🔴 The sun's zenith cosine, never the view's. ② is the isotropic term and has forgotten which way the
///        light came from — that is what makes it a thirty-two square surface rather than a directional one, and
///        it is why ② survives every rotation the sun moves through without a rebuild.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectMultiScatterCoordinate(MediumProfile Medium, Real64 Radius, Real64 SunZenithCosine,
                                                SLATE_OUT(Real64) CoordinateX,
                                                SLATE_OUT(Real64) CoordinateY)
{
    ProjectTransmittanceCoordinate(Medium, Radius, SunZenithCosine, CoordinateX, CoordinateY);
}

/// 🧩 The inverse — what one texel centre of ② stands for.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectMultiScatterParameter(MediumProfile Medium,
                                               Real64 CoordinateX, Real64 CoordinateY,
                                               SLATE_OUT(Real64) Radius,
                                               SLATE_OUT(Real64) SunZenithCosine)
{
    ProjectTransmittanceParameter(Medium, CoordinateX, CoordinateY, Radius, SunZenithCosine);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           ③ THE SKY-VIEW PARAMETERISATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The view direction one texel centre of ③ stands for.
/// out   DirectionX/Y/Z  [-]  unit length; the local zenith is the second axis
/// note  📐 The zenith axis is quadratic **about the horizon** rather than linear in either the angle or its
///        cosine. The horizon is where the radiance gradient is steep and where a linear parameterisation spends a
///        handful of its hundred and eight texels, which reaches the artist as a banded sunset over a perfectly
///        smooth zenith. Below the halfway coordinate the ray descends and above it climbs, with the horizon at
///        the halfway point where both branches meet at zero departure.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectSkyViewDirection(Real64 CoordinateX, Real64 CoordinateY,
                                          SLATE_OUT(Real64) DirectionX,
                                          SLATE_OUT(Real64) DirectionY,
                                          SLATE_OUT(Real64) DirectionZ)
{
    const Real64 Azimuth = (CoordinateX * 2.0 - 1.0) * Pi;

    Real64 Zenith = 0.0;

    if (CoordinateY < 0.5)
    {
        const Real64 Departure = 1.0 - 2.0 * CoordinateY;   // [-] - nothing at the horizon, unity at the nadir

        Zenith = Pi * 0.5 + Departure * Departure * Pi * 0.5;
    }
    else
    {
        const Real64 Departure = 2.0 * CoordinateY - 1.0;   // [-] - nothing at the horizon, unity at the zenith

        Zenith = Pi * 0.5 - Departure * Departure * Pi * 0.5;
    }

    const Real64 ZenithSine = Sine(Zenith);

    DirectionX = ZenithSine * Cosine(Azimuth);
    DirectionY = Cosine(Zenith);
    DirectionZ = ZenithSine * Sine(Azimuth);
}

/// 🧩 Where a declared view direction sits on ③.
/// in    DirectionX      [-]  unit length; the local zenith is the second axis
/// in    DirectionY      [-]
/// in    DirectionZ      [-]
/// note  📝 The inverse of the quadratic bias above, and it is a square root rather than a second fit of it. A fit
///        would agree with the bake to whatever tolerance somebody measured; the square root agrees with it
///        identically, which is the difference between one mapping and two that resemble each other.
/// note  🔴 The azimuth is wrapped **here**, because only this routine knows the azimuth is periodic.
///        `ResidentSurface::Sample` clamps both of its axes and is right to: the zenith axis genuinely ends at the
///        zenith, and a wrapped sample there reads the horizon, which appears as a bright ring directly overhead.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectSkyViewCoordinate(Real64 DirectionX, Real64 DirectionY, Real64 DirectionZ,
                                           SLATE_OUT(Real64) CoordinateX,
                                           SLATE_OUT(Real64) CoordinateY)
{
    const Real64 Azimuth = ArcTangentQuadrant(DirectionZ, DirectionX);
    const Real64 Zenith  = ArcCosine(BoundedMagnitude(DirectionY, -1.0, 1.0));

    CoordinateX = BoundedMagnitude(Azimuth / (2.0 * Pi) + 0.5, 0.0, 1.0);

    if (Zenith > Pi * 0.5)
    {
        const Real64 Departure = SquareRoot(BoundedMagnitude((Zenith - Pi * 0.5) / (Pi * 0.5), 0.0, 1.0));

        CoordinateY = 0.5 * (1.0 - Departure);
    }
    else
    {
        const Real64 Departure = SquareRoot(BoundedMagnitude((Pi * 0.5 - Zenith) / (Pi * 0.5), 0.0, 1.0));

        CoordinateY = 0.5 * (1.0 + Departure);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE LOCAL FRAME
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The local up at a marched sample, as a direction from the planet centre.
/// in    Position  [m]  how far along the ray the sample sits
/// note  📐 The sun's zenith cosine at a sample is taken against the **sample's** up and never against the ray
///        origin's. A ray that travels a hundred kilometres around a planet of six thousand has turned measurably
///        under itself, and the error is largest at exactly the grazing angles where twilight is decided.
/// note  📝 The ray origin sits on the second axis by construction — every march in `28` starts at (0, r, 0) — so
///        the arrival is the origin displaced along the view direction, and dividing by the arrival's radius is
///        what normalises it without a second square root.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectLocalUp(Real64 StartRadius, Real64 SampleRadius, Real64 Position,
                                 Real64 ViewX, Real64 ViewY, Real64 ViewZ,
                                 SLATE_OUT(Real64) UpX,
                                 SLATE_OUT(Real64) UpY,
                                 SLATE_OUT(Real64) UpZ)
{
    UpX = (ViewX * Position) / SampleRadius;
    UpY = (StartRadius + ViewY * Position) / SampleRadius;
    UpZ = (ViewZ * Position) / SampleRadius;
}

}   // namespace Slate
