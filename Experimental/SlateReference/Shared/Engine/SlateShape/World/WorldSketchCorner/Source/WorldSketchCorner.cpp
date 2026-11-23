//============================================================================================================================================
//                                                       WORLDSKETCHCORNER.CPP
//============================================================================================================================================

#include "SlateShape/World/WorldSketchCorner/Api/WorldSketchCorner.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{

// 📐 Two endpoints are the same corner within this much. Endpoints that were authored by clicking a snap
//    agree exactly; endpoints that arrived through a transform agree to rounding.
constexpr double JunctionTolerance = 1.0e-6;

// 📐 Below this the two legs are running straight through each other and there is no corner to round.
//    At 179.99 degrees a fillet's tangent distance already exceeds any leg a sketch is likely to hold.
constexpr double CollinearTolerance = 1.0e-4;

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right)
{
    return LengthSquared(Difference(Left, Right)) <= JunctionTolerance * JunctionTolerance;
}

/// 🧩 One leg of a corner, oriented so that `Far` is the end AWAY from the junction.
/// 📝 Orienting both legs the same way is what lets the arithmetic below stop caring which end of which
///    curve the artist happened to draw first. Every asymmetry is resolved here, once.
struct Leg
{
    WorldCurveName   Name    = {};
    SpatialPoint     Corner  = {};   // [-] - the shared endpoint
    SpatialPoint     Far     = {};   // [-] - the other end
    SpatialDirection Outward = {};   // [-] - unit, from the corner towards Far
    double           Length  = 0.0;
    bool             CornerIsOrigin = false;   // [-] - which end of the stored line the corner is
};

/// 🧩 Reads a curve as a straight leg, refusing anything that is not a line.
bool ResolveLeg(const WorldSketchStructure& Declared, WorldCurveName Name, Leg& Resolved)
{
    const DeclaredWorldCurve* Held = Declared.Resolve(Name);
    if (Held == nullptr || !Held->Geometry.Declared())
        return false;
    if (Held->Geometry.Subject() != CurveSubject::Line)
        return false;

    Resolved.Name = Name;
    return true;
}

/// 🧩 Orients two legs about the endpoint they share.
/// out  false when they share no endpoint, which is the ordinary answer for two unrelated curves.
bool OrientLegs(const WorldSketchStructure& Declared,
                WorldCurveName FirstName,
                WorldCurveName SecondName,
                Leg& First,
                Leg& Second)
{
    if (!FirstName.Assigned() || !SecondName.Assigned() || FirstName.IssuedIndex == SecondName.IssuedIndex)
        return false;
    if (!ResolveLeg(Declared, FirstName, First) || !ResolveLeg(Declared, SecondName, Second))
        return false;

    const LineCurve& FirstLine  = Declared.Resolve(FirstName)->Geometry.HeldLine();
    const LineCurve& SecondLine = Declared.Resolve(SecondName)->Geometry.HeldLine();

    // 🔴 FOUR WAYS TWO SEGMENTS CAN MEET, and all four are corners. Testing only origin-to-terminus --
    //    the arrangement a loop's traversal happens to produce -- would have found the corners of a
    //    rectangle and missed the corner of an L drawn by clicking outwards from the middle.
    const SpatialPoint* Shared = nullptr;
    if (SamePoint(FirstLine.Origin, SecondLine.Origin))        { Shared = &FirstLine.Origin;   First.CornerIsOrigin = true;  Second.CornerIsOrigin = true;  }
    else if (SamePoint(FirstLine.Origin, SecondLine.Terminus)) { Shared = &FirstLine.Origin;   First.CornerIsOrigin = true;  Second.CornerIsOrigin = false; }
    else if (SamePoint(FirstLine.Terminus, SecondLine.Origin)) { Shared = &FirstLine.Terminus; First.CornerIsOrigin = false; Second.CornerIsOrigin = true;  }
    else if (SamePoint(FirstLine.Terminus, SecondLine.Terminus)){ Shared = &FirstLine.Terminus; First.CornerIsOrigin = false; Second.CornerIsOrigin = false; }
    else
        return false;

    First.Corner  = *Shared;
    Second.Corner = *Shared;
    First.Far     = First.CornerIsOrigin  ? FirstLine.Terminus  : FirstLine.Origin;
    Second.Far    = Second.CornerIsOrigin ? SecondLine.Terminus : SecondLine.Origin;

    const SpatialDirection FirstSpan  = Difference(First.Corner, First.Far);
    const SpatialDirection SecondSpan = Difference(Second.Corner, Second.Far);
    First.Length  = std::sqrt(LengthSquared(FirstSpan));
    Second.Length = std::sqrt(LengthSquared(SecondSpan));
    if (!(First.Length > JunctionTolerance) || !(Second.Length > JunctionTolerance))
        return false;

    First.Outward  = Normalize(FirstSpan);
    Second.Outward = Normalize(SecondSpan);
    return true;
}

/// 🧩 The interior angle between two oriented legs, in radians.
double CornerRadians(const Leg& First, const Leg& Second)
{
    // 📐 Both directions point AWAY from the corner, so their dot product is the cosine of the interior
    //    angle directly -- no sign correction, and none of the quadrant ambiguity that made an unsigned
    //    `acos` the wrong tool for measuring an arc's sweep elsewhere in this tree. Here the answer is
    //    genuinely in [0, pi] and unsigned is correct.
    const double Cosine = std::clamp(Dot(First.Outward, Second.Outward), -1.0, 1.0);
    return std::acos(Cosine);
}

/// 🧩 The tangent distance a fillet of this radius eats off each leg.
double TangentReach(double Radius, double Radians)
{
    return Radius / std::tan(Radians * 0.5);
}

/// 🧩 The plane the corner lies in, for placing the arc's midpoint.
/// 📝 Taken from the two legs themselves rather than from a workplane, so a corner between two curves
///    that were moved off their original plane still rounds in the plane they now occupy.
bool CornerNormal(const Leg& First, const Leg& Second, SpatialDirection& Normal)
{
    const SpatialDirection Perpendicular = Cross(First.Outward, Second.Outward);
    if (LengthSquared(Perpendicular) <= 1.0e-18)
        return false;
    Normal = Normalize(Perpendicular);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------

double ResolveCornerLimit(const WorldSketchStructure& Declared,
                          WorldCurveName First,
                          WorldCurveName Second)
{
    Leg LeftLeg;
    Leg RightLeg;
    if (!OrientLegs(Declared, First, Second, LeftLeg, RightLeg))
        return 0.0;

    const double Radians = CornerRadians(LeftLeg, RightLeg);
    if (Radians <= CollinearTolerance || Radians >= 3.141592653589793 - CollinearTolerance)
        return 0.0;

    // 📐 Invert the tangent reach: the longest reach that fits is the whole of the shorter leg, so the
    //    largest radius is that reach times tan(theta/2).
    // 🔴 ALL OF THE SHORTER LEG, NOT HALF. The radius the artist may ask for is bounded by the geometry
    //    that is actually there, and a leg is as long as it is. Reserving half of it for some second
    //    corner that may never be drawn refuses radii that fit perfectly well, and refuses them for a
    //    reason the artist cannot see. A corner that later eats an edge another corner needs is caught
    //    when that second corner is evaluated, against the leg length as it stands by then.
    const double Reach = std::min(LeftLeg.Length, RightLeg.Length);
    return Reach * std::tan(Radians * 0.5);
}

CornerVerdict EvaluateWorldCorner(const WorldSketchStructure& Declared,
                                  WorldCurveName First,
                                  WorldCurveName Second,
                                  double Radius)
{
    Leg LeftLeg;
    Leg RightLeg;

    // 📝 Unsupported geometry is reported ahead of a missing junction, because "these are not lines" is
    //    the more useful thing to say about an arc meeting an arc than "these do not meet".
    const DeclaredWorldCurve* HeldFirst  = Declared.Resolve(First);
    const DeclaredWorldCurve* HeldSecond = Declared.Resolve(Second);
    if (HeldFirst != nullptr && HeldSecond != nullptr &&
        HeldFirst->Geometry.Declared() && HeldSecond->Geometry.Declared() &&
        (HeldFirst->Geometry.Subject() != CurveSubject::Line ||
         HeldSecond->Geometry.Subject() != CurveSubject::Line))
        return CornerVerdict::UnsupportedGeometry;

    if (!OrientLegs(Declared, First, Second, LeftLeg, RightLeg))
        return CornerVerdict::NoSharedEndpoint;

    const double Radians = CornerRadians(LeftLeg, RightLeg);
    if (Radians <= CollinearTolerance || Radians >= 3.141592653589793 - CollinearTolerance)
        return CornerVerdict::Collinear;

    if (!(Radius > 0.0))
        return CornerVerdict::RadiusNotPositive;

    // 🔴 THE SAME WHOLE-LEG BOUND `ResolveCornerLimit` REPORTS. If this test were stricter than the
    //    limit the readout shows, the artist would type the number they were offered and be refused.
    const double Reach = TangentReach(Radius, Radians);
    if (Reach > LeftLeg.Length + JunctionTolerance ||
        Reach > RightLeg.Length + JunctionTolerance)
        return CornerVerdict::RadiusBeyondLimit;

    SpatialDirection Normal = {};
    if (!CornerNormal(LeftLeg, RightLeg, Normal))
        return CornerVerdict::Collinear;

    return CornerVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------

CornerVerdict EvaluateWorldCornerShape(const WorldSketchStructure& Declared,
                                       WorldCurveName First,
                                       WorldCurveName Second,
                                       double Radius,
                                       bool Chamfer,
                                       SpatialPoint& EnterPoint,
                                       SpatialPoint& Through,
                                       SpatialPoint& ExitPoint)
{
    EnterPoint = {};
    Through    = {};
    ExitPoint  = {};

    const CornerVerdict Verdict = EvaluateWorldCorner(Declared, First, Second, Radius);
    if (Verdict != CornerVerdict::Produced)
        return Verdict;

    Leg LeftLeg;
    Leg RightLeg;
    if (!OrientLegs(Declared, First, Second, LeftLeg, RightLeg))
        return CornerVerdict::NoSharedEndpoint;

    const double Radians = CornerRadians(LeftLeg, RightLeg);
    const double Reach   = TangentReach(Radius, Radians);

    EnterPoint = Added(LeftLeg.Corner,  Scaled(LeftLeg.Outward,  Reach));
    ExitPoint  = Added(RightLeg.Corner, Scaled(RightLeg.Outward, Reach));

    // 📝 A chamfer is the straight chord, so its midpoint is simply halfway along it. Reporting one keeps
    //    the two manners the same shape of answer and spares every caller a special case.
    if (Chamfer)
    {
        Through = Added(EnterPoint, Scaled(Difference(EnterPoint, ExitPoint), 0.5));
        return CornerVerdict::Produced;
    }

    const SpatialDirection BisectorSpan = Added(LeftLeg.Outward, RightLeg.Outward);
    if (LengthSquared(BisectorSpan) <= 1.0e-18)
        return CornerVerdict::Collinear;

    const SpatialDirection Bisector = Normalize(BisectorSpan);
    const double CentreDistance = Radius / std::sin(Radians * 0.5);
    const SpatialPoint Centre = Added(LeftLeg.Corner, Scaled(Bisector, CentreDistance));
    const SpatialDirection Inward = Normalize(Difference(Centre, LeftLeg.Corner));

    Through = Added(Centre, Scaled(Inward, Radius));
    return CornerVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------

CornerVerdict ApplyWorldCorner(WorldSketchStructure& Declared,
                               WorldCurveName First,
                               WorldCurveName Second,
                               double Radius,
                               bool Chamfer,
                               WorldCurveName& Produced)
{
    Produced = {};

    // 🔴 THE DRY RUN DECIDES. `EvaluateWorldCorner` is the single place the rules live, so the preview
    //    and the commit cannot disagree about whether a radius fits.
    const CornerVerdict Verdict = EvaluateWorldCorner(Declared, First, Second, Radius);
    if (Verdict != CornerVerdict::Produced)
        return Verdict;

    Leg LeftLeg;
    Leg RightLeg;
    if (!OrientLegs(Declared, First, Second, LeftLeg, RightLeg))
        return CornerVerdict::NoSharedEndpoint;

    // 🔴 THE PREVIEW'S OWN ANSWER, so the arc the artist was shown is the arc that gets written. Deriving
    //    the shape twice -- once to draw and once to commit -- is how a preview starts lying.
    SpatialPoint EnterPoint = {};
    SpatialPoint Through    = {};
    SpatialPoint ExitPoint  = {};
    const CornerVerdict Shaped =
        EvaluateWorldCornerShape(Declared, First, Second, Radius, Chamfer, EnterPoint, Through, ExitPoint);
    if (Shaped != CornerVerdict::Produced)
        return Shaped;

    // 📝 The support frame is inherited from the first leg, so the rounded corner belongs to the same
    //    workplane the geometry it joins does.
    const DeclaredWorldCurve* HeldFirst = Declared.Resolve(First);
    const bool FrameStanding = HeldFirst != nullptr && HeldFirst->SupportFrameStanding;
    const WorldPlacementFrame Frame = HeldFirst != nullptr ? HeldFirst->SupportFrame : WorldPlacementFrame{};

    WorldCurveName Joined = {};
    if (Chamfer)
    {
        // 📝 A chamfer is the chord between the same two tangent points. Nothing else differs, which is
        //    why the two operations share every line above this one.
        Joined = FrameStanding ? Declared.DeclareLine(EnterPoint, ExitPoint, Frame)
                               : Declared.DeclareLine(EnterPoint, ExitPoint);
    }
    else
    {
        // 📐 The through-point came from the shared derivation above -- the point on the arc furthest
        //    into the corner, which is what a three-point arc needs.
        Joined = FrameStanding
            ? Declared.DeclareThreePointArc(EnterPoint, Through, ExitPoint, Frame)
            : Declared.DeclareThreePointArc(EnterPoint, Through, ExitPoint);
    }

    if (!Joined.Assigned())
        return CornerVerdict::UnsupportedGeometry;

    // 🔴 THE LEGS ARE SHORTENED ONLY NOW, once the joining curve exists. Writing them first and then
    //    failing to declare the arc would leave a gap in the sketch with nothing bridging it, and the
    //    artist would have no way to tell that from a fillet that simply looked wrong.
    // 🔴 AND THEY ARE SHORTENED, NOT REPLACED. `First` and `Second` keep their names, so every loop
    //    traversal, constraint and selection that already names them stays valid.
    DeclaredWorldCurve* MutableFirst  = Declared.Resolve(First);
    DeclaredWorldCurve* MutableSecond = Declared.Resolve(Second);
    if (MutableFirst == nullptr || MutableSecond == nullptr)
        return CornerVerdict::NoSharedEndpoint;

    LineCurve& FirstLine  = MutableFirst->Geometry.HeldLine();
    LineCurve& SecondLine = MutableSecond->Geometry.HeldLine();
    (LeftLeg.CornerIsOrigin  ? FirstLine.Origin  : FirstLine.Terminus)  = EnterPoint;
    (RightLeg.CornerIsOrigin ? SecondLine.Origin : SecondLine.Terminus) = ExitPoint;

    // 🔴 AND THE NEW CURVE IS SPLICED INTO EVERY LOOP THAT WALKED THE CORNER. Shortening the legs keeps
    //    their names valid, but it leaves a GAP: the loop still runs first-leg then second-leg, and the
    //    two no longer meet. A gapped traversal is not closed, so the analysis stops calling the loop
    //    fillable and the face vanishes -- the shape reads as "the old outline with an arc lying on top"
    //    rather than as a rounded rectangle. The arc has to become part of the walk, between the two
    //    legs it joins, or the loop is no longer a description of the shape on screen.
    for (DeclaredWorldLoop& Walked : Declared.Loops())
    {
        std::vector<WorldCurveUse>& Traversal = Walked.Traversal;
        if (Traversal.size() < 2u)
            continue;

        for (std::size_t Step = 0u; Step < Traversal.size(); ++Step)
        {
            const std::size_t Next = (Step + 1u) % Traversal.size();
            const std::uint32_t Here  = Traversal[Step].TraversedCurve.IssuedIndex;
            const std::uint32_t There = Traversal[Next].TraversedCurve.IssuedIndex;

            // 📝 Either order: the loop may walk the corner first-then-second or the other way about,
            //    and both are the same corner.
            const bool Forward = Here == First.IssuedIndex  && There == Second.IssuedIndex;
            const bool Reverse = Here == Second.IssuedIndex && There == First.IssuedIndex;
            if (!Forward && !Reverse)
                continue;

            // 📝 Inserted AFTER `Step`, which is where the gap now is. The sense follows the walk, so a
            //    loop traversed backwards still reads continuously through the new curve.
            WorldCurveUse Bridging = {};
            Bridging.TraversedCurve = Joined;
            Bridging.SameSense      = Forward;
            Traversal.insert(Traversal.begin() + static_cast<std::ptrdiff_t>(Step) + 1, Bridging);
            break;
        }
    }

    Produced = Joined;
    return CornerVerdict::Produced;
}

//------------------------------------------------------------------------------------------------------------------------

void CollectWorldCorners(const WorldSketchStructure& Declared,
                         std::vector<WorldCornerTarget>& Corners)
{
    Corners.clear();

    const std::size_t Count = Declared.Curves().size();
    for (std::size_t Outer = 0u; Outer < Count; ++Outer)
    {
        for (std::size_t Inner = Outer + 1u; Inner < Count; ++Inner)
        {
            const WorldCurveName FirstName  = { static_cast<std::uint32_t>(Outer + 1u) };
            const WorldCurveName SecondName = { static_cast<std::uint32_t>(Inner + 1u) };

            Leg LeftLeg;
            Leg RightLeg;
            if (!OrientLegs(Declared, FirstName, SecondName, LeftLeg, RightLeg))
                continue;

            const double Radians = CornerRadians(LeftLeg, RightLeg);
            if (Radians <= CollinearTolerance || Radians >= 3.141592653589793 - CollinearTolerance)
                continue;

            WorldCornerTarget Target;
            Target.First    = FirstName;
            Target.Second   = SecondName;
            Target.Position = LeftLeg.Corner;
            Target.Radians  = Radians;
            Target.Limit    = ResolveCornerLimit(Declared, FirstName, SecondName);
            if (Target.Limit > 0.0)
                Corners.push_back(Target);
        }
    }
}

Deliver<WorldCornerTarget> ResolveWorldCornerNear(const WorldSketchStructure& Declared,
                                                  const SpatialPoint& Probe,
                                                  double Reach)
{
    std::vector<WorldCornerTarget> Corners;
    CollectWorldCorners(Declared, Corners);

    const WorldCornerTarget* Nearest = nullptr;
    double NearestDistance = Reach;
    for (const WorldCornerTarget& Held : Corners)
    {
        const double Distance = std::sqrt(LengthSquared(Difference(Probe, Held.Position)));
        if (Distance <= NearestDistance)
        {
            NearestDistance = Distance;
            Nearest = &Held;
        }
    }

    if (Nearest == nullptr)
        return Deliver<WorldCornerTarget>::Refuse({ RefusalReason::ContentUnsupported,
                                                    "no corner lies within reach of the probe" });
    return Deliver<WorldCornerTarget>::Result(*Nearest);
}

} // namespace Slate
