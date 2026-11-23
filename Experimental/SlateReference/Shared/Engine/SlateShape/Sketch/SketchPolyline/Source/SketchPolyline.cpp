//============================================================================================================================================
//                                                      SKETCHPOLYLINE.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    /// 🧩 One cubic Bezier segment, by de Casteljau over exactly four control points.
    SpatialPoint EvaluateCubicSpan(const SpatialPoint& First, const SpatialPoint& Second,
                                   const SpatialPoint& Third, const SpatialPoint& Fourth,
                                   double Parameter)
    {
        const double Remaining = 1.0 - Parameter;
        const double A = Remaining * Remaining * Remaining;
        const double B = 3.0 * Remaining * Remaining * Parameter;
        const double C = 3.0 * Remaining * Parameter * Parameter;
        const double D = Parameter * Parameter * Parameter;

        return { A * First.Left    + B * Second.Left    + C * Third.Left    + D * Fourth.Left,
                 A * First.Up      + B * Second.Up      + C * Third.Up      + D * Fourth.Up,
                 A * First.Forward + B * Second.Forward + C * Third.Forward + D * Fourth.Forward };
    }

    /// 🧩 A curve through every point the artist clicked, as a CHAIN of cubic segments.
    ///
    /// 🔴 THIS WAS ONE BEZIER OF DEGREE N-1, AND THAT IS WHY THE CURVE LOST ITS SHAPE. De Casteljau
    ///    was run across every control point at once, so the tenth click did not extend the curve --
    ///    it raised the degree of the whole thing. A Bezier of degree N-1 passes through its first
    ///    and last control point and NO OTHER: the interior anchors only pull, and the pull weakens
    ///    as the degree rises. Measured on a zig-zag, the curve missed its own interior anchors by
    ///    40.00 at three anchors and 42.86 at eight, worsening with every point added. The artist
    ///    reads that as the curve smoothing itself away from what they drew, which is exactly what
    ///    it is.
    ///
    /// 📝 Each pair of anchors now spans its own cubic, and the interior control points are placed
    ///    a third of the way along the neighbouring chord -- the Catmull-Rom tangent, written as
    ///    Bezier control points. The curve therefore INTERPOLATES every anchor, and adding one
    ///    changes only the two segments beside it. The ends use the single adjacent chord, so the
    ///    first and last segments lean the way the drawing does.
    ///
    /// ⚠️ `Parameter` runs over the WHOLE chain, not one segment: the caller samples 0 to 1 across
    ///    the curve and this maps that onto the segment it lands in.
    SpatialPoint EvaluateBezier(const std::vector<SpatialPoint>& ControlPoints,
                                double Parameter)
    {
        if (ControlPoints.empty())
            return {};
        if (ControlPoints.size() == 1u)
            return ControlPoints.front();
        if (ControlPoints.size() == 2u)
            return EvaluateCubicSpan(ControlPoints[0], ControlPoints[0],
                                     ControlPoints[1], ControlPoints[1], Parameter);

        const std::size_t SpanCount = ControlPoints.size() - 1u;

        // ⚠️ Held one short of the end for the same reason the spline clamp is: at exactly 1.0 the
        //    landed span would be one past the last, and the curve would evaluate to nothing.
        const double Reached = Parameter >= 1.0
                             ? static_cast<double>(SpanCount) * (1.0 - 1.0e-12)
                             : (Parameter <= 0.0 ? 0.0 : Parameter * static_cast<double>(SpanCount));

        const std::size_t Landed = static_cast<std::size_t>(Reached);
        const std::size_t Span   = Landed < SpanCount ? Landed : SpanCount - 1u;
        const double      Local  = Reached - static_cast<double>(Span);

        const SpatialPoint& Start = ControlPoints[Span];
        const SpatialPoint& End   = ControlPoints[Span + 1u];

        // 📝 The neighbours on either side, mirrored at the ends so a chord always exists.
        const SpatialPoint& Before = Span == 0u ? Start : ControlPoints[Span - 1u];
        const SpatialPoint& After  = Span + 2u < ControlPoints.size() ? ControlPoints[Span + 2u] : End;

        constexpr double Sixth = 1.0 / 6.0;
        const SpatialDirection Leaving  = Scaled(Difference(Before, End), Sixth);
        const SpatialDirection Arriving = Scaled(Difference(Start, After), Sixth);

        return EvaluateCubicSpan(Start,
                                 Added(Start, Leaving),
                                 Added(End, Negated(Arriving)),
                                 End,
                                 Local);
    }

    std::vector<double> BuildClampedKnots(std::uint32_t Degree,
                                          std::size_t ControlCount,
                                          bool Periodic)
    {
        const std::size_t KnotCount = ControlCount + Degree + 1u;
        std::vector<double> Knots(KnotCount, 0.0);
        if (Periodic)
        {
            for (std::size_t KnotIndex = 0u; KnotIndex < KnotCount; ++KnotIndex)
                Knots[KnotIndex] = static_cast<double>(KnotIndex);
            return Knots;
        }

        for (std::size_t KnotIndex = 0u; KnotIndex <= Degree; ++KnotIndex)
            Knots[KnotIndex] = 0.0;
        const std::size_t InteriorCount = ControlCount > Degree + 1u ? ControlCount - Degree - 1u : 0u;
        for (std::size_t InteriorIndex = 0u; InteriorIndex < InteriorCount; ++InteriorIndex)
            Knots[Degree + 1u + InteriorIndex] = static_cast<double>(InteriorIndex + 1u);
        const double Maximum = static_cast<double>(InteriorCount + 1u);
        for (std::size_t KnotIndex = KnotCount - Degree - 1u; KnotIndex < KnotCount; ++KnotIndex)
            Knots[KnotIndex] = Maximum;
        return Knots;
    }

    double BasisValue(std::size_t BasisIndex,
                      std::uint32_t Degree,
                      double Parameter,
                      const std::vector<double>& Knots)
    {
        if (Degree == 0u)
        {
            if ((Knots[BasisIndex] <= Parameter && Parameter < Knots[BasisIndex + 1u])
             || (Parameter == Knots.back() && BasisIndex + 1u == Knots.size() - 1u))
                return 1.0;
            return 0.0;
        }

        double Left = 0.0;
        const double LeftWidth = Knots[BasisIndex + Degree] - Knots[BasisIndex];
        if (LeftWidth > 0.0)
            Left = (Parameter - Knots[BasisIndex]) / LeftWidth * BasisValue(BasisIndex, Degree - 1u, Parameter, Knots);

        double Right = 0.0;
        const double RightWidth = Knots[BasisIndex + Degree + 1u] - Knots[BasisIndex + 1u];
        if (RightWidth > 0.0)
            Right = (Knots[BasisIndex + Degree + 1u] - Parameter) / RightWidth
                  * BasisValue(BasisIndex + 1u, Degree - 1u, Parameter, Knots);

        return Left + Right;
    }

    SpatialPoint EvaluateSpline(const std::vector<SpatialPoint>& ControlPoints,
                                const std::vector<double>* Weights,
                                std::uint32_t Degree,
                                bool Periodic,
                                double Parameter)
    {
        const std::vector<double> Knots = BuildClampedKnots(Degree, ControlPoints.size(), Periodic);
        const double Maximum = Knots[Knots.size() - Degree - 1u];

        // 🔴 THE LAST POINT OF EVERY SPLINE JUMPED BACK TO THE FIRST, drawing a long straight line
        //    from the end of the curve to its start that was always on screen while drawing -- the
        //    reported "from the moment i start drawing that line is always connecting start to end".
        //    A clamped basis is a HALF-OPEN interval: the span tests are `Knot <= t < Next`, so at
        //    exactly the upper knot EVERY basis function evaluates to zero, the weight sum with it,
        //    and the guard below returned `ControlPoints.front()` -- the start of the curve, at the
        //    parameter that should have produced its end.
        //
        // 📝 Nudged just inside the last span rather than special-cased: the limit of the curve as
        //    the parameter approaches the end IS the last control point for a clamped basis, so this
        //    lands on it to well within a pixel without a second code path to keep correct.
        const double Local = Parameter >= 1.0
                           ? Maximum * (1.0 - 1.0e-12)
                           : Parameter * Maximum;

        SpatialPoint Sum = {};
        double WeightSum = 0.0;
        for (std::size_t Index = 0u; Index < ControlPoints.size(); ++Index)
        {
            const double Basis = BasisValue(Index, Degree, Local, Knots);
            const double Weight = Weights != nullptr ? (*Weights)[Index] : 1.0;
            Sum.Left += ControlPoints[Index].Left * Basis * Weight;
            Sum.Up += ControlPoints[Index].Up * Basis * Weight;
            Sum.Forward += ControlPoints[Index].Forward * Basis * Weight;
            WeightSum += Basis * Weight;
        }

        if (WeightSum == 0.0)
            return ControlPoints.front();
        return { Sum.Left / WeightSum, Sum.Up / WeightSum, Sum.Forward / WeightSum };
    }

    SpatialPoint EvaluateHermite(const HermiteCurve& Curve,
                                 double Parameter)
    {
        if (Curve.ControlPoints.size() >= 2u)
        {
            const std::size_t SpanCount = Curve.ControlPoints.size() - 1u;
            const double ScaledParam = std::clamp(Parameter, 0.0, 1.0) * static_cast<double>(SpanCount);
            std::size_t SpanIndex = static_cast<std::size_t>(std::floor(ScaledParam));
            if (SpanIndex >= SpanCount)
                SpanIndex = SpanCount - 1u;
            const double LocalParam = ScaledParam - static_cast<double>(SpanIndex);

            const SpatialPoint& P0 = Curve.ControlPoints[SpanIndex];
            const SpatialPoint& P1 = Curve.ControlPoints[SpanIndex + 1u];

            SpatialDirection T0 = {};
            if (Curve.Tangents.size() > SpanIndex)
                T0 = Curve.Tangents[SpanIndex];
            else
            {
                const SpatialPoint& Before = (SpanIndex == 0u) ? P0 : Curve.ControlPoints[SpanIndex - 1u];
                T0 = Scaled(Difference(Before, P1), 0.5);
            }

            SpatialDirection T1 = {};
            if (Curve.Tangents.size() > SpanIndex + 1u)
                T1 = Curve.Tangents[SpanIndex + 1u];
            else
            {
                const SpatialPoint& After = (SpanIndex + 2u < Curve.ControlPoints.size()) ? Curve.ControlPoints[SpanIndex + 2u] : P1;
                T1 = Scaled(Difference(P0, After), 0.5);
            }

            const double Square = LocalParam * LocalParam;
            const double Cube = Square * LocalParam;
            const double H00 = 2.0 * Cube - 3.0 * Square + 1.0;
            const double H10 = Cube - 2.0 * Square + LocalParam;
            const double H01 = -2.0 * Cube + 3.0 * Square;
            const double H11 = Cube - Square;
            return {
                H00 * P0.Left + H10 * T0.Left + H01 * P1.Left + H11 * T1.Left,
                H00 * P0.Up + H10 * T0.Up + H01 * P1.Up + H11 * T1.Up,
                H00 * P0.Forward + H10 * T0.Forward + H01 * P1.Forward + H11 * T1.Forward
            };
        }

        const double Square = Parameter * Parameter;
        const double Cube = Square * Parameter;
        const double H00 = 2.0 * Cube - 3.0 * Square + 1.0;
        const double H10 = Cube - 2.0 * Square + Parameter;
        const double H01 = -2.0 * Cube + 3.0 * Square;
        const double H11 = Cube - Square;
        return {
            H00 * Curve.StartPoint.Left + H10 * Curve.StartTangent.Left + H01 * Curve.EndPoint.Left + H11 * Curve.EndTangent.Left,
            H00 * Curve.StartPoint.Up + H10 * Curve.StartTangent.Up + H01 * Curve.EndPoint.Up + H11 * Curve.EndTangent.Up,
            H00 * Curve.StartPoint.Forward + H10 * Curve.StartTangent.Forward + H01 * Curve.EndPoint.Forward + H11 * Curve.EndTangent.Forward
        };
    }
}

namespace
{
    /// 🔴 Steps for a circular sweep held to `CurveChordTolerance`. Inverting the sagitta
    ///    `t = R(1 - cos(θ/2))` gives `θ = 2·acos(1 - t/R)`, and the count is the sweep over that.
    std::uint32_t StepsForSweep(double Radius, double SweepRadians)
    {
        const double Sweep = std::fabs(SweepRadians);
        if (!(Radius > CurveChordTolerance) || !(Sweep > 0.0))
            return 0u;

        const double PerStep = 2.0 * std::acos(std::clamp(1.0 - CurveChordTolerance / Radius, -1.0, 1.0));
        if (!(PerStep > 1.0e-9))
            return CurveStepLimit;
        return static_cast<std::uint32_t>(std::ceil(Sweep / PerStep));
    }

    /// 🔴 A spline has no single radius, so the length of its control polygon stands in for arc length —
    ///    of which it is an upper bound. One chord per `8 × CurveChordTolerance` of that length.
    std::uint32_t StepsForControlPolygon(const std::vector<SpatialPoint>& ControlPoints)
    {
        if (ControlPoints.size() < 2u)
            return 0u;

        double Span = 0.0;
        for (std::size_t Index = 0u; Index + 1u < ControlPoints.size(); ++Index)
            Span += std::sqrt(LengthSquared(Difference(ControlPoints[Index], ControlPoints[Index + 1u])));

        return static_cast<std::uint32_t>(std::ceil(Span / (CurveChordTolerance * 8.0)));
    }
}

std::uint32_t ResolveCurveStepCount(const CurveSpecification& Geometry,
                                    std::uint32_t Floor)
{
    std::uint32_t Wanted = 0u;

    switch (Geometry.Subject())
    {
        // 📝 A line is exact at two points however long it is drawn; there is nothing to shrink.
        case CurveSubject::Line:
            return 2u;

        case CurveSubject::CircularArc:
            Wanted = StepsForSweep(Geometry.HeldCircularArc().Radius,
                                   Geometry.HeldCircularArc().SweepRadians);
            break;

        case CurveSubject::Circle:
            Wanted = StepsForSweep(Geometry.HeldCircle().Radius, 6.283185307179586);
            break;

        // 📝 The major radius is the worst case: curvature is highest at the ends of the major axis.
        case CurveSubject::EllipticalArc:
            Wanted = StepsForSweep(Geometry.HeldEllipticalArc().MajorRadius,
                                   Geometry.HeldEllipticalArc().SweepRadians);
            break;

        case CurveSubject::Ellipse:
            Wanted = StepsForSweep(Geometry.HeldEllipse().MajorRadius, 6.283185307179586);
            break;

        case CurveSubject::Bezier:
            Wanted = StepsForControlPolygon(Geometry.HeldBezier().ControlPoints);
            break;

        case CurveSubject::BasisSpline:
            Wanted = StepsForControlPolygon(Geometry.HeldBasisSpline().ControlPoints);
            break;

        case CurveSubject::RationalSpline:
            Wanted = StepsForControlPolygon(Geometry.HeldRationalSpline().ControlPoints);
            break;

        // 📝 A Hermite span is bounded by its endpoints and its two tangents, and a tangent may carry the
        //    curve well past the straight line between them, so all four contribute.
        case CurveSubject::Hermite:
        {
            const HermiteCurve& Hermite = Geometry.HeldHermite();
            if (Hermite.ControlPoints.size() >= 2u)
                Wanted = StepsForControlPolygon(Hermite.ControlPoints);
            else
                Wanted = StepsForControlPolygon({ Hermite.StartPoint,
                                                  Added(Hermite.StartPoint, Hermite.StartTangent),
                                                  Added(Hermite.EndPoint, Hermite.EndTangent),
                                                  Hermite.EndPoint });
            break;
        }

        case CurveSubject::SubjectCount:
        default:
            break;
    }

    return std::clamp(std::max(Wanted, Floor), 2u, CurveStepLimit);
}

std::uint32_t ResolveCurveStepCountForDetail(const CurveSpecification& Geometry,
                                             std::uint32_t Floor,
                                             double DetailScale)
{
    const double BoundedScale = std::clamp(DetailScale, 1.0, 8.0);
    const std::uint32_t DetailedFloor = static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(Floor) * BoundedScale));
    return ResolveCurveStepCount(Geometry, DetailedFloor);
}

void AppendCurvePolyline(const CurveSpecification& Geometry,
                         std::vector<SpatialPoint>& Polyline,
                         std::uint32_t StepCount)
{
    Polyline.clear();
    const std::uint32_t Steps = StepCount < 2u ? 2u : StepCount;

    switch (Geometry.Subject())
    {
        case CurveSubject::Line:
            Polyline.push_back(Geometry.HeldLine().Origin);
            Polyline.push_back(Geometry.HeldLine().Terminus);
            return;

        case CurveSubject::CircularArc:
        {
            const CircularArcCurve& Arc = Geometry.HeldCircularArc();
            const SpatialDirection StartDirection = Normalize(Arc.StartDirection);
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
            {
                const double Fraction = static_cast<double>(StepIndex) / static_cast<double>(Steps);
                const SpatialDirection CurrentDirection = RotateAroundAxis(StartDirection, Arc.Normal, Arc.SweepRadians * Fraction);
                Polyline.push_back(Added(Arc.Centre, Scaled(CurrentDirection, Arc.Radius)));
            }
            return;
        }

        case CurveSubject::Circle:
        {
            const CircleCurve& Circle = Geometry.HeldCircle();
            const SpatialDirection StartDirection = Normalize(Circle.StartDirection);
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
            {
                const double Fraction = static_cast<double>(StepIndex) / static_cast<double>(Steps);
                const SpatialDirection CurrentDirection = RotateAroundAxis(StartDirection, Circle.Normal, 6.283185307179586 * Fraction);
                Polyline.push_back(Added(Circle.Centre, Scaled(CurrentDirection, Circle.Radius)));
            }
            return;
        }

        case CurveSubject::EllipticalArc:
        {
            const EllipticalArcCurve& Arc = Geometry.HeldEllipticalArc();
            const SpatialDirection MajorDirection = Normalize(Arc.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Arc.Normal, MajorDirection));
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
            {
                const double Fraction = static_cast<double>(StepIndex) / static_cast<double>(Steps);
                const double Radians = Arc.StartRadians + Arc.SweepRadians * Fraction;
                Polyline.push_back({ Arc.Centre.Left + MajorDirection.Left * Arc.MajorRadius * std::cos(Radians) + MinorDirection.Left * Arc.MinorRadius * std::sin(Radians),
                                     Arc.Centre.Up + MajorDirection.Up * Arc.MajorRadius * std::cos(Radians) + MinorDirection.Up * Arc.MinorRadius * std::sin(Radians),
                                     Arc.Centre.Forward + MajorDirection.Forward * Arc.MajorRadius * std::cos(Radians) + MinorDirection.Forward * Arc.MinorRadius * std::sin(Radians) });
            }
            return;
        }

        case CurveSubject::Ellipse:
        {
            const EllipseCurve& Ellipse = Geometry.HeldEllipse();
            const SpatialDirection MajorDirection = Normalize(Ellipse.MajorDirection);
            const SpatialDirection MinorDirection = Normalize(Cross(Ellipse.Normal, MajorDirection));
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
            {
                const double Fraction = static_cast<double>(StepIndex) / static_cast<double>(Steps);
                const double Radians = 6.283185307179586 * Fraction;
                Polyline.push_back({ Ellipse.Centre.Left + MajorDirection.Left * Ellipse.MajorRadius * std::cos(Radians) + MinorDirection.Left * Ellipse.MinorRadius * std::sin(Radians),
                                     Ellipse.Centre.Up + MajorDirection.Up * Ellipse.MajorRadius * std::cos(Radians) + MinorDirection.Up * Ellipse.MinorRadius * std::sin(Radians),
                                     Ellipse.Centre.Forward + MajorDirection.Forward * Ellipse.MajorRadius * std::cos(Radians) + MinorDirection.Forward * Ellipse.MinorRadius * std::sin(Radians) });
            }
            return;
        }

        case CurveSubject::Bezier:
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
                Polyline.push_back(EvaluateBezier(Geometry.HeldBezier().ControlPoints,
                                                  static_cast<double>(StepIndex) / static_cast<double>(Steps)));
            return;

        case CurveSubject::BasisSpline:
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
                Polyline.push_back(EvaluateSpline(Geometry.HeldBasisSpline().ControlPoints,
                                                  nullptr,
                                                  Geometry.HeldBasisSpline().Degree,
                                                  Geometry.HeldBasisSpline().Periodic,
                                                  static_cast<double>(StepIndex) / static_cast<double>(Steps)));
            return;

        case CurveSubject::RationalSpline:
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
                Polyline.push_back(EvaluateSpline(Geometry.HeldRationalSpline().ControlPoints,
                                                  &Geometry.HeldRationalSpline().Weights,
                                                  Geometry.HeldRationalSpline().Degree,
                                                  Geometry.HeldRationalSpline().Periodic,
                                                  static_cast<double>(StepIndex) / static_cast<double>(Steps)));
            return;

        case CurveSubject::Hermite:
            for (std::uint32_t StepIndex = 0u; StepIndex <= Steps; ++StepIndex)
                Polyline.push_back(EvaluateHermite(Geometry.HeldHermite(),
                                                   static_cast<double>(StepIndex) / static_cast<double>(Steps)));
            return;

        case CurveSubject::SubjectCount:
            return;
    }
}

} // namespace Slate
