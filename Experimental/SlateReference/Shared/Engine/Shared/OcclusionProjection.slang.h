//============================================================================================================================================
//                                                       OCCLUSIONPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 Which component of the packed word one illuminant occupies, the comparison that decides occlusion, and the upsample `18` reads it through.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 🔴 `60` §3.1 packs one visibility per illuminant per pixel into one RGBA8 word, **by `OcclusionIndex`
//    position**, and `18` unpacks by that same position. The position rule therefore crosses the toolchain seam
//    in both directions and is declared once here — the identical argument `AtmosphereProjection.slang.h` makes
//    for its three surface parameterisations. Two implementations of one position rule are two that will
//    eventually disagree about which slot an illuminant occupies, and the artist meets that as one light casting
//    another light's shadow.
//
// ⚠️ The packed capacity is `DirectOcclusionCapacity` and the reaching set is `44` §5's, which is wider. The
//    excess is integrated **unattenuated** and reported — `60` §3.1. Dropping the excess instead makes an
//    over-lit region go dark, which reads as a defect; leaving it unshadowed reads as missing shadow, which is
//    what it is.

// 📝 No slot; never a valid component ordinal. Declared as a constant rather than as an enumeration for the
//    reason `OcclusionUniform.slang` gives: an enumeration declared on the host has no spelling the shader
//    toolchain shares without a second declaration that must be kept identical.
#define SlateOcclusionSlotAbsent (0xFFFFFFFFu)

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PACKED SLOT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which component of the packed word one position in the reaching set occupies.
/// in    ReachIndex  [-]  the illuminant's position in `44` §5's reaching set, in identity order
/// out   Slot          [-]  below `DirectOcclusionCapacity`, or `SlateOcclusionSlotAbsent` beyond it
/// note  🔴 The identity mapping below the capacity, and absence above it. It is written as a routine rather
///        than as a comparison at each site precisely because it is trivial: a trivial rule spelled at six sites
///        is one that acquires an off-by-one at exactly one of them, and the sixth site is `18`'s unpack.
/// cost  ✔️
/// note  Exact — an integer comparison; identical on the host and on the device by construction.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Unsigned32 ProjectOcclusionSlot(Unsigned32 ReachIndex)
{
    return ReachIndex < DirectOcclusionCapacity ? ReachIndex : SlateOcclusionSlotAbsent;
}

/// 🧩 How many of a reaching set the packed word cannot carry.
/// out   Truncated  [-]  zero where the whole set fits
/// note  📝 Counted rather than derived at each reporting site, so `86`'s truncation row and the recording that
///        integrates the excess unattenuated read one number.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Unsigned32 ClassifyOcclusionTruncation(Unsigned32 ReachingCount)
{
    return ReachingCount > DirectOcclusionCapacity ? ReachingCount - DirectOcclusionCapacity : 0u;
}

/// 🧩 The visibility a slot carries, or full visibility where the slot is absent.
/// in    PackedRed    [-]  the word's first component, at slot nought
/// in    PackedGreen  [-]  its second, at slot one
/// in    PackedBlue   [-]  its third, at slot two
/// in    PackedAlpha  [-]  its fourth, at slot three
/// in    Slot         [-]  as `ProjectOcclusionSlot` resolved it
/// out   Visible      [-]  unity where nothing occludes, and unity for an absent slot
/// note  🔴 An absent slot resolves to **unity** and never to zero. `60` §3.1: the excess is integrated
///        unattenuated, so the truncated illuminant contributes its whole direct term. Resolving to zero would
///        make the sixteenth illuminant of a partition darken the surface it was meant to light.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ResolveOcclusionSlot(Real64 PackedRed,
                                         Real64 PackedGreen,
                                         Real64 PackedBlue,
                                         Real64 PackedAlpha,
                                         Unsigned32 Slot)
{
    if (Slot == 0u) { return PackedRed;   }
    if (Slot == 1u) { return PackedGreen; }
    if (Slot == 2u) { return PackedBlue;  }
    if (Slot == 3u) { return PackedAlpha; }

    return 1.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE COMPARISON
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether one receiver stands behind what its illuminant's projection recorded.
/// in    ReceiverDepth  [-]  the receiver's own coordinate in the projection, reversed
/// in    RecordedDepth  [-]  the nearest coordinate the projection recorded there, reversed
/// in    GrazingTangent [-]  the tangent of the angle between the surface and the incidence direction
/// out   Occluded       [-]  true only where something nearer to the illuminant stands
/// note  📐 Reversed depth throughout — near is one. The receiver is occluded when its own coordinate is **below**
///        the recorded one by more than the offset, so the offset is subtracted from the recorded coordinate and
///        never added to it. Written the other way round, every surface shadows itself completely and the image
///        is uniformly unlit by every registered illuminant at once.
/// note  🔴 The offset is slope-scaled and both of its terms live in `Foundation/` — `60` §7 and `02` §8. A
///        constant term alone either leaves a gap under every contact or fails to clear a surface at a grazing
///        angle, and the two failures are corrected by moving one number in opposite directions.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED bool DepthOccludesReceiver(Real64 ReceiverDepth, Real64 RecordedDepth, Real64 GrazingTangent)
{
    const Real64 Bounded = GrazingTangent < 0.0 ? 0.0 : (GrazingTangent > 8.0 ? 8.0 : GrazingTangent);
    const Real64 Offset  = ShadowComparisonOffset + ShadowComparisonSlopeFactor * Bounded;

    return ReceiverDepth < RecordedDepth - Offset;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PENUMBRA
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The penumbra one emission shape casts, as a fraction of the projection's own extent.
/// in    EmissionSize     [mm]  the shape's declared size — `44` §3
/// in    OccluderDistance [mm]  from the illuminant to what stands in the way
/// in    ReceiverDistance [mm]  from the illuminant to the surface being shaded
/// out   Width            [-]   zero where the occluder and the receiver coincide
/// note  📐 Similar triangles: the shape's size scaled by how far the receiver stands beyond the occluder,
///        divided by the occluder's own distance. `60` §3.2 resolves the width from the declared size and the
///        occluder distance, and this is that sentence as arithmetic.
/// note  🔴 A receiver at or in front of the occluder casts **nothing**. The subtraction is signed and a
///        negative width filtered as though it were positive produces a penumbra that widens as a surface
///        approaches its own occluder, which reads as contact shadows blooming outward on approach.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectPenumbraWidth(Real64 EmissionSize, Real64 OccluderDistance, Real64 ReceiverDistance)
{
    if (OccluderDistance <= 0.0 || ReceiverDistance <= OccluderDistance)
    {
        return 0.0;
    }

    return EmissionSize * (ReceiverDistance - OccluderDistance) / OccluderDistance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE UPSAMPLE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The weight one half-extent tap carries at a display-extent pixel.
/// in    CentreDepth  [-]  the shading pixel's own coordinate
/// in    SampleDepth  [-]  the coarse tap's coordinate
/// out   Weight       [-]  unity where the two describe one surface, falling to nothing across a discontinuity
/// note  🔴 `60` §5: the ambient term is upsampled with a **depth-aware** weighting and never bilinearly. A
///        bilinear read crosses depth discontinuities and pulls a background surface's occlusion onto a
///        foreground silhouette, which the artist meets as a dark fringe around every object in the workspace.
/// note  📐 Relative to the centre coordinate rather than absolute, so the bound means the same thing at every
///        distance. An absolute bound rejects every tap in the distance and accepts every tap up close, which
///        is the fringe appearing at one depth and vanishing at another.
/// note  📝 The falloff is linear in the departure rather than a step. A step accepts a tap and then rejects the
///        one beside it, and the boundary between the two is a hard edge in the upsampled term.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectDepthAwareWeight(Real64 CentreDepth, Real64 SampleDepth)
{
    const Real64 Scale     = Magnitude(CentreDepth) > 0.0 ? Magnitude(CentreDepth) : 1.0;
    const Real64 Departure = Magnitude(CentreDepth - SampleDepth) / Scale;

    if (Departure >= AmbientUpsampleDepthBound)
    {
        return 0.0;
    }

    return 1.0 - Departure / AmbientUpsampleDepthBound;
}

/// 🧩 Four half-extent taps resolved into one display-extent occlusion.
/// out   Occlusion  [-]  the weighted mean; the nearest tap alone where every weight vanishes
/// note  🔴 A pixel whose every tap crosses a discontinuity takes the **nearest** tap rather than unity. Unity
///        is fully unoccluded, so a silhouette one texel wide would read as a bright rim — the one artefact the
///        depth-aware weighting exists to remove, reintroduced by its own fallback.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ResolveAmbientUpsample(Real64 CentreDepth,
                                           Real64 DepthLowerLeft,  Real64 OcclusionLowerLeft,
                                           Real64 DepthLowerRight, Real64 OcclusionLowerRight,
                                           Real64 DepthUpperLeft,  Real64 OcclusionUpperLeft,
                                           Real64 DepthUpperRight, Real64 OcclusionUpperRight)
{
    const Real64 WeightLowerLeft  = ProjectDepthAwareWeight(CentreDepth, DepthLowerLeft);
    const Real64 WeightLowerRight = ProjectDepthAwareWeight(CentreDepth, DepthLowerRight);
    const Real64 WeightUpperLeft  = ProjectDepthAwareWeight(CentreDepth, DepthUpperLeft);
    const Real64 WeightUpperRight = ProjectDepthAwareWeight(CentreDepth, DepthUpperRight);

    const Real64 Accumulated = WeightLowerLeft + WeightLowerRight + WeightUpperLeft + WeightUpperRight;

    if (Accumulated <= 0.0)
    {
        Real64 NearestDepth     = DepthLowerLeft;
        Real64 NearestOcclusion = OcclusionLowerLeft;

        if (Magnitude(DepthLowerRight - CentreDepth) < Magnitude(NearestDepth - CentreDepth))
        {
            NearestDepth     = DepthLowerRight;
            NearestOcclusion = OcclusionLowerRight;
        }

        if (Magnitude(DepthUpperLeft - CentreDepth) < Magnitude(NearestDepth - CentreDepth))
        {
            NearestDepth     = DepthUpperLeft;
            NearestOcclusion = OcclusionUpperLeft;
        }

        if (Magnitude(DepthUpperRight - CentreDepth) < Magnitude(NearestDepth - CentreDepth))
        {
            NearestOcclusion = OcclusionUpperRight;
        }

        return NearestOcclusion;
    }

    return (WeightLowerLeft  * OcclusionLowerLeft
          + WeightLowerRight * OcclusionLowerRight
          + WeightUpperLeft  * OcclusionUpperLeft
          + WeightUpperRight * OcclusionUpperRight) / Accumulated;
}

}   // namespace Slate
