/*====================================================================================================================================
                                                      SKETCHCONSTRAINING.CPP
====================================================================================================================================*/
// 🧩 Interactive sketch constraining: verb + picks → constraint, dimension measuring, names, pinned point drag

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchConstraining.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Kind = SketchConstraintCategory;

    // 📝 The picks sorted by what they are: points, lines, circles (circles and arcs), splines — each in pick order.
    struct SortedPicks
    {
        std::vector<int> Points  = {};                                                       // [-] - Point indices
        std::vector<int> Lines   = {};                                                       // [-] - Curve indices
        std::vector<int> Circles = {};                                                       // [-] - Curve indices (circle or arc)
        std::vector<int> Splines = {};                                                       // [-] - Curve indices
        std::string      Refusal = {};                                                       // [-] - Out of range / repeated
    };

    [[nodiscard]] SortedPicks SortPicks(const SketchModel& Sketch, const std::vector<SketchPick>& Picks)
    {
        SortedPicks Sorted;
        for (const SketchPick& Pick : Picks)
        {
            if (Pick.Kind == SketchPickCategory::Point)
            {
                if (Pick.Index < 0 || Pick.Index >= static_cast<int>(Sketch.Points.size())) { Sorted.Refusal = "a picked point is not in the sketch"; return Sorted; }
                if (std::find(Sorted.Points.begin(), Sorted.Points.end(), Pick.Index) != Sorted.Points.end()) { Sorted.Refusal = "the same point is picked twice"; return Sorted; }
                Sorted.Points.push_back(Pick.Index);
                continue;
            }
            if (Pick.Index < 0 || Pick.Index >= static_cast<int>(Sketch.Curves.size())) { Sorted.Refusal = "a picked curve is not in the sketch"; return Sorted; }
            std::vector<int>& Bucket = Sketch.Curves[Pick.Index].Kind == SketchCurveCategory::Line ? Sorted.Lines
                                     : Sketch.Curves[Pick.Index].Kind == SketchCurveCategory::Spline ? Sorted.Splines : Sorted.Circles;
            if (std::find(Bucket.begin(), Bucket.end(), Pick.Index) != Bucket.end()) { Sorted.Refusal = "the same curve is picked twice"; return Sorted; }
            Bucket.push_back(Pick.Index);
        }
        return Sorted;
    }

    [[nodiscard]] bool Pattern(const SortedPicks& Sorted, size_t Points, size_t Lines, size_t Circles, size_t Splines = 0) noexcept
    {
        return Sorted.Points.size() == Points && Sorted.Lines.size() == Lines && Sorted.Circles.size() == Circles && Sorted.Splines.size() == Splines;
    }

    // 📝 Where a spline meets another curve: the spline end (point index) and, for a second spline, which of its ends
    //    (0 first · 1 last). Ends meet when they are the same point or lie within 10⁻⁶ of the sketch size; a full circle
    //    is met by a spline end on it.
    struct SplineJoint
    {
        int         End       = -1;                                                          // [-] - Spline end point index
        double      OtherEnd  = 0.0;                                                         // [-] - Second spline: 0 first · 1 last
        std::string Refusal   = {};                                                          // [-] - Why they do not meet
    };

    [[nodiscard]] SplineJoint JoinSpline(const SketchModel& Sketch, int Spline, int Other)
    {
        SplineJoint  Joint;
        const auto&  Curve = Sketch.Curves[Spline];
        const auto&  Shape = Sketch.Curves[Other];
        const auto   Position = [&](int Index) { return Sketch.Points[Index].Position; };
        if (Curve.Points.size() < 2 || Curve.Points.front() == Curve.Points.back())
        {
            Joint.Refusal = SketchCurveName(Curve) + " is closed: it has no end";
            return Joint;
        }
        Vector2 Low{ 1e300, 1e300 }, High{ -1e300, -1e300 };
        for (const SketchPoint& Point : Sketch.Points)
        {
            Low  = { std::min(Low.X, Point.Position.X), std::min(Low.Y, Point.Position.Y) };
            High = { std::max(High.X, Point.Position.X), std::max(High.Y, Point.Position.Y) };
        }
        const double Join = 1.0e-6 * std::max(1.0, Distance(Low, High));
        double Best = 1e300;
        for (int End : { Curve.Points.front(), Curve.Points.back() })
        {
            if (Shape.Kind == SketchCurveCategory::Circle)
            {
                const double Gap = std::fabs(Distance(Position(End), Position(Shape.Centre)) - Shape.Radius);
                if (Gap < Best) { Best = Gap; Joint.End = End; }
                continue;
            }
            std::vector<std::pair<int, double>> Ends;
            if (Shape.Kind == SketchCurveCategory::Spline)
            {
                if (Shape.Points.size() < 2 || Shape.Points.front() == Shape.Points.back()) { Joint.Refusal = SketchCurveName(Shape) + " is closed: it has no end"; return Joint; }
                Ends = { { Shape.Points.front(), 0.0 }, { Shape.Points.back(), 1.0 } };
            }
            else Ends = { { Shape.Start, 0.0 }, { Shape.End, 1.0 } };
            for (const auto& [Point, Which] : Ends)
            {
                const double Gap = Point == End ? 0.0 : Distance(Position(End), Position(Point));
                if (Gap < Best) { Best = Gap; Joint.End = End; Joint.OtherEnd = Which; }
            }
        }
        if (Best > Join)
        {
            Joint.End     = -1;
            Joint.Refusal = SketchCurveName(Curve) + " and " + SketchCurveName(Shape) + " do not meet at an end (O their ends together first)";
        }
        return Joint;
    }

    [[nodiscard]] bool EndpointCondition(const SketchCurve& Curve, int Point) noexcept
    {
        return Point == Curve.Start || Point == Curve.End || Point == Curve.Centre;
    }

    [[nodiscard]] Vector2 LineVector(const SketchModel& Sketch, int Curve) noexcept
    {
        const SketchCurve& Line = Sketch.Curves[Curve];
        return Sketch.Points[Line.End].Position - Sketch.Points[Line.Start].Position;
    }

    [[nodiscard]] std::string FormatNumber(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof(Text), "%.6g", std::abs(Value) < 5.0e-13 ? 0.0 : Value);
        return Text;
    }

    [[nodiscard]] SketchConstraint Make(Kind Constraint, int First, int Second = -1, int Third = -1, double Value = 0.0, double SecondValue = 0.0)
    {
        SketchConstraint Made;
        Made.Kind        = Constraint;
        Made.First       = First;
        Made.Second      = Second;
        Made.Third       = Third;
        Made.Value       = Value;
        Made.SecondValue = SecondValue;
        return Made;
    }

    [[nodiscard]] SketchConstraintProposal Refuse(SketchConstraintVerb Verb, const std::string& Reason)
    {
        SketchConstraintProposal Proposal;
        Proposal.Refusal = std::string(SketchConstraintVerbLabel(Verb)) + ": " + Reason;
        return Proposal;
    }

    [[nodiscard]] SketchConstraintProposal Accept(SketchConstraint Constraint)
    {
        SketchConstraintProposal Proposal;
        Proposal.Constraint = std::move(Constraint);
        return Proposal;
    }

    // References per kind: which of First / Second / Third are points (true) and which curves (false); -1 slots unused.
    struct ReferenceLayout
    {
        int  Count     = 0;                                                                  // [-] - Used slots
        bool Points[3] = { false, false, false };                                            // [-] - Slot is a point
    };

    [[nodiscard]] ReferenceLayout LayoutOf(Kind Constraint) noexcept
    {
        switch (Constraint)
        {
        case Kind::Coincident:
        case Kind::Distance:
        case Kind::HorizontalDistance:
        case Kind::VerticalDistance:   return { 2, { true, true, false } };
        case Kind::Fixed:              return { 1, { true, false, false } };
        case Kind::Horizontal:
        case Kind::Vertical:
        case Kind::Length:
        case Kind::Radius:
        case Kind::ArcRule:            return { 1, { false, false, false } };
        case Kind::Parallel:
        case Kind::Perpendicular:
        case Kind::Angle:
        case Kind::EqualLength:
        case Kind::EqualRadius:
        case Kind::Tangent:
        case Kind::Concentric:         return { 2, { false, false, false } };
        case Kind::PointOnLine:
        case Kind::PointOnCircle:
        case Kind::Midpoint:           return { 2, { true, false, false } };
        case Kind::Symmetric:          return { 3, { true, true, false } };
        case Kind::SplineTangent:
        case Kind::SplineCurvature:    return { 2, { false, false, false } };
        case Kind::PointOnSpline:      return { 2, { true, false, false } };
        case Kind::SplineDirection:    return { 1, { false, false, false } };
        }
        return {};
    }

    // 📝 A fillet arc (tangent to a line at both ends) turns less than π. A drag that solves to the far root, the same
    //    tangent circle swept the long way, has turned the fillet inside out: the sign of (start − centre) × (end − centre)
    //    flips. Returns the first such arc of After (−1: none); arcs at π (slot ends) carry no sign and are skipped.
    [[nodiscard]] int InvertedFillet(const SketchModel& Before, const SketchModel& After)
    {
        std::vector<int> TangentEnds(Before.Curves.size(), 0);
        for (const SketchConstraint& Constraint : Before.Constraints)
        {
            if (Constraint.Kind != Kind::Tangent || Constraint.First < 0 || Constraint.Second < 0) continue;
            if (Constraint.First >= int(Before.Curves.size()) || Constraint.Second >= int(Before.Curves.size())) continue;
            const SketchCurve& Line = Before.Curves[Constraint.First];
            const SketchCurve& Arc  = Before.Curves[Constraint.Second];
            if (Line.Kind != SketchCurveCategory::Line || Arc.Kind != SketchCurveCategory::Arc) continue;
            const bool Joined = Line.Start == Arc.Start || Line.Start == Arc.End || Line.End == Arc.Start || Line.End == Arc.End;
            TangentEnds[Constraint.Second] += Joined ? 1 : 0;
        }
        const auto Turn = [](const SketchModel& Sketch, const SketchCurve& Arc, double& Reach)
        {
            const Vector2 Centre = Sketch.Points[Arc.Centre].Position;
            const Vector2 Start  = Sketch.Points[Arc.Start].Position - Centre;
            const Vector2 End    = Sketch.Points[Arc.End].Position - Centre;
            Reach = Length(Start) * Length(End);
            return Cross(Start, End);
        };
        for (size_t Curve = 0; Curve < Before.Curves.size() && Curve < After.Curves.size(); ++Curve)
        {
            if (TangentEnds[Curve] < 2) continue;
            double       ReachBefore = 0.0, ReachAfter = 0.0;
            const double Earlier = Turn(Before, Before.Curves[Curve], ReachBefore);
            const double Later   = Turn(After, After.Curves[Curve], ReachAfter);
            if (std::fabs(Earlier) <= 1.0e-9 * ReachBefore || std::fabs(Later) <= 1.0e-9 * ReachAfter) continue;
            if ((Earlier > 0.0) != (Later > 0.0)) return static_cast<int>(Curve);
        }
        return -1;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketchConstraintProposal ProposeSketchConstraint(const SketchModel& Sketch, const std::vector<SketchPick>& Picks, SketchConstraintVerb Verb)
{
    if (Picks.empty()) return Refuse(Verb, "pick points or curves of the sketch first");
    const SortedPicks Sorted = SortPicks(Sketch, Picks);
    if (!Sorted.Refusal.empty()) return Refuse(Verb, Sorted.Refusal);
    const auto& P = Sorted.Points;
    const auto& L = Sorted.Lines;
    const auto& C = Sorted.Circles;
    const auto& S = Sorted.Splines;
    if (!S.empty())
    {
        const std::string Spline = SketchCurveName(Sketch.Curves[S[0]]);
        switch (Verb)
        {
        case SketchConstraintVerb::On:
        {
            if (!Pattern(Sorted, 1, 0, 0, 1)) return Refuse(Verb, "takes a point and a spline");
            const auto& Defining = Sketch.Curves[S[0]].Points;
            if (std::find(Defining.begin(), Defining.end(), P[0]) != Defining.end())
                return Refuse(Verb, NameSketchPick(Sketch, { SketchPickCategory::Point, P[0] }) + " defines " + Spline);
            return Accept(Make(Kind::PointOnSpline, P[0], S[0]));
        }
        case SketchConstraintVerb::Horizontal:
        case SketchConstraintVerb::Vertical:
        {
            const auto& Defining = Sketch.Curves[S[0]].Points;
            if (!Pattern(Sorted, 1, 0, 0, 1) || Defining.size() < 2 || Defining.front() == Defining.back() || (P[0] != Defining.front() && P[0] != Defining.back()))
                return Refuse(Verb, "takes a spline and one of its end points (the end tangent turns level / plumb)");
            return Accept(Make(Kind::SplineDirection, S[0], -1, P[0], Verb == SketchConstraintVerb::Horizontal ? 0.0 : std::numbers::pi / 2.0));
        }
        case SketchConstraintVerb::Parallel:
        case SketchConstraintVerb::Curvature:
        {
            int First = -1, Other = -1;
            if (Pattern(Sorted, 0, 1, 0, 1)) { First = S[0]; Other = L[0]; }
            else if (Pattern(Sorted, 0, 0, 1, 1)) { First = S[0]; Other = C[0]; }
            else if (Pattern(Sorted, 0, 0, 0, 2)) { First = S[0]; Other = S[1]; }
            else return Refuse(Verb, "takes a spline and a line, circle / arc or spline it meets at an end");
            const SplineJoint Joint = JoinSpline(Sketch, First, Other);
            if (!Joint.Refusal.empty()) return Refuse(Verb, Joint.Refusal);
            // A natural cubic spline has C″ = 0 at its ends: its end curvature is 0 by construction, not a free quantity.
            if (Verb == SketchConstraintVerb::Curvature)
            {
                for (int Curve : { First, Other })
                {
                    if (Sketch.Curves[Curve].Kind == SketchCurveCategory::Spline && Sketch.Curves[Curve].Form == SketchSplineForm::CubicSpline)
                        return Refuse(Verb, SketchCurveName(Sketch.Curves[Curve]) + " is a cubic spline: its ends have zero curvature by construction (use Hermite, Bezier or NURBS for G2)");
                }
            }
            return Accept(Make(Verb == SketchConstraintVerb::Parallel ? Kind::SplineTangent : Kind::SplineCurvature, First, Other, Joint.End, 0.0, Joint.OtherEnd));
        }
        default: return Refuse(Verb, Spline + " is a spline: it takes O, L, C and H / V (constrain its defining points for the rest)");
        }
    }

    switch (Verb)
    {
    case SketchConstraintVerb::Dimension:
    {
        if (Pattern(Sorted, 0, 1, 0)) return Accept(Make(Kind::Length, L[0], -1, -1, Length(LineVector(Sketch, L[0]))));
        if (Pattern(Sorted, 2, 0, 0))
        {
            const double Gap = Distance(Sketch.Points[P[0]].Position, Sketch.Points[P[1]].Position);
            if (!(Gap > 0.0)) return Refuse(Verb, "the two points coincide (O makes them one)");
            return Accept(Make(Kind::Distance, P[0], P[1], -1, Gap));
        }
        if (Pattern(Sorted, 0, 0, 1)) return Accept(Make(Kind::Radius, C[0], -1, -1, MeasureSketchRadius(Sketch, Sketch.Curves[C[0]])));
        if (Pattern(Sorted, 1, 1, 0)) return Refuse(Verb, "a point-to-line distance is not in the solver yet");
        return Refuse(Verb, "takes one line (length), two points (distance) or one circle / arc (radius)");
    }
    case SketchConstraintVerb::Angle:
    {
        if (!Pattern(Sorted, 0, 2, 0)) return Refuse(Verb, "takes two lines");
        const Vector2 First = LineVector(Sketch, L[0]), Second = LineVector(Sketch, L[1]);
        return Accept(Make(Kind::Angle, L[0], L[1], -1, std::atan2(Cross(First, Second), Dot(First, Second))));
    }
    case SketchConstraintVerb::Horizontal:
    case SketchConstraintVerb::Vertical:
    {
        const bool Level = Verb == SketchConstraintVerb::Horizontal;
        if (Pattern(Sorted, 0, 1, 0)) return Accept(Make(Level ? Kind::Horizontal : Kind::Vertical, L[0]));
        if (Pattern(Sorted, 2, 0, 0)) return Accept(Make(Level ? Kind::VerticalDistance : Kind::HorizontalDistance, P[0], P[1], -1, 0.0));
        return Refuse(Verb, "takes one line or two points");
    }
    case SketchConstraintVerb::On:
    {
        if (Pattern(Sorted, 2, 0, 0)) return Accept(Make(Kind::Coincident, P[0], P[1]));
        if (Pattern(Sorted, 1, 1, 0))
        {
            if (EndpointCondition(Sketch.Curves[L[0]], P[0])) return Refuse(Verb, NameSketchPick(Sketch, { SketchPickCategory::Point, P[0] }) + " already ends " + SketchCurveName(Sketch.Curves[L[0]]));
            return Accept(Make(Kind::PointOnLine, P[0], L[0]));
        }
        if (Pattern(Sorted, 1, 0, 1))
        {
            if (EndpointCondition(Sketch.Curves[C[0]], P[0])) return Refuse(Verb, NameSketchPick(Sketch, { SketchPickCategory::Point, P[0] }) + " already belongs to " + SketchCurveName(Sketch.Curves[C[0]]));
            return Accept(Make(Kind::PointOnCircle, P[0], C[0]));
        }
        if (Pattern(Sorted, 0, 0, 2)) return Accept(Make(Kind::Concentric, C[0], C[1]));
        return Refuse(Verb, "takes two points, a point and a line or circle, or two circles");
    }
    case SketchConstraintVerb::Equal:
    {
        if (Pattern(Sorted, 0, 2, 0)) return Accept(Make(Kind::EqualLength, L[0], L[1]));
        if (Pattern(Sorted, 0, 0, 2)) return Accept(Make(Kind::EqualRadius, C[0], C[1]));
        return Refuse(Verb, "takes two lines or two circles / arcs");
    }
    case SketchConstraintVerb::Midpoint:
    {
        if (!Pattern(Sorted, 1, 1, 0)) return Refuse(Verb, "takes a point and a line");
        if (EndpointCondition(Sketch.Curves[L[0]], P[0])) return Refuse(Verb, NameSketchPick(Sketch, { SketchPickCategory::Point, P[0] }) + " ends " + SketchCurveName(Sketch.Curves[L[0]]));
        return Accept(Make(Kind::Midpoint, P[0], L[0]));
    }
    case SketchConstraintVerb::Symmetric:
    {
        if (!Pattern(Sorted, 2, 1, 0)) return Refuse(Verb, "takes two points and the line between them");
        return Accept(Make(Kind::Symmetric, P[0], P[1], L[0]));
    }
    case SketchConstraintVerb::Parallel:
    {
        if (Pattern(Sorted, 0, 2, 0)) return Accept(Make(Kind::Parallel, L[0], L[1]));
        if (Pattern(Sorted, 0, 1, 1)) return Accept(Make(Kind::Tangent, L[0], C[0]));
        if (Pattern(Sorted, 0, 0, 2))
        {
            // Internal when one circle holds the other's centre (SecondValue < 0), external otherwise.
            const SketchCurve& First = Sketch.Curves[C[0]];
            const SketchCurve& Second = Sketch.Curves[C[1]];
            const double Separation = Distance(Sketch.Points[First.Centre].Position, Sketch.Points[Second.Centre].Position);
            const double Larger     = std::max(MeasureSketchRadius(Sketch, First), MeasureSketchRadius(Sketch, Second));
            return Accept(Make(Kind::Tangent, C[0], C[1], -1, 0.0, Separation < Larger ? -1.0 : 0.0));
        }
        return Refuse(Verb, "takes two lines (parallel), or a line / circle with a circle (tangent)");
    }
    case SketchConstraintVerb::Perpendicular:
    {
        if (!Pattern(Sorted, 0, 2, 0)) return Refuse(Verb, "takes two lines");
        return Accept(Make(Kind::Perpendicular, L[0], L[1]));
    }
    case SketchConstraintVerb::Lock:
    {
        if (!Pattern(Sorted, 1, 0, 0)) return Refuse(Verb, "takes one point");
        const Vector2 Where = Sketch.Points[P[0]].Position;
        return Accept(Make(Kind::Fixed, P[0], -1, -1, Where.X, Where.Y));
    }
    case SketchConstraintVerb::Curvature: return Refuse(Verb, "takes a spline and the curve it meets at an end");
    }
    return Refuse(Verb, "unknown verb");
}

SketchConstraintProposal ProposeSketchReference(const SketchModel& Sketch, const std::vector<SketchPick>& Picks)
{
    SketchConstraintProposal Proposal = ProposeSketchConstraint(Sketch, Picks, SketchConstraintVerb::Dimension);
    if (!Proposal.Refusal.empty())
    {
        const SketchConstraintProposal Angle = ProposeSketchConstraint(Sketch, Picks, SketchConstraintVerb::Angle);
        if (!Angle.Refusal.empty())
        {
            Proposal.Refusal = "Reference dimension: takes one line, two points, one circle / arc or two lines";
            return Proposal;
        }
        Proposal = Angle;
    }
    Proposal.Constraint.DrivenCondition = true;
    return Proposal;
}

double MeasureSketchConstraint(const SketchModel& Sketch, const SketchConstraint& Constraint)
{
    const auto Point = [&](int Index) { return Sketch.Points[Index].Position; };
    switch (Constraint.Kind)
    {
    case Kind::Distance:           return Distance(Point(Constraint.First), Point(Constraint.Second));
    case Kind::HorizontalDistance: return Point(Constraint.Second).X - Point(Constraint.First).X;
    case Kind::VerticalDistance:   return Point(Constraint.Second).Y - Point(Constraint.First).Y;
    case Kind::Length:             return Length(LineVector(Sketch, Constraint.First));
    case Kind::Radius:             return MeasureSketchRadius(Sketch, Sketch.Curves[Constraint.First]);
    case Kind::Angle:
    {
        const Vector2 First = LineVector(Sketch, Constraint.First), Second = LineVector(Sketch, Constraint.Second);
        return std::atan2(Cross(First, Second), Dot(First, Second));
    }
    default: return 0.0;
    }
}

bool SketchDimensionCondition(SketchConstraintCategory Constraint) noexcept
{
    return Constraint == Kind::Distance || Constraint == Kind::HorizontalDistance || Constraint == Kind::VerticalDistance || Constraint == Kind::Length ||
           Constraint == Kind::Angle || Constraint == Kind::Radius;
}

std::string NameSketchPick(const SketchModel& Sketch, SketchPick Pick)
{
    if (Pick.Kind == SketchPickCategory::Curve)
        return Pick.Index >= 0 && Pick.Index < static_cast<int>(Sketch.Curves.size()) ? SketchCurveName(Sketch.Curves[Pick.Index]) : "Curve?";
    for (const SketchCurve& Curve : Sketch.Curves)
    {
        if (Curve.Kind != SketchCurveCategory::Spline)
        {
            if (Curve.Start == Pick.Index && Curve.Kind != SketchCurveCategory::Circle) return SketchCurveName(Curve) + ".Start";
            if (Curve.End == Pick.Index && Curve.Kind != SketchCurveCategory::Circle) return SketchCurveName(Curve) + ".End";
            if (Curve.Centre == Pick.Index && Curve.Kind != SketchCurveCategory::Line) return SketchCurveName(Curve) + ".Centre";
            continue;
        }
        const auto Found = std::find(Curve.Points.begin(), Curve.Points.end(), Pick.Index);
        if (Found != Curve.Points.end()) return SketchCurveName(Curve) + ".Point" + std::to_string(Found - Curve.Points.begin());
    }
    return "Point" + std::to_string(Pick.Index);
}

std::string DescribeSketchConstraint(const SketchModel& Sketch, const SketchConstraint& Constraint)
{
    std::string Text = SketchConstraintTitle(Constraint.Kind);
    if (Constraint.Kind == Kind::SplineDirection)
    {
        const double Degrees = Constraint.Value * 180.0 / std::numbers::pi;
        Text = std::abs(Degrees) < 1.0e-9 ? "Horizontal end" : std::abs(Degrees - 90.0) < 1.0e-9 ? "Vertical end" : "End direction " + FormatNumber(Degrees) + "°";
        return Text + " · " + NameSketchPick(Sketch, { SketchPickCategory::Point, Constraint.Third });
    }
    if (Constraint.Kind == Kind::Angle) Text += " " + FormatNumber(Constraint.Value * 180.0 / std::numbers::pi) + "°";
    else if (SketchDimensionCondition(Constraint.Kind)) Text += " " + FormatNumber(Constraint.Value);
    if (Constraint.DrivenCondition) Text += " (reference)";
    else if (Constraint.Kind == Kind::Fixed) Text += " (" + FormatNumber(Constraint.Value) + ", " + FormatNumber(Constraint.SecondValue) + ")";
    else if (Constraint.Kind == Kind::Tangent && Constraint.SecondValue < 0.0) Text += " (inside)";
    const ReferenceLayout Layout = LayoutOf(Constraint.Kind);
    const int Slots[3] = { Constraint.First, Constraint.Second, Constraint.Third };
    for (int Slot = 0; Slot < Layout.Count; ++Slot)
        Text += " · " + NameSketchPick(Sketch, { Layout.Points[Slot] ? SketchPickCategory::Point : SketchPickCategory::Curve, Slots[Slot] });
    return Text;
}

const char* SketchConstraintTitle(SketchConstraintCategory Constraint) noexcept
{
    switch (Constraint)
    {
    case Kind::Coincident:         return "Coincident";
    case Kind::Fixed:              return "Fixed";
    case Kind::Horizontal:         return "Horizontal";
    case Kind::Vertical:           return "Vertical";
    case Kind::Distance:           return "Distance";
    case Kind::HorizontalDistance: return "Horizontal distance";
    case Kind::VerticalDistance:   return "Vertical distance";
    case Kind::Length:             return "Length";
    case Kind::Parallel:           return "Parallel";
    case Kind::Perpendicular:      return "Perpendicular";
    case Kind::Angle:              return "Angle";
    case Kind::EqualLength:        return "Equal length";
    case Kind::Radius:             return "Radius";
    case Kind::EqualRadius:        return "Equal radius";
    case Kind::PointOnLine:        return "Point on line";
    case Kind::PointOnCircle:      return "Point on circle";
    case Kind::Tangent:            return "Tangent";
    case Kind::Midpoint:           return "Midpoint";
    case Kind::Symmetric:          return "Symmetric";
    case Kind::Concentric:         return "Concentric";
    case Kind::ArcRule:            return "Arc rule";
    case Kind::SplineTangent:      return "Tangent (G1)";
    case Kind::SplineCurvature:    return "Curvature (G2)";
    case Kind::PointOnSpline:      return "Point on spline";
    case Kind::SplineDirection:    return "End direction";
    }
    return "?";
}

char SketchConstraintVerbKey(SketchConstraintVerb Verb) noexcept
{
    static constexpr char Keys[] = { 'D', 'N', 'H', 'V', 'O', 'Q', 'M', 'Y', 'L', '[', ']', 'C' };
    const int Index = static_cast<int>(Verb);
    return Index >= 0 && Index < static_cast<int>(sizeof(Keys)) ? Keys[Index] : '?';
}

const char* SketchConstraintVerbLabel(SketchConstraintVerb Verb) noexcept
{
    switch (Verb)
    {
    case SketchConstraintVerb::Dimension:     return "Dimension";
    case SketchConstraintVerb::Angle:         return "Angle";
    case SketchConstraintVerb::Horizontal:    return "Horizontal";
    case SketchConstraintVerb::Vertical:      return "Vertical";
    case SketchConstraintVerb::On:            return "On point / curve";
    case SketchConstraintVerb::Equal:         return "Equal";
    case SketchConstraintVerb::Midpoint:      return "Midpoint";
    case SketchConstraintVerb::Symmetric:     return "Symmetric";
    case SketchConstraintVerb::Parallel:      return "Parallel / tangent";
    case SketchConstraintVerb::Perpendicular: return "Perpendicular";
    case SketchConstraintVerb::Lock:          return "Lock point";
    case SketchConstraintVerb::Curvature:     return "Curvature (G2)";
    }
    return "?";
}

SketchDragOutcome DragSketchPoint(const SketchModel& Sketch, int Point, Vector2 Target, const SketchSolveSettings& Settings)
{
    SketchDragOutcome Outcome;
    if (Point < 0 || Point >= static_cast<int>(Sketch.Points.size())) { Outcome.Refusal = "Drag: the point is not in the sketch"; return Outcome; }
    const Vector2 Origin = Sketch.Points[Point].Position;
    if (!Sketch.Points[Point].Link.Sketch.empty())
    {
        Outcome.Refusal = "Drag: " + NameSketchPick(Sketch, { SketchPickCategory::Point, Point }) + " is linked to " + Sketch.Points[Point].Link.Sketch + " (move it there)";
        return Outcome;
    }

    // Tangent joints keep the side they stand on before the move (a fillet stays a fillet, never its 2π complement);
    // the held sides go back to the sketch's own values in the outcome.
    SketchModel Held = Sketch;
    HoldSketchTangentSides(Held);
    const auto Release = [&Sketch](SketchModel& Moved)
    {
        for (size_t Index = 0; Index < Sketch.Constraints.size() && Index < Moved.Constraints.size(); ++Index) Moved.Constraints[Index].Value = Sketch.Constraints[Index].Value;
    };

    // ① Pinned: the point held at the target by a temporary Fixed constraint, the rest follows.
    SketchModel Pinned = Held;
    Pinned.Points[Point].Position = Target;
    Pinned.Constraints.push_back(Make(Kind::Fixed, Point, -1, -1, Target.X, Target.Y));
    const SketchSolveOutcome PinnedSolve = SolveSketch(Pinned, Settings);
    if (PinnedSolve.Converged && PinnedSolve.ConflictGroups.empty() && InvertedFillet(Sketch, Pinned) < 0)
    {
        Pinned.Constraints.pop_back();
        Outcome.Sketch          = std::move(Pinned);
        Outcome.Solve           = SolveSketch(Outcome.Sketch, Settings);
        Release(Outcome.Sketch);
        Outcome.PinnedCondition = Distance(Outcome.Sketch.Points[Point].Position, Target) <= 1.0e-9 * std::max(1.0, Outcome.Solve.Scale);
        Outcome.Travel          = Distance(Outcome.Sketch.Points[Point].Position, Origin);
        return Outcome;
    }

    // ② Unpinned: solved from the dragged configuration, so the point lands as near the target as it may.
    SketchModel Free = Held;
    Free.Points[Point].Position = Target;
    Outcome.Solve = SolveSketch(Free, Settings);
    if (!Outcome.Solve.Converged || !Outcome.Solve.ConflictGroups.empty()) { Outcome.Refusal = "Drag: the sketch has no solution near there"; return Outcome; }
    if (const int Inverted = InvertedFillet(Sketch, Free); Inverted >= 0)
    {
        Outcome.Refusal = "Drag: " + NameSketchPick(Sketch, { SketchPickCategory::Curve, Inverted }) + " would turn inside out (a fillet stays under a half turn)";
        return Outcome;
    }
    Release(Free);
    Outcome.Sketch = std::move(Free);
    Outcome.Travel = Distance(Outcome.Sketch.Points[Point].Position, Origin);
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
