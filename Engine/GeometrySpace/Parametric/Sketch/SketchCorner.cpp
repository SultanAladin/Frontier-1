/*====================================================================================================================================
                                                        SKETCHCORNER.CPP
====================================================================================================================================*/
// 🧩 Sketch corner fillet / chamfer: setbacks along the two lines, a tangent arc or a cut line, the corner kept as a virtual sharp

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchCorner.h"

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
    constexpr double StraightAngle = 1.0e-6;                                                 // [rad] - θ this close to 0 or π is no corner

    // 📝 One line at a corner: its index and which end sits on the corner.
    struct CornerLine
    {
        int  Curve         = -1;                                                             // [-] - Line index
        bool EndCondition  = false;                                                          // [-] - The corner is its End (else Start)
    };

    [[nodiscard]] double MeasureScale(const SketchModel& Sketch)
    {
        double Largest = 1.0;
        for (const SketchPoint& Point : Sketch.Points) Largest = std::max({ Largest, std::abs(Point.Position.X), std::abs(Point.Position.Y) });
        return Largest;
    }

    [[nodiscard]] std::string Decimal(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.4g", Value);
        return Text;
    }

    [[nodiscard]] int CornerPoint(const SketchCurve& Line, bool EndCondition) { return EndCondition ? Line.End : Line.Start; }
    [[nodiscard]] int FarPoint(const SketchCurve& Line, bool EndCondition)    { return EndCondition ? Line.Start : Line.End; }

    [[nodiscard]] int NextIdentifier(const SketchModel& Sketch, SketchCurveCategory Kind)
    {
        int Largest = 0;
        for (const SketchCurve& Curve : Sketch.Curves) if (Curve.Kind == Kind) Largest = std::max(Largest, Curve.Identifier);
        return Largest + 1;
    }

    // Every profile curve ending on Point (or on a point at the same place): the lines, and whether anything else ends there.
    void GatherCornerCurves(const SketchModel& Sketch, int Point, double Join, std::vector<CornerLine>& Lines, int& Others)
    {
        Lines.clear();
        Others = 0;
        const Vector2 Position = Sketch.Points[Point].Position;
        auto Meets = [&](int Index) { return Index >= 0 && (Index == Point || Length(Sketch.Points[Index].Position - Position) <= Join); };
        for (size_t Index = 0; Index < Sketch.Curves.size(); ++Index)
        {
            const SketchCurve& Curve = Sketch.Curves[Index];
            if (Curve.Construction) continue;
            if (Curve.Kind == SketchCurveCategory::Line)
            {
                if (Meets(Curve.Start)) Lines.push_back({ static_cast<int>(Index), false });
                else if (Meets(Curve.End)) Lines.push_back({ static_cast<int>(Index), true });
                continue;
            }
            if (Curve.Kind == SketchCurveCategory::Arc && (Meets(Curve.Start) || Meets(Curve.End))) ++Others;
            if (Curve.Kind == SketchCurveCategory::Spline && !Curve.Points.empty() && (Meets(Curve.Points.front()) || Meets(Curve.Points.back()))) ++Others;
        }
    }

    // Interior angle at the corner between the two lines (0 … π).
    [[nodiscard]] double CornerAngle(const SketchModel& Sketch, const CornerLine& First, const CornerLine& Second)
    {
        const SketchCurve& A = Sketch.Curves[First.Curve];
        const SketchCurve& B = Sketch.Curves[Second.Curve];
        const Vector2 Corner = Sketch.Points[CornerPoint(A, First.EndCondition)].Position;
        const Vector2 U = Sketch.Points[FarPoint(A, First.EndCondition)].Position - Corner;
        const Vector2 V = Sketch.Points[FarPoint(B, Second.EndCondition)].Position - Corner;
        const double  Scale = Length(U) * Length(V);
        if (!(Scale > 0.0)) return 0.0;
        return std::acos(std::clamp((U.X * V.X + U.Y * V.Y) / Scale, -1.0, 1.0));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<int> FindSketchCorners(const SketchModel& Sketch)
{
    std::vector<int> Corners;
    const double Join = 1.0e-9 * MeasureScale(Sketch);
    std::vector<CornerLine> Lines;
    int Others = 0;
    for (size_t Point = 0; Point < Sketch.Points.size(); ++Point)
    {
        GatherCornerCurves(Sketch, static_cast<int>(Point), Join, Lines, Others);
        if (Lines.size() != 2 || Others != 0) continue;
        // A corner made of two coincident points is listed once (at the lower index).
        const int CornerA = CornerPoint(Sketch.Curves[Lines[0].Curve], Lines[0].EndCondition);
        const int CornerB = CornerPoint(Sketch.Curves[Lines[1].Curve], Lines[1].EndCondition);
        if (static_cast<int>(Point) != std::min(CornerA, CornerB)) continue;
        const double Angle = CornerAngle(Sketch, Lines[0], Lines[1]);
        if (Angle > StraightAngle && Angle < std::numbers::pi - StraightAngle) Corners.push_back(static_cast<int>(Point));
    }
    return Corners;
}

SketchCornerOutcome CutSketchCorners(const SketchModel& Sketch, const std::vector<int>& Points, double Size, SketchCornerShape Shape)
{
    SketchCornerOutcome Outcome;
    Outcome.Sketch = Sketch;
    const char* Verb = Shape == SketchCornerShape::Fillet ? "fillet" : "chamfer";
    auto Refuse = [&](const std::string& Reason) { Outcome.Sketch = Sketch; Outcome.Corners.clear(); Outcome.Setback = 0.0; Outcome.Refusal = Reason; return Outcome; };
    if (!(Size > 0.0) || !std::isfinite(Size)) return Refuse(std::string(Verb) + " size must be positive");
    const std::vector<int> Targets = Points.empty() ? FindSketchCorners(Sketch) : Points;
    if (Targets.empty()) return Refuse("the sketch has no corner where two lines meet (🚧 corners with arcs or splines)");

    SketchModel& Edited = Outcome.Sketch;
    const double Join = 1.0e-9 * MeasureScale(Sketch);
    std::vector<CornerLine> Lines;
    int Others = 0;
    for (int Point : Targets)
    {
        if (Point < 0 || Point >= static_cast<int>(Edited.Points.size())) return Refuse("the sketch has no point " + std::to_string(Point));
        if (std::find(Outcome.Corners.begin(), Outcome.Corners.end(), Point) != Outcome.Corners.end()) continue;
        GatherCornerCurves(Edited, Point, Join, Lines, Others);
        if (Others != 0) return Refuse("point " + std::to_string(Point) + " joins an arc or spline (🚧 " + Verb + "s between two lines only)");
        if (Lines.size() != 2) return Refuse("point " + std::to_string(Point) + " is no corner: " + std::to_string(Lines.size()) + " line(s) end there, a " + Verb + " needs 2");
        const double Theta = CornerAngle(Edited, Lines[0], Lines[1]);
        if (!(Theta > StraightAngle) || !(Theta < std::numbers::pi - StraightAngle)) return Refuse("the lines at point " + std::to_string(Point) + " run straight on (no corner to " + Verb + ")");

        // ① Setback along both lines from the corner P.
        const Vector2 Corner  = Edited.Points[Point].Position;
        const double  Setback = Shape == SketchCornerShape::Fillet ? Size / std::tan(0.5 * Theta) : Size;
        int    Ends[2]  = { -1, -1 };                                                        // [-]   - Original corner index on each line
        int    Fars[2]  = { -1, -1 };                                                        // [-]   - Far end of each line
        Vector2 Units[2] = {};                                                               // [-]   - û from the corner along each line
        for (int Side = 0; Side < 2; ++Side)
        {
            const SketchCurve& Line = Edited.Curves[Lines[Side].Curve];
            Ends[Side] = CornerPoint(Line, Lines[Side].EndCondition);
            Fars[Side] = FarPoint(Line, Lines[Side].EndCondition);
            const Vector2 Along = Edited.Points[Fars[Side]].Position - Corner;
            const double  Span  = Length(Along);
            if (Setback >= Span - Join)
            {
                return Refuse(std::string(Verb) + " " + Decimal(Size) + " at point " + std::to_string(Point) + " needs " + Decimal(Setback) + " along " + SketchCurveName(Line)
                              + ", which has only " + Decimal(Span) + " left");
            }
            Units[Side] = Along * (1.0 / Span);
        }
        const int Tangent[2] = { AppendSketchPoint(Edited, Corner + Units[0] * Setback), AppendSketchPoint(Edited, Corner + Units[1] * Setback) };

        // ② The lines now end on the setback points; constraints that measured their full length move to the virtual sharp.
        for (int Side = 0; Side < 2; ++Side)
        {
            SketchCurve& Line = Edited.Curves[Lines[Side].Curve];
            (Lines[Side].EndCondition ? Line.End : Line.Start) = Tangent[Side];
        }
        std::vector<SketchConstraint> Kept;
        for (SketchConstraint Constraint : Edited.Constraints)
        {
            int Side = -1;
            for (int Candidate = 0; Candidate < 2; ++Candidate)
            {
                const bool Names = (Constraint.Kind == SketchConstraintCategory::Length || Constraint.Kind == SketchConstraintCategory::EqualLength) ? (Constraint.First == Lines[Candidate].Curve || (Constraint.Kind == SketchConstraintCategory::EqualLength && Constraint.Second == Lines[Candidate].Curve))
                                 : Constraint.Kind == SketchConstraintCategory::Midpoint ? Constraint.Second == Lines[Candidate].Curve : false;
                if (Names) Side = Candidate;
            }
            if (Side < 0) { Kept.push_back(std::move(Constraint)); continue; }
            if (Constraint.Kind != SketchConstraintCategory::Length) continue;                   // EqualLength / Midpoint drop
            Constraint.Kind   = SketchConstraintCategory::Distance;
            Constraint.First  = Ends[Side];
            Constraint.Second = Fars[Side];
            Kept.push_back(std::move(Constraint));
        }
        Edited.Constraints = std::move(Kept);
        for (int Side = 0; Side < 2; ++Side)
        {
            for (int Line = 0; Line < 2; ++Line)
            {
                if (Side == 1 && Ends[0] == Ends[1]) continue;                               // One shared corner point: pinned once per line
                AppendSketchConstraint(Edited, SketchConstraintCategory::PointOnLine, Ends[Side], Lines[Line].Curve);
            }
        }

        // ③ The new curve between the setback points.
        if (Shape == SketchCornerShape::Fillet)
        {
            Vector2 Bisector = Units[0] + Units[1];
            Bisector = Bisector * (1.0 / Length(Bisector));
            const int     Centre = AppendSketchPoint(Edited, Corner + Bisector * (Size / std::sin(0.5 * Theta)));
            const Vector2 Middle = Edited.Points[Centre].Position;
            const Vector2 From   = Edited.Points[Tangent[0]].Position - Middle;
            const Vector2 To     = Edited.Points[Tangent[1]].Position - Middle;
            const bool    TurnCondition = From.X * To.Y - From.Y * To.X > 0.0;                // Counter-clockwise T₁ → T₂
            const int     Arc = AppendSketchArc(Edited, Centre, TurnCondition ? Tangent[0] : Tangent[1], TurnCondition ? Tangent[1] : Tangent[0], NextIdentifier(Edited, SketchCurveCategory::Arc));
            AppendSketchConstraint(Edited, SketchConstraintCategory::Tangent, Lines[0].Curve, Arc);
            AppendSketchConstraint(Edited, SketchConstraintCategory::Tangent, Lines[1].Curve, Arc);
            AppendSketchConstraint(Edited, SketchConstraintCategory::Radius, Arc, -1, -1, Size, "Fillet");
        }
        else
        {
            AppendSketchLine(Edited, Tangent[0], Tangent[1], NextIdentifier(Edited, SketchCurveCategory::Line));
            AppendSketchConstraint(Edited, SketchConstraintCategory::Distance, Ends[0], Tangent[0], -1, Size, "Chamfer");
            AppendSketchConstraint(Edited, SketchConstraintCategory::Distance, Ends[1], Tangent[1], -1, Size, "Chamfer");
        }
        Outcome.Corners.push_back(Point);
        Outcome.Setback = std::max(Outcome.Setback, Setback);
        // A corner given by two coincident points: the partner is done too.
        if (Ends[0] != Ends[1]) Outcome.Corners.push_back(Ends[0] == Point ? Ends[1] : Ends[0]);
    }
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
