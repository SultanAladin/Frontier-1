/*====================================================================================================================================
                                                          SKETCHTRIM.CPP
====================================================================================================================================*/
// 🧩 Curve trim (Plasticity T): crossings by polyline seeds + Newton, then the picked piece comes off the sketch curve

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchTrim.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"

#include <algorithm>
#include <set>
#include <array>
#include <cmath>
#include <limits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int    SamplesPerPiece  = 24;                                                  // [-]   - Polyline samples per polynomial piece
    constexpr double MergeFraction    = 1.0e-9;                                              // [-]   - Crossings closer than this are one
    constexpr double SeedMargin       = 0.02;                                                // [-]   - Polyline crossing parameter slack
    constexpr int    NewtonIterations = 40;                                                  // [-]

    struct SampledSegment
    {
        std::vector<double>  Fractions = {};                                                 // [-]
        std::vector<Vector2> Points    = {};                                                 // [len]
    };

    // 📝 A crossing on the trimmed curve, and the curve of the same sketch that made it (−1: a foreign curve).
    struct TrimCrossing
    {
        double  Fraction    = 0.0;                                                           // [-]
        Vector2 Position    = {};                                                            // [len]
        int     SketchCurve = -1;                                                            // [-]
    };

    [[nodiscard]] bool ClosedCondition(const ProfileSegment& Segment) noexcept { return Segment.Kind == ProfileSegmentKind::Circle; }

    [[nodiscard]] double Wrap(double Fraction) noexcept
    {
        const double Wrapped = Fraction - std::floor(Fraction);
        return Wrapped >= 1.0 - MergeFraction ? 0.0 : Wrapped;
    }

    [[nodiscard]] double Settle(const ProfileSegment& Segment, double Fraction) noexcept
    {
        return ClosedCondition(Segment) ? Wrap(Fraction) : std::clamp(Fraction, 0.0, 1.0);
    }

    [[nodiscard]] SampledSegment SampleSegment(const ProfileSegment& Segment)
    {
        SampledSegment            Sampled;
        const std::vector<double> Breaks = CollectProfileBreaks(Segment);
        const int                 Steps  = Segment.Kind == ProfileSegmentKind::Line ? 1 : SamplesPerPiece;
        for (size_t Piece = 0; Piece + 1 < Breaks.size(); ++Piece)
        {
            for (int Step = Piece == 0 ? 0 : 1; Step <= Steps; ++Step)
            {
                const double Fraction = Breaks[Piece] + (Breaks[Piece + 1] - Breaks[Piece]) * Step / Steps;
                Sampled.Fractions.push_back(Fraction);
                Sampled.Points.push_back(EvaluateProfileSegment(Segment, Fraction));
            }
        }
        return Sampled;
    }

    // 💡 Newton on F(s, t) = Target(s) − Cutter(t): J = [T′(s), −C′(t)], Cramer's rule; stops at a tangency (det → 0).
    //    Returns the final gap |F|.
    [[nodiscard]] double RefineCrossing(const ProfileSegment& Target, const ProfileSegment& Cutter, double& TargetFraction, double& CutterFraction)
    {
        double Gap = std::numeric_limits<double>::infinity();
        for (int Iteration = 0; Iteration < NewtonIterations; ++Iteration)
        {
            const Vector2 Difference = EvaluateProfileSegment(Target, TargetFraction) - EvaluateProfileSegment(Cutter, CutterFraction);
            Gap = Length(Difference);
            if (Gap == 0.0) break;
            const Vector2 TargetTangent = EvaluateProfileTangent(Target, TargetFraction);
            const Vector2 CutterTangent = EvaluateProfileTangent(Cutter, CutterFraction);
            const double  Determinant   = -Cross(TargetTangent, CutterTangent);
            if (std::fabs(Determinant) <= 1.0e-14 * Length(TargetTangent) * Length(CutterTangent)) break;
            const double TargetStep =  Cross(Difference, CutterTangent) / Determinant;     // Cramer on [T′, −C′]·(Δs, Δt) = −F
            const double CutterStep = -Cross(TargetTangent, Difference) / Determinant;
            const double NextTarget = Settle(Target, TargetFraction + TargetStep);
            const double NextCutter = Settle(Cutter, CutterFraction + CutterStep);
            const bool   StillCondition = std::fabs(NextTarget - TargetFraction) <= 1.0e-16 && std::fabs(NextCutter - CutterFraction) <= 1.0e-16;
            TargetFraction = NextTarget;
            CutterFraction = NextCutter;
            if (StillCondition) break;
        }
        return Length(EvaluateProfileSegment(Target, TargetFraction) - EvaluateProfileSegment(Cutter, CutterFraction));
    }

    // 📝 Which of First / Second / Third name a curve (see SketchConstraintKind).
    [[nodiscard]] std::array<bool, 3> CurveSlots(SketchConstraintKind Kind) noexcept
    {
        switch (Kind)
        {
            case SketchConstraintKind::Horizontal:
            case SketchConstraintKind::Vertical:
            case SketchConstraintKind::Length:
            case SketchConstraintKind::Radius:
            case SketchConstraintKind::ArcRule:       return { true, false, false };
            case SketchConstraintKind::Parallel:
            case SketchConstraintKind::Perpendicular:
            case SketchConstraintKind::Angle:
            case SketchConstraintKind::EqualLength:
            case SketchConstraintKind::EqualRadius:
            case SketchConstraintKind::Tangent:
            case SketchConstraintKind::Concentric:
            case SketchConstraintKind::SplineTangent:
            case SketchConstraintKind::SplineCurvature: return { true, true, false };
            case SketchConstraintKind::SplineDirection: return { true, false, false };
            case SketchConstraintKind::PointOnSpline:
            case SketchConstraintKind::PointOnLine:
            case SketchConstraintKind::PointOnCircle:
            case SketchConstraintKind::Midpoint:      return { false, true, false };
            case SketchConstraintKind::Symmetric:     return { false, false, true };
            default:                                  return { false, false, false };
        }
    }

    // 📝 Which of First / Second / Third name a point (see SketchConstraintKind).
    [[nodiscard]] std::array<bool, 3> PointSlots(SketchConstraintKind Kind) noexcept
    {
        switch (Kind)
        {
            case SketchConstraintKind::Coincident:
            case SketchConstraintKind::Distance:
            case SketchConstraintKind::HorizontalDistance:
            case SketchConstraintKind::VerticalDistance:
            case SketchConstraintKind::Symmetric:     return { true, true, false };
            case SketchConstraintKind::Fixed:
            case SketchConstraintKind::PointOnLine:
            case SketchConstraintKind::PointOnCircle:
            case SketchConstraintKind::PointOnSpline:
            case SketchConstraintKind::Midpoint:      return { true, false, false };
            default:                                  return { false, false, false };
        }
    }

    [[nodiscard]] bool NamesCurve(const SketchConstraint& Constraint, int CurveIndex) noexcept
    {
        const std::array<bool, 3> Slots = CurveSlots(Constraint.Kind);
        return (Slots[0] && Constraint.First == CurveIndex) || (Slots[1] && Constraint.Second == CurveIndex) || (Slots[2] && Constraint.Third == CurveIndex);
    }

    // Constraint copy with every reference to From turned into To.
    [[nodiscard]] SketchConstraint Redirect(SketchConstraint Constraint, int From, int To) noexcept
    {
        const std::array<bool, 3> Slots = CurveSlots(Constraint.Kind);
        if (Slots[0] && Constraint.First  == From) Constraint.First  = To;
        if (Slots[1] && Constraint.Second == From) Constraint.Second = To;
        if (Slots[2] && Constraint.Third  == From) Constraint.Third  = To;
        Constraint.Label.clear();
        return Constraint;
    }

    // Length-type constraints a shortened line no longer meets.
    void DropLengthConstraints(SketchModel& Sketch, int CurveIndex)
    {
        std::erase_if(Sketch.Constraints, [&](const SketchConstraint& Constraint)
        {
            const bool LengthKind = Constraint.Kind == SketchConstraintKind::Length || Constraint.Kind == SketchConstraintKind::EqualLength
                                 || Constraint.Kind == SketchConstraintKind::Midpoint || Constraint.Kind == SketchConstraintKind::Symmetric;
            return LengthKind && NamesCurve(Constraint, CurveIndex);
        });
    }

    // A split curve's second part inherits the direction (lines) or radius (arcs) constraints of the first.
    void CopyShapeConstraints(SketchModel& Sketch, int CurveIndex, int NewIndex, SketchCurveKind Kind)
    {
        const size_t Count = Sketch.Constraints.size();
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const SketchConstraint Constraint = Sketch.Constraints[Index];
            if (!NamesCurve(Constraint, CurveIndex)) continue;
            const SketchConstraintKind Family = Constraint.Kind;
            const bool LineCopy = Kind == SketchCurveKind::Line && (Family == SketchConstraintKind::Horizontal || Family == SketchConstraintKind::Vertical
                               || Family == SketchConstraintKind::Parallel || Family == SketchConstraintKind::Perpendicular || Family == SketchConstraintKind::Angle);
            const bool ArcCopy  = Kind == SketchCurveKind::Arc && (Family == SketchConstraintKind::Radius || Family == SketchConstraintKind::EqualRadius);
            if (LineCopy || ArcCopy) Sketch.Constraints.push_back(Redirect(Constraint, CurveIndex, NewIndex));
        }
    }

    // A new end made by a line / circle / arc of the same sketch stays on it.
    void PinToCutter(SketchModel& Sketch, int Point, int SketchCurve)
    {
        if (SketchCurve < 0) return;
        const SketchCurveKind Kind = Sketch.Curves[SketchCurve].Kind;
        if (Kind == SketchCurveKind::Line) AppendSketchConstraint(Sketch, SketchConstraintKind::PointOnLine, Point, SketchCurve);
        else if (Kind == SketchCurveKind::Circle || Kind == SketchCurveKind::Arc) AppendSketchConstraint(Sketch, SketchConstraintKind::PointOnCircle, Point, SketchCurve);
    }

    [[nodiscard]] int NextIdentifier(const SketchModel& Sketch, SketchCurveKind Kind)
    {
        int Largest = 0;
        for (const SketchCurve& Curve : Sketch.Curves) if (Curve.Kind == Kind) Largest = std::max(Largest, Curve.Identifier);
        return Largest + 1;
    }

    [[nodiscard]] double MeasureScale(const SketchModel& Sketch, const std::vector<ProfileSegment>& Foreign)
    {
        Vector2 Low = {  std::numeric_limits<double>::infinity(),  std::numeric_limits<double>::infinity() };
        Vector2 High = { -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
        auto Grow = [&](Vector2 Point)
        {
            Low  = { std::min(Low.X, Point.X),  std::min(Low.Y, Point.Y) };
            High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) };
        };
        for (const SketchPoint& Point : Sketch.Points) Grow(Point.Position);
        for (const SketchCurve& Curve : Sketch.Curves)
        {
            if (Curve.Kind != SketchCurveKind::Circle) continue;
            const Vector2 Centre = Sketch.Points[Curve.Centre].Position;
            Grow(Centre + Vector2{ Curve.Radius, Curve.Radius });
            Grow(Centre - Vector2{ Curve.Radius, Curve.Radius });
        }
        for (const ProfileSegment& Segment : Foreign) { Grow(Segment.Start); Grow(Segment.End); }
        const double Span = High.X >= Low.X ? Distance(Low, High) : 0.0;
        return Span > 0.0 ? Span : 1.0;
    }

    // 📝 Exact spline pieces (Trim / Cut without changing the shape): a knot τ inserted p times into the clamped carrier
    //    (Boehm, Piegl & Tiller A5.1, r = 1 each) makes C(τ) a pole, and the carrier falls apart there into two clamped
    //    B-splines that trace the same points (knot insertion never moves the curve).
    [[nodiscard]] bool SameKnot(double Left, double Right, const std::vector<double>& Knots) noexcept
    {
        return std::fabs(Left - Right) <= 1.0e-12 * std::max(1.0, Knots.back() - Knots.front());
    }

    void InsertSplineKnot(SketchSplineCurve& Curve, double τ)
    {
        const int Degree = Curve.Degree;
        const int Last   = static_cast<int>(Curve.Poles.size()) - 1;                         // n
        int Span = Degree;                                                                   // k: U[k] ≤ τ < U[k + 1]
        while (Span < Last && Curve.Knots[Span + 1] <= τ) ++Span;
        int Multiplicity = 0;
        for (double Knot : Curve.Knots) if (SameKnot(Knot, τ, Curve.Knots)) ++Multiplicity;
        std::vector<Vector2> Poles(Last + 2);
        for (int Index = 0; Index <= Span - Degree; ++Index) Poles[Index] = Curve.Poles[Index];
        for (int Index = Span - Multiplicity; Index <= Last; ++Index) Poles[Index + 1] = Curve.Poles[Index];
        for (int Index = Span - Degree + 1; Index <= Span - Multiplicity; ++Index)
        {
            const double Share = (τ - Curve.Knots[Index]) / (Curve.Knots[Index + Degree] - Curve.Knots[Index]);
            Poles[Index] = Curve.Poles[Index] * Share + Curve.Poles[Index - 1] * (1.0 - Share);
        }
        Curve.Poles = std::move(Poles);
        Curve.Knots.insert(Curve.Knots.begin() + Span + 1, τ);
    }

    // The carrier on [Low, τ] and [τ, High] (τ strictly inside the domain).
    [[nodiscard]] std::pair<SketchSplineCurve, SketchSplineCurve> SplitSplineCarrier(SketchSplineCurve Curve, double τ)
    {
        const int Degree = Curve.Degree;
        for (int Count = 0;; ++Count)
        {
            int Multiplicity = 0;
            for (double Knot : Curve.Knots) if (SameKnot(Knot, τ, Curve.Knots)) ++Multiplicity;
            if (Multiplicity >= Degree || Count > Degree) break;
            InsertSplineKnot(Curve, τ);
        }
        int First = 1;                                                                       // m: first knot equal to τ
        while (First < static_cast<int>(Curve.Knots.size()) && !SameKnot(Curve.Knots[First], τ, Curve.Knots)) ++First;
        for (int Index = First; Index < First + Degree; ++Index) Curve.Knots[Index] = τ;      // Exactly τ (no 1e-12 drift)
        SketchSplineCurve Left, Right;
        Left.Degree = Right.Degree = Degree;
        Left.Poles.assign(Curve.Poles.begin(), Curve.Poles.begin() + First);
        Left.Knots.assign(Curve.Knots.begin(), Curve.Knots.begin() + First + Degree);
        Left.Knots.push_back(τ);
        Right.Poles.assign(Curve.Poles.begin() + (First - 1), Curve.Poles.end());
        Right.Knots.push_back(τ);
        Right.Knots.insert(Right.Knots.end(), Curve.Knots.begin() + First, Curve.Knots.end());
        return { std::move(Left), std::move(Right) };
    }

    // The carrier on [From, To] (Low ≤ From < To ≤ High).
    [[nodiscard]] SketchSplineCurve SplineCarrierPiece(const SketchSplineCurve& Curve, double From, double To)
    {
        SketchSplineCurve Piece = Curve;
        if (!SameKnot(To, Curve.Knots.back(), Curve.Knots)) Piece = SplitSplineCarrier(Piece, To).first;
        if (!SameKnot(From, Curve.Knots.front(), Curve.Knots)) Piece = SplitSplineCarrier(Piece, From).second;
        return Piece;
    }

    // First then Second, Second's start on First's end: one clamped B-spline with a p-fold knot at the joint (C0 there,
    // every point kept). Second's knots shift to follow First's.
    [[nodiscard]] SketchSplineCurve JoinSplineCarriers(const SketchSplineCurve& First, const SketchSplineCurve& Second)
    {
        SketchSplineCurve Joined = First;
        const int    Degree = First.Degree;
        const double Shift  = First.Knots.back() - Second.Knots.front();
        Joined.Knots.pop_back();                                                             // The joint keeps p copies
        for (size_t Index = static_cast<size_t>(Degree) + 1; Index < Second.Knots.size(); ++Index) Joined.Knots.push_back(Second.Knots[Index] + Shift);
        Joined.Poles.insert(Joined.Poles.end(), Second.Poles.begin() + 1, Second.Poles.end());
        return Joined;
    }

    // Curve CurveIndex becomes Piece as a NURBS-form spline: a sketch point per pole; Keep[0] / Keep[1] (when ≥ 0) carry
    // the first / last pole (an untouched end keeps its point and whatever is coincident with it).
    void AssignSplinePiece(SketchModel& Sketch, int CurveIndex, const SketchSplineCurve& Piece, std::array<int, 2> Keep)
    {
        std::vector<int> Points;
        for (size_t Pole = 0; Pole < Piece.Poles.size(); ++Pole)
        {
            const bool FirstPole = Pole == 0, LastPole = Pole + 1 == Piece.Poles.size();
            if (FirstPole && Keep[0] >= 0) { Points.push_back(Keep[0]); continue; }
            if (LastPole && Keep[1] >= 0) { Points.push_back(Keep[1]); continue; }
            Points.push_back(AppendSketchPoint(Sketch, Piece.Poles[Pole]));
        }
        SketchCurve& Curve = Sketch.Curves[CurveIndex];
        Curve.Kind   = SketchCurveKind::Spline;
        Curve.Form   = SketchSplineForm::Nurbs;
        Curve.Points = std::move(Points);
        Curve.Knots  = Piece.Knots;
    }

    [[nodiscard]] std::vector<Vector2> SamplePiece(const ProfileSegment& Segment, double From, double To)
    {
        std::vector<Vector2> Polyline;
        const int Steps = Segment.Kind == ProfileSegmentKind::Line ? 1 : 64;
        for (int Step = 0; Step <= Steps; ++Step) Polyline.push_back(EvaluateProfileSegment(Segment, From + (To - From) * Step / Steps));
        return Polyline;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<SegmentCrossing> IntersectProfileSegments(const ProfileSegment& Target, const ProfileSegment& Cutter, double Join)
{
    const SampledSegment TargetSamples = SampleSegment(Target);
    const SampledSegment CutterSamples = SampleSegment(Cutter);
    std::vector<SegmentCrossing> Found;
    for (size_t First = 0; First + 1 < TargetSamples.Points.size(); ++First)
    {
        const Vector2 Origin = TargetSamples.Points[First];
        const Vector2 Along  = TargetSamples.Points[First + 1] - Origin;
        for (size_t Second = 0; Second + 1 < CutterSamples.Points.size(); ++Second)
        {
            const Vector2 Other   = CutterSamples.Points[Second];
            const Vector2 Across  = CutterSamples.Points[Second + 1] - Other;
            const double  Denominator = Cross(Along, Across);
            if (std::fabs(Denominator) <= 1.0e-14 * Length(Along) * Length(Across)) continue;  // Parallel pieces: no isolated crossing
            const double TargetShare = Cross(Other - Origin, Across) / Denominator;
            const double CutterShare = Cross(Other - Origin, Along) / Denominator;
            if (TargetShare < -SeedMargin || TargetShare > 1.0 + SeedMargin || CutterShare < -SeedMargin || CutterShare > 1.0 + SeedMargin) continue;
            double TargetFraction = Settle(Target, TargetSamples.Fractions[First] + TargetShare * (TargetSamples.Fractions[First + 1] - TargetSamples.Fractions[First]));
            double CutterFraction = Settle(Cutter, CutterSamples.Fractions[Second] + CutterShare * (CutterSamples.Fractions[Second + 1] - CutterSamples.Fractions[Second]));
            if (RefineCrossing(Target, Cutter, TargetFraction, CutterFraction) > Join) continue;
            Found.push_back({ TargetFraction, CutterFraction, EvaluateProfileSegment(Target, TargetFraction) });
        }
    }
    std::sort(Found.begin(), Found.end(), [](const SegmentCrossing& Left, const SegmentCrossing& Right) { return Left.TargetFraction < Right.TargetFraction; });
    std::vector<SegmentCrossing> Merged;
    for (const SegmentCrossing& Crossing : Found)
    {
        const bool Repeat = std::any_of(Merged.begin(), Merged.end(), [&](const SegmentCrossing& Kept)
        {
            return std::fabs(Kept.TargetFraction - Crossing.TargetFraction) <= MergeFraction && Distance(Kept.Position, Crossing.Position) <= Join;
        });
        if (!Repeat) Merged.push_back(Crossing);
    }
    return Merged;
}

double LocateProfileFraction(const ProfileSegment& Segment, Vector2 Point)
{
    const SampledSegment Samples = SampleSegment(Segment);
    double Best = 0.0, BestDistance = std::numeric_limits<double>::infinity();
    for (size_t Index = 0; Index + 1 < Samples.Points.size(); ++Index)
    {
        const Vector2 Along  = Samples.Points[Index + 1] - Samples.Points[Index];
        const double  Square = Dot(Along, Along);
        const double  Share  = Square > 0.0 ? std::clamp(Dot(Point - Samples.Points[Index], Along) / Square, 0.0, 1.0) : 0.0;
        const double  Gap    = Distance(Samples.Points[Index] + Along * Share, Point);
        if (Gap < BestDistance)
        {
            BestDistance = Gap;
            Best         = Samples.Fractions[Index] + Share * (Samples.Fractions[Index + 1] - Samples.Fractions[Index]);
        }
    }
    for (int Iteration = 0; Iteration < NewtonIterations; ++Iteration)
    {
        const Vector2 Tangent = EvaluateProfileTangent(Segment, Best);
        const double  Square  = Dot(Tangent, Tangent);
        if (!(Square > 0.0)) break;
        const double Next = Settle(Segment, Best + Dot(Point - EvaluateProfileSegment(Segment, Best), Tangent) / Square);
        const bool   StillCondition = std::fabs(Next - Best) <= 1.0e-15;
        Best = Next;
        if (StillCondition) break;
    }
    return Best;
}

void EraseSketchCurve(SketchModel& Sketch, int CurveIndex)
{
    std::erase_if(Sketch.Constraints, [&](const SketchConstraint& Constraint) { return NamesCurve(Constraint, CurveIndex); });
    for (SketchConstraint& Constraint : Sketch.Constraints)
    {
        const std::array<bool, 3> Slots = CurveSlots(Constraint.Kind);
        if (Slots[0] && Constraint.First  > CurveIndex) --Constraint.First;
        if (Slots[1] && Constraint.Second > CurveIndex) --Constraint.Second;
        if (Slots[2] && Constraint.Third  > CurveIndex) --Constraint.Third;
    }
    Sketch.Curves.erase(Sketch.Curves.begin() + CurveIndex);
}

int EraseSketchPoints(SketchModel& Sketch, const std::vector<int>& Points)
{
    const std::set<int> Doomed(Points.begin(), Points.end());
    auto Uses = [&](const SketchCurve& Curve, auto&& Test)
    {
        if (Test(Curve.Start) || Test(Curve.End) || Test(Curve.Centre)) return true;
        return std::any_of(Curve.Points.begin(), Curve.Points.end(), [&](int Point) { return Test(Point); });
    };
    // ① The curves through a doomed point, last first (indices stay valid).
    int Erased = 0;
    for (int CurveIndex = static_cast<int>(Sketch.Curves.size()) - 1; CurveIndex >= 0; --CurveIndex)
    {
        if (!Uses(Sketch.Curves[CurveIndex], [&](int Point) { return Doomed.contains(Point); })) continue;
        EraseSketchCurve(Sketch, CurveIndex);
        ++Erased;
    }
    // ② Points no curve uses any more: dropped with their constraints, the rest renumbered.
    std::vector<int> Renumber(Sketch.Points.size(), -1);
    int              Kept = 0;
    for (int Point = 0; Point < static_cast<int>(Sketch.Points.size()); ++Point)
    {
        const bool Used = std::any_of(Sketch.Curves.begin(), Sketch.Curves.end(), [&](const SketchCurve& Curve)
                                      { return Uses(Curve, [&](int Other) { return Other == Point; }); });
        if (Used) Renumber[Point] = Kept++;
    }
    auto Gone = [&](int Point) { return Point < 0 || Point >= static_cast<int>(Renumber.size()) || Renumber[Point] < 0; };
    std::erase_if(Sketch.Constraints, [&](const SketchConstraint& Constraint)
    {
        const std::array<bool, 3> Slots = PointSlots(Constraint.Kind);
        return (Slots[0] && Gone(Constraint.First)) || (Slots[1] && Gone(Constraint.Second)) || (Slots[2] && Gone(Constraint.Third));
    });
    for (SketchConstraint& Constraint : Sketch.Constraints)
    {
        const std::array<bool, 3> Slots = PointSlots(Constraint.Kind);
        if (Slots[0]) Constraint.First  = Renumber[Constraint.First];
        if (Slots[1]) Constraint.Second = Renumber[Constraint.Second];
        if (Slots[2]) Constraint.Third  = Renumber[Constraint.Third];
    }
    auto Map = [&](int& Point) { if (Point >= 0) Point = Renumber[Point]; };
    for (SketchCurve& Curve : Sketch.Curves)
    {
        Map(Curve.Start);
        Map(Curve.End);
        Map(Curve.Centre);
        for (int& Point : Curve.Points) Map(Point);
    }
    std::vector<SketchPoint> Survivors;
    for (int Point = 0; Point < static_cast<int>(Sketch.Points.size()); ++Point) if (Renumber[Point] >= 0) Survivors.push_back(Sketch.Points[Point]);
    Sketch.Points = std::move(Survivors);
    return Erased;
}

SketchTrimOutcome TrimSketchCurve(const SketchModel& Sketch, int CurveIndex, Vector2 Pick, const std::vector<ProfileSegment>& Foreign)
{
    SketchTrimOutcome Outcome;
    Outcome.Sketch = Sketch;
    if (CurveIndex < 0 || CurveIndex >= static_cast<int>(Sketch.Curves.size())) { Outcome.Refusal = "the sketch has no curve " + std::to_string(CurveIndex); return Outcome; }
    const double Join = 1.0e-9 * MeasureScale(Sketch, Foreign);
    std::string  Refusal;
    const ProfileSegment Target = ResolveSketchCurveSegment(Sketch, CurveIndex, Join, Refusal);
    if (!Refusal.empty()) { Outcome.Refusal = Refusal; return Outcome; }
    // A circle, or a spline whose last point is its first (closed: first = last).
    const bool Loop = ClosedCondition(Target) || (Target.Kind == ProfileSegmentKind::Spline && Sketch.Curves[CurveIndex].Points.size() >= 2
                                                  && Sketch.Curves[CurveIndex].Points.front() == Sketch.Curves[CurveIndex].Points.back());
    const std::string Name = SketchCurveName(Sketch.Curves[CurveIndex]);

    // ① crossings with the sketch's other curves and the foreign ones, merged along the target.
    std::vector<TrimCrossing> Crossings;
    auto Gather = [&](const ProfileSegment& Cutter, int SketchCurve)
    {
        for (const SegmentCrossing& Crossing : IntersectProfileSegments(Target, Cutter, Join))
            Crossings.push_back({ Loop ? Wrap(Crossing.TargetFraction) : Crossing.TargetFraction, Crossing.Position, SketchCurve });
    };
    for (int Other = 0; Other < static_cast<int>(Sketch.Curves.size()); ++Other)
    {
        if (Other == CurveIndex) continue;
        std::string OtherRefusal;
        const ProfileSegment Cutter = ResolveSketchCurveSegment(Sketch, Other, Join, OtherRefusal);
        if (OtherRefusal.empty()) Gather(Cutter, Other);
    }
    for (const ProfileSegment& Cutter : Foreign) Gather(Cutter, -1);
    std::sort(Crossings.begin(), Crossings.end(), [](const TrimCrossing& Left, const TrimCrossing& Right) { return Left.Fraction < Right.Fraction; });
    std::vector<TrimCrossing> Distinct;
    for (const TrimCrossing& Crossing : Crossings)
    {
        if (!Distinct.empty() && Crossing.Fraction - Distinct.back().Fraction <= MergeFraction) continue;
        Distinct.push_back(Crossing);
    }
    if (Loop && Distinct.size() >= 2 && Distinct.front().Fraction + 1.0 - Distinct.back().Fraction <= MergeFraction) Distinct.pop_back();
    if (!Loop)
        std::erase_if(Distinct, [](const TrimCrossing& Crossing) { return Crossing.Fraction <= MergeFraction || Crossing.Fraction >= 1.0 - MergeFraction; });
    Outcome.Crossings = static_cast<int>(Distinct.size());
    const double Picked = LocateProfileFraction(Target, Pick);

    // ② nothing to stop at: the whole curve goes.
    const size_t Needed = Loop ? 2 : 1;
    if (Distinct.size() < Needed)
    {
        Outcome.FromFraction = 0.0;
        Outcome.ToFraction   = 1.0;
        Outcome.Removed      = SamplePiece(Target, 0.0, 1.0);
        Outcome.Pieces       = 0;
        EraseSketchCurve(Outcome.Sketch, CurveIndex);
        return Outcome;
    }
    if (Target.Kind == ProfileSegmentKind::Spline)
    {
        // ③′ a spline (Hermite, Bezier, NURBS, cubic): the kept pieces are exact pieces of its B-spline carrier (knot
        //    insertion, the shape untouched), each a NURBS-form curve; constraints on the curve let go (their slots
        //    change), an untouched end keeps its point.
        SketchModel&             Edited  = Outcome.Sketch;
        const SketchSplineCurve& Carrier = Target.Spline;
        const double Low = Carrier.Knots.front(), High = Carrier.Knots.back();
        const auto   At  = [&](double Fraction) { return Low + Fraction * (High - Low); };
        const SketchCurve Original = Edited.Curves[CurveIndex];
        const int StartSlot = Original.Points.empty() ? -1 : Original.Points.front();
        const int EndSlot   = Original.Points.empty() ? -1 : Original.Points.back();
        std::erase_if(Edited.Constraints, [&](const SketchConstraint& Constraint) { return NamesCurve(Constraint, CurveIndex); });
        int Lower = -1, Upper = -1;
        if (Loop)
        {
            Lower = static_cast<int>(Distinct.size()) - 1;
            for (int Index = 0; Index < static_cast<int>(Distinct.size()); ++Index) if (Distinct[Index].Fraction <= Picked) Lower = Index;
            Upper = (Lower + 1) % static_cast<int>(Distinct.size());
            const bool WrapCondition = Upper == 0;                                           // The removed piece crosses the seam
            Outcome.FromFraction = Distinct[Lower].Fraction;
            Outcome.ToFraction   = Distinct[Upper].Fraction + (WrapCondition ? 1.0 : 0.0);
            Outcome.Removed      = WrapCondition ? SamplePiece(Target, Outcome.FromFraction, 1.0) : SamplePiece(Target, Outcome.FromFraction, Outcome.ToFraction);
            if (WrapCondition) { const std::vector<Vector2> Tail = SamplePiece(Target, 0.0, Distinct[Upper].Fraction); Outcome.Removed.insert(Outcome.Removed.end(), Tail.begin() + 1, Tail.end()); }
            const SketchSplineCurve Kept = WrapCondition ? SplineCarrierPiece(Carrier, At(Distinct[Upper].Fraction), At(Distinct[Lower].Fraction))
                                                         : JoinSplineCarriers(SplineCarrierPiece(Carrier, At(Distinct[Upper].Fraction), High),
                                                                              SplineCarrierPiece(Carrier, Low, At(Distinct[Lower].Fraction)));
            AssignSplinePiece(Edited, CurveIndex, Kept, { -1, -1 });
            Outcome.Pieces = 1;
        }
        else
        {
            for (int Index = 0; Index < static_cast<int>(Distinct.size()); ++Index)
            {
                if (Distinct[Index].Fraction <= Picked) Lower = Index;
                else { Upper = Index; break; }
            }
            Outcome.FromFraction = Lower < 0 ? 0.0 : Distinct[Lower].Fraction;
            Outcome.ToFraction   = Upper < 0 ? 1.0 : Distinct[Upper].Fraction;
            Outcome.Removed      = SamplePiece(Target, Outcome.FromFraction, Outcome.ToFraction);
            if (Lower < 0) AssignSplinePiece(Edited, CurveIndex, SplineCarrierPiece(Carrier, At(Distinct[Upper].Fraction), High), { -1, EndSlot });
            else if (Upper < 0) AssignSplinePiece(Edited, CurveIndex, SplineCarrierPiece(Carrier, Low, At(Distinct[Lower].Fraction)), { StartSlot, -1 });
            else
            {
                AssignSplinePiece(Edited, CurveIndex, SplineCarrierPiece(Carrier, Low, At(Distinct[Lower].Fraction)), { StartSlot, -1 });
                SketchCurve Second = Original;
                Second.Identifier  = NextIdentifier(Edited, SketchCurveKind::Spline);
                Edited.Curves.push_back(Second);
                AssignSplinePiece(Edited, static_cast<int>(Edited.Curves.size()) - 1, SplineCarrierPiece(Carrier, At(Distinct[Upper].Fraction), High), { -1, EndSlot });
            }
            Outcome.Pieces = Lower >= 0 && Upper >= 0 ? 2 : 1;
        }
        // New ends made by a line / circle / arc of the same sketch stay on it; points no curve uses any more go.
        const auto PinEnd = [&](int CurveSlot, bool LastCondition, int Crossing)
        {
            if (Crossing < 0) return;
            const SketchCurve& Piece = Edited.Curves[CurveSlot];
            PinToCutter(Edited, LastCondition ? Piece.Points.back() : Piece.Points.front(), Distinct[Crossing].SketchCurve);
        };
        if (Loop) { PinEnd(CurveIndex, false, Upper); PinEnd(CurveIndex, true, Lower); }
        else if (Lower < 0) PinEnd(CurveIndex, false, Upper);
        else if (Upper < 0) PinEnd(CurveIndex, true, Lower);
        else { PinEnd(CurveIndex, true, Lower); PinEnd(static_cast<int>(Edited.Curves.size()) - 1, false, Upper); }
        (void)EraseSketchPoints(Edited, {});
        return Outcome;
    }

    // ③ the piece holding the pick: [Lower, Upper] between neighbouring crossings (or the curve ends).
    SketchModel& Edited = Outcome.Sketch;
    if (Loop)
    {
        size_t Lower = Distinct.size() - 1;
        for (size_t Index = 0; Index < Distinct.size(); ++Index) if (Distinct[Index].Fraction <= Picked) Lower = Index;
        const size_t Upper = (Lower + 1) % Distinct.size();
        const bool   WrapCondition = Upper == 0;
        Outcome.FromFraction = Distinct[Lower].Fraction;
        Outcome.ToFraction   = Distinct[Upper].Fraction + (WrapCondition ? 1.0 : 0.0);
        Outcome.Removed      = SamplePiece(Target, Outcome.FromFraction, Outcome.ToFraction);
        const int StartPoint = AppendSketchPoint(Edited, Distinct[Upper].Position);          // The arc runs counter-clockwise
        const int EndPoint   = AppendSketchPoint(Edited, Distinct[Lower].Position);          // from the removed end to its start
        SketchCurve& Curve = Edited.Curves[CurveIndex];
        Curve.Kind  = SketchCurveKind::Arc;
        Curve.Start = StartPoint;
        Curve.End   = EndPoint;
        PinToCutter(Edited, StartPoint, Distinct[Upper].SketchCurve);
        PinToCutter(Edited, EndPoint, Distinct[Lower].SketchCurve);
        Outcome.Pieces = 1;
        return Outcome;
    }
    int Lower = -1, Upper = -1;                                                              // Crossing indices (−1: curve end)
    for (int Index = 0; Index < static_cast<int>(Distinct.size()); ++Index)
    {
        if (Distinct[Index].Fraction <= Picked) Lower = Index;
        else { Upper = Index; break; }
    }
    Outcome.FromFraction = Lower < 0 ? 0.0 : Distinct[Lower].Fraction;
    Outcome.ToFraction   = Upper < 0 ? 1.0 : Distinct[Upper].Fraction;
    Outcome.Removed      = SamplePiece(Target, Outcome.FromFraction, Outcome.ToFraction);
    const SketchCurveKind Kind = Edited.Curves[CurveIndex].Kind;
    if (Kind == SketchCurveKind::Line) DropLengthConstraints(Edited, CurveIndex);
    if (Lower < 0)                                                                           // The start piece comes off
    {
        const int Point = AppendSketchPoint(Edited, Distinct[Upper].Position);
        Edited.Curves[CurveIndex].Start = Point;
        PinToCutter(Edited, Point, Distinct[Upper].SketchCurve);
        Outcome.Pieces = 1;
        return Outcome;
    }
    if (Upper < 0)                                                                           // The end piece comes off
    {
        const int Point = AppendSketchPoint(Edited, Distinct[Lower].Position);
        Edited.Curves[CurveIndex].End = Point;
        PinToCutter(Edited, Point, Distinct[Lower].SketchCurve);
        Outcome.Pieces = 1;
        return Outcome;
    }
    const int FirstEnd    = AppendSketchPoint(Edited, Distinct[Lower].Position);             // A middle piece: split in two
    const int SecondStart = AppendSketchPoint(Edited, Distinct[Upper].Position);
    SketchCurve Second = Edited.Curves[CurveIndex];
    Second.Identifier  = NextIdentifier(Edited, Kind);
    Second.Start       = SecondStart;
    Edited.Curves[CurveIndex].End = FirstEnd;
    Edited.Curves.push_back(Second);
    const int SecondIndex = static_cast<int>(Edited.Curves.size()) - 1;
    CopyShapeConstraints(Edited, CurveIndex, SecondIndex, Kind);
    PinToCutter(Edited, FirstEnd, Distinct[Lower].SketchCurve);
    PinToCutter(Edited, SecondStart, Distinct[Upper].SketchCurve);
    Outcome.Pieces = 2;
    return Outcome;
}

SketchTrimOutcome SplitSketchCurve(const SketchModel& Sketch, int CurveIndex, Vector2 Pick, std::optional<Vector2> Partner)
{
    constexpr double EndGuard = 1.0e-3;                                                      // [-] - No split this close to an open end
    SketchTrimOutcome Outcome;
    Outcome.Sketch = Sketch;
    if (CurveIndex < 0 || CurveIndex >= static_cast<int>(Sketch.Curves.size())) { Outcome.Refusal = "the sketch has no curve " + std::to_string(CurveIndex); return Outcome; }
    const double Join = 1.0e-9 * MeasureScale(Sketch, {});
    std::string  Refusal;
    const ProfileSegment Target = ResolveSketchCurveSegment(Sketch, CurveIndex, Join, Refusal);
    if (!Refusal.empty()) { Outcome.Refusal = Refusal; return Outcome; }
    // A circle, or a spline whose last point is its first (closed: first = last).
    const bool Loop = ClosedCondition(Target) || (Target.Kind == ProfileSegmentKind::Spline && Sketch.Curves[CurveIndex].Points.size() >= 2
                                                  && Sketch.Curves[CurveIndex].Points.front() == Sketch.Curves[CurveIndex].Points.back());
    const std::string Name = SketchCurveName(Sketch.Curves[CurveIndex]);
    const double Picked = LocateProfileFraction(Target, Pick);
    if (Target.Kind == ProfileSegmentKind::Spline)
    {
        // A spline: two exact pieces of its carrier meeting at the pick (closed: at the pick and the partner, else the
        // pick and the point half the parameter range on); the shape is untouched.
        if (!Loop && (Picked <= EndGuard || Picked >= 1.0 - EndGuard))
        {
            Outcome.Refusal = "the pointer is on an end of " + Name + ": Cut needs a point inside the curve";
            return Outcome;
        }
        SketchModel&             Edited  = Outcome.Sketch;
        const SketchSplineCurve& Carrier = Target.Spline;
        const double Low = Carrier.Knots.front(), High = Carrier.Knots.back();
        const auto   At  = [&](double Fraction) { return Low + Fraction * (High - Low); };
        const SketchCurve Original = Edited.Curves[CurveIndex];
        std::erase_if(Edited.Constraints, [&](const SketchConstraint& Constraint) { return NamesCurve(Constraint, CurveIndex); });
        Outcome.FromFraction = Outcome.ToFraction = Picked;
        SketchCurve Second = Original;
        Second.Identifier  = NextIdentifier(Edited, SketchCurveKind::Spline);
        Edited.Curves.push_back(Second);
        const int SecondIndex = static_cast<int>(Edited.Curves.size()) - 1;
        if (Loop)
        {
            double Across = 0.5;
            if (Partner.has_value())
            {
                const double Gap = Wrap(LocateProfileFraction(Target, *Partner) - Picked);
                if (Gap > EndGuard && Gap < 1.0 - EndGuard) Across = Gap;
            }
            const double First = Wrap(Picked), Other = Wrap(Picked + Across);
            const double From = std::min(First, Other), To = std::max(First, Other);
            const auto   Inner = [&](double Left, double Right) { return SplineCarrierPiece(Carrier, At(Left), At(Right)); };
            const SketchSplineCurve Middle = From <= EndGuard ? Inner(0.0, To) : Inner(From, To);
            const SketchSplineCurve Rest   = From <= EndGuard ? SplineCarrierPiece(Carrier, At(To), High)
                                                              : JoinSplineCarriers(SplineCarrierPiece(Carrier, At(To), High), Inner(0.0, From));
            AssignSplinePiece(Edited, CurveIndex, Middle, { -1, -1 });
            AssignSplinePiece(Edited, SecondIndex, Rest, { Edited.Curves[CurveIndex].Points.back(), Edited.Curves[CurveIndex].Points.front() });
            Outcome.SplitPositions = { EvaluateProfileSegment(Target, From), EvaluateProfileSegment(Target, To) };
        }
        else
        {
            const auto [Left, Right] = SplitSplineCarrier(Carrier, At(Picked));
            AssignSplinePiece(Edited, CurveIndex, Left, { Original.Points.front(), -1 });
            AssignSplinePiece(Edited, SecondIndex, Right, { Edited.Curves[CurveIndex].Points.back(), Original.Points.back() });
            Outcome.SplitPositions = { EvaluateProfileSegment(Target, Picked) };
        }
        (void)EraseSketchPoints(Edited, {});
        Outcome.Pieces = 2;
        return Outcome;
    }
    Outcome.FromFraction = Picked;
    Outcome.ToFraction   = Picked;
    SketchModel& Edited = Outcome.Sketch;
    if (Loop)
    {
        // ① a circle: two arcs (counter-clockwise like the circle) meeting at the pick and the partner (a second
        //    crossing), else half arcs meeting at the pick and its opposite point.
        double Across = 0.5;                                                                 // [-] - Second break, fraction past the pick
        if (Partner.has_value())
        {
            const double Gap = Wrap(LocateProfileFraction(Target, *Partner) - Picked);
            if (Gap > EndGuard && Gap < 1.0 - EndGuard) Across = Gap;
        }
        const Vector2 First  = EvaluateProfileSegment(Target, Wrap(Picked));
        const Vector2 Second = EvaluateProfileSegment(Target, Wrap(Picked + Across));
        const int     FirstPoint  = AppendSketchPoint(Edited, First);
        const int     SecondPoint = AppendSketchPoint(Edited, Second);
        SketchCurve& Curve = Edited.Curves[CurveIndex];
        Curve.Kind  = SketchCurveKind::Arc;
        Curve.Start = FirstPoint;
        Curve.End   = SecondPoint;
        SketchCurve Other = Curve;
        Other.Identifier  = NextIdentifier(Edited, SketchCurveKind::Arc);
        Other.Start       = SecondPoint;
        Other.End         = FirstPoint;
        Edited.Curves.push_back(Other);
        CopyShapeConstraints(Edited, CurveIndex, static_cast<int>(Edited.Curves.size()) - 1, SketchCurveKind::Arc);
        Outcome.SplitPositions = { First, Second };
        Outcome.Pieces         = 2;
        return Outcome;
    }
    if (Picked <= EndGuard || Picked >= 1.0 - EndGuard)
    {
        Outcome.Refusal = "the pointer is on an end of " + Name + ": Cut needs a point inside the curve";
        return Outcome;
    }

    // ② a line or an arc: the curve keeps its start part, a new curve carries the rest; both share the new point.
    const Vector2         Position = EvaluateProfileSegment(Target, Picked);
    const SketchCurveKind Kind     = Edited.Curves[CurveIndex].Kind;
    if (Kind == SketchCurveKind::Line) DropLengthConstraints(Edited, CurveIndex);
    const int   Point  = AppendSketchPoint(Edited, Position);
    SketchCurve Second = Edited.Curves[CurveIndex];
    Second.Identifier  = NextIdentifier(Edited, Kind);
    Second.Start       = Point;
    Edited.Curves[CurveIndex].End = Point;
    Edited.Curves.push_back(Second);
    CopyShapeConstraints(Edited, CurveIndex, static_cast<int>(Edited.Curves.size()) - 1, Kind);
    Outcome.SplitPositions = { Position };
    Outcome.Pieces         = 2;
    return Outcome;
}


SketchTrimOutcome InsertSketchCurvePoint(const SketchModel& Sketch, int CurveIndex, Vector2 Pick)
{
    constexpr double PointGuard = 1.0e-3;                                                    // [-] - Of a span: no new point this close to one
    SketchTrimOutcome Outcome;
    Outcome.Sketch = Sketch;
    if (CurveIndex < 0 || CurveIndex >= static_cast<int>(Sketch.Curves.size())) { Outcome.Refusal = "the sketch has no curve " + std::to_string(CurveIndex); return Outcome; }
    const SketchCurve& Original = Sketch.Curves[CurveIndex];
    const std::string  Name     = SketchCurveName(Original);

    // ① lines, arcs, circles: Cut's split, the line's direction constraints let go.
    if (Original.Kind != SketchCurveKind::Spline)
    {
        const size_t PointsBefore = Sketch.Points.size();
        Outcome = SplitSketchCurve(Sketch, CurveIndex, Pick);
        if (!Outcome.Refusal.empty())
        {
            if (Outcome.Refusal.find("on an end") != std::string::npos) Outcome.Refusal = "the pointer is on an end of " + Name + ": there is a point already";
            return Outcome;
        }
        Outcome.InsertedPoint = static_cast<int>(PointsBefore);                              // The first point the split appended
        if (Original.Kind == SketchCurveKind::Line)
        {
            const int Second = static_cast<int>(Outcome.Sketch.Curves.size()) - 1;
            std::erase_if(Outcome.Sketch.Constraints, [&](const SketchConstraint& Constraint)
            {
                using Kind = SketchConstraintKind;
                const bool Direction = Constraint.Kind == Kind::Horizontal || Constraint.Kind == Kind::Vertical || Constraint.Kind == Kind::Parallel
                                    || Constraint.Kind == Kind::Perpendicular || Constraint.Kind == Kind::Angle;
                const bool Touches   = Constraint.First == CurveIndex || Constraint.First == Second || Constraint.Second == CurveIndex || Constraint.Second == Second;
                return Direction && Touches;
            });
        }
        return Outcome;
    }

    // ② splines: τ* nearest to Pick on the exact B-spline (span samples, then Newton on (C − P)·C′ = 0).
    const SketchSplineOutcome Resolved = ResolveSketchCurveSpline(Sketch, Original);
    if (!Resolved.Refusal.empty()) { Outcome.Refusal = Resolved.Refusal; return Outcome; }
    const SketchSplineCurve&  Carrier = Resolved.Curve;
    const std::vector<double> Breaks  = CollectSketchSplineBreaks(Carrier);
    double Best = Breaks.front(), BestGap = std::numeric_limits<double>::infinity();
    for (size_t Piece = 0; Piece + 1 < Breaks.size(); ++Piece)
        for (int Sample = 0; Sample <= 32; ++Sample)
        {
            const double Tau = Breaks[Piece] + (Breaks[Piece + 1] - Breaks[Piece]) * Sample / 32.0;
            const double Gap = Distance(EvaluateSketchSpline(Carrier, Tau).Position, Pick);
            if (Gap < BestGap) { BestGap = Gap; Best = Tau; }
        }
    for (int Step = 0; Step < 12; ++Step)
    {
        const SketchSplineDerivatives At = EvaluateSketchSpline(Carrier, Best);
        const Vector2 Offset = At.Position - Pick;
        const double  Slope  = Dot(At.First, At.First) + Dot(Offset, At.Second);
        if (!(std::fabs(Slope) > 0.0)) break;
        Best = std::clamp(Best - Dot(Offset, At.First) / Slope, Breaks.front(), Breaks.back());
    }
    const Vector2 Position = EvaluateSketchSpline(Carrier, Best).Position;
    SketchModel&  Edited   = Outcome.Sketch;
    SketchCurve&  Curve    = Edited.Curves[CurveIndex];
    const int     Count    = static_cast<int>(Curve.Points.size());

    if (Original.Form == SketchSplineForm::CubicSpline)
    {
        // ③ cubic spline: a through-point at τ*, its parameter kept beside the others.
        std::vector<double> Parameters = Curve.Knots;
        if (Parameters.empty()) for (int Index = 0; Index < Count; ++Index) Parameters.push_back(static_cast<double>(Index));
        int Span = 0;
        while (Span + 2 < Count && Parameters[Span + 1] <= Best) ++Span;
        const double Width = Parameters[Span + 1] - Parameters[Span];
        if (Best - Parameters[Span] <= PointGuard * Width || Parameters[Span + 1] - Best <= PointGuard * Width)
        {
            Outcome.Refusal = "there is a point of " + Name + " there already";
            return Outcome;
        }
        const int Point = AppendSketchPoint(Edited, Position);
        Edited.Curves[CurveIndex].Points.insert(Edited.Curves[CurveIndex].Points.begin() + Span + 1, Point);
        Parameters.insert(Parameters.begin() + Span + 1, Best);
        Edited.Curves[CurveIndex].Knots = std::move(Parameters);
        Outcome.InsertedPoint  = Point;
        Outcome.SplitPositions = { Position };
        Outcome.Pieces         = 1;
        return Outcome;
    }

    // ④ Hermite / Bezier / NURBS: control points + knots of the exact B-spline (Hermite: its handles become points),
    //    then one Boehm insertion (Piegl & Tiller A5.1, r = 1) of τ*.
    const int Degree = Carrier.Degree;
    std::vector<int> Slots;                                                                  // Sketch point per carrier pole
    if (Original.Form == SketchSplineForm::Hermite)
    {
        for (size_t Pole = 0; Pole < Carrier.Poles.size(); ++Pole)
            Slots.push_back(Pole % 3 == 0 ? Curve.Points[Pole / 3] : AppendSketchPoint(Edited, Carrier.Poles[Pole]));
    }
    else Slots = Curve.Points;
    const std::vector<double>& Knots = Carrier.Knots;
    const int Last = static_cast<int>(Carrier.Poles.size()) - 1;                            // n
    int Span = Degree;                                                                       // k: Knots[k] ≤ τ* < Knots[k + 1]
    while (Span < Last && Knots[Span + 1] <= Best) ++Span;
    int Multiplicity = 0;
    for (double Knot : Knots) if (std::fabs(Knot - Best) <= 1.0e-12 * std::max(1.0, std::fabs(Knots.back() - Knots.front()))) ++Multiplicity;
    if (Multiplicity > 0) Best = Knots[Span];
    const double Range = Knots.back() - Knots.front();
    if (Best - Knots.front() <= PointGuard * Range * 1.0e-2 || Knots.back() - Best <= PointGuard * Range * 1.0e-2)
    {
        Outcome.Refusal = "the pointer is on an end of " + Name + ": there is a point already";
        return Outcome;
    }
    if (Multiplicity >= Degree)
    {
        Outcome.Refusal = Name + " already has a corner there (a knot repeated " + std::to_string(Multiplicity) + " times)";
        return Outcome;
    }
    std::vector<Vector2> Poles(Last + 2);
    for (int Index = 0; Index <= Span - Degree; ++Index) Poles[Index] = Carrier.Poles[Index];
    for (int Index = Span - Multiplicity; Index <= Last; ++Index) Poles[Index + 1] = Carrier.Poles[Index];
    for (int Index = Span - Degree + 1; Index <= Span - Multiplicity; ++Index)
    {
        const double Share = (Best - Knots[Index]) / (Knots[Index + Degree] - Knots[Index]);
        Poles[Index] = Carrier.Poles[Index] * Share + Carrier.Poles[Index - 1] * (1.0 - Share);
    }
    std::vector<double> Refined = Knots;
    Refined.insert(Refined.begin() + Span + 1, Best);
    // Points: the untouched poles keep theirs, the moved ones Span − p + 1 … Span − s − 1 keep theirs at the new place,
    // pole Span − s is the new point.
    std::vector<int> Points(Last + 2, -1);
    for (int Index = 0; Index <= Span - Degree; ++Index) Points[Index] = Slots[Index];
    for (int Index = Span - Degree + 1; Index < Span - Multiplicity; ++Index) Points[Index] = Slots[Index];
    for (int Index = Span - Multiplicity; Index <= Last; ++Index) Points[Index + 1] = Slots[Index];
    Points[Span - Multiplicity] = AppendSketchPoint(Edited, Poles[Span - Multiplicity]);
    for (int Index = Span - Degree + 1; Index < Span - Multiplicity; ++Index) Edited.Points[Points[Index]].Position = Poles[Index];
    Edited.Curves[CurveIndex].Form   = SketchSplineForm::Nurbs;
    Edited.Curves[CurveIndex].Points = Points;
    Edited.Curves[CurveIndex].Knots  = std::move(Refined);
    Outcome.InsertedPoint  = Points[Span - Multiplicity];
    Outcome.SplitPositions = { Position };
    Outcome.Pieces         = 1;
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
