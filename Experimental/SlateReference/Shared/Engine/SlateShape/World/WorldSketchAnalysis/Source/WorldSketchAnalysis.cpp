//============================================================================================================================================
//                                                    WORLDSKETCHANALYSIS.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    bool SamePoint(const SpatialPoint& Left,
                   const SpatialPoint& Right,
                   double Tolerance)
    {
        return LengthSquared(Difference(Left, Right)) <= Tolerance * Tolerance;
    }

    bool ResolveCurvePolyline(const WorldSketchStructure& Declared,
                              WorldCurveName Subject,
                              std::uint32_t StepFloor,
                              std::vector<SpatialPoint>& Delivered)
    {
        Delivered.clear();
        const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
        if (Held == nullptr || !Held->Geometry.Declared())
            return false;

        AppendCurvePolyline(Held->Geometry, Delivered,
                            ResolveCurveStepCount(Held->Geometry, std::max(StepFloor, 2u)));
        return Delivered.size() >= 2u;
    }

    bool ResolveLoopOutline(const WorldSketchStructure& Declared,
                            WorldLoopName Subject,
                            std::uint32_t StepFloor,
                            double ClosureTolerance,
                            std::vector<SpatialPoint>& Outline,
                            bool& Closed,
                            std::vector<WorldLoopIssue>& Issues)
    {
        Outline.clear();
        Closed = false;

        const DeclaredWorldLoop* Held = Declared.Resolve(Subject);
        if (Held == nullptr || Held->Traversal.empty())
            return false;

        SpatialPoint FirstStart = {};
        SpatialPoint PreviousEnd = {};
        WorldCurveName PreviousCurve = {};
        bool FirstCurve = true;
        bool Connected = true;

        std::vector<SpatialPoint> Polyline;
        for (const WorldCurveUse& Use : Held->Traversal)
        {
            if (!ResolveCurvePolyline(Declared, Use.TraversedCurve, StepFloor, Polyline))
            {
                Issues.push_back({ Subject, WorldLoopIssueSubject::MissingCurve,
                                   Use.TraversedCurve, {}, {}, {}, 0.0 });
                Outline.clear();
                Closed = false;
                return false;
            }

            if (!Use.SameSense)
                std::reverse(Polyline.begin(), Polyline.end());

            if (FirstCurve)
            {
                FirstStart = Polyline.front();
                Outline = Polyline;
                FirstCurve = false;
            }
            else
            {
                const double GapDistance = std::sqrt(LengthSquared(Difference(PreviousEnd, Polyline.front())));
                if (!SamePoint(PreviousEnd, Polyline.front(), ClosureTolerance))
                {
                    Connected = false;
                    Issues.push_back({ Subject, WorldLoopIssueSubject::Gap,
                                       PreviousCurve, Use.TraversedCurve,
                                       PreviousEnd, Polyline.front(), GapDistance });
                }

                if (!Outline.empty() && SamePoint(Outline.back(), Polyline.front(), ClosureTolerance))
                    Outline.insert(Outline.end(), Polyline.begin() + 1u, Polyline.end());
                else
                    Outline.insert(Outline.end(), Polyline.begin(), Polyline.end());
            }

            PreviousEnd = Polyline.back();
            PreviousCurve = Use.TraversedCurve;
        }

        if (FirstCurve)
            return false;

        if (!Outline.empty() && SamePoint(Outline.front(), Outline.back(), ClosureTolerance))
            Outline.pop_back();

        Closed = Connected && SamePoint(PreviousEnd, FirstStart, ClosureTolerance);
        if (!Closed)
        {
            Issues.push_back({ Subject, WorldLoopIssueSubject::OpenLoop,
                               PreviousCurve, Held->Traversal.front().TraversedCurve,
                               PreviousEnd, FirstStart,
                               std::sqrt(LengthSquared(Difference(PreviousEnd, FirstStart))) });
        }

        return !Outline.empty();
    }

    bool ResolveSupportFrameFromOutline(const std::vector<SpatialPoint>& Outline,
                                        WorldPlacementFrame& Delivered,
                                        double& MaximumDeviation)
    {
        Delivered = {};
        MaximumDeviation = 0.0;
        if (Outline.size() < 3u)
            return false;

        for (std::size_t OriginIndex = 0u; OriginIndex + 2u < Outline.size(); ++OriginIndex)
        {
            for (std::size_t AlongIndex = OriginIndex + 1u; AlongIndex + 1u < Outline.size(); ++AlongIndex)
            {
                const SpatialDirection Along = Difference(Outline[OriginIndex], Outline[AlongIndex]);
                if (LengthSquared(Along) <= 1.0e-18)
                    continue;

                for (std::size_t ThirdIndex = AlongIndex + 1u; ThirdIndex < Outline.size(); ++ThirdIndex)
                {
                    const SpatialDirection Across = Difference(Outline[OriginIndex], Outline[ThirdIndex]);
                    const SpatialDirection RawNormal = Cross(Along, Across);
                    if (LengthSquared(RawNormal) <= 1.0e-18)
                        continue;

                    const SpatialDirection Normal = Normalize(RawNormal);
                    SpatialDirection AlongDirection = Added(Along, Scaled(Normal, -Dot(Along, Normal)));
                    if (LengthSquared(AlongDirection) <= 1.0e-18)
                        continue;

                    Delivered.Origin = Outline[OriginIndex];
                    Delivered.Normal = Normal;
                    Delivered.AlongDirection = Normalize(AlongDirection);

                    for (const SpatialPoint& Point : Outline)
                        MaximumDeviation = std::max(MaximumDeviation,
                                                    std::fabs(Dot(Normal, Difference(Delivered.Origin, Point))));
                    return Delivered.Declared();
                }
            }
        }

        return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              WHICH LOOPS ARE HOLES IN WHICH
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether a point lies inside a closed outline, both flattened into the same frame.
/// 📐 Ray casting: count the boundary crossings of a ray from the point. Odd means inside. It is exact
///    for any simple polygon, convex or not, which a winding-number shortcut assuming convexity is not.
bool OutlineEncloses(const WorldPlacementFrame& Frame,
                     const std::vector<SpatialPoint>& Outline,
                     const SpatialPoint& Probe)
{
    if (Outline.size() < 3u)
        return false;

    double ProbeAlong = 0.0;
    double ProbeAcross = 0.0;
    ResolveWorldPlacementCoordinates(Frame, Probe, ProbeAlong, ProbeAcross);

    bool Inside = false;
    for (std::size_t Index = 0u, Previous = Outline.size() - 1u; Index < Outline.size(); Previous = Index++)
    {
        double AlongA = 0.0, AcrossA = 0.0, AlongB = 0.0, AcrossB = 0.0;
        ResolveWorldPlacementCoordinates(Frame, Outline[Index],    AlongA, AcrossA);
        ResolveWorldPlacementCoordinates(Frame, Outline[Previous], AlongB, AcrossB);

        const bool Straddles = (AcrossA > ProbeAcross) != (AcrossB > ProbeAcross);
        if (!Straddles)
            continue;
        const double Span = AcrossB - AcrossA;
        if (std::fabs(Span) <= 1.0e-18)
            continue;
        const double CrossingAlong = AlongA + (ProbeAcross - AcrossA) * (AlongB - AlongA) / Span;
        if (ProbeAlong < CrossingAlong)
            Inside = !Inside;
    }
    return Inside;
}

/// 🧩 How much ground an outline covers, flattened into its own frame.
/// 📝 The shoelace formula, made unsigned. Only the magnitude matters here -- this is used to rank
///    enclosers by size, and which way an outline was drawn should not change how big it is.
double OutlineArea(const WorldPlacementFrame& Frame, const std::vector<SpatialPoint>& Outline)
{
    if (Outline.size() < 3u)
        return 0.0;

    double Sum = 0.0;
    for (std::size_t Index = 0u; Index < Outline.size(); ++Index)
    {
        const std::size_t Next = (Index + 1u) % Outline.size();
        double AlongA = 0.0, AcrossA = 0.0, AlongB = 0.0, AcrossB = 0.0;
        ResolveWorldPlacementCoordinates(Frame, Outline[Index], AlongA, AcrossA);
        ResolveWorldPlacementCoordinates(Frame, Outline[Next],  AlongB, AcrossB);
        Sum += AlongA * AcrossB - AlongB * AcrossA;
    }
    return std::fabs(Sum) * 0.5;
}

/// 🧩 Counts how many other loops enclose each loop, and withdraws the fill from the odd ones.
/// 🔴 THIS IS WHAT MAKES A CIRCLE INSIDE A CIRCLE A TUBE. Both loops are closed and both are planar, so
///    judged on their own merits both fill -- and the inner disc is painted straight over the hole it is
///    meant to be. Depth settles it: even is material, odd is a hole. An island inside a hole is depth
///    two and fills again, which the same rule gives for free.
/// 📝 Only loops sharing a plane are compared. Two circles on perpendicular walls do not nest, however
///    they happen to line up when projected.
void ResolveLoopNesting(WorldSketchAnalysis& Analysis)
{
    for (WorldLoopAnalysisRecord& Subject : Analysis.Loops)
    {
        if (!Subject.Closed || !Subject.SupportFrame.Declared() || Subject.Outline.empty())
            continue;

        std::uint32_t Depth = 0u;
        double SmallestEncloser = 0.0;
        WorldLoopName Container = {};
        bool ContainerFound = false;

        for (const WorldLoopAnalysisRecord& Other : Analysis.Loops)
        {
            if (Other.Loop.IssuedIndex == Subject.Loop.IssuedIndex)
                continue;
            if (!Other.Closed || !Other.SupportFrame.Declared() || Other.Outline.size() < 3u)
                continue;

            // ⚠️ Same plane only. Loops on different planes cannot enclose one another, and comparing
            //    them would nest a wall's circle inside the floor's.
            const double Separation =
                std::fabs(Dot(Difference(Other.SupportFrame.Origin, Subject.SupportFrame.Origin),
                              Other.SupportFrame.Normal));
            const double Alignment = std::fabs(Dot(Other.SupportFrame.Normal, Subject.SupportFrame.Normal));
            if (Separation > 0.01 || Alignment < 0.999)
                continue;

            // 📝 One representative point decides it. The loops of a well-formed sketch do not cross, so
            //    if any point of the subject is inside the other, all of them are.
            if (!OutlineEncloses(Other.SupportFrame, Other.Outline, Subject.Outline.front()))
                continue;

            ++Depth;

            // 📝 The INNERMOST encloser is the container. With three rings, the middle one is a hole in
            //    the outer and the inner is material again inside the middle -- and it is the middle that
            //    has to be cut, not the outer.
            // ⚠️ Ranked by AREA, not by vertex count. A tessellated circle carries far more points than
            //    the big rectangle around it, so counting points would name the wrong container every
            //    time a curved loop enclosed a straight one.
            const double Covered = OutlineArea(Other.SupportFrame, Other.Outline);
            if (!ContainerFound || Covered < SmallestEncloser)
            {
                SmallestEncloser = Covered;
                Container = Other.Loop;
                ContainerFound = true;
            }
        }

        Subject.Container = Container;
        Subject.Nesting = Depth;
        Subject.Hole = (Depth % 2u) == 1u;
        if (Subject.Hole)
            Subject.FillEligible = false;
    }
}

WorldSketchAnalysis AnalyzeWorldSketch(const WorldSketchStructure& Declared,
                                     std::uint32_t StepFloor,
                                     double ClosureTolerance,
                                     double CoplanarTolerance)
{
    WorldSketchAnalysis Analysis;
    Analysis.Loops.reserve(Declared.LoopCount());

    for (std::uint32_t LoopIndex = 1u; LoopIndex <= Declared.LoopCount(); ++LoopIndex)
    {
        WorldLoopAnalysisRecord Record = {};
        Record.Loop = { LoopIndex };

        if (ResolveLoopOutline(Declared, Record.Loop, StepFloor, ClosureTolerance,
                               Record.Outline, Record.Closed, Analysis.Issues) &&
            Record.Closed)
        {
            if (ResolveSupportFrameFromOutline(Record.Outline, Record.SupportFrame,
                                               Record.MaximumDeviation))
            {
                Record.Coplanar = Record.MaximumDeviation <= CoplanarTolerance;

                // 🔴 THE ARTIST'S WISH IS PART OF THE TEST, not applied afterwards by whoever draws.
                //    Two consumers read `FillEligible` -- the renderer and the picker -- and if the Fill
                //    toggle were applied at only one of them, an unfilled loop would still be pickable
                //    by its face. One question, answered once.
                const DeclaredWorldLoop* Held = Declared.Resolve(Record.Loop);
                const bool Wanted = Held == nullptr || Held->FillWanted;
                Record.FillEligible = Record.Coplanar && Wanted;
                if (!Record.Coplanar)
                {
                    Analysis.Issues.push_back({ Record.Loop, WorldLoopIssueSubject::NonCoplanar,
                                               {}, {}, Record.SupportFrame.Origin, {},
                                               Record.MaximumDeviation });
                }
            }
            else
            {
                Analysis.Issues.push_back({ Record.Loop, WorldLoopIssueSubject::DegeneratePlane,
                                           {}, {}, {}, {}, 0.0 });
            }
        }

        Analysis.Loops.push_back(std::move(Record));
    }

    ResolveLoopNesting(Analysis);
    return Analysis;
}

Deliver<ProfileSpecification> ResolvePlanarWorldLoopProfile(const WorldSketchStructure& Declared,
                                                            WorldLoopName Subject,
                                                            std::uint32_t StepFloor,
                                                            double ClosureTolerance,
                                                            double CoplanarTolerance)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > Declared.LoopCount())
        return Deliver<ProfileSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the world loop is not declared" });

    const DeclaredWorldLoop* Held = Declared.Resolve(Subject);
    if (Held == nullptr)
        return Deliver<ProfileSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the world loop could not be resolved" });

    const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Declared, StepFloor,
                                                          ClosureTolerance, CoplanarTolerance);
    const WorldLoopAnalysisRecord* Derived = nullptr;
    for (const WorldLoopAnalysisRecord& Candidate : Analysis.Loops)
        if (Candidate.Loop.IssuedIndex == Subject.IssuedIndex)
        {
            Derived = &Candidate;
            break;
        }

    if (Derived == nullptr || !Derived->Closed)
        return Deliver<ProfileSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the world loop is not closed" });
    if (!Derived->Coplanar || !Derived->SupportFrame.Declared())
        return Deliver<ProfileSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the world loop is not planar enough for a profile" });

    ProfileSpecification Profile;
    Profile.DeclarePlane({ Derived->SupportFrame.Origin,
                           Derived->SupportFrame.Normal,
                           Derived->SupportFrame.AlongDirection });

    ProfileLoop Loop;
    Loop.Orientation = ProfileLoopOrientation::Outer;
    Loop.Traversal.reserve(Held->Traversal.size());
    for (const WorldCurveUse& Use : Held->Traversal)
        Loop.Traversal.push_back({ { Use.TraversedCurve.IssuedIndex }, Use.SameSense });
    Profile.DeclareLoop(Loop);

    if (!Profile.Declared())
        return Deliver<ProfileSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the planar world loop could not become a profile" });

    return Deliver<ProfileSpecification>::Result(Profile);
}

} // namespace Slate
