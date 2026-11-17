/*====================================================================================================================================
                                                           SKETCHFLOW.CPP
====================================================================================================================================*/
// 🧩 Flow along curve: shape axis box, rail chain with an arc-length table, mapped samples refitted as cubic splines;
//    Wrap: the same rail table swept along its normal, front hits bracketed and bisected

#include "Engine/GeometrySpace/Parametric/Sketch/SketchFlow.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchEditing.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int RailSamples  = 48;                                                         // [-] - Table intervals per polynomial piece
    constexpr int CurveSamples = 160;                                                        // [-] - Mapped samples across the shape length
    constexpr int SampleLimit  = 480;                                                        // [-] - Per curve

    // 📝 The rail as an arc-length table: per oriented piece, fractions and the length reached at each.
    struct RailTable
    {
        std::vector<ProfileSegment>      Pieces    = {};                                     // [-]   - Chain order, oriented
        std::vector<std::vector<double>> Fractions = {};                                     // [-]   - Per piece
        std::vector<std::vector<double>> Lengths   = {};                                     // [len] - Cumulative s at each fraction
        double                           Total     = 0.0;                                    // [len] - Rail length
        bool                             Closed    = false;                                  // [-]   - The chain returns to its start
    };

    // Speed |C′| integrated over [From, To] of one piece (5-point Gauss–Legendre).
    [[nodiscard]] double MeasurePiece(const ProfileSegment& Piece, double From, double To)
    {
        static constexpr double Nodes[5]   = { -0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640 };
        static constexpr double Weights[5] = { 0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891 };
        double Sum = 0.0;
        for (int Index = 0; Index < 5; ++Index) Sum += Weights[Index] * Length(EvaluateProfileTangent(Piece, 0.5 * (From + To) + 0.5 * (To - From) * Nodes[Index]));
        return 0.5 * (To - From) * Sum;
    }

    // The rail's non-construction curves chained end to end (one chain), then tabulated by arc length.
    [[nodiscard]] std::string BuildRail(const SketchModel& Rail, bool ReverseCondition, RailTable& Table)
    {
        const double Join = 1.0e-9 * std::max(1.0, MeasureSketchScale(Rail));
        std::vector<ProfileSegment> Free;
        for (int Curve = 0; Curve < static_cast<int>(Rail.Curves.size()); ++Curve)
        {
            if (Rail.Curves[Curve].Construction) continue;
            std::string    Refusal;
            ProfileSegment Segment = ResolveSketchCurveSegment(Rail, Curve, Join, Refusal);
            if (!Refusal.empty()) return "rail: " + Refusal;
            Free.push_back(std::move(Segment));
        }
        if (Free.empty()) return "the rail has no curves";
        const auto Meets = [&](Vector2 First, Vector2 Second) { return Distance(First, Second) <= 1.0e3 * Join; };
        // Start at an end touched by no other piece (an open chain), else at the first piece (a loop).
        size_t Opening = 0;
        bool   Flip    = false;
        bool   Found   = false;
        for (size_t Index = 0; Index < Free.size() && !Found; ++Index)
            for (const bool End : { false, true })
            {
                if (Found) break;
                const Vector2 Point = End ? Free[Index].End : Free[Index].Start;
                bool Shared = false;
                for (size_t Other = 0; Other < Free.size(); ++Other)
                    if (Other != Index && (Meets(Point, Free[Other].Start) || Meets(Point, Free[Other].End))) Shared = true;
                if (Shared) continue;
                Opening = Index;
                Flip    = End;                                                               // A free end: the chain starts there
                Found   = true;
            }
        std::vector<bool> Used(Free.size(), false);
        ProfileSegment First = Free[Opening];
        if (Flip) ReverseProfileSegment(First);
        Used[Opening] = true;
        Table.Pieces.push_back(First);
        for (bool Grew = true; Grew;)
        {
            Grew = false;
            const Vector2 Tip = Table.Pieces.back().End;
            for (size_t Index = 0; Index < Free.size() && !Grew; ++Index)
            {
                if (Used[Index]) continue;
                ProfileSegment Next = Free[Index];
                if (Meets(Tip, Next.End) && !Meets(Tip, Next.Start)) ReverseProfileSegment(Next);
                if (!Meets(Tip, Next.Start)) continue;
                Used[Index] = true;
                Table.Pieces.push_back(Next);
                Grew = true;
            }
        }
        const int Left = static_cast<int>(std::count(Used.begin(), Used.end(), false));
        if (Left > 0) return "the rail is not one chain (" + std::to_string(Left) + " of its curves do not continue it)";
        Table.Closed = Meets(Table.Pieces.front().Start, Table.Pieces.back().End) || Table.Pieces.front().Kind == ProfileSegmentKind::Circle;
        if (ReverseCondition)
        {
            std::reverse(Table.Pieces.begin(), Table.Pieces.end());
            for (ProfileSegment& Piece : Table.Pieces) ReverseProfileSegment(Piece);
        }
        for (const ProfileSegment& Piece : Table.Pieces)
        {
            std::vector<double> Fractions = { 0.0 }, Lengths = { Table.Total };
            const std::vector<double> Breaks = CollectProfileBreaks(Piece);
            for (size_t Span = 0; Span + 1 < Breaks.size(); ++Span)
                for (int Sample = 1; Sample <= RailSamples; ++Sample)
                {
                    const double From = Fractions.back(), To = Breaks[Span] + (Breaks[Span + 1] - Breaks[Span]) * Sample / RailSamples;
                    Table.Total += MeasurePiece(Piece, From, To);
                    Fractions.push_back(To);
                    Lengths.push_back(Table.Total);
                }
            Table.Fractions.push_back(std::move(Fractions));
            Table.Lengths.push_back(std::move(Lengths));
        }
        if (!(Table.Total > Join)) return "the rail has no length";
        return {};
    }

    // Point and unit tangent at arc length s (open rails continue straight past their ends; loops wrap).
    void EvaluateRail(const RailTable& Table, double Along, Vector2& Point, Vector2& Tangent)
    {
        if (Table.Closed) Along = Along - Table.Total * std::floor(Along / Table.Total);
        const auto Direction = [](const ProfileSegment& Piece, double Fraction) { const Vector2 Speed = EvaluateProfileTangent(Piece, Fraction); const double Size = Length(Speed); return Size > 0.0 ? Speed * (1.0 / Size) : Vector2{ 1.0, 0.0 }; };
        if (Along <= 0.0)
        {
            Tangent = Direction(Table.Pieces.front(), 0.0);
            Point   = EvaluateProfileSegment(Table.Pieces.front(), 0.0) + Tangent * Along;
            return;
        }
        if (Along >= Table.Total)
        {
            Tangent = Direction(Table.Pieces.back(), 1.0);
            Point   = EvaluateProfileSegment(Table.Pieces.back(), 1.0) + Tangent * (Along - Table.Total);
            return;
        }
        size_t Piece = 0;
        while (Piece + 1 < Table.Pieces.size() && Table.Lengths[Piece].back() < Along) ++Piece;
        const std::vector<double>& Lengths   = Table.Lengths[Piece];
        const std::vector<double>& Fractions = Table.Fractions[Piece];
        const size_t Upper = std::min<size_t>(Lengths.size() - 1, std::max<size_t>(1, std::lower_bound(Lengths.begin(), Lengths.end(), Along) - Lengths.begin()));
        const double Width = Lengths[Upper] - Lengths[Upper - 1];
        double Fraction = Width > 0.0 ? Fractions[Upper - 1] + (Fractions[Upper] - Fractions[Upper - 1]) * (Along - Lengths[Upper - 1]) / Width : Fractions[Upper];
        // One Newton step on the interval's arc length makes the parameter exact to second order.
        const double Reached = Lengths[Upper - 1] + MeasurePiece(Table.Pieces[Piece], Fractions[Upper - 1], Fraction);
        const double Speed   = Length(EvaluateProfileTangent(Table.Pieces[Piece], Fraction));
        if (Speed > 0.0) Fraction = std::clamp(Fraction + (Along - Reached) / Speed, Fractions[Upper - 1], Fractions[Upper]);
        Point   = EvaluateProfileSegment(Table.Pieces[Piece], Fraction);
        Tangent = Direction(Table.Pieces[Piece], Fraction);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketchFlowOutcome FlowSketchModel(const SketchModel& Shape, const SketchModel& Rail, const SketchFlowSettings& Settings)
{
    SketchFlowOutcome Outcome;
    RailTable Table;
    if (const std::string Refusal = BuildRail(Rail, Settings.ReverseCondition, Table); !Refusal.empty()) { Outcome.Refusal = Refusal; return Outcome; }
    Outcome.RailLength = Table.Total;

    // ① shape curves as exact segments, and the box of dense samples.
    const double Join = 1.0e-9 * std::max(1.0, MeasureSketchScale(Shape));
    std::vector<int>            Kept;
    std::vector<ProfileSegment> Segments;
    for (int Curve = 0; Curve < static_cast<int>(Shape.Curves.size()); ++Curve)
    {
        if (Shape.Curves[Curve].Construction) continue;
        std::string    Refusal;
        ProfileSegment Segment = ResolveSketchCurveSegment(Shape, Curve, Join, Refusal);
        if (!Refusal.empty()) { Outcome.Refusal = "shape: " + Refusal; return Outcome; }
        Kept.push_back(Curve);
        Segments.push_back(std::move(Segment));
    }
    if (Kept.empty()) { Outcome.Refusal = "the shape has no curves (construction curves are left out)"; return Outcome; }
    double Low[2] = { std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() };
    double High[2] = { -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
    for (const ProfileSegment& Segment : Segments)
        for (int Sample = 0; Sample <= 256; ++Sample)
        {
            const Vector2 Point = EvaluateProfileSegment(Segment, Sample / 256.0);
            Low[0] = std::min(Low[0], Point.X); High[0] = std::max(High[0], Point.X);
            Low[1] = std::min(Low[1], Point.Y); High[1] = std::max(High[1], Point.Y);
        }
    Outcome.AlongAxis   = Settings.AlongAxis == 0 || Settings.AlongAxis == 1 ? Settings.AlongAxis : (High[0] - Low[0] >= High[1] - Low[1] ? 0 : 1);
    const int Along     = Outcome.AlongAxis;
    Outcome.ShapeLength = High[Along] - Low[Along];
    if (!(Outcome.ShapeLength > Join)) { Outcome.Refusal = std::string("the shape has no extent along ") + (Along == 0 ? "x" : "y"); return Outcome; }
    const double Middle = 0.5 * (Low[1 - Along] + High[1 - Along]);                           // [len] - Across: the box middle lies on the rail
    const double Factor = Settings.StretchEnabled ? Table.Total / Outcome.ShapeLength : 1.0; // [-]   - s per unit of a
    // (x, y) → (a, b): along x a = x − x₀, b = y − y₀; along y a = y − y₀, b = x₀ − x (a quarter turn, no mirror).
    const auto Map = [&](Vector2 Point)
    {
        const double A = Along == 0 ? Point.X - Low[0] : Point.Y - Low[1];
        const double B = Along == 0 ? Point.Y - Middle : Middle - Point.X;
        Vector2 Base, Tangent;
        EvaluateRail(Table, A * Factor, Base, Tangent);
        return Base + Vector2{ -Tangent.Y, Tangent.X } * B;
    };

    // ② output: shared shape points stay shared; lines across the axis stay lines; the rest are cubic splines.
    SketchModel&       Flowed = Outcome.Sketch;
    std::map<int, int> Shared;                                                               // Shape point → flowed point
    const auto SharedPoint = [&](int Index)
    {
        const auto Found = Shared.find(Index);
        if (Found != Shared.end()) return Found->second;
        return Shared[Index] = AppendSketchPoint(Flowed, Map(Shape.Points[Index].Position));
    };
    int LineNumber = 0, SplineNumber = 0;
    for (size_t Index = 0; Index < Kept.size(); ++Index)
    {
        const SketchCurve&    Curve   = Shape.Curves[Kept[Index]];
        const ProfileSegment& Segment = Segments[Index];
        if (Curve.Kind == SketchCurveKind::Line)
        {
            const Vector2 Offset = Segment.End - Segment.Start;
            if (std::fabs(Along == 0 ? Offset.X : Offset.Y) <= 1.0e-12 * std::max(1.0, Length(Offset)))
            {
                AppendSketchLine(Flowed, SharedPoint(Curve.Start), SharedPoint(Curve.End), ++LineNumber);
                ++Outcome.Lines;
                continue;
            }
        }
        const bool Closed = Segment.Kind == ProfileSegmentKind::Circle || (Curve.Kind == SketchCurveKind::Spline && Curve.Points.size() > 2 && Curve.Points.front() == Curve.Points.back());
        // Samples: dense enough along the axis (CurveSamples over the shape length), at least 12 per polynomial piece;
        //   interior error O(h⁴·κ³) (≈ 10⁻⁹ for a rail of radius 3).
        const double Reach   = MeasurePiece(Segment, 0.0, 1.0);
        const int    Pieces  = static_cast<int>(CollectProfileBreaks(Segment).size()) - 1;
        const int    Samples = std::clamp(std::max(12 * std::max(Pieces, 1), static_cast<int>(std::ceil(CurveSamples * Reach / Outcome.ShapeLength))), 8, SampleLimit);
        int StartPoint = -1, EndPoint = -1;
        if (!Closed)
        {
            StartPoint = SharedPoint(Curve.Kind == SketchCurveKind::Spline ? Curve.Points.front() : Curve.Start);
            EndPoint   = SharedPoint(Curve.Kind == SketchCurveKind::Spline ? Curve.Points.back() : Curve.End);
        }
        std::vector<int>    Points;
        std::vector<double> Parameters;
        double Chord = 0.0;
        for (int Sample = 0; Sample <= Samples; ++Sample)
        {
            int Point;
            if (Sample == 0 && !Closed) Point = StartPoint;
            else if (Sample == Samples) Point = Closed ? Points.front() : EndPoint;
            else
            {
                // Open curves: cosine spacing (dense at the ends, where natural end conditions would cost O(h²·κ)).
                const double Share    = static_cast<double>(Sample) / Samples;
                const double Fraction = Closed ? Share : 0.5 - 0.5 * std::cos(std::numbers::pi * Share);
                Point = AppendSketchPoint(Flowed, Map(EvaluateProfileSegment(Segment, Fraction)));
            }
            if (!Points.empty())
            {
                const double Step = Distance(Flowed.Points[Points.back()].Position, Flowed.Points[Point].Position);
                if (Step <= 1.0e3 * Join) { if (Sample == Samples) { Points.back() = Point; } continue; }   // folded onto the last sample
                Chord += Step;
            }
            Points.push_back(Point);
            Parameters.push_back(Chord);
        }
        if (Points.size() < 2 || (Closed && Points.size() < 4)) { Outcome.Refusal = "the flowed " + SketchCurveName(Curve) + " collapses to a point"; return Outcome; }
        const int Added = AppendSketchSpline(Flowed, Points, SketchSplineForm::CubicSpline, ++SplineNumber);
        Flowed.Curves[Added].Knots = std::move(Parameters);
        ++Outcome.Splines;
    }
    return Outcome;
}

SketchWrapOutcome WrapSketchModels(const std::vector<SketchWrapShape>& Shapes, const SketchModel& Rail, const OrthonormalFrame& RailPlane,
                                   const SketchWrapSettings& Settings)
{
    SketchWrapOutcome Outcome;
    // ① the wrap axis and the rail frame (O, E₁, E₂, n) holding the rail as a 2D chain
    const bool Turned = Length(Settings.Axis) > 0.0 && std::abs(Dot(Normalize(Settings.Axis), RailPlane.ZAxis)) < 1.0 - 1.0e-9;
    const Vector3 Axis = Length(Settings.Axis) > 0.0 ? Normalize(Settings.Axis) : RailPlane.ZAxis;
    OrthonormalFrame Frame = RailPlane;
    SketchModel      Lone;
    const SketchModel* Chain = &Rail;
    {
        int Drawn = 0, Round = -1;
        for (int Curve = 0; Curve < static_cast<int>(Rail.Curves.size()); ++Curve)
        {
            if (Rail.Curves[Curve].Construction) continue;
            ++Drawn;
            if (Rail.Curves[Curve].Kind == SketchCurveKind::Circle || Rail.Curves[Curve].Kind == SketchCurveKind::Arc) Round = Curve;
        }
        if (Drawn == 1 && Round >= 0) Outcome.Radius = MeasureSketchRadius(Rail, Rail.Curves[Round]);
        if (Turned)
        {
            if (Drawn != 1 || Round < 0) { Outcome.Refusal = "only a lone circle or arc can turn onto another wrap axis (this rail wraps about its own plane normal)"; return Outcome; }
            const Vector2 Centre = Rail.Points[Rail.Curves[Round].Centre].Position;
            const Vector3 Seed   = std::abs(Axis.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
            Frame.Origin = RailPlane.Origin + RailPlane.XAxis * Centre.X + RailPlane.YAxis * Centre.Y;
            Frame.XAxis  = Normalize(Seed - Axis * Dot(Seed, Axis));
            Frame.YAxis  = Cross(Axis, Frame.XAxis);
            Frame.ZAxis  = Axis;
            AppendSketchCircle(Lone, AppendSketchPoint(Lone, { 0.0, 0.0 }), Outcome.Radius, 1);
            Chain = &Lone;
        }
    }
    Outcome.Axis = Axis;
    Vector3 Direction = Settings.Direction - Axis * Dot(Settings.Direction, Axis);
    if (!(Length(Direction) > 1.0e-6 * std::max(1.0, Length(Settings.Direction)))) { Outcome.Refusal = "the projection direction runs along the wrap axis: choose another axis"; return Outcome; }
    Direction = Normalize(Direction);
    Outcome.Direction = Direction;
    const Vector3 Across = Cross(Direction, Axis);                                           // [-] - Right, seen along d with n up
    const Vector2 Depth2{ Dot(Direction, Frame.XAxis), Dot(Direction, Frame.YAxis) };
    const Vector2 Across2{ Dot(Across, Frame.XAxis), Dot(Across, Frame.YAxis) };
    RailTable Table;
    if (const std::string Refusal = BuildRail(*Chain, false, Table); !Refusal.empty()) { Outcome.Refusal = Refusal; return Outcome; }
    Outcome.RailLength = Table.Total;

    // ② the rail tabulated for bracketing: s, C(s)
    constexpr int Ticks = 2048;
    std::vector<double>  Stations(Ticks + 1);
    std::vector<Vector2> Places(Ticks + 1);
    for (int Tick = 0; Tick <= Ticks; ++Tick)
    {
        Vector2 Tangent;
        Stations[Tick] = Table.Total * Tick / Ticks;
        EvaluateRail(Table, Stations[Tick], Places[Tick], Tangent);
    }
    // The front hit of the line {x·Across + λ·d}: roots of f(s) = C(s)·Across − x, the one with the least λ = C(s)·d.
    const auto FrontHit = [&](double Lateral) -> std::optional<double>
    {
        std::optional<double> Best;
        double Nearest = std::numeric_limits<double>::infinity();
        const auto Offside = [&](const Vector2& Place) { return Dot(Place, Across2) - Lateral; };
        for (int Tick = 0; Tick < Ticks; ++Tick)
        {
            double Low = Stations[Tick], High = Stations[Tick + 1];
            double FLow = Offside(Places[Tick]), FHigh = Offside(Places[Tick + 1]);
            if (FLow == 0.0) High = Low;
            else if (FHigh == 0.0 && Tick + 1 == Ticks) Low = High;
            else if ((FLow < 0.0) == (FHigh < 0.0)) continue;
            for (int Halving = 0; Halving < 80 && High - Low > 1.0e-15 * std::max(1.0, Table.Total); ++Halving)
            {
                const double Middle = 0.5 * (Low + High);
                Vector2 Place, Tangent;
                EvaluateRail(Table, Middle, Place, Tangent);
                const double FMiddle = Offside(Place);
                if (FMiddle == 0.0) { Low = High = Middle; break; }
                if ((FMiddle < 0.0) == (FLow < 0.0)) { Low = Middle; FLow = FMiddle; } else High = Middle;
            }
            const double Root = 0.5 * (Low + High);
            Vector2 Place, Tangent;
            EvaluateRail(Table, Root, Place, Tangent);
            const double Lambda = Dot(Place, Depth2);
            if (Lambda < Nearest) { Nearest = Lambda; Best = Root; }
        }
        return Best;
    };

    // ③ shape curves (every shape): exact segments, their world points as (across, height)
    struct Piece { int Shape = 0; int Curve = 0; ProfileSegment Segment; };
    std::vector<Piece> Pieces;
    for (int Owner = 0; Owner < static_cast<int>(Shapes.size()); ++Owner)
    {
        const SketchModel& Shape = Shapes[Owner].Sketch;
        const double Join = 1.0e-9 * std::max(1.0, MeasureSketchScale(Shape));
        for (int Curve = 0; Curve < static_cast<int>(Shape.Curves.size()); ++Curve)
        {
            if (Shape.Curves[Curve].Construction) continue;
            std::string    Refusal;
            ProfileSegment Segment = ResolveSketchCurveSegment(Shape, Curve, Join, Refusal);
            if (!Refusal.empty()) { Outcome.Refusal = "shape: " + Refusal; return Outcome; }
            Pieces.push_back({ Owner, Curve, std::move(Segment) });
        }
    }
    if (Pieces.empty()) { Outcome.Refusal = "the shape has no curves (construction curves are left out)"; return Outcome; }
    const auto Lift = [&](int Owner, Vector2 Point)
    {
        const OrthonormalFrame& ShapePlane = Shapes[Owner].Plane;
        const Vector3 World = ShapePlane.Origin + ShapePlane.XAxis * Point.X + ShapePlane.YAxis * Point.Y - Frame.Origin;
        return std::pair<double, double>{ Dot(World, Across), Dot(World, Axis) };
    };
    double Low = std::numeric_limits<double>::infinity(), High = -std::numeric_limits<double>::infinity();
    for (const Piece& Each : Pieces)
        for (int Sample = 0; Sample <= 256; ++Sample)
        {
            const double Lateral = Lift(Each.Shape, EvaluateProfileSegment(Each.Segment, Sample / 256.0)).first;
            Low = std::min(Low, Lateral); High = std::max(High, Lateral);
        }
    const double Join = 1.0e-9 * std::max(1.0, High - Low);
    Outcome.Extent = High - Low;
    const double Centre = 0.5 * (Low + High);
    const std::optional<double> Anchor = FrontHit(Centre);
    if (!Anchor) { Outcome.Refusal = "the shape's centre, seen along the projection direction, misses the rail"; return Outcome; }
    double Sense = 1.0;                                                                      // Across grows along the rail's front face
    {
        Vector2 Place, Tangent;
        EvaluateRail(Table, *Anchor, Place, Tangent);
        if (Dot(Tangent, Across2) < 0.0) Sense = -1.0;
    }
    std::string Outside;
    const auto Place3 = [&](double Along, double Height)
    {
        Vector2 Place, Tangent;
        EvaluateRail(Table, Along, Place, Tangent);
        return Frame.Origin + Frame.XAxis * Place.X + Frame.YAxis * Place.Y + Axis * Height;
    };
    const auto Map = [&](int Owner, Vector2 Point) -> Vector3
    {
        const auto [Lateral, Height] = Lift(Owner, Point);
        if (Settings.Kind == SketchWrapKind::Wrap) return Place3(*Anchor + Sense * (Lateral - Centre), Height);
        const std::optional<double> Hit = FrontHit(Lateral);
        if (!Hit) { Outside = "part of the shape lies outside the rail seen along the projection direction (Wrap keeps lengths and goes round)"; return {}; }
        return Place3(*Hit, Height);
    };

    // ④ dense samples per curve (≥ 12 per polynomial piece, 160 across the shape's width)
    for (const Piece& Each : Pieces)
    {
        const SketchCurve&    Curve   = Shapes[Each.Shape].Sketch.Curves[Each.Curve];
        const ProfileSegment& Segment = Each.Segment;
        const bool Closed  = Segment.Kind == ProfileSegmentKind::Circle || (Curve.Kind == SketchCurveKind::Spline && Curve.Points.size() > 2 && Curve.Points.front() == Curve.Points.back());
        const double Reach = MeasurePiece(Segment, 0.0, 1.0);
        const int Pieces   = static_cast<int>(CollectProfileBreaks(Segment).size()) - 1;
        const int Samples  = std::clamp(std::max(12 * std::max(Pieces, 1), static_cast<int>(std::ceil(CurveSamples * Reach / std::max(Outcome.Extent, Join)))), 16, SampleLimit);
        std::vector<Vector3> Line;
        for (int Sample = 0; Sample <= Samples; ++Sample)
        {
            if (Closed && Sample == Samples) break;                                          // A loop does not repeat its first sample
            Line.push_back(Map(Each.Shape, EvaluateProfileSegment(Segment, static_cast<double>(Sample) / Samples)));
            if (!Outside.empty()) { Outcome.Refusal = Outside; Outcome.Curves.clear(); return Outcome; }
        }
        Outcome.Curves.push_back(std::move(Line));
        Outcome.Closed.push_back(Closed);
        Outcome.Names.push_back(SketchCurveName(Curve));
        Outcome.Owners.push_back(Each.Shape);
    }
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
