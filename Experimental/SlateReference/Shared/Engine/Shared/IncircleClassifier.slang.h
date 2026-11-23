//============================================================================================================================================
//                                                        INCIRCLECLASSIFIER.SLANG.H
//============================================================================================================================================
// 🧩 Whether a position lies inside the circle through three others — filtered fast path over an exact expansion.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Shared/OrientationClassifier.slang.h"

// 📐 The predicate answers: is Delta inside, on, or outside the circle through Alpha, Beta and Gamma. Taking
//    every position relative to Delta and lifting each onto the paraboloid, the quantity is
//
//        | αₓ−δₓ   α_y−δ_y   ‖α−δ‖² |
//        | βₓ−δₓ   β_y−δ_y   ‖β−δ‖² |
//        | γₓ−δₓ   γ_y−δ_y   ‖γ−δ‖² |
//
//    and only its **sign** is guaranteeed. The sign is positive for a Delta inside the circle when Alpha, Beta
//    and Gamma wind counter-clockwise, and negates with their winding — which is why every consumer classifies
//    the winding first rather than assuming it.
//
// 📐 Expanded along the third column the determinant is a sum of three products, each of a two-term lift against
//    a two-by-two determinant. Both factors are short exact expansions, so the whole quantity is the accumulated
//    pairwise product of the two — degree four in the inputs, and never evaluated in floating point on this path.

// 💾 Three lifted products contribute at most thirty-two terms each, so ninety-six terms is the worst the arena
//    can hold. The declared extent carries margin over that. On a device this is a kibibyte of scratch per lane,
//    which is the reason `68` solves its unwrap on the host through `34` and the reason the filter below decides
//    every well-conditioned input before the arena is ever touched.
#define SlateIncircleCapacity 128

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                               THE LIFTED ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The arithmetic primitives are `OrientationClassifier`'s, unchanged and unduplicated. Only the arena extent
//    differs, and an extent cannot be shared: the shader toolchain fixes array extents in the signature, so a
//    routine over a forty-term arena cannot be handed a hundred-and-twenty-eight-term one.

/// 🧩 Accumulates one term into the incircle arena, exactly, preserving the increasing-magnitude ordering.
/// in    Arena      [-]  the terms accumulated so far, in increasing magnitude
/// in    TermCount  [-]  how many of them are occupied
/// in    Incoming   [-]  the term to accumulate
/// out   TermCount  [-]  the occupied count after accumulation
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void AccumulateIncircle(SLATE_INOUT_SPAN(Real64, Arena, SlateIncircleCapacity),
                                     SLATE_INOUT(Signed32) TermCount,
                                     Real64                Incoming)
{
    Real64   Carried  = Incoming;
    Signed32 Occupied = 0;

    for (Signed32 TermIndex = 0; TermIndex < TermCount; ++TermIndex)
    {
        Real64 Leading = 0.0;
        Real64 Residue = 0.0;
        SumExactly(Arena[TermIndex], Carried, Leading, Residue);
        Carried = Leading;

        if (Residue != 0.0)
        {
            Arena[Occupied] = Residue;
            ++Occupied;
        }
    }

    if (Carried != 0.0)
    {
        Arena[Occupied] = Carried;
        ++Occupied;
    }

    TermCount = Occupied;
}

/// 🧩 Accumulates the exact product of two short expansions into the incircle arena.
/// in    Lift        [-]  the lifted squared distance, as an expansion
/// in    LiftCount   [-]  its occupied count
/// in    Cross       [-]  the two-by-two determinant, as an expansion
/// in    CrossCount  [-]  its occupied count
/// cost  🚩
/// note  📐 Every pairwise product is exact in two terms, so the accumulated sum is the exact product of the two
///        expansions. Multiplying their leading terms alone would be the ordinary floating-point product wearing
///        an expansion's clothes.
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void AccumulateLifted(SLATE_INOUT_SPAN(Real64, Arena, SlateIncircleCapacity),
                                   SLATE_INOUT(Signed32) TermCount,
                                   SLATE_INOUT_SPAN(Real64, Lift, SlateExpansionCapacity),
                                   Signed32              LiftCount,
                                   SLATE_INOUT_SPAN(Real64, Cross, SlateExpansionCapacity),
                                   Signed32              CrossCount)
{
    for (Signed32 LiftIndex = 0; LiftIndex < LiftCount; ++LiftIndex)
    {
        for (Signed32 CrossIndex = 0; CrossIndex < CrossCount; ++CrossIndex)
        {
            Real64 Leading = 0.0;
            Real64 Residue = 0.0;
            ProductExactly(Lift[LiftIndex], Cross[CrossIndex], Leading, Residue);

            AccumulateIncircle(Arena, TermCount, Residue);
            AccumulateIncircle(Arena, TermCount, Leading);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PREDICATE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Classifies Delta against the circle through Alpha, Beta and Gamma.
/// in    AlphaX     [-]  first position of the triangle, abscissa
/// in    AlphaY     [-]  first position of the triangle, coordinate
/// in    BetaX      [-]  second position, abscissa
/// in    BetaY      [-]  second position, coordinate
/// in    GammaX     [-]  third position, abscissa
/// in    GammaY     [-]  third position, coordinate
/// in    DeltaX     [-]  the position being classified, abscissa
/// in    DeltaY     [-]  the position being classified, coordinate
/// out   Incircle   [-]  +1 inside, 0 exactly cocircular, −1 outside — for a counter-clockwise triangle
/// err   never refuses; the sign is total over every finite input
/// cost  🚩
/// note  ✔️ on the filtered path; the 🚩 is the arena, reached only when the filter cannot exclude zero.
/// note  🔴 The sign is **relative to the triangle's winding** and negates with it. A caller that does not
///        classify the winding first has written a predicate whose answer inverts on half its inputs, and the
///        inversion presents as an unwrap that flips one chart in ten rather than as a wrong answer everywhere.
/// note  🔴 A Delta coincident with any of the three returns exactly zero, on the filtered path, because the
///        lifted distance and both surviving determinants vanish identically rather than approximately.
/// note  Exact — the sign agrees bit for bit between the host form and the device form.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Signed32 ClassifyIncircle(Real64 AlphaX, Real64 AlphaY,
                                       Real64 BetaX,  Real64 BetaY,
                                       Real64 GammaX, Real64 GammaY,
                                       Real64 DeltaX, Real64 DeltaY)
{
    const Real64 AlphaSpanX = AlphaX - DeltaX;
    const Real64 AlphaSpanY = AlphaY - DeltaY;
    const Real64 BetaSpanX  = BetaX  - DeltaX;
    const Real64 BetaSpanY  = BetaY  - DeltaY;
    const Real64 GammaSpanX = GammaX - DeltaX;
    const Real64 GammaSpanY = GammaY - DeltaY;

    const Real64 BetaGammaLeft  = BetaSpanX  * GammaSpanY;
    const Real64 BetaGammaRight = GammaSpanX * BetaSpanY;
    const Real64 GammaAlphaLeft  = GammaSpanX * AlphaSpanY;
    const Real64 GammaAlphaRight = AlphaSpanX * GammaSpanY;
    const Real64 AlphaBetaLeft   = AlphaSpanX * BetaSpanY;
    const Real64 AlphaBetaRight  = BetaSpanX  * AlphaSpanY;

    const Real64 AlphaLift = AlphaSpanX * AlphaSpanX + AlphaSpanY * AlphaSpanY;
    const Real64 BetaLift  = BetaSpanX  * BetaSpanX  + BetaSpanY  * BetaSpanY;
    const Real64 GammaLift = GammaSpanX * GammaSpanX + GammaSpanY * GammaSpanY;

    const Real64 Filtered = AlphaLift * (BetaGammaLeft  - BetaGammaRight)
                          + BetaLift  * (GammaAlphaLeft - GammaAlphaRight)
                          + GammaLift * (AlphaBetaLeft  - AlphaBetaRight);

    // 📐 The permanent is the same expression with every subtraction replaced by an addition of magnitudes. It
    //    bounds the accumulated rounding of the filtered evaluation from above, so a filtered sign exceeding it
    //    is guaranteeed and nothing else is.
    const Real64 Permanent = (Magnitude(BetaGammaLeft)  + Magnitude(BetaGammaRight))  * AlphaLift
                           + (Magnitude(GammaAlphaLeft) + Magnitude(GammaAlphaRight)) * BetaLift
                           + (Magnitude(AlphaBetaLeft)  + Magnitude(AlphaBetaRight))  * GammaLift;

    const Real64 ErrorEdge = IncircleErrorFactor * Permanent;

    if (Filtered > ErrorEdge)
    {
        return 1;
    }

    if (-Filtered > ErrorEdge)
    {
        return -1;
    }

    Real64   BetaGammaCross[SlateExpansionCapacity];
    Signed32 BetaGammaCount = 0;
    AccumulateProduct(BetaGammaCross, BetaGammaCount, BetaSpanX,  GammaSpanY,  1.0);
    AccumulateProduct(BetaGammaCross, BetaGammaCount, GammaSpanX, BetaSpanY,  -1.0);

    Real64   GammaAlphaCross[SlateExpansionCapacity];
    Signed32 GammaAlphaCount = 0;
    AccumulateProduct(GammaAlphaCross, GammaAlphaCount, GammaSpanX, AlphaSpanY,  1.0);
    AccumulateProduct(GammaAlphaCross, GammaAlphaCount, AlphaSpanX, GammaSpanY, -1.0);

    Real64   AlphaBetaCross[SlateExpansionCapacity];
    Signed32 AlphaBetaCount = 0;
    AccumulateProduct(AlphaBetaCross, AlphaBetaCount, AlphaSpanX, BetaSpanY,  1.0);
    AccumulateProduct(AlphaBetaCross, AlphaBetaCount, BetaSpanX,  AlphaSpanY, -1.0);

    Real64   AlphaLifted[SlateExpansionCapacity];
    Signed32 AlphaLiftedCount = 0;
    AccumulateProduct(AlphaLifted, AlphaLiftedCount, AlphaSpanX, AlphaSpanX, 1.0);
    AccumulateProduct(AlphaLifted, AlphaLiftedCount, AlphaSpanY, AlphaSpanY, 1.0);

    Real64   BetaLifted[SlateExpansionCapacity];
    Signed32 BetaLiftedCount = 0;
    AccumulateProduct(BetaLifted, BetaLiftedCount, BetaSpanX, BetaSpanX, 1.0);
    AccumulateProduct(BetaLifted, BetaLiftedCount, BetaSpanY, BetaSpanY, 1.0);

    Real64   GammaLifted[SlateExpansionCapacity];
    Signed32 GammaLiftedCount = 0;
    AccumulateProduct(GammaLifted, GammaLiftedCount, GammaSpanX, GammaSpanX, 1.0);
    AccumulateProduct(GammaLifted, GammaLiftedCount, GammaSpanY, GammaSpanY, 1.0);

    Real64   Arena[SlateIncircleCapacity];
    Signed32 ArenaCount = 0;

    AccumulateLifted(Arena, ArenaCount, AlphaLifted, AlphaLiftedCount, BetaGammaCross,  BetaGammaCount);
    AccumulateLifted(Arena, ArenaCount, BetaLifted,  BetaLiftedCount,  GammaAlphaCross, GammaAlphaCount);
    AccumulateLifted(Arena, ArenaCount, GammaLifted, GammaLiftedCount, AlphaBetaCross,  AlphaBetaCount);

    // 📝 Terms are held in increasing magnitude, so the last occupied term carries the sign of the whole.
    if (ArenaCount == 0)
    {
        return 0;
    }

    const Real64 Dominant = Arena[ArenaCount - 1];

    if (Dominant > 0.0)
    {
        return 1;
    }

    if (Dominant < 0.0)
    {
        return -1;
    }

    return 0;
}

/// 🧩 Classifies Delta against the circle through three positions of unknown winding.
/// out   Incircle  [-]  +1 inside, 0 cocircular, −1 outside — regardless of how the triangle winds
/// note  🔴 The convenience every consumer actually wants. `68` §4.1 tests folds and `52` classifies outlines
///        whose winding neither of them controls, so the winding is classified here and the sign corrected once
///        rather than at each of the call sites that would otherwise have to remember.
/// cost  🚩
/// note  Exact — both classifications are exact, and their product is an integer.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Signed32 ClassifyIncircleUnwound(Real64 AlphaX, Real64 AlphaY,
                                              Real64 BetaX,  Real64 BetaY,
                                              Real64 GammaX, Real64 GammaY,
                                              Real64 DeltaX, Real64 DeltaY)
{
    const Signed32 Winding = ClassifyOrientation(AlphaX, AlphaY, BetaX, BetaY, GammaX, GammaY);

    // 📝 A degenerate triangle describes no circle, so nothing is inside one. Reporting a containment against a
    //    circle that does not exist is worse than reporting none: `68` §4.1 would accept the fold it was testing.
    if (Winding == 0)
    {
        return 0;
    }

    return Winding * ClassifyIncircle(AlphaX, AlphaY, BetaX, BetaY, GammaX, GammaY, DeltaX, DeltaY);
}

}   // namespace Slate
