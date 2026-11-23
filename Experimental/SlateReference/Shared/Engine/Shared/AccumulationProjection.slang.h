//============================================================================================================================================
//                                                      ACCUMULATIONPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 The count-derived weight, the four rejection tests, and the saturating ceiling that keeps a still workspace responsive.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 🔴 `64` §3: the accumulation weight is derived from the **recorded sample count** and never from a constant.
//    A constant weight is an exponential average that never converges and never fully forgets — it leaves a
//    permanent trail behind moving owners and a permanent floor of residual noise on the ones that stopped.
//    A count-derived weight converges while the workspace is still and resets cleanly when it is not.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE WEIGHT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How much of the incoming sample the accumulation takes.
/// in    HeldCount     [-]  samples already accumulated at this pixel, before this one
/// in    CountLimit  [-]  the declared saturating ceiling
/// out   Weight        [-]  one on the first sample, falling as the count rises, floored at the ceiling
/// note  🔴 The ceiling is not a refinement. Without it a workspace left untouched accumulates a weight so small
///        that a subsequent change takes seconds to appear, and the artist believes the program has stopped
///        responding — `64` §3.
/// note  📐 A weight of one over the incoming count is the running mean, which converges to the true mean while
///        nothing changes. Saturating the count converts it to an exponential average with a declared time
///        constant, which is what a still-but-editable workspace actually wants.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectAccumulationWeight(Unsigned32 HeldCount, Unsigned32 CountLimit)
{
    const Unsigned32 Bounded = HeldCount < CountLimit ? HeldCount : CountLimit;

    return 1.0 / Real64(Bounded + 1u);
}

/// 🧩 The count one pixel carries after accumulating one sample.
/// note  📝 Saturating rather than wrapping. A wrapped count returns to one and the pixel restarts its
///        convergence, which reads as the whole image sharpening and softening on a cycle nobody declared.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR Unsigned32 ProjectAccumulatedCount(Unsigned32 HeldCount, Unsigned32 CountLimit)
{
    return HeldCount < CountLimit ? HeldCount + 1u : CountLimit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE REJECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether a reprojected position lies off the accumulated extent.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool ReprojectionOffExtent(Real64 CoordinateX, Real64 CoordinateY)
{
    return CoordinateX < 0.0 || CoordinateX >= 1.0
        || CoordinateY < 0.0 || CoordinateY >= 1.0;
}

/// 🧩 Whether the history describes the same surface as the incoming sample.
/// in    HeldOwner     [-]  the owner `16` §4.1 resolved there last rotation
/// in    IncomingOwner [-]  the owner resolved there now
/// out   Same             [-]  an integer comparison; Exact
/// note  🔴 The test reads `16` §4.1's **owner** resolution and not the partition identity — `64` §4. A
///        partition identity changes when topology is re-partitioned, and re-partitioning would then discard
///        every pixel's history for a change the artist cannot see.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED SLATE_CONSTEXPR bool ReprojectionSameOwner(Unsigned32 HeldOwner, Unsigned32 IncomingOwner)
{
    return HeldOwner == IncomingOwner;
}

/// 🧩 Whether the history's depth still describes the incoming sample's surface.
/// in    HeldDepth      [-]  reversed, as recorded last rotation
/// in    IncomingDepth  [-]  reversed, as recorded now
/// in    DepthBound     [-]  the declared relative bound
/// out   Rejected        [-]  true where the two describe different parts of one owner
/// note  📐 Relative to the incoming coordinate rather than absolute, so the bound means the same thing at every
///        distance. An absolute bound rejects everything in the distance and accepts everything up close.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED bool ReprojectionDepthRejected(Real64 HeldDepth, Real64 IncomingDepth, Real64 DepthBound)
{
    const Real64 Scale = Magnitude(IncomingDepth) > 0.0 ? Magnitude(IncomingDepth) : 1.0;

    return Magnitude(HeldDepth - IncomingDepth) / Scale > DepthBound;
}

/// 🧩 Bounds one accumulated component against the incoming rotation's local neighbourhood.
/// in    HeldComponent   [-]  the reprojected history
/// in    MinimumComponent  [-]  the least value among the incoming neighbourhood
/// in    MaximumComponent [-] the greatest among it
/// out   Bounded         [-]  the history, brought inside the neighbourhood
/// note  🔴 This is what handles illumination that changed without the surface moving — an illuminant
///        brightened, a stroke textured. `22`'s texturing invalidates nothing here explicitly; the bound resolves
///        it — `64` §4.
/// note  ⚠️ Bounded rather than rejected, because a refusal resets the count and a lighting change that merely
///        brightened a surface does not warrant discarding its whole convergence.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 BoundNeighbourhood(Real64 HeldComponent, Real64 MinimumComponent, Real64 MaximumComponent)
{
    return BoundedMagnitude(HeldComponent, MinimumComponent, MaximumComponent);
}

}   // namespace Slate
