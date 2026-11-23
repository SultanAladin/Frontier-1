//============================================================================================================================================
//                                                      ORIENTATIONCLASSIFIER.SLANG.H
//============================================================================================================================================
// 🧩 Sign of a planar orientation determinant — filtered fast path over an exact expansion fallback.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 The predicate answers: walking Alpha → Beta → Gamma, does the path turn left, turn right, or stay
//    collinear. The quantity is the determinant
//
//        | Alphaₓ − Gammaₓ   Alpha_y − Gamma_y |
//        | Betaₓ  − Gammaₓ   Beta_y  − Gamma_y |
//
//    and only its **sign** is guaranteeed. A predicate that is usually exact provides no topological
//    guarantee at all, so the filtered path is taken only where its own error bound excludes zero.

// 📝 An expansion is a sequence of non-overlapping terms held in increasing magnitude whose exact sum is
//    the quantity represented. Eight exact products contribute two terms each; the accumulation never
//    exceeds sixteen occupied terms and the declared capacity carries margin over that.
#define SlateExpansionCapacity 40

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                             EXACT ARITHMETIC PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every routine below is exact in the 64-bit representation. They rely on round-to-nearest and on the
//    absence of guaranteeion, which is why /fp:precise is declared in the build and never relaxed.

/// 🧩 Splits one operand into two halves whose product with another split operand is exactly representable.
/// in    Operand    [-]  the quantity to split
/// out   HighHalf   [-]  the leading half
/// out   LowHalf    [-]  the trailing half; Operand = HighHalf + LowHalf exactly
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void SplitExactly(Real64 Operand, SLATE_OUT(Real64) HighHalf, SLATE_OUT(Real64) LowHalf)
{
    const Real64 Scaled = ExpansionSplitter * Operand;
    const Real64 Gap    = Scaled - Operand;
    HighHalf            = Scaled - Gap;
    LowHalf             = Operand - HighHalf;
}

/// 🧩 Exact sum of two operands as a leading term and the rounding residue it discarded.
/// in    LeftTerm   [-]  first operand
/// in    RightTerm  [-]  second operand
/// out   Leading    [-]  the rounded sum
/// out   Residue    [-]  LeftTerm + RightTerm − Leading, exactly
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void SumExactly(Real64 LeftTerm, Real64 RightTerm,
                             SLATE_OUT(Real64) Leading, SLATE_OUT(Real64) Residue)
{
    Leading                = LeftTerm + RightTerm;
    const Real64 RightPart = Leading - LeftTerm;
    const Real64 LeftPart  = Leading - RightPart;
    Residue                = (LeftTerm - LeftPart) + (RightTerm - RightPart);
}

/// 🧩 Exact difference of two operands as a leading term and the rounding residue it discarded.
/// in    Minuend    [-]  the operand subtracted from
/// in    Subtrahend [-]  the operand subtracted
/// out   Leading    [-]  the rounded difference
/// out   Residue    [-]  Minuend − Subtrahend − Leading, exactly
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void DifferenceExactly(Real64 Minuend, Real64 Subtrahend,
                                    SLATE_OUT(Real64) Leading, SLATE_OUT(Real64) Residue)
{
    Leading                = Minuend - Subtrahend;
    const Real64 RightPart = Minuend - Leading;
    const Real64 LeftPart  = Leading + RightPart;
    Residue                = (Minuend - LeftPart) + (RightPart - Subtrahend);
}

/// 🧩 Exact product of two operands as a leading term and the rounding residue it discarded.
/// in    LeftTerm   [-]  first operand
/// in    RightTerm  [-]  second operand
/// out   Leading    [-]  the rounded product
/// out   Residue    [-]  LeftTerm × RightTerm − Leading, exactly
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void ProductExactly(Real64 LeftTerm, Real64 RightTerm,
                                 SLATE_OUT(Real64) Leading, SLATE_OUT(Real64) Residue)
{
    Leading = LeftTerm * RightTerm;

    Real64 LeftHigh  = 0.0;
    Real64 LeftLow   = 0.0;
    Real64 RightHigh = 0.0;
    Real64 RightLow  = 0.0;

    SplitExactly(LeftTerm,  LeftHigh,  LeftLow);
    SplitExactly(RightTerm, RightHigh, RightLow);

    const Real64 FirstResidue  = Leading       - (LeftHigh * RightHigh);
    const Real64 SecondResidue = FirstResidue  - (LeftLow  * RightHigh);
    const Real64 ThirdResidue  = SecondResidue - (LeftHigh * RightLow);
    Residue                    = (LeftLow * RightLow) - ThirdResidue;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                EXPANSION ACCUMULATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Accumulates one term into an expansion, exactly, preserving the increasing-magnitude ordering.
/// in    Expansion  [-]  the terms accumulated so far, in increasing magnitude
/// in    TermCount  [-]  how many of them are occupied
/// in    Incoming   [-]  the term to accumulate
/// out   TermCount  [-]  the occupied count after accumulation
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void AccumulateExactly(SLATE_INOUT_SPAN(Real64, Expansion, SlateExpansionCapacity),
                                    SLATE_INOUT(Signed32) TermCount,
                                    Real64                Incoming)
{
    Real64   Carried  = Incoming;
    Signed32 Occupied = 0;

    for (Signed32 TermIndex = 0; TermIndex < TermCount; ++TermIndex)
    {
        Real64 Leading = 0.0;
        Real64 Residue = 0.0;
        SumExactly(Expansion[TermIndex], Carried, Leading, Residue);
        Carried = Leading;

        if (Residue != 0.0)
        {
            Expansion[Occupied] = Residue;
            ++Occupied;
        }
    }

    if (Carried != 0.0)
    {
        Expansion[Occupied] = Carried;
        ++Occupied;
    }

    TermCount = Occupied;
}

/// 🧩 Accumulates both terms of an exact product into an expansion, scaled by a declared sign.
/// in    LeftTerm   [-]  first factor
/// in    RightTerm  [-]  second factor
/// in    Signum     [-]  +1 to add the product, −1 to subtract it
/// cost  ✔️
/// tag   shared, nonallocating, nonthrowing
SLATE_SHARED void AccumulateProduct(SLATE_INOUT_SPAN(Real64, Expansion, SlateExpansionCapacity),
                                    SLATE_INOUT(Signed32) TermCount,
                                    Real64                LeftTerm,
                                    Real64                RightTerm,
                                    Real64                Signum)
{
    Real64 Leading = 0.0;
    Real64 Residue = 0.0;
    ProductExactly(LeftTerm, RightTerm, Leading, Residue);

    AccumulateExactly(Expansion, TermCount, Signum * Residue);
    AccumulateExactly(Expansion, TermCount, Signum * Leading);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PREDICATE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Classifies the turn direction of Alpha → Beta → Gamma. Positive is counter-clockwise.
/// in    AlphaX       [-]  first position, abscissa
/// in    AlphaY       [-]  first position, coordinate
/// in    BetaX        [-]  second position, abscissa
/// in    BetaY        [-]  second position, coordinate
/// in    GammaX       [-]  third position, abscissa
/// in    GammaY       [-]  third position, coordinate
/// out   Orientation  [-]  +1 counter-clockwise, −1 clockwise, 0 exactly collinear
/// err   never refuses; the sign is total over every finite input
/// cost  🚩
/// note  ✔️ on the filtered path; the 🚩 is the expansion, reached only when the filter cannot exclude zero.
/// note  Exact — the sign agrees bit for bit between the host form and the device form.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Signed32 ClassifyOrientation(Real64 AlphaX, Real64 AlphaY,
                                          Real64 BetaX,  Real64 BetaY,
                                          Real64 GammaX, Real64 GammaY)
{
    const Real64 LeftSpan  = (AlphaX - GammaX) * (BetaY - GammaY);
    const Real64 RightSpan = (AlphaY - GammaY) * (BetaX - GammaX);
    const Real64 Filtered  = LeftSpan - RightSpan;

    const Real64 Summed    = Magnitude(LeftSpan) + Magnitude(RightSpan);
    const Real64 ErrorEdge = OrientationErrorFactor * Summed;

    // 📝 The filtered sign is guaranteeed only where the determinant exceeds its own error bound. Every
    //    other input falls through to the expansion, including every exactly collinear one.
    if (Filtered > ErrorEdge)
    {
        return 1;
    }

    if (-Filtered > ErrorEdge)
    {
        return -1;
    }

    Real64 AlphaSpanX    = 0.0;   // [-] - Alphaₓ − Gammaₓ, exactly, as leading term plus residue
    Real64 AlphaResidueX = 0.0;
    Real64 AlphaSpanY    = 0.0;
    Real64 AlphaResidueY = 0.0;
    Real64 BetaSpanX     = 0.0;
    Real64 BetaResidueX  = 0.0;
    Real64 BetaSpanY     = 0.0;
    Real64 BetaResidueY  = 0.0;

    DifferenceExactly(AlphaX, GammaX, AlphaSpanX, AlphaResidueX);
    DifferenceExactly(AlphaY, GammaY, AlphaSpanY, AlphaResidueY);
    DifferenceExactly(BetaX,  GammaX, BetaSpanX,  BetaResidueX);
    DifferenceExactly(BetaY,  GammaY, BetaSpanY,  BetaResidueY);

    Real64   Expansion[SlateExpansionCapacity];
    Signed32 TermCount = 0;

    AccumulateProduct(Expansion, TermCount, AlphaSpanX,    BetaSpanY,     1.0);
    AccumulateProduct(Expansion, TermCount, AlphaSpanX,    BetaResidueY,  1.0);
    AccumulateProduct(Expansion, TermCount, AlphaResidueX, BetaSpanY,     1.0);
    AccumulateProduct(Expansion, TermCount, AlphaResidueX, BetaResidueY,  1.0);

    AccumulateProduct(Expansion, TermCount, AlphaSpanY,    BetaSpanX,    -1.0);
    AccumulateProduct(Expansion, TermCount, AlphaSpanY,    BetaResidueX, -1.0);
    AccumulateProduct(Expansion, TermCount, AlphaResidueY, BetaSpanX,    -1.0);
    AccumulateProduct(Expansion, TermCount, AlphaResidueY, BetaResidueX, -1.0);

    // 📝 Terms are held in increasing magnitude, so the last occupied term carries the sign of the whole.
    if (TermCount == 0)
    {
        return 0;
    }

    const Real64 Dominant = Expansion[TermCount - 1];

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

}   // namespace Slate
