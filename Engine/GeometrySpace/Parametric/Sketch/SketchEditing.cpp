/*====================================================================================================================================
                                                        SKETCHEDITING.CPP
====================================================================================================================================*/
// 🧩 Sketch editing (Plasticity O, J): chain offsets re-joined at carrier intersections, sketch merging with point welding

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchEditing.h"

#include "Engine/GeometrySpace/Parametric/Sketch/SketchTrim.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double FullTurn      = 2.0 * std::numbers::pi;                                 // [rad]
    constexpr double JoinRatio     = 1.0e-9;                                                 // [-]   - Endpoint join distance / L
    constexpr double TangentSine   = 1.0e-9;                                                 // [-]   - |sin| of a corner below which it is tangent
    constexpr double AxisSlack     = 1.0e-9;                                                 // [-]   - Placement orthonormality
    constexpr int    AreaSamples   = 32;                                                     // [-]   - Samples per piece for the loop sense

    // 📝 One chain of a sketch: its pieces oriented end to end, closed when the last ends where the first starts.
    struct SketchChain
    {
        std::vector<ProfileSegment> Pieces          = {};                                    // [-] - Traversal order
        bool                        ClosedCondition = false;                                 // [-] - Loop
    };

    [[nodiscard]] Vector2 RightNormal(Vector2 Tangent) noexcept { return Normalize(Vector2{ Tangent.Y, -Tangent.X }); }

    [[nodiscard]] Vector2 UnitTangent(const ProfileSegment& Segment, double Fraction) noexcept { return Normalize(EvaluateProfileTangent(Segment, Fraction)); }

    // Signed area of a closed chain (polyline through dense samples; only the sign is used).
    [[nodiscard]] double ChainArea(const SketchChain& Chain)
    {
        double Area = 0.0;
        for (const ProfileSegment& Piece : Chain.Pieces)
        {
            for (int Sample = 0; Sample < AreaSamples; ++Sample)
            {
                const Vector2 First  = EvaluateProfileSegment(Piece, double(Sample) / AreaSamples);
                const Vector2 Second = EvaluateProfileSegment(Piece, double(Sample + 1) / AreaSamples);
                Area += 0.5 * Cross(First, Second);
            }
        }
        return Area;
    }

    // 💡 Endpoints clustered within Join; every cluster must hold at most two piece ends. Open chains start at a lone end.
    [[nodiscard]] std::string CollectChains(const SketchModel& Sketch, double Join, std::vector<SketchChain>& Chains)
    {
        std::vector<ProfileSegment> Pieces;
        for (size_t Index = 0; Index < Sketch.Curves.size(); ++Index)
        {
            const SketchCurve& Curve = Sketch.Curves[Index];
            if (Curve.Construction) continue;
            if (Curve.Kind == SketchCurveCategory::Spline) return "Offset: " + SketchCurveName(Curve) + " is a spline (🚧 splines have no exact offset; lines, arcs and circles offset exactly)";
            std::string Refusal;
            const ProfileSegment Piece = ResolveSketchCurveSegment(Sketch, static_cast<int>(Index), Join, Refusal);
            if (!Refusal.empty()) return "Offset: " + Refusal;
            if (Piece.Kind == ProfileSegmentCategory::Circle) { Chains.push_back({ { Piece }, true }); continue; }
            Pieces.push_back(Piece);
        }
        // Ends: 2k = start of piece k, 2k + 1 = its end; clusters by a simple quadratic scan (sketches are small).
        const size_t Ends = 2 * Pieces.size();
        std::vector<int> Cluster(Ends, -1);
        std::vector<std::vector<size_t>> Members;
        auto EndPoint = [&](size_t End) { return End % 2 == 0 ? Pieces[End / 2].Start : Pieces[End / 2].End; };
        for (size_t End = 0; End < Ends; ++End)
        {
            for (size_t Group = 0; Group < Members.size() && Cluster[End] < 0; ++Group)
                if (Distance(EndPoint(Members[Group].front()), EndPoint(End)) <= Join) { Cluster[End] = static_cast<int>(Group); Members[Group].push_back(End); }
            if (Cluster[End] < 0) { Cluster[End] = static_cast<int>(Members.size()); Members.push_back({ End }); }
        }
        for (const std::vector<size_t>& Group : Members)
            if (Group.size() > 2) return "Offset: " + std::to_string(Group.size()) + " curves meet at one point (branching chain; offset each branch on its own)";
        // Walk: lone ends first (open chains), then whatever is left (closed loops).
        std::vector<bool> Used(Pieces.size(), false);
        auto Walk = [&](size_t StartEnd)
        {
            SketchChain Chain;
            size_t End = StartEnd;
            while (true)
            {
                const size_t Piece = End / 2;
                if (Used[Piece]) break;
                Used[Piece] = true;
                ProfileSegment Oriented = Pieces[Piece];
                if (End % 2 == 1) ReverseProfileSegment(Oriented);
                Chain.Pieces.push_back(Oriented);
                const size_t Far = End ^ 1u;                                                 // The piece's other end
                const std::vector<size_t>& Group = Members[Cluster[Far]];
                size_t Next = SIZE_MAX;
                for (size_t Candidate : Group) if (Candidate != Far) Next = Candidate;
                if (Next == SIZE_MAX) break;                                                 // Open chain ends here
                End = Next;
            }
            Chain.ClosedCondition = Chain.Pieces.size() > 1 && Distance(Chain.Pieces.back().End, Chain.Pieces.front().Start) <= Join;
            if (Chain.Pieces.size() == 1 && Distance(Chain.Pieces.front().End, Chain.Pieces.front().Start) <= Join) Chain.ClosedCondition = true;
            Chains.push_back(std::move(Chain));
        };
        for (size_t End = 0; End < Ends; ++End) if (!Used[End / 2] && Members[Cluster[End]].size() == 1) Walk(End);
        for (size_t End = 0; End < Ends; End += 2) if (!Used[End / 2]) Walk(End);
        return {};
    }

    // Offset carrier of one piece by Right (signed, toward its right-hand normal); Refusal when a radius vanishes.
    [[nodiscard]] ProfileSegment OffsetPiece(const ProfileSegment& Piece, double Right, double Join, std::string& Refusal)
    {
        ProfileSegment Image = Piece;
        if (Piece.Kind == ProfileSegmentCategory::Line)
        {
            const Vector2 Normal = RightNormal(Piece.End - Piece.Start);
            Image.Start = Piece.Start + Normal * Right;
            Image.End   = Piece.End + Normal * Right;
            return Image;
        }
        // 📝 Travelling counter-clockwise (Sweep > 0) the right-hand normal points away from the centre.
        Image.Radius = Piece.Radius + Right * (Piece.Sweep > 0.0 ? 1.0 : -1.0);
        if (!(Image.Radius > Join)) { Refusal = "Offset: " + Piece.Name + " would vanish (radius " + std::to_string(Piece.Radius) + " − " + std::to_string(std::fabs(Right)) + " ≤ 0)"; return Image; }
        Image.Start = EvaluateProfileSegment(Image, 0.0);
        Image.End   = EvaluateProfileSegment(Image, 1.0);
        return Image;
    }

    // Intersections of the carriers (infinite line / full circle) of two pieces.
    [[nodiscard]] std::vector<Vector2> IntersectCarriers(const ProfileSegment& First, const ProfileSegment& Second)
    {
        std::vector<Vector2> Points;
        const bool FirstLine = First.Kind == ProfileSegmentCategory::Line, SecondLine = Second.Kind == ProfileSegmentCategory::Line;
        if (FirstLine && SecondLine)
        {
            const Vector2 A = First.End - First.Start, B = Second.End - Second.Start;
            const double  Denominator = Cross(A, B);
            if (std::fabs(Denominator) <= 1.0e-300) return Points;
            const double Along = Cross(Second.Start - First.Start, B) / Denominator;
            Points.push_back(First.Start + A * Along);
            return Points;
        }
        if (FirstLine || SecondLine)
        {
            const ProfileSegment& Line   = FirstLine ? First : Second;
            const ProfileSegment& Circle = FirstLine ? Second : First;
            const Vector2 Direction = Normalize(Line.End - Line.Start);
            const Vector2 Foot      = Line.Start + Direction * Dot(Circle.Centre - Line.Start, Direction);
            const double  Gap       = Distance(Foot, Circle.Centre);
            const double  Square    = Circle.Radius * Circle.Radius - Gap * Gap;
            if (Square < 0.0) return Points;
            const double Half = std::sqrt(Square);
            Points.push_back(Foot - Direction * Half);
            Points.push_back(Foot + Direction * Half);
            return Points;
        }
        const Vector2 Between = Second.Centre - First.Centre;
        const double  Gap     = Length(Between);
        if (!(Gap > 0.0)) return Points;
        const double Along  = (Gap * Gap + First.Radius * First.Radius - Second.Radius * Second.Radius) / (2.0 * Gap);
        const double Square = First.Radius * First.Radius - Along * Along;
        if (Square < 0.0) return Points;
        const Vector2 Axis = Between * (1.0 / Gap), Across = { -Axis.Y, Axis.X };
        const Vector2 Middle = First.Centre + Axis * Along;
        const double  Half = std::sqrt(Square);
        Points.push_back(Middle - Across * Half);
        Points.push_back(Middle + Across * Half);
        return Points;
    }

    // Angle of Point on an arc carrier, unwrapped to the branch nearest Reference.
    [[nodiscard]] double NearestAngle(const ProfileSegment& Arc, Vector2 Point, double Reference) noexcept
    {
        const double Angle = std::atan2(Point.Y - Arc.Centre.Y, Point.X - Arc.Centre.X);
        return Angle + FullTurn * std::round((Reference - Angle) / FullTurn);
    }

    // Re-ends an offset arc at new end points (angles unwrapped near the old ones); Refusal when the sweep turns over.
    void ReshapeArc(ProfileSegment& Image, Vector2 Start, Vector2 End, std::string& Refusal)
    {
        const double OldStart = Image.StartAngle, OldEnd = Image.StartAngle + Image.Sweep;
        const double NewStart = NearestAngle(Image, Start, OldStart), NewEnd = NearestAngle(Image, End, OldEnd);
        const double Sweep = NewEnd - NewStart;
        if (Sweep * Image.Sweep <= 0.0 || std::fabs(Sweep) >= FullTurn) { Refusal = "Offset: the offset turns " + Image.Name + " inside out (🚧 no loop clean-up)"; return; }
        Image.StartAngle = NewStart;
        Image.Sweep      = Sweep;
        Image.Start      = EvaluateProfileSegment(Image, 0.0);
        Image.End        = EvaluateProfileSegment(Image, 1.0);
    }

    // Writes one offset chain into Sketch: shared corner points, arcs counter-clockwise, circles with their own centre.
    void WriteChain(SketchModel& Sketch, const std::vector<ProfileSegment>& Images, bool ClosedCondition, int& Identifier)
    {
        if (Images.size() == 1 && Images.front().Kind == ProfileSegmentCategory::Circle)
        {
            const int Centre = AppendSketchPoint(Sketch, Images.front().Centre);
            AppendSketchCircle(Sketch, Centre, Images.front().Radius, Identifier++);
            return;
        }
        std::vector<int> Corners;
        for (const ProfileSegment& Image : Images) Corners.push_back(AppendSketchPoint(Sketch, Image.Start));
        Corners.push_back(ClosedCondition ? Corners.front() : AppendSketchPoint(Sketch, Images.back().End));
        for (size_t Index = 0; Index < Images.size(); ++Index)
        {
            const ProfileSegment& Image = Images[Index];
            if (Image.Kind == ProfileSegmentCategory::Line) { AppendSketchLine(Sketch, Corners[Index], Corners[Index + 1], Identifier++); continue; }
            const int Centre = AppendSketchPoint(Sketch, Image.Centre);
            if (Image.Sweep > 0.0) AppendSketchArc(Sketch, Centre, Corners[Index], Corners[Index + 1], Identifier++);
            else AppendSketchArc(Sketch, Centre, Corners[Index + 1], Corners[Index], Identifier++);
        }
    }

    // Constraint reference families (see SketchConstraintCategory).
    enum class ReferenceGroup : uint8_t { None, Point, Curve };

    struct ConstraintShape
    {
        ReferenceGroup First = ReferenceGroup::None, Second = ReferenceGroup::None, Third = ReferenceGroup::None;
    };

    [[nodiscard]] ConstraintShape ShapeOf(SketchConstraintCategory Kind) noexcept
    {
        using enum SketchConstraintCategory;
        constexpr ReferenceGroup P = ReferenceGroup::Point, C = ReferenceGroup::Curve, N = ReferenceGroup::None;
        switch (Kind)
        {
        case Coincident: case Distance: case HorizontalDistance: case VerticalDistance: return { P, P, N };
        case Fixed:                                                                    return { P, N, N };
        case PointOnLine: case PointOnCircle: case Midpoint:                           return { P, C, N };
        case Symmetric:                                                                return { P, P, C };
        case Horizontal: case Vertical: case Length: case Radius: case ArcRule:        return { C, N, N };
        default:                                                                       return { C, C, N };
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

double MeasureSketchScale(const SketchModel& Sketch)
{
    if (Sketch.Points.empty()) return 1.0;
    Vector2 Low = Sketch.Points.front().Position, High = Low;
    for (const SketchPoint& Point : Sketch.Points)
    {
        Low  = { std::min(Low.X, Point.Position.X), std::min(Low.Y, Point.Position.Y) };
        High = { std::max(High.X, Point.Position.X), std::max(High.Y, Point.Position.Y) };
    }
    double Scale = std::max(Distance(Low, High), 1.0e-300);
    for (const SketchCurve& Curve : Sketch.Curves) if (Curve.Kind == SketchCurveCategory::Circle) Scale = std::max(Scale, 2.0 * Curve.Radius);
    return Scale;
}

SketchOffsetOutcome OffsetSketchCurves(const SketchModel& Sketch, double Distance)
{
    SketchOffsetOutcome Outcome;
    const double Scale = MeasureSketchScale(Sketch);
    const double Join  = JoinRatio * Scale;
    if (!(std::fabs(Distance) > Join)) { Outcome.Refusal = "Offset: the distance is zero"; return Outcome; }
    std::vector<SketchChain> Chains;
    if (const std::string Refusal = CollectChains(Sketch, Join, Chains); !Refusal.empty()) { Outcome.Refusal = Refusal; return Outcome; }
    if (Chains.empty()) { Outcome.Refusal = "Offset: the sketch has no curves to offset (construction curves are skipped)"; return Outcome; }

    SketchModel Result;
    int Identifier = 1;
    for (const SketchChain& Chain : Chains)
    {
        // ① Side: closed loops grow outward for d > 0 (right of a counter-clockwise loop); open chains go left.
        double Right = -Distance;
        if (Chain.ClosedCondition)
        {
            const bool CounterClockwise = Chain.Pieces.size() == 1 ? Chain.Pieces.front().Sweep > 0.0 : ChainArea(Chain) > 0.0;
            Right = CounterClockwise ? Distance : -Distance;
        }
        // ② Offset carriers.
        std::vector<ProfileSegment> Images;
        for (const ProfileSegment& Piece : Chain.Pieces)
        {
            std::string Refusal;
            Images.push_back(OffsetPiece(Piece, Right, Join, Refusal));
            if (!Refusal.empty()) { Outcome.Refusal = Refusal; return Outcome; }
        }
        // ③ Corners: tangent joins keep their (coincident) offset ends; others meet where the carriers cross nearest the
        //    original corner. Pieces are re-ended only after every corner is known.
        const size_t Count   = Images.size();
        const size_t Corners = Chain.ClosedCondition ? Count : Count - 1;
        std::vector<Vector2> Starts(Count), Ends(Count);
        for (size_t Index = 0; Index < Count; ++Index) { Starts[Index] = Images[Index].Start; Ends[Index] = Images[Index].End; }
        if (!(Count == 1 && Chain.ClosedCondition))
        {
            for (size_t Corner = 0; Corner < Corners; ++Corner)
            {
                const size_t Before = Corner, After = (Corner + 1) % Count;
                const Vector2 Original  = Chain.Pieces[Before].End;
                const Vector2 Incoming  = UnitTangent(Chain.Pieces[Before], 1.0);
                const Vector2 Outgoing  = UnitTangent(Chain.Pieces[After], 0.0);
                Vector2 Meeting;
                if (std::fabs(Cross(Incoming, Outgoing)) <= TangentSine)
                {
                    if (Dot(Incoming, Outgoing) < 0.0) { Outcome.Refusal = "Offset: " + Chain.Pieces[Before].Name + " doubles back on " + Chain.Pieces[After].Name; return Outcome; }
                    Meeting = (Images[Before].End + Images[After].Start) * 0.5;
                }
                else
                {
                    const std::vector<Vector2> Candidates = IntersectCarriers(Images[Before], Images[After]);
                    if (Candidates.empty()) { Outcome.Refusal = "Offset: the offsets of " + Chain.Pieces[Before].Name + " and " + Chain.Pieces[After].Name + " no longer meet (🚧 no loop clean-up)"; return Outcome; }
                    Meeting = Candidates.front();
                    for (const Vector2& Candidate : Candidates) if (Length(Candidate - Original) < Length(Meeting - Original)) Meeting = Candidate;
                }
                Ends[Before]  = Meeting;
                Starts[After] = Meeting;
            }
            for (size_t Index = 0; Index < Count; ++Index)
            {
                ProfileSegment& Image = Images[Index];
                if (Image.Kind == ProfileSegmentCategory::Line)
                {
                    const Vector2 Direction = Chain.Pieces[Index].End - Chain.Pieces[Index].Start;
                    if (Dot(Ends[Index] - Starts[Index], Direction) <= Join * Length(Direction)) { Outcome.Refusal = "Offset: the offset turns " + Image.Name + " inside out (🚧 no loop clean-up)"; return Outcome; }
                    Image.Start = Starts[Index];
                    Image.End   = Ends[Index];
                    continue;
                }
                std::string Refusal;
                ReshapeArc(Image, Starts[Index], Ends[Index], Refusal);
                if (!Refusal.empty()) { Outcome.Refusal = Refusal; return Outcome; }
                // The corner points are exactly on the carrier up to rounding: keep the shared corner as the end.
                Image.Start = Starts[Index];
                Image.End   = Ends[Index];
            }
        }
        WriteChain(Result, Images, Chain.ClosedCondition, Identifier);
        ++Outcome.Chains;
        Outcome.Closed += Chain.ClosedCondition ? 1 : 0;
    }
    Outcome.Sketch = std::move(Result);
    return Outcome;
}

SketchModel PlaceSketchModel(const SketchModel& Sketch, const SketchPlacement& Placement)
{
    SketchModel Placed = Sketch;
    for (SketchPoint& Point : Placed.Points) Point.Position = Placement.Origin + Placement.XAxis * Point.Position.X + Placement.YAxis * Point.Position.Y;
    if (Cross(Placement.XAxis, Placement.YAxis) < 0.0)
        for (SketchCurve& Curve : Placed.Curves) if (Curve.Kind == SketchCurveCategory::Arc) std::swap(Curve.Start, Curve.End);   // Arcs stay counter-clockwise
    return Placed;
}

SketchJoinOutcome JoinSketchModels(const SketchModel& Base, const SketchModel& Joined, const SketchPlacement& Placement, double Join)
{
    SketchJoinOutcome Outcome;
    const Vector2 X = Placement.XAxis, Y = Placement.YAxis;
    if (std::fabs(Length(X) - 1.0) > AxisSlack || std::fabs(Length(Y) - 1.0) > AxisSlack || std::fabs(Dot(X, Y)) > AxisSlack)
    {
        Outcome.Refusal = "Join: the placement axes are not orthonormal";
        return Outcome;
    }
    const bool Mirrored = Cross(X, Y) < 0.0;
    const bool Identity = X.X == 1.0 && X.Y == 0.0 && Y.X == 0.0 && Y.Y == 1.0 && Placement.Origin.X == 0.0 && Placement.Origin.Y == 0.0;
    const SketchModel Placed = PlaceSketchModel(Joined, Placement);
    SketchModel Result = Base;

    // ① Points: curve ends weld into curve ends already present (base first, then earlier joined points).
    std::vector<bool> EndCondition(Base.Points.size() + Joined.Points.size(), false);
    auto MarkEnds = [&](const SketchModel& Sketch, size_t Shift)
    {
        for (const SketchCurve& Curve : Sketch.Curves)
        {
            if (Curve.Start >= 0) EndCondition[Shift + Curve.Start] = true;
            if (Curve.End >= 0) EndCondition[Shift + Curve.End] = true;
            if (Curve.Kind == SketchCurveCategory::Spline && !Curve.Points.empty()) { EndCondition[Shift + Curve.Points.front()] = true; EndCondition[Shift + Curve.Points.back()] = true; }
        }
    };
    MarkEnds(Base, 0);
    MarkEnds(Placed, Base.Points.size());
    std::vector<int> PointMap(Placed.Points.size(), -1);
    std::vector<bool> ResultEnd(EndCondition.begin(), EndCondition.begin() + static_cast<std::ptrdiff_t>(Base.Points.size()));
    for (size_t Index = 0; Index < Placed.Points.size(); ++Index)
    {
        const bool End = EndCondition[Base.Points.size() + Index];
        if (End)
        {
            for (size_t Existing = 0; Existing < Result.Points.size(); ++Existing)
            {
                if (!ResultEnd[Existing] || Distance(Result.Points[Existing].Position, Placed.Points[Index].Position) > Join) continue;
                PointMap[Index] = static_cast<int>(Existing);
                ++Outcome.Welded;
                break;
            }
        }
        if (PointMap[Index] >= 0) continue;
        PointMap[Index] = static_cast<int>(Result.Points.size());
        Result.Points.push_back(Placed.Points[Index]);
        ResultEnd.push_back(End);
    }

    // ② Curves with identifiers after the base's largest.
    int Largest = 0;
    for (const SketchCurve& Curve : Base.Curves) Largest = std::max(Largest, Curve.Identifier);
    const int CurveShift = static_cast<int>(Base.Curves.size());
    auto MapPoint = [&](int Index) { return Index < 0 ? Index : PointMap[Index]; };
    for (SketchCurve Curve : Placed.Curves)
    {
        Curve.Identifier += Largest;
        Curve.Start  = MapPoint(Curve.Start);
        Curve.End    = MapPoint(Curve.End);
        Curve.Centre = MapPoint(Curve.Centre);
        for (int& Point : Curve.Points) Point = MapPoint(Point);
        Result.Curves.push_back(std::move(Curve));
    }

    // ③ Constraints renumbered; frame-bound ones re-expressed on axis-aligned placements, else left out.
    const auto Aligned = [](Vector2 Axis, Vector2 Unit) { return std::fabs(std::fabs(Dot(Axis, Unit)) - 1.0) <= AxisSlack; };
    for (SketchConstraint Constraint : Joined.Constraints)
    {
        const ConstraintShape Shape = ShapeOf(Constraint.Kind);
        auto Renumber = [&](int& Reference, ReferenceGroup Family)
        {
            if (Reference < 0 || Family == ReferenceGroup::None) return;
            Reference = Family == ReferenceGroup::Point ? MapPoint(Reference) : Reference + CurveShift;
        };
        Renumber(Constraint.First, Shape.First);
        Renumber(Constraint.Second, Shape.Second);
        Renumber(Constraint.Third, Shape.Third);
        bool Keep = true;
        switch (Constraint.Kind)
        {
        case SketchConstraintCategory::Coincident:
            if (Constraint.First == Constraint.Second) continue;                             // Welded already: a self pair has a zero row
            break;
        case SketchConstraintCategory::Fixed:
        {
            const Vector2 Position = Placement.Origin + X * Constraint.Value + Y * Constraint.SecondValue;
            Constraint.Value       = Position.X;
            Constraint.SecondValue = Position.Y;
            Keep = Constraint.Parameter.empty() || Identity;
            break;
        }
        case SketchConstraintCategory::Horizontal:
        case SketchConstraintCategory::Vertical:
        {
            const Vector2 Axis = Constraint.Kind == SketchConstraintCategory::Horizontal ? X : Y;  // Image of the line's direction
            if (Aligned(Axis, { 1.0, 0.0 })) Constraint.Kind = SketchConstraintCategory::Horizontal;
            else if (Aligned(Axis, { 0.0, 1.0 })) Constraint.Kind = SketchConstraintCategory::Vertical;
            else Keep = false;
            break;
        }
        case SketchConstraintCategory::HorizontalDistance:
        case SketchConstraintCategory::VerticalDistance:
        {
            const Vector2 Axis = Constraint.Kind == SketchConstraintCategory::HorizontalDistance ? X : Y;
            double Sign = 0.0;
            if (Aligned(Axis, { 1.0, 0.0 })) { Constraint.Kind = SketchConstraintCategory::HorizontalDistance; Sign = Axis.X > 0.0 ? 1.0 : -1.0; }
            else if (Aligned(Axis, { 0.0, 1.0 })) { Constraint.Kind = SketchConstraintCategory::VerticalDistance; Sign = Axis.Y > 0.0 ? 1.0 : -1.0; }
            if (Sign == 0.0 || (Sign < 0.0 && !Constraint.Parameter.empty())) Keep = false;
            else Constraint.Value *= Sign;
            break;
        }
        case SketchConstraintCategory::Angle:
            if (Mirrored)
            {
                if (Constraint.Parameter.empty()) Constraint.Value = -Constraint.Value;     // A mirror turns counter-clockwise angles clockwise
                else Keep = false;
            }
            break;
        default:
            break;
        }
        if (!Keep) { ++Outcome.Dropped; continue; }
        Result.Constraints.push_back(std::move(Constraint));
    }
    Outcome.Sketch  = std::move(Result);
    return Outcome;
}

bool SketchContactCondition(const SketchModel& First, const SketchModel& Second, double Join)
{
    auto Segments = [&](const SketchModel& Sketch)
    {
        std::vector<ProfileSegment> Pieces;
        for (size_t Index = 0; Index < Sketch.Curves.size(); ++Index)
        {
            if (Sketch.Curves[Index].Construction) continue;
            std::string Refusal;
            const ProfileSegment Piece = ResolveSketchCurveSegment(Sketch, static_cast<int>(Index), Join, Refusal);
            if (Refusal.empty()) Pieces.push_back(Piece);
        }
        return Pieces;
    };
    const std::vector<ProfileSegment> FirstPieces = Segments(First), SecondPieces = Segments(Second);
    for (const ProfileSegment& One : FirstPieces)
        for (const ProfileSegment& Other : SecondPieces)
        {
            if (Distance(One.Start, Other.Start) <= Join || Distance(One.Start, Other.End) <= Join || Distance(One.End, Other.Start) <= Join || Distance(One.End, Other.End) <= Join) return true;
            if (!IntersectProfileSegments(One, Other, Join).empty()) return true;
        }
    return false;
}

} // namespace Frontier::GeometrySpace::Parametric
