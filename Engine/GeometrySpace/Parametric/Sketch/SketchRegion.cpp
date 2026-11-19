/*====================================================================================================================================
                                                         SKETCHREGION.CPP
====================================================================================================================================*/
// 🧩 Closed regions of overlapping sketch curves: arrangement by crossings, faces traced left-handed, the face under a point

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchRegion.h"

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchTrim.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double MergeFraction   = 1.0e-9;                                               // [-]   - Breaks closer than this are one
    constexpr double VertexShare     = 1.0e-6;                                               // [-]   - Piece ends within this · Scale merge
    constexpr double JoinShare       = 1.0e-7;                                               // [-]   - Crossing acceptance · Scale
    constexpr double ProbeFraction   = 1.0e-4;                                               // [-]   - Departure probe along a piece
    constexpr double KnotShare       = 1.0e-12;                                              // [-]   - Knots this close (· domain) are one

    // 📝 One piece of the arrangement: a source segment between two breaks, and its end vertices.
    struct ArrangementPiece
    {
        int    Source = -1;                                                                  // [-]
        double From   = 0.0;                                                                 // [-] - Fraction on the source
        double To     = 1.0;                                                                 // [-]
        int    Start  = -1;                                                                  // [-] - Vertex index
        int    End    = -1;                                                                  // [-]
        bool   AliveCondition = true;                                                        // [-] - Not pruned as dangling
    };

    // ---- Spline knot insertion ---------------------------------------------------------------------------------------

    [[nodiscard]] int CountKnot(const std::vector<double>& Knots, double Value, double Gap)
    {
        return static_cast<int>(std::count_if(Knots.begin(), Knots.end(), [&](double Knot) { return std::fabs(Knot - Value) <= Gap; }));
    }

    // Boehm: one insertion of τ (the curve is unchanged, one more pole).
    void InsertSplineKnot(SketchSplineCurve& Spline, double Value)
    {
        const int Degree = Spline.Degree;
        const int Count  = static_cast<int>(Spline.Poles.size());
        int Span = Degree;                                                                   // U[k] ≤ τ < U[k+1]
        while (Span + 1 < Count && Spline.Knots[Span + 1] <= Value) ++Span;
        std::vector<Vector2> Poles(Count + 1);
        for (int Index = 0; Index <= Count; ++Index)
        {
            if (Index <= Span - Degree) Poles[Index] = Spline.Poles[Index];
            else if (Index >= Span + 1) Poles[Index] = Spline.Poles[Index - 1];
            else
            {
                const double Width = Spline.Knots[Index + Degree] - Spline.Knots[Index];
                const double Blend = Width > 0.0 ? (Value - Spline.Knots[Index]) / Width : 0.0;
                Poles[Index] = Spline.Poles[Index - 1] * (1.0 - Blend) + Spline.Poles[Index] * Blend;
            }
        }
        Spline.Poles = std::move(Poles);
        Spline.Knots.insert(Spline.Knots.begin() + Span + 1, Value);
    }

    // τ raised to multiplicity Degree (snapped onto an existing knot within the gap); returns the value used.
    double RaiseSplineKnot(SketchSplineCurve& Spline, double Value, double Gap)
    {
        for (double Knot : Spline.Knots) if (std::fabs(Knot - Value) <= Gap) { Value = Knot; break; }
        while (CountKnot(Spline.Knots, Value, 0.0) < Spline.Degree) InsertSplineKnot(Spline, Value);
        return Value;
    }

    // The clamped sub-curve over [Low, High] (exact).
    [[nodiscard]] SketchSplineCurve RestrictSpline(const SketchSplineCurve& Source, double Low, double High)
    {
        SketchSplineCurve Spline = Source;
        const double Front = Spline.Knots.front(), Back = Spline.Knots.back();
        const double Gap   = KnotShare * std::max(1.0, Back - Front);
        const int    Degree = Spline.Degree;
        if (Low > Front + Gap) Low = RaiseSplineKnot(Spline, Low, Gap); else Low = Front;
        if (High < Back - Gap) High = RaiseSplineKnot(Spline, High, Gap); else High = Back;
        int LowIndex = 0, HighIndex = static_cast<int>(Spline.Knots.size()) - 1;
        for (int Index = 0; Index < static_cast<int>(Spline.Knots.size()); ++Index) if (Spline.Knots[Index] == Low) LowIndex = Index;
        for (int Index = static_cast<int>(Spline.Knots.size()) - 1; Index >= 0; --Index) if (Spline.Knots[Index] == High) HighIndex = Index;
        SketchSplineCurve Piece;
        Piece.Degree = Degree;
        Piece.Poles.assign(Spline.Poles.begin() + (LowIndex - Degree), Spline.Poles.begin() + HighIndex);
        Piece.Knots.assign(Degree + 1, Low);
        for (int Index = LowIndex + 1; Index < HighIndex; ++Index) Piece.Knots.push_back(Spline.Knots[Index]);
        Piece.Knots.insert(Piece.Knots.end(), Degree + 1, High);
        return Piece;
    }

    // ---- Segments ----------------------------------------------------------------------------------------------------

    // The exact piece of Source between two fractions, in the source direction.
    [[nodiscard]] ProfileSegment SliceSegment(const ProfileSegment& Source, double From, double To)
    {
        ProfileSegment Piece = Source;
        const bool Whole = From <= MergeFraction && To >= 1.0 - MergeFraction;
        if (Whole) return Piece;
        if (Source.Kind == ProfileSegmentCategory::Spline)
        {
            const double Low = Source.Spline.Knots.front(), High = Source.Spline.Knots.back();
            Piece.Spline = RestrictSpline(Source.Spline, Low + From * (High - Low), Low + To * (High - Low));
            Piece.Start  = Piece.Spline.Poles.front();
            Piece.End    = Piece.Spline.Poles.back();
            return Piece;
        }
        if (Source.Kind != ProfileSegmentCategory::Line)
        {
            Piece.Kind       = ProfileSegmentCategory::Arc;
            Piece.StartAngle = Source.StartAngle + From * Source.Sweep;
            Piece.Sweep      = (To - From) * Source.Sweep;
        }
        Piece.Start = EvaluateProfileSegment(Source, From);
        Piece.End   = EvaluateProfileSegment(Source, To);
        return Piece;
    }

    // Closed or open polyline of a segment (end point included).
    void SampleSegment(const ProfileSegment& Segment, std::vector<Vector2>& Points)
    {
        int Count = 1;
        if (Segment.Kind == ProfileSegmentCategory::Arc || Segment.Kind == ProfileSegmentCategory::Circle) Count = std::max(4, static_cast<int>(std::ceil(std::fabs(Segment.Sweep) / 0.05)));
        if (Segment.Kind == ProfileSegmentCategory::Spline) Count = 16 * std::max<int>(1, static_cast<int>(Segment.Spline.Knots.size()) - 2 * Segment.Spline.Degree - 1);
        for (int Index = 1; Index <= Count; ++Index) Points.push_back(EvaluateProfileSegment(Segment, static_cast<double>(Index) / Count));
    }

    [[nodiscard]] double MeasureArea(const std::vector<Vector2>& Outline)
    {
        double Twice = 0.0;
        for (size_t Index = 0; Index < Outline.size(); ++Index)
        {
            const Vector2& Here = Outline[Index];
            const Vector2& Next = Outline[(Index + 1) % Outline.size()];
            Twice += Here.X * Next.Y - Next.X * Here.Y;
        }
        return 0.5 * Twice;
    }

    // Winding number of a closed polyline about Point (0: outside).
    [[nodiscard]] int MeasureWinding(const std::vector<Vector2>& Outline, Vector2 Point)
    {
        int Winding = 0;
        for (size_t Index = 0; Index < Outline.size(); ++Index)
        {
            const Vector2& Here = Outline[Index];
            const Vector2& Next = Outline[(Index + 1) % Outline.size()];
            const double   Side = (Next.X - Here.X) * (Point.Y - Here.Y) - (Point.X - Here.X) * (Next.Y - Here.Y);
            if (Here.Y <= Point.Y) { if (Next.Y > Point.Y && Side > 0.0) ++Winding; }
            else if (Next.Y <= Point.Y && Side < 0.0) --Winding;
        }
        return Winding;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::vector<Vector2>> ExtractSplineBezierSpans(const SketchSplineCurve& Source, double Low, double High)
{
    SketchSplineCurve Spline = RestrictSpline(Source, Low, High);
    const double Gap = KnotShare * std::max(1.0, Spline.Knots.back() - Spline.Knots.front());
    std::vector<double> Interior;
    for (size_t Index = Spline.Degree + 1; Index + Spline.Degree + 1 < Spline.Knots.size(); ++Index)
        if (Interior.empty() || Spline.Knots[Index] > Interior.back()) Interior.push_back(Spline.Knots[Index]);
    for (double Knot : Interior) (void)RaiseSplineKnot(Spline, Knot, Gap);
    std::vector<std::vector<Vector2>> Spans;
    const int Degree = Spline.Degree;
    for (size_t First = 0; First + Degree < Spline.Poles.size(); First += Degree)
        Spans.emplace_back(Spline.Poles.begin() + First, Spline.Poles.begin() + First + Degree + 1);
    return Spans;
}

SketchRegion LocateSketchRegion(const std::vector<ProfileSegment>& Segments, Vector2 Point, double Scale)
{
    SketchRegion Region;
    Scale = std::max(Scale, 1.0e-9);
    const double Join   = JoinShare * Scale;                                                 // [len]
    const double Share  = VertexShare * Scale;                                               // [len]
    if (Segments.empty()) { Region.Refusal = "no curves to fill between"; return Region; }

    // ① Breaks: both ends and every crossing with another segment.
    std::vector<std::vector<double>> Breaks(Segments.size(), std::vector<double>{ 0.0, 1.0 });
    for (size_t First = 0; First < Segments.size(); ++First)
        for (size_t Second = First + 1; Second < Segments.size(); ++Second)
            for (const SegmentCrossing& Crossing : IntersectProfileSegments(Segments[First], Segments[Second], Join))
            {
                Breaks[First].push_back(std::clamp(Crossing.TargetFraction, 0.0, 1.0));
                Breaks[Second].push_back(std::clamp(Crossing.CutterFraction, 0.0, 1.0));
            }

    // ② Pieces and their end vertices (merged within Share).
    std::vector<Vector2>          Vertices;
    std::vector<ArrangementPiece> Pieces;
    const auto Vertex = [&](Vector2 Position) -> int
    {
        for (size_t Index = 0; Index < Vertices.size(); ++Index) if (Distance(Vertices[Index], Position) <= Share) return static_cast<int>(Index);
        Vertices.push_back(Position);
        return static_cast<int>(Vertices.size()) - 1;
    };
    for (size_t Source = 0; Source < Segments.size(); ++Source)
    {
        std::vector<double>& Fractions = Breaks[Source];
        std::sort(Fractions.begin(), Fractions.end());
        Fractions.erase(std::unique(Fractions.begin(), Fractions.end(), [](double Left, double Right) { return Right - Left <= MergeFraction; }), Fractions.end());
        Fractions.back() = 1.0;
        for (size_t Index = 0; Index + 1 < Fractions.size(); ++Index)
        {
            ArrangementPiece Piece;
            Piece.Source = static_cast<int>(Source);
            Piece.From   = Fractions[Index];
            Piece.To     = Fractions[Index + 1];
            Piece.Start  = Vertex(EvaluateProfileSegment(Segments[Source], Piece.From));
            Piece.End    = Vertex(EvaluateProfileSegment(Segments[Source], Piece.To));
            if (Piece.Start == Piece.End)
            {
                // A piece closing on itself must enclose something (a full circle), not be a sliver between merged ends.
                std::vector<Vector2> Outline{ EvaluateProfileSegment(Segments[Source], Piece.From) };
                SampleSegment(SliceSegment(Segments[Source], Piece.From, Piece.To), Outline);
                if (std::fabs(MeasureArea(Outline)) <= Share * Scale) continue;
            }
            Pieces.push_back(Piece);
        }
    }

    // ②′ Coincident pieces (the same curve drawn twice, or a filled region over the curves it came from) count once.
    for (size_t First = 0; First < Pieces.size(); ++First)
    {
        if (!Pieces[First].AliveCondition) continue;
        const Vector2 Middle = EvaluateProfileSegment(Segments[Pieces[First].Source], 0.5 * (Pieces[First].From + Pieces[First].To));
        for (size_t Second = First + 1; Second < Pieces.size(); ++Second)
        {
            ArrangementPiece& Other = Pieces[Second];
            const bool Same = (Other.Start == Pieces[First].Start && Other.End == Pieces[First].End) || (Other.Start == Pieces[First].End && Other.End == Pieces[First].Start);
            if (!Other.AliveCondition || !Same) continue;
            if (Distance(EvaluateProfileSegment(Segments[Other.Source], 0.5 * (Other.From + Other.To)), Middle) <= Share) Other.AliveCondition = false;
        }
    }

    // ③ Dangling pieces (an end met by nothing else) bound no region.
    std::vector<int> Degree(Vertices.size(), 0);
    for (const ArrangementPiece& Piece : Pieces) if (Piece.AliveCondition) { ++Degree[Piece.Start]; ++Degree[Piece.End]; }
    for (bool Pruned = true; Pruned;)
    {
        Pruned = false;
        for (ArrangementPiece& Piece : Pieces)
        {
            if (!Piece.AliveCondition || (Degree[Piece.Start] > 1 && Degree[Piece.End] > 1)) continue;
            Piece.AliveCondition = false;
            --Degree[Piece.Start];
            --Degree[Piece.End];
            Pruned = true;
        }
    }

    // ④ Half-pieces (2k forward, 2k + 1 backward), sorted counter-clockwise around their origin by departure direction.
    const int HalfCount = static_cast<int>(Pieces.size()) * 2;
    std::vector<double> Departure(HalfCount, 0.0);
    std::vector<std::vector<int>> Around(Vertices.size());
    const auto Origin = [&](int Half) { const ArrangementPiece& Piece = Pieces[Half / 2]; return Half % 2 == 0 ? Piece.Start : Piece.End; };
    for (int Half = 0; Half < HalfCount; ++Half)
    {
        const ArrangementPiece& Piece = Pieces[Half / 2];
        if (!Piece.AliveCondition) continue;
        const ProfileSegment& Source = Segments[Piece.Source];
        const double Probe = ProbeFraction * (Piece.To - Piece.From);
        const double Here  = Half % 2 == 0 ? Piece.From : Piece.To;
        const double Ahead = Half % 2 == 0 ? Piece.From + Probe : Piece.To - Probe;
        const Vector2 Way  = EvaluateProfileSegment(Source, Ahead) - EvaluateProfileSegment(Source, Here);
        Departure[Half] = std::atan2(Way.Y, Way.X);
        Around[Origin(Half)].push_back(Half);
    }
    for (std::vector<int>& Leaving : Around) std::sort(Leaving.begin(), Leaving.end(), [&](int Left, int Right) { return Departure[Left] < Departure[Right]; });
    const auto Next = [&](int Half) -> int
    {
        const int Back = Half ^ 1;                                                           // Twin: leaves the end vertex
        const std::vector<int>& Leaving = Around[Origin(Back)];
        const auto Found = std::find(Leaving.begin(), Leaving.end(), Back);
        const size_t Index = static_cast<size_t>(Found - Leaving.begin());
        return Leaving[(Index + Leaving.size() - 1) % Leaving.size()];                       // First clockwise from the way back
    };

    // ⑤ Faces with their outlines and signed areas.
    struct TracedFace { std::vector<int> Halves; std::vector<Vector2> Outline; double Area = 0.0; int Group = -1; };
    std::vector<TracedFace> Faces;
    std::vector<char> Visited(HalfCount, 0);
    const auto Piece = [&](int Half)
    {
        const ArrangementPiece& Source = Pieces[Half / 2];
        ProfileSegment Segment = SliceSegment(Segments[Source.Source], Source.From, Source.To);
        if (Half % 2 == 1) ReverseProfileSegment(Segment);
        return Segment;
    };
    for (int Seed = 0; Seed < HalfCount; ++Seed)
    {
        if (Visited[Seed] || !Pieces[Seed / 2].AliveCondition) continue;
        TracedFace Face;
        int Half = Seed;
        for (int Guard = 0; Guard <= HalfCount && !Visited[Half]; ++Guard)
        {
            Visited[Half] = 1;
            Face.Halves.push_back(Half);
            const ProfileSegment Segment = Piece(Half);
            if (Face.Outline.empty()) Face.Outline.push_back(Segment.Start);
            SampleSegment(Segment, Face.Outline);
            Half = Next(Half);
        }
        if (Half != Seed) continue;                                                          // Not closed (should not happen)
        if (Face.Outline.size() > 1) Face.Outline.pop_back();                                // Last sample repeats the first
        Face.Area = MeasureArea(Face.Outline);
        Faces.push_back(std::move(Face));
    }

    // ⑥ Curve groups (connected through shared vertices) and each group's outer boundary (its clockwise face).
    std::vector<int> Group(Vertices.size());
    std::iota(Group.begin(), Group.end(), 0);
    const auto Find = [&](int Index) { while (Group[Index] != Index) Index = Group[Index] = Group[Group[Index]]; return Index; };
    for (const ArrangementPiece& Source : Pieces) if (Source.AliveCondition) Group[Find(Source.Start)] = Find(Source.End);
    for (TracedFace& Face : Faces) Face.Group = Find(Origin(Face.Halves.front()));

    // ⑦ The smallest counter-clockwise face around Point.
    const double AreaFloor = 1.0e-12 * Scale * Scale;
    int Chosen = -1;
    for (size_t Index = 0; Index < Faces.size(); ++Index)
    {
        if (!(Faces[Index].Area > AreaFloor) || MeasureWinding(Faces[Index].Outline, Point) == 0) continue;
        if (Chosen < 0 || Faces[Index].Area < Faces[Chosen].Area) Chosen = static_cast<int>(Index);
    }
    if (Chosen < 0) { Region.Refusal = "no closed region here"; return Region; }

    // ⑧ Holes: outer boundaries of other groups inside the chosen face, not inside one another.
    std::vector<int> Holes;
    for (size_t Index = 0; Index < Faces.size(); ++Index)
    {
        const TracedFace& Face = Faces[Index];
        if (!(Face.Area < -AreaFloor) || Face.Group == Faces[Chosen].Group) continue;
        if (MeasureWinding(Faces[Chosen].Outline, Face.Outline.front()) == 0) continue;
        Holes.push_back(static_cast<int>(Index));
    }
    std::erase_if(Holes, [&](int Candidate)
    {
        return std::any_of(Holes.begin(), Holes.end(), [&](int Other)
        {
            return Other != Candidate && MeasureWinding(Faces[Other].Outline, Faces[Candidate].Outline.front()) != 0;
        });
    });

    // ⑨ The region: exact pieces per loop.
    Region.Area = Faces[Chosen].Area;
    std::vector<int> Loops{ Chosen };
    Loops.insert(Loops.end(), Holes.begin(), Holes.end());
    for (int Loop : Loops)
    {
        std::vector<RegionPiece> Boundary;
        for (int Half : Faces[Loop].Halves)
        {
            const ArrangementPiece& Source = Pieces[Half / 2];
            Boundary.push_back({ Source.Source, Source.From, Source.To, Piece(Half) });
        }
        Region.Loops.push_back(std::move(Boundary));
        Region.Outlines.push_back(Faces[Loop].Outline);
        if (Loop != Chosen) Region.Area += Faces[Loop].Area;                                // Negative: holes subtract
    }
    return Region;
}

SketchModel ComposeRegionSketch(const SketchRegion& Region)
{
    SketchModel Sketch;
    double Scale = 1.0;
    for (const std::vector<Vector2>& Outline : Region.Outlines)
        for (const Vector2& Point : Outline) Scale = std::max({ Scale, std::fabs(Point.X), std::fabs(Point.Y) });
    const double Share = 1.0e-9 * Scale;
    std::vector<int> Junctions;                                                              // Curve end points (centres and inner poles never join)
    const auto Shared = [&](Vector2 Position) -> int
    {
        for (int Index : Junctions) if (Distance(Sketch.Points[Index].Position, Position) <= Share) return Index;
        Sketch.Points.push_back({ Position });
        Junctions.push_back(static_cast<int>(Sketch.Points.size()) - 1);
        return Junctions.back();
    };
    const auto Fresh = [&](Vector2 Position) { Sketch.Points.push_back({ Position }); return static_cast<int>(Sketch.Points.size()) - 1; };

    // Consecutive arcs of one circle (a circle's seam inside the region) become one arc.
    const auto Continues = [&](const ProfileSegment& One, const ProfileSegment& Two)
    {
        const bool Round = (One.Kind == ProfileSegmentCategory::Arc || One.Kind == ProfileSegmentCategory::Circle) && (Two.Kind == ProfileSegmentCategory::Arc || Two.Kind == ProfileSegmentCategory::Circle);
        return Round && Distance(One.Centre, Two.Centre) <= Share && std::fabs(One.Radius - Two.Radius) <= Share && (One.Sweep > 0.0) == (Two.Sweep > 0.0)
            && Distance(One.End, Two.Start) <= Share && std::fabs(One.Sweep + Two.Sweep) < 2.0 * std::numbers::pi;
    };
    std::vector<std::vector<ProfileSegment>> Loops;
    for (const std::vector<RegionPiece>& Loop : Region.Loops)
    {
        std::vector<ProfileSegment> Merged;
        for (const RegionPiece& Piece : Loop)
        {
            if (!Merged.empty() && Continues(Merged.back(), Piece.Segment))
            {
                Merged.back().Kind  = ProfileSegmentCategory::Arc;
                Merged.back().Sweep += Piece.Segment.Sweep;
                Merged.back().End   = Piece.Segment.End;
            }
            else Merged.push_back(Piece.Segment);
        }
        if (Merged.size() > 1 && Continues(Merged.back(), Merged.front()))
        {
            Merged.back().Kind  = ProfileSegmentCategory::Arc;
            Merged.back().Sweep += Merged.front().Sweep;
            Merged.back().End   = Merged.front().End;
            Merged.erase(Merged.begin());
        }
        Loops.push_back(std::move(Merged));
    }

    int Identifier = 1;
    for (const std::vector<ProfileSegment>& Loop : Loops)
        for (const ProfileSegment& Segment : Loop)
        {
            SketchCurve Curve;
            Curve.Identifier = Identifier++;
            switch (Segment.Kind)
            {
            case ProfileSegmentCategory::Line:
                Curve.Kind  = SketchCurveCategory::Line;
                Curve.Start = Shared(Segment.Start);
                Curve.End   = Shared(Segment.End);
                Sketch.Curves.push_back(Curve);
                break;
            case ProfileSegmentCategory::Circle:
                Curve.Kind   = SketchCurveCategory::Circle;
                Curve.Centre = Fresh(Segment.Centre);
                Curve.Radius = Segment.Radius;
                Sketch.Curves.push_back(Curve);
                break;
            case ProfileSegmentCategory::Arc:
                if (std::fabs(Segment.Sweep) >= 2.0 * std::numbers::pi - 1.0e-12)
                {
                    Curve.Kind   = SketchCurveCategory::Circle;
                    Curve.Centre = Fresh(Segment.Centre);
                    Curve.Radius = Segment.Radius;
                }
                else
                {
                    // Sketch arcs run counter-clockwise from Start to End.
                    Curve.Kind   = SketchCurveCategory::Arc;
                    Curve.Start  = Shared(Segment.Sweep > 0.0 ? Segment.Start : Segment.End);
                    Curve.End    = Shared(Segment.Sweep > 0.0 ? Segment.End : Segment.Start);
                    Curve.Centre = Fresh(Segment.Centre);
                }
                Sketch.Curves.push_back(Curve);
                break;
            case ProfileSegmentCategory::Spline:
                for (const std::vector<Vector2>& Span : ExtractSplineBezierSpans(Segment.Spline, Segment.Spline.Knots.front(), Segment.Spline.Knots.back()))
                {
                    SketchCurve Bezier;
                    Bezier.Kind       = SketchCurveCategory::Spline;
                    Bezier.Form       = SketchSplineForm::Bezier;
                    Bezier.Identifier = Curve.Identifier;
                    for (size_t Index = 0; Index < Span.size(); ++Index)
                        Bezier.Points.push_back(Index == 0 || Index + 1 == Span.size() ? Shared(Span[Index]) : Fresh(Span[Index]));
                    Sketch.Curves.push_back(Bezier);
                    Curve.Identifier = Identifier++;
                }
                --Identifier;
                break;
            }
        }
    return Sketch;
}

} // namespace Frontier::GeometrySpace::Parametric
