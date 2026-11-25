/*====================================================================================================================================
                                                        FACEDOMAIN.CPP
====================================================================================================================================*/
// 🧩 Trimmed face domains in (𝑢, 𝑣): SP-curve loops, exact 2D winding number, point-in-face, interior point location

#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    constexpr double FullTurn         = 2.0 * std::numbers::pi;                              // [rad] - 2π
    constexpr double ClosureGap       = 1.0e-9;                                              // [-]   - Chart gap needing a closure
    constexpr int    MinimumDepth     = 3;                                                   // [-]   - Curved pieces ≤ 1/8 segment
    constexpr int    DepthLimit       = 52;                                                  // [-]   - Bisection cap
    constexpr int    WindowSamples    = 32;                                                  // [-]   - Per curved segment
    constexpr int    BoundsGrid       = 16;                                                  // [-]   - Cells per direction

    [[nodiscard]] double SweptAngle(Vector2 From, Vector2 To) noexcept
    {
        return std::atan2(Cross(From, To), Dot(From, To));
    }

    // Swept angle of q([τa, τb]) about the point, bisected until consistent.
    [[nodiscard]] double SweepCurve(const ParameterCurve& Curve, double τa, double τb, Vector2 Qa, Vector2 Qb, Vector2 Point, int Depth)
    {
        const double  τm    = 0.5 * (τa + τb);                                               // [-] - Midpoint parameter
        const Vector2 Qm    = EvaluateParameterCurve(Curve, τm).Position;                    // [-] - Midpoint image
        const double  Whole = SweptAngle(Qa - Point, Qb - Point);                            // [rad] - Chord angle
        const double  Left  = SweptAngle(Qa - Point, Qm - Point);                            // [rad] - First half
        const double  Right = SweptAngle(Qm - Point, Qb - Point);                            // [rad] - Second half
        if (Depth >= MinimumDepth && std::fabs(Left + Right - Whole) < 1.0e-11 && std::fabs(Whole) < 1.0) return Whole;
        if (Depth >= DepthLimit) return Left + Right;
        return SweepCurve(Curve, τa, τm, Qa, Qm, Point, Depth + 1) + SweepCurve(Curve, τm, τb, Qm, Qb, Point, Depth + 1);
    }

    [[nodiscard]] double SweepSegment(const DomainSegment& Segment, Vector2 Point)
    {
        const Vector2 Qa = EvaluateParameterCurve(Segment.Curve, Segment.Start).Position;    // [-] - Start image
        const Vector2 Qb = EvaluateParameterCurve(Segment.Curve, Segment.End).Position;      // [-] - End image
        if (std::holds_alternative<ParameterLine>(Segment.Curve)) return SweptAngle(Qa - Point, Qb - Point);
        return SweepCurve(Segment.Curve, Segment.Start, Segment.End, Qa, Qb, Point, 0);
    }

    [[nodiscard]] double SegmentDistance(const CurveGeometry& Curve, double Low, double High, Vector3 Position) noexcept
    {
        const double τ = ProjectOntoCurve(Curve, Position, Low);                             // [-] - Closest parameter
        double Nearest = std::min(Distance(EvaluateCurve(Curve, Low).Position, Position), Distance(EvaluateCurve(Curve, High).Position, Position));
        if (τ >= Low && τ <= High) Nearest = std::min(Nearest, Distance(EvaluateCurve(Curve, τ).Position, Position));
        return Nearest;
    }

    // Images of a chart coordinate inside [Low − slack, High + slack] for a direction of the given period.
    [[nodiscard]] std::vector<double> EnumerateImages(double Value, double Low, double High, double Period)
    {
        if (Period <= 0.0) return { Value };
        std::vector<double> Images;
        const double Slack = 1.0e-9 * Period;                                                // [-] - Window slack
        const double First = Value - Period * std::floor((Value - Low + Slack) / Period);    // [-] - Lowest image ≥ Low − slack
        for (double Image = First; Image <= High + Slack; Image += Period) Images.push_back(Image);
        if (Images.empty()) Images.push_back(First);
        return Images;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<Vector2> SampleDomainLoop(const std::vector<DomainSegment>& Loop, int Samples)
{
    std::vector<Vector2> Polyline;
    for (const DomainSegment& Segment : Loop)
    {
        const int Count = std::holds_alternative<ParameterLine>(Segment.Curve) ? 1 : Samples;    // [-] - Pieces
        for (int Index = 0; Index < Count; ++Index)
        {
            const double τ = Segment.Start + (Segment.End - Segment.Start) * Index / Count;  // [-] - Sample parameter
            Polyline.push_back(EvaluateParameterCurve(Segment.Curve, τ).Position);
        }
    }
    return Polyline;
}

double MeasureLoopArea(const std::vector<DomainSegment>& Loop)
{
    const std::vector<Vector2> Polyline = SampleDomainLoop(Loop, 64);
    double Area = 0.0;                                                                       // [-] - Twice the signed area
    for (size_t Index = 0; Index < Polyline.size(); ++Index)
    {
        Area += Cross(Polyline[Index], Polyline[(Index + 1) % Polyline.size()]);
    }
    return 0.5 * Area;
}

void CompleteFaceDomain(FaceDomain& Domain)
{
    // ① Chart window from dense loop samples (a full period stays exactly one period).
    Vector2 Low  = {  std::numeric_limits<double>::max(),  std::numeric_limits<double>::max() };
    Vector2 High = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };
    for (const auto& Loop : Domain.Loops)
    {
        for (const Vector2& Sample : SampleDomainLoop(Loop, WindowSamples))
        {
            Low  = { std::min(Low.X, Sample.X), std::min(Low.Y, Sample.Y) };
            High = { std::max(High.X, Sample.X), std::max(High.Y, Sample.Y) };
        }
    }
    const SurfacePeriodicity Period = ResolvePeriodicity(Domain.Surface);
    const auto Widen = [](double& Lower, double& Upper, double PeriodValue)
    {
        const double Span = Upper - Lower;                                                   // [-] - Sampled span
        if (PeriodValue > 0.0 && Span >= PeriodValue - 1.0e-9) { Upper = Lower + PeriodValue; return; }
        const double Margin = 1.0e-3 * Span + 1.0e-9;                                        // [-] - Between-sample bulge
        Lower -= Margin;
        Upper += Margin;
    };
    if (std::holds_alternative<PlaneSurface>(Domain.Surface))
    {
        Low  = Low - Vector2{ 1.0e-9, 1.0e-9 };
        High = High + Vector2{ 1.0e-9, 1.0e-9 };
    }
    else
    {
        Widen(Low.X, High.X, Period.PeriodU);
        Widen(Low.Y, High.Y, Period.PeriodV);
        if (std::holds_alternative<SphereSurface>(Domain.Surface))
        {
            Low.Y  = std::max(Low.Y, -0.5 * std::numbers::pi);
            High.Y = std::min(High.Y, 0.5 * std::numbers::pi);
        }
        if (const NurbsSurface* Spline = ResolveNurbsChart(Domain.Surface))
        {
            // A NURBS patch (or an offset of one) ends at its knot domain: the window never reaches past it in an open direction.
            const NurbsDomain U = MeasureNurbsSurfaceDomain(*Spline->Definition, true), V = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
            if (Period.PeriodU <= 0.0) { Low.X = std::max(Low.X, U.Low); High.X = std::min(High.X, U.High); }
            if (Period.PeriodV <= 0.0) { Low.Y = std::max(Low.Y, V.Low); High.Y = std::min(High.Y, V.High); }
        }
    }
    Domain.Window = { Low, High };

    // ② Conservative 3D bounds: the untrimmed window patch on a grid, inflated by the chord bulge.
    Vector3 BoxLow  = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
    Vector3 BoxHigh = { -BoxLow.X, -BoxLow.Y, -BoxLow.Z };
    for (int Row = 0; Row <= BoundsGrid; ++Row)
    {
        for (int Column = 0; Column <= BoundsGrid; ++Column)
        {
            const Vector2 Parameter = { Low.X + (High.X - Low.X) * Column / BoundsGrid, Low.Y + (High.Y - Low.Y) * Row / BoundsGrid };
            const Vector3 Position  = EvaluateSurface(Domain.Surface, Parameter).Position;
            BoxLow  = { std::min(BoxLow.X, Position.X), std::min(BoxLow.Y, Position.Y), std::min(BoxLow.Z, Position.Z) };
            BoxHigh = { std::max(BoxHigh.X, Position.X), std::max(BoxHigh.Y, Position.Y), std::max(BoxHigh.Z, Position.Z) };
        }
    }
    const double Inflation = std::holds_alternative<PlaneSurface>(Domain.Surface) ? 1.0e-9 : 0.03 * Length(BoxHigh - BoxLow) + 1.0e-9;
    Domain.BoundsLow  = BoxLow - Vector3{ Inflation, Inflation, Inflation };
    Domain.BoundsHigh = BoxHigh + Vector3{ Inflation, Inflation, Inflation };
}

FaceDomain DescribeFaceDomain(const ModelPartition& Partition, FaceToken Face)
{
    const FaceProfile& Profile = Partition.Faces.Resolve(Face);
    FaceDomain Domain;
    Domain.Face    = Face;
    Domain.Surface = Partition.Surfaces.Resolve(Profile.Surface);
    Domain.Sense   = Profile.Sense;
    for (LoopToken Loop : Profile.Loops)
    {
        const LoopProfile& LoopRecord = Partition.Loops.Resolve(Loop);
        if (LoopRecord.IsolatedVertex.PresentCondition())
        {
            Domain.BoundaryPoints.push_back(Partition.Vertices.Resolve(LoopRecord.IsolatedVertex).Position);
            continue;
        }
        std::vector<DomainSegment> Segments;
        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
        {
            const FinProfile& Use = Partition.Fins.Resolve(Fin);
            Segments.push_back({ *Use.SurfaceCurve, Partition.FinParameterStart(Fin), Partition.FinParameterEnd(Fin), false });
        });
        // Virtual closures where consecutive SP-curves do not meet (singular rows).
        std::vector<DomainSegment> Closed;
        for (size_t Index = 0; Index < Segments.size(); ++Index)
        {
            const DomainSegment& Current = Segments[Index];
            const DomainSegment& Next    = Segments[(Index + 1) % Segments.size()];
            Closed.push_back(Current);
            const Vector2 Gap0 = EvaluateParameterCurve(Current.Curve, Current.End).Position;    // [-] - End of this fin
            const Vector2 Gap1 = EvaluateParameterCurve(Next.Curve, Next.Start).Position;        // [-] - Start of the next
            if (Distance(Gap0, Gap1) > ClosureGap) Closed.push_back({ ParameterLine{ Gap0, Gap1 - Gap0 }, 0.0, 1.0, true });
        }
        Domain.Loops.push_back(std::move(Closed));
    }
    for (LoopToken Loop : Profile.Loops)
    {
        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
        {
            const EdgeProfile& Edge = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
            Domain.BoundaryCurves.push_back({ Partition.Curves.Resolve(Edge.Curve), Edge.IntervalLow, Edge.IntervalHigh });
            Domain.BoundaryPoints.push_back(Partition.Vertices.Resolve(Partition.FinStart(Fin)).Position);
        });
    }
    CompleteFaceDomain(Domain);
    return Domain;
}

double MeasureParameterWinding(const FaceDomain& Domain, Vector2 Parameter)
{
    double Total = 0.0;                                                                      // [rad] - Σ swept angle
    for (const auto& Loop : Domain.Loops)
    {
        for (const DomainSegment& Segment : Loop) Total += SweepSegment(Segment, Parameter);
    }
    return Total / FullTurn;
}

PointMembership ClassifyParameterPoint(const FaceDomain& Domain, Vector2 Parameter)
{
    const SurfacePeriodicity Period = ResolvePeriodicity(Domain.Surface);
    for (double ImageU : EnumerateImages(Parameter.X, Domain.Window.Low.X, Domain.Window.High.X, Period.PeriodU))
    {
        for (double ImageV : EnumerateImages(Parameter.Y, Domain.Window.Low.Y, Domain.Window.High.Y, Period.PeriodV))
        {
            if (std::fabs(MeasureParameterWinding(Domain, { ImageU, ImageV })) > 0.5) return PointMembership::Inside;
        }
    }
    return PointMembership::Outside;
}

FacePositionClass ClassifyFacePosition(const FaceDomain& Domain, Vector3 Position, double Precision)
{
    FacePositionClass Outcome;
    const Vector2 Centre = (Domain.Window.Low + Domain.Window.High) * 0.5;                  // [-] - Projection reference
    Outcome.Parameter       = ProjectOntoSurface(Domain.Surface, Position, Centre);
    Outcome.SurfaceDistance = Distance(EvaluateSurface(Domain.Surface, Outcome.Parameter).Position, Position);
    for (const Vector3& Point : Domain.BoundaryPoints)
    {
        if (Distance(Point, Position) <= Precision) { Outcome.Membership = PointMembership::Boundary; return Outcome; }
    }
    for (const DomainBoundaryCurve& Edge : Domain.BoundaryCurves)
    {
        if (SegmentDistance(Edge.Curve, Edge.Low, Edge.High, Position) <= Precision)
        {
            Outcome.Membership = PointMembership::Boundary;
            return Outcome;
        }
    }
    if (Outcome.SurfaceDistance > Precision) return Outcome;
    Outcome.Membership = ClassifyParameterPoint(Domain, Outcome.Parameter);
    return Outcome;
}

std::optional<Vector2> LocateFaceInterior(const FaceDomain& Domain)
{
    std::vector<std::vector<Vector2>> Polylines;
    for (const auto& Loop : Domain.Loops) Polylines.push_back(SampleDomainLoop(Loop, 96));

    // Crossings of the scanline {coordinate Axis = Level} with all loop polylines, sorted along the other axis.
    const auto Crossings = [&](int Axis, double Level)
    {
        std::vector<double> Values;
        for (const auto& Polyline : Polylines)
        {
            for (size_t Index = 0; Index < Polyline.size(); ++Index)
            {
                const Vector2 First  = Polyline[Index];
                const Vector2 Second = Polyline[(Index + 1) % Polyline.size()];
                const double  A = Axis == 0 ? First.Y : First.X;                             // [-] - Scan coordinate
                const double  B = Axis == 0 ? Second.Y : Second.X;                           // [-] - Scan coordinate
                if ((A <= Level) == (B <= Level)) continue;
                const double Fraction = (Level - A) / (B - A);                               // [-] - Along the piece
                const double OtherA   = Axis == 0 ? First.X : First.Y;                       // [-] - Other coordinate
                const double OtherB   = Axis == 0 ? Second.X : Second.Y;                     // [-] - Other coordinate
                Values.push_back(OtherA + (OtherB - OtherA) * Fraction);
            }
        }
        std::sort(Values.begin(), Values.end());
        return Values;
    };
    // Half-width of the interval containing Value on the scanline (0 when Value is not between crossings).
    const auto HalfWidth = [](const std::vector<double>& Values, double Value)
    {
        for (size_t Index = 0; Index + 1 < Values.size(); ++Index)
        {
            if (Value > Values[Index] && Value < Values[Index + 1]) return std::min(Value - Values[Index], Values[Index + 1] - Value);
        }
        return 0.0;
    };

    std::optional<Vector2> Best;
    double                 BestClearance = 0.0;                                              // [len] - Smaller 3D half-width
    // ⚠️ Irrational-ish fractions: a scanline at a "nice" level (½) runs along section edges of split faces.
    const double Fractions[] = { 0.5137, 0.3219, 0.6823, 0.1741, 0.8377, 0.4291, 0.5869, 0.0713, 0.9293, 0.2531, 0.7547 };
    for (double Fraction : Fractions)
    {
        const double Level = Domain.Window.Low.Y + (Domain.Window.High.Y - Domain.Window.Low.Y) * Fraction;
        const std::vector<double> Values = Crossings(0, Level);
        for (size_t Index = 0; Index + 1 < Values.size(); ++Index)
        {
            const Vector2 Candidate = { 0.5 * (Values[Index] + Values[Index + 1]), Level };  // [-] - Interval midpoint
            const double  WidthU    = 0.5 * (Values[Index + 1] - Values[Index]);             // [-] - Chart half-width in 𝑢
            if (!(WidthU > 0.0)) continue;
            const double  WidthV    = HalfWidth(Crossings(1, Candidate.X), Candidate.Y);     // [-] - Chart half-width in 𝑣
            const SurfaceSample Sample = EvaluateSurface(Domain.Surface, Candidate);
            const double  Clearance = std::min(WidthU * Length(Sample.DerivativeU), WidthV * Length(Sample.DerivativeV));
            if (Clearance <= BestClearance) continue;
            const double Winding = MeasureParameterWinding(Domain, Candidate);              // [-] - ±1 inside, 0 outside
            if (std::fabs(Winding) < 0.5 || std::fabs(Winding - std::round(Winding)) > 1.0e-6) continue;
            Best          = Candidate;
            BestClearance = Clearance;
        }
    }
    return Best;
}

} // namespace Frontier::GeometrySpace::Parametric
