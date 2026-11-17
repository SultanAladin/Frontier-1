/*====================================================================================================================================
                                                       SKETCHPROFILE.CPP
====================================================================================================================================*/
// 🧩 Closed profile loops from a solved sketch: chaining, nesting, orientation (outer CCW, holes CW), exact area

#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double FullTurn = 2.0 * std::numbers::pi;                                      // [rad] - 2π

    // Polyline through a segment: one piece per line, ≥ 4 and ~64 per turn for arcs, 16 per spline knot span.
    [[nodiscard]] std::vector<Vector2> SampleSegment(const ProfileSegment& Segment)
    {
        std::vector<Vector2> Samples;
        if (Segment.Kind == ProfileSegmentKind::Spline)
        {
            const std::vector<double> Breaks = CollectProfileBreaks(Segment);
            for (size_t Piece = 0; Piece + 1 < Breaks.size(); ++Piece)
            {
                for (int Index = Piece == 0 ? 0 : 1; Index <= 16; ++Index) Samples.push_back(EvaluateProfileSegment(Segment, Breaks[Piece] + (Breaks[Piece + 1] - Breaks[Piece]) * Index / 16.0));
            }
            return Samples;
        }
        const int Pieces = Segment.Kind == ProfileSegmentKind::Line ? 1 : std::max(4, static_cast<int>(std::ceil(std::fabs(Segment.Sweep) / FullTurn * 64.0)));
        for (int Index = 0; Index <= Pieces; ++Index) Samples.push_back(EvaluateProfileSegment(Segment, static_cast<double>(Index) / Pieces));
        return Samples;
    }

    [[nodiscard]] std::vector<Vector2> SampleLoop(const ProfileLoop& Loop)
    {
        std::vector<Vector2> Samples;
        for (const ProfileSegment& Segment : Loop.Segments)
        {
            std::vector<Vector2> Piece = SampleSegment(Segment);
            Samples.insert(Samples.end(), Piece.begin(), Piece.end() - 1);
        }
        return Samples;
    }

    // Winding number of a closed polyline about Probe (non-zero: inside).
    [[nodiscard]] int WindingNumber(const std::vector<Vector2>& Polygon, Vector2 Probe)
    {
        int Winding = 0;
        for (size_t Index = 0; Index < Polygon.size(); ++Index)
        {
            const Vector2 First = Polygon[Index], Second = Polygon[(Index + 1) % Polygon.size()];
            const double  Side  = (Second.X - First.X) * (Probe.Y - First.Y) - (Probe.X - First.X) * (Second.Y - First.Y);
            if (First.Y <= Probe.Y) { if (Second.Y > Probe.Y && Side > 0.0) ++Winding; }
            else if (Second.Y <= Probe.Y && Side < 0.0) --Winding;
        }
        return Winding;
    }

    [[nodiscard]] double Orientation(Vector2 First, Vector2 Second, Vector2 Third)
    {
        return (Second.X - First.X) * (Third.Y - First.Y) - (Second.Y - First.Y) * (Third.X - First.X);
    }

    // Proper crossing of two pieces (strict sign changes on both: touching at shared ends is not a crossing).
    [[nodiscard]] bool PiecesCross(Vector2 A, Vector2 B, Vector2 C, Vector2 D, double Tolerance)
    {
        const double First = Orientation(A, B, C), Second = Orientation(A, B, D);
        const double Third = Orientation(C, D, A), Fourth = Orientation(C, D, B);
        const double Cut   = Tolerance * Tolerance;
        return ((First > Cut && Second < -Cut) || (First < -Cut && Second > Cut)) && ((Third > Cut && Fourth < -Cut) || (Third < -Cut && Fourth > Cut));
    }

    // ∮ over one segment of (x·dy − y·dx)/2, x²·dy/2 and −y²·dx/2 (area and first moments) by 16-point Gauss–Legendre.
    void IntegrateSegment(const ProfileSegment& Segment, double& Area, double& MomentX, double& MomentY)
    {
        static constexpr double Nodes[8]   = { 0.0950125098376374, 0.2816035507792589, 0.4580167776572274, 0.6178762444026438,
                                               0.7554044083550030, 0.8656312023878318, 0.9445750230732326, 0.9894009349916499 };
        static constexpr double Weights[8] = { 0.1894506104550685, 0.1826034150449236, 0.1691565193950025, 0.1495959888165767,
                                               0.1246289712555339, 0.0951585116824928, 0.0622535239386479, 0.0271524594117541 };
        const std::vector<double> Breaks = CollectProfileBreaks(Segment);
        for (size_t Piece = 0; Piece + 1 < Breaks.size(); ++Piece)
        {
            const double Low = Breaks[Piece], High = Breaks[Piece + 1];
            for (int GaussIndex = 0; GaussIndex < 16; ++GaussIndex)
            {
                const double Abscissa = (GaussIndex < 8 ? -Nodes[7 - GaussIndex] : Nodes[GaussIndex - 8]);
                const double Weight   = (GaussIndex < 8 ? Weights[7 - GaussIndex] : Weights[GaussIndex - 8]) * 0.5 * (High - Low);
                const double Fraction = Low + (Abscissa + 1.0) * 0.5 * (High - Low);
                const Vector2 Point   = EvaluateProfileSegment(Segment, Fraction);
                const Vector2 Tangent = EvaluateProfileTangent(Segment, Fraction);
                Area    += Weight * 0.5 * (Point.X * Tangent.Y - Point.Y * Tangent.X);
                MomentX += Weight * 0.5 * Point.X * Point.X * Tangent.Y;
                MomentY -= Weight * 0.5 * Point.Y * Point.Y * Tangent.X;
            }
        }
    }

    [[nodiscard]] double MeasureLoopArea(const ProfileLoop& Loop)
    {
        double Area = 0.0, MomentX = 0.0, MomentY = 0.0;
        for (const ProfileSegment& Segment : Loop.Segments) IntegrateSegment(Segment, Area, MomentX, MomentY);
        return Area;
    }

    void ReverseLoop(ProfileLoop& Loop)
    {
        std::reverse(Loop.Segments.begin(), Loop.Segments.end());
        for (ProfileSegment& Segment : Loop.Segments) ReverseProfileSegment(Segment);
        Loop.SignedArea = -Loop.SignedArea;
    }

    // Union–find root with path halving.
    [[nodiscard]] int FindRoot(std::vector<int>& Roots, int Index)
    {
        while (Roots[Index] != Index) { Roots[Index] = Roots[Roots[Index]]; Index = Roots[Index]; }
        return Index;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

Vector2 EvaluateProfileSegment(const ProfileSegment& Segment, double Fraction) noexcept
{
    if (Segment.Kind == ProfileSegmentKind::Line) return Segment.Start + (Segment.End - Segment.Start) * Fraction;
    if (Segment.Kind == ProfileSegmentKind::Spline)
    {
        if (Fraction <= 0.0) return Segment.Start;
        if (Fraction >= 1.0) return Segment.End;
        const double Low = Segment.Spline.Knots.front(), High = Segment.Spline.Knots.back();
        return EvaluateSketchSpline(Segment.Spline, Low + Fraction * (High - Low)).Position;
    }
    const double Phi = Segment.StartAngle + Fraction * Segment.Sweep;                        // [rad] - Angle at Fraction
    return Segment.Centre + Vector2{ std::cos(Phi), std::sin(Phi) } * Segment.Radius;
}

Vector2 EvaluateProfileTangent(const ProfileSegment& Segment, double Fraction) noexcept
{
    if (Segment.Kind == ProfileSegmentKind::Line) return Segment.End - Segment.Start;
    if (Segment.Kind == ProfileSegmentKind::Spline)
    {
        const double Low = Segment.Spline.Knots.front(), High = Segment.Spline.Knots.back();
        return EvaluateSketchSpline(Segment.Spline, Low + Fraction * (High - Low)).First * (High - Low);
    }
    const double Phi = Segment.StartAngle + Fraction * Segment.Sweep;                        // [rad] - Angle at Fraction
    return Vector2{ -std::sin(Phi), std::cos(Phi) } * (Segment.Radius * Segment.Sweep);
}

std::vector<double> CollectProfileBreaks(const ProfileSegment& Segment)
{
    std::vector<double> Breaks;
    if (Segment.Kind == ProfileSegmentKind::Spline)
    {
        const double Low = Segment.Spline.Knots.front(), High = Segment.Spline.Knots.back();
        for (double Knot : CollectSketchSplineBreaks(Segment.Spline)) Breaks.push_back((Knot - Low) / (High - Low));
        Breaks.front() = 0.0;
        Breaks.back()  = 1.0;
        return Breaks;
    }
    const int Pieces = Segment.Kind == ProfileSegmentKind::Line ? 1 : std::max(1, static_cast<int>(std::ceil(std::fabs(Segment.Sweep) / (std::numbers::pi / 4.0))));
    for (int Piece = 0; Piece <= Pieces; ++Piece) Breaks.push_back(static_cast<double>(Piece) / Pieces);
    return Breaks;
}

void ReverseProfileSegment(ProfileSegment& Segment) noexcept
{
    if (Segment.Kind == ProfileSegmentKind::Spline) ReverseSketchSpline(Segment.Spline);
    std::swap(Segment.Start, Segment.End);
    Segment.StartAngle += Segment.Sweep;
    Segment.Sweep       = -Segment.Sweep;
}

ProfileSegment ResolveSketchCurveSegment(const SketchModel& Sketch, int CurveIndex, double Join, std::string& Refusal)
{
    const SketchCurve& Curve = Sketch.Curves[CurveIndex];
    ProfileSegment Segment;
    Segment.Curve = CurveIndex;
    Segment.Name  = SketchCurveName(Curve);
    if (Curve.Kind == SketchCurveKind::Circle)
    {
        if (!(Curve.Radius > Join)) { Refusal = "circle " + Segment.Name + " has no radius"; return Segment; }
        Segment.Kind   = ProfileSegmentKind::Circle;
        Segment.Centre = Sketch.Points[Curve.Centre].Position;
        Segment.Radius = Curve.Radius;
        Segment.Sweep  = FullTurn;
        Segment.Start  = Segment.End = Segment.Centre + Vector2{ Curve.Radius, 0.0 };
        return Segment;
    }
    if (Curve.Kind == SketchCurveKind::Spline)
    {
        const SketchSplineOutcome Resolved = ResolveSketchCurveSpline(Sketch, Curve);
        if (!Resolved.Refusal.empty()) { Refusal = Resolved.Refusal; return Segment; }
        Segment.Kind   = ProfileSegmentKind::Spline;
        Segment.Spline = Resolved.Curve;
        Segment.Start  = Resolved.Curve.Poles.front();
        Segment.End    = Resolved.Curve.Poles.back();
        return Segment;
    }
    Segment.Start = Sketch.Points[Curve.Start].Position;
    Segment.End   = Sketch.Points[Curve.End].Position;
    if (Distance(Segment.Start, Segment.End) <= Join) { Refusal = Segment.Name + " has zero length"; return Segment; }
    if (Curve.Kind == SketchCurveKind::Arc)
    {
        Segment.Kind       = ProfileSegmentKind::Arc;
        Segment.Centre     = Sketch.Points[Curve.Centre].Position;
        Segment.Radius     = Distance(Segment.Start, Segment.Centre);
        Segment.StartAngle = std::atan2(Segment.Start.Y - Segment.Centre.Y, Segment.Start.X - Segment.Centre.X);
        double EndAngle    = std::atan2(Segment.End.Y - Segment.Centre.Y, Segment.End.X - Segment.Centre.X);
        double Sweep       = std::fmod(EndAngle - Segment.StartAngle + 2.0 * FullTurn, FullTurn);
        if (Sweep <= 1.0e-12) Sweep += FullTurn;
        Segment.Sweep = Sweep;
        Segment.End   = EvaluateProfileSegment(Segment, 1.0);                                 // Exactly on the circle
    }
    return Segment;
}

SketchProfile ExtractSketchProfile(const SketchModel& Sketch)
{
    SketchProfile Profile;

    // ① Scale and point clusters (coincident endpoints join even without a shared index).
    Vector2 Low{ 1e300, 1e300 }, High{ -1e300, -1e300 };
    for (const SketchPoint& Point : Sketch.Points)
    {
        Low  = { std::min(Low.X, Point.Position.X), std::min(Low.Y, Point.Position.Y) };
        High = { std::max(High.X, Point.Position.X), std::max(High.Y, Point.Position.Y) };
    }
    Profile.Scale = Sketch.Points.empty() ? 1.0 : std::max(Distance(Low, High), 1.0e-300);
    for (const SketchCurve& Curve : Sketch.Curves) if (Curve.Kind == SketchCurveKind::Circle && !Curve.Construction) Profile.Scale = std::max(Profile.Scale, 2.0 * Curve.Radius);
    const double Join = 1.0e-9 * Profile.Scale;                                              // [len] - Endpoint join distance
    std::vector<int> Roots(Sketch.Points.size());
    for (size_t Index = 0; Index < Roots.size(); ++Index) Roots[Index] = static_cast<int>(Index);
    for (size_t First = 0; First < Sketch.Points.size(); ++First)
    {
        for (size_t Second = First + 1; Second < Sketch.Points.size(); ++Second)
        {
            if (Distance(Sketch.Points[First].Position, Sketch.Points[Second].Position) <= Join) Roots[FindRoot(Roots, static_cast<int>(Second))] = FindRoot(Roots, static_cast<int>(First));
        }
    }

    // ② Segments in natural direction; circles become loops of their own.
    std::vector<ProfileSegment> Open;
    std::vector<std::pair<int, int>> Ends;
    for (int Index = 0; Index < static_cast<int>(Sketch.Curves.size()); ++Index)
    {
        const SketchCurve& Curve = Sketch.Curves[Index];
        if (Curve.Construction) continue;
        std::string Refusal;
        ProfileSegment Segment = ResolveSketchCurveSegment(Sketch, Index, Join, Refusal);
        if (!Refusal.empty()) { Profile.Refusal = Refusal; return Profile; }
        if (Curve.Kind == SketchCurveKind::Circle)
        {
            ProfileLoop Loop;
            Loop.Segments.push_back(Segment);
            Profile.Loops.push_back(Loop);
            continue;
        }
        if (Curve.Kind == SketchCurveKind::Spline)
        {
            Open.push_back(Segment);
            Ends.push_back({ FindRoot(Roots, Curve.Points.front()), FindRoot(Roots, Curve.Points.back()) });
            continue;
        }
        Open.push_back(Segment);
        Ends.push_back({ FindRoot(Roots, Curve.Start), FindRoot(Roots, Curve.End) });
    }

    // ③ Chain: every junction must join exactly two segments.
    std::vector<std::vector<int>> Incident(Sketch.Points.size());
    for (int Index = 0; Index < static_cast<int>(Open.size()); ++Index)
    {
        Incident[Ends[Index].first].push_back(Index);
        Incident[Ends[Index].second].push_back(Index);
    }
    for (size_t Point = 0; Point < Incident.size(); ++Point)
    {
        if (Incident[Point].empty() || Incident[Point].size() == 2) continue;
        const char* Problem = Incident[Point].size() == 1 ? "open profile: " : "branching profile: ";
        Profile.Refusal = Problem + Open[Incident[Point][0]].Name + " ends at a point joined to " + std::to_string(Incident[Point].size() - 1) + " other curves";
        return Profile;
    }
    std::vector<bool> Used(Open.size(), false);
    for (int Seed = 0; Seed < static_cast<int>(Open.size()); ++Seed)
    {
        if (Used[Seed]) continue;
        ProfileLoop Loop;
        int Current = Seed, Junction = Ends[Seed].first;
        while (!Used[Current])
        {
            Used[Current] = true;
            ProfileSegment Segment = Open[Current];
            if (Ends[Current].first != Junction) ReverseProfileSegment(Segment);
            Junction = Ends[Current].first == Junction ? Ends[Current].second : Ends[Current].first;
            Loop.Segments.push_back(Segment);
            const std::vector<int>& Next = Incident[Junction];
            Current = Next[0] == Current ? Next[1] : Next[0];
        }
        Profile.Loops.push_back(Loop);
    }
    if (Profile.Loops.empty()) { Profile.Refusal = "sketch has no profile curves"; return Profile; }

    ArrangeProfileLoops(Profile);
    return Profile;
}

void ArrangeProfileLoops(SketchProfile& Profile)
{
    const double Join = 1.0e-9 * Profile.Scale;                                              // [len] - Touch distance
    // ④ No crossings between any two segments (adjacent ones may only touch at their shared end).
    std::vector<std::vector<Vector2>> Samples;
    std::vector<std::pair<int, int>>  Owners;
    for (int Loop = 0; Loop < static_cast<int>(Profile.Loops.size()); ++Loop)
    {
        for (int Segment = 0; Segment < static_cast<int>(Profile.Loops[Loop].Segments.size()); ++Segment)
        {
            Samples.push_back(SampleSegment(Profile.Loops[Loop].Segments[Segment]));
            Owners.push_back({ Loop, Segment });
        }
    }
    for (size_t First = 0; First < Samples.size(); ++First)
    {
        // A spline against itself: non-neighbouring pieces must not cross (a closed spline's two end pieces touch).
        const ProfileSegment& Own = Profile.Loops[Owners[First].first].Segments[Owners[First].second];
        const size_t Pieces = Samples[First].size() - 1;
        for (size_t Low1 = 0; Own.Kind == ProfileSegmentKind::Spline && Low1 < Pieces; ++Low1)
        {
            for (size_t Low2 = Low1 + 2; Low2 < Pieces; ++Low2)
            {
                if (!PiecesCross(Samples[First][Low1], Samples[First][Low1 + 1], Samples[First][Low2], Samples[First][Low2 + 1], Join)) continue;
                Profile.Refusal = "profile crosses itself: " + Own.Name + " × " + Own.Name;
                return;
            }
        }
        for (size_t Second = First + 1; Second < Samples.size(); ++Second)
        {
            for (size_t Low1 = 0; Low1 + 1 < Samples[First].size(); ++Low1)
            {
                for (size_t Low2 = 0; Low2 + 1 < Samples[Second].size(); ++Low2)
                {
                    if (!PiecesCross(Samples[First][Low1], Samples[First][Low1 + 1], Samples[Second][Low2], Samples[Second][Low2 + 1], Join)) continue;
                    const ProfileSegment& One = Profile.Loops[Owners[First].first].Segments[Owners[First].second];
                    const ProfileSegment& Two = Profile.Loops[Owners[Second].first].Segments[Owners[Second].second];
                    Profile.Refusal = "profile crosses itself: " + One.Name + " × " + Two.Name;
                    return;
                }
            }
        }
    }

    // ⑤ Nesting depth by winding number of a sample point; orientation outer CCW (even depth), holes CW.
    std::vector<std::vector<Vector2>> Polygons;
    for (ProfileLoop& Loop : Profile.Loops)
    {
        Loop.SignedArea = MeasureLoopArea(Loop);
        Polygons.push_back(SampleLoop(Loop));
    }
    for (size_t Loop = 0; Loop < Profile.Loops.size(); ++Loop)
    {
        const Vector2 Probe = EvaluateProfileSegment(Profile.Loops[Loop].Segments[0], 0.5);
        double SmallestEnclosure = 1e300;
        for (size_t Other = 0; Other < Profile.Loops.size(); ++Other)
        {
            if (Other == Loop || WindingNumber(Polygons[Other], Probe) == 0) continue;
            ++Profile.Loops[Loop].Depth;
            const double Size = std::fabs(Profile.Loops[Other].SignedArea);
            if (Size < SmallestEnclosure) { SmallestEnclosure = Size; Profile.Loops[Loop].Enclosure = static_cast<int>(Other); }
        }
    }
    double MomentX = 0.0, MomentY = 0.0, Area = 0.0;
    for (ProfileLoop& Loop : Profile.Loops)
    {
        const bool Outer = Loop.Depth % 2 == 0;
        if ((Loop.SignedArea > 0.0) != Outer) ReverseLoop(Loop);
        if (Outer) Loop.Enclosure = -1;
        for (const ProfileSegment& Segment : Loop.Segments) IntegrateSegment(Segment, Area, MomentX, MomentY);
    }
    Profile.Area     = Area;
    Profile.Centroid = Area != 0.0 ? Vector2{ MomentX / Area, MomentY / Area } : Vector2{};
}

} // namespace Frontier::GeometrySpace::Parametric
