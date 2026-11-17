/*====================================================================================================================================
                                                        SKETCHSECTION.CPP
====================================================================================================================================*/
// 🧩 A solved sketch as one exact NURBS section curve (lines, polylines, arcs, slots, circles, splines) for Loft

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSection.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/SurfaceAlgebra.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr double JoinRatio      = 1.0e-9;                                                    // [-] - Junction gap / sketch scale
constexpr int    LengthSamples  = 48;                                                        // [-] - Chords per piece for its span
constexpr int    CrossSamples   = 64;                                                        // [-] - Samples per piece for the start ray
constexpr int    BisectionSteps = 80;                                                        // [-] - Halvings of a ray crossing

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3 Lift(const OrthonormalFrame& Plane, Vector2 Point)
{
    return Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y;
}

[[nodiscard]] double MeasureSegmentLength(const ProfileSegment& Segment)
{
    double  Total    = 0.0;
    Vector2 Previous = EvaluateProfileSegment(Segment, 0.0);
    for (int Index = 1; Index <= LengthSamples; ++Index)
    {
        const Vector2 Next = EvaluateProfileSegment(Segment, double(Index) / LengthSamples);
        Total   += std::hypot(Next.X - Previous.X, Next.Y - Previous.Y);
        Previous = Next;
    }
    return Total;
}

// One segment, in its traversal direction, as an exact NURBS on the plane.
[[nodiscard]] std::optional<NurbsCurveDefinition> ConvertSegment(const ProfileSegment& Segment, const OrthonormalFrame& Plane)
{
    switch (Segment.Kind)
    {
    case ProfileSegmentKind::Line:
    {
        const Vector3 From = Lift(Plane, Segment.Start), To = Lift(Plane, Segment.End);
        const double  Span = Length(To - From);
        if (!(Span > 0.0)) return std::nullopt;
        return *ConvertLineToNurbs(StraightLine{ From, (To - From) * (1.0 / Span) }, 0.0, Span).Definition;
    }
    case ProfileSegmentKind::Arc:
    case ProfileSegmentKind::Circle:
    {
        const double Sweep = Segment.Sweep != 0.0 ? Segment.Sweep : 2.0 * std::numbers::pi;   // [rad] - Circles: a full turn
        if (!(Segment.Radius > 0.0)) return std::nullopt;
        const ConicForm Conic{ Lift(Plane, Segment.Centre), Plane.XAxis * Segment.Radius, Plane.YAxis * Segment.Radius, Plane.ZAxis };
        if (Sweep > 0.0) return *ConvertConicToNurbs(Conic, Segment.StartAngle, Segment.StartAngle + Sweep).Definition;
        const NurbsCurve Forward = ConvertConicToNurbs(Conic, Segment.StartAngle + Sweep, Segment.StartAngle);
        return *ReverseNurbsCurve(*Forward.Definition).Definition;
    }
    case ProfileSegmentKind::Spline:
    {
        std::vector<Vector3> Poles;
        for (const Vector2& Pole : Segment.Spline.Poles) Poles.push_back(Lift(Plane, Pole));
        const std::optional<NurbsCurve> Curve = ConstructNurbsCurve(Segment.Spline.Degree, Segment.Spline.Knots, Poles, std::vector<double>(Poles.size(), 1.0));
        if (!Curve) return std::nullopt;
        return *Curve->Definition;
    }
    }
    return std::nullopt;
}

// Open run: every curve once, end to start; refused when the curves branch or fall apart.
[[nodiscard]] std::string ChainOpenSegments(std::vector<ProfileSegment>& Segments, double Join)
{
    const auto Near = [&](Vector2 First, Vector2 Second) { return std::hypot(First.X - Second.X, First.Y - Second.Y) <= Join; };
    const size_t Count = Segments.size();
    // ① a free end: an endpoint meeting no other curve's endpoint
    auto Meetings = [&](size_t Owner, Vector2 Point)
    {
        int Total = 0;
        for (size_t Other = 0; Other < Count; ++Other)
            if (Other != Owner) Total += int(Near(Point, Segments[Other].Start)) + int(Near(Point, Segments[Other].End));
        return Total;
    };
    size_t First = Count;
    for (size_t Index = 0; Index < Count && First == Count; ++Index)
    {
        if (Meetings(Index, Segments[Index].Start) == 0) First = Index;
        else if (Meetings(Index, Segments[Index].End) == 0) { ReverseProfileSegment(Segments[Index]); First = Index; }
    }
    if (First == Count) return "the curves form no single open run (a loop with a branch, or curves meeting three at a point)";
    // ② walk end → start
    std::vector<ProfileSegment> Chain{ Segments[First] };
    std::vector<bool>           Used(Count, false);
    Used[First] = true;
    while (Chain.size() < Count)
    {
        const Vector2 Tail = Chain.back().End;
        size_t Found = Count;
        int    Candidates = 0;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            if (Used[Index]) continue;
            if (Near(Tail, Segments[Index].Start) || Near(Tail, Segments[Index].End)) { Found = Index; ++Candidates; }
        }
        if (Candidates == 0) return std::to_string(Count - Chain.size()) + " curve(s) are not connected to the run (one chain per section)";
        if (Candidates > 1) return "the curves branch at " + Chain.back().Name + "'s end (one chain per section)";
        if (!Near(Tail, Segments[Found].Start)) ReverseProfileSegment(Segments[Found]);
        Used[Found] = true;
        Chain.push_back(Segments[Found]);
    }
    Segments = std::move(Chain);
    return {};
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketchSectionOutcome ComposeSketchSection(const SketchModel& Sketch, const OrthonormalFrame& Plane)
{
    SketchSectionOutcome Outcome;
    // ① the drawn curves as segments
    double Scale = 1.0;
    for (const SketchPoint& Point : Sketch.Points) Scale = std::max({ Scale, std::fabs(Point.Position.X), std::fabs(Point.Position.Y) });
    const double Join = JoinRatio * Scale;
    std::vector<ProfileSegment> Segments;
    for (size_t Index = 0; Index < Sketch.Curves.size(); ++Index)
    {
        if (Sketch.Curves[Index].Construction) continue;
        std::string Problem;
        ProfileSegment Segment = ResolveSketchCurveSegment(Sketch, static_cast<int>(Index), Join, Problem);
        if (!Problem.empty()) { Outcome.Refusal = Problem; return Outcome; }
        Segments.push_back(std::move(Segment));
    }
    if (Segments.empty()) { Outcome.Refusal = "the sketch has no drawn curves"; return Outcome; }

    // ② closed: the profile loop (counter-clockwise outer); otherwise one open run
    Vector2 Centroid{};
    const SketchProfile Profile = ExtractSketchProfile(Sketch);
    if (Profile.Refusal.empty() && Profile.Loops.size() > 1)
    {
        Outcome.Refusal = "the sketch holds " + std::to_string(Profile.Loops.size()) + " loops (one chain per section)";
        return Outcome;
    }
    if (Profile.Refusal.empty() && Profile.Loops.size() == 1)
    {
        Segments                = Profile.Loops.front().Segments;
        Centroid                = Profile.Centroid;
        Outcome.ClosedCondition = true;
    }
    else if (std::string Problem = ChainOpenSegments(Segments, Join); !Problem.empty())
    {
        Outcome.Refusal = Problem + (Profile.Refusal.empty() ? std::string() : " · as a loop: " + Profile.Refusal);
        return Outcome;
    }

    // ③ exact pieces and their spans
    std::vector<NurbsCurveDefinition> Pieces;
    std::vector<double>               Spans;
    for (const ProfileSegment& Segment : Segments)
    {
        std::optional<NurbsCurveDefinition> Piece = ConvertSegment(Segment, Plane);
        const double                        Span  = MeasureSegmentLength(Segment);
        if (!Piece || !(Span > 0.0)) { Outcome.Refusal = Segment.Name + " is degenerate"; return Outcome; }
        Pieces.push_back(std::move(*Piece));
        Spans.push_back(Span);
    }

    // ④ closed: start on the ray from the centroid along +x (split there), so similar sections align
    if (Outcome.ClosedCondition)
    {
        size_t Best = Segments.size();
        double BestFraction = 0.0, BestReach = 1e300;
        for (size_t Index = 0; Index < Segments.size(); ++Index)
        {
            const auto Height = [&](double Fraction) { return EvaluateProfileSegment(Segments[Index], Fraction).Y - Centroid.Y; };
            for (int Sample = 0; Sample < CrossSamples; ++Sample)
            {
                double Low = double(Sample) / CrossSamples, High = double(Sample + 1) / CrossSamples;
                if (!(Height(Low) <= 0.0 && Height(High) > 0.0)) continue;                    // Counter-clockwise: upward crossing
                for (int Step = 0; Step < BisectionSteps; ++Step)
                {
                    const double Middle = 0.5 * (Low + High);
                    (Height(Middle) <= 0.0 ? Low : High) = Middle;
                }
                const double Reach = EvaluateProfileSegment(Segments[Index], Low).X - Centroid.X;
                if (Reach > 0.0 && Reach < BestReach) { BestReach = Reach; Best = Index; BestFraction = Low; }
            }
        }
        if (Best < Segments.size())
        {
            const NurbsDomain Domain = MeasureNurbsCurveDomain(Pieces[Best]);
            const Vector3     Target = Lift(Plane, EvaluateProfileSegment(Segments[Best], BestFraction));
            const double      τ      = ProjectOntoNurbsCurve(Pieces[Best], Target, Domain.Low);
            const double      Margin = 1.0e-9 * (Domain.High - Domain.Low);
            std::vector<NurbsCurveDefinition> Turned;
            std::vector<double>               TurnedSpans;
            size_t First = Best;                                                             // [idx] - Piece the loop starts with
            if (τ > Domain.Low + Margin && τ < Domain.High - Margin && Length(EvaluateNurbsCurve(Pieces[Best], τ).Position - Target) <= 1.0e-7 * Scale)
            {
                const auto Halves = SplitNurbsCurve(Pieces[Best], τ);
                if (Halves)
                {
                    const double Share = BestFraction;
                    Turned.push_back(Halves->second);
                    TurnedSpans.push_back(Spans[Best] * (1.0 - Share));
                    for (size_t Offset = 1; Offset < Pieces.size(); ++Offset)
                    {
                        Turned.push_back(Pieces[(Best + Offset) % Pieces.size()]);
                        TurnedSpans.push_back(Spans[(Best + Offset) % Pieces.size()]);
                    }
                    Turned.push_back(Halves->first);
                    TurnedSpans.push_back(Spans[Best] * Share);
                }
            }
            else if (τ >= Domain.High - Margin) First = (Best + 1) % Pieces.size();
            if (Turned.empty())
            {
                for (size_t Offset = 0; Offset < Pieces.size(); ++Offset)
                {
                    Turned.push_back(Pieces[(First + Offset) % Pieces.size()]);
                    TurnedSpans.push_back(Spans[(First + Offset) % Pieces.size()]);
                }
            }
            Pieces = std::move(Turned);
            Spans  = std::move(TurnedSpans);
        }
    }

    // ⑤ one curve; a closed loop ends exactly on its first pole
    std::optional<NurbsCurveDefinition> Joined = JoinNurbsCurves(Pieces, Spans, 1.0e-7 * Scale);
    if (!Joined) { Outcome.Refusal = "the pieces could not be joined into one curve"; return Outcome; }
    if (Outcome.ClosedCondition)
    {
        if (Length(Joined->Poles.back() - Joined->Poles.front()) > 1.0e-7 * Scale) { Outcome.Refusal = "the loop does not close"; return Outcome; }
        Joined->Poles.back() = Joined->Poles.front();
    }
    Outcome.Curve  = ConstructNurbsCurve(Joined->Degree, Joined->Knots, Joined->Poles, Joined->Weights);
    Outcome.Pieces = static_cast<int>(Segments.size());
    if (!Outcome.Curve) Outcome.Refusal = "the joined curve is invalid";
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
