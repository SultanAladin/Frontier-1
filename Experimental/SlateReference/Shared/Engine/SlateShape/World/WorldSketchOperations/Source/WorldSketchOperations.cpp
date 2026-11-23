//============================================================================================================================================
//                                                     WORLDSKETCHOPERATIONS.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchOperations/Api/WorldSketchOperations.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include "Shared/IntersectionClassifier.slang.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slate
{

namespace
{

// 📐 Two points are the same within this much, in world units.
constexpr double PointTolerance = 1.0e-6;

// 📐 How far off a curve a probe may sit and still count as being on it. Generous, because the artist is
//    pointing at a line a few pixels wide with a pointer that is itself a few pixels wide.
constexpr double OnCurveTolerance = 1.0e-3;

/// 🧩 The part of a direction that is perpendicular to an axis.
/// 📝 Gram-Schmidt, one step. The axis is assumed unit, which every caller here guarantees.
SpatialDirection PerpendicularPart(const SpatialDirection& Subject, const SpatialDirection& Axis)
{
    const double Projected = Dot(Subject, Axis);
    return { Subject.Left    - Axis.Left    * Projected,
             Subject.Up      - Axis.Up      * Projected,
             Subject.Forward - Axis.Forward * Projected };
}

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right, double Tolerance = PointTolerance)
{
    return LengthSquared(Difference(Left, Right)) <= Tolerance * Tolerance;
}

/// 🧩 A line resolved from the sketch, or nothing when the curve is missing or is not a line.
const LineCurve* ResolveLine(const WorldSketchStructure& Declared, WorldCurveName Subject)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared() || Held->Geometry.Subject() != CurveSubject::Line)
        return nullptr;
    return &Held->Geometry.HeldLine();
}

OperationVerdict ClassifySubject(const WorldSketchStructure& Declared, WorldCurveName Subject)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared())
        return OperationVerdict::SubjectMissing;
    if (Held->Geometry.Subject() != CurveSubject::Line)
        return OperationVerdict::UnsupportedGeometry;
    return OperationVerdict::Produced;
}

/// 🧩 Where a point falls along a segment, as a fraction from origin to terminus.
/// 📝 Unclamped on purpose: a value outside [0,1] means the point is beyond an end, and callers need to
///    be able to tell that from a point that merely sits near one.
double ParameterAlong(const LineCurve& Line, const SpatialPoint& Position)
{
    const SpatialDirection Span = Difference(Line.Origin, Line.Terminus);
    const double Length = LengthSquared(Span);
    if (!(Length > 0.0))
        return 0.0;
    return Dot(Difference(Line.Origin, Position), Span) / Length;
}

/// 🧩 The perpendicular distance from a point to the infinite line a segment lies on.
double DistanceToInfiniteLine(const LineCurve& Line, const SpatialPoint& Position)
{
    const SpatialDirection Span = Difference(Line.Origin, Line.Terminus);
    const double Length = std::sqrt(LengthSquared(Span));
    if (!(Length > PointTolerance))
        return std::sqrt(LengthSquared(Difference(Position, Line.Origin)));
    const SpatialDirection Along = Normalize(Span);
    const double Projected = Dot(Difference(Line.Origin, Position), Along);
    const SpatialPoint Foot = Added(Line.Origin, Scaled(Along, Projected));
    return std::sqrt(LengthSquared(Difference(Position, Foot)));
}

SpatialPoint PointAt(const LineCurve& Line, double Parameter)
{
    return Added(Line.Origin, Scaled(Difference(Line.Origin, Line.Terminus), Parameter));
}

//------------------------------------------------------------------------------------------------------------------------
//                                            CROSSINGS, MEASURED IN A SHARED PLANE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A plane in which two segments can be compared, built from the subject's own direction and the
///    separation between the two curves.
/// 🔴 THE PLANE IS DERIVED, NOT ASSUMED. Two lines on a wall have no world-Y separation, so testing them
///    as world XZ segments would report a crossing wherever they merely passed one another in depth. The
///    frame here is spanned by the subject's direction and a perpendicular that lies in the plane both
///    curves share, so the two-dimensional test is asked the question it can actually answer.
struct SharedPlane
{
    SpatialPoint     Origin = {};
    SpatialDirection Along  = {};
    SpatialDirection Across = {};
};

bool BuildSharedPlane(const LineCurve& Subject, const LineCurve& Other, SharedPlane& Frame)
{
    const SpatialDirection SubjectSpan = Difference(Subject.Origin, Subject.Terminus);
    if (LengthSquared(SubjectSpan) <= PointTolerance * PointTolerance)
        return false;

    Frame.Origin = Subject.Origin;
    Frame.Along  = Normalize(SubjectSpan);

    // 📐 The other segment's own direction, made perpendicular to the subject's. When the two are
    //    parallel this is degenerate, and parallel lines have no crossing to find anyway.
    const SpatialDirection OtherSpan = Difference(Other.Origin, Other.Terminus);
    SpatialDirection Candidate = PerpendicularPart(OtherSpan, Frame.Along);
    if (LengthSquared(Candidate) <= PointTolerance * PointTolerance)
    {
        // 📝 Parallel directions: fall back to the offset between the two lines, which still spans the
        //    plane they share unless they are collinear -- and collinear lines cross nowhere.
        Candidate = PerpendicularPart(Difference(Subject.Origin, Other.Origin), Frame.Along);
        if (LengthSquared(Candidate) <= PointTolerance * PointTolerance)
            return false;
    }

    Frame.Across = Normalize(Candidate);
    return true;
}

void PlaneCoordinates(const SharedPlane& Frame, const SpatialPoint& Position, double& Along, double& Across)
{
    const SpatialDirection Offset = Difference(Frame.Origin, Position);
    Along  = Dot(Offset, Frame.Along);
    Across = Dot(Offset, Frame.Across);
}

/// 🧩 Where another curve crosses the subject, as a parameter along the subject.
/// 🧩 Whether two segments meet at all, and where along the subject, counting TOUCHES as well as crossings.
/// out  false when they do not meet, are parallel, or meet outside the plane they were compared in.
/// note 🔴 A TOUCH IS A DIVISION. The classifier separates a transversal crossing from a touch -- one
///      segment's endpoint landing on the other -- and only the crossing was ever accepted here. But a
///      T-junction IS a touch, and so is a rectangle's corner, and those are the divisions an artist
///      actually draws. Accepting only crossings meant Trim could find nothing to bound a piece on any
///      ordinary shape, and refused; see `CollectCrossings`.
bool CrossingParameter(const LineCurve& Subject, const LineCurve& Other, double& Parameter)
{
    SharedPlane Frame;
    if (!BuildSharedPlane(Subject, Other, Frame))
        return false;

    double AlphaX = 0.0, AlphaY = 0.0, BetaX = 0.0, BetaY = 0.0;
    double GammaX = 0.0, GammaY = 0.0, DeltaX = 0.0, DeltaY = 0.0;
    PlaneCoordinates(Frame, Subject.Origin,  AlphaX, AlphaY);
    PlaneCoordinates(Frame, Subject.Terminus, BetaX,  BetaY);
    PlaneCoordinates(Frame, Other.Origin,    GammaX, GammaY);
    PlaneCoordinates(Frame, Other.Terminus,  DeltaX, DeltaY);

    // ⚠️ THE FLATTENING MUST BE CHECKED, not trusted. The other segment is only genuinely in this plane
    //    if both its endpoints project back to where they started; if they do not, the two curves are
    //    skew and a two-dimensional crossing would be an artefact of the projection.
    const SpatialPoint FlatGamma = Added(Frame.Origin,
        Added(Scaled(Frame.Along, GammaX), Scaled(Frame.Across, GammaY)));
    const SpatialPoint FlatDelta = Added(Frame.Origin,
        Added(Scaled(Frame.Along, DeltaX), Scaled(Frame.Across, DeltaY)));
    if (!SamePoint(FlatGamma, Other.Origin, OnCurveTolerance) ||
        !SamePoint(FlatDelta, Other.Terminus, OnCurveTolerance))
        return false;

    // 📝 Collinear and Disjoint are still refused. Two segments lying along one another share no single
    //    division point, and disjoint ones share nothing at all.
    const Signed32 Meeting = ClassifySegmentIntersection(AlphaX, AlphaY, BetaX, BetaY,
                                                         GammaX, GammaY, DeltaX, DeltaY);
    if (Meeting != SlateIntersectionCrossing && Meeting != SlateIntersectionTouching)
        return false;

    double CrossingX = 0.0;
    double CrossingY = 0.0;
    if (!ResolveSegmentCrossing(AlphaX, AlphaY, BetaX, BetaY, GammaX, GammaY, DeltaX, DeltaY,
                                CrossingX, CrossingY))
        return false;

    const SpatialPoint Crossing = Added(Frame.Origin,
        Added(Scaled(Frame.Along, CrossingX), Scaled(Frame.Across, CrossingY)));
    Parameter = ParameterAlong(Subject, Crossing);
    return true;
}

/// 🧩 Every crossing along a curve, as sorted parameters, excluding the curve's own ends.
void CollectCrossings(const WorldSketchStructure& Declared,
                      WorldCurveName Subject,
                      std::vector<double>& Parameters)
{
    Parameters.clear();
    const LineCurve* SubjectLine = ResolveLine(Declared, Subject);
    if (SubjectLine == nullptr)
        return;

    for (std::uint32_t Index = 1u; Index <= Declared.CurveCount(); ++Index)
    {
        if (Index == Subject.IssuedIndex)
            continue;
        const LineCurve* OtherLine = ResolveLine(Declared, { Index });
        if (OtherLine == nullptr)
            continue;

        double Parameter = 0.0;
        if (!CrossingParameter(*SubjectLine, *OtherLine, Parameter))
            continue;
        // ⚠️ A crossing at an endpoint is a junction, not a division. Cutting there produces nothing.
        if (Parameter <= 1.0e-9 || Parameter >= 1.0 - 1.0e-9)
            continue;
        Parameters.push_back(Parameter);
    }

    std::sort(Parameters.begin(), Parameters.end());
    Parameters.erase(std::unique(Parameters.begin(), Parameters.end(),
                                 [](double Left, double Right)
                                 { return std::fabs(Left - Right) <= 1.0e-9; }),
                     Parameters.end());
}

/// 🧩 The two ends of any curve, straight or curved, taken from its own polyline.
/// 🔴 A WHOLE-EDGE TRIM IS NOT A LINE OPERATION. Removing an entire edge is the same act whatever the
///    edge is -- a side of a rectangle, an arc of a slot, a circle -- so the span it shows the artist is
///    read from the curve's polyline rather than from `LineCurve`, and an arc reports the trim it is
///    about to receive instead of refusing as unsupported geometry.
/// 📝 A closed curve -- a circle, a full ellipse -- has one point for both ends; the span it names is a
///    marker on itself, which reads as "this whole ring goes" rather than as a segment across nothing.
bool CurveEndpoints(const WorldSketchStructure& Declared, WorldCurveName Subject,
                    SpatialPoint& Origin, SpatialPoint& Terminus)
{
    const DeclaredWorldCurve* const Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;

    std::vector<SpatialPoint> Polyline;
    AppendCurvePolyline(Held->Geometry, Polyline, 8u);
    if (Polyline.empty())
        return false;

    Origin   = Polyline.front();
    Terminus = Polyline.back();
    return true;
}

/// 🧩 Declares a line carrying the subject's support frame, so a piece belongs to the plane its parent did.
WorldCurveName DeclareLike(WorldSketchStructure& Declared,
                           WorldCurveName Parent,
                           const SpatialPoint& Origin,
                           const SpatialPoint& Terminus)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Parent);
    if (Held != nullptr && Held->SupportFrameStanding)
        return Declared.DeclareLine(Origin, Terminus, Held->SupportFrame);
    return Declared.DeclareLine(Origin, Terminus);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           CUT
//------------------------------------------------------------------------------------------------------------------------

OperationVerdict CutWorldCurve(WorldSketchStructure& Declared,
                               WorldCurveName Subject,
                               const SpatialPoint& Position,
                               WorldCurveName& Leading,
                               WorldCurveName& Trailing)
{
    Leading = {};
    Trailing = {};

    const OperationVerdict Classified = ClassifySubject(Declared, Subject);
    if (Classified != OperationVerdict::Produced)
        return Classified;

    const LineCurve Original = *ResolveLine(Declared, Subject);

    if (DistanceToInfiniteLine(Original, Position) > OnCurveTolerance)
        return OperationVerdict::PointNotOnCurve;

    const double Parameter = ParameterAlong(Original, Position);
    if (Parameter < -1.0e-9 || Parameter > 1.0 + 1.0e-9)
        return OperationVerdict::PointNotOnCurve;
    if (Parameter <= 1.0e-9 || Parameter >= 1.0 - 1.0e-9)
        return OperationVerdict::PointAtEnd;

    // 📐 Snapped onto the curve, so the two pieces meet exactly. Using the probe as given would leave a
    //    sub-tolerance gap at the join, and a loop that no longer closes.
    const SpatialPoint Division = PointAt(Original, Parameter);

    // 🔴 THE TRAILING PIECE IS DECLARED FIRST. If it could not be declared the subject must stay whole,
    //    and shortening it beforehand would leave the sketch with a curve that simply got shorter.
    Trailing = DeclareLike(Declared, Subject, Division, Original.Terminus);
    if (!Trailing.Assigned())
        return OperationVerdict::SubjectMissing;

    DeclaredWorldCurve* Mutable = Declared.Resolve(Subject);
    Mutable->Geometry.HeldLine().Terminus = Division;
    Leading = Subject;
    return OperationVerdict::Produced;
}

OperationVerdict CutWorldCurveAtCrossings(WorldSketchStructure& Declared,
                                          WorldCurveName Subject,
                                          std::vector<WorldCurveName>& Produced)
{
    Produced.clear();

    const OperationVerdict Classified = ClassifySubject(Declared, Subject);
    if (Classified != OperationVerdict::Produced)
        return Classified;

    std::vector<double> Parameters;
    CollectCrossings(Declared, Subject, Parameters);
    if (Parameters.empty())
        return OperationVerdict::NoIntersection;

    // 🔴 CUT FROM THE FAR END BACKWARDS. Cutting forwards would shorten the subject at the first
    //    division, and every later parameter -- measured against the ORIGINAL span -- would then name
    //    the wrong point on a curve that is no longer the same length.
    const LineCurve Original = *ResolveLine(Declared, Subject);
    for (std::size_t Index = Parameters.size(); Index-- > 0u; )
    {
        WorldCurveName Leading = {};
        WorldCurveName Trailing = {};
        const SpatialPoint Division = PointAt(Original, Parameters[Index]);
        if (CutWorldCurve(Declared, Subject, Division, Leading, Trailing)
                == OperationVerdict::Produced)
            Produced.push_back(Trailing);
    }

    if (Produced.empty())
        return OperationVerdict::NoIntersection;

    // 🔴 N CROSSINGS MAKE N+1 PIECES, and the loop above only ever declared the TRAILING one of each
    //    cut. The first piece -- from the start of the curve to the first crossing -- is the subject
    //    itself, shortened in place, and is as much a product of this operation as the others.
    std::reverse(Produced.begin(), Produced.end());
    Produced.insert(Produced.begin(), Subject);
    return OperationVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          TRIM
//------------------------------------------------------------------------------------------------------------------------

OperationVerdict TrimWorldCurve(WorldSketchStructure& Declared,
                                WorldCurveName Subject,
                                const SpatialPoint& Probe,
                                std::vector<WorldCurveName>& Remaining)
{
    Remaining.clear();

    // 🔴 A WHOLE EDGE CAN BE TRIMMED WHATEVER SHAPE IT IS. Only the crossing-bounded partial trim below
    //    is line geometry; removing an entire edge is not, so a curve the classifier calls unsupported
    //    is still eligible to be taken out in one piece. This is checked before the line refusal so an
    //    arc reaches the whole-edge path instead of being turned away as unsupported.
    const DeclaredWorldCurve* const Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared())
        return OperationVerdict::SubjectMissing;

    const bool IsLine = Held->Geometry.Subject() == CurveSubject::Line;

    // 🔴 THE PIECE UNDER THE POINTER IS THE WHOLE CURVE WHEN NOTHING DIVIDES IT. An ordinary shape's edge
    //    -- a side of a rectangle, an isolated line, an arc -- meets its neighbours only at its own ends,
    //    which are junctions and not interior crossings, so there is exactly one piece to take and it is
    //    all of it. Trim removes the piece the artist clicked; when that piece is the entire edge, the
    //    entire edge goes. This is what the tool does on the geometry it is used on most, and refusing it
    //    -- as this once did, on the argument that a whole-edge removal is a deletion -- is what made
    //    Trim appear to do nothing at all on a drawn shape.
    std::vector<double> Parameters;
    if (IsLine)
        CollectCrossings(Declared, Subject, Parameters);

    if (Parameters.empty())
    {
        // 📝 A curved edge is only ever removed whole here, so the probe need only land on it. A straight
        //    edge is held to the same tolerance the partial trim uses, so a click that misses the line is
        //    refused rather than silently deleting whatever edge happened to be nearest.
        if (IsLine)
        {
            const LineCurve Bare = *ResolveLine(Declared, Subject);
            if (DistanceToInfiniteLine(Bare, Probe) > OnCurveTolerance)
                return OperationVerdict::PointNotOnCurve;
        }

        if (!Declared.RetireCurve(Subject))
            return OperationVerdict::SubjectMissing;
        return OperationVerdict::Produced;
    }

    const LineCurve Original = *ResolveLine(Declared, Subject);
    if (DistanceToInfiniteLine(Original, Probe) > OnCurveTolerance)
        return OperationVerdict::PointNotOnCurve;

    const double At = std::clamp(ParameterAlong(Original, Probe), 0.0, 1.0);

    // 📐 The bounds of the piece the probe sits in: the nearest crossing below it and the nearest above.
    //    Absent either, that side runs to the curve's own end -- which is how an overhang is trimmed.
    double Lower = 0.0;
    double Upper = 1.0;
    bool HasLower = false;
    bool HasUpper = false;
    for (const double Parameter : Parameters)
    {
        if (Parameter <= At) { Lower = Parameter; HasLower = true; }
        else if (!HasUpper)  { Upper = Parameter; HasUpper = true; }
    }

    // 📝 A crossed curve probed in the piece beyond every crossing at BOTH ends is being asked to lose its
    //    whole span between the outermost crossings -- which, with the ends kept, is the two overhangs and
    //    the middle all at once. That is not one piece, so it is removed as the whole edge rather than
    //    guessed at.
    if (!HasLower && !HasUpper)
    {
        if (!Declared.RetireCurve(Subject))
            return OperationVerdict::SubjectMissing;
        return OperationVerdict::Produced;
    }

    const SpatialPoint LowerPoint = PointAt(Original, Lower);
    const SpatialPoint UpperPoint = PointAt(Original, Upper);

    // 🔴 A TRIM THROUGH THE MIDDLE LEAVES TWO PIECES, and both must be kept. Treating the result as one
    //    shortened curve silently discards the far side of the shape.
    if (HasLower && HasUpper)
    {
        const WorldCurveName Far = DeclareLike(Declared, Subject, UpperPoint, Original.Terminus);
        if (!Far.Assigned())
            return OperationVerdict::SubjectMissing;
        Declared.Resolve(Subject)->Geometry.HeldLine().Terminus = LowerPoint;
        Remaining.push_back(Subject);
        Remaining.push_back(Far);
        return OperationVerdict::Produced;
    }

    // 📝 An overhang: one bound is a crossing, the other is the curve's free end. The subject survives as
    //    the piece that is kept, so nothing that names it is disturbed.
    if (HasLower)
        Declared.Resolve(Subject)->Geometry.HeldLine().Terminus = LowerPoint;
    else
        Declared.Resolve(Subject)->Geometry.HeldLine().Origin = UpperPoint;

    Remaining.push_back(Subject);
    return OperationVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------

OperationVerdict EvaluateWorldTrim(const WorldSketchStructure& Declared,
                                   WorldCurveName Subject,
                                   const SpatialPoint& Probe,
                                   SpatialPoint& DepartingFrom,
                                   SpatialPoint& DepartingTo)
{
    DepartingFrom = {};
    DepartingTo   = {};

    // 🔴 THE PREVIEW IS THE COMMIT'S OWN ANSWER. Every case the trim accepts, this must accept and show,
    //    and every case it refuses, this must refuse -- a highlight over a piece the click then leaves
    //    standing reads as the tool being broken. So this mirrors `TrimWorldCurve` step for step: the
    //    whole-edge case first, the crossing-bounded piece second.
    const DeclaredWorldCurve* const Held = Declared.Resolve(Subject);
    if (Held == nullptr || !Held->Geometry.Declared())
        return OperationVerdict::SubjectMissing;

    const bool IsLine = Held->Geometry.Subject() == CurveSubject::Line;

    std::vector<double> Parameters;
    if (IsLine)
        CollectCrossings(Declared, Subject, Parameters);

    // 📝 NOTHING DIVIDES THE EDGE, SO THE WHOLE EDGE IS THE PIECE. The span shown runs end to end of the
    //    curve -- the same edge the click will remove -- so the artist sees exactly what will go. An
    //    arc's ends are read from its polyline; a straight edge is held to the on-curve tolerance so a
    //    miss is not highlighted.
    if (Parameters.empty())
    {
        if (IsLine)
        {
            const LineCurve Bare = *ResolveLine(Declared, Subject);
            if (DistanceToInfiniteLine(Bare, Probe) > OnCurveTolerance)
                return OperationVerdict::PointNotOnCurve;
        }

        if (!CurveEndpoints(Declared, Subject, DepartingFrom, DepartingTo))
            return OperationVerdict::SubjectMissing;
        return OperationVerdict::Produced;
    }

    const LineCurve Original = *ResolveLine(Declared, Subject);
    if (DistanceToInfiniteLine(Original, Probe) > OnCurveTolerance)
        return OperationVerdict::PointNotOnCurve;

    const double At = std::clamp(ParameterAlong(Original, Probe), 0.0, 1.0);

    // 📐 The same bracket the trim resolves: the nearest crossing below the probe and the nearest above,
    //    each falling back to the curve's own end so an overhang reads as a piece rather than as nothing.
    double Lower = 0.0;
    double Upper = 1.0;
    bool HasLower = false;
    bool HasUpper = false;
    for (const double Parameter : Parameters)
    {
        if (Parameter <= At) { Lower = Parameter; HasLower = true; }
        else if (!HasUpper)  { Upper = Parameter; HasUpper = true; }
    }

    // 📝 Probed beyond every crossing at both ends: the whole span between the outermost crossings goes,
    //    which the commit removes as the whole edge. The preview says the same by naming the edge's ends.
    if (!HasLower && !HasUpper)
    {
        if (!CurveEndpoints(Declared, Subject, DepartingFrom, DepartingTo))
            return OperationVerdict::SubjectMissing;
        return OperationVerdict::Produced;
    }

    // 📝 The piece that GOES is the one the probe sits in, which is bounded by the pair just found. The
    //    trim keeps everything outside it.
    DepartingFrom = PointAt(Original, Lower);
    DepartingTo   = PointAt(Original, Upper);
    return OperationVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------

OperationVerdict EvaluateWorldCut(const WorldSketchStructure& Declared,
                                  WorldCurveName Subject,
                                  const SpatialPoint& Probe,
                                  SpatialPoint& Division)
{
    Division = {};

    const OperationVerdict Classified = ClassifySubject(Declared, Subject);
    if (Classified != OperationVerdict::Produced)
        return Classified;

    const LineCurve Original = *ResolveLine(Declared, Subject);

    if (DistanceToInfiniteLine(Original, Probe) > OnCurveTolerance)
        return OperationVerdict::PointNotOnCurve;

    const double Parameter = ParameterAlong(Original, Probe);
    if (Parameter < -1.0e-9 || Parameter > 1.0 + 1.0e-9)
        return OperationVerdict::PointNotOnCurve;

    // 🔴 THE END CASE IS A REFUSAL, NOT A CUT AT THE END. Cutting where a curve already stops produces a
    //    zero-length piece, so the marker must not appear there either.
    if (Parameter <= 1.0e-9 || Parameter >= 1.0 - 1.0e-9)
        return OperationVerdict::PointAtEnd;

    Division = PointAt(Original, Parameter);
    return OperationVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         EXTEND
//------------------------------------------------------------------------------------------------------------------------

OperationVerdict EvaluateWorldExtend(const WorldSketchStructure& Declared,
                                     WorldCurveName Subject,
                                     const SpatialPoint& Probe,
                                     SpatialPoint& Landing)
{
    Landing = {};

    const OperationVerdict Classified = ClassifySubject(Declared, Subject);
    if (Classified != OperationVerdict::Produced)
        return Classified;

    const LineCurve Original = *ResolveLine(Declared, Subject);

    // 📐 Which end the artist meant: the nearer one to the probe.
    const bool GrowTerminus =
        LengthSquared(Difference(Probe, Original.Terminus)) <=
        LengthSquared(Difference(Probe, Original.Origin));

    // 📐 A ray from the moving end, along the curve's own direction, made long enough to reach anything
    //    in the sketch. The ray is a SEGMENT so the same exact crossing test can be used -- and its reach
    //    is derived from the sketch's own extent rather than being a magic number.
    double Reach = 0.0;
    for (std::uint32_t Index = 1u; Index <= Declared.CurveCount(); ++Index)
    {
        const LineCurve* Other = ResolveLine(Declared, { Index });
        if (Other == nullptr)
            continue;
        Reach = std::max(Reach, std::sqrt(LengthSquared(Difference(Original.Origin, Other->Origin))));
        Reach = std::max(Reach, std::sqrt(LengthSquared(Difference(Original.Origin, Other->Terminus))));
    }
    Reach = (Reach + std::sqrt(LengthSquared(Difference(Original.Origin, Original.Terminus)))) * 2.0 + 1.0;

    const SpatialDirection Span = Difference(Original.Origin, Original.Terminus);
    if (LengthSquared(Span) <= PointTolerance * PointTolerance)
        return OperationVerdict::UnsupportedGeometry;
    const SpatialDirection Outward = GrowTerminus ? Normalize(Span)
                                                  : Scaled(Normalize(Span), -1.0);
    const SpatialPoint From = GrowTerminus ? Original.Terminus : Original.Origin;

    LineCurve Ray;
    Ray.Origin   = From;
    Ray.Terminus = Added(From, Scaled(Outward, Reach));

    // 🔴 THE NEAREST CROSSING WINS. "Extend to meet" means the first thing in the way; stopping at the
    //    furthest would pass straight through geometry the artist can see.
    bool Found = false;
    double Best = 0.0;
    for (std::uint32_t Index = 1u; Index <= Declared.CurveCount(); ++Index)
    {
        if (Index == Subject.IssuedIndex)
            continue;
        const LineCurve* Other = ResolveLine(Declared, { Index });
        if (Other == nullptr)
            continue;

        double Parameter = 0.0;
        if (!CrossingParameter(Ray, *Other, Parameter))
            continue;
        if (Parameter <= 1.0e-9)
            continue;
        if (!Found || Parameter < Best)
        {
            Best = Parameter;
            Found = true;
        }
    }

    if (!Found)
        return OperationVerdict::NoIntersection;

    Landing = PointAt(Ray, Best);
    return OperationVerdict::Produced;
}

OperationVerdict ExtendWorldCurve(WorldSketchStructure& Declared,
                                  WorldCurveName Subject,
                                  const SpatialPoint& Probe)
{
    SpatialPoint Landing = {};
    const OperationVerdict Verdict = EvaluateWorldExtend(Declared, Subject, Probe, Landing);
    if (Verdict != OperationVerdict::Produced)
        return Verdict;

    LineCurve& Line = Declared.Resolve(Subject)->Geometry.HeldLine();
    const bool GrowTerminus =
        LengthSquared(Difference(Probe, Line.Terminus)) <= LengthSquared(Difference(Probe, Line.Origin));

    // 🔴 ONLY THE NAMED END MOVES. The far end and the direction are untouched, which is what makes the
    //    result predictable from where the artist clicked.
    (GrowTerminus ? Line.Terminus : Line.Origin) = Landing;
    return OperationVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         OFFSET
//------------------------------------------------------------------------------------------------------------------------

namespace
{

/// 🧩 One leg of a chain, flattened into the offset frame.
struct FlatLeg
{
    double OriginAlong = 0.0, OriginAcross = 0.0;
    double EndAlong = 0.0,    EndAcross = 0.0;
};

bool FlattenChain(const WorldSketchStructure& Declared,
                  const std::vector<WorldCurveName>& Chain,
                  const WorldPlacementFrame& Frame,
                  std::vector<FlatLeg>& Legs)
{
    Legs.clear();
    for (const WorldCurveName& Name : Chain)
    {
        const LineCurve* Line = ResolveLine(Declared, Name);
        if (Line == nullptr)
            return false;
        FlatLeg Leg;
        ResolveWorldPlacementCoordinates(Frame, Line->Origin,   Leg.OriginAlong, Leg.OriginAcross);
        ResolveWorldPlacementCoordinates(Frame, Line->Terminus, Leg.EndAlong,    Leg.EndAcross);
        Legs.push_back(Leg);
    }
    return !Legs.empty();
}

/// 🧩 A leg pushed sideways by `Distance`, in the flattened frame.
/// 📐 The left normal of (dx, dy) is (-dy, dx); a negative distance takes the other side.
bool PushLeg(const FlatLeg& Leg, double Distance, FlatLeg& Pushed)
{
    const double SpanX = Leg.EndAlong - Leg.OriginAlong;
    const double SpanY = Leg.EndAcross - Leg.OriginAcross;
    const double Length = std::sqrt(SpanX * SpanX + SpanY * SpanY);
    if (!(Length > PointTolerance))
        return false;
    const double NormalX = -SpanY / Length * Distance;
    const double NormalY =  SpanX / Length * Distance;
    Pushed.OriginAlong  = Leg.OriginAlong + NormalX;
    Pushed.OriginAcross = Leg.OriginAcross + NormalY;
    Pushed.EndAlong     = Leg.EndAlong + NormalX;
    Pushed.EndAcross    = Leg.EndAcross + NormalY;
    return true;
}

/// 🧩 Where two pushed legs' infinite lines meet, which is the offset chain's corner.
/// 🔴 THE INTERSECTION, NOT THE PUSHED ENDPOINT. Pushing each corner along its own leg's normal leaves a
///    gap on the outside of a bend and an overlap on the inside, and the offset distance stops being
///    constant -- which is the only property an offset has.
bool MeetLegs(const FlatLeg& First, const FlatLeg& Second, double& MeetX, double& MeetY)
{
    const double AX = First.EndAlong - First.OriginAlong;
    const double AY = First.EndAcross - First.OriginAcross;
    const double BX = Second.EndAlong - Second.OriginAlong;
    const double BY = Second.EndAcross - Second.OriginAcross;

    const double Denominator = AX * BY - AY * BX;
    if (std::fabs(Denominator) <= 1.0e-12)
        return false;   // 📝 Parallel legs: the two pushed lines never meet, and the joint is a straight run.

    const double DX = Second.OriginAlong - First.OriginAlong;
    const double DY = Second.OriginAcross - First.OriginAcross;
    const double Travel = (DX * BY - DY * BX) / Denominator;

    MeetX = First.OriginAlong + AX * Travel;
    MeetY = First.OriginAcross + AY * Travel;
    return true;
}

/// 🧩 Builds the offset chain's vertices in the flattened frame.
bool BuildOffsetVertices(const std::vector<FlatLeg>& Legs, double Distance,
                         std::vector<double>& AlongOut, std::vector<double>& AcrossOut)
{
    AlongOut.clear();
    AcrossOut.clear();

    std::vector<FlatLeg> Pushed;
    Pushed.reserve(Legs.size());
    for (const FlatLeg& Leg : Legs)
    {
        FlatLeg Moved;
        if (!PushLeg(Leg, Distance, Moved))
            return false;
        Pushed.push_back(Moved);
    }

    AlongOut.push_back(Pushed.front().OriginAlong);
    AcrossOut.push_back(Pushed.front().OriginAcross);

    for (std::size_t Index = 0u; Index + 1u < Pushed.size(); ++Index)
    {
        double MeetX = 0.0;
        double MeetY = 0.0;
        if (MeetLegs(Pushed[Index], Pushed[Index + 1u], MeetX, MeetY))
        {
            AlongOut.push_back(MeetX);
            AcrossOut.push_back(MeetY);
        }
        else
        {
            // 📝 Parallel neighbours run straight on; the shared end is the pushed point itself.
            AlongOut.push_back(Pushed[Index].EndAlong);
            AcrossOut.push_back(Pushed[Index].EndAcross);
        }
    }

    AlongOut.push_back(Pushed.back().EndAlong);
    AcrossOut.push_back(Pushed.back().EndAcross);
    return true;
}

/// 🧩 Whether an offset at this distance would invert any leg.
/// 📐 A leg collapses when its offset copy runs backwards relative to the original -- the sign of the dot
///    product of the two directions flips. That is the exact moment the shape starts self-intersecting.
bool OffsetWouldCollapse(const std::vector<FlatLeg>& Legs, double Distance)
{
    std::vector<double> Along;
    std::vector<double> Across;
    if (!BuildOffsetVertices(Legs, Distance, Along, Across))
        return true;

    for (std::size_t Index = 0u; Index < Legs.size(); ++Index)
    {
        const double OriginalX = Legs[Index].EndAlong - Legs[Index].OriginAlong;
        const double OriginalY = Legs[Index].EndAcross - Legs[Index].OriginAcross;
        const double CopyX = Along[Index + 1u] - Along[Index];
        const double CopyY = Across[Index + 1u] - Across[Index];
        if (OriginalX * CopyX + OriginalY * CopyY <= 0.0)
            return true;
    }
    return false;
}

} // namespace

OperationVerdict OffsetWorldChain(WorldSketchStructure& Declared,
                                  const std::vector<WorldCurveName>& Chain,
                                  const WorldPlacementFrame& Frame,
                                  double Distance,
                                  std::vector<WorldCurveName>& Produced)
{
    Produced.clear();
    if (Chain.empty())
        return OperationVerdict::SubjectMissing;
    if (std::fabs(Distance) <= PointTolerance)
        return OperationVerdict::DistanceNotPositive;

    for (const WorldCurveName& Name : Chain)
    {
        const OperationVerdict Classified = ClassifySubject(Declared, Name);
        if (Classified != OperationVerdict::Produced)
            return Classified;
    }

    std::vector<FlatLeg> Legs;
    if (!FlattenChain(Declared, Chain, Frame, Legs))
        return OperationVerdict::UnsupportedGeometry;

    if (OffsetWouldCollapse(Legs, Distance))
        return OperationVerdict::WouldCollapse;

    std::vector<double> Along;
    std::vector<double> Across;
    if (!BuildOffsetVertices(Legs, Distance, Along, Across))
        return OperationVerdict::UnsupportedGeometry;

    // 🔴 THE ORIGINAL IS NEVER TOUCHED. Every curve here is newly declared; an offset that moved the
    //    curve it was measured from would not be an offset.
    for (std::size_t Index = 0u; Index + 1u < Along.size(); ++Index)
    {
        const SpatialPoint Origin   = ResolveWorldPlacementPosition(Frame, Along[Index], Across[Index]);
        const SpatialPoint Terminus = ResolveWorldPlacementPosition(Frame, Along[Index + 1u], Across[Index + 1u]);
        Produced.push_back(Declared.DeclareLine(Origin, Terminus, Frame));
    }

    return Produced.empty() ? OperationVerdict::UnsupportedGeometry : OperationVerdict::Produced;
}

double ResolveOffsetLimit(const WorldSketchStructure& Declared,
                          const std::vector<WorldCurveName>& Chain,
                          const WorldPlacementFrame& Frame)
{
    std::vector<FlatLeg> Legs;
    if (!FlattenChain(Declared, Chain, Frame, Legs))
        return 0.0;

    // 📐 Bisection on the collapse test. The predicate is monotone in the distance -- once a leg has
    //    inverted, pushing further cannot un-invert it -- so bisection converges on the exact threshold,
    //    and it needs no closed form for a chain of arbitrary shape.
    double Reach = 0.0;
    for (const FlatLeg& Leg : Legs)
    {
        const double SpanX = Leg.EndAlong - Leg.OriginAlong;
        const double SpanY = Leg.EndAcross - Leg.OriginAcross;
        Reach += std::sqrt(SpanX * SpanX + SpanY * SpanY);
    }
    if (!(Reach > 0.0))
        return 0.0;

    // 📝 Negative is the inward side by convention here; the caller negates for the other.
    double Safe = 0.0;
    double Unsafe = -Reach;
    if (!OffsetWouldCollapse(Legs, Unsafe))
        return Reach;

    for (std::uint32_t Step = 0u; Step < 60u; ++Step)
    {
        const double Middle = (Safe + Unsafe) * 0.5;
        if (OffsetWouldCollapse(Legs, Middle))
            Unsafe = Middle;
        else
            Safe = Middle;
    }
    return std::fabs(Safe);
}

} // namespace Slate
