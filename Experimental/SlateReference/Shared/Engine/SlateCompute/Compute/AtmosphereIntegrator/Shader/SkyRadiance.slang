//============================================================================================================================================
//                                                            SKYRADIANCE.SLANG
//============================================================================================================================================
// 🧩 The sky along one view direction, with the analytic solar disc — what `18` §5.1's unoccupied class reads.

#pragma once

#include "SlateCompute/Compute/AtmosphereIntegrator/Shader/AtmosphereUniform.slang"

// 📝 🔴 This is the donor formulation's per-slot sky dome, and it is **not** a recording of its own. `08` §3
//    schedules ① `28`, ② `16`, then ④ `18`, and `18` §5.1 declares that the unoccupied class is dispatched like
//    any other and writes `RadianceSurface` at every pixel that resolved to no surface. A fullscreen dome recorded
//    before `16` would write the whole extent and then have `18` write most of it again, and `08` §2's producer
//    rule refuses a target with two producers. So the dome becomes what it always was underneath: one lookup,
//    included by the dispatch that already owns those pixels. There is likewise no fullscreen vertex entry point,
//    because there is no fullscreen recording to feed.
//
// ⚠️ The direction arrives in the **atmosphere-local frame** — zenith on the second axis, `28` §2's convention —
//    and this file does not convert into it. No world up-axis is declared anywhere in the sequence, so a remap
//    written here would be this file inventing a convention rather than reading one; `46` owns the camera's frame
//    and the caller reconciles it. The donor formulation remaps (x, y, z) → (x, z, y) at its own call site for
//    exactly this reason and documents that its world is Z-up.
//
// ⚠️ Nothing here tone maps and nothing here applies exposure. `08` §3.1's display-referred line falls after ⑧
//    `66`, and this radiance is read at ④. The donor formulation carried an ACES-ish curve at this site that both
//    double-mapped against its own resolve and clamped to unity — destroying every value above one before it
//    reached a float target, which is the headroom the solar disc lives in. The curve is absent by construction.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📐 The disc's limb softens over the inner half of its angular radius. A hard edge at the exact radius aliases
//    against any sampling `64` performs, and the softening is the physical limb rather than an antialiasing
//    concession — the solar disc genuinely dims toward its edge.
static const Real64 SolarLimbFraction = 0.5;   // [-] - of the angular radius over which the disc softens

// 📝 The disc is sampled against ① at a radius just above the surface rather than at the camera's own. The
//    difference is a few metres of the densest air the profile carries, and reading it at the camera's radius makes
//    the disc's colour move with the camera in a way the sky behind it — baked at one altitude — does not follow.
static const Real64 SolarSurfaceDeparture = 0.5;   // [m] - above the planet surface, where the disc reads ①

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The radiance incoming along a view direction, disc included.
/// in    Profile          [-]    the widened medium, its sun direction already unit in the local frame
/// in    DirectionX       [-]    unit, atmosphere-local, zenith on the second axis
/// in    DirectionY       [-]
/// in    DirectionZ       [-]
/// in    SkyViewSurface   [-]    ③, sampled through `Shared/`'s inverse of the mapping that baked it
/// in    TransmittanceSurface [-] ①, for the disc's own attenuation to space
/// in    SolarAngularRadius [rad] half-angle of the disc; a non-positive radius draws no disc — `28` §8's open row
/// out   Red/Green/Blue   [-]    linear radiance, unbounded above unity
/// note  🔴 The illuminant's flux is applied to **neither** the sky nor the disc here, and that is what keeps the
///        two in step. ③ is baked without it and the disc is resolved without it, so one flux applied by whoever
///        reads this scales both together. Applying it to the disc alone — which is where a port of the donor
///        formulation lands, since its disc names an illuminance its sky-view lookup does not — brightens the sun
///        away from the sky immediately behind it, and the seam that produces is at the one place in the image
///        the eye is already looking.
/// note  📝 The disc is analytic rather than baked. ③ is a hundred and ninety-two texels of azimuth, and the sun
///        subtends about a quarter of a degree; a disc carried through that surface is a texel wide at best and
///        filtered into a smear at worst.
/// cost  🚩
/// tag   nonallocating, nonthrowing
void ResolveSkyRadiance(MediumProfile Profile,
                        Real64 DirectionX, Real64 DirectionY, Real64 DirectionZ,
                        Texture2D<Real32x4> SkyViewSurface,
                        Texture2D<Real32x4> TransmittanceSurface,
                        SamplerState SurfaceSampler,
                        Real64 SolarAngularRadius,
                        SLATE_OUT(Real64) Red, SLATE_OUT(Real64) Green, SLATE_OUT(Real64) Blue)
{
    Real64 CoordinateX  = 0.0;
    Real64 CoordinateY = 0.0;
    ProjectSkyViewCoordinate(DirectionX, DirectionY, DirectionZ, CoordinateX, CoordinateY);

    const Real32x4 Sampled = SkyViewSurface.SampleLevel(
        SurfaceSampler, Real32x2(Real32(CoordinateX), Real32(CoordinateY)), 0.0f);

    Red   = Real64(Sampled[0]);
    Green = Real64(Sampled[1]);
    Blue  = Real64(Sampled[2]);

    if (SolarAngularRadius <= 0.0)
    {
        return;
    }

    const Real64 SolarCosine = BoundedMagnitude(DirectionX * Profile.SunDirectionX
                                              + DirectionY * Profile.SunDirectionY
                                              + DirectionZ * Profile.SunDirectionZ, -1.0, 1.0);

    const Real64 EdgeCosine = Cosine(SolarAngularRadius);

    if (SolarCosine <= EdgeCosine)
    {
        return;
    }

    // 📐 The limb rises from nothing at the edge to unity at the fraction declared above, smoothed by the
    //    Hermite 3t² − 2t³ rather than taken linearly. A linear rise carries a discontinuity in its first
    //    derivative at both ends, and against the brightest object in the image that reads as two visible rings.
    const Real64 InnerCosine = EdgeCosine + (1.0 - EdgeCosine) * SolarLimbFraction;
    const Real64 Span        = InnerCosine - EdgeCosine;
    const Real64 Departure   = Span > 0.0 ? BoundedMagnitude((SolarCosine - EdgeCosine) / Span, 0.0, 1.0) : 1.0;
    const Real64 Limb        = Departure * Departure * (3.0 - 2.0 * Departure);

    const Real64 DiscRadius = Profile.PlanetRadius + SolarSurfaceDeparture;

    Real64 DiscX  = 0.0;
    Real64 DiscY = 0.0;
    ProjectTransmittanceCoordinate(Profile, DiscRadius,
                                   BoundedMagnitude(Profile.SunDirectionY, -1.0, 1.0),
                                   DiscX, DiscY);

    const Real32x4 DiscTransmit = TransmittanceSurface.SampleLevel(
        SurfaceSampler, Real32x2(Real32(DiscX), Real32(DiscY)), 0.0f);

    Red   += Real64(DiscTransmit[0]) * Limb;
    Green += Real64(DiscTransmit[1]) * Limb;
    Blue  += Real64(DiscTransmit[2]) * Limb;
}

}   // namespace Slate
