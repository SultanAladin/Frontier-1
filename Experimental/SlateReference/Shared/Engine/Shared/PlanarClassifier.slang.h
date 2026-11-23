//============================================================================================================================================
//                                                        PLANARCLASSIFIER.SLANG.H
//============================================================================================================================================
// 🧩 Whether a position is inside, on, or outside a closed planar outline — exact over the flattened polyline.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Shared/OrientationClassifier.slang.h"

// 📐 The classifier answers containment by accumulating, over every edge of a closed polyline, both the winding
//    number and the crossing count — the first for the non-zero rule, the second for even-odd. Each edge's
//    contribution is decided by the **sign** of an orientation determinant, which is why the whole classification
//    inherits ClassifyOrientation's exactness rather than declaring an exactness of its own.
//
// 🔴 Exact over the polyline it is given, and the polyline is produced once. `CurveSolver`'s flattening is
//    Bounded, so flattening on the host and again on the device would give two polylines that agree to a
//    tolerance and disagree in their last bit — and an exact predicate over two different inputs is exact about
//    the wrong thing. `52` §4's parity requirement is met by classifying one shared polyline on both sides.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  EDGE ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Accumulates one edge's contribution to the winding number, the crossing count and the boundary condition.
/// in    AlphaX          [-]  the edge's first position
/// in    AlphaY          [-]
/// in    BetaX           [-]  the edge's second position
/// in    BetaY           [-]
/// in    PointX          [-]  the position being classified
/// in    PointY          [-]
/// out   WindingCount    [-]  raised or lowered by one where the edge crosses the ray
/// out   CrossingCount   [-]  raised by one where the edge crosses the ray, in either direction
/// out   DividerTouched [-]  set where the position lies exactly on this edge
/// err   never refuses; every finite input contributes
/// cost  🚩
/// note  📐 The ray is taken along increasing abscissa, and the half-open coordinate test — first inclusive, second
///        exclusive — is what makes a position level with a shared vertex contribute exactly once. A closed test
///        counts it twice on one side and not at all on the other, and the artist sees a hole at every vertex.
/// note  Exact — the classification agrees bit for bit between the host form and the device form.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void AccumulateWinding(Real64 AlphaX, Real64 AlphaY,
                                    Real64 BetaX,  Real64 BetaY,
                                    Real64 PointX, Real64 PointY,
                                    SLATE_INOUT(Signed32) WindingCount,
                                    SLATE_INOUT(Signed32) CrossingCount,
                                    SLATE_INOUT(Signed32) DividerTouched)
{
    const Signed32 Orientation = ClassifyOrientation(AlphaX, AlphaY, BetaX, BetaY, PointX, PointY);

    // 📝 Collinear and within the edge's closed extent is on the boundary. Collinear alone is not: the whole
    //    infinite line through the edge is collinear with it, and most of that line is nowhere near the outline.
    if (Orientation == 0)
    {
        const Real64 MinimumX    = AlphaX < BetaX ? AlphaX : BetaX;
        const Real64 MaximumX = AlphaX < BetaX ? BetaX  : AlphaX;
        const Real64 MinimumY    = AlphaY < BetaY ? AlphaY : BetaY;
        const Real64 MaximumY = AlphaY < BetaY ? BetaY  : AlphaY;

        if (PointX >= MinimumX && PointX <= MaximumX && PointY >= MinimumY && PointY <= MaximumY)
        {
            DividerTouched = 1;
            return;
        }
    }

    if (AlphaY <= PointY && BetaY > PointY && Orientation > 0)
    {
        WindingCount  = WindingCount + 1;
        CrossingCount = CrossingCount + 1;
    }
    else if (BetaY <= PointY && AlphaY > PointY && Orientation < 0)
    {
        WindingCount  = WindingCount - 1;
        CrossingCount = CrossingCount + 1;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Resolves accumulated counts into a containment classification.
/// in    WindingCount    [-]  as accumulated over every edge
/// in    CrossingCount   [-]  as accumulated over every edge
/// in    DividerTouched [-]  non-zero where any edge held the position
/// in    EvenOddDeclared [-]  non-zero selects the even-odd rule, zero the non-zero rule
/// out   Containment     [-]  +1 inside, 0 exactly on the boundary, −1 outside
/// cost  ✔️
/// note  📝 The fill rule crosses as an integer rather than as an enumeration, because an enumeration declared on
///        the host has no spelling the shader toolchain shares. `52` holds the declared rule; this reads the one
///        bit of it that the classification depends on.
/// note  🔴 A boundary position resolves to zero and not to inside. `70` resolves coverage from this and a
///        boundary reported as interior gives every outline a one-texel bias outward at its own edge.
/// note  Exact — an integer decision over integer counts.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Signed32 ResolveContainment(Signed32 WindingCount,
                                         Signed32 CrossingCount,
                                         Signed32 DividerTouched,
                                         Signed32 EvenOddDeclared)
{
    if (DividerTouched != 0)
    {
        return 0;
    }

    if (EvenOddDeclared != 0)
    {
        return (CrossingCount & 1) != 0 ? 1 : -1;
    }

    return WindingCount != 0 ? 1 : -1;
}

}   // namespace Slate
