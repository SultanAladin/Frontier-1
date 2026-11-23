//============================================================================================================================================
//                                                             CURVESOLVER.CPP
//============================================================================================================================================
// 🧩 Adaptive subdivision, endpoint arc parameterisation, and bevelled offsetting.

#include "SlateMath/Numeric/CurveSolver/Api/CurveSolver.h"

#include "Foundation/NumericTolerance.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SUBDIVISION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The subdivision ceiling bounds the appended count at any tolerance, so a pathological control layout
//    cannot make one segment consume unbounded storage. `20` §2.2's evaluation budget cannot bound what it
//    cannot predict, and this is where the prediction comes from.
constexpr std::uint32_t SubdivisionLimit = 16u;   // [-] - halvings permitted per segment

double ChordDeviation(PlanarPosition Origin,
                      PlanarPosition Control,
                      PlanarPosition Terminus)
{
    // 📐 Twice the triangle's area over the chord length is the perpendicular distance from the control to the
    //    chord. A degenerate chord falls back to the direct displacement, which is the deviation in that case.
    const double ChordX = Terminus.PositionX - Origin.PositionX;
    const double ChordY = Terminus.PositionY - Origin.PositionY;
    const double SpanX  = Control.PositionX  - Origin.PositionX;
    const double SpanY  = Control.PositionY  - Origin.PositionY;

    const double ChordLength = std::sqrt(ChordX * ChordX + ChordY * ChordY);

    if (ChordLength <= 0.0)
        return std::sqrt(SpanX * SpanX + SpanY * SpanY);

    return std::fabs(ChordX * SpanY - ChordY * SpanX) / ChordLength;
}

PlanarPosition Interpolate(PlanarPosition Earlier, PlanarPosition Later, double Fraction)
{
    PlanarPosition Between;
    Between.PositionX = Earlier.PositionX + (Later.PositionX - Earlier.PositionX) * Fraction;
    Between.PositionY = Earlier.PositionY + (Later.PositionY - Earlier.PositionY) * Fraction;

    return Between;
}

void SubdivideCubic(PlanarPosition                Origin,
                    PlanarPosition                FirstControl,
                    PlanarPosition                SecondControl,
                    PlanarPosition                Terminus,
                    double                        Tolerance,
                    std::uint32_t                 Remaining,
                    std::vector<PlanarPosition>&  Appending)
{
    const double FirstDeviation  = ChordDeviation(Origin, FirstControl,  Terminus);
    const double SecondDeviation = ChordDeviation(Origin, SecondControl, Terminus);

    if (Remaining == 0u || (FirstDeviation <= Tolerance && SecondDeviation <= Tolerance))
    {
        Appending.push_back(Terminus);
        return;
    }

    // 📐 De Casteljau at the midpoint. Halving the curve halves the deviation roughly fourfold, so the ceiling
    //    above is reached only by control positions that are pathological rather than merely tight.
    const PlanarPosition FirstLeft   = Interpolate(Origin,        FirstControl,  0.5);
    const PlanarPosition Middle      = Interpolate(FirstControl,  SecondControl, 0.5);
    const PlanarPosition SecondRight = Interpolate(SecondControl, Terminus,      0.5);

    const PlanarPosition SecondLeft = Interpolate(FirstLeft,  Middle,      0.5);
    const PlanarPosition FirstRight = Interpolate(Middle,     SecondRight, 0.5);
    const PlanarPosition Divided    = Interpolate(SecondLeft, FirstRight,  0.5);

    SubdivideCubic(Origin,  FirstLeft,  SecondLeft,  Divided,  Tolerance, Remaining - 1u, Appending);
    SubdivideCubic(Divided, FirstRight, SecondRight, Terminus, Tolerance, Remaining - 1u, Appending);
}

void FlattenArc(PlanarPosition                Origin,
                const PathSegment&            Segment,
                double                        Tolerance,
                std::vector<PlanarPosition>&  Appending)
{
    const double RadiusX  = std::fabs(Segment.RadiusX);
    const double RadiusY = std::fabs(Segment.RadiusY);

    if (RadiusX <= 0.0 || RadiusY <= 0.0)
    {
        // 📝 A degenerate radius is a line to the terminus, which is what the geometry degenerates to. Refusing
        //    here would refuse a whole outline over one flattened segment nobody can see.
        Appending.push_back(Segment.Terminus);
        return;
    }

    // 📐 Endpoint parameterisation to centre parameterisation. The arc's own axes are rotated by Rotation, so
    //    the endpoints are brought into those axes, the centre is solved there, and the sweep is walked back out.
    const double Radians = Segment.Rotation * Pi / 180.0;
    const double Cosine  = std::cos(Radians);
    const double Sine    = std::sin(Radians);

    const double HalfSpanX = (Origin.PositionX - Segment.Terminus.PositionX) * 0.5;
    const double HalfSpanY = (Origin.PositionY - Segment.Terminus.PositionY) * 0.5;

    const double RotatedX = Cosine * HalfSpanX + Sine   * HalfSpanY;
    const double RotatedY = -Sine  * HalfSpanX + Cosine * HalfSpanY;

    double ScaledX  = RadiusX;
    double ScaledY = RadiusY;

    const double Excess = (RotatedX * RotatedX) / (ScaledX * ScaledX)
                        + (RotatedY * RotatedY) / (ScaledY * ScaledY);

    if (Excess > 1.0)
    {
        const double Enlargement = std::sqrt(Excess);
        ScaledX  *= Enlargement;
        ScaledY *= Enlargement;
    }

    const double Numerator = ScaledX * ScaledX * ScaledY * ScaledY
                           - ScaledX * ScaledX * RotatedY * RotatedY
                           - ScaledY * ScaledY * RotatedX * RotatedX;

    const double Denominator = ScaledX * ScaledX * RotatedY * RotatedY
                             + ScaledY * ScaledY * RotatedX * RotatedX;

    double CentreScale = 0.0;

    if (Denominator > 0.0 && Numerator > 0.0)
        CentreScale = std::sqrt(Numerator / Denominator);

    if (Segment.LargeArcEnabled == Segment.SweepEnabled)
        CentreScale = -CentreScale;

    const double RotatedCentreX =  CentreScale * ScaledX  * RotatedY / ScaledY;
    const double RotatedCentreY = -CentreScale * ScaledY * RotatedX / ScaledX;

    const double CentreX = Cosine * RotatedCentreX - Sine   * RotatedCentreY
                         + (Origin.PositionX + Segment.Terminus.PositionX) * 0.5;
    const double CentreY = Sine   * RotatedCentreX + Cosine * RotatedCentreY
                         + (Origin.PositionY + Segment.Terminus.PositionY) * 0.5;

    const double FirstAngle = std::atan2((RotatedY - RotatedCentreY) / ScaledY,
                                         (RotatedX - RotatedCentreX) / ScaledX);
    const double LastAngle  = std::atan2((-RotatedY - RotatedCentreY) / ScaledY,
                                         (-RotatedX - RotatedCentreX) / ScaledX);

    double Sweep = LastAngle - FirstAngle;

    if (!Segment.SweepEnabled && Sweep > 0.0)
        Sweep -= 2.0 * Pi;
    else if (Segment.SweepEnabled && Sweep < 0.0)
        Sweep += 2.0 * Pi;

    // 📐 The sagitta of one step of angular width θ over radius r is r(1 − cos(θ/2)). Solving that for the
    //    declared tolerance gives the step count directly, so the arc is never subdivided further than the
    //    tolerance asks and never less.
    const double MaximumRadius = ScaledX > ScaledY ? ScaledX : ScaledY;
    double       StepAngle      = Pi * 0.5;

    if (Tolerance > 0.0 && Tolerance < MaximumRadius)
        StepAngle = 2.0 * std::acos(1.0 - Tolerance / MaximumRadius);

    std::uint32_t StepCount = static_cast<std::uint32_t>(std::ceil(std::fabs(Sweep) / StepAngle));

    if (StepCount == 0u)
        StepCount = 1u;

    if (StepCount > (1u << SubdivisionLimit))
        StepCount = 1u << SubdivisionLimit;

    for (std::uint32_t Index = 1u; Index <= StepCount; ++Index)
    {
        const double Angle = FirstAngle + Sweep * static_cast<double>(Index)
                                                / static_cast<double>(StepCount);

        const double XTerm  = ScaledX  * std::cos(Angle);
        const double YTerm = ScaledY * std::sin(Angle);

        PlanarPosition Walked;
        Walked.PositionX = CentreX + Cosine * XTerm - Sine   * YTerm;
        Walked.PositionY = CentreY + Sine   * XTerm + Cosine * YTerm;

        Appending.push_back(Walked);
    }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     FLATTENING
//------------------------------------------------------------------------------------------------------------------------

void Flatten(PlanarPosition                Origin,
             const PathSegment&            Segment,
             double                        Tolerance,
             std::vector<PlanarPosition>&  Appending)
{
    const double Bounded = Tolerance > 0.0 ? Tolerance : 0.0;

    switch (Segment.Subject)
    {
        case SegmentSubject::Line:
            Appending.push_back(Segment.Terminus);
            return;

        case SegmentSubject::Quadratic:
        {
            // 📐 A quadratic is the cubic whose two controls sit two thirds of the way from each end toward the
            //    single control. Raising the degree once costs two interpolations and removes a whole
            //    subdivision path that would have to agree with the cubic one forever.
            PlanarPosition FirstControl  = Interpolate(Origin,            Segment.FirstControl, 2.0 / 3.0);
            PlanarPosition SecondControl = Interpolate(Segment.Terminus,  Segment.FirstControl, 2.0 / 3.0);

            SubdivideCubic(Origin, FirstControl, SecondControl, Segment.Terminus,
                           Bounded, SubdivisionLimit, Appending);
            return;
        }

        case SegmentSubject::Cubic:
            SubdivideCubic(Origin, Segment.FirstControl, Segment.SecondControl, Segment.Terminus,
                           Bounded, SubdivisionLimit, Appending);
            return;

        case SegmentSubject::Arc:
            FlattenArc(Origin, Segment, Bounded, Appending);
            return;

        case SegmentSubject::SegmentCount:
            break;
    }

    Appending.push_back(Segment.Terminus);
}

std::vector<PlanarPosition> Flatten(PlanarPosition                  Origin,
                                    const std::vector<PathSegment>& Segments,
                                    double                          Tolerance)
{
    std::vector<PlanarPosition> Flattened;
    Flattened.reserve(Segments.size() * 4u + 1u);
    Flattened.push_back(Origin);

    PlanarPosition Walking = Origin;

    for (const PathSegment& Traversing : Segments)
    {
        Flatten(Walking, Traversing, Tolerance, Flattened);
        Walking = Traversing.Terminus;
    }

    return Flattened;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     OFFSETTING
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::vector<PlanarPosition>> OffsetOutline(const std::vector<PlanarPosition>& Traversed,
                                                   double                             HalfWidth,
                                                   bool                               ClosedRun)
{
    if (HalfWidth <= 0.0)
    {
        return Deliver<std::vector<PlanarPosition>>::Refuse(
            { RefusalReason::ContentUnsupported, "a stroke of no width encloses nothing" });
    }

    if (Traversed.size() < 2u)
    {
        return Deliver<std::vector<PlanarPosition>>::Refuse(
            { RefusalReason::ExtentExhausted, "a stroke needs two positions to have a direction" });
    }

    // 📝 One side is walked forward and the other backward, so the two meet as a single closed run and the fill
    //    rule sees one outline rather than two disjoint ones it has to relate.
    std::vector<PlanarPosition> Outlined;
    Outlined.reserve(Traversed.size() * 2u + 2u);

    for (std::uint32_t Side = 0u; Side < 2u; ++Side)
    {
        const double Signum = Side == 0u ? 1.0 : -1.0;

        for (std::size_t Passed = 0u; Passed < Traversed.size(); ++Passed)
        {
            const std::size_t Index = Side == 0u ? Passed : Traversed.size() - 1u - Passed;

            const std::size_t Earlier = Index == 0u
                                      ? (ClosedRun ? Traversed.size() - 1u : 0u)
                                      : Index - 1u;

            const std::size_t Later = Index + 1u >= Traversed.size()
                                    ? (ClosedRun ? 0u : Traversed.size() - 1u)
                                    : Index + 1u;

            const double SpanX = Traversed[Later].PositionX - Traversed[Earlier].PositionX;
            const double SpanY = Traversed[Later].PositionY - Traversed[Earlier].PositionY;
            const double Length = std::sqrt(SpanX * SpanX + SpanY * SpanY);

            if (Length <= 0.0)
                continue;

            PlanarPosition Offset;
            Offset.PositionX = Traversed[Index].PositionX - Signum * HalfWidth * SpanY / Length;
            Offset.PositionY = Traversed[Index].PositionY + Signum * HalfWidth * SpanX / Length;

            Outlined.push_back(Offset);
        }
    }

    return Deliver<std::vector<PlanarPosition>>::Result(Outlined);
}

}   // namespace Slate
