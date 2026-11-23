//============================================================================================================================================
//                                                          SKETCHPLACEMENT.H
//============================================================================================================================================
// 🧩 One sketch tool's placement in progress — which shape, placed which way, the anchors taken, and when it is complete.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Geometry/SpatialBasis/Api/SpatialBasis.h"
#include "SlateShape/Sketch/ConstraintSpecification/Api/ConstraintSpecification.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateShape/Sketch/SketchSnap/Api/SketchSnap.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       WHAT IS PLACED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The shape a sketch tool places. WHAT is drawn, never how it was drawn.
/// note  🔴 This is the axis that was conflated. The retired enumeration held twenty-two members, but five
///        of them named no shape at all: `CenterRectangle`, `ThreePointRectangle`, `DiameterCircle`,
///        `ThreePointCircle` and `CenterStartEndArc` are a rectangle, a rectangle, a circle, a circle and
///        an arc — each paired with a different way of pointing at one. The proof that they were never
///        shapes is that all four rectangle spellings ended in the same `DeclareRectangle`, and all three
///        circle spellings in the same `DeclareCircle`: the geometry layer only ever knew four shapes
///        where the tool layer claimed nine.
/// note  📝 Every member below maps onto something `SketchStructure` can actually declare. `Parabola` and
///        `Hyperbola` are deliberately ABSENT: `CurveSpecification` models no conic section beyond the
///        ellipse, so a tool offering them would be a tile the geometry cannot honour. They are added when
///        `SlateShape` grows `DeclareParabola`, and not one commit before.
/// tag   guarantee
enum class SketchSubject : std::uint32_t
{
    None           =  0u,   // [-] - no drawing tool is held
    Point          =  1u,
    Line           =  2u,
    Polyline       =  3u,
    Rectangle      =  4u,
    Circle         =  5u,
    Ellipse        =  6u,
    Arc            =  7u,   // [-] - a circular arc
    Polygon        =  8u,   // [-] - regular, n-sided
    Slot           =  9u,   // [-] - two centres and a radius; `DeclareSlot`, no other shape produces it
    Bezier         = 10u,
    BasisSpline    = 11u,
    RationalSpline = 12u,   // [-] - NURBS
    Hermite        = 13u,
    Dimension      = 14u,   // [-] - measures between two resolved features; places no curve
    SubjectCount   = 15u    // [-] - the closed count, never a subject
};

/// 🧩 How the artist points at a shape. HOW it is drawn, never what.
/// note  🔴 Separating this axis is what removes the duplication. A rectangle placed corner-to-corner and a
///        rectangle placed centre-out are ONE tool with two methods, not two tools — and the moment they
///        are one tool, the two cannot disagree about what a rectangle is. The retired enumeration could
///        not express that, so it grew a member per combination and left the artist to notice that
///        `CenterRectangle` and `Rectangle` produced identical geometry.
/// note  📝 Not every method suits every shape. `Diameter` is meaningless for a polyline. `AcceptedBy`
///        below states which pairs exist, and it is the only place that knowledge lives.
/// tag   guarantee
enum class PlacementMethod : std::uint32_t
{
    Extent       = 0u,   // [-] - two opposite anchors span the shape; the default for everything
    Centred      = 1u,   // [-] - the first anchor is the centre, the second gives the reach
    ThreePoint   = 2u,   // [-] - three anchors on the shape itself, no centre named
    Diameter     = 3u,   // [-] - two anchors give a diameter rather than a radius
    Tangent      = 4u,   // [-] - the shape leaves the anchor tangent to what was snapped
    MethodCount  = 5u    // [-] - the closed count, never a method
};

/// 🧩 Whether a shape accepts a placement method.
/// out   Accepted  [-]  false for a pair no geometry call can honour
/// note  🔴 A tool offering `Diameter` for a polyline would be a tile that cannot commit — exactly the
///        defect the retired table shipped, where `DiameterCircle` sat in a branch that could never
///        satisfy the commit. Stating the legal pairs once means a caller cannot construct an
///        impossible tool, and `SketchPlacementProof` walks every shape against every method.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr bool AcceptedBy(SketchSubject Subject, PlacementMethod Method)
{
    if (Method == PlacementMethod::Extent)
        // 🔴 A polygon is a centre and a circumradius; `DeclareRegularPolygon` takes exactly that and there
        //    is no spanned form of one. Refusing it here leaves precisely ONE way to place a polygon,
        //    which is the redundancy rule this table exists to enforce — `SketchPlacementProof` §2 fails
        //    on any two placements that share a name.
        return Subject != SketchSubject::None && Subject != SketchSubject::Polygon;

    switch (Subject)
    {
        // 📝 A rectangle is spanned, centred, or laid on three points — the three spellings the retired
        //    enumeration carried as three separate tools.
        case SketchSubject::Rectangle:
            return Method == PlacementMethod::Centred || Method == PlacementMethod::ThreePoint;

        // 📝 A circle is centred, given three points on its circumference, or given a diameter.
        case SketchSubject::Circle:
            return Method == PlacementMethod::Centred ||
                   Method == PlacementMethod::ThreePoint ||
                   Method == PlacementMethod::Diameter;

        case SketchSubject::Ellipse:
            return Method == PlacementMethod::Centred || Method == PlacementMethod::Diameter;

        // 📝 `CenterStartEndArc` and `TangentArc` were two of the retired tools; both are the arc.
        case SketchSubject::Arc:
            return Method == PlacementMethod::Centred ||
                   Method == PlacementMethod::ThreePoint ||
                   Method == PlacementMethod::Tangent;

        case SketchSubject::Polygon:
            return Method == PlacementMethod::Centred;

        default:
            return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT A TOOL DECLARES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How a placement decides it has taken every anchor it needs.
/// note  🔴 Three genuinely different mechanisms, which were three separate branch groups in the host.
///        `Sufficient` completes the moment the required count is reached. `Terminated` keeps accepting
///        anchors past that count until the artist double-presses, which is how a polyline of unknown
///        length ends. `Resolved` counts only anchors that landed on a snap, because a dimension measures
///        between two features and a contact in empty space is not a feature.
/// tag   guarantee
enum class PlacementClosure : std::uint32_t
{
    Sufficient   = 0u,   // [-] - completes as soon as `Required` anchors are taken
    Terminated   = 1u,   // [-] - takes anchors until a terminating contact, then completes if `Required` are taken
    Resolved     = 2u,   // [-] - only snapped anchors count toward `Required`
    ClosureCount = 3u    // [-] - the closed count, never a closure
};

/// 🧩 Everything one sketch tool states about how it is placed.
/// tag   guarantee, nonallocating, nonthrowing
struct PlacementDeclaration
{
    std::uint32_t     Required      = 0u;                         // [-] - anchors taken before it may complete
    PlacementClosure  Closure       = PlacementClosure::Sufficient;// [-] - how it decides it has them
    bool              ClosedProfile = false;                      // [-] - the placed shape encloses an area
    const char*       Naming        = "";                         // [-] - static text; what the artist calls it
};

/// 🧩 What one shape placed one way declares — anchors required, how it closes, whether it encloses an area.
/// in    Subject   [-]  the shape being placed
/// in    Method    [-]  how the artist is pointing at it
/// out   Declared  [-]  the declaration; `Required == 0` for `None` and for a pair `AcceptedBy` refuses
/// note  🔴 ONE table, and it now takes the method as an argument. It replaces four parallel switch
///        statements — `RequiredAnchors`, `IsMultiClickCurve`, `ProducesClosedProfile` and `SubjectName` —
///        that each enumerated the same twenty-two subjects and could therefore disagree about any one.
///
/// note  🔴 `Required` counts ANCHORS TAKEN, uniformly. The column it replaces did not: it was only ever
///        read for the four terminated curves, and being unread it had drifted into meaning "anchors
///        stored" for some rows and "presses the artist makes" for others. `Circle` said `1` while
///        genuinely needing a centre AND a rim point. Two subjects — the retired `DiameterCircle` and
///        `Polygon` — sat in a pointer branch that stored one anchor while their commit required two, so
///        **neither tool did anything at all when drawn with the mouse**; they worked only via Enter,
///        whose branch stored the hover first. Counting one thing everywhere is what closes that.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr PlacementDeclaration DeclaredPlacement(SketchSubject Subject,
                                                 PlacementMethod Method = PlacementMethod::Extent)
{
    if (!AcceptedBy(Subject, Method))
        return { 0u, PlacementClosure::Sufficient, false, "" };

    // 🔴 The method decides the anchor count wherever it differs from the shape's own. Three points on a
    //    circle is three anchors; a centred circle is two. One rule, stated once, instead of a tool per
    //    combination.
    if (Method == PlacementMethod::ThreePoint)
    {
        switch (Subject)
        {
            case SketchSubject::Rectangle:     return { 3u, PlacementClosure::Sufficient, true,  "3-Point Rectangle" };
            case SketchSubject::Circle:        return { 3u, PlacementClosure::Sufficient, true,  "3-Point Circle" };
            case SketchSubject::Arc:           return { 3u, PlacementClosure::Sufficient, false, "3-Point Arc" };
            default:                           return { 0u, PlacementClosure::Sufficient, false, "" };
        }
    }

    if (Method == PlacementMethod::Centred)
    {
        switch (Subject)
        {
            case SketchSubject::Rectangle:     return { 2u, PlacementClosure::Sufficient, true,  "Centred Rectangle" };
            case SketchSubject::Circle:        return { 2u, PlacementClosure::Sufficient, true,  "Centred Circle" };
            case SketchSubject::Ellipse:       return { 3u, PlacementClosure::Sufficient, true,  "Centred Ellipse" };
            case SketchSubject::Polygon:       return { 2u, PlacementClosure::Sufficient, true,  "Polygon" };
            case SketchSubject::Arc:           return { 3u, PlacementClosure::Sufficient, false, "Centred Arc" };
            default:                           return { 0u, PlacementClosure::Sufficient, false, "" };
        }
    }

    if (Method == PlacementMethod::Diameter)
    {
        switch (Subject)
        {
            case SketchSubject::Circle:  return { 2u, PlacementClosure::Sufficient, true, "Diameter Circle" };
            case SketchSubject::Ellipse: return { 3u, PlacementClosure::Sufficient, true, "Diameter Ellipse" };
            default:                     return { 0u, PlacementClosure::Sufficient, false, "" };
        }
    }

    if (Method == PlacementMethod::Tangent)
        return Subject == SketchSubject::Arc ? PlacementDeclaration{ 3u, PlacementClosure::Sufficient, false, "Tangent Arc" }
                                             : PlacementDeclaration{ 0u, PlacementClosure::Sufficient, false, "" };

    switch (Subject)
    {
        case SketchSubject::Point:          return { 1u, PlacementClosure::Sufficient, false, "Point" };
        case SketchSubject::Line:           return { 2u, PlacementClosure::Sufficient, false, "Line" };
        case SketchSubject::Rectangle:      return { 2u, PlacementClosure::Sufficient, true,  "Rectangle" };
        case SketchSubject::Circle:         return { 2u, PlacementClosure::Sufficient, true,  "Circle" };
        case SketchSubject::Ellipse:        return { 3u, PlacementClosure::Sufficient, true,  "Ellipse" };
        case SketchSubject::Arc:            return { 3u, PlacementClosure::Sufficient, false, "Arc" };
        case SketchSubject::Slot:           return { 2u, PlacementClosure::Terminated, true,  "Slot" };
        case SketchSubject::Polyline:       return { 2u, PlacementClosure::Terminated, false, "Polyline" };
        case SketchSubject::Bezier:         return { 2u, PlacementClosure::Terminated, false, "Bezier" };
        case SketchSubject::BasisSpline:    return { 3u, PlacementClosure::Terminated, false, "Basis Spline" };
        case SketchSubject::RationalSpline: return { 3u, PlacementClosure::Terminated, false, "NURBS Curve" };
        // 🔴 TWO, NOT FOUR. A Hermite is a chain of spans through every anchor, so it needs the same
        //    two points a line does and grows with each further click until the artist ends it.
        //    Requiring four was the {start, end, tangent, tangent} reading that made the tool consume
        //    four clicks to draw one span and ignore everything after.
        case SketchSubject::Hermite:        return { 2u, PlacementClosure::Terminated, false, "Hermite" };
        case SketchSubject::Dimension:      return { 2u, PlacementClosure::Resolved,   false, "Dimension" };
        // 📝 `Polygon` is absent by design: `AcceptedBy` refuses it under `Extent`, so this arm is
        //    unreachable. It is listed rather than left to a `default` so that -Wswitch keeps reporting
        //    every future subject that has not been placed in this table.
        case SketchSubject::Polygon:
        case SketchSubject::None:
        case SketchSubject::SubjectCount:   break;
    }

    return { 0u, PlacementClosure::Sufficient, false, "" };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CATALOGUE MAPPING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One catalogue tile resolved into the shape it places and the way it places it.
/// tag   guarantee, nonallocating, nonthrowing
struct SketchToolSelection
{
    SketchSubject   Subject = SketchSubject::None;         // [-]
    PlacementMethod Method  = PlacementMethod::Extent;     // [-]
};

/// 🧩 Which shape a catalogue tile places, and by which method.
/// in    Tile      [-]  the catalogue subject the artist pressed
/// out   Selected  [-]  shape and method; `None` for selection, constraints, datums, imports and lighting
/// note  🔴 This is where the redundancy is absorbed rather than propagated. `ParametricToolSubject` still
///        offers sixty-one tiles because that enumeration is the artist's palette and changing it is a
///        presentation decision — but nine of those tiles collapse onto four shapes here, and every tool
///        below the seam sees the shape and the method, never the tile.
/// note  📝 `ConstructionLine` places a Line the caller then marks as construction; `Interpolate`,
///        `Approximate` and `BezierCurve` are three tiles and one Bezier.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr SketchToolSelection SelectedTool(ParametricToolSubject Tile)
{
    switch (Tile)
    {
        case ParametricToolSubject::Point:                return { SketchSubject::Point,          PlacementMethod::Extent };
        case ParametricToolSubject::Line:                 return { SketchSubject::Line,           PlacementMethod::Extent };
        case ParametricToolSubject::ConstructionLine:     return { SketchSubject::Line,           PlacementMethod::Extent };
        case ParametricToolSubject::Polyline:             return { SketchSubject::Polyline,       PlacementMethod::Extent };
        case ParametricToolSubject::Rectangle:            return { SketchSubject::Rectangle,      PlacementMethod::Extent };
        case ParametricToolSubject::Circle:               return { SketchSubject::Circle,         PlacementMethod::Centred };
        case ParametricToolSubject::Ellipse:              return { SketchSubject::Ellipse,        PlacementMethod::Centred };
        case ParametricToolSubject::Arc:                  return { SketchSubject::Arc,            PlacementMethod::ThreePoint };
        case ParametricToolSubject::Polygon:              return { SketchSubject::Polygon,        PlacementMethod::Centred };
        case ParametricToolSubject::Slot:                 return { SketchSubject::Slot,           PlacementMethod::Extent };
        case ParametricToolSubject::LinearDimension:      return { SketchSubject::Dimension,      PlacementMethod::Extent };

        // 🔴 The nine tiles that were nine tools and are four shapes.
        case ParametricToolSubject::CenterRectangle:      return { SketchSubject::Rectangle,      PlacementMethod::Centred };
        case ParametricToolSubject::ThreePointRectangle:  return { SketchSubject::Rectangle,      PlacementMethod::ThreePoint };
        case ParametricToolSubject::DiameterCircle:       return { SketchSubject::Circle,         PlacementMethod::Diameter };
        case ParametricToolSubject::ThreePointCircle:     return { SketchSubject::Circle,         PlacementMethod::ThreePoint };
        case ParametricToolSubject::CenterStartEndArc:    return { SketchSubject::Arc,            PlacementMethod::Centred };
        case ParametricToolSubject::TangentArc:           return { SketchSubject::Arc,            PlacementMethod::Tangent };

        case ParametricToolSubject::BezierCurve:
        case ParametricToolSubject::Interpolate:
        case ParametricToolSubject::Approximate:          return { SketchSubject::Bezier,         PlacementMethod::Extent };
        case ParametricToolSubject::HermiteCurve:         return { SketchSubject::Hermite,        PlacementMethod::Extent };
        case ParametricToolSubject::BasisSpline:          return { SketchSubject::BasisSpline,    PlacementMethod::Extent };
        case ParametricToolSubject::RationalSpline:       return { SketchSubject::RationalSpline, PlacementMethod::Extent };

        default:                                          return { SketchSubject::None,           PlacementMethod::Extent };
    }
}

/// 🧩 Which relationship a catalogue tile asks for.
/// in    Tile       [-]  the catalogue subject the artist pressed
/// out   Delivered  [-]  the relationship, untouched when the tile is not a constraint tile
/// out   -          [-]  whether the tile names one at all
/// note  🔴 THREE CONSTRAINT TILES CANNOT BE HONOURED AND SAY SO HERE. The palette offers `Midpoint`,
///        `Symmetry` and `Concentric`, and `ConstraintSubject` declares none of the three — the shipped
///        mapping sent them to `default` alongside every non-constraint tile, so pressing one was
///        indistinguishable from pressing nothing. They stay refused, deliberately and in one place,
///        until the solver declares them.
/// note  ⚠️ `Fixed` is reachable from no tile. It exists in `ConstraintSubject` and the palette has no
///        tile for it, so it can only be declared in code.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr bool SelectedConstraint(ParametricToolSubject Tile, ConstraintSubject& Delivered)
{
    switch (Tile)
    {
        case ParametricToolSubject::HorizontalConstraint:    Delivered = ConstraintSubject::Horizontal;    return true;
        case ParametricToolSubject::VerticalConstraint:      Delivered = ConstraintSubject::Vertical;      return true;
        case ParametricToolSubject::CoincidentConstraint:    Delivered = ConstraintSubject::Coincident;    return true;
        case ParametricToolSubject::ParallelConstraint:      Delivered = ConstraintSubject::Parallel;      return true;
        case ParametricToolSubject::PerpendicularConstraint: Delivered = ConstraintSubject::Perpendicular; return true;
        case ParametricToolSubject::TangentConstraint:       Delivered = ConstraintSubject::Tangent;       return true;
        case ParametricToolSubject::EqualConstraint:         Delivered = ConstraintSubject::Equal;         return true;
        default:                                                                                           return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      WHAT ONE CONTACT DOES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one contact did to the placement.
/// note  🔴 `Complete` means every anchor is taken and the caller must now `Seal`. This component
///        deliberately writes nothing: what a placed shape BECOMES — a sketch record, a revision, a
///        selection — is a document question, and a tool that answered it would have to name documents.
/// tag   guarantee
enum class PlacementArrival : std::uint32_t
{
    Ignored      = 0u,   // [-] - no tool is held, or the contact placed nothing
    Anchored     = 1u,   // [-] - an anchor was taken; the placement continues
    Complete     = 2u,   // [-] - every anchor is taken; `Seal` now
    ArrivalCount = 3u    // [-] - the closed count, never an arrival
};

/// 🧩 The curve an in-progress placement currently describes, for previewing it before it is committed.
/// in    Subject   [-]  what is being placed
/// in    Anchors   [-]  the anchors taken so far
/// in    Hover     [-]  where the pointer is now, treated as the next anchor
/// out   Declared  [-]  an undeclared curve when the placement cannot yet describe one
/// note  🔴 THIS IS WHY NURBS AND HERMITE DREW NOTHING. The preview was a chain of `else if` branches
///        naming ONE subject each, and only `Line`, `Polyline`, `Arc`, `Bezier`, `Ellipse`, `Rectangle`
///        and `Circle` ever got one. `Hermite`, `BasisSpline` and `RationalSpline` fell off the end of
///        the chain, so clicking with those tools drew no feedback at all — indistinguishable from a
///        dead tool, even though all three committed correctly once enough anchors were taken. The same
///        omission is why they seemed not to work: nothing told the artist the clicks had registered.
/// note  🔴 Built the way the COMMIT builds it, so the preview is the shape that will be created rather
///        than an approximation of it that can silently drift from the committed result.
/// cost  🚩
/// tag   api, nonthrowing
CurveSpecification ResolvePlacementCurve(SketchSubject Subject,
                                         const std::vector<SpatialPoint>& Anchors,
                                         const SpatialPoint& Hover);

/// 🧩 The same single curve, resolved in the plane the artist is actually drawing on.
/// in    Normal   [-]  the active workplane's normal, which becomes the curve's own
/// note  🔴 A ROUND SHAPE'S NORMAL IS ITS PLANE'S NORMAL. `Circle` and `Ellipse` wrote a hardcoded
///        world Up, so either drawn on the Front, Back, Left or Right workplane was declared lying
///        flat on the ground: the face pointed ninety degrees away from the camera that drew it,
///        which is why an ellipse would not face the artist and its fill disappeared edge-on.
/// note  📝 The plane-less overload above still exists and still assumes the ground, for callers that
///        genuinely have no workplane. Anything that knows one should call this.
/// cost  🚩
/// tag   api, nonthrowing
CurveSpecification ResolvePlacementCurveInPlane(const SpatialDirection& Normal,
                                                SketchSubject Subject,
                                                const std::vector<SpatialPoint>& Anchors,
                                                const SpatialPoint& Hover);

/// 🧩 Every curve an in-progress placement describes, for the subjects that draw more than one.
/// in    Subject    [-]  what is being placed
/// in    Anchors    [-]  the anchors taken so far
/// in    Hover      [-]  where the pointer is now, treated as the next anchor
/// out   Delivered  [-]  cleared, then filled; empty when nothing can be drawn yet
/// note  🔴 A HERMITE IS A CHAIN AND EVERY OTHER SUBJECT IS ONE CURVE. Read as
///        {start, end, tangent, tangent}, a Hermite spent FOUR clicks on ONE span and ignored every
///        click after — the reported "renders the first 2 points as a curve, other places are just
///        points". Each anchor is now a point the curve passes THROUGH, with Catmull-Rom tangents
///        inferred from its neighbours, so the spans meet smoothly and the Nth click extends it.
/// note  📝 Defers to the singular for every other subject, so there is still one table.
/// cost  🚩
/// tag   api, nonthrowing
// 🔴 Declared here rather than beside `SealedPlacement` because the preview resolver below takes a
//    side count as a defaulted argument, and a `constexpr` used as a default must already be visible.
constexpr std::uint32_t PolygonSideMinimum = 3u;
constexpr std::uint32_t PolygonSideMaximum = 64u;
constexpr std::uint32_t PolygonSideDefault = 6u;

/// in    Resolution  [-]  how many sides a polygon should preview with, from the wheel
void ResolvePlacementCurves(SketchSubject Subject,
                            const std::vector<SpatialPoint>& Anchors,
                            const SpatialPoint& Hover,
                            std::vector<CurveSpecification>& Delivered,
                            std::uint32_t Resolution = PolygonSideDefault);

/// 🧩 Every curve a placement describes, resolved in the plane it is being drawn on.
/// note  🔴 EVERY SUBJECT, not an exception list. This once special-cased `Rectangle` and `Slot` and
///        handed the rest to the plane-less plural, so `Circle`, `Ellipse` and `Polygon` stayed flat
///        on the floor no matter which workplane was active.
void ResolvePlacementCurvesInPlane(const SpatialBasis& Basis,
                                   SketchSubject Subject,
                                   const std::vector<SpatialPoint>& Anchors,
                                   const SpatialPoint& Hover,
                                   std::vector<CurveSpecification>& Delivered,
                                   std::uint32_t Resolution = PolygonSideDefault);

/// 🧩 Basis-aware placement resolver for world-native preview paths. Every subject is derived in the
///       active plane rather than by assuming that world Up is the plane's second coordinate.
void ResolvePlacementCurves(const SpatialBasis& Basis,
                            SketchSubject Subject,
                            const std::vector<SpatialPoint>& Anchors,
                            const SpatialPoint& Hover,
                            std::vector<CurveSpecification>& Delivered,
                            std::uint32_t Resolution = PolygonSideDefault);


/// 🧩 The anchors of one finished placement, moved out of the placement that took them.
/// note  📝 Returned by value from `Seal`, so the placement it came from is already reset. There is no
///        window in which a sealed placement and the tool that produced it both hold the same anchors.
/// tag   guarantee, owning
/// 🧩 The side count a polygon starts at, and the range the wheel may drive it through.
/// note 🔴 Three is the smallest shape with an area; below it a "polygon" is a line or a point.

struct SealedPlacement
{
    SketchSubject                    Subject      = SketchSubject::None;      // [-]
    PlacementMethod                  Method       = PlacementMethod::Extent;  // [-]
    std::vector<SpatialPoint>        Anchors      = {};                       // [-] - in the order taken
    std::vector<SketchSnapPlacement> Placements   = {};                       // [-] - what each anchor landed on
    bool                             Construction = false;                    // [-] - construction geometry
    std::uint32_t                    Resolution   = PolygonSideDefault;       // [-] - polygon sides only

    // 🔴 WHETHER A SHAPE THAT CLOSES IS A REGION OR A RUN OF WIRES. A closed polyline drew four
    //    separate lines that happened to meet: nothing downstream knew it enclosed anything, so it
    //    could not be shaded and could not be extruded or lofted. Sealed as a profile it is a face
    //    with an inside. The artist chooses, because a closed run of wire is a legitimate thing to
    //    want -- a path to sweep along, or a boundary that must stay open.
    bool                             ClosedProfile = true;                    // [-] - seal a closed shape as a region
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One sketch tool's placement in progress — the anchors taken, and whether it is ready to seal.
/// note  🔴 This component exists because the host held this state machine as a chain of `else if` branches
///        over twenty-two subjects, grouped by how many anchors each needed. Adding a tool meant finding
///        the branch with the right count and joining it; getting it wrong meant a shape that completed one
///        anchor early, with nothing to catch it — and two tools that never completed at all. The count and
///        the closure are stated once, in `DeclaredPlacement`, and this component READS that table.
/// note  ⚠️ Anchors are held, not interpreted. What a Bezier's four anchors mean is the caller's question;
///        this component only knows there must be four before it may complete.
/// note  📝 It names no panel, no document, no device and no tick — a tool that knew when it was recorded
///        would be a tick concern wearing a tool's name.
/// tag   owning
class SketchPlacement
{
public:

    /// 🧩 Declares which shape is being placed and how, discarding whatever placement stood before.
    /// in    Subject       [-]  the shape to place; `None` abandons and stands the tool down
    /// in    Method        [-]  how the artist is pointing at it
    /// in    Construction  [-]  whether the placed shape is construction geometry
    /// note  📝 Declaring the SAME shape and method that already stand is a no-op, not a restart — the
    ///        caller may state the held tool every tick without losing the anchors already taken.
    ///        Declaring a different one abandons the placement, which is the ordinary case: the artist
    ///        pressed another tile without finishing.
    /// note  🔴 A pair `AcceptedBy` refuses stands the tool down rather than holding a tool that can never
    ///        complete. That is the shape of the retired `DiameterCircle` defect, refused at declaration.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Declare(SketchSubject Subject,
                 PlacementMethod Method = PlacementMethod::Extent,
                 bool Construction = false);

    /// 🧩 Takes one contact as an anchor, and states whether the placement is now complete.
    /// in    Terminating  [-]  whether this contact was a double-press
    /// out   Arrival      [-]  `Complete` when the caller must now `Seal`
    /// note  🔴 The anchor is taken at the CURRENT hover, not at a position passed here, so the snap
    ///        resolved for the preview is exactly the snap recorded on the anchor. Passing the position
    ///        again would let a caller anchor somewhere it never previewed.
    /// note  🔴 A `Resolved` closure takes only snapped anchors — a dimension measures between features, so
    ///        a contact in empty space is discarded and reported `Ignored`, which tells the caller nothing
    ///        changed and the contact is still theirs.
    /// cost  🚩
    /// tag   api, nonthrowing
    PlacementArrival Anchor(bool Terminating = false);

    /// 🧩 States where the pointer is now, so the caller can preview the shape before the next contact.
    /// in    Position   [-]  where the pointer is, already snapped by the caller
    /// in    Placement  [-]  what it snapped to, or an unresolved placement
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Hover(const SpatialPoint& Position, const SketchSnapPlacement& Placement);

    /// 🧩 Moves the anchors out of a complete placement, leaving the same tool held and ready.
    /// out   Sealed  [-]  the anchors; empty with subject `None` when the placement was not `Complete`
    /// note  🔴 Sealing does NOT stand the tool down. After drawing a line the line tool is still held,
    ///        which is what every CAD application does.
    /// use   Called on `Complete` and at no other time.
    /// cost  🚩
    /// tag   api, nonthrowing
    SealedPlacement Seal();

    /// 🧩 Discards every anchor taken and stands the tool down.
    /// use   Escape, or a tool change to `None`.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Abandon();

    /// 🧩 What is being placed, and how.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    SketchSubject   Subject() const { return Placing; }
    PlacementMethod Method() const  { return PlacingMethod; }

    /// 🧩 Whether a drawing tool is held at all.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Standing() const { return Placing != SketchSubject::None; }

    /// 🧩 Whether the placed shape is construction geometry.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Construction() const { return ConstructionDeclared; }

    /// 🧩 Whether a shape that closes should be sealed as a filled region.
    /// note 📝 Tool state, like construction, so it survives from one shape to the next: an artist
    ///       drawing outlines to extrude sets it once rather than for every shape.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ClosedProfile() const { return ClosedProfileDeclared; }

    /// 🧩 Chooses whether closed shapes seal as regions.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareClosedProfile(bool Closed) { ClosedProfileDeclared = Closed; }

    /// 🧩 The anchors taken so far, in the order they were taken.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<SpatialPoint>&        Anchors() const { return Taken; }
    const std::vector<SketchSnapPlacement>& Placements() const { return TakenPlacements; }

    /// 🧩 How many anchors this placement still needs before it may complete.
    /// out   Remaining  [-]  zero once enough are taken, including while a `Terminated` curve grows
    /// note  📝 For the status text drawn under the pointer: "Polyline — 2 taken, double-press to end".
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t Remaining() const;

    /// 🧩 Where the pointer is now, for previewing the shape before the next contact.
    /// out   Standing  [-]  false until the first `Hover` after a `Declare`
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool                       HoverStanding() const { return HoverTaken; }
    const SpatialPoint&        HoverPosition() const { return HoverAt; }
    const SketchSnapPlacement& HoverPlacement() const { return HoverSnap; }

    /// 🧩 Turns the wheel while a placement is in progress, changing how many sides a polygon has.
    /// in    Notches  [-]  wheel travel; positive adds sides
    /// out   -        [-]  whether the wheel was consumed, so the caller does not also zoom with it
    /// note  🔴 A POLYGON IS A CIRCLE WITH A SIDE COUNT, AND IS DRAWN LIKE ONE. Centre, drag for the
    ///        circumradius, WHEEL for the resolution, Enter to commit — the count is a property of the
    ///        placement in progress, not a mode chosen beforehand. The commit read a hardcoded six.
    /// note  ⚠️ Refused unless a polygon is being placed, so the wheel keeps zooming for every other tool.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Resolve(float Notches);

    /// 🧩 How many sides the polygon being placed will have.
    /// note  📝 Meaningless for every other subject, and left at its default there.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t Resolution() const { return SideCount; }

    /// 🧩 Whether a slot's spine is locked, so the pointer now measures its thickness.
    /// out   -  [-]  false while the spine is still being clicked out, true after the accepting press
    /// note  🔴 THE PREVIEW CANNOT INFER THIS FROM THE ANCHOR COUNT and it tried to, because the phase
    ///        was held privately here and never published. A slot is drawn in two phases through one
    ///        anchor list, so three anchors mean "a three-point spine being drawn" before the accepting
    ///        press and "a two-point spine plus a thickness" after it. Counting alone therefore drew a
    ///        thick slot over a spine still being clicked out, and left a locked spine previewing as a
    ///        bare polyline with no thickness at all — the artist pressed Enter and nothing changed.
    /// note  📝 Meaningless for every other subject, and false there.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool SpineFinished() const { return SpineFinishedDeclared; }

private:

    SketchSubject                    Placing              = SketchSubject::None;     // [-]
    PlacementMethod                  PlacingMethod        = PlacementMethod::Extent; // [-]
    std::vector<SpatialPoint>        Taken                = {};                      // [-] - anchors, in order
    std::vector<SketchSnapPlacement> TakenPlacements      = {};                      // [-] - what each landed on
    SpatialPoint                     HoverAt              = {};                      // [-]
    SketchSnapPlacement              HoverSnap            = {};                      // [-]
    bool                             HoverTaken           = false;                   // [-]
    bool                             ConstructionDeclared = false;                   // [-]
    // 📝 Filled by default: a closed shape reading as a region is what an artist expects, and it is
    //    the state the extrude and loft operations need.
    bool                             ClosedProfileDeclared = true;                   // [-]
    std::uint32_t                    SideCount            = PolygonSideDefault;      // [-] - polygon only
    bool                             SpineFinishedDeclared = false;                  // [-] - polyline slot only
};

/// 🧩 A whole placement in progress, previewed as the shape it will commit to.
/// in    Placing    [-]  the tool holding the anchors, the hover and the phase it has reached
/// in    Basis      [-]  the active plane, so the slot thickens in it and not about world Up
/// out   Delivered  [-]  cleared, then filled
/// note  🔴 THE PREVIEW MUST BE TOLD THE PHASE, NOT LEFT TO GUESS IT. Passing anchors alone is what
///        made a slot undrawable: the resolver saw a list of points with no way to know whether the
///        last one was another spine click or the pointer setting the thickness.
/// cost  🚩
/// tag   api, nonthrowing
void ResolvePlacementCurves(const SketchPlacement& Placing,
                            const SpatialBasis& Basis,
                            std::vector<CurveSpecification>& Delivered);

}   // namespace Slate
