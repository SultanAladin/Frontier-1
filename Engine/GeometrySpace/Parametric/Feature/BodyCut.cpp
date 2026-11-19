/*====================================================================================================================================
                                                          BODYCUT.CPP
====================================================================================================================================*/
// 🧩 Cut (Plasticity C): a solid or sheet split by a sketch profile or an open curve swept through it, into two bodies

#include "Engine/GeometrySpace/Parametric/Feature/BodyCut.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] Vector2 UnitTangent(const ProfileSegment& Segment, double Fraction) noexcept
    {
        const Vector2 Tangent = EvaluateProfileTangent(Segment, Fraction);
        const double  Size    = std::hypot(Tangent.X, Tangent.Y);
        return Size > 0.0 ? Vector2{ Tangent.X / Size, Tangent.Y / Size } : Vector2{ 1.0, 0.0 };
    }

    [[nodiscard]] ProfileSegment StraightSegment(Vector2 Start, Vector2 End, const std::string& Name)
    {
        ProfileSegment Segment;
        Segment.Kind  = ProfileSegmentCategory::Line;
        Segment.Start = Start;
        Segment.End   = End;
        Segment.Name  = Name;
        return Segment;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

double MeasureCutReach(const ModelPartition& Partition, BodyToken Body, const OrthonormalBasis& Plane)
{
    TessellationSettings Settings;
    Settings.SagRatio        = 1.0e-2;
    Settings.EdgeLengthRatio = 0.5;
    const TessellationOutcome Tessellation = TessellateBody(Partition, Body, Settings);
    double Reach = 0.0;
    for (const Vector3& Position : Tessellation.Complex.Positions) Reach = std::max(Reach, Length(Position - Plane.Origin));
    // Curved faces bulge past their chords by at most the sag; pad generously (the tool only has to overshoot).
    return 1.1 * Reach + 2.0 * Tessellation.Sag + 0.05 * Tessellation.Diagonal + 1.0e-6;
}

SketchProfile CloseCutChain(std::vector<ProfileSegment> Chain, double Reach)
{
    SketchProfile Profile;
    if (Chain.empty()) { Profile.Refusal = "Cut: the cutting curve is empty"; return Profile; }
    const Vector2 Start = Chain.front().Start, End = Chain.back().End;
    const Vector2 StartTangent = UnitTangent(Chain.front(), 0.0), EndTangent = UnitTangent(Chain.back(), 1.0);
    // ① Tangent extensions end at least 2·Reach from the plane origin (the body lies within Reach of it).
    const double  StartExtension = 2.0 * Reach + Length(Start), EndExtension = 2.0 * Reach + Length(End);
    const Vector2 StartFar = Start - StartTangent * StartExtension;
    const Vector2 EndFar   = End + EndTangent * EndExtension;
    // ② Far box on the left of the chord between the extended ends, deeper than the body.
    const Vector2 Chord = EndFar - StartFar;
    const double  ChordSize = Length(Chord);
    if (!(ChordSize > 0.0)) { Profile.Refusal = "Cut: the cutting curve closes on itself without a profile"; return Profile; }
    // 💡 The box first runs outward along the chord, so its sides never fold back onto the extensions (a U-shaped
    //    chain has its chord ⟂ to both legs).
    const Vector2 Along = Chord * (1.0 / ChordSize);
    const Vector2 Left  = Vector2{ -Along.Y, Along.X };
    const double  Depth = 4.0 * Reach + Length(StartFar) + Length(EndFar);
    const Vector2 EndOut = EndFar + Along * Depth, StartOut = StartFar - Along * Depth;
    const Vector2 EndCorner = EndOut + Left * Depth, StartCorner = StartOut + Left * Depth;

    ProfileLoop Loop;
    Loop.Segments.push_back(StraightSegment(StartFar, Start, "ExtensionStart"));
    for (ProfileSegment& Segment : Chain) Loop.Segments.push_back(std::move(Segment));
    Loop.Segments.push_back(StraightSegment(End, EndFar, "ExtensionEnd"));
    Loop.Segments.push_back(StraightSegment(EndFar, EndOut, "ReachEndOut"));
    Loop.Segments.push_back(StraightSegment(EndOut, EndCorner, "ReachEnd"));
    Loop.Segments.push_back(StraightSegment(EndCorner, StartCorner, "ReachFar"));
    Loop.Segments.push_back(StraightSegment(StartCorner, StartOut, "ReachStart"));
    Loop.Segments.push_back(StraightSegment(StartOut, StartFar, "ReachStartOut"));
    Profile.Loops.push_back(std::move(Loop));
    Profile.Scale = 4.0 * Depth;
    ArrangeProfileLoops(Profile);
    if (!Profile.Refusal.empty()) Profile.Refusal = "Cut: the curve cannot be closed on one side (" + Profile.Refusal + ")";
    return Profile;
}

SketchProfile BuildSketchCutProfile(const SketchModel& Sketch, double Reach)
{
    // ① A closed profile cuts out its own interior.
    SketchProfile Closed = ExtractSketchProfile(Sketch);
    if (Closed.Refusal.empty()) return Closed;

    // ② Otherwise exactly one open chain: segments joined end to end, chained from the start of the first curve.
    std::vector<ProfileSegment> Segments;
    double Scale = 0.0;
    for (const SketchPoint& Point : Sketch.Points) Scale = std::max(Scale, Length(Point.Position));
    const double Join = 1.0e-9 * std::max(Scale, 1.0e-300);
    for (int Index = 0; Index < static_cast<int>(Sketch.Curves.size()); ++Index)
    {
        if (Sketch.Curves[Index].Construction) continue;
        std::string Refusal;
        ProfileSegment Segment = ResolveSketchCurveSegment(Sketch, Index, Join, Refusal);
        if (!Refusal.empty()) { SketchProfile Profile; Profile.Refusal = "Cut: " + Refusal; return Profile; }
        if (Segment.Kind == ProfileSegmentCategory::Circle) { SketchProfile Profile; Profile.Refusal = "Cut: " + Closed.Refusal; return Profile; }
        Segments.push_back(std::move(Segment));
    }
    if (Segments.empty()) { SketchProfile Profile; Profile.Refusal = "Cut: the sketch has no cutting curves"; return Profile; }
    std::vector<ProfileSegment> Chain{ Segments.front() };
    std::vector<bool> Used(Segments.size(), false);
    Used[0] = true;
    for (bool Grown = true; Grown;)
    {
        Grown = false;
        for (size_t Index = 0; Index < Segments.size(); ++Index)
        {
            if (Used[Index]) continue;
            ProfileSegment Segment = Segments[Index];
            if (Distance(Segment.End, Chain.back().End) <= Join) ReverseProfileSegment(Segment);
            if (Distance(Segment.Start, Chain.back().End) <= Join) { Chain.push_back(Segment); Used[Index] = Grown = true; continue; }
            if (Distance(Segment.Start, Chain.front().Start) <= Join) ReverseProfileSegment(Segment);
            if (Distance(Segment.End, Chain.front().Start) <= Join) { Chain.insert(Chain.begin(), Segment); Used[Index] = Grown = true; }
        }
    }
    if (std::find(Used.begin(), Used.end(), false) != Used.end())
    {
        SketchProfile Profile;
        Profile.Refusal = "Cut: the sketch curves do not form one chain (" + Closed.Refusal + ")";
        return Profile;
    }
    return CloseCutChain(std::move(Chain), Reach);
}

std::optional<ProfileSegment> ProjectCutCurve(const NurbsCurve& Curve, const OrthonormalBasis& Plane, const std::string& Name)
{
    if (!Curve.Definition) return std::nullopt;
    const NurbsCurveDefinition& Definition = *Curve.Definition;
    if (Definition.Period != 0.0) return std::nullopt;
    for (double Weight : Definition.Weights) if (std::fabs(Weight - 1.0) > 1.0e-12) return std::nullopt;
    ProfileSegment Segment;
    Segment.Kind          = ProfileSegmentCategory::Spline;
    Segment.Name          = Name;
    Segment.Spline.Degree = Definition.Degree;
    Segment.Spline.Knots  = Definition.Knots;
    for (const Vector3& Pole : Definition.Poles)
    {
        const Vector3 Offset = Pole - Plane.Origin;
        Segment.Spline.Poles.push_back({ Dot(Offset, Plane.XAxis), Dot(Offset, Plane.YAxis) });
    }
    Segment.Start = Segment.Spline.Poles.front();
    Segment.End   = Segment.Spline.Poles.back();
    return Segment;
}

static BodyCutOutcome CutBodyUntraced(ModelPartition& Partition, BodyToken Body, const SketchProfile& Profile, const OrthonormalBasis& Plane, double Reach)
{
    BodyCutOutcome Outcome;
    Outcome.Profile        = Profile;
    Outcome.SheetCondition = Partition.Bodies.Resolve(Body).Category == BodyCategory::Sheet;
    Outcome.Reach          = Reach > 0.0 ? Reach : MeasureCutReach(Partition, Body, Plane);
    if (!Profile.Refusal.empty()) { Outcome.Refusal = Profile.Refusal.rfind("Cut:", 0) == 0 ? Profile.Refusal : "Cut: " + Profile.Refusal; return Outcome; }

    // ① Tool: the profile swept from −2·Reach to +2·Reach along the plane normal.
    OrthonormalBasis Floor = Plane;
    Floor.Origin = Plane.Origin - Plane.ZAxis * (2.0 * Outcome.Reach);
    Outcome.Tool = ExtrudeProfile(Partition, Profile, Floor, 4.0 * Outcome.Reach);
    if (!Outcome.Tool.Refusal.empty()) { Outcome.Refusal = "Cut: " + Outcome.Tool.Refusal; return Outcome; }

    // ② Pieces: solids take the tool caps (closed halves); sheets keep only their own fragments.
    GeneralFuseSettings Settings;
    BooleanOperation Within = BooleanOperation::Intersection, Without = BooleanOperation::Difference;
    if (Outcome.SheetCondition)
    {
        Settings.SecondClassificationEnabled = false;
        Within  = BooleanOperation::FirstInside;
        Without = BooleanOperation::FirstOutside;
    }
    Outcome.Inside = ComputeBoolean(Partition, Body, Outcome.Tool.Body, Within, Settings);
    if (!Outcome.Inside.Refusal.empty())
    {
        Outcome.Refusal = Outcome.Inside.Refusal == "Boolean: empty result" ? "Cut: the cut misses the body (nothing on the inside)" : "Cut: " + Outcome.Inside.Refusal;
        return Outcome;
    }
    Outcome.Outside = ComputeBoolean(Partition, Body, Outcome.Tool.Body, Without, Settings);
    if (!Outcome.Outside.Refusal.empty())
    {
        Outcome.Refusal = Outcome.Outside.Refusal == "Boolean: empty result" ? "Cut: the cut misses the body (nothing on the outside)" : "Cut: " + Outcome.Outside.Refusal;
        return Outcome;
    }
    return Outcome;
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

BodyCutOutcome CutBody(ModelPartition& Partition, BodyToken Body, const SketchProfile& Profile, const OrthonormalBasis& Plane, double Reach)
{
    FRONTIER_TRACE_SCOPE("Cut", "CutBody · body " + std::to_string(Body.Slot) + " · " + std::to_string(Profile.Loops.size()) + " loop(s)");
    BodyCutOutcome Outcome = CutBodyUntraced(Partition, Body, Profile, Plane, Reach);
    FRONTIER_TRACE("Cut", Outcome.Refusal.empty() ? std::string("built") : "refused: " + Outcome.Refusal);
    return Outcome;
}


} // namespace Frontier::GeometrySpace::Parametric
