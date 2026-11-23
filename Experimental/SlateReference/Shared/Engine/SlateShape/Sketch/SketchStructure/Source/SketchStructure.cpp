//============================================================================================================================================
//                                                      SKETCHSTRUCTURE.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

namespace
{
    CurveName CurveReferenceOf(SketchCurveName Name)
    {
        return { Name.IssuedIndex };
    }

    constexpr double SlotHalfTurn = 3.141592653589793;

    // 📝 Squared, so a length test never takes a square root it does not need.
    constexpr double SlotDegenerateLengthSquared = 1.0e-12;

    // 📝 Below this the two runs are parallel and the corner is not a corner at all.
    constexpr double SlotCollinearRadians = 1.0e-9;

    // 🔴 A MITRE ON A FOLD-BACK RUNS AWAY TO INFINITY. The inner corner sits at `Radius / cos(Turn/2)`
    //    from the spine vertex, which diverges as the turn approaches a full reversal — a spine that
    //    doubles back on itself would place that corner kilometres away and blow the shape's extent.
    //    Eight radii is the same limit a stroke offset uses, and past it the corner is reported as a
    //    join rather than pretended to be exact.
    constexpr double SlotMitreLimit = 8.0;
}

bool SketchPlane::Declared() const
{
    return LengthSquared(Normal) > 0.0 && LengthSquared(AlongDirection) > 0.0;
}

SketchCurveName SketchStructure::DeclareCurve(const CurveSpecification& Incoming)
{
    HeldCurves.push_back({ Incoming });
    return { static_cast<std::uint32_t>(HeldCurves.size()) };
}

ProfileNameInFeature SketchStructure::DeclareProfile(const ProfileSpecification& Incoming)
{
    HeldProfiles.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldProfiles.size()) };
}

ConstraintName SketchStructure::DeclareConstraint(const ConstraintSpecification& Incoming)
{
    HeldConstraints.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldConstraints.size()) };
}

DimensionName SketchStructure::DeclareDimension(const DimensionSpecification& Incoming)
{
    HeldDimensions.push_back(Incoming);
    return { static_cast<std::uint32_t>(HeldDimensions.size()) };
}

SketchCurveName SketchStructure::DeclareLine(const SpatialPoint& Origin, const SpatialPoint& Terminus)
{
    return DeclareCurve(CurveSpecification::DeclareLine(Origin, Terminus));
}

SketchCurveName SketchStructure::DeclareThreePointArc(const SpatialPoint& StartPoint,
                                                      const SpatialPoint& ThroughPoint,
                                                      const SpatialPoint& EndPoint)
{
    return DeclareCurve(CurveSpecification::DeclareThreePointArc(StartPoint, ThroughPoint, EndPoint));
}

SketchCurveName SketchStructure::DeclareCircle(const CircleCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareCircle(Declared));
}

SketchCurveName SketchStructure::DeclareEllipse(const EllipseCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareEllipse(Declared));
}

SketchCurveName SketchStructure::DeclareOval(const EllipseCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareOval(Declared));
}

SketchCurveName SketchStructure::DeclareBezier(const std::vector<SpatialPoint>& ControlPoints)
{
    return DeclareCurve(CurveSpecification::DeclareBezier(ControlPoints, { 0.0, 1.0 }));
}

SketchCurveName SketchStructure::DeclareBasisSpline(const BasisSplineCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareBasisSpline(Declared, { 0.0, 1.0 }));
}

SketchCurveName SketchStructure::DeclareRationalSpline(const RationalSplineCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareRationalSpline(Declared, { 0.0, 1.0 }));
}

SketchCurveName SketchStructure::DeclareHermite(const HermiteCurve& Declared)
{
    return DeclareCurve(CurveSpecification::DeclareHermite(Declared, { 0.0, 1.0 }));
}

Deliver<bool> SketchStructure::DeclarePolyline(const std::vector<SpatialPoint>& Positions,
                                               std::vector<SketchCurveName>& DeclaredCurves)
{
    DeclaredCurves.clear();
    if (Positions.size() < 2u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a polyline requires at least two positions" });

    DeclaredCurves.reserve(Positions.size() - 1u);
    for (std::size_t PositionIndex = 0u; PositionIndex + 1u < Positions.size(); ++PositionIndex)
    {
        // 🔴 A ZERO-LENGTH SEGMENT IS NOT A LINE, AND ONE OF THEM BLANKED THE WHOLE SKETCH. Closing a
        //    polyline anchors the start point a second time, so the final pair was coincident and
        //    `DeclareLine` produced an UNDECLARED curve. `SketchStructure::Declared()` is all-or-
        //    nothing across every curve, and `ProjectSketchRendering` refuses outright on an
        //    undeclared sketch -- so closing a shape and pressing Enter made every shape already
        //    drawn disappear at once. Coincident neighbours are skipped rather than declared.
        const SpatialDirection Span = Difference(Positions[PositionIndex],
                                                 Positions[PositionIndex + 1u]);
        if (LengthSquared(Span) <= 0.0)
            continue;

        DeclaredCurves.push_back(DeclareLine(Positions[PositionIndex], Positions[PositionIndex + 1u]));
    }

    // ⚠️ Every pair coincident means the artist clicked one spot repeatedly; there is no polyline.
    if (DeclaredCurves.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "a polyline requires two distinct positions" });

    return Deliver<bool>::Result(true);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareCircleProfile(const CircleCurve& Declared)
{
    return DeclareCircleProfile(Declared, Plane);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareCircleProfile(const CircleCurve& Declared,
                                                                     const SketchPlane& ActivePlane)
{
    if (!ActivePlane.Declared())
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the sketch plane is not declared" });
    if (Declared.Radius <= 0.0)
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the circle requires a positive radius" });

    ProfileSpecification Profile;
    Profile.DeclarePlane({ ActivePlane.Origin, ActivePlane.Normal, ActivePlane.AlongDirection });
    ProfileLoop Loop;
    Loop.Orientation = ProfileLoopOrientation::Outer;

    const SpatialDirection StartDirection = Normalize(Declared.StartDirection);
    const SpatialDirection QuarterDirection = Normalize(Cross(Declared.Normal, StartDirection));
    for (std::uint32_t QuarterIndex = 0u; QuarterIndex < 4u; ++QuarterIndex)
    {
        const double StartRadians = 1.5707963267948966 * static_cast<double>(QuarterIndex);
        const SpatialDirection QuarterStart = Added(Scaled(StartDirection, std::cos(StartRadians)),
                                                    Scaled(QuarterDirection, std::sin(StartRadians)));
        // ⚠️ EVERY FIELD, NAMED BY POSITION. `CircularArcCurve` carries a `ThroughPoint` and a
        //    `ThroughDeclared` between the start direction and the radius. A five-element brace list
        //    silently slid the radius into `ThroughPoint` and the sweep into `ThroughDeclared`, leaving
        //    the real radius at zero — so every circle profile in the application collapsed to its own
        //    centre. It still declared, still selected, still appeared in the outliner, and drew nothing.
        CircularArcCurve Quarter = {};
        Quarter.Centre         = Declared.Centre;
        Quarter.Normal         = Declared.Normal;
        Quarter.StartDirection = QuarterStart;
        Quarter.Radius         = Declared.Radius;
        Quarter.SweepRadians   = 1.5707963267948966;

        const SketchCurveName DeclaredCurve = DeclareCurve(
            CurveSpecification::DeclareCircularArc(Quarter, { 0.0, 1.0 }));
        Loop.Traversal.push_back({ CurveReferenceOf(DeclaredCurve), true });
    }

    Profile.DeclareLoop(Loop);
    return Deliver<ProfileNameInFeature>::Result(DeclareProfile(Profile));
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareEllipseProfile(const EllipseCurve& Declared)
{
    return DeclareEllipseProfile(Declared, Plane);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareEllipseProfile(const EllipseCurve& Declared,
                                                                     const SketchPlane& ActivePlane)
{
    if (!ActivePlane.Declared())
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the sketch plane is not declared" });
    if (Declared.MajorRadius <= 0.0 || Declared.MinorRadius <= 0.0)
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the ellipse requires positive axes" });

    ProfileSpecification Profile;
    Profile.DeclarePlane({ ActivePlane.Origin, ActivePlane.Normal, ActivePlane.AlongDirection });
    ProfileLoop Loop;
    Loop.Orientation = ProfileLoopOrientation::Outer;

    for (std::uint32_t QuarterIndex = 0u; QuarterIndex < 4u; ++QuarterIndex)
    {
        const SketchCurveName DeclaredCurve = DeclareCurve(CurveSpecification::DeclareEllipticalArc(
            { Declared.Centre, Declared.Normal, Declared.MajorDirection,
              Declared.MajorRadius, Declared.MinorRadius,
              1.5707963267948966 * static_cast<double>(QuarterIndex),
              1.5707963267948966 },
            { 0.0, 1.0 }));
        Loop.Traversal.push_back({ CurveReferenceOf(DeclaredCurve), true });
    }

    Profile.DeclareLoop(Loop);
    return Deliver<ProfileNameInFeature>::Result(DeclareProfile(Profile));
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareOvalProfile(const EllipseCurve& Declared)
{
    return DeclareEllipseProfile(Declared);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareOvalProfile(const EllipseCurve& Declared,
                                                                   const SketchPlane& ActivePlane)
{
    return DeclareEllipseProfile(Declared, ActivePlane);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareRegularPolygon(const SpatialPoint& Centre,
                                                                     double Radius,
                                                                     std::uint32_t SideCount,
                                                                     const SpatialDirection& StartDirection)
{
    return DeclareRegularPolygon(Centre, Radius, SideCount, Plane, StartDirection);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareRegularPolygon(const SpatialPoint& Centre,
                                                                     double Radius,
                                                                     std::uint32_t SideCount,
                                                                     const SketchPlane& ActivePlane,
                                                                     const SpatialDirection& StartDirection)
{
    if (!ActivePlane.Declared())
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the sketch plane is not declared" });
    if (Radius <= 0.0 || SideCount < 3u)
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the polygon requires positive radius and three sides" });

    // 🔴 THE FIRST CORNER GOES WHERE THE ARTIST DRAGGED. It was pinned to the plane's own
    //    `AlongDirection`, so the committed polygon was rotated away from the one just previewed --
    //    the shape visibly turned on release. An unstated direction still falls back to the plane's
    //    axis, so a polygon declared from a script behaves as it always did.
    const SpatialDirection AlongDirection = LengthSquared(StartDirection) > 1.0e-12
                                          ? Normalize(StartDirection)
                                          : Normalize(ActivePlane.AlongDirection);
    const SpatialDirection AcrossDirection = Normalize(Cross(ActivePlane.Normal, AlongDirection));

    ProfileSpecification Profile;
    Profile.DeclarePlane({ ActivePlane.Origin, ActivePlane.Normal, ActivePlane.AlongDirection });
    ProfileLoop Loop;
    Loop.Orientation = ProfileLoopOrientation::Outer;

    const double StepRadians = 6.283185307179586 / static_cast<double>(SideCount);
    std::vector<SpatialPoint> Corners;
    Corners.reserve(SideCount);
    for (std::uint32_t CornerIndex = 0u; CornerIndex < SideCount; ++CornerIndex)
    {
        const double AngleRadians = StepRadians * static_cast<double>(CornerIndex);
        const SpatialDirection Offset = {
            AlongDirection.Left * Radius * std::cos(AngleRadians) + AcrossDirection.Left * Radius * std::sin(AngleRadians),
            AlongDirection.Up * Radius * std::cos(AngleRadians) + AcrossDirection.Up * Radius * std::sin(AngleRadians),
            AlongDirection.Forward * Radius * std::cos(AngleRadians) + AcrossDirection.Forward * Radius * std::sin(AngleRadians)
        };
        Corners.push_back(Added(Centre, Offset));
    }

    for (std::uint32_t EdgeIndex = 0u; EdgeIndex < SideCount; ++EdgeIndex)
    {
        const std::uint32_t NextIndex = (EdgeIndex + 1u) % SideCount;
        const SketchCurveName DeclaredCurve = DeclareLine(Corners[EdgeIndex], Corners[NextIndex]);
        Loop.Traversal.push_back({ CurveReferenceOf(DeclaredCurve), true });
    }

    Profile.DeclareLoop(Loop);
    return Deliver<ProfileNameInFeature>::Result(DeclareProfile(Profile));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLOT OUTLINE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

double ResolveSpineDistance(const std::vector<SpatialPoint>& Spine,
                            const SpatialPoint& Reference)
{
    if (Spine.empty())
        return 0.0;

    // 📝 The perpendicular foot is clamped into the segment, so a pointer beyond either end measures to
    //    that end — which is what the round cap there actually is.
    double Nearest = std::sqrt(LengthSquared(Difference(Spine.front(), Reference)));

    for (std::size_t Index = 0u; Index + 1u < Spine.size(); ++Index)
    {
        const SpatialDirection Along = Difference(Spine[Index], Spine[Index + 1u]);
        const double           Span  = LengthSquared(Along);
        if (Span <= SlotDegenerateLengthSquared)
            continue;

        const SpatialDirection Reach    = Difference(Spine[Index], Reference);
        const double           Fraction = std::clamp(Dot(Reach, Along) / Span, 0.0, 1.0);
        const SpatialPoint     Foot     = Added(Spine[Index], Scaled(Along, Fraction));

        Nearest = std::min(Nearest, std::sqrt(LengthSquared(Difference(Foot, Reference))));
    }

    return Nearest;
}

namespace
{

/// 🧩 The spine with consecutive duplicates removed, so no segment has zero length.
std::vector<SpatialPoint> ResolveSlotSpine(const std::vector<SpatialPoint>& Spine)
{
    std::vector<SpatialPoint> Distinct;
    Distinct.reserve(Spine.size());

    for (const SpatialPoint& Point : Spine)
        if (Distinct.empty() || LengthSquared(Difference(Distinct.back(), Point)) > SlotDegenerateLengthSquared)
            Distinct.push_back(Point);

    return Distinct;
}

/// 🧩 One side of a slot: the offset run along every segment, joined at each bend.
/// in    Sign  [-]  +1 walks the spine forwards on one side, -1 walks it backwards on the other
/// note  📝 Both sides are the same construction, so writing it once is what keeps the two halves of the
///        outline consistent with each other.
void AppendSlotSide(const std::vector<SpatialPoint>& Spine,
                    double Radius,
                    const SpatialDirection& Normal,
                    double Sign,
                    std::vector<CurveSpecification>& Delivered)
{
    const std::size_t SegmentCount = Spine.size() - 1u;

    // 📝 Where the run being emitted BEGINS. An inner corner moves it onto the mitre, so the trim is
    //    applied to the segment that follows rather than by adding a span between the two.
    SpatialPoint Origin         = {};
    bool         OriginStanding = false;

    for (std::size_t Step = 0u; Step < SegmentCount; ++Step)
    {
        // 📝 `Sign` reverses the walk as well as the side, so the outline stays a single loop.
        const std::size_t Index = (Sign > 0.0) ? Step : (SegmentCount - 1u - Step);

        const SpatialPoint& From = (Sign > 0.0) ? Spine[Index] : Spine[Index + 1u];
        const SpatialPoint& To   = (Sign > 0.0) ? Spine[Index + 1u] : Spine[Index];

        const SpatialDirection Along  = Normalize(Difference(From, To));
        const SpatialDirection Offset = Scaled(Normalize(Cross(Normal, Along)), Radius);

        if (!OriginStanding)
        {
            Origin         = Added(From, Offset);
            OriginStanding = true;
        }

        SpatialPoint Terminus = Added(To, Offset);

        if (Step + 1u >= SegmentCount)
        {
            Delivered.push_back(CurveSpecification::DeclareLine(Origin, Terminus));
            continue;
        }

        // 🔴 THE CORNER. The next run is offset about the same vertex but in a different direction, so
        //    the two offset points differ and something must join them. What joins them depends on
        //    WHICH WAY the spine turns relative to this side.
        const std::size_t NextIndex = (Sign > 0.0) ? (Index + 1u) : (Index - 1u);

        const SpatialPoint& Vertex     = To;
        const SpatialPoint& NextFrom   = (Sign > 0.0) ? Spine[NextIndex] : Spine[NextIndex + 1u];
        const SpatialPoint& NextTo     = (Sign > 0.0) ? Spine[NextIndex + 1u] : Spine[NextIndex];

        const SpatialDirection NextAlong  = Normalize(Difference(NextFrom, NextTo));
        const SpatialDirection NextOffset = Scaled(Normalize(Cross(Normal, NextAlong)), Radius);

        // 📝 Signed about the plane normal, so its sign says which side of the spine bulges.
        const double Turn = std::atan2(Dot(Cross(Along, NextAlong), Normal), Dot(Along, NextAlong));

        if (std::fabs(Turn) < SlotCollinearRadians)
        {
            // 📝 A straight join: the two runs are one run, so no span is closed here at all.
            continue;
        }

        Delivered.push_back(CurveSpecification::DeclareLine(Origin, Terminus));

        // 🔴 THE OUTER SIDE IS AN ARC ABOUT THE VERTEX. Every point of a swept disc's boundary is
        //    exactly `Radius` from the spine, and about a vertex that locus IS a circular arc -- the
        //    same radius and the same centre-to-boundary relation as the end caps. A straight chord
        //    here is the "weird bevel": it cuts the corner off the outside of the bend.
        if (Turn < 0.0)
        {
            CircularArcCurve Fillet = {};
            Fillet.Centre         = Vertex;
            Fillet.Normal         = Normal;
            Fillet.StartDirection = Normalize(Offset);
            Fillet.Radius         = Radius;
            Fillet.SweepRadians   = Turn;
            Delivered.push_back(CurveSpecification::DeclareCircularArc(Fillet, { 0.0, 1.0 }));

            Origin = Added(Vertex, NextOffset);
            continue;
        }

        // 🔴 THE INNER SIDE OVERLAPS AND IS TRIMMED, NOT BRIDGED. Here the two offset runs cross, so
        //    the boundary of the swept region is their intersection -- the mitre point. Joining their
        //    endpoints instead draws a chord across the inside of the bend, which is the edge that was
        //    seen cutting through the slot body.
        const double Cosine = std::cos(0.5 * Turn);

        if (Cosine > 1.0 / SlotMitreLimit)
        {
            const SpatialDirection Bisector = Normalize(Added(Normalize(Offset), Normalize(NextOffset)));
            const SpatialPoint     Mitre    = Added(Vertex, Scaled(Bisector, Radius / Cosine));

            // 📝 Both runs are pulled back onto the mitre rather than a span being inserted between
            //    them, so the inner boundary carries no spur doubling back along itself.
            Delivered.back().HeldLine().Terminus = Mitre;
            Origin = Mitre;
            continue;
        }

        // ⚠️ Past the mitre limit the spine has all but doubled back and the mitre runs away to
        //    infinity. Both runs are pulled onto the vertex itself, which is on the boundary of the
        //    swept region and so cannot escape it.
        Delivered.back().HeldLine().Terminus = Vertex;
        Origin = Vertex;
    }
}

/// 🧩 One end cap: the half turn from one side of the spine to the other, around the end.
/// note  🔴 The sweep is NEGATIVE so the cap goes the LONG way round, over the end it belongs to. A
///        positive sweep turns it towards the other cap instead, carving both semicircles out of the
///        body rather than adding them to its ends. The outline still closes either way, so nothing
///        downstream refuses it; it simply draws as two crescents biting into the slot.
void AppendSlotCap(const SpatialPoint& Centre,
                   const SpatialDirection& Normal,
                   const SpatialDirection& StartDirection,
                   double Radius,
                   std::vector<CurveSpecification>& Delivered)
{
    // ⚠️ The five-versus-seven field slide this once had is why every field is named rather than
    //    written as a brace list; a positional initialiser here silently gave both caps a zero radius.
    CircularArcCurve Cap = {};
    Cap.Centre         = Centre;
    Cap.Normal         = Normal;
    Cap.StartDirection = StartDirection;
    Cap.Radius         = Radius;
    Cap.SweepRadians   = -SlotHalfTurn;

    Delivered.push_back(CurveSpecification::DeclareCircularArc(Cap, { 0.0, 1.0 }));
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SLOT BOUNDARY BY UNION
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THE OFFSET WALK ABOVE DECIDES EVERY CORNER FROM THE TWO SEGMENTS THAT MEET THERE, and that is only
//    ever right when the bend is a LOCAL event. It stops being local in two ways the artist reaches by
//    accident. A turn approaching a full reversal drives the mitre past its limit, so both runs are
//    pulled onto the spine vertex and the inner boundary doubles back through the slot. And a radius
//    larger than the leg beside it makes the offset run overshoot the far vertex entirely, so a segment
//    that is wholly swallowed by its neighbours' discs still contributes an edge — one that lies inside
//    the slot it belongs to. Both draw the bevel that cuts through the body.
//
// 🔴 NO CORNER RULE FIXES THAT, because the defect is not at the corner: it is that the swept region is a
//    UNION and the walk never unions anything. The boundary is built here as the union actually defines
//    it — every offset run and every vertex disc is cut at its crossings, each fragment is kept only if
//    it is still a full radius from the spine, and the survivors are stitched into the loop. A fragment
//    that another part of the slot has swallowed measures closer than the radius and is dropped, which is
//    exactly the test the two cases above fail.

constexpr double SlotFullTurn = 6.283185307179586;

// 📝 Both are relative to the slot radius, so a slot 0.1 across and one 1000 across are judged alike.
constexpr double SlotJunctionTolerance = 1.0e-9;
constexpr double SlotStitchTolerance   = 1.0e-7;

/// 🧩 The sketch plane as an origin and two in-plane axes, so the union is solved in two dimensions.
struct SlotFrame
{
    SpatialPoint     Origin = {};
    SpatialDirection Along  = {};
    SpatialDirection Across = {};
    SpatialDirection Normal = {};
};

PlanarPoint ProjectIntoSlotFrame(const SlotFrame& Frame, const SpatialPoint& Position)
{
    const SpatialDirection Reach = Difference(Frame.Origin, Position);
    return { Dot(Reach, Frame.Along), Dot(Reach, Frame.Across) };
}

SpatialPoint LiftFromSlotFrame(const SlotFrame& Frame, const PlanarPoint& Position)
{
    return Added(Added(Frame.Origin, Scaled(Frame.Along, Position.Along)),
                 Scaled(Frame.Across, Position.Across));
}

PlanarPoint PlanarSum(const PlanarPoint& Left, const PlanarPoint& Right)
{
    return { Left.Along + Right.Along, Left.Across + Right.Across };
}

PlanarPoint PlanarReach(const PlanarPoint& From, const PlanarPoint& To)
{
    return { To.Along - From.Along, To.Across - From.Across };
}

PlanarPoint PlanarScaledBy(const PlanarPoint& Subject, double Amount)
{
    return { Subject.Along * Amount, Subject.Across * Amount };
}

double PlanarDot(const PlanarPoint& Left, const PlanarPoint& Right)
{
    return Left.Along * Right.Along + Left.Across * Right.Across;
}

double PlanarCross(const PlanarPoint& Left, const PlanarPoint& Right)
{
    return Left.Along * Right.Across - Left.Across * Right.Along;
}

double PlanarLengthSquared(const PlanarPoint& Subject)
{
    return PlanarDot(Subject, Subject);
}

double PlanarSpan(const PlanarPoint& From, const PlanarPoint& To)
{
    return std::sqrt(PlanarLengthSquared(PlanarReach(From, To)));
}

/// 📝 A quarter turn about the plane normal — what `Cross(Normal, Along)` reduces to inside the plane.
PlanarPoint PlanarQuarterTurn(const PlanarPoint& Subject)
{
    return { -Subject.Across, Subject.Along };
}

PlanarPoint PlanarUnit(const PlanarPoint& Subject)
{
    const double Length = std::sqrt(PlanarLengthSquared(Subject));
    if (!(Length > 0.0))
        return {};
    return { Subject.Along / Length, Subject.Across / Length };
}

PlanarPoint PointOnSlotCircle(const PlanarPoint& Centre, double Radius, double Turn)
{
    return { Centre.Along + Radius * std::cos(Turn), Centre.Across + Radius * std::sin(Turn) };
}

double NormalizedTurn(double Radians)
{
    double Wrapped = std::fmod(Radians, SlotFullTurn);
    if (Wrapped < 0.0)
        Wrapped += SlotFullTurn;
    return Wrapped;
}

/// 🧩 The in-plane twin of `ResolveSpineDistance` — the perpendicular reach to the nearest spine segment.
double ResolvePlanarSpineDistance(const std::vector<PlanarPoint>& Spine, const PlanarPoint& Reference)
{
    if (Spine.empty())
        return 0.0;

    double Nearest = PlanarSpan(Spine.front(), Reference);

    for (std::size_t Index = 0u; Index + 1u < Spine.size(); ++Index)
    {
        const PlanarPoint Along = PlanarReach(Spine[Index], Spine[Index + 1u]);
        const double      Span  = PlanarLengthSquared(Along);
        if (Span <= SlotDegenerateLengthSquared)
            continue;

        const PlanarPoint Reach    = PlanarReach(Spine[Index], Reference);
        const double      Fraction = std::clamp(PlanarDot(Reach, Along) / Span, 0.0, 1.0);
        const PlanarPoint Foot     = PlanarSum(Spine[Index], PlanarScaledBy(Along, Fraction));

        Nearest = std::min(Nearest, PlanarSpan(Foot, Reference));
    }

    return Nearest;
}

/// 🧩 One whole offset run or one whole vertex disc, with the parameters it must be cut at.
struct SlotCandidate
{
    bool                Arc    = false;
    PlanarPoint         Start  = {};
    PlanarPoint         End    = {};
    PlanarPoint         Centre = {};
    std::vector<double> Splits = {};
};

/// 🧩 A surviving fragment of the boundary: a straight run, or a turn about a vertex.
struct SlotOutlineFragment
{
    bool        Arc        = false;
    PlanarPoint Start      = {};
    PlanarPoint End        = {};
    PlanarPoint Centre     = {};
    double      StartTurn  = 0.0;
    double      SweepTurn  = 0.0;
};

void RecordLineSplit(SlotCandidate& Line, double Fraction)
{
    if (Fraction > SlotJunctionTolerance && Fraction < 1.0 - SlotJunctionTolerance)
        Line.Splits.push_back(Fraction);
}

void RecordArcSplit(SlotCandidate& Arc, const PlanarPoint& At)
{
    Arc.Splits.push_back(NormalizedTurn(std::atan2(At.Across - Arc.Centre.Across,
                                                   At.Along  - Arc.Centre.Along)));
}

void IntersectSlotLines(SlotCandidate& First, SlotCandidate& Second)
{
    const PlanarPoint FirstReach  = PlanarReach(First.Start, First.End);
    const PlanarPoint SecondReach = PlanarReach(Second.Start, Second.End);

    const double Scale       = std::sqrt(PlanarLengthSquared(FirstReach) * PlanarLengthSquared(SecondReach));
    const double Denominator = PlanarCross(FirstReach, SecondReach);
    if (!(Scale > 0.0) || std::fabs(Denominator) <= Scale * SlotJunctionTolerance)
        return;

    const PlanarPoint Between       = PlanarReach(First.Start, Second.Start);
    const double      FirstFraction  = PlanarCross(Between, SecondReach) / Denominator;
    const double      SecondFraction = PlanarCross(Between, FirstReach) / Denominator;

    if (FirstFraction  < -SlotJunctionTolerance || FirstFraction  > 1.0 + SlotJunctionTolerance)
        return;
    if (SecondFraction < -SlotJunctionTolerance || SecondFraction > 1.0 + SlotJunctionTolerance)
        return;

    RecordLineSplit(First, FirstFraction);
    RecordLineSplit(Second, SecondFraction);
}

void IntersectSlotLineWithCircle(SlotCandidate& Line, SlotCandidate& Circle, double Radius)
{
    const PlanarPoint Reach  = PlanarReach(Line.Start, Line.End);
    const PlanarPoint Offset = PlanarReach(Circle.Centre, Line.Start);

    const double Quadratic = PlanarLengthSquared(Reach);
    if (!(Quadratic > 0.0))
        return;

    const double Linear       = 2.0 * PlanarDot(Reach, Offset);
    const double Constant     = PlanarLengthSquared(Offset) - Radius * Radius;
    const double Discriminant = Linear * Linear - 4.0 * Quadratic * Constant;

    // 🔴 A GRAZING CONTACT IS THE ORDINARY CASE HERE, NOT A RARE ONE. Every offset run is parallel to
    //    its segment at exactly the slot radius, so it is TANGENT to the disc at each end of that
    //    segment — and a tangent puts this discriminant on zero, where rounding leaves it at ±1e-16 at
    //    random. Refusing every negative value therefore dropped the cut at a tangency about half the
    //    time, leaving the disc uncut where a run meets it and the loop unable to close.
    const double Grazing = 4.0 * Quadratic * Radius * Radius * SlotJunctionTolerance;
    if (Discriminant < -Grazing)
        return;

    const double Root = std::sqrt(std::max(Discriminant, 0.0));

    for (const double Fraction : { (-Linear - Root) / (2.0 * Quadratic),
                                   (-Linear + Root) / (2.0 * Quadratic) })
    {
        if (Fraction < -SlotJunctionTolerance || Fraction > 1.0 + SlotJunctionTolerance)
            continue;

        RecordLineSplit(Line, Fraction);
        RecordArcSplit(Circle, PlanarSum(Line.Start, PlanarScaledBy(Reach, Fraction)));
    }
}

void IntersectSlotCircles(SlotCandidate& First, SlotCandidate& Second, double Radius)
{
    const PlanarPoint Between     = PlanarReach(First.Centre, Second.Centre);
    const double      SpanSquared = PlanarLengthSquared(Between);
    if (SpanSquared <= SlotDegenerateLengthSquared)
        return;

    const double Span = std::sqrt(SpanSquared);
    if (Span >= 2.0 * Radius)
        return;

    const double      Half     = Span * 0.5;
    const double      Height   = std::sqrt(std::max(Radius * Radius - Half * Half, 0.0));
    const PlanarPoint Middle   = PlanarSum(First.Centre, PlanarScaledBy(Between, 0.5));
    const PlanarPoint Sideways = PlanarScaledBy(PlanarQuarterTurn(PlanarUnit(Between)), Height);

    for (const PlanarPoint& At : { PlanarSum(Middle, Sideways),
                                   PlanarSum(Middle, PlanarScaledBy(Sideways, -1.0)) })
    {
        RecordArcSplit(First, At);
        RecordArcSplit(Second, At);
    }
}

void AppendCandidateFragments(const SlotCandidate& Candidate,
                              double Radius,
                              std::vector<SlotOutlineFragment>& Pieces)
{
    std::vector<double> Ordered = Candidate.Splits;
    std::sort(Ordered.begin(), Ordered.end());

    if (!Candidate.Arc)
    {
        std::vector<double> Bounds = { 0.0 };
        for (const double Value : Ordered)
            if (Value > Bounds.back() + SlotJunctionTolerance && Value < 1.0 - SlotJunctionTolerance)
                Bounds.push_back(Value);
        Bounds.push_back(1.0);

        const PlanarPoint Reach = PlanarReach(Candidate.Start, Candidate.End);
        for (std::size_t Index = 0u; Index + 1u < Bounds.size(); ++Index)
        {
            SlotOutlineFragment Piece;
            Piece.Start = PlanarSum(Candidate.Start, PlanarScaledBy(Reach, Bounds[Index]));
            Piece.End   = PlanarSum(Candidate.Start, PlanarScaledBy(Reach, Bounds[Index + 1u]));
            Pieces.push_back(Piece);
        }
        return;
    }

    std::vector<double> Bounds;
    for (const double Value : Ordered)
        if (Bounds.empty() || Value > Bounds.back() + SlotJunctionTolerance)
            Bounds.push_back(Value);
    if (Bounds.size() >= 2u && Bounds.front() + SlotFullTurn - Bounds.back() <= SlotJunctionTolerance)
        Bounds.pop_back();

    // 📝 A disc nothing reaches is a whole circle, which is the slot of a spine that never left its
    //    first point; it is emitted as one closed turn rather than dropped.
    if (Bounds.empty())
    {
        SlotOutlineFragment Piece;
        Piece.Arc       = true;
        Piece.Centre    = Candidate.Centre;
        Piece.StartTurn = 0.0;
        Piece.SweepTurn = SlotFullTurn;
        Piece.Start     = PointOnSlotCircle(Candidate.Centre, Radius, 0.0);
        Piece.End       = Piece.Start;
        Pieces.push_back(Piece);
        return;
    }

    for (std::size_t Index = 0u; Index < Bounds.size(); ++Index)
    {
        const double From = Bounds[Index];
        const double To   = (Index + 1u < Bounds.size()) ? Bounds[Index + 1u]
                                                         : Bounds.front() + SlotFullTurn;
        if (To - From <= SlotJunctionTolerance)
            continue;

        SlotOutlineFragment Piece;
        Piece.Arc       = true;
        Piece.Centre    = Candidate.Centre;
        Piece.StartTurn = From;
        Piece.SweepTurn = To - From;
        Piece.Start     = PointOnSlotCircle(Candidate.Centre, Radius, From);
        Piece.End       = PointOnSlotCircle(Candidate.Centre, Radius, To);
        Pieces.push_back(Piece);
    }
}

PlanarPoint ResolveFragmentMidpoint(const SlotOutlineFragment& Piece, double Radius)
{
    if (!Piece.Arc)
        return PlanarSum(Piece.Start, PlanarScaledBy(PlanarReach(Piece.Start, Piece.End), 0.5));
    return PointOnSlotCircle(Piece.Centre, Radius, Piece.StartTurn + Piece.SweepTurn * 0.5);
}

SlotOutlineFragment ReversedFragment(const SlotOutlineFragment& Piece)
{
    SlotOutlineFragment Turned = Piece;
    Turned.Start = Piece.End;
    Turned.End   = Piece.Start;
    if (Piece.Arc)
    {
        Turned.StartTurn = NormalizedTurn(Piece.StartTurn + Piece.SweepTurn);
        Turned.SweepTurn = -Piece.SweepTurn;
    }
    return Turned;
}

/// 🧩 The direction of travel as a fragment leaves its start, and as it arrives at its end.
PlanarPoint ResolveFragmentDeparture(const SlotOutlineFragment& Piece)
{
    if (!Piece.Arc)
        return PlanarUnit(PlanarReach(Piece.Start, Piece.End));

    const double Facing = (Piece.SweepTurn >= 0.0) ? 1.0 : -1.0;
    return { -std::sin(Piece.StartTurn) * Facing, std::cos(Piece.StartTurn) * Facing };
}

PlanarPoint ResolveFragmentArrival(const SlotOutlineFragment& Piece)
{
    if (!Piece.Arc)
        return PlanarUnit(PlanarReach(Piece.Start, Piece.End));

    const double Facing = (Piece.SweepTurn >= 0.0) ? 1.0 : -1.0;
    const double Finish = Piece.StartTurn + Piece.SweepTurn;
    return { -std::sin(Finish) * Facing, std::cos(Finish) * Facing };
}

/// 🧩 Walks the surviving fragments into closed loops.
/// note  🔴 MORE THAN TWO FRAGMENTS MEET AT A PINCH POINT, and a walk that simply took the nearest
///        unused end could not tell them apart. Where a spine crosses itself four boundary fragments
///        share one point, so the nearest-end rule paired them at random: two of the four joined into
///        a short circuit and the rest were stranded as an open chain, which read as "this does not
///        close" and threw the whole union away. The turn is what distinguishes them — the boundary
///        always continues along the sharpest RIGHT turn available, which is the branch that hugs the
///        region rather than cutting across it.
bool TraceSlotLoops(const std::vector<SlotOutlineFragment>& Pieces,
                    double Radius,
                    double Reach,
                    std::vector<std::vector<SlotOutlineFragment>>& Loops)
{
    // 🔴 THE TOLERANCE HAS TO FOLLOW THE COORDINATES, NOT ONLY THE RADIUS. A junction found by the
    //    tangent branch above resolves to about √ε of the numbers it was computed from, so on a spine
    //    240 long the two fragments meeting at one point can land 1.7e-6 apart while a tolerance of
    //    radius × 1e-7 admitted only 4e-7. The ends were genuinely the same point and the walk called
    //    them different, so a slot that was perfectly well formed fell back to the offset walk purely
    //    because it had been drawn far from the origin.
    const double      Tolerance = std::max(Radius, Reach) * SlotStitchTolerance;
    std::vector<bool> Taken(Pieces.size(), false);

    for (std::size_t Seed = 0u; Seed < Pieces.size(); ++Seed)
    {
        if (Taken[Seed])
            continue;

        Taken[Seed] = true;
        std::vector<SlotOutlineFragment> Loop = { Pieces[Seed] };

        const PlanarPoint Head    = Pieces[Seed].Start;
        PlanarPoint       Tail    = Pieces[Seed].End;
        PlanarPoint       Arrival = ResolveFragmentArrival(Pieces[Seed]);

        while (PlanarSpan(Tail, Head) > Tolerance)
        {
            std::size_t Best     = Pieces.size();
            double      Sharpest = 0.0;

            for (std::size_t Index = 0u; Index < Pieces.size(); ++Index)
            {
                if (Taken[Index] || PlanarSpan(Tail, Pieces[Index].Start) > Tolerance)
                    continue;

                // 📝 Measured from the direction already being travelled, so a straight-on
                //    continuation scores zero and a hard right scores least.
                const PlanarPoint Departure = ResolveFragmentDeparture(Pieces[Index]);
                const double      Turn      = std::atan2(PlanarCross(Arrival, Departure),
                                                         PlanarDot(Arrival, Departure));

                if (Best >= Pieces.size() || Turn < Sharpest)
                {
                    Sharpest = Turn;
                    Best     = Index;
                }
            }

            // ⚠️ An open chain means the fragments do not describe a closed region, which the caller
            //    answers by keeping the offset walk rather than emitting a boundary with a hole in it.
            if (Best >= Pieces.size())
                return false;

            Taken[Best] = true;
            Loop.push_back(Pieces[Best]);
            Tail    = Loop.back().End;
            Arrival = ResolveFragmentArrival(Loop.back());
        }

        Loops.push_back(Loop);
    }

    return !Loops.empty();
}

double ResolveLoopArea(const std::vector<SlotOutlineFragment>& Loop, double Radius)
{
    std::vector<PlanarPoint> Outline;

    for (const SlotOutlineFragment& Piece : Loop)
    {
        if (!Piece.Arc)
        {
            Outline.push_back(Piece.Start);
            continue;
        }

        constexpr std::size_t Steps = 24u;
        for (std::size_t Step = 0u; Step < Steps; ++Step)
            Outline.push_back(PointOnSlotCircle(Piece.Centre, Radius,
                Piece.StartTurn + Piece.SweepTurn * (static_cast<double>(Step) / static_cast<double>(Steps))));
    }

    double Twice = 0.0;
    for (std::size_t Index = 0u; Index < Outline.size(); ++Index)
        Twice += PlanarCross(Outline[Index], Outline[(Index + 1u) % Outline.size()]);

    return 0.5 * Twice;
}

std::vector<SlotOutlineFragment> ReversedLoop(const std::vector<SlotOutlineFragment>& Loop)
{
    std::vector<SlotOutlineFragment> Turned;
    Turned.reserve(Loop.size());
    for (std::size_t Index = Loop.size(); Index > 0u; --Index)
        Turned.push_back(ReversedFragment(Loop[Index - 1u]));
    return Turned;
}

/// 🧩 Rejoins fragments that were only ever cut apart by a grazing contact, so the loop carries the
///    fewest spans that describe it — one line per straight run, one arc per turn.
void MergeLoopFragments(std::vector<SlotOutlineFragment>& Loop, double Radius, double Reach)
{
    const double Tolerance = std::max(Radius, Reach) * SlotStitchTolerance;

    for (bool Merged = true; Merged && Loop.size() > 1u; )
    {
        Merged = false;

        for (std::size_t Index = 0u; Index < Loop.size(); ++Index)
        {
            const std::size_t Next = (Index + 1u) % Loop.size();
            if (Next == Index)
                break;

            SlotOutlineFragment&       Current   = Loop[Index];
            const SlotOutlineFragment& Following = Loop[Next];

            if (Current.Arc != Following.Arc)
                continue;

            if (!Current.Arc)
            {
                const PlanarPoint First  = PlanarReach(Current.Start, Current.End);
                const PlanarPoint Second = PlanarReach(Following.Start, Following.End);
                const double      Scale  = std::sqrt(PlanarLengthSquared(First) * PlanarLengthSquared(Second));

                if (!(Scale > 0.0) || std::fabs(PlanarCross(First, Second)) > Scale * SlotJunctionTolerance)
                    continue;
                if (PlanarDot(First, Second) <= 0.0)
                    continue;

                Current.End = Following.End;
            }
            else
            {
                if (PlanarSpan(Current.Centre, Following.Centre) > Tolerance)
                    continue;
                if (Current.SweepTurn * Following.SweepTurn <= 0.0)
                    continue;
                if (std::fabs(Current.SweepTurn + Following.SweepTurn) >= SlotFullTurn)
                    continue;

                Current.SweepTurn += Following.SweepTurn;
                Current.End        = Following.End;
            }

            Loop.erase(Loop.begin() + static_cast<std::ptrdiff_t>(Next));
            Merged = true;
            break;
        }
    }
}

/// 🧩 Builds the slot boundary as the union of its offset runs and vertex discs.
/// out   Delivered  [-]  the closed outline, appended in traversal order
/// err   returns false and appends nothing when the fragments do not close
bool AppendSlotUnionOutline(const std::vector<SpatialPoint>& Run,
                            double Radius,
                            const SpatialDirection& PlaneNormal,
                            std::vector<CurveSpecification>& Delivered)
{
    SlotFrame Frame;
    Frame.Origin = Run.front();
    Frame.Normal = PlaneNormal;
    Frame.Along  = Normalize(Difference(Run.front(), Run[1]));
    Frame.Across = Normalize(Cross(PlaneNormal, Frame.Along));

    std::vector<PlanarPoint> Spine;
    Spine.reserve(Run.size());
    for (const SpatialPoint& Point : Run)
        Spine.push_back(ProjectIntoSlotFrame(Frame, Point));

    // 📝 How far the construction reaches from its own origin, which is what its rounding scales with.
    double Reach = Radius;
    for (const PlanarPoint& Point : Spine)
        Reach = std::max(Reach, std::sqrt(PlanarLengthSquared(Point)) + Radius);

    // ① Every offset run and every vertex disc, before any of them is cut.
    std::vector<SlotCandidate> Candidates;
    for (std::size_t Index = 0u; Index + 1u < Spine.size(); ++Index)
    {
        const PlanarPoint Along    = PlanarUnit(PlanarReach(Spine[Index], Spine[Index + 1u]));
        const PlanarPoint Sideways = PlanarScaledBy(PlanarQuarterTurn(Along), Radius);

        // 🔴 EACH RUN IS LAID DOWN ALREADY FACING THE WAY THE BOUNDARY TRAVELS, so the walk never has to
        //    choose an orientation for it. The whole outline is traversed with the slot on the LEFT, and
        //    for a run offset to one side that fixes its direction: the far side runs back against the
        //    segment, the near side runs with it. Leaving them unoriented let the walk consume a run
        //    backwards at a pinch point, which stranded the fragments that needed it the other way.
        SlotCandidate Far;
        Far.Start = PlanarSum(Spine[Index + 1u], Sideways);
        Far.End   = PlanarSum(Spine[Index],      Sideways);
        Candidates.push_back(Far);

        SlotCandidate Near;
        Near.Start = PlanarSum(Spine[Index],      PlanarScaledBy(Sideways, -1.0));
        Near.End   = PlanarSum(Spine[Index + 1u], PlanarScaledBy(Sideways, -1.0));
        Candidates.push_back(Near);
    }
    for (const PlanarPoint& Vertex : Spine)
    {
        SlotCandidate Disc;
        Disc.Arc    = true;
        Disc.Centre = Vertex;
        Candidates.push_back(Disc);
    }

    // ② Cut each one everywhere another crosses it.
    for (std::size_t First = 0u; First < Candidates.size(); ++First)
        for (std::size_t Second = First + 1u; Second < Candidates.size(); ++Second)
        {
            if (!Candidates[First].Arc && !Candidates[Second].Arc)
                IntersectSlotLines(Candidates[First], Candidates[Second]);
            else if (Candidates[First].Arc && Candidates[Second].Arc)
                IntersectSlotCircles(Candidates[First], Candidates[Second], Radius);
            else if (Candidates[First].Arc)
                IntersectSlotLineWithCircle(Candidates[Second], Candidates[First], Radius);
            else
                IntersectSlotLineWithCircle(Candidates[First], Candidates[Second], Radius);
        }

    // ③ Keep only the fragments the swept disc has not swallowed. A fragment inside the slot measures
    //    nearer to the spine than the radius, which is precisely what a corner cutting through the body
    //    does; the offset walk had no such test and so drew it.
    const double InsideLimit = Radius * (1.0 - SlotJunctionTolerance);

    std::vector<SlotOutlineFragment> Fragments;
    for (const SlotCandidate& Candidate : Candidates)
    {
        std::vector<SlotOutlineFragment> Cut;
        AppendCandidateFragments(Candidate, Radius, Cut);

        // 📝 A run cut at two coincident crossings leaves a fragment of no length, whose direction of
        //    travel is undefined; it is not part of the boundary and would only give the walk a turn it
        //    cannot measure.
        for (const SlotOutlineFragment& Piece : Cut)
        {
            if (!Piece.Arc && PlanarSpan(Piece.Start, Piece.End) <= Reach * SlotStitchTolerance)
                continue;
            if (ResolvePlanarSpineDistance(Spine, ResolveFragmentMidpoint(Piece, Radius)) >= InsideLimit)
                Fragments.push_back(Piece);
        }
    }

    if (Fragments.empty())
        return false;

    // ④ Stitch, then take the enclosing loop. A spine that crosses itself encircles holes as well, and
    //    the profile this feeds carries one outer loop.
    std::vector<std::vector<SlotOutlineFragment>> Loops;
    if (!TraceSlotLoops(Fragments, Radius, Reach, Loops))
        return false;

    std::size_t Widest = 0u;
    double      Extent = std::fabs(ResolveLoopArea(Loops.front(), Radius));
    for (std::size_t Index = 1u; Index < Loops.size(); ++Index)
    {
        const double Measured = std::fabs(ResolveLoopArea(Loops[Index], Radius));
        if (Measured > Extent)
        {
            Extent = Measured;
            Widest = Index;
        }
    }

    std::vector<SlotOutlineFragment> Outer = Loops[Widest];
    if (ResolveLoopArea(Outer, Radius) < 0.0)
        Outer = ReversedLoop(Outer);

    MergeLoopFragments(Outer, Radius, Reach);

    for (const SlotOutlineFragment& Piece : Outer)
    {
        if (!Piece.Arc)
        {
            Delivered.push_back(CurveSpecification::DeclareLine(LiftFromSlotFrame(Frame, Piece.Start),
                                                                LiftFromSlotFrame(Frame, Piece.End)));
            continue;
        }

        CircularArcCurve Arc = {};
        Arc.Centre         = LiftFromSlotFrame(Frame, Piece.Centre);
        Arc.Normal         = Frame.Normal;
        Arc.StartDirection = Added(Scaled(Frame.Along,  std::cos(Piece.StartTurn)),
                                   Scaled(Frame.Across, std::sin(Piece.StartTurn)));
        Arc.Radius         = Radius;
        Arc.SweepRadians   = Piece.SweepTurn;
        Delivered.push_back(CurveSpecification::DeclareCircularArc(Arc, { 0.0, 1.0 }));
    }

    return true;
}

}   // namespace

void AppendSlotOutline(const std::vector<SpatialPoint>& Spine,
                       double Radius,
                       const SpatialDirection& Normal,
                       std::vector<CurveSpecification>& Delivered)
{
    const std::vector<SpatialPoint> Run = ResolveSlotSpine(Spine);
    if (Run.size() < 2u || !(Radius > 0.0) || !(LengthSquared(Normal) > 0.0))
        return;

    const SpatialDirection PlaneNormal = Normalize(Normal);

    // 📝 The union is the definition, so it is what is asked first. The offset walk below remains as the
    //    answer of last resort: it is wrong only where the swept region overlaps itself, and it always
    //    closes, so a boundary that failed to stitch is better served by it than by nothing.
    const std::size_t Standing = Delivered.size();
    if (AppendSlotUnionOutline(Run, Radius, PlaneNormal, Delivered))
        return;

    Delivered.resize(Standing);

    const SpatialDirection FirstAlong = Normalize(Difference(Run.front(), Run[1]));
    const SpatialDirection LastAlong  = Normalize(Difference(Run[Run.size() - 2u], Run.back()));
    const SpatialDirection FirstSide  = Normalize(Cross(PlaneNormal, FirstAlong));
    const SpatialDirection LastSide   = Normalize(Cross(PlaneNormal, LastAlong));

    AppendSlotSide(Run, Radius, PlaneNormal, 1.0, Delivered);
    AppendSlotCap(Run.back(), PlaneNormal, LastSide, Radius, Delivered);
    AppendSlotSide(Run, Radius, PlaneNormal, -1.0, Delivered);
    AppendSlotCap(Run.front(), PlaneNormal, Negated(FirstSide), Radius, Delivered);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareSlot(const SpatialPoint& StartPoint,
                                                           const SpatialPoint& EndPoint,
                                                           double Radius)
{
    return DeclareSlot(StartPoint, EndPoint, Radius, Plane);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclareSlot(const SpatialPoint& StartPoint,
                                                           const SpatialPoint& EndPoint,
                                                           double Radius,
                                                           const SketchPlane& ActivePlane)
{
    // 📝 A two-point slot is a one-segment spine, so it is declared through the SAME outline every
    //    longer spine uses rather than through a second construction that has to be kept in step.
    return DeclarePolylineSlot({ StartPoint, EndPoint }, Radius, ActivePlane);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclarePolylineSlot(const std::vector<SpatialPoint>& Spine,
                                                                   double Radius)
{
    return DeclarePolylineSlot(Spine, Radius, Plane);
}

Deliver<ProfileNameInFeature> SketchStructure::DeclarePolylineSlot(const std::vector<SpatialPoint>& Spine,
                                                                   double Radius,
                                                                   const SketchPlane& ActivePlane)
{
    if (!ActivePlane.Declared())
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the sketch plane is not declared" });
    if (Radius <= 0.0)
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the slot requires a positive radius" });
    if (Spine.size() < 2u)
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the slot requires at least two points" });

    // 🔴 THE OUTLINE IS BUILT BY THE SAME CALL THE PREVIEW USES. It was built here a second time, and
    //    the two constructions drifted in exactly the way that guarantees: both joined consecutive
    //    offset runs with a straight chord, so every bend in a spine drew a bevel that cut through
    //    the slot's own body instead of the semicircle a swept disc actually traces.
    std::vector<CurveSpecification> Outline;
    AppendSlotOutline(Spine, Radius, ActivePlane.Normal, Outline);

    if (Outline.empty())
        return Deliver<ProfileNameInFeature>::Refuse({ RefusalReason::ContentUnsupported, "the slot spine points are degenerate" });

    ProfileSpecification Profile;
    Profile.DeclarePlane({ ActivePlane.Origin, ActivePlane.Normal, ActivePlane.AlongDirection });

    ProfileLoop Loop;
    Loop.Orientation = ProfileLoopOrientation::Outer;

    // 📝 The outline already arrives in traversal order, so the loop is the curves in the order they
    //    were delivered and the profile needs no second opinion about how they join.
    for (const CurveSpecification& Span : Outline)
        Loop.Traversal.push_back({ CurveReferenceOf(DeclareCurve(Span)), true });

    Profile.DeclareLoop(Loop);
    return Deliver<ProfileNameInFeature>::Result(DeclareProfile(Profile));
}

bool SketchStructure::Declared() const
{
    if (!PlaneStanding || !Plane.Declared())
        return false;

    for (const DeclaredSketchCurve& Curve : HeldCurves)
    {
        // 🔴 A RETIRED CURVE IS EMPTY ON PURPOSE, mirrored from a world-model Trim. Its cleared geometry
        //    is the intended state, not a malformed one, so it does not fail the sketch -- exactly as the
        //    world model treats its own retired curves.
        if (Curve.Retired)
            continue;
        if (!Curve.Geometry.Declared())
            return false;
    }

    for (const ProfileSpecification& Profile : HeldProfiles)
        if (!Profile.Declared())
            return false;

    for (const ConstraintSpecification& Constraint : HeldConstraints)
        if (!Constraint.Declared())
            return false;

    for (const DimensionSpecification& Dimension : HeldDimensions)
        if (!Dimension.Declared())
            return false;

    return true;
}

void SketchStructure::Reclaim()
{
    Plane = {};
    PlaneStanding = false;
    HeldCurves.clear();
    HeldProfiles.clear();
    HeldConstraints.clear();
    HeldDimensions.clear();
}

} // namespace Slate
