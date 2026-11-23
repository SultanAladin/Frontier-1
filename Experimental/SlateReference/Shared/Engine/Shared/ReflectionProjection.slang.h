//============================================================================================================================================
//                                                        REFLECTIONPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 `30`'s exact-composite rule, the perturbed reflection direction, and the march step — the arithmetic no failure path may bypass.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Shared/SampleProjection.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 🔴 `30` §1 is the load-bearing rule of that document, and it is here rather than at the resolve site so
//    that the composition cannot be written twice. `18` already added a specular ambient term from `28`, so a
//    reflection simply added on top lights the same surface twice; the resolve therefore **subtracts what `18`
//    contributed and swaps in what the trace found**.
//
// 🔴 Every failure resolves to a weight of nothing, at which point the subtraction and the addition cancel and
//    the pixel keeps exactly `18`'s ambient specular. That is the property that lets the trace be aggressive
//    about giving up: failure is free and invisible, so there is no fallback path to write and none to test.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE EXACT COMPOSITE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Resolves one component of `30` §1's composite.
/// in    CurrentRadiance  [-]  `RadianceSurface` as `18` and `62` left it
/// in    PreAddedComponent [-]  what `18`'s ambient term already contributed here — `ReflectionSurface` RGB
/// in    TracedRadiance    [-]  what the trace found, or anything at all where the weight is nothing
/// in    Weight            [-]  `ReflectionSurface` A; how much of the specular the trace resolved
/// out   Resolved          [-]  the amended radiance
/// note  🔴 The alpha channel carries a **weight** rather than an opacity, and the RGB carries a **pre-added
///        contribution** rather than the trace result. Any other formulation either double-counts where the
///        trace succeeds or darkens where it fails, and the second is the one that ships — a failed trace is
///        exactly where nobody is looking.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ResolveExactComposite(Real64 CurrentRadiance,
                                          Real64 PreAddedComponent,
                                          Real64 TracedRadiance,
                                          Real64 Weight)
{
    const Real64 Resolved = BoundedMagnitude(Weight, 0.0, 1.0);

    return CurrentRadiance - PreAddedComponent * Resolved + TracedRadiance * Resolved;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE REFLECTED DIRECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Reflects a view direction about an orientation, perturbed by roughness.
/// in    ViewX          [-]  toward the camera, unit
/// in    ViewY          [-]
/// in    ViewZ          [-]
/// in    OrientationX   [-]  the perturbed orientation, unit
/// in    OrientationY   [-]
/// in    OrientationZ   [-]
/// in    Roughness      [-]  `18` §2 channel 3, perceptual
/// in    SampleIndex  [-]  the sample; `02` §6's progressive pattern supplies the offset
/// out   ReflectedX/Y/Z [-]  unit
/// note  📐 The perturbation is applied to the **reflected direction** rather than to the orientation, because
///        perturbing the orientation would also perturb the cosine every downstream term reads. What `30` §4
///        asks for is a wider lobe, not a differently oriented surface.
/// note  🔴 The pattern is `02` §6's and is never invented here — `64` accumulates `RadianceSurface` across
///        rotations, so a trace sampled from a pattern that is not progressive converges to a different value
///        than the one `64` assumes it is averaging.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectReflectedDirection(Real64 ViewX, Real64 ViewY, Real64 ViewZ,
                                            Real64 OrientationX, Real64 OrientationY, Real64 OrientationZ,
                                            Real64 Roughness,
                                            Unsigned32 SampleIndex,
                                            SLATE_OUT(Real64) ReflectedX,
                                            SLATE_OUT(Real64) ReflectedY,
                                            SLATE_OUT(Real64) ReflectedZ)
{
    const Real64 Alignment = OrientationX * ViewX + OrientationY * ViewY + OrientationZ * ViewZ;

    Real64 MirrorX = 2.0 * Alignment * OrientationX - ViewX;
    Real64 MirrorY = 2.0 * Alignment * OrientationY - ViewY;
    Real64 MirrorZ = 2.0 * Alignment * OrientationZ - ViewZ;

    const Real64 Parameter = BoundedMagnitude(Roughness, 0.0, 1.0) * BoundedMagnitude(Roughness, 0.0, 1.0);

    if (Parameter > 0.0)
    {
        Real64 FirstCoordinate  = 0.0;
        Real64 SecondCoordinate = 0.0;
        ProjectPlanarSample(SampleIndex + 1u, FirstCoordinate, SecondCoordinate);

        Real64 SampleX = 0.0;
        Real64 SampleY = 0.0;
        Real64 SampleZ = 0.0;
        ProjectHemisphericalSample(FirstCoordinate, SecondCoordinate, SampleX, SampleY, SampleZ);

        // 📐 The hemispherical sample is about its own third axis, so it is folded in as a displacement scaled
        //    by the distribution parameter rather than rotated onto a basis. A basis built here would be a
        //    second tangent frame beside the one `18` §1.1 already interpolated, and the two would disagree
        //    wherever the domain does.
        MirrorX += Parameter * (SampleX - MirrorX * SampleZ);
        MirrorY += Parameter * (SampleY - MirrorY * SampleZ);
        MirrorZ += Parameter * (1.0 - SampleZ) * (MirrorZ >= 0.0 ? 1.0 : -1.0) * 0.0;
    }

    const Real64 Length = SquareRoot(MirrorX * MirrorX + MirrorY * MirrorY + MirrorZ * MirrorZ);

    if (Length <= 0.0)
    {
        ReflectedX = 0.0;
        ReflectedY = 0.0;
        ReflectedZ = 1.0;

        return;
    }

    ReflectedX = MirrorX / Length;
    ReflectedY = MirrorY / Length;
    ReflectedZ = MirrorZ / Length;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE FAILURES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether a marched position has left the display extent.
/// note  🔴 `30` §3's first failure row. Weight zero, and §1's guarantee makes it a no-op.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool ReflectionLeftExtent(Real64 CoordinateX, Real64 CoordinateY)
{
    return CoordinateX < 0.0 || CoordinateX >= 1.0
        || CoordinateY < 0.0 || CoordinateY >= 1.0;
}

/// 🧩 Whether a crossing is a genuine hit or lies behind a surface by more than the declared thickness.
/// in    MarchedDepth   [-]  the ray's own reversed depth at the sample
/// in    RecordedDepth  [-]  `DepthSurface` at the sample, reversed
/// in    Thickness      [-]  the declared thickness threshold, in reversed depth
/// out   Crossed        [-]  true only where the ray passed just behind the recorded surface
/// note  📐 Reversed depth — near is one — so "behind" is a **lesser** coordinate and the crossing condition reads
///        the way it does. Written the other way round the march reports a crossing at its first step, and every
///        reflection is a copy of the surface reflecting it.
/// note  🔴 A crossing deeper than the thickness is **not** a hit. Without the threshold a ray passing behind a
///        thin object reports the object's own surface, and a mirror shows a duplicate of whatever stands in
///        front of it at every grazing angle.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool ReflectionCrossed(Real64 MarchedDepth, Real64 RecordedDepth, Real64 Thickness)
{
    return MarchedDepth < RecordedDepth && MarchedDepth > RecordedDepth - Thickness;
}

/// 🧩 Whether a roughness is above the ceiling at which the trace is skipped entirely.
/// note  🔴 `30` §4: above the declared roughness the trace is skipped, weight is nothing, `18`'s ambient
///        stands, and the visual difference is below the threshold that would justify the cost.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool ReflectionSkipped(Real64 Roughness, Real64 RoughnessLimit)
{
    return Roughness > RoughnessLimit;
}

}   // namespace Slate
