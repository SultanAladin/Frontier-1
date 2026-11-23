//============================================================================================================================================
//                                                          ATMOSPHEREUNIFORM.SLANG
//============================================================================================================================================
// 🧩 The one uniform block every atmosphere entry point reads, and its widening into the shared medium profile.

#pragma once

#include "Shared/AtmosphereProjection.slang.h"

// 📝 🔴 The block is declared **once**, here, and included by all three entry points. A uniform block spelled
//    separately per entry point is the same layout written three times, and a member inserted into two of the
//    three is not a compilation failure — it is two entry points reading the third's fields at an offset, which
//    reaches the artist as an atmosphere that is wrong in one of its three surfaces only.
//
// ⚠️ The block is **32-bit** and the maths is 64-bit. That is deliberate and it is not a concession: the upload
//    crosses the device edge once per medium amendment and carries no precision a half-precision surface could
//    ever express, while the march inside one texel accumulates thirty steps of optical depth and does need the
//    width. `06` negotiates no 64-bit uniform storage and would have to were this block widened.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE UNIFORM BLOCK
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The medium, the sun and the camera altitude, as the host uploads them.
/// note  📝 Every member is scalar and ordered widest-first so the layout carries no implicit padding under any
///        of the three declared layout rules. A vector member here would pad to sixteen bytes under std140 and to
///        its own width elsewhere, and the host mirror would agree with exactly one of them.
/// tag   guarantee
struct MediumUniform
{
    Real32  PlanetRadius            ;   // [m]   - to the surface
    Real32  AtmosphereThickness     ;   // [m]   - above it
    Real32  RayleighScaleHeight     ;   // [m]   - exponential
    Real32  MieScaleHeight          ;   // [m]   - exponential
    Real32  OzoneCentreAltitude     ;   // [m]   - the tent's centre
    Real32  OzoneHalfWidth          ;   // [m]   - the tent falls to nothing at this departure
    Real32  MieAsymmetry            ;   // [-]   - forward-biased; zero is isotropic
    Real32  MieScattering           ;   // [1/m] - wavelength-neutral, at sea level
    Real32  MieExtinction           ;   // [1/m] - scattering plus absorption
    Real32  RayleighScatteringRed   ;   // [1/m] - working space, at sea level
    Real32  RayleighScatteringGreen ;   // [1/m]
    Real32  RayleighScatteringBlue  ;   // [1/m]
    Real32  OzoneAbsorptionRed      ;   // [1/m] - working space, at the tent's centre
    Real32  OzoneAbsorptionGreen    ;   // [1/m]
    Real32  OzoneAbsorptionBlue     ;   // [1/m]
    Real32  SunDirectionX           ;   // [-]   - toward the sun, unit, local frame
    Real32  SunDirectionY           ;   // [-]   - the local zenith is the second axis
    Real32  SunDirectionZ           ;   // [-]
    Real32  CameraAltitude          ;   // [m]   - above the surface, where ③ is baked from
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE WIDENING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Widens the uploaded block into the profile `Shared/` computes against.
/// note  🔴 One assignment per member and nothing derived. A widening that recomputed a coefficient would be a
///        second derivation of what the host already resolved spectrally through `02` §5, and the two would agree
///        only until one of them was amended.
/// cost  ✔️
/// tag   nonallocating, nonthrowing
MediumProfile ResolveMediumProfile(MediumUniform Uploaded)
{
    MediumProfile Resolved;

    Resolved.PlanetRadius             = Real64(Uploaded.PlanetRadius);
    Resolved.AtmosphereThickness      = Real64(Uploaded.AtmosphereThickness);
    Resolved.RayleighScaleHeight      = Real64(Uploaded.RayleighScaleHeight);
    Resolved.MieScaleHeight           = Real64(Uploaded.MieScaleHeight);
    Resolved.OzoneCentreAltitude      = Real64(Uploaded.OzoneCentreAltitude);
    Resolved.OzoneHalfWidth           = Real64(Uploaded.OzoneHalfWidth);
    Resolved.MieAsymmetry             = Real64(Uploaded.MieAsymmetry);
    Resolved.MieScattering            = Real64(Uploaded.MieScattering);
    Resolved.MieExtinction            = Real64(Uploaded.MieExtinction);
    Resolved.RayleighScatteringRed    = Real64(Uploaded.RayleighScatteringRed);
    Resolved.RayleighScatteringGreen  = Real64(Uploaded.RayleighScatteringGreen);
    Resolved.RayleighScatteringBlue   = Real64(Uploaded.RayleighScatteringBlue);
    Resolved.OzoneAbsorptionRed       = Real64(Uploaded.OzoneAbsorptionRed);
    Resolved.OzoneAbsorptionGreen     = Real64(Uploaded.OzoneAbsorptionGreen);
    Resolved.OzoneAbsorptionBlue      = Real64(Uploaded.OzoneAbsorptionBlue);
    Resolved.SunDirectionX            = Real64(Uploaded.SunDirectionX);
    Resolved.SunDirectionY            = Real64(Uploaded.SunDirectionY);
    Resolved.SunDirectionZ            = Real64(Uploaded.SunDirectionZ);
    Resolved.CameraAltitude           = Real64(Uploaded.CameraAltitude);

    return Resolved;
}

/// 🧩 The coordinate of one texel centre on a surface of a declared extent.
/// note  📐 The half-texel offset is what makes the bake and the lookup the same mapping. A bake that placed its
///        samples at texel corners and a lookup that filters between texel centres disagree by half a texel
///        everywhere — uniformly, so nothing looks broken and the horizon simply sits in the wrong place.
/// cost  ✔️
/// tag   nonallocating, nonthrowing
void ProjectTexelCoordinate(Unsigned32 X, Unsigned32 Y,
                            Unsigned32 Width, Unsigned32 Height,
                            SLATE_OUT(Real64) CoordinateX,
                            SLATE_OUT(Real64) CoordinateY)
{
    CoordinateX  = (Real64(X)  + 0.5) / Real64(Width);
    CoordinateY = (Real64(Y) + 0.5) / Real64(Height);
}

}   // namespace Slate
