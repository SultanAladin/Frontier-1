//============================================================================================================================================
//                                                             PARITYRUNNER.CPP
//============================================================================================================================================
// 🧩 Registration and comparison over the common sample set.

#include "SlateCompute/Compute/ParityRunner/Api/ParityRunner.h"

#include "Foundation/NumericTolerance.h"
#include "Shared/AtmosphereProjection.slang.h"
#include "Shared/ContainmentClassifier.slang.h"
#include "Shared/IncircleClassifier.slang.h"
#include "Shared/IntersectionClassifier.slang.h"
#include "Shared/LatticeProjection.slang.h"
#include "Shared/OrientationClassifier.slang.h"
#include "Shared/AccumulationProjection.slang.h"
#include "Shared/ReflectionProjection.slang.h"
#include "Shared/ToneProjection.slang.h"
#include "Shared/TransmissionProjection.slang.h"
#include "Shared/SampleProjection.slang.h"

#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ParityRunner::Register(const ParityRegistration& Incoming)
{
    for (const ParityRegistration& Held : Registered)
    {
        if (std::strcmp(Held.EntryName, Incoming.EntryName) == 0)
            return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the entry point is already registered" });
    }

    Registered.push_back(Incoming);
    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE COMMON SAMPLE SET
//------------------------------------------------------------------------------------------------------------------------

// 📝 The sample set deliberately concentrates on the inputs where the filtered path cannot decide: nearly
//    collinear triples, exactly collinear triples, and triples separated by many orders of magnitude. A
//    sample set of well-conditioned inputs proves only that the fast path works.
namespace
{
    struct OrientationSample
    {
        double  AlphaX = 0.0;   // [-] - first position
        double  AlphaY = 0.0;
        double  BetaX  = 0.0;   // [-] - second position
        double  BetaY  = 0.0;
        double  GammaX = 0.0;   // [-] - third position
        double  GammaY = 0.0;
    };

    constexpr OrientationSample OrientationSampleSet[] =
    {
        { 0.0,     0.0,     1.0,     0.0,     0.0,     1.0     },   // well conditioned, counter-clockwise
        { 0.0,     0.0,     0.0,     1.0,     1.0,     0.0     },   // well conditioned, clockwise
        { 0.0,     0.0,     1.0,     1.0,     2.0,     2.0     },   // exactly collinear
        { 0.5,     0.5,     12.0,    12.0,    24.0,    24.0    },   // exactly collinear, off origin
        { 0.0,     0.0,     1.0e-15, 1.0e-15, 2.0e-15, 2.0e-15 },   // collinear at the representable floor
        { 0.0,     0.0,     1.0,     1.0,     2.0,     2.0000000000000004 },  // one ULP off collinear
        { 1.0e18,  1.0e18,  1.0e18,  1.0e18,  1.0e18,  1.0e18  },   // degenerate — all three coincide
        { 1.0e-30, 1.0e-30, 1.0e30,  1.0e30,  1.0,     1.0     }    // spanning sixty orders of magnitude
    };

    // 📝 Four positions each. The set concentrates on inputs the incircle filter cannot decide: exactly
    //    cocircular quadrilaterals, a position one ULP off the circle, and a triangle whose winding is
    //    reversed so that the sign correction is exercised rather than assumed.
    struct IncircleSample
    {
        double  AlphaX = 0.0;   // [-] - the triangle
        double  AlphaY = 0.0;
        double  BetaX  = 0.0;
        double  BetaY  = 0.0;
        double  GammaX = 0.0;
        double  GammaY = 0.0;
        double  DeltaX = 0.0;   // [-] - the position classified against its circle
        double  DeltaY = 0.0;
    };

    constexpr IncircleSample IncircleSampleSet[] =
    {
        { 0.0, 0.0,  1.0, 0.0,  0.0, 1.0,   0.4,  0.4  },   // well inside
        { 0.0, 0.0,  1.0, 0.0,  0.0, 1.0,   2.0,  2.0  },   // well outside
        { 0.0, 0.0,  1.0, 0.0,  0.0, 1.0,   1.0,  1.0  },   // exactly cocircular
        { 0.0, 0.0,  0.0, 1.0,  1.0, 0.0,   0.4,  0.4  },   // the same, wound the other way
        { 0.0, 0.0,  1.0, 0.0,  0.0, 1.0,   1.0,  0.9999999999999999 },  // one ULP inside
        { 0.0, 0.0,  1.0e8, 0.0,  0.0, 1.0e8,  1.0e8, 1.0e8 },           // cocircular at scale
        { 0.0, 0.0,  1.0, 1.0,  2.0, 2.0,   3.0,  3.0  }    // degenerate triangle, no circle
    };

    struct SegmentSample
    {
        double  AlphaX = 0.0;   // [-] - the first segment
        double  AlphaY = 0.0;
        double  BetaX  = 0.0;
        double  BetaY  = 0.0;
        double  GammaX = 0.0;   // [-] - the second
        double  GammaY = 0.0;
        double  DeltaX = 0.0;
        double  DeltaY = 0.0;
    };

    constexpr SegmentSample SegmentSampleSet[] =
    {
        { 0.0, 0.0,  1.0, 1.0,   0.0, 1.0,  1.0, 0.0 },   // proper crossing
        { 0.0, 0.0,  1.0, 0.0,   2.0, 0.0,  3.0, 0.0 },   // collinear, disjoint
        { 0.0, 0.0,  2.0, 0.0,   1.0, 0.0,  3.0, 0.0 },   // collinear, overlapping
        { 0.0, 0.0,  1.0, 0.0,   1.0, 0.0,  2.0, 0.0 },   // collinear, meeting at one position
        { 0.0, 0.0,  1.0, 0.0,   0.5, 0.0,  0.5, 1.0 },   // an endpoint on the other segment
        { 0.0, 0.0,  1.0, 1.0,   2.0, 0.0,  3.0, 1.0 },   // parallel, disjoint
        { 0.0, 0.0,  0.0, 0.0,   0.0, 0.0,  1.0, 0.0 },   // degenerate against a segment through it
        { 0.0, 1.0,  0.0, 3.0,   0.0, 2.0,  0.0, 4.0 }    // collinear and vertical — the axis choice
    };

    struct IntervalSample
    {
        std::uint64_t  OuterBegin = 0u;   // [-] - the candidate enclosing interval
        std::uint64_t  OuterEnd   = 0u;
        std::uint64_t  InnerBegin = 0u;   // [-] - the candidate enclosed interval
        std::uint64_t  InnerEnd   = 0u;
    };

    constexpr IntervalSample IntervalSampleSet[] =
    {
        {   0u, 100u,  10u,  20u },   // strictly contained
        {   0u, 100u,   0u, 100u },   // identical
        {   0u, 100u,   0u,  50u },   // sharing the lower bound
        {   0u, 100u,  50u, 100u },   // sharing the upper bound
        {   0u,  50u,  40u,  60u },   // overlapping, contained by neither
        {   0u,  10u,  20u,  30u },   // disjoint
        { 100u,   0u,  10u,  20u }    // inverted, therefore no interval at all
    };

    // 📝 One validated lattice each. The set concentrates on the declarations `54` §2 permits and on the cells
    //    either side of the origin, because flooring below the origin is where a truncating classification
    //    first departs from a flooring one and the departure is one cell wide.
    struct LatticeSample
    {
        double       PositionX           = 0.0;   // [-] - the domain position classified
        double       PositionY          = 0.0;
        double       CellExtentX         = 1.0;   // [-] - the repeating unit, strictly positive
        double       CellExtentY        = 1.0;
        double       OffsetProgressionX  = 0.0;   // [-] - never declared beside the one across
        double       OffsetProgressionY = 0.0;
        double       SkewX               = 0.0;   // [-] - the shear a diagonal repeat declares
        double       SkewY              = 0.0;
        std::uint32_t  ReflectionMask        = 0u;    // [-] - SlateReflectX and SlateReflectY, composed
        std::uint32_t  RotationIncrement     = 0u;    // [-] - quarter turns per step of the cell schedule
    };

    constexpr LatticeSample LatticeSampleSet[] =
    {
        { 0.25,  0.25,  1.0, 1.0,  0.0,  0.0,  0.0, 0.0,  0u,  0u },   // the first cell, nothing declared
        { 0.0,   0.0,   1.0, 1.0,  0.0,  0.0,  0.0, 0.0,  0u,  0u },   // exactly on the origin's boundary
        { -0.25, -0.75, 1.0, 1.0,  0.0,  0.0,  0.0, 0.0,  0u,  0u },   // below the origin — flooring, not truncation
        { -1.0,  -1.0,  1.0, 1.0,  0.0,  0.0,  0.0, 0.0,  0u,  0u },   // exactly on a boundary below the origin
        { 3.5,   7.25,  2.0, 4.0,  0.0,  0.0,  0.0, 0.0,  1u,  0u },   // unequal extents, mirrored along
        { 5.5,   2.5,   1.0, 1.0,  0.5,  0.0,  0.0, 0.0,  0u,  0u },   // a row displacement, resolved across first
        { 5.5,   2.5,   1.0, 1.0,  0.0,  0.5,  0.0, 0.0,  0u,  0u },   // a column displacement, the other order
        { -2.5,  4.5,   1.0, 1.0,  0.0,  0.0,  0.0, 0.0,  3u,  1u },   // both reflections and a quarter turn
        { 1.75,  -3.25, 1.0, 1.0,  0.0,  0.0,  0.2, 0.4,  2u,  3u },   // sheared, mirrored across, three turns
        { 1.0e6,  1.0e6, 1.0, 1.0, 0.0,  0.0,  0.0, 0.0,  1u,  2u }    // far from the origin, where the ordinal is large
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      COMPARISON
//------------------------------------------------------------------------------------------------------------------------

const std::vector<ParityReport>& ParityRunner::Compare()
{
    Reported.clear();
    Reported.reserve(Registered.size());

    AgreementDeclared = true;

    for (const ParityRegistration& Held : Registered)
    {
        ParityReport Report;
        Report.EntryName = Held.EntryName;

        if (Held.Reserved == PrecisionGuarantee::Perceptual)
        {
            // 📝 Perceptual entry points are not compared. Reporting agreement for one would claim a
            //    guarantee the guarantee itself disclaims.
            Report.AgreementHeld = true;
            Reported.push_back(Report);
            continue;
        }

        if (std::strcmp(Held.EntryName, "ClassifyOrientation") == 0)
        {
            const std::uint32_t SampleSpan =
                static_cast<std::uint32_t>(sizeof(OrientationSampleSet) / sizeof(OrientationSampleSet[0]));

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                const OrientationSample& Sample = OrientationSampleSet[Index];

                const Signed32 Classified = ClassifyOrientation(Sample.AlphaX, Sample.AlphaY,
                                                                Sample.BetaX,  Sample.BetaY,
                                                                Sample.GammaX, Sample.GammaY);

                // 📐 Reversing two operands negates an orientation determinant exactly. The exact path must
                //    reproduce that antisymmetry for every input, and the filtered path must not break it
                //    where it decides — which is the strongest host-side statement available before the
                //    shader-side comparison exists.
                const Signed32 Reversed = ClassifyOrientation(Sample.BetaX,  Sample.BetaY,
                                                              Sample.AlphaX, Sample.AlphaY,
                                                              Sample.GammaX, Sample.GammaY);

                if (Classified != -Reversed)
                    ++Report.DisagreeingCount;
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ClassifyIncircle") == 0)
        {
            const std::uint32_t SampleSpan =
                static_cast<std::uint32_t>(sizeof(IncircleSampleSet) / sizeof(IncircleSampleSet[0]));

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                const IncircleSample& Sample = IncircleSampleSet[Index];

                const Signed32 Classified = ClassifyIncircle(Sample.AlphaX, Sample.AlphaY,
                                                             Sample.BetaX,  Sample.BetaY,
                                                             Sample.GammaX, Sample.GammaY,
                                                             Sample.DeltaX, Sample.DeltaY);

                // 📐 Exchanging two positions of the triangle reverses its winding, and the incircle
                //    determinant negates exactly with it. The exact path must reproduce that antisymmetry for
                //    every input, and the filtered path must not break it where it decides.
                const Signed32 Exchanged = ClassifyIncircle(Sample.AlphaX, Sample.AlphaY,
                                                            Sample.GammaX, Sample.GammaY,
                                                            Sample.BetaX,  Sample.BetaY,
                                                            Sample.DeltaX, Sample.DeltaY);

                if (Classified != -Exchanged)
                    ++Report.DisagreeingCount;

                // 📐 A position of the triangle is cocircular with the triangle, exactly and by definition.
                //    This is the one identity that holds for every input including the degenerate ones.
                if (ClassifyIncircle(Sample.AlphaX, Sample.AlphaY,
                                     Sample.BetaX,  Sample.BetaY,
                                     Sample.GammaX, Sample.GammaY,
                                     Sample.BetaX,  Sample.BetaY) != 0)
                {
                    ++Report.DisagreeingCount;
                }
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ClassifySegmentIntersection") == 0)
        {
            const std::uint32_t SampleSpan =
                static_cast<std::uint32_t>(sizeof(SegmentSampleSet) / sizeof(SegmentSampleSet[0]));

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                const SegmentSample& Sample = SegmentSampleSet[Index];

                const Signed32 Classified = ClassifySegmentIntersection(Sample.AlphaX, Sample.AlphaY,
                                                                        Sample.BetaX,  Sample.BetaY,
                                                                        Sample.GammaX, Sample.GammaY,
                                                                        Sample.DeltaX, Sample.DeltaY);

                // 📐 The relation between two segments does not depend on which was named first, nor on which
                //    end of a segment was named first. Both symmetries are checked, because the algorithm
                //    tests the two segments asymmetrically and a lapse shows up only under one of them.
                const Signed32 Exchanged = ClassifySegmentIntersection(Sample.GammaX, Sample.GammaY,
                                                                       Sample.DeltaX, Sample.DeltaY,
                                                                       Sample.AlphaX, Sample.AlphaY,
                                                                       Sample.BetaX,  Sample.BetaY);

                const Signed32 Reversed = ClassifySegmentIntersection(Sample.BetaX,  Sample.BetaY,
                                                                      Sample.AlphaX, Sample.AlphaY,
                                                                      Sample.GammaX, Sample.GammaY,
                                                                      Sample.DeltaX, Sample.DeltaY);

                if (Classified != Exchanged || Classified != Reversed)
                    ++Report.DisagreeingCount;
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ClassifyIntervalContainment") == 0)
        {
            const std::uint32_t SampleSpan =
                static_cast<std::uint32_t>(sizeof(IntervalSampleSet) / sizeof(IntervalSampleSet[0]));

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                const IntervalSample& Sample = IntervalSampleSet[Index];

                const Signed32 Classified = ClassifyIntervalContainment(Sample.OuterBegin, Sample.OuterEnd,
                                                                        Sample.InnerBegin, Sample.InnerEnd);

                const Signed32 Exchanged = ClassifyIntervalContainment(Sample.InnerBegin, Sample.InnerEnd,
                                                                       Sample.OuterBegin, Sample.OuterEnd);

                // 📐 Strict containment is antisymmetric and identity is symmetric, so the two classifications
                //    agree only where both are zero. Anything else would let two owners each enclose the
                //    other, which is `12` invariant 3 broken by the predicate rather than by the relation.
                if (Classified > 0 && Exchanged > 0)
                    ++Report.DisagreeingCount;

                if ((Classified == 0) != (Exchanged == 0))
                    ++Report.DisagreeingCount;

                // 📐 An interval never strictly contains itself, and every consumer relies on it so that an
                //    owner tested against itself answers false without an exclusion at the call site.
                if (ClassifyIntervalContainment(Sample.OuterBegin, Sample.OuterEnd,
                                                Sample.OuterBegin, Sample.OuterEnd) > 0)
                {
                    ++Report.DisagreeingCount;
                }
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ClassifyLatticeCell") == 0)
        {
            // 🔴 `02` §5 places `LatticeProjection` at Tier A, so it is compared here rather than trusted:
            //    `82` classifies a position on the host and `70` classifies it on the device, and a cell
            //    boundary the two disagree about is a pattern that does not meet itself across a tile edge.
            const std::uint32_t SampleSpan =
                static_cast<std::uint32_t>(sizeof(LatticeSampleSet) / sizeof(LatticeSampleSet[0]));

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                const LatticeSample& Sample = LatticeSampleSet[Index];

                Signed32 CellX    = 0;
                Signed32 CellY   = 0;
                Real64   WithinX  = 0.0;
                Real64   WithinY = 0.0;

                ClassifyLatticeCell(Sample.PositionX,           Sample.PositionY,
                                    Sample.CellExtentX,         Sample.CellExtentY,
                                    Sample.OffsetProgressionX,  Sample.OffsetProgressionY,
                                    Sample.SkewX,               Sample.SkewY,
                                    CellX, CellY, WithinX, WithinY);

                // 📐 The position within a cell lies in the half-open unit interval, which is what makes the
                //    cell ordinals a partition of the domain rather than a covering of it. A position landing
                //    at one belongs to the next cell and has been classified into the wrong one.
                if (WithinX < 0.0 || WithinX >= 1.0 || WithinY < 0.0 || WithinY >= 1.0)
                    ++Report.DisagreeingCount;

                // 📐 The ordinal and the position within recompose the coordinate they were classified from,
                //    exactly. The fractional part of a double is itself representable, so the subtraction that
                //    produced the position within lost nothing and the sum must return the coordinate.
                //    Compared only where no progression is declared, because a declared progression classifies
                //    the displaced coordinate rather than the supplied one.
                if (Sample.OffsetProgressionX == 0.0 && Sample.OffsetProgressionY == 0.0)
                {
                    const Real64 Determinant    = 1.0 - Sample.SkewX * Sample.SkewY;
                    const Real64 UnskewedX  = (Sample.PositionX
                                                - Sample.SkewX  * Sample.PositionY) / Determinant;
                    const Real64 UnskewedY = (Sample.PositionY
                                                - Sample.SkewY * Sample.PositionX)  / Determinant;

                    if (Real64(CellX)  + WithinX  != UnskewedX  / Sample.CellExtentX
                     || Real64(CellY) + WithinY != UnskewedY / Sample.CellExtentY)
                    {
                        ++Report.DisagreeingCount;
                    }
                }

                // 📐 Flooring, never truncation toward zero. The ordinal is never above the coordinate it was
                //    floored from, which is the statement that separates the two below the origin and the one
                //    a truncating device form would break by exactly one cell.
                if (FlooredIndex(Sample.PositionX) > Sample.PositionX)
                    ++Report.DisagreeingCount;

                Real64 ProjectedX  = 0.0;
                Real64 ProjectedY = 0.0;

                ProjectWithinCell(CellX, CellY, WithinX, WithinY,
                                  Sample.ReflectionMask, Sample.RotationIncrement,
                                  ProjectedX, ProjectedY);

                // 📐 Bounded by the closed unit interval rather than the half-open one, because reflection
                //    carries a position at zero onto one. The consumer samples content with it and a position
                //    outside the interval samples content that was never resolved.
                if (ProjectedX < 0.0 || ProjectedX > 1.0
                 || ProjectedY < 0.0 || ProjectedY > 1.0)
                {
                    ++Report.DisagreeingCount;
                }

                // 📐 The turn count is taken modulo four, so an increment four greater turns the cell the same
                //    way. Exactly — the two calls run the same arithmetic and any difference is a lapse in the
                //    modulus rather than a rounding, which is what a signed turn count would produce.
                Real64 TurnedX  = 0.0;
                Real64 TurnedY = 0.0;

                ProjectWithinCell(CellX, CellY, WithinX, WithinY,
                                  Sample.ReflectionMask, Sample.RotationIncrement + 4u,
                                  TurnedX, TurnedY);

                if (TurnedX != ProjectedX || TurnedY != ProjectedY)
                    ++Report.DisagreeingCount;

                // 📐 An undeclared symmetry does nothing at all. A form that reflected on an empty mask would
                //    mirror alternate cells of a lattice that declared no mirror, which reads as a pattern the
                //    artist did not ask for rather than as a defect in a predicate.
                Real64 UnturnedX  = 0.0;
                Real64 UnturnedY = 0.0;

                ProjectWithinCell(CellX, CellY, WithinX, WithinY, 0u, 0u,
                                  UnturnedX, UnturnedY);

                if (UnturnedX != WithinX || UnturnedY != WithinY)
                    ++Report.DisagreeingCount;

                // 📐 The zigzag is a bijection onto the unsigned ordinals: a non-negative ordinal lands on an
                //    even one and a negative ordinal on an odd one, so no two cells either side of the origin
                //    can fold onto one variation. `54` §1's variation is a function of the fold alone.
                if ((ZigzagIndex(CellX)  & 1u) != (CellX  < 0 ? 1u : 0u)
                 || (ZigzagIndex(CellY) & 1u) != (CellY < 0 ? 1u : 0u))
                {
                    ++Report.DisagreeingCount;
                }

                // 📐 A cell never shares a variation with the cell beside it. `54` §1 concedes that the fold
                //    cannot be injective and that two distant cells eventually collide; a collision between
                //    neighbours is a different matter, and reads as two adjacent tiles carrying one variation.
                if (FoldedCellIndex(CellX, CellY) == FoldedCellIndex(CellX + 1, CellY)
                 || FoldedCellIndex(CellX, CellY) == FoldedCellIndex(CellX, CellY + 1))
                {
                    ++Report.DisagreeingCount;
                }
            }

            // 📐 The two ordinals the zigzag is defined by, stated rather than derived. Zero lands on zero and
            //    minus one on one; a form negating the most negative ordinal directly is undefined and is why
            //    the negative branch is written against the successor.
            if (ZigzagIndex(0) != 0u || ZigzagIndex(-1) != 1u || ZigzagIndex(1) != 2u)
                ++Report.DisagreeingCount;

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ProjectPlanarSample") == 0)
        {
            constexpr std::uint32_t SampleSpan = 4096u;

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                Real64 FirstCoordinate  = 0.0;
                Real64 SecondCoordinate = 0.0;
                ProjectPlanarSample(Index, FirstCoordinate, SecondCoordinate);

                if (FirstCoordinate < 0.0 || FirstCoordinate >= 1.0
                 || SecondCoordinate < 0.0 || SecondCoordinate >= 1.0)
                {
                    ++Report.DisagreeingCount;
                }

                // 📐 Reversing the bits of an even ordinal and of its successor differ in the highest bit
                //    alone, so the base-two inverses differ by exactly one half. Exactly, in binary, with no
                //    tolerance — which is the strongest statement available about any sample in the engine.
                if ((Index & 1u) == 0u)
                {
                    if (ProjectRadicalTwo(Index + 1u) - ProjectRadicalTwo(Index) != 0.5)
                        ++Report.DisagreeingCount;
                }
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ProjectSubPixelOffset") == 0)
        {
            // 🔴 `64` §8's gate. The offset is compared for the three properties `82`'s preview depends on:
            //    it lies **within** the pixel and never at a corner, it repeats on exactly the declared
            //    length, and the sequence's first ordinal is not the origin. A preview replaying a sequence
            //    that disagreed with the workspace's converges to a different image than the one on screen.
            constexpr std::uint32_t SampleSpan = 2048u;

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                Real64 OffsetX = 0.0;
                Real64 OffsetY = 0.0;
                ProjectSubPixelOffset(Index, OffsetX, OffsetY);

                if (OffsetX <= -0.5 || OffsetX >= 0.5 || OffsetY <= -0.5 || OffsetY >= 0.5)
                    ++Report.DisagreeingCount;

                // 📐 Exactly periodic on the declared length, so a rotation ordinal that has wrapped carries
                //    the offset the ordinal it wrapped from carried. Compared exactly — the two calls run the
                //    same arithmetic on the same ordinal and any difference is a lapse, never a rounding.
                Real64 WrappedX = 0.0;
                Real64 WrappedY = 0.0;
                ProjectSubPixelOffset(Index + Unsigned32(SubPixelSequenceLength), WrappedX, WrappedY);

                if (WrappedX != OffsetX || WrappedY != OffsetY)
                    ++Report.DisagreeingCount;

                // 📐 The origin is never sampled. A zero offset would place that rotation at the pixel corner
                //    and the accumulation would converge to a corner-sampled image on one rotation in the
                //    sequence — which reads as a single sharp rotation among softer ones.
                if (OffsetX == 0.0 && OffsetY == 0.0)
                    ++Report.DisagreeingCount;
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ProjectSphericalSample") == 0)
        {
            constexpr std::uint32_t SampleSpan = 1024u;

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                Real64 FirstCoordinate  = 0.0;
                Real64 SecondCoordinate = 0.0;
                ProjectPlanarSample(Index, FirstCoordinate, SecondCoordinate);

                Real64 DirectionX = 0.0;
                Real64 DirectionY = 0.0;
                Real64 DirectionZ = 0.0;
                ProjectSphericalSample(FirstCoordinate, SecondCoordinate, DirectionX, DirectionY, DirectionZ);

                const double Length = std::sqrt(DirectionX * DirectionX
                                              + DirectionY * DirectionY
                                              + DirectionZ * DirectionZ);

                // 📐 Measured in units in the last place about unity, which is what the report's deviation
                //    column declares. A Bounded entry point is compared against a bound and never for equality.
                const double Deviation = std::fabs(Length - 1.0) / MachineEpsilon;

                if (Deviation > Report.LargestDeviation)
                    Report.LargestDeviation = Deviation;
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ProjectTransmittanceCoordinate") == 0)
        {
            // 📐 ① is compared as an **inversion**, not against a second implementation of itself. The bake walks
            //    the surface backwards through `ProjectTransmittanceParameter` and every reader walks it forwards
            //    through this routine, so the two composing to the identity is the whole of what `28` §2 needs
            //    from them — and it is a statement each toolchain can check without the other present.
            // ⚠️ The deviation is measured on the **zenith axis only**. The altitude axis carries a radius of six
            //    and a half million metres and recovers a coordinate from it, so its round trip loses about five
            //    decimal places to the subtraction alone — honestly, identically on both toolchains, and far
            //    outside a ceiling stated in units in the last place. The altitude axis is held to containment
            //    below instead, which is the strongest statement that survives the magnitude.
            constexpr std::uint32_t AxisSpan = 64u;

            MediumProfile Profile{};
            Profile.PlanetRadius        = 6360000.0;
            Profile.AtmosphereThickness = 100000.0;

            for (std::uint32_t XIndex = 0u; XIndex < AxisSpan; ++XIndex)
            {
                for (std::uint32_t YIndex = 0u; YIndex < AxisSpan; ++YIndex)
                {
                    const Real64 CoordinateX  = (static_cast<Real64>(XIndex)  + 0.5) / AxisSpan;
                    const Real64 CoordinateY = (static_cast<Real64>(YIndex) + 0.5) / AxisSpan;

                    Real64 Radius       = 0.0;
                    Real64 ZenithCosine = 0.0;
                    ProjectTransmittanceParameter(Profile, CoordinateX, CoordinateY,
                                                  Radius, ZenithCosine);

                    if (Radius < Profile.PlanetRadius
                     || Radius > Profile.PlanetRadius + Profile.AtmosphereThickness
                     || ZenithCosine < -1.0 || ZenithCosine > 1.0)
                    {
                        ++Report.DisagreeingCount;
                    }

                    Real64 ReturnedX  = 0.0;
                    Real64 ReturnedY = 0.0;
                    ProjectTransmittanceCoordinate(Profile, Radius, ZenithCosine,
                                                   ReturnedX, ReturnedY);

                    const double Deviation = std::fabs(ReturnedX - CoordinateX) / MachineEpsilon;

                    if (Deviation > Report.LargestDeviation)
                        Report.LargestDeviation = Deviation;
                }
            }

            Report.SampleCount = AxisSpan * AxisSpan;
        }
        else if (std::strcmp(Held.EntryName, "ProjectSkyViewDirection") == 0)
        {
            // 📐 ③'s zenith axis is quadratic about the horizon and its inverse is a square root, so composing
            //    the two through an arc cosine near the pole recovers the coordinate to about half its digits —
            //    a property of the arc cosine and not of the mapping. What survives every magnitude is that the
            //    direction the mapping hands back is a **unit** direction, which is what every consumer of it
            //    assumes and what a mis-set quadratic branch would break immediately.
            constexpr std::uint32_t AxisSpan = 64u;

            for (std::uint32_t XIndex = 0u; XIndex < AxisSpan; ++XIndex)
            {
                for (std::uint32_t YIndex = 0u; YIndex < AxisSpan; ++YIndex)
                {
                    const Real64 CoordinateX  = (static_cast<Real64>(XIndex)  + 0.5) / AxisSpan;
                    const Real64 CoordinateY = (static_cast<Real64>(YIndex) + 0.5) / AxisSpan;

                    Real64 DirectionX = 0.0;
                    Real64 DirectionY = 0.0;
                    Real64 DirectionZ = 0.0;
                    ProjectSkyViewDirection(CoordinateX, CoordinateY,
                                            DirectionX, DirectionY, DirectionZ);

                    const double Length = std::sqrt(DirectionX * DirectionX
                                                  + DirectionY * DirectionY
                                                  + DirectionZ * DirectionZ);

                    const double Deviation = std::fabs(Length - 1.0) / MachineEpsilon;

                    if (Deviation > Report.LargestDeviation)
                        Report.LargestDeviation = Deviation;

                    // 📝 The lower half of the across axis descends and the upper half climbs, with the horizon
                    //    exactly at the halfway coordinate. A branch written the other way round produces a sky
                    //    that is upside down and otherwise entirely plausible.
                    const bool Climbing = CoordinateY > 0.5;

                    if (Climbing != (DirectionY > 0.0))
                        ++Report.DisagreeingCount;
                }
            }

            Report.SampleCount = AxisSpan * AxisSpan;
        }
        else if (std::strcmp(Held.EntryName, "TransmissionPrecedes") == 0)
        {
            // 📐 `62` §7 declares the ordering Tier A, so it is compared for the two properties an order has
            //    rather than against a second implementation: it is **irreflexive** and it is **total**, and a
            //    lapse in either lets two coplanar panes swap between rotations.
            constexpr std::uint32_t SampleSpan = 512u;

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                const std::uint32_t EarlierKey     = ProjectTransmissionKey(static_cast<double>(Index) / SampleSpan);
                const std::uint32_t LaterKey       = ProjectTransmissionKey(static_cast<double>(SampleSpan - Index)
                                                                          / SampleSpan);
                const std::uint32_t EarlierSurface = PackTransmissionSurface(Index, Index & 0x7Fu);
                const std::uint32_t LaterSurface   = PackTransmissionSurface(SampleSpan - Index, 3u);

                if (TransmissionPrecedes(EarlierKey, EarlierSurface, EarlierKey, EarlierSurface))
                    ++Report.DisagreeingCount;

                const bool Forward  = TransmissionPrecedes(EarlierKey, EarlierSurface, LaterKey, LaterSurface);
                const bool Reversed = TransmissionPrecedes(LaterKey, LaterSurface, EarlierKey, EarlierSurface);

                const bool Identical = EarlierKey == LaterKey && EarlierSurface == LaterSurface;

                if (!Identical && Forward == Reversed)
                    ++Report.DisagreeingCount;

                // 📐 The packing round-trips exactly, or the pixel resolves to another triangle of the same
                //    partition — which shades as a surface that is very nearly the right one.
                if (UnpackTransmissionPartition(EarlierSurface) != Index
                 || UnpackTransmissionTriangle(EarlierSurface) != (Index & 0x7Fu))
                {
                    ++Report.DisagreeingCount;
                }
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ResolveExactComposite") == 0)
        {
            // 🔴 `30` §1's whole guarantee, as two identities. At a weight of nothing the composite is the
            //    identity on the standing radiance — which is what makes every failure free and invisible — and
            //    at a weight of one it has swapped the pre-added contribution for the traced one exactly.
            constexpr std::uint32_t SampleSpan = 1024u;

            for (std::uint32_t Index = 0u; Index < SampleSpan; ++Index)
            {
                double Current = 0.0;
                double PreAdded = 0.0;
                ProjectPlanarSample(Index + 1u, Current, PreAdded);

                const double Traced = ProjectRadicalThree(Index + 7u) * 4.0;

                if (ResolveExactComposite(Current, PreAdded, Traced, 0.0) != Current)
                    ++Report.DisagreeingCount;

                const double Swapped  = ResolveExactComposite(Current, PreAdded, Traced, 1.0);
                const double Expected = Current - PreAdded + Traced;

                const double Deviation = std::fabs(Swapped - Expected) / MachineEpsilon;

                if (Deviation > Report.LargestDeviation)
                    Report.LargestDeviation = Deviation;
            }

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ProjectToneCompressed") == 0)
        {
            // 🔴 `66` §3's two requirements, measured rather than asserted: the curve is **monotonic** and it
            //    carries the declared white to exactly full display code. A curve that is monotonic almost
            //    everywhere lets an artist brighten a highlight and watch it darken.
            constexpr std::uint32_t SampleSpan = 4096u;
            constexpr double        White      = 6.0;

            double Preceding = ProjectToneCompressed(0.0, White);

            if (Preceding != 0.0)
                ++Report.DisagreeingCount;

            for (std::uint32_t Index = 1u; Index <= SampleSpan; ++Index)
            {
                const double Magnitude  = static_cast<double>(Index) / SampleSpan * White * 4.0;
                const double Compressed = ProjectToneCompressed(Magnitude, White);

                if (Compressed < Preceding)
                    ++Report.DisagreeingCount;

                Preceding = Compressed;
            }

            const double AtWhite   = ProjectToneCompressed(White, White);
            const double Deviation = std::fabs(AtWhite - 1.0) / MachineEpsilon;

            if (Deviation > Report.LargestDeviation)
                Report.LargestDeviation = Deviation;

            Report.SampleCount = SampleSpan;
        }
        else if (std::strcmp(Held.EntryName, "ProjectAccumulationWeight") == 0)
        {
            // 📐 `64` §3: the weight is one on the first sample, strictly decreasing while the count rises, and
            //    constant once the ceiling saturates. The third is what keeps a still workspace responsive, and
            //    it is the one an implementation forgets.
            constexpr std::uint32_t SampleSpan   = 256u;
            constexpr std::uint32_t CountLimit = 64u;

            if (ProjectAccumulationWeight(0u, CountLimit) != 1.0)
                ++Report.DisagreeingCount;

            for (std::uint32_t Index = 1u; Index < SampleSpan; ++Index)
            {
                const double Earlier = ProjectAccumulationWeight(Index - 1u, CountLimit);
                const double Later   = ProjectAccumulationWeight(Index, CountLimit);

                const bool Saturated = Index > CountLimit;

                if (Saturated ? Later != Earlier : Later >= Earlier)
                    ++Report.DisagreeingCount;

                if (ProjectAccumulatedCount(CountLimit, CountLimit) != CountLimit)
                    ++Report.DisagreeingCount;
            }

            Report.SampleCount = SampleSpan;
        }
        else
        {
            // 🚧 An entry point with no comparison declared here reports zero samples and does not hold.
            //    Reporting agreement over an empty sample set is how an unproven entry point passes a gate.
            Report.SampleCount = 0u;
        }

        // 🔴 A Bounded entry point holds only when its measured deviation stays inside the declared bound.
        //    Without this clause a Bounded registration would hold on the strength of an equality comparison
        //    it never performed, which is the vacant pass in a different disguise.
        const bool DeviationHeld = Held.Reserved != PrecisionGuarantee::Bounded
                                || Report.LargestDeviation <= SampleUnitPlaceLimit;

        Report.AgreementHeld = Report.SampleCount > 0u && Report.DisagreeingCount == 0u && DeviationHeld;

        if (!Report.AgreementHeld)
            AgreementDeclared = false;

        Reported.push_back(Report);
    }

    return Reported;
}

bool ParityRunner::AgreementHeld() const
{
    return AgreementDeclared;
}

}   // namespace Slate
