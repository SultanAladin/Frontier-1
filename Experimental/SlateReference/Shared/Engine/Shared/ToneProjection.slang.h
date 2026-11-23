//============================================================================================================================================
//                                                          TONEPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 The monotonic range compression, and the hue behaviour that is declared rather than inherited from per-channel arithmetic.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 🔴 `66` §3: the scene carries radiance without an upper bound — `18` §2's emissive channels are unbounded —
//    and the display has one. The projection compresses that range and is required to be **monotonic**: a
//    brighter radiance is never a darker display code, or the artist can brighten a highlight and watch it darken.
//
// 🔴 The projection is **not invertible in general**, which is why `36` §6 samples a colour from the workspace
//    scene-referred, before this. No correction applied afterwards recovers what the compression discarded.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE COMPRESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Compresses one scene-referred magnitude into the display's range.
/// in    Magnitude     [-]  scene-referred, exposure already applied, non-negative
/// in    WhiteMagnitude[-]  the scene magnitude that maps to full display code
/// out   Compressed    [-]  in the closed unit interval
/// note  📐 A rational curve rather than a fitted piecewise one: it is monotonic everywhere by construction, it
///        maps nought to nought and the declared white to unity exactly, and it has one parameter the artist can
///        be shown. A fitted curve is monotonic because somebody checked, and the check does not survive a
///        parameter change.
/// note  🔴 A negative magnitude is transferred by odd reflection rather than clamped. `36`'s working space is
///        wider than the display, so negative display coordinates arise legitimately; clamping here loses the
///        sign before `66` §4 has encoded it, and the out-of-gamut colour becomes an in-gamut wrong one.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectToneCompressed(Real64 Magnitude, Real64 WhiteMagnitude)
{
    const Real64 Signum   = Magnitude < 0.0 ? -1.0 : 1.0;
    const Real64 Absolute = Magnitude < 0.0 ? -Magnitude : Magnitude;

    const Real64 White = WhiteMagnitude > 0.0 ? WhiteMagnitude : 1.0;

    // 📐 y = x(1 + x/W²)/(1 + x). At x = 0 this is 0; at x = W it is exactly 1; its derivative is positive for
    //    every non-negative x and every positive W, so monotonicity is a property of the form rather than of the
    //    parameter — which is what lets `66` §9's open row change W without re-establishing the gate.
    const Real64 Numerator   = Absolute * (1.0 + Absolute / (White * White));
    const Real64 Denominator = 1.0 + Absolute;

    return Signum * Numerator / Denominator;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE HUE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Compresses three components with a declared hue behaviour.
/// in    Red               [-]  scene-referred, exposure applied
/// in    Green             [-]
/// in    Blue              [-]
/// in    WhiteMagnitude    [-]  the scene magnitude mapping to full display code
/// in    PreservationBlend [-]  nought compresses per channel; one preserves hue entirely
/// out   OutRed/Green/Blue [-]  display-range magnitudes
/// note  🔴 `66` §3.1: compressing the three channels independently rotates hue as it desaturates, so a saturated
///        red highlight becomes orange as it brightens. That is not a preference — an artist texturing a saturated
///        colour and watching it shift as they brighten it cannot tell whether the shift is in their texture or in
///        the display, and every correction they make is a correction to the wrong thing.
/// note  📐 Hue preservation compresses the **greatest** component and scales the other two by the same factor,
///        so the ratio between them is untouched and the hue is exactly preserved. The blend between the two
///        behaviours is declared because full preservation keeps saturation all the way to clipping, which reads
///        as a hard-edged bloom; the desaturating path is what makes a bright highlight read as bright.
/// note  🔴 A neutral radiance projects to a neutral display code under **both** behaviours and therefore under
///        every blend — `66` §3's neutral-axis requirement, satisfied by construction rather than by a test.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectToneTriple(Real64 Red, Real64 Green, Real64 Blue,
                                    Real64 WhiteMagnitude,
                                    Real64 PreservationBlend,
                                    SLATE_OUT(Real64) OutRed,
                                    SLATE_OUT(Real64) OutGreen,
                                    SLATE_OUT(Real64) OutBlue)
{
    const Real64 PerChannelRed   = ProjectToneCompressed(Red,   WhiteMagnitude);
    const Real64 PerChannelGreen = ProjectToneCompressed(Green, WhiteMagnitude);
    const Real64 PerChannelBlue  = ProjectToneCompressed(Blue,  WhiteMagnitude);

    Real64 Maximum = Red > Green ? Red : Green;
    Maximum        = Maximum > Blue ? Maximum : Blue;

    Real64 PreservedRed   = PerChannelRed;
    Real64 PreservedGreen = PerChannelGreen;
    Real64 PreservedBlue  = PerChannelBlue;

    if (Maximum > 0.0)
    {
        const Real64 Scale = ProjectToneCompressed(Maximum, WhiteMagnitude) / Maximum;

        PreservedRed   = Red   * Scale;
        PreservedGreen = Green * Scale;
        PreservedBlue  = Blue  * Scale;
    }

    const Real64 Blend = BoundedMagnitude(PreservationBlend, 0.0, 1.0);

    OutRed   = PerChannelRed   + (PreservedRed   - PerChannelRed)   * Blend;
    OutGreen = PerChannelGreen + (PreservedGreen - PerChannelGreen) * Blend;
    OutBlue  = PerChannelBlue  + (PreservedBlue  - PerChannelBlue)  * Blend;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE EXPOSURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The linear scale one exposure value applies.
/// in    ExposureValue  [EV]  the authored camera property — `46` §6
/// out   Scale          [-]   a doubling per stop
/// note  📝 Applied first, in the working space, as a scale on radiance — `66` §2. Applied after the compression
///        it would be a scale on display code, which brightens the compressed image rather than exposing the
///        scene, and the highlights would never recover their detail.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectExposureScale(Real64 ExposureValue)
{
    return Power(2.0, ExposureValue);
}

}   // namespace Slate
