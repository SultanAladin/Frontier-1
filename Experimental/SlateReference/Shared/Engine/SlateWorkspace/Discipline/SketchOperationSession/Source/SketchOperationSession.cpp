//============================================================================================================================================
//                                                     SKETCHOPERATIONSESSION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/SketchOperationSession/Api/SketchOperationSession.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slate
{

namespace
{

/// 🧩 How far a point sits from a segment, and where on it the nearest place is.
double DistanceToSegment(const SpatialPoint& Probe, const SpatialPoint& Start, const SpatialPoint& End)
{
    // 📝 `Difference(From, To)` reads To minus From, so the span runs Start to End and the offset runs
    //    Start to Probe. Reversing either silently mirrors the foot to the wrong side of the segment.
    const SpatialDirection Span = Difference(Start, End);
    const double SpanLengthSquared = LengthSquared(Span);
    if (SpanLengthSquared <= 1.0e-18)
        return std::sqrt(LengthSquared(Difference(Start, Probe)));

    const double Parameter = std::clamp(Dot(Difference(Start, Probe), Span) / SpanLengthSquared, 0.0, 1.0);
    const SpatialPoint Nearest = { Start.Left    + Span.Left    * Parameter,
                                   Start.Up      + Span.Up      * Parameter,
                                   Start.Forward + Span.Forward * Parameter };
    return std::sqrt(LengthSquared(Difference(Nearest, Probe)));
}

/// 🧩 How far a point sits from a curve of any kind.
/// 📝 Through the tessellation, so an arc is measured as an arc rather than as its chord. The same
///    polyline the renderer draws is the one the pointer is tested against, so what looks near is near.
double DistanceToCurve(const CurveSpecification& Geometry, const SpatialPoint& Probe)
{
    std::vector<SpatialPoint> Polyline;
    AppendCurvePolyline(Geometry, Polyline, 48u);
    if (Polyline.size() < 2u)
        return std::numeric_limits<double>::max();

    double Nearest = std::numeric_limits<double>::max();
    for (std::size_t Index = 1u; Index < Polyline.size(); ++Index)
        Nearest = std::min(Nearest, DistanceToSegment(Probe, Polyline[Index - 1u], Polyline[Index]));
    return Nearest;
}

/// 🧩 The curve nearest the pointer, within reach.
/// 📝 The reach is the frame's, so the target holds its size on screen at every zoom.
WorldCurveName CurveNear(const WorldSketchStructure& Declared, const SpatialPoint& Probe, double Reach)
{
    WorldCurveName Found = {};
    double Nearest = Reach;

    // 📝 A curve's name is its position in the list, one-based. The list carries no name of its own, so
    //    the index is recovered here rather than read off the record.
    const std::vector<DeclaredWorldCurve>& Held = Declared.Curves();
    for (std::size_t Index = 0u; Index < Held.size(); ++Index)
    {
        const double Distance = DistanceToCurve(Held[Index].Geometry, Probe);
        if (Distance < Nearest)
        {
            Nearest = Distance;
            Found = WorldCurveName{ static_cast<std::uint32_t>(Index + 1u) };
        }
    }
    return Found;
}

/// 🧩 The point on a named curve nearest the pointer.
/// 🔴 THE PROBE MUST BE ON THE CURVE BEFORE THE OPERATIONS SEE IT. Reaching a curve is deliberately
///    generous -- twelve PIXELS, which at metre scale is a long way in world units -- but Cut and Trim
///    both refuse a probe further than `OnCurveTolerance` (one thousandth of a unit) from the line. So
///    the gesture would name a curve, arm, and then be refused by the very operation it had armed: the
///    artist clicks a line, the tool highlights it, and nothing happens. Snapping here closes the gap
///    between what the pointer may reach and what the operation will accept.
/// 📝 Measured along the tessellation, exactly as `DistanceToCurve` measures, so the snapped point lies
///    on the polyline the artist can actually see.
SpatialPoint PointOnCurveNear(const CurveSpecification& Geometry, const SpatialPoint& Probe)
{
    std::vector<SpatialPoint> Polyline;
    AppendCurvePolyline(Geometry, Polyline, 48u);
    if (Polyline.size() < 2u)
        return Probe;

    SpatialPoint Best = Polyline.front();
    double Nearest = std::numeric_limits<double>::max();

    for (std::size_t Index = 1u; Index < Polyline.size(); ++Index)
    {
        const SpatialPoint& Start = Polyline[Index - 1u];
        const SpatialPoint& End   = Polyline[Index];

        const SpatialDirection Span = Difference(Start, End);
        const double Length = LengthSquared(Span);

        // 📐 The projection of the probe onto this chord, held inside it so the answer is a point on the
        //    segment rather than on the infinite line through it.
        const double Parameter = Length <= 1.0e-18
            ? 0.0
            : std::clamp(Dot(Difference(Start, Probe), Span) / Length, 0.0, 1.0);

        const SpatialPoint Candidate = { Start.Left    + Span.Left    * Parameter,
                                         Start.Up      + Span.Up      * Parameter,
                                         Start.Forward + Span.Forward * Parameter };

        const double Distance = LengthSquared(Difference(Candidate, Probe));
        if (Distance < Nearest)
        {
            Nearest = Distance;
            Best = Candidate;
        }
    }
    return Best;
}

/// 🧩 The loop that traverses a named curve, if any does.
/// 🔴 FILL HAD NO WAY TO NAME ITS SUBJECT AT ALL. `Session.Loop` was cleared on cancel and read on
///    apply, and nothing in between ever wrote it -- so `DeclareWorldLoopFill` was handed a default
///    `WorldLoopName`, `Resolve` refused it, and Fill could not work however the artist clicked. The
///    curve under the pointer is already known; the loop that uses it is the loop being pointed at.
/// 📝 The first loop that traverses the curve. A curve shared by two loops is a seam, and the artist
///    pointing at it has named both equally; taking the first is a stated choice rather than an
///    accident, and the alternative -- refusing an ambiguous seam -- would make the tool feel broken at
///    exactly the places a shape is most likely to need it.
WorldLoopName LoopTraversing(const WorldSketchStructure& Declared, WorldCurveName Curve)
{
    if (!Curve.Assigned())
        return {};

    const std::vector<DeclaredWorldLoop>& Held = Declared.Loops();
    for (std::size_t Index = 0u; Index < Held.size(); ++Index)
        for (const WorldCurveUse& Use : Held[Index].Traversal)
            if (Use.TraversedCurve.IssuedIndex == Curve.IssuedIndex)
                return WorldLoopName{ static_cast<std::uint32_t>(Index + 1u) };

    return {};
}

/// 🧩 Holds a figure inside what the geometry accepts, recording whether it had to.
/// note  📝 Offset is signed -- the artist may drag either side -- so the limit binds the MAGNITUDE and
///        the direction is kept. Clamping the signed value would silently flip an inward drag outward.
double HoldWithinLimit(double Asked, double Limit, bool& Clamped)
{
    if (Limit <= 0.0)
    {
        Clamped = false;
        return Asked;
    }
    Clamped = std::fabs(Asked) > Limit;
    return std::clamp(Asked, -Limit, Limit);
}

/// 🧩 The offset distance the pointer is asking for: how far it has strayed from the chain.
double AskedOffset(const WorldSketchStructure& Declared,
                   const std::vector<WorldCurveName>& Chain,
                   const WorldPlacementFrame& Frame,
                   const SpatialPoint& Probe)
{
    double Nearest = std::numeric_limits<double>::max();
    for (const WorldCurveName& Name : Chain)
    {
        const DeclaredWorldCurve* Held = Declared.Resolve(Name);
        if (Held == nullptr)
            continue;
        Nearest = std::min(Nearest, DistanceToCurve(Held->Geometry, Probe));
    }
    if (Nearest == std::numeric_limits<double>::max())
        return 0.0;

    // 🔴 THE SIGN COMES FROM WHICH SIDE THE POINTER IS ON, not from the distance, which is never
    //    negative. Without it, dragging inwards and outwards would both offset the same way and half of
    //    the gesture would be unreachable.
    const DeclaredWorldCurve* First = Declared.Resolve(Chain.front());
    if (First == nullptr)
        return Nearest;

    double Along = 0.0;
    double Across = 0.0;
    ResolveWorldPlacementCoordinates(Frame, Probe, Along, Across);

    std::vector<SpatialPoint> Reference;
    AppendCurvePolyline(First->Geometry, Reference, 8u);
    if (Reference.empty())
        return Nearest;

    double CentreAlong = 0.0;
    double CentreAcross = 0.0;
    ResolveWorldPlacementCoordinates(Frame, Reference.front(), CentreAlong, CentreAcross);

    return (Across >= CentreAcross) ? Nearest : -Nearest;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE GESTURE
//------------------------------------------------------------------------------------------------------------------------

void AdvanceSketchOperationSession(const WorldSketchStructure& Declared,
                                   const std::vector<WorldCurveName>& Chain,
                                   const WorldPlacementFrame& Frame,
                                   const OperationPointerFrame& Pointer,
                                   SketchOperationSession& Session)
{
    // 🔴 `Applied` LASTS ONE FRAME, as it does for the corner gesture. The caller reads it, and the
    //    session then goes back to hunting. Left standing, every later frame reads as a fresh commit.
    if (Session.Phase == OperationPhase::Applied)
        Session.Phase = OperationPhase::Idle;

    // ① A pending figure belongs to the readout. The pointer no longer steers it; the artist is typing.
    if (Session.Phase == OperationPhase::Pending)
        return;

    // ② A drag in progress. Only Offset ever reaches here.
    if (Session.Phase == OperationPhase::Dragging)
    {
        Session.Distance = HoldWithinLimit(AskedOffset(Declared, Chain, Frame, Pointer.Probe),
                                           Session.Limit, Session.Clamped);

        // 🔴 THE RELEASE DOES NOT APPLY, exactly as the fillet's does not. It hands the figure to the
        //    readout, which is the artist's chance to type an exact number before anything is written.
        if (Pointer.Released)
            Session.Phase = OperationPhase::Pending;
        return;
    }

    // ③ Hunting. What is under the pointer, and what would happen to it?
    // 🔴 THE FRAME'S OWN REACH, so a curve is as easy to hit at ten metres as at ten millimetres. The
    //    constant remains the fallback for a caller that states nothing.
    const double Reach = Pointer.Reach > 0.0 ? Pointer.Reach : OperationProbeReach;

    Session.Target = CurveNear(Declared, Pointer.Probe, Reach);

    // 🔴 SNAPPED ONTO THE CURVE THAT WAS JUST FOUND. Reaching one is generous by design; Cut and Trim
    //    are not, and refuse anything further than a thousandth of a unit off the line. Storing the raw
    //    pointer here is what let the gesture arm on a curve the operation would then refuse -- the
    //    artist clicks the line and nothing happens. The snapped point is also what Cut divides at and
    //    what Trim measures its piece from, so it must be the one remembered.
    const DeclaredWorldCurve* const Reached =
        Session.Target.Assigned() ? Declared.Resolve(Session.Target) : nullptr;

    Session.Probe = Reached != nullptr && Reached->Geometry.Declared()
                  ? PointOnCurveNear(Reached->Geometry, Pointer.Probe)
                  : Pointer.Probe;

    Session.Clamped = false;

    if (Session.Manner == OperationManner::Offset)
    {
        // 📝 Offset acts on a chain the artist has already chosen, so it does not hunt for a target.
        Session.Preview = Chain.empty() ? OperationVerdict::SubjectMissing : OperationVerdict::Produced;
        Session.Phase = Chain.empty() ? OperationPhase::Idle : OperationPhase::Ready;
        Session.Limit = Chain.empty() ? 0.0 : ResolveOffsetLimit(Declared, Chain, Frame);

        if (Session.Phase == OperationPhase::Ready && Pointer.Pressed)
            Session.Phase = OperationPhase::Dragging;
        return;
    }

    if (!Session.Target.Assigned())
    {
        Session.Phase = OperationPhase::Idle;
        Session.Preview = OperationVerdict::SubjectMissing;
        return;
    }

    // 🔴 THE PREVIEW IS THE COMMIT'S OWN ANSWER, asked of the same functions and thrown away. A preview
    //    that reasoned separately about what would happen would eventually disagree with what does, and
    //    the artist would be shown a result they do not get.
    switch (Session.Manner)
    {
        case OperationManner::Extend:
            Session.Preview = EvaluateWorldExtend(Declared, Session.Target, Session.Probe, Session.Landing);
            break;

        case OperationManner::Fill:
            // 🔴 FILL ACTS ON A LOOP, AND THE LOOP IS NAMED HERE. Nothing used to write `Session.Loop`,
            //    so the apply below resolved a default name, was refused, and the tool did nothing at
            //    all. Pointing at a curve names the loop that traverses it; pointing at a curve no loop
            //    uses is a curve with no face to toggle, and the preview says so rather than pretending.
            Session.Loop = LoopTraversing(Declared, Session.Target);
            Session.Preview = Session.Loop.Assigned() ? OperationVerdict::Produced
                                                      : OperationVerdict::SubjectMissing;
            break;

        case OperationManner::Trim:
            // 🔴 THE PIECE THAT WOULD GO, RESOLVED EVERY FRAME. This used to answer `Produced` merely
            //    because a curve was in reach, which promised a trim at places the commit then refused
            //    -- a curve with no crossings, or a probe off the line -- and left the artist clicking
            //    at nothing. Asking the real dry run costs one crossing search and makes the highlight
            //    and the refusal agree, because they are now the same answer.
            Session.Preview = EvaluateWorldTrim(Declared, Session.Target, Session.Probe,
                                                Session.DepartingFrom, Session.DepartingTo);
            break;

        case OperationManner::Cut:
            // 🔴 AND THE POINT THE CUT WOULD FALL ON, snapped onto the curve. Reaching a curve was the
            //    whole of this preview too, so Cut reported success while hovering the very ends it
            //    refuses to divide.
            Session.Preview = EvaluateWorldCut(Declared, Session.Target, Session.Probe,
                                               Session.Division);
            break;

        default:
            // 📝 Anything without a dry run of its own is previewed by having reached a curve at all.
            Session.Preview = OperationVerdict::Produced;
            break;
    }

    Session.Phase = OperationPhase::Ready;

    // 🔴 THE CLICK OPERATIONS PERFORM ON RELEASE AND RAISE NO READOUT. They have no figure to set, so a
    //    popup would be asking the artist to confirm a decision they have already made by clicking.
    if (Pointer.Released && Session.Preview == OperationVerdict::Produced)
        Session.Phase = OperationPhase::Applied;
}

//------------------------------------------------------------------------------------------------------------------------

void DeclareOperationDistance(SketchOperationSession& Session, double Distance)
{
    // 📝 The typed figure passes the same clamp as the dragged one. A number that cannot be dragged to
    //    cannot be typed either, or the readout would be a way around the limit rather than a way to be
    //    precise within it.
    Session.Distance = HoldWithinLimit(Distance, Session.Limit, Session.Clamped);
}

//------------------------------------------------------------------------------------------------------------------------

OperationVerdict PerformSketchOperation(WorldSketchStructure& Declared,
                                        const std::vector<WorldCurveName>& Chain,
                                        const WorldPlacementFrame& Frame,
                                        SketchOperationSession& Session,
                                        std::vector<WorldCurveName>& Produced)
{
    Produced.clear();

    const bool Standing = Session.Phase == OperationPhase::Applied ||
                          Session.Phase == OperationPhase::Pending;
    if (!Standing)
        return OperationVerdict::SubjectMissing;

    OperationVerdict Verdict = OperationVerdict::SubjectMissing;
    switch (Session.Manner)
    {
        case OperationManner::Cut:
        {
            WorldCurveName Leading = {};
            WorldCurveName Trailing = {};
            Verdict = CutWorldCurve(Declared, Session.Target, Session.Probe, Leading, Trailing);
            if (Verdict == OperationVerdict::Produced)
            {
                Produced.push_back(Leading);
                Produced.push_back(Trailing);
            }
            break;
        }

        case OperationManner::Trim:
            Verdict = TrimWorldCurve(Declared, Session.Target, Session.Probe, Produced);
            break;

        case OperationManner::Extend:
            Verdict = ExtendWorldCurve(Declared, Session.Target, Session.Probe);
            if (Verdict == OperationVerdict::Produced)
                Produced.push_back(Session.Target);
            break;

        case OperationManner::Offset:
            Verdict = OffsetWorldChain(Declared, Chain, Frame, Session.Distance, Produced);
            break;

        case OperationManner::Fill:
            Verdict = DeclareWorldLoopFill(Declared, Session.Loop,
                                           !WorldLoopFillWanted(Declared, Session.Loop))
                          ? OperationVerdict::Produced
                          : OperationVerdict::SubjectMissing;
            break;

        default:
            break;
    }

    Session.Phase = OperationPhase::Idle;
    Session.Clamped = false;
    return Verdict;
}

//------------------------------------------------------------------------------------------------------------------------

void CancelSketchOperationSession(SketchOperationSession& Session)
{
    // 📝 Nothing is undone because nothing was written. Every one of these operations writes only in
    //    `PerformSketchOperation`, which is what makes cancelling free.
    Session.Phase = OperationPhase::Idle;
    Session.Distance = 0.0;
    Session.Clamped = false;
    Session.Target = {};
    Session.Loop = {};
    Session.Preview = OperationVerdict::SubjectMissing;

    // 📝 The preview geometry goes with the preview. A stale span left standing here is a highlight over
    //    a piece the artist is no longer pointing at, which is worse than no highlight at all.
    Session.DepartingFrom = {};
    Session.DepartingTo = {};
    Session.Division = {};
    Session.Landing = {};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FILL
//------------------------------------------------------------------------------------------------------------------------

bool DeclareWorldLoopFill(WorldSketchStructure& Declared, WorldLoopName Subject, bool Wanted)
{
    DeclaredWorldLoop* Held = Declared.Resolve(Subject);
    if (Held == nullptr)
        return false;

    // 🔴 THE WISH IS RECORDED EVEN WHEN THE GEOMETRY CANNOT HONOUR IT. Filling an open loop turns nothing
    //    on today, and turns it on the moment the loop is closed -- which is what an artist who filled it
    //    then closed it expects, rather than having to fill it a second time.
    Held->FillWanted = Wanted;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------

bool WorldLoopFillWanted(const WorldSketchStructure& Declared, WorldLoopName Subject)
{
    const DeclaredWorldLoop* Held = Declared.Resolve(Subject);
    return Held != nullptr && Held->FillWanted;
}

} // namespace Slate
