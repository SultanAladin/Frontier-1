//============================================================================================================================================
//                                                         SKETCHPLACEMENT.CPP
//============================================================================================================================================

#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       DECLARATION
//------------------------------------------------------------------------------------------------------------------------

void SketchPlacement::Declare(SketchSubject Subject, PlacementMethod Method, bool Construction)
{
    // 🔴 A pair no geometry call can honour stands the tool down rather than holding a tool that can never
    //    complete. The retired table shipped exactly that: `DiameterCircle` sat in a branch storing one
    //    anchor while its commit required two, so the tool was held, consumed every press, and produced
    //    nothing for as long as the artist kept clicking.
    if (!AcceptedBy(Subject, Method))
    {
        Abandon();
        return;
    }

    // 🔴 Declaring what is already held keeps the anchors. The caller states the held tool every tick from
    //    whatever the artist last pressed, so restarting here would discard the first anchor of every
    //    two-anchor placement on the tick after it was taken.
    if (Subject == Placing && Method == PlacingMethod)
    {
        ConstructionDeclared = Construction;
        return;
    }

    Abandon();
    Placing              = Subject;
    PlacingMethod        = Method;
    ConstructionDeclared = Construction;
}

void SketchPlacement::Abandon()
{
    Placing               = SketchSubject::None;
    PlacingMethod         = PlacementMethod::Extent;
    ConstructionDeclared  = false;
    HoverTaken            = false;
    HoverAt               = {};
    HoverSnap             = {};
    SpineFinishedDeclared = false;

    // 📝 `clear` rather than assigning `{}`: the placement is reused every time the artist draws another
    //    shape with the same tool, so keeping the reserved extent means the common case allocates once.
    Taken.clear();
    TakenPlacements.clear();

    // 📝 The side count is a property of the TOOL, not of one placement: an artist who sets an octagon
    //    expects the next octagon to be an octagon too. It is reset only when the tool itself changes.
}

bool SketchPlacement::Resolve(float Notches)
{
    // 🔴 Only a polygon has a resolution, so every other tool leaves the wheel to the camera. Returning
    //    false is what lets the host keep zooming while a line is being drawn.
    if (Placing != SketchSubject::Polygon)
        return false;

    const int Travel = static_cast<int>(Notches > 0.0f ? std::floor(Notches + 0.5f)
                                                       : std::ceil(Notches - 0.5f));
    if (Travel == 0)
        return false;

    const long long Wanted = static_cast<long long>(SideCount) + Travel;
    SideCount = static_cast<std::uint32_t>(std::clamp<long long>(Wanted,
                                                                 PolygonSideMinimum,
                                                                 PolygonSideMaximum));
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE POINTER
//------------------------------------------------------------------------------------------------------------------------

void SketchPlacement::Hover(const SpatialPoint& Position, const SketchSnapPlacement& Placement)
{
    if (!Standing())
        return;

    HoverAt    = Position;
    HoverSnap  = Placement;
    HoverTaken = true;
}

PlacementArrival SketchPlacement::Anchor(bool Terminating)
{
    // 🔴 An anchor is taken at the hover, so a caller that has not stated one this tick has nothing to
    //    anchor. Reporting `Ignored` leaves the contact unconsumed rather than placing an anchor at the
    //    stale position of the previous tick.
    if (!Standing() || !HoverTaken)
        return PlacementArrival::Ignored;

    const PlacementDeclaration Declared = DeclaredPlacement(Placing, PlacingMethod);
    if (Declared.Required == 0u)
        return PlacementArrival::Ignored;

    // 🔴 A `Resolved` closure measures between features, so an unsnapped contact is not an anchor at all.
    //    Refusing it lets a dimension tool ignore empty space without the caller knowing dimensions are
    //    special.
    if (Declared.Closure == PlacementClosure::Resolved && !HoverSnap.Resolved())
        return PlacementArrival::Ignored;

    if (Placing == SketchSubject::Slot)
    {
        if (!SpineFinishedDeclared)
        {
            if (Terminating && Taken.size() >= 2u)
            {
                SpineFinishedDeclared = true;
                return PlacementArrival::Anchored;
            }
            Taken.push_back(HoverAt);
            TakenPlacements.push_back(HoverSnap);
            if (Terminating && Taken.size() >= 2u)
            {
                SpineFinishedDeclared = true;
                return PlacementArrival::Anchored;
            }
            return PlacementArrival::Anchored;
        }
        else
        {
            Taken.push_back(HoverAt);
            TakenPlacements.push_back(HoverSnap);
            SpineFinishedDeclared = false;
            return PlacementArrival::Complete;
        }
    }

    // 🔴 ENTER ENDS THE SHAPE, IT DOES NOT PLACE A POINT. A terminating press anchored the hover
    //    FIRST and asked about termination afterwards, so pressing Enter to finish a polyline left a
    //    stray anchor wherever the pointer happened to be resting -- an extra leg the artist never
    //    clicked, on every terminated shape. When enough anchors already stand, a terminating press
    //    is only a full stop.
    //
    // ⚠️ It still anchors when the count is short, because otherwise Enter on a half-drawn shape
    //    would be silently ignored rather than taking the point that completes it.
    // ⚠️ ONLY a growing shape. A rectangle, an ellipse and a slot complete on a fixed count, and the
    //    press that places their last anchor may well be the second of a double-press -- swallowing
    //    it would refuse to draw the shape at all. Those close on `Sufficient`; only `Terminated`
    //    subjects run until the artist stops them, and only they can be ended without a new point.
    const bool AlreadyEnough = static_cast<std::uint32_t>(Taken.size()) >= Declared.Required;

    if (Terminating && Declared.Closure == PlacementClosure::Terminated && AlreadyEnough)
        return PlacementArrival::Complete;

    Taken.push_back(HoverAt);
    TakenPlacements.push_back(HoverSnap);

    const std::uint32_t Count = static_cast<std::uint32_t>(Taken.size());

    switch (Declared.Closure)
    {
        case PlacementClosure::Sufficient:
            // 📝 `>=` not `==`: a count reached exactly still completes, and a count somehow passed
            //    completes rather than running on forever.
            return Count >= Declared.Required ? PlacementArrival::Complete : PlacementArrival::Anchored;

        case PlacementClosure::Terminated:
            // 🔴 A terminated curve completes only when the artist says so AND enough anchors stand. A
            //    double-press on the second anchor of a four-anchor Hermite is not a curve.
            return (Terminating && Count >= Declared.Required) ? PlacementArrival::Complete
                                                               : PlacementArrival::Anchored;

        case PlacementClosure::Resolved:
            // 📝 Every anchor here is snapped — the unsnapped ones were refused above — so the count of
            //    anchors and the count of resolved anchors are the same number.
            return Count >= Declared.Required ? PlacementArrival::Complete : PlacementArrival::Anchored;

        case PlacementClosure::ClosureCount:
            break;
    }

    return PlacementArrival::Anchored;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          SEALING
//------------------------------------------------------------------------------------------------------------------------

SealedPlacement SketchPlacement::Seal()
{
    SealedPlacement Sealed = {};
    if (!Standing() || Taken.empty())
        return Sealed;

    Sealed.Subject      = Placing;
    Sealed.Method       = PlacingMethod;
    Sealed.Construction = ConstructionDeclared;

    // 🔴 The toggle travels with the placement. Deciding at commit time from a panel the commit
    //    cannot see is how the construction flag used to be lost.
    Sealed.ClosedProfile = ClosedProfileDeclared;

    // 🔴 The wheel-chosen side count travels with the sealed placement. The commit used to declare a
    //    hardcoded six sides, so a polygon was always a hexagon whatever the artist asked for.
    Sealed.Resolution   = SideCount;
    Sealed.Anchors      = std::move(Taken);
    Sealed.Placements   = std::move(TakenPlacements);

    // 🔴 The moved-from vectors are cleared, not left in whatever state the move produced, so the tool
    //    that continues to be held starts its next shape empty. The tool itself stays held: after drawing
    //    a line the artist still has the line tool.
    Taken.clear();
    TakenPlacements.clear();
    HoverTaken            = false;
    SpineFinishedDeclared = false;

    return Sealed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        REPORTING
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t SketchPlacement::Remaining() const
{
    if (!Standing())
        return 0u;

    const PlacementDeclaration Declared = DeclaredPlacement(Placing, PlacingMethod);
    const std::uint32_t        Count    = static_cast<std::uint32_t>(Taken.size());

    return Count >= Declared.Required ? 0u : Declared.Required - Count;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PREVIEW CURVE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 🔴 THE PREVIEW AND THE COMMIT MUST BE THE SAME SHAPE, so the shapes that are built from several
//    curves are built ONCE, here, and both callers ask for them. A preview that computes its own
//    geometry is a second implementation of the shape, and it drifts -- which is exactly what
//    happened to the ellipse, whose preview took the drag as its major axis while the commit
//    measured along the plane's own axes and produced a visibly different figure on release.

/// 🧩 The four sides of a rectangle spanning two opposite corners in the active plane.
void AppendRectangleSpans(const SpatialBasis& Basis,
                          const SpatialPoint& Corner,
                          const SpatialPoint& Opposite,
                          std::vector<CurveSpecification>& Delivered)
{
    const auto PlaneCoordinates = [&](const SpatialPoint& Position, double& Along, double& Across)
    {
        const SpatialDirection Offset = Difference(Basis.Origin, Position);
        Along = Dot(Offset, Basis.Along);
        Across = Dot(Offset, Basis.Across);
    };
    const auto PlanarPoint = [&](double Along, double Across)
    {
        return Added(Basis.Origin,
                     Added(Scaled(Basis.Along, Along), Scaled(Basis.Across, Across)));
    };

    double CornerAlong = 0.0;
    double CornerAcross = 0.0;
    double OppositeAlong = 0.0;
    double OppositeAcross = 0.0;
    PlaneCoordinates(Corner, CornerAlong, CornerAcross);
    PlaneCoordinates(Opposite, OppositeAlong, OppositeAcross);

    // 🔴 NEVER MIX WORLD COORDINATES TO BUILD A PLANE-RELATIVE SHAPE. The old construction always
    //    held world Up constant, which made an XY rectangle collapse one side to zero and made the
    //    preview/commit look like a triangle. Build both intermediate corners in the resolved basis
    //    used by the mouse, snapping all four vertices back onto the active plane.
    const SpatialPoint Second = PlanarPoint(OppositeAlong, CornerAcross);
    const SpatialPoint Fourth = PlanarPoint(CornerAlong, OppositeAcross);

    Delivered.push_back(CurveSpecification::DeclareLine(Corner, Second));
    Delivered.push_back(CurveSpecification::DeclareLine(Second, Opposite));
    Delivered.push_back(CurveSpecification::DeclareLine(Opposite, Fourth));
    Delivered.push_back(CurveSpecification::DeclareLine(Fourth, Corner));
}

/// 🧩 The sides of a regular polygon about a centre, with the first vertex under the pointer.
/// 🔴 THE NORMAL IS THE PLANE'S, NOT THE WORLD'S. This rotated the spoke about a hardcoded world up,
///    so a polygon drawn on the Front or Right workplane was laid out FLAT ON THE FLOOR and seen
///    edge-on -- the face pointed away from the camera instead of at it. Every vertex now stays in
///    the plane the artist is drawing on, which is the same rule `AppendRectangleSpans` already kept.
void AppendPolygonSpans(const SpatialDirection& Normal,
                        const SpatialPoint& Centre, const SpatialPoint& Vertex,
                        std::uint32_t Sides, std::vector<CurveSpecification>& Delivered)
{
    const std::uint32_t Resolved = Sides < PolygonSideMinimum ? PolygonSideMinimum
                                 : (Sides > PolygonSideMaximum ? PolygonSideMaximum : Sides);

    const SpatialDirection Spoke = Difference(Centre, Vertex);
    const double Radius = std::sqrt(LengthSquared(Spoke));
    if (!(Radius > 1.0e-6))
        return;

    // 📝 Rotated about the plane's own normal, so the first vertex sits exactly under the pointer
    //    and the polygon visibly follows the drag in the plane it belongs to.
    const double Turn = 6.283185307179586 / static_cast<double>(Resolved);

    SpatialPoint Previous = Vertex;
    for (std::uint32_t Corner = 1u; Corner <= Resolved; ++Corner)
    {
        const SpatialPoint Next = Added(Centre,
            Scaled(RotateAroundAxis(Normalize(Spoke), Normal, Turn * static_cast<double>(Corner)),
                   Radius));
        Delivered.push_back(CurveSpecification::DeclareLine(Previous, Next));
        Previous = Next;
    }
}

/// 🧩 A slot placement previewed at the phase it has actually reached.
/// in    Points         [-]  the anchors taken, with the hover appended as the uncommitted one
/// in    SpineFinished  [-]  whether the accepting press has locked the spine
/// in    Normal         [-]  the plane the slot is drawn in
/// note  🔴 THE OUTLINE ITSELF IS DECLARED BY `SlateShape`, so the preview and the commit are the SAME
///        geometry rather than two constructions that agree only until one is edited. They had already
///        drifted apart in the same place: both joined consecutive offset runs with a straight chord,
///        so every bend drew a bevel cutting through the slot's own body.
/// note  🔴 WHILE THE SPINE IS BEING DRAWN A SLOT IS A POLYLINE. It has no thickness yet -- the artist
///        has not said what it is -- so drawing one would be inventing a number they never gave.
void AppendSlotPlacement(const std::vector<SpatialPoint>& Points,
                         bool SpineFinished,
                         const SpatialDirection& Normal,
                         std::vector<CurveSpecification>& Delivered)
{
    if (Points.size() < 2u)
        return;

    // 📝 Phase one: the spine is still being clicked out, so it previews exactly as a polyline does --
    //    every leg taken stays on screen while the next one follows the pointer.
    if (!SpineFinished)
    {
        for (std::size_t Index = 0u; Index + 1u < Points.size(); ++Index)
            if (LengthSquared(Difference(Points[Index], Points[Index + 1u])) > 0.0)
                Delivered.push_back(CurveSpecification::DeclareLine(Points[Index], Points[Index + 1u]));
        return;
    }

    // 🔴 THE THICKNESS IS THE PERPENDICULAR DISTANCE TO THE SPINE, NOT THE DISTANCE TO ITS LAST POINT.
    //    Measuring from the last point made the thickness depend on where along the run the pointer
    //    was: dragging 20 out from the middle of a spine 100 long reported 53.852 and committed a slot
    //    nearly three times too thick. `ResolveSpineDistance` measures what the artist is pointing at.
    const std::vector<SpatialPoint> Spine(Points.begin(), Points.end() - 1u);
    AppendSlotOutline(Spine, ResolveSpineDistance(Spine, Points.back()), Normal, Delivered);
}

}   // namespace

/// 🧩 The one curve a placement describes, resolved in the plane it is being drawn on.
/// 🔴 THE PLANE HAD TO BE THREADED IN. `Circle` and `Ellipse` both wrote a HARDCODED world-up normal,
///    so a round shape drawn on any workplane but the ground was declared lying flat on the floor.
///    Seen from the camera that drew it, the face pointed ninety degrees away -- visible as a shape
///    that would not face the artist, and as a fill that vanished edge-on. A curve's normal is the
///    normal of the plane the artist drew it on; nothing else can supply it.
CurveSpecification ResolvePlacementCurveInPlane(const SpatialDirection& Normal,
                                                SketchSubject Subject,
                                                const std::vector<SpatialPoint>& Anchors,
                                                const SpatialPoint& Hover)
{
    // 📝 The hover is the anchor the artist has not committed to yet, so every subject below sees the
    //    same list the commit will see: the anchors taken, plus where the pointer is now.
    std::vector<SpatialPoint> Points = Anchors;
    Points.push_back(Hover);

    switch (Subject)
    {
        case SketchSubject::Line:
        case SketchSubject::Dimension:
            if (Points.size() >= 2u)
                return CurveSpecification::DeclareLine(Points.front(), Points.back());
            break;

        // 🔴 A polyline previews its LAST leg here; the finished legs are drawn by the plural, which
        //    returns one line per pair. Falling through to `default` left the tool with no preview at
        //    all, so an artist mid-polyline saw nothing follow the pointer.
        case SketchSubject::Polyline:
            if (Points.size() >= 2u)
                return CurveSpecification::DeclareLine(Points[Points.size() - 2u], Points.back());
            break;



        case SketchSubject::Arc:
            if (Points.size() >= 3u)
                return CurveSpecification::DeclareThreePointArc(Points[0], Points[1], Points[2]);
            if (Points.size() == 2u)
                return CurveSpecification::DeclareLine(Points[0], Points[1]);
            break;

        // 🔴 A Bezier's anchors ARE its control points, so the curve grows a degree with every click.
        case SketchSubject::Bezier:
            if (Points.size() >= 2u)
                return CurveSpecification::DeclareBezier(Points, { 0.0, 1.0 });
            break;

        // 🔴 THE THREE SUBJECTS THAT PREVIEWED AS NOTHING. Degree is clamped to what the control count
        //    can carry -- a cubic needs four points -- so the curve is drawn from the second click on
        //    rather than staying invisible until the last one.
        case SketchSubject::BasisSpline:
            if (Points.size() >= 2u)
            {
                BasisSplineCurve Spline;
                Spline.ControlPoints = Points;
                Spline.Degree = std::min<std::uint32_t>(3u, static_cast<std::uint32_t>(Points.size() - 1u));
                Spline.Periodic = false;
                return CurveSpecification::DeclareBasisSpline(Spline, { 0.0, 1.0 });
            }
            break;

        case SketchSubject::RationalSpline:
            if (Points.size() >= 2u)
            {
                RationalSplineCurve Spline;
                Spline.ControlPoints = Points;
                Spline.Weights.assign(Points.size(), 1.0);
                Spline.Degree = std::min<std::uint32_t>(3u, static_cast<std::uint32_t>(Points.size() - 1u));
                Spline.Periodic = false;
                return CurveSpecification::DeclareRationalSpline(Spline, { 0.0, 1.0 });
            }
            break;

        // 🔴 A HERMITE IS A CHAIN OF SPANS, and the chain is built by `ResolvePlacementCurves`.
        //    A single `CurveSpecification` cannot express more than one span, which is exactly why
        //    the shipped tool drew the first two points as a curve and left every later click as a
        //    bare point. This arm answers for the FIRST span only; callers that want the whole chain
        //    ask for the plural.
        case SketchSubject::Hermite:
            if (Points.size() >= 2u)
            {
                HermiteCurve Hermite;
                Hermite.ControlPoints = Points;
                Hermite.StartPoint = Points.front();
                Hermite.EndPoint = Points.back();
                Hermite.Tangents.resize(Points.size());
                for (std::size_t i = 0u; i < Points.size(); ++i)
                {
                    const SpatialPoint& Before = (i == 0u) ? Points[0] : Points[i - 1u];
                    const SpatialPoint& After = (i + 1u == Points.size()) ? Points[i] : Points[i + 1u];
                    Hermite.Tangents[i] = Scaled(Difference(Before, After), 0.5);
                }
                Hermite.StartTangent = Hermite.Tangents.front();
                Hermite.EndTangent = Hermite.Tangents.back();
                return CurveSpecification::DeclareHermite(Hermite, { 0.0, 1.0 });
            }
            break;

        case SketchSubject::Circle:
        case SketchSubject::Polygon:
            if (Points.size() >= 2u)
            {
                const double Radius = std::sqrt(LengthSquared(Difference(Points[0], Points[1])));
                if (Radius > 1.0e-6)
                {
                    CircleCurve Round;
                    Round.Centre         = Points[0];
                    Round.Normal         = Normal;
                    Round.StartDirection = Normalize(Difference(Points[0], Points[1]));
                    Round.Radius         = Radius;
                    return CurveSpecification::DeclareCircle(Round);
                }
            }
            break;

        // 🔴 THE ELLIPSE HAD NO ARM AND FELL THROUGH TO `default`, so it previewed as NOTHING while
        //    being dragged. What the artist saw was the COMMITTED shape appearing on release -- and
        //    because a committed ellipse is four quarter arcs whose fill needs a convex loop, an
        //    almost-closed outline with an open tip is exactly the failure that reads as "draws
        //    almost completely, but open at the tip". One ellipse curve previews closed by
        //    construction.
        case SketchSubject::Ellipse:
            if (Points.size() >= 2u)
            {
                const SpatialDirection Span = Difference(Points[0], Points[1]);
                const double Major = std::sqrt(LengthSquared(Span));
                if (Major > 1.0e-6)
                {
                    EllipseCurve Round;
                    Round.Centre         = Points[0];
                    Round.Normal         = Normal;
                    Round.MajorDirection = Normalize(Span);
                    Round.MajorRadius    = Major;
                    Round.MinorRadius    = Points.size() >= 3u
                        ? std::max(std::sqrt(LengthSquared(Difference(Points[0], Points[2]))), 1.0e-6)
                        : Major * 0.5;
                    return CurveSpecification::DeclareEllipse(Round);
                }
            }
            break;

        // 📝 A rectangle and a slot preview as their diagonal until they are committed; drawing the
        //    four sides needs a profile, which only the commit declares.
        case SketchSubject::Rectangle:
        case SketchSubject::Slot:
            if (Points.size() >= 2u)
                return CurveSpecification::DeclareLine(Points.front(), Points.back());
            break;

        default:
            break;
    }

    return {};
}

CurveSpecification ResolvePlacementCurve(SketchSubject Subject,
                                         const std::vector<SpatialPoint>& Anchors,
                                         const SpatialPoint& Hover)
{
    // 📝 Callers with no plane to offer keep the historic ground normal. Every path that KNOWS the
    //    workplane goes through the basis-aware overloads instead, which is what the live tool does.
    return ResolvePlacementCurveInPlane({ 0.0, 1.0, 0.0 }, Subject, Anchors, Hover);
}

void ResolvePlacementCurvesInPlane(const SpatialBasis& Basis,
                                   SketchSubject Subject,
                                   const std::vector<SpatialPoint>& Anchors,
                                   const SpatialPoint& Hover,
                                   std::vector<CurveSpecification>& Delivered,
                                   std::uint32_t Resolution)
{
    Delivered.clear();

    std::vector<SpatialPoint> Points = Anchors;
    Points.push_back(Hover);

    // 🔴 ONLY A HERMITE IS A CHAIN. Every other subject is one curve, so the plural defers to the
    //    singular rather than duplicating twenty-two arms of the same table.
    // 🔴 A POLYLINE IS A CHAIN TOO. Every leg already taken must stay on screen while the next one
    //    follows the pointer -- previewing only the last leg would make the shape appear to be a
    //    single moving line rather than the run of segments it is.
    if (Subject == SketchSubject::Polyline && Points.size() >= 2u)
    {
        for (std::size_t Index = 0u; Index + 1u < Points.size(); ++Index)
            if (LengthSquared(Difference(Points[Index], Points[Index + 1u])) > 0.0)
                Delivered.push_back(CurveSpecification::DeclareLine(Points[Index], Points[Index + 1u]));
        return;
    }

    // 🔴 A RECTANGLE IS FOUR SIDES AND MUST LOOK LIKE ONE FROM THE FIRST CLICK. It previewed as the
    //    DIAGONAL -- a single line from the corner to the pointer -- so the artist saw a rectangle
    //    only after the second click had already placed it. The four sides are built from the same
    //    two corners the commit uses, so what is dragged out is what lands.
    if (Subject == SketchSubject::Rectangle && Points.size() >= 2u)
    {
        AppendRectangleSpans(Basis, Points.front(), Points.back(), Delivered);
        return;
    }

    // 🔴 A POLYGON PREVIEWED AS A CIRCLE, so scrolling the side count changed nothing on screen and
    //    the artist could not see what they were choosing until it committed. It is drawn as its
    //    actual sides, at the resolution the wheel has reached.
    if (Subject == SketchSubject::Polygon && Points.size() >= 2u)
    {
        AppendPolygonSpans(Basis.Normal, Points.front(), Points.back(), Resolution, Delivered);
        return;
    }

    // 📝 Without a placement to read the phase from, a slot is taken to be complete when a point
    //    beyond the spine's two is present -- which is what a SEALED slot always looks like. The live
    //    preview goes through the overload below, which is told the phase instead of inferring it.
    if (Subject == SketchSubject::Slot && Points.size() >= 2u)
    {
        AppendSlotPlacement(Points, Points.size() >= 3u, Basis.Normal, Delivered);
        return;
    }

    const CurveSpecification Single = ResolvePlacementCurveInPlane(Basis.Normal, Subject, Anchors, Hover);
    if (Single.Declared())
        Delivered.push_back(Single);
}

/// 📝 The plane-less plural keeps the historic ground basis, so callers that never knew a workplane
///    behave exactly as before. There is still ONE table of subjects behind both.
void ResolvePlacementCurves(SketchSubject Subject,
                            const std::vector<SpatialPoint>& Anchors,
                            const SpatialPoint& Hover,
                            std::vector<CurveSpecification>& Delivered,
                            std::uint32_t Resolution)
{
    ResolvePlacementCurvesInPlane(
        SpatialBasis{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } },
        Subject, Anchors, Hover, Delivered, Resolution);
}

/// 🔴 EVERY SUBJECT NOW GETS THE PLANE, NOT JUST TWO OF THEM. This used to special-case `Rectangle`
///    and `Slot` and then hand everything else to the plane-less plural, which assumed the ground.
///    That is why the two shapes somebody had already chased were correct while `Circle`, `Ellipse`
///    and `Polygon` were still drawn lying on the floor whatever workplane was active. A per-subject
///    exception list is the same structural fault as a per-subject `else if` chain: it is silently
///    wrong for whatever nobody remembered to add. There is one plane-aware table now.
void ResolvePlacementCurves(const SpatialBasis& Basis,
                            SketchSubject Subject,
                            const std::vector<SpatialPoint>& Anchors,
                            const SpatialPoint& Hover,
                            std::vector<CurveSpecification>& Delivered,
                            std::uint32_t Resolution)
{
    ResolvePlacementCurvesInPlane(Basis, Subject, Anchors, Hover, Delivered, Resolution);
}

void ResolvePlacementCurves(const SketchPlacement& Placing,
                            const SpatialBasis& Basis,
                            std::vector<CurveSpecification>& Delivered)
{
    Delivered.clear();

    if (!Placing.Standing() || !Placing.HoverStanding())
        return;

    // 🔴 THE PHASE COMES FROM THE TOOL, WHICH IS THE ONLY THING THAT KNOWS IT. Every other subject is
    //    decided by its anchor count alone, but a slot is drawn in two phases through one anchor list,
    //    so the same three points mean a spine still being drawn before the accepting press and a
    //    thickened slot after it.
    if (Placing.Subject() == SketchSubject::Slot && !Placing.Anchors().empty())
    {
        std::vector<SpatialPoint> Points = Placing.Anchors();
        Points.push_back(Placing.HoverPosition());
        AppendSlotPlacement(Points, Placing.SpineFinished(), Basis.Normal, Delivered);
        return;
    }

    ResolvePlacementCurves(Basis, Placing.Subject(), Placing.Anchors(), Placing.HoverPosition(),
                           Delivered, Placing.Resolution());
}

} // namespace Slate
