//============================================================================================================================================
//                                                        SAMPLEPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 Deterministic sample placement — the one sequence `18`, `28`, `30`, `60`, `46`, `64` and `82` all read.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 Every sample in the engine is placed by projecting a **sample ordinal** into a domain, and never by drawing
//    from a source of randomness. `02` §6 requires the device and the host to place samples identically, and
//    `54` §1 states the same rule one layer up for a different reason: a sampled source is not reproducible, so
//    the same document reopens looking different and a tile re-resolved at a finer level disagrees with the
//    coarse one it replaced.
//
// 🔴 The projection is in two halves and they carry **different guarantees**, which is stated here rather than
//    discovered by whoever first measures a disagreement. The ordinal-to-unit-square half is Exact: it is a bit
//    reversal and an integer ratio, and both toolchains produce the same bits. The unit-square-to-direction half
//    is Bounded: it reads a square root and a pair of circular functions, and no two toolchains agree on those
//    to the last place. A file that claimed one guarantee for both would be claiming the weaker one is stronger.

// 📝 Twenty digits of base three cover every ordinal below 3²⁰, and 3²⁰ is exactly representable, so the ratio
//    below is one correctly-rounded division rather than an accumulation of twenty of them.
#define SlateRadicalDigitLimit 20

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE UNIT SQUARE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The radical inverse of one ordinal in base two.
/// in    Index  [-]  the sample ordinal
/// out   Fraction [-]  in the half-open unit interval
/// note  📐 Reversing the bits of the ordinal and scaling by a power of two is exact in binary: no bit is lost
///        and the scale is a change of exponent alone. This is the strongest determinism available anywhere in
///        the engine, and it is why the first coordinate of every sample is the base-two one.
/// cost  ✔️
/// note  Exact — bit for bit between the host form and the device form.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectRadicalTwo(Unsigned32 Index)
{
    const Real64 ReciprocalSpan = 2.3283064365386963e-10;   // [-] - 2⁻³², exact

    return Real64(ReversedBits(Index)) * ReciprocalSpan;
}

/// 🧩 The radical inverse of one ordinal in base three.
/// in    Index  [-]  the sample ordinal
/// out   Fraction [-]  in the half-open unit interval
/// note  📐 The digits are accumulated as an exact integer numerator over an exact integer denominator, and the
///        single division at the end is correctly rounded. Accumulating a fraction digit by digit instead would
///        round twenty times, and the twenty roundings differ between two toolchains that reassociate.
/// note  ⚠️ An ordinal at or above 3²⁰ has its leading digits truncated, which are the least significant of the
///        inverse. The result stays in the unit interval and stays deterministic; it merely repeats sooner.
/// cost  🚩
/// note  Exact — bit for bit between the host form and the device form.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectRadicalThree(Unsigned32 Index)
{
    Unsigned64 Numerator   = 0;
    Unsigned64 Denominator = 1;
    Unsigned64 Remaining   = Unsigned64(Index);

    for (Signed32 DigitIndex = 0; DigitIndex < SlateRadicalDigitLimit; ++DigitIndex)
    {
        if (Remaining == 0)
        {
            break;
        }

        const Unsigned64 Digit = Remaining % 3;
        Remaining              = Remaining / 3;
        Numerator              = Numerator * 3 + Digit;
        Denominator            = Denominator * 3;
    }

    return Real64(Numerator) / Real64(Denominator);
}

/// 🧩 Projects one sample ordinal onto the unit square, progressively.
/// in    Index           [-]  the sample ordinal
/// out   FirstCoordinate   [-]  in the half-open unit interval
/// out   SecondCoordinate  [-]  in the half-open unit interval
/// note  🔴 Progressive — `02` §6 requires it and `60` §3.2 gives the reason from the consuming side: `64`
///        accumulates across rotations, so a pattern that is uniform only at a fixed count converges to a
///        different value than the one `64` assumes it is averaging.
/// cost  🚩
/// note  Exact — both coordinates are exact, so the pattern is identical everywhere.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectPlanarSample(Unsigned32 Index,
                                      SLATE_OUT(Real64) FirstCoordinate,
                                      SLATE_OUT(Real64) SecondCoordinate)
{
    FirstCoordinate  = ProjectRadicalTwo(Index);
    SecondCoordinate = ProjectRadicalThree(Index);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE VARIATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Permutes one ordinal against a declared seed.
/// in    Index  [-]  the cell ordinal of `54` §1, or the impression ordinal of `58` §6
/// in    Seed     [-]  the pattern seed or the stroke seed; stored with the declaration, never drawn
/// out   Permuted [-]  the ordinal's image under the permutation the seed selects
/// note  🔴 A **permutation**, never a sample — `00` §5 declares continuous stochastic sources absent and this is
///        the substitution both `54` §1 and `58` §6 name. The distinction is the whole point: a permutation of an
///        ordinal reopens identically, agrees between a coarse reduction level and the finer one that replaces
///        it, and agrees between `82`'s host preview and the device resolution. Sampled noise satisfies none of
///        those, and each failure reaches the artist as the pattern changing when nothing was edited.
/// note  📐 Every step is a bijection on the 32-bit ordinals — a wrapping product by an odd factor, and a shift
///        exclusive-or whose shift is under half the width. A composition of bijections is a bijection, so the
///        sequence visits every ordinal once before it repeats and no ordinal is favoured.
/// cost  ✔️
/// note  Exact — wrapping integer arithmetic alone; bit for bit between the host form and the device form.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Unsigned32 ProjectPermutedIndex(Unsigned32 Index, Unsigned32 Seed)
{
    Unsigned32 Permuted = Index + Seed * 0x9E3779B9u;

    Permuted = (Permuted ^ (Permuted >> 16)) * 0x21F0AAADu;
    Permuted = (Permuted ^ (Permuted >> 15)) * 0x735A2D97u;

    return Permuted ^ (Permuted >> 15);
}

/// 🧩 Projects one ordinal onto the unit interval, through the permutation its seed selects.
/// in    Index   [-]  the cell ordinal, or the impression ordinal
/// in    Seed      [-]  the pattern seed or the stroke seed
/// out   Fraction  [-]  in the half-open unit interval
/// note  🔴 `58` §6 records the stroke seed with the transaction for the reason `54` §1 stores the pattern seed
///        with the declaration. A stroke varying from an unrecorded source resolves differently every time it is
///        re-resolved, so an undo would produce the inverse of a stroke the artist never made and `20`'s
///        reconstruction of an evicted tile would disagree with what was on screen.
/// note  📝 The permuted ordinal is scaled by 2⁻³² rather than reduced modulo a span. A modulus that does not
///        divide the ordinal range favours its low residues, and the bias is a pattern leaning one way that no
///        declaration asked for.
/// cost  ✔️
/// note  Exact — the permutation is integral and the scale is a change of exponent alone.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectVariation(Unsigned32 Index, Unsigned32 Seed)
{
    const Real64 ReciprocalSpan = 2.3283064365386963e-10;   // [-] - 2⁻³², exact

    return Real64(ProjectPermutedIndex(Index, Seed)) * ReciprocalSpan;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SUB-PIXEL OFFSETS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The sub-pixel offset one cycle slot carries.
/// in    RecordingIndex  [-]  the rotation, counted from bring-up
/// out   OffsetX          [-]  in the half-open interval about zero, in pixels
/// out   OffsetY          [-]  in the half-open interval about zero, in pixels
/// note  🔴 `64` §3.1 applies this to `46`'s **projection** and never to a resolved position. An offset applied
///        after resolution shifts an already-resolved image, which resamples rather than samples, and the
///        result is blur where convergence was wanted.
/// note  📝 The sequence begins at ordinal one rather than zero, because the radical inverse of zero is zero and
///        would place the first rotation of every run at a pixel corner rather than within the pixel.
/// note  🔴 `82` replays this same sequence for a preview and `46` applies it on the host, so it is registered
///        with `ParityRunner` like any other shared entry point — `64` §8's gate.
/// cost  🚩
/// note  Exact — the offsets are exact, so a preview converges to the workspace's image and not beside it.
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectSubPixelOffset(Unsigned32 RecordingIndex,
                                        SLATE_OUT(Real64) OffsetX,
                                        SLATE_OUT(Real64) OffsetY)
{
    const Unsigned32 Index = (RecordingIndex % Unsigned32(SubPixelSequenceLength)) + 1u;

    Real64 FirstCoordinate  = 0.0;
    Real64 SecondCoordinate = 0.0;
    ProjectPlanarSample(Index, FirstCoordinate, SecondCoordinate);

    OffsetX = FirstCoordinate  - 0.5;
    OffsetY = SecondCoordinate - 0.5;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIRECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Projects a unit-square sample onto the sphere, uniformly in solid angle.
/// in    FirstCoordinate   [-]  in the unit interval
/// in    SecondCoordinate  [-]  in the unit interval
/// out   DirectionX        [-]  unit length
/// out   DirectionY        [-]  unit length
/// out   DirectionZ        [-]  unit length
/// note  📐 The zenith is distributed uniformly in its cosine rather than in its angle, which is what makes the
///        projection uniform in solid angle. Distributing the angle uniformly clusters samples at the poles, and
///        `28` §2 integrates its multiple-scattering surface over exactly this sphere.
/// note  🔴 Bounded, not Exact. A square root and a pair of circular functions do not agree to the last place
///        between two toolchains, so `ParityRunner` compares this against a declared bound in units in the last
///        place rather than for equality.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectSphericalSample(Real64 FirstCoordinate,
                                         Real64 SecondCoordinate,
                                         SLATE_OUT(Real64) DirectionX,
                                         SLATE_OUT(Real64) DirectionY,
                                         SLATE_OUT(Real64) DirectionZ)
{
    const Real64 CosineZenith  = 1.0 - 2.0 * FirstCoordinate;
    const Real64 SquaredRadius = 1.0 - CosineZenith * CosineZenith;
    const Real64 SineZenith    = SquareRoot(SquaredRadius < 0.0 ? 0.0 : SquaredRadius);
    const Real64 Azimuth       = 2.0 * Pi * SecondCoordinate;

    DirectionX = SineZenith * Cosine(Azimuth);
    DirectionY = SineZenith * Sine(Azimuth);
    DirectionZ = CosineZenith;
}

/// 🧩 Projects a unit-square sample onto the hemisphere, weighted to the cosine lobe.
/// out   DirectionZ  [-]  never negative; the hemisphere is the one about the positive third axis
/// note  📐 The radius is the square root of the first coordinate, which is the concentric-disc projection whose
///        lift to the hemisphere is cosine-weighted by construction. Weighting after the fact by multiplying an
///        evenly distributed direction by its own cosine is the same integral with more variance, and `60` §5
///        accumulates it over a target that is already half extent.
/// note  🔴 Bounded, for the reason the spherical projection is.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectHemisphericalSample(Real64 FirstCoordinate,
                                             Real64 SecondCoordinate,
                                             SLATE_OUT(Real64) DirectionX,
                                             SLATE_OUT(Real64) DirectionY,
                                             SLATE_OUT(Real64) DirectionZ)
{
    const Real64 Radius   = SquareRoot(FirstCoordinate < 0.0 ? 0.0 : FirstCoordinate);
    const Real64 Azimuth  = 2.0 * Pi * SecondCoordinate;
    const Real64 Vertical = 1.0 - FirstCoordinate;

    DirectionX = Radius * Cosine(Azimuth);
    DirectionY = Radius * Sine(Azimuth);
    DirectionZ = SquareRoot(Vertical < 0.0 ? 0.0 : Vertical);
}

}   // namespace Slate
