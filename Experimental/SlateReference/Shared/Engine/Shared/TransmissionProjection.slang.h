//============================================================================================================================================
//                                                       TRANSMISSIONPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 The packed fragment word, the ordering that sorts it, and the sorted insertion that discards the farthest rather than the nearest.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 🔴 `62` §3 collects transmissive fragments into `TransmissionIndex` with atomic sorted insertion on the
//    device, and `82` resolves the same column on the host for a preview. Both orderings are therefore this
//    file's, once: two implementations of one comparison are two that will eventually disagree about which of
//    two coplanar panes is in front, and the artist meets that as the two swapping as they orbit.
//
// 🔴 The ordering is **Exact**. `62` §7 declares the depth comparison Tier A even though depth itself is Tier B,
//    because what is required is not an exact depth but a **stable order** — an order that resolves by arrival
//    flickers, and it flickers most on exactly the coplanar surfaces artists build deliberately.

// 📝 No fragment; never a valid packed surface word. The partition field of a written word is always below
//    `PartitionLimit`, so the all-ones word cannot collide with one.
#define SlateTransmissionAbsent (0xFFFFFFFFu)

// 📝 Triangle ordinals count **within** a partition — `16` §4 — and `Foundation/`'s `PartitionTriangleLimit` is
//    128, so seven bits carry every one of them. The partition ordinal takes the remaining twenty-five, which is
//    above `42`'s own partition ceiling of 2²⁰ with five bits of headroom.
#define SlateTriangleFieldBits (7u)
#define SlateTriangleFieldMask (0x7Fu)

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PACKED WORD
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Packs one fragment's partition and triangle ordinals into the second component of `TransmissionIndex`.
/// in    PartitionIndex  [-]  document-wide, as `16` §4 writes it
/// in    TriangleIndex   [-]  within the partition, never within the document
/// out   SurfaceWord       [-]  the packed pair
/// note  🔴 The same two ordinals `16` §4 writes, in one word rather than two, because the second component of
///        the pair is already spent on the depth key below. A third component would widen `TransmissionIndex`
///        by a third at `TransmissionDepth` layers of the display extent — which is the one target in `08` §2
///        whose extent is multiplied by a capacity rather than by one.
/// cost  ✔️
/// note  Exact — a shift and a mask; identical on the host and on the device.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Unsigned32 PackTransmissionSurface(Unsigned32 PartitionIndex,
                                                                Unsigned32 TriangleIndex)
{
    return (PartitionIndex << SlateTriangleFieldBits) | (TriangleIndex & SlateTriangleFieldMask);
}

/// 🧩 The partition ordinal one packed surface word carries.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Unsigned32 UnpackTransmissionPartition(Unsigned32 SurfaceWord)
{
    return SurfaceWord >> SlateTriangleFieldBits;
}

/// 🧩 The triangle ordinal one packed surface word carries.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Unsigned32 UnpackTransmissionTriangle(Unsigned32 SurfaceWord)
{
    return SurfaceWord & SlateTriangleFieldMask;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DEPTH KEY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The ordering key one reversed-depth coordinate carries.
/// in    ReversedDepth  [-]  in the closed unit interval; near is one — `Foundation/`'s convention
/// out   DepthKey       [-]  an unsigned ordinal ordered identically to the coordinate
/// note  📐 A non-negative floating-point coordinate and its own bit pattern are **identically ordered**, so the
///        key is the bit pattern and the comparison is an integer one. That is what makes the ordering Exact
///        without quantising the depth: a quantisation coarse enough to compare cheaply is one that reports two
///        distinct panes as coplanar, and a fine one is a second precision nobody declared.
/// note  🔴 Reversed depth is bounded below at nought by the convention, so the sign bit is never set and the
///        ordering never inverts. An coordinate outside the interval is bounded rather than trusted — a depth
///        incoming negative would order above every legitimate fragment and occupy the nearest slot forever.
/// cost  ✔️
/// note  Exact — a bound and a reinterpretation; identical on the host and on the device.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Unsigned32 ProjectTransmissionKey(Real64 ReversedDepth)
{
    const Real64 Bounded = BoundedMagnitude(ReversedDepth, FarPlaneDepth, NearPlaneDepth);

    // 📐 Scaled onto the whole unsigned range rather than reinterpreted, because the two toolchains do not
    //    share a reinterpretation spelling and a union is not expressible under one of them. The scale is a
    //    power of two less one, so it is exact at every representable coordinate and monotone throughout.
    return Unsigned32(Bounded * 4294967295.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ORDERING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether one fragment stands in front of another.
/// in    EarlierKey     [-]  the first fragment's depth key
/// in    EarlierSurface [-]  its packed surface word
/// in    LaterKey       [-]  the second fragment's depth key
/// in    LaterSurface   [-]  its packed surface word
/// out   Precedes       [-]  true where the first is nearer, or coplanar and lower in owner order
/// note  🔴 Ties resolve by the **surface word** and never by arrival — `62` §7. The word's high field is the
///        document-wide partition ordinal, and `16`'s registration lays each owner's partitions contiguously,
///        so ordering by it is ordering by owner with a deterministic order inside each one. Two coplanar
///        surfaces therefore order the same way on every rotation, every run and every machine.
/// cost  ✔️
/// note  Exact — two integer comparisons.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool TransmissionPrecedes(Unsigned32 EarlierKey,
                                                       Unsigned32 EarlierSurface,
                                                       Unsigned32 LaterKey,
                                                       Unsigned32 LaterSurface)
{
    return EarlierKey != LaterKey ? EarlierKey > LaterKey : EarlierSurface < LaterSurface;
}

/// 🧩 Where one incoming fragment belongs in a column already held in nearest-first order.
/// in    HeldKey      [-]  the column's depth keys, nearest first
/// in    HeldCount    [-]  entries occupied
/// in    IncomingKey  [-]  the incoming fragment's key
/// in    IncomingSurface [-] its packed surface word
/// in    HeldSurface  [-]  the column's surface words, parallel to HeldKey
/// out   Slot         [-]  below `TransmissionDepth`, or `SlateTransmissionAbsent` where the column is full
///                         and the arrival is farther than everything in it
/// note  🔴 The column is held **nearest first** and the overflow leaves the end. `62` §3.1: the nearest
///        transmissive surface is the one the artist is looking at and the one whose amendment dominates;
///        dropping it to keep a distant one is dropping the visible in favour of the invisible.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Unsigned32 ProjectTransmissionSlot(SLATE_INOUT_SPAN(Unsigned32, HeldKey, TransmissionDepth),
                                                SLATE_INOUT_SPAN(Unsigned32, HeldSurface, TransmissionDepth),
                                                Unsigned32 HeldCount,
                                                Unsigned32 IncomingKey,
                                                Unsigned32 IncomingSurface)
{
    for (Unsigned32 Index = 0u; Index < HeldCount; ++Index)
    {
        if (TransmissionPrecedes(IncomingKey, IncomingSurface, HeldKey[Index], HeldSurface[Index]))
        {
            return Index;
        }
    }

    return HeldCount < TransmissionDepth ? HeldCount : SlateTransmissionAbsent;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE AMENDMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What survives one transmissive fragment, per component.
/// in    Opacity      [-]  channel 8; how much of what is behind is stopped
/// in    Transmission [-]  channel 18; the fraction refracted rather than reflected
/// in    TintComponent[-]  one component of channel 1
/// in    FresnelTerm  [-]  the reflected fraction at this view angle
/// out   Surviving    [-]  the factor applied to what stands behind the fragment
/// note  🔴 Refraction does **not displace** what is behind — `62` §4 and `00` §5.1's third substitution point.
///        A screen-space displacement reads whatever happens to be at the displaced pixel, which is frequently a
///        surface in front of the glass, and the artist sees the foreground smeared through the object it
///        stands behind. What a transmissive owner does is tint, attenuate and blur.
/// note  📐 The four factors multiply rather than compose, because each is independently declared and none is a
///        function of another. An arrangement that folded Fresnel into opacity would make a sheet of glass
///        opaque at grazing angles regardless of the opacity its material declared.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectTransmittedFraction(Real64 Opacity,
                                               Real64 Transmission,
                                               Real64 TintComponent,
                                               Real64 FresnelTerm)
{
    const Real64 Passing   = 1.0 - BoundedMagnitude(Opacity, 0.0, 1.0);
    const Real64 Refracted = BoundedMagnitude(Transmission, 0.0, 1.0);
    const Real64 Reflected = BoundedMagnitude(FresnelTerm, 0.0, 1.0);

    return Passing * Refracted * BoundedMagnitude(TintComponent, 0.0, 1.0) * (1.0 - Reflected);
}

/// 🧩 The width of the transmitted lobe one roughness implies, as a fraction of the display extent.
/// in    Roughness  [-]  channel 3, perceptual
/// out   Width      [-]  zero at a mirror-smooth surface
/// note  🚧 `62` §9 leaves open whether the transmitted lobe reads a reduction of `RadianceSurface`. What is
///        declared here is the width the reader would select a level against; nothing consumes it until that row
///        closes, and declaring it now keeps the roughness channel from being silently unread.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Real64 ProjectTransmittedLobeWidth(Real64 Roughness)
{
    return Roughness * Roughness;
}

}   // namespace Slate
