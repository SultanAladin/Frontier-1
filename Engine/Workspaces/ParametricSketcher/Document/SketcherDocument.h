/*====================================================================================================================================
                                                          SOLIDARCDOCUMENT.H
====================================================================================================================================*/
// 🧩 Sketcher document: feature tree, atomic commands with undo / redo, selection by persistent name, regeneration

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlane.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherReferenceSurface.h"
#include "Engine/GeometrySpace/Parametric/Drawing/DrawingSheet.h"
#include "Engine/GeometrySpace/Parametric/Feature/FeatureTree.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceOperations.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchConstraining.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchCorner.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchEditing.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchRailDeform.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchRegion.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchTrim.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Construct catalogue tiles. Every tile authors an editable sketch + sweep pair, never a frozen primitive body.
enum class SketcherConstructCategory : uint8_t
{
    Block    = 0,                                                                            // Rectangle sketch on XY, extruded along +z
    Cylinder = 1,                                                                            // Circle sketch on XY, extruded along +z
    Sphere   = 2,                                                                            // Half disc on XZ, revolved about its diameter
    Torus    = 3,                                                                            // Circle on XZ, revolved about a construction axis
    Cone     = 4,                                                                            // Right triangle on XZ, revolved about its upright leg
    Wedge    = 5,                                                                            // Right triangle on XZ (high side at x = X), extruded along +y
    Prism    = 6,                                                                            // Regular polygon on XY, extruded along +z
    Tube     = 7,                                                                            // Two concentric circles on XY (annulus), extruded along +z
};

// 📝 Drawing tools on the construction plane (Plasticity: Shift+A line, Shift+Q corner rectangle, Shift+W centre circle,
//    Shift+S curve; arc and slot from the Draw card). Each finished drawing becomes one editable sketch step.
enum class SketcherDrawingCategory : uint8_t
{
    Line      = 0,                                                                           // Polyline through the clicks (closes on its first point)
    Rectangle = 1,                                                                           // Two corners, or centre + corner
    Circle    = 2,                                                                           // Centre + rim, or two diameter ends
    Arc       = 3,                                                                           // Start, end, a point on the arc
    Slot      = 4,                                                                           // Two cap centres + a point on the rim
    Curve     = 5,                                                                           // Spline through / on the clicks (form 1 … 4)
    Polygon   = 6,                                                                           // Regular polygon: centre + a vertex (Sides)
};

enum class SketcherSelectionCategory : uint8_t
{
    Face = 0,                                                                                // A face (persistent name)
    Edge = 1,                                                                                // An edge (persistent name)
    Body = 2,                                                                                // A whole body (key = creating step label)
    Vertex = 3,                                                                              // A body vertex (persistent name; mode 1)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Only the fields of the chosen kind are read. Position: block / wedge corner (minimum x, y, z), cylinder / cone / prism /
//    tube base centre, sphere / torus centre. Cone: Radius at the base, apex Height above it. Wedge: Height at x = X falling to
//    0 at x = X + Width.
//    PlaneCondition (drawn on a body face, oriented to it): Position and the axes are in the face frame — x / y on the face,
//    z (and every Height) along its outward normal — so the solid stands on the face whatever its tilt.
struct SketcherConstructRequest
{
    SketcherConstructCategory        Kind        = SketcherConstructCategory::Block;                 // [-]   - Tile
    GeometrySpace::Parametric::FeatureCombination Combination = GeometrySpace::Parametric::FeatureCombination::Union;           // [-]   - With the target body (first body: New; Separate = new body)
    std::string                  Target      = {};                                           // [-]   - Body key the Boolean acts on (empty: the active body)
    GeometrySpace::Parametric::Vector3            Position    = { 0.0, 0.0, 0.0 };                             // [len] - Placement
    double                       Width       = 2.0;                                          // [len] - Block x extent
    double                       Depth       = 1.5;                                          // [len] - Block y extent
    double                       Height      = 1.0;                                          // [len] - Block / cylinder z extent
    double                       Radius      = 0.5;                                          // [len] - Cylinder / sphere radius
    double                       Major       = 1.5;                                          // [len] - Torus axis → tube centre
    double                       Minor       = 0.4;                                          // [len] - Torus tube radius
    int                          Axis        = 2;                                            // [-]   - Cylinder axis: 0 x · 1 y · 2 z (Height runs along +axis)
    double                       Inner       = 0.3;                                          // [len] - Tube bore radius (< Radius)
    int                          Sides       = 6;                                            // [-]   - Prism sides (3 … 64)
    double                       Angle       = 0.0;                                          // [rad] - Prism: direction of the first vertex from the centre
    bool                         PlaneCondition = false;                                     // [-]   - Built on Plane (a body face) instead of the world floor
    GeometrySpace::Parametric::OrthonormalBasis   Plane       = {};                                           // [len] - Face frame (origin, x̂, ŷ, outward n̂) when PlaneCondition
    double                       Start       = 0.0;                                          // [len] - ≤ 0: Union extrudes start this far below Plane (curved face)
};

// 📝 Points are construction-plane coordinates (GridPlane 0 XY, 1 XZ, 2 YZ; see SketcherSketchPlane). Only the fields of
//    the kind are read; a Line or Curve is closed when ClosedCondition (its last point joins its first).
struct SketcherDrawingRequest
{
    SketcherDrawingCategory            Kind            = SketcherDrawingCategory::Line;              // [-]   - Tool
    int                            GridPlane       = 0;                                      // [-]   - 0 XY · 1 XZ · 2 YZ
    std::vector<GeometrySpace::Parametric::Vector2> Points          = {};                                     // [len] - Clicks in plane coordinates
    bool                           ClosedCondition = false;                                  // [-]   - Line / curve: close the chain
    bool                           CentreCondition = false;                                  // [-]   - Rectangle: centre + corner · circle: centre + rim (else two diameter ends)
    GeometrySpace::Parametric::SketchSplineForm     Form            = GeometrySpace::Parametric::SketchSplineForm::CubicSpline; // [-]  - Curve form (Plasticity 1 … 4)
    int                            Sides           = 6;                                      // [-]   - Polygon sides (3 … 64)
    bool                           PlaneCondition  = false;                                  // [-]   - Plane below replaces GridPlane (drawn on a body face)
    GeometrySpace::Parametric::OrthonormalBasis     Plane           = {};                                     // [len] - Face plane (origin, x̂, ŷ, n̂) when PlaneCondition
    bool                           FillCondition   = false;                                  // [-]   - Closed loops start filled (Plasticity: unfilled until F / Fill)
};

// 📝 Selection is stored as persistent names, so it survives regeneration whenever the names still resolve.
struct SketcherSelection
{
    std::vector<std::string> Faces = {};                                                     // [-] - Selected face names
    std::vector<std::string> Edges = {};                                                     // [-] - Selected edge names
    std::vector<std::string> Curves = {};                                                    // [-] - Selected SpaceCurve labels, in pick order (loft order)
    std::vector<std::string> Bodies = {};                                                    // [-] - Selected body keys, in pick order (first = Boolean target)
    SketcherSelectionCategory    LastKind = SketcherSelectionCategory::Face;                          // [-] - Kind of the most recent pick (active operand)
    std::string              PickedBody = {};                                                // [-] - Body the faces / edges were picked on (pieces of one cut share names)
    std::vector<std::string> Vertices = {};                                                  // [-] - Selected body vertex names (mode 1, reshape)
};

struct SketcherCutRequest
{
    std::string       Target    = {};                                                        // [-] - Body key (empty = active body)
    std::string       Cutter    = {};                                                        // [-] - Sketch step or SpaceCurve label
    GeometrySpace::Parametric::Vector3 Direction = { 0.0, 0.0, -1.0 };                                        // [-] - SpaceCurve cutters: projection direction
};

// 📝 Plasticity fillet-curve on sketch corners: the picked corner points of one sketch (mode 1), or every corner of each
//    listed sketch when Points is empty.
struct SketcherCornerRequest
{
    std::vector<std::string>              Sketches = {};                                     // [-]   - Sketch step labels
    std::vector<int>                      Points   = {};                                     // [-]   - Corner points (one sketch only; empty: all)
    double                                Size     = 0.25;                                   // [len] - Fillet radius / chamfer setback
    GeometrySpace::Parametric::SketchCornerShape Shape    = GeometrySpace::Parametric::SketchCornerShape::Fillet; // [-] - Fillet or chamfer
};

// 📝 Pick is in the sketch's plane coordinates (the pointer met that plane).
struct SketcherTrimRequest
{
    std::string       Sketch = {};                                                           // [-]   - Sketch step label
    int               Curve  = -1;                                                           // [-]   - Curve index in that sketch
    GeometrySpace::Parametric::Vector2 Pick   = {};                                                           // [len] - Where the pointer met the sketch plane
    bool              SplitCondition = false;                                                // [-]   - Cut (break at Pick) instead of trim
    bool              InsertCondition = false;                                               // [-]   - Insert point (shape kept) instead of cut / trim
};

// 📝 Flow along curve: Rail is the sketch the shapes follow (the first pick), Shapes the coplanar sketches bent along it.
struct SketcherRailDeformRequest
{
    std::string                            Rail     = {};                                    // [-] - Rail sketch step label
    std::vector<std::string>               Shapes   = {};                                    // [-] - Shape sketch step labels
    GeometrySpace::Parametric::SketchRailDeformSettings Settings = {};                                    // [-] - Axis, stretch, direction
};

// 📝 Wrap around curve: the shapes (any planes) laid round the surface the rail sweeps along its normal (a circle: a can).
struct SketcherWrapRequest
{
    std::string                            Rail     = {};                                    // [-] - Rail sketch step label (a circle: the can)
    std::vector<std::string>               Shapes   = {};                                    // [-] - Shape sketch step labels
    GeometrySpace::Parametric::SketchWrapSettings Settings = {};                                    // [-] - Wrap / project, direction, axis
};

// 📝 Fill tool: the closed region under Pick (coordinates of Sketch's plane) among Sketch and every shown sketch coplanar
//    with it.
struct SketcherRegionRequest
{
    std::string                 Sketch = {};                                                 // [-]   - Sketch step whose plane carries Pick
    GeometrySpace::Parametric::Vector2 Pick   = {};                                                 // [len] - Pointer on that plane
};

struct SketcherRegionPreview
{
    GeometrySpace::Parametric::SketchRegion     Region      = {};                                   // [-] - Loops in Plane coordinates
    GeometrySpace::Parametric::OrthonormalBasis Plane       = {};                                   // [-] - Plane of Region (elevation applied)
    std::vector<std::string>             Sources     = {};                                   // [-] - Sketch label per input segment
    std::string                          WholeSketch = {};                                   // [-] - Region is exactly this sketch's own loops
    std::string                          Refusal     = {};                                   // [-] - Why no region
};

// Plasticity H family: H hides the selected bodies, Shift+H every other body, Alt+H shows all, Ctrl+H swaps shown and hidden.
enum class SketcherVisibilityVerb : uint8_t
{
    HideSelected   = 0,
    HideUnselected = 1,
    UnhideAll      = 2,
    InvertHidden   = 3,
};

// 📝 Variable when EndSize is set: Size at each edge's anchor end, EndSize at its other end. The anchor is the edge's
//    start vertex on the current body, or its end vertex when ReversedCondition (Plasticity F flips it); it is stored as
//    a persistent vertex name, so a regeneration that reverses the edge keeps each size at its corner.
struct SketcherBlendRequest
{
    GeometrySpace::Parametric::BlendProfile Shape             = GeometrySpace::Parametric::BlendProfile::Fillet;               // [-]   - Cross-section
    double                 Size              = 0.25;                                         // [len] - Radius / setback (at the anchors)
    std::optional<double>  EndSize           = {};                                           // [len] - Size at the far ends (empty = constant)
    bool                   ReversedCondition = false;                                        // [-]   - Anchor at each edge's end vertex
    std::vector<std::pair<std::string, double>> VertexSizes = {};                            // [len] - Handles: size per persistent vertex name (unlisted: Size)
};

// 📝 Bevel handles (Plasticity's fillet gizmo): one per distinct vertex of the picked edges — dragged, it gives that
//    corner its own size (variable blend) — plus the uniform handle at the middle of the first edge (Vertex empty).
//    Direction is the outward bisector of the two faces' normals there; Edges counts the picked edges at the vertex.
struct SketcherBlendHandle
{
    std::string                  Vertex    = {};                                             // [-]   - Persistent vertex name (empty: uniform handle)
    GeometrySpace::Parametric::Vector3  Position  = {};                                             // [len] - Vertex / mid-edge point
    GeometrySpace::Parametric::Vector3  Direction = {};                                             // [-]   - Unit outward bisector
    int                          Edges     = 0;                                              // [-]   - Picked edges meeting there
    double                       Spread    = 0.0;                                            // [-]   - Mean Direction · face normal (cos of half the turn)
};

// 📝 What the picked edges allow: Span is the shortest length that bounds a blend on them (each open edge's length,
//    a closed edge's radius of curvature at its middle, and the other edges of both faces at its ends); Fitted is a
//    round size (1 · 2 · 5 steps) of about 15 % of it, which a bevel can start at and show at once. Span is not a limit:
//    a blend past it runs on over the next face (overflow). The reaches are: a setback of the body's diagonal leaves
//    nothing; a fillet of radius r sets back r · tan(φ / 2) at an edge turning φ (90°: r; 135° faces: 0.41 r).
struct SketcherBlendFit
{
    double      Span    = 0.0;                                                               // [len] - Bounding length (0: nothing to measure)
    double      Fitted  = 0.0;                                                               // [len] - Suggested start size
    double      FilletReach  = 0.0;                                                          // [len] - Radius past which a fillet eats the body
    double      ChamferReach = 0.0;                                                          // [len] - Setback past which a chamfer eats the body
    std::vector<double> FilletMerges  = {};                                                  // [len] - Radii where rails meet a neighbour (sorted)
    std::vector<double> ChamferMerges = {};                                                  // [len] - Chamfer sizes where rails meet a neighbour (sorted)
    std::string Refusal = {};                                                                // [-]   - Why nothing was measured
};

struct SketcherCommandOutcome
{
    bool        AcceptedCondition = false;                                                   // [-]  - Document changed
    std::string Label             = {};                                                      // [-]  - History caption ("Block1", "Fillet r 0.2")
    std::string Refusal           = {};                                                      // [-]  - Why not, when refused (document untouched)
    double      Milliseconds      = 0.0;                                                     // [ms] - Command + regeneration time
};

// 📝 Figures of one body (Inspector body list, Outliner).
struct SketcherBodyFigures
{
    std::string Key            = {};                                                         // [-]    - Creating step label ("Extrude#2")
    std::string Caption        = {};                                                         // [-]    - Shown name ("Block1")
    bool        SoundCondition = false;                                                      // [-]    - Audit found nothing
    bool        SheetCondition = false;                                                      // [-]    - Open sheet body (a lofted panel)
    int         Faces          = 0;                                                          // [-]    - Face count
    int         Edges          = 0;                                                          // [-]    - Edge count
    int         Vertices       = 0;                                                          // [-]    - Vertex count
    int         Genus          = -1;                                                         // [-]    - Euler–Poincaré genus
    double      Volume         = 0.0;                                                        // [len³] - Enclosed volume (0 for a sheet)
};

// 📝 Part figures shown by the Inspector (recomputed on every regeneration): totals over every body, plus each body.
struct SketcherBodySummary
{
    bool   PresentCondition = false;                                                         // [-]     - At least one body exists
    bool   SoundCondition   = false;                                                         // [-]     - Every body audits sound
    int    Faces            = 0;                                                             // [-]     - Face count (all bodies)
    int    Edges            = 0;                                                             // [-]     - Edge count (all bodies)
    int    Vertices         = 0;                                                             // [-]     - Vertex count (all bodies)
    int    Genus            = -1;                                                            // [-]     - Sum of the bodies' genera
    double Volume           = 0.0;                                                           // [len³]  - Sum of the solid bodies' volumes
    bool   SheetCondition   = false;                                                         // [-]     - Some body is an open sheet (a lofted panel)
    double RegenerationMs   = 0.0;                                                           // [ms]    - Last replay
    std::vector<SketcherBodyFigures> Bodies = {};                                            // [-]     - One per body, in creation order
};

// 📝 One undo entry: the whole tree before the command (trees are small; replay rebuilds the body exactly). Recent entries
//    also keep the replay checkpoints of that moment (shared, immutable), so undo / redo / a rolled-back command restore
//    every step instead of replaying it.
// 📝 Persistent associative CAD dimension annotation in 3D model space: Linear (aligned or ΔX/ΔY/ΔZ), Radius (R),
//    Diameter (⌀), or Angular (°). When FirstName / SecondName reference persistent B-rep topology or curves,
//    RefreshSketcherDimensions recomputes exact endpoints / radii / angles on every regeneration.
enum class SketcherDimensionCategory : uint8_t
{
    Linear   = 0,                                                                            // Point-to-point, edge length, clearance, or ΔX/ΔY/ΔZ
    Radius   = 1,                                                                            // R callout on arc, circle, cylinder, or sphere
    Diameter = 2,                                                                            // ⌀ callout on circle, cylinder, or sphere
    Angular  = 3,                                                                            // ° arc between two lines or planar faces
};

struct SketcherDimension
{
    std::string                 Id               = {};                                       // [-]     - Unique identifier ("Dim1", "Dim2", …)
    SketcherDimensionCategory       Kind             = SketcherDimensionCategory::Linear;            // [-]     - Linear, Radius, Diameter, Angular
    SketcherSelectionCategory       FirstKind        = SketcherSelectionCategory::Vertex;            // [-]     - First associative entity kind
    std::string                 FirstName        = {};                                       // [-]     - Persistent name (empty: explicit point)
    SketcherSelectionCategory       SecondKind       = SketcherSelectionCategory::Vertex;            // [-]     - Second associative entity kind
    std::string                 SecondName       = {};                                       // [-]     - Persistent name (empty: explicit / single)
    GeometrySpace::Parametric::Vector3 FirstPoint       = {};                                       // [len]   - Start point (or centre for R / ⌀ / ∠)
    GeometrySpace::Parametric::Vector3 SecondPoint      = {};                                       // [len]   - End point (or rim point for R / ⌀)
    GeometrySpace::Parametric::Vector3 PlaneNormal      = { 0.0, 0.0, 1.0 };                        // [-]     - Dimension plane normal
    GeometrySpace::Parametric::Vector3 Direction        = { 1.0, 0.0, 0.0 };                        // [-]     - Secondary ray / reference axis
    double                      Offset           = 0.35;                                     // [len]   - Witness-line offset in dimension plane / slide axis
    double                      Value            = 0.0;                                      // [m/deg] - Exact measured value
    int                         AxisFilter       = -1;                                       // [-]     - -1 aligned, 0 ΔX, 1 ΔY, 2 ΔZ
    int                         SlideAxis        = -1;                                       // [-]     - -1 plane-perpendicular, 0 slide X, 1 slide Y, 2 slide Z
    bool                        VisibleCondition = true;                                     // [-]     - Drawn in the 3D viewport
    std::string                 Note             = {};                                       // [-]     - Optional prefix / tag ("W", "D", "H")
};

struct SketcherHistoryEntry
{
    GeometrySpace::Parametric::FeatureTree              Tree     = {};                                        // [-] - Tree before the command
    std::map<std::string, std::string> Captions = {};                                        // [-] - Step captions before the command
    std::string                        Label    = {};                                        // [-] - Command caption
    SketcherAppearance                 Appearance = {};                                      // [-] - Matcaps before the command
    bool                               AppearanceCondition = false;                          // [-] - Matcap-only entry: restore without regenerating
    std::shared_ptr<const GeometrySpace::Parametric::RegenerationCache> Replay = {};                          // [-] - Checkpoints then (recent entries only)
    std::shared_ptr<const GeometrySpace::Parametric::ReplayCheckpoint>  SummaryCheckpoint = {};               // [-] - Checkpoint the summary below was measured on
    SketcherBodySummary                                Summary = {};                         // [-] - Body figures then (reused when every step restores)
    std::vector<SketcherDimension>                     Dimensions = {};                      // [-] - Dimension lines before the command
};

// 📝 Section plane choice (Section card, Alt+S). World planes pass through Offset on their axis; the Face plane is the
//    selected planar face moved Offset along its outward normal (negative = into the body). The kept side is the
//    plane's +normal side, as in the kernel; defaults remove the half that faces the isometric camera
//    (X: n = −x̂, Y: n = +ŷ, Z: n = −ẑ, Face: n = −outward). Flipped negates n.
enum class SketcherSectionAxis : uint8_t
{
    X    = 0,                                                                                // Plane x = Offset
    Y    = 1,                                                                                // Plane y = Offset
    Z    = 2,                                                                                // Plane z = Offset
    Face = 3,                                                                                // Selected planar face, offset along its normal
};

struct SketcherSectionRequest
{
    SketcherSectionAxis Axis    = SketcherSectionAxis::X;                                    // [-]   - Plane family
    double              Offset  = 0.0;                                                       // [len] - Coordinate / normal offset
    bool                Flipped = false;                                                     // [-]   - Keep the other half
    std::string         Face    = {};                                                        // [-]   - Face name (Axis = Face)
};

// 📝 Title block fields; an empty Title prints the document title.
struct SketcherTitleFields
{
    std::string Title         = {};                                                          // [-] - Part name (empty: document title)
    std::string DrawingNumber = "DWG-0001";                                                  // [-] - Identifier
    std::string Author        = {};                                                          // [-] - Drawn by
    std::string Date          = {};                                                          // [-] - Issue date
    std::string Material      = {};                                                          // [-] - Material
    std::string Revision      = "A";                                                         // [-] - Revision letter
    std::string Company       = "Frontier";                                                  // [-] - Organisation
};

// 📝 Saved with the document ([DrawingSheet], only when not the defaults), not in the undo history — like image planes.
//    First angle by default (ISO 5456-2, SANS 10111); Scale 0 picks the largest standard scale that fits. Section A-A
//    cuts along a Section-card plane (Alt+S semantics: the +normal side is kept and seen from the removed side).
struct SketcherDrawingSheetSettings
{
    GeometrySpace::Parametric::DrawingPaperSize       Paper               = GeometrySpace::Parametric::DrawingPaperSize::A3;    // [-] - ISO 216, landscape
    GeometrySpace::Parametric::DrawingProjectionAngle Angle               = GeometrySpace::Parametric::DrawingProjectionAngle::First; // [-] - Layout and symbol
    double                           Scale               = 0.0;                               // [-] - Paper : model, 0 = automatic
    bool                             IsometricEnabled    = true;                              // [-] - Isometric in the free quadrant
    bool                             SectionEnabled      = false;                             // [-] - Section A-A instead of the isometric
    SketcherSectionRequest           Section             = { SketcherSectionAxis::Y, 0.0, false, {} }; // [-] - Plane of section A-A
    bool                             HiddenLinesEnabled  = true;                              // [-] - Dashed hidden lines
    bool                             TangentEdgesEnabled = true;                              // [-] - Thin tangent edges
    bool                             DimensionsEnabled   = true;                              // [-] - Automatic dimensions + centre marks
    SketcherTitleFields              Title               = {};                                // [-] - Title block
};

struct SketcherDocument
{
    std::string                                Title        = "Untitled";                    // [-] - Document caption
    GeometrySpace::Parametric::FeatureTree                      Tree         = {};                            // [-] - Authored feature tree
    std::map<std::string, std::string>         Captions     = {};                            // [-] - Step label → caption ("Sketch#1" → "Block1")
    std::unique_ptr<GeometrySpace::Parametric::ModelPartition>  Partition    = {};                            // [-] - Records of the last regeneration
    GeometrySpace::Parametric::RegenerationOutcome              Regeneration = {};                            // [-] - Last replay (body, names, step reports)
    SketcherBodySummary                        Summary      = {};                            // [-] - Figures of the last body
    SketcherSelection                          Selection    = {};                            // [-] - Current selection (names)
    std::vector<SketcherHistoryEntry>          UndoEntries  = {};                            // [-] - Most recent last
    std::vector<SketcherHistoryEntry>          RedoEntries  = {};                            // [-] - Most recent last
    uint64_t                                   Revision          = 0;                        // [-] - Bumped by every regeneration (scene cache key)
    std::vector<SketcherImagePlane>            ImagePlanes       = {};                       // [-] - Blueprint references (saved, not in the history)
    std::vector<SketcherReferenceSurface>      ReferenceSurfaces = {};                       // [-] - GPU-resident high-poly Blender reference surfaces
    uint64_t                                   ReferenceRevision = 0;                        // [-] - Bumped when ReferenceSurfaces geometry changes
    SketcherAppearance                         Appearance        = {};                       // [-] - Body / face matcaps (saved, undoable)
    SketcherDrawingSheetSettings               DrawingSheet = {};                            // [-] - Alt+D sheet settings (saved, not in the history)
    std::vector<SketcherDimension>             Dimensions   = {};                            // [-] - Persistent 3D CAD dimension annotations (saved, undoable)
    GeometrySpace::Parametric::RegenerationCache                Replay       = {};                            // [-] - Step checkpoints of the last replay (incremental regeneration)
    std::shared_ptr<const GeometrySpace::Parametric::ReplayCheckpoint> SummaryCheckpoint = {};                // [-] - Final checkpoint Summary was measured on (figures reused when a
                                                                                             //       regeneration restores every step from it: undo / redo / clone)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Empty document (no steps, no body).
struct VehicleCurveFile;

[[nodiscard]] SketcherDocument CreateSketcherDocument(const std::string& Title = "Untitled");

// Independent copy (tree, captions, selection; history not copied), regenerated into its own partition. The replay
// checkpoints are shared, so the copy restores every step instead of replaying the tree (preview staging is instant).
[[nodiscard]] SketcherDocument CloneSketcherDocument(const SketcherDocument& Source);

// 💡 Commands are atomic: the tree is snapshotted, changed and regenerated; if the step the command added is refused by
//    the modeller (or the command cannot apply), the snapshot is restored and the refusal is returned. Parameter edits
//    that make a *later* step refuse are kept — the tree marks the step and keeps the previous body (plan § 3).
[[nodiscard]] SketcherCommandOutcome ConstructSketcherPrimitive(SketcherDocument& Document, const SketcherConstructRequest& Request);
[[nodiscard]] SketcherCommandOutcome BlendSketcherSelection(SketcherDocument& Document, GeometrySpace::Parametric::BlendProfile Shape, double Size);
[[nodiscard]] SketcherCommandOutcome BlendSketcherSelection(SketcherDocument& Document, const SketcherBlendRequest& Request);
// CAD vertex bevel: resolve every topological edge incident to the selected persistent body vertices.
[[nodiscard]] std::vector<std::string> ResolveSketcherVertexBlendEdges(const SketcherDocument& Document, const std::vector<std::string>& Vertices);
[[nodiscard]] std::vector<SketcherBlendHandle> MeasureSketcherBlendHandles(const SketcherDocument& Document, const std::vector<std::string>& Edges);
[[nodiscard]] SketcherBlendFit MeasureSketcherBlendFit(const SketcherDocument& Document, const std::vector<std::string>& Edges);
// Direction (gizmo): Tweak moves along it by Amount, Rotate turns about it (through each face centre) by Amount [rad].
[[nodiscard]] SketcherCommandOutcome ChangeSketcherSelection(SketcherDocument& Document, GeometrySpace::Parametric::FaceOperationCategory Operation, double Amount,
                                                             std::optional<GeometrySpace::Parametric::Vector3> Direction = std::nullopt);
// Inset (Plasticity O on faces · I): border ring Distance wide; the inner face moves Depth along the outward normal (− pocket).
// 💡 Direct edit of the picked vertices / edges (mode 1 / 2 gizmo): a FaceOperation Reshape step carrying them by Map; the
//    faces at them are re-fitted (planes when still flat, or bilinear NURBS patches for four-sided faces across single or
//    repeated vertex edits). Like face changes, it goes before the same body's trailing fillets so they re-solve on the new shape.
[[nodiscard]] SketcherCommandOutcome ReshapeSketcherSelection(SketcherDocument& Document, const GeometrySpace::Parametric::AffineMapping& Map, const std::string& Verb);
[[nodiscard]] SketcherCommandOutcome InsetSketcherSelection(SketcherDocument& Document, double Distance, double Depth);
[[nodiscard]] SketcherCommandOutcome AssignSketcherParameter(SketcherDocument& Document, const std::string& Name, double Value);
[[nodiscard]] SketcherCommandOutcome DeleteSketcherStep(SketcherDocument& Document, const std::string& Label);
[[nodiscard]] SketcherCommandOutcome ReplaceSketcherTree(SketcherDocument& Document, GeometrySpace::Parametric::FeatureTree Tree,
                                                         std::map<std::string, std::string> Captions, const std::string& Label);

// 💡 Drawing → sketch: the request becomes one Sketch step on its construction plane (caption "Line1", "Curve2", …).
//    ComposeSketcherSketch builds the same sketch without touching a document (live preview); a degenerate drawing
//    (zero size, collinear arc, coincident spline points) is refused with the reason.
[[nodiscard]] SketcherCommandOutcome DrawSketcherSketch(SketcherDocument& Document, const SketcherDrawingRequest& Request);
[[nodiscard]] std::string            ComposeSketcherSketch(const SketcherDrawingRequest& Request, GeometrySpace::Parametric::SketchModel& Sketch);

// Extrude a sketch step's profile by Distance along its plane normal; the first body is always New, a later one must
// combine (Union, Difference, Intersection). Refused (document untouched) when the profile or the Boolean refuses.
// Start (opposite the distance's sign) moves the start of the sweep behind the sketch plane: a profile on the tangent plane
// of a curved face (MeasureSketcherProfileSink) then reaches into the body, so a Union closes instead of touching along a
// line; a cut pushed into the face the sketch lies on starts a hair outside it (no coincident caps). FillCondition: a
// Difference / Intersection fills an unfilled sketch first (same undo entry) instead of refusing its walls.
[[nodiscard]] SketcherCommandOutcome ExtrudeSketcherSketch(SketcherDocument& Document, const std::string& SketchLabel, double Distance,
                                                           GeometrySpace::Parametric::FeatureCombination Combination, const std::string& Target = {},
                                                           double Start = 0.0, bool FillCondition = false);

// Plasticity Q (command:boolean): Combine steps merging each tool body into Target (Union / Difference / Intersection);
// the tools are consumed. One undo entry; refused (document untouched) unless every Boolean succeeds.
[[nodiscard]] SketcherCommandOutcome CombineSketcherBodies(SketcherDocument& Document, const std::string& Target,
                                                           const std::vector<std::string>& Tools, GeometrySpace::Parametric::FeatureCombination Combination);

// 💡 Plasticity C (command:cut): one Cut step splitting Target (empty = the active body) by Cutter, which is a sketch step
//    (its closed profile cuts out its interior, an open chain splits along itself) or a SpaceCurve projected along
//    Direction (the view axis). The outside piece keeps its key; the inside piece becomes body "Cut#n" (caption Cut1 …).
//    Refused (document untouched) when the cut misses or cannot be closed.
[[nodiscard]] SketcherCommandOutcome CutSketcherBody(SketcherDocument& Document, const SketcherCutRequest& Request);

// 💡 Plasticity T (command:trim): the piece of the curve under Pick, between its crossings with the sketch's other curves
//    and with every visible coplanar sketch, comes off (GeometrySpace::Parametric::TrimSketchCurve); one undo entry. A sketch left
//    without curves is deleted when nothing uses it. When a later step reads the sketch (an extrude, a loft section …)
//    and the trimmed sketch would break it, the trim detaches (Plasticity: curves are free of the solids made from them):
//    the original sketch stays for its body, hidden, and the trimmed curves become a new sketch step at the end. Refused
//    (document untouched) only when the trim itself refuses.
[[nodiscard]] SketcherCommandOutcome TrimSketcherSketch(SketcherDocument& Document, const SketcherTrimRequest& Request);
//    With SplitCondition the curve breaks at Pick instead (Plasticity sketch Cut, GeometrySpace::Parametric::SplitSketchCurve):
//    nothing comes off, a line / arc becomes two curves, a circle two half arcs.
//    With InsertCondition a point is added at Pick and the shape stays (GeometrySpace::Parametric::InsertSketchCurvePoint).
// The same trim on the solved sketch without touching the document (hover preview); Removed is in plane coordinates.
[[nodiscard]] GeometrySpace::Parametric::SketchTrimOutcome PreviewSketcherTrim(const SketcherDocument& Document, const SketcherTrimRequest& Request);

// 💡 Flow along curve (Rhino Flow): the shapes (joined in the first shape's plane) bent along the rail by
//    GeometrySpace::Parametric::FlowSketchModel, as a new sketch step "FlowN" in the rail's plane, picked; filled when the first
//    shape is filled and the result is still a closed profile. The shapes and the rail stay. One undo entry.
[[nodiscard]] SketcherCommandOutcome FlowSketcherSketches(SketcherDocument& Document, const SketcherRailDeformRequest& Request);

// 💡 Wrap around curve: GeometrySpace::Parametric::WrapSketchModels lays every shape curve round the rail's surface (one shared
//    centre, so letters stay apart); each wrapped curve becomes a 3D curve step ("WrapN", a SpaceCurve fitted to 10⁻⁶ of
//    the shape's width — 10⁻⁵ / 10⁻⁴ when the fitter's span limit cannot hold it), all picked. The shapes and the rail stay. One undo entry.
[[nodiscard]] SketcherCommandOutcome WrapSketcherSketches(SketcherDocument& Document, const SketcherWrapRequest& Request);

// 📝 Cut at a point (the Cut tool with Ctrl: a snapped crossing, end, midpoint or quarter point): every shown sketch curve
//    through Point breaks there — the two overlapping circles both. A circle breaks at Point and at its next crossing
//    (counter-clockwise) with another curve of its plane, so one click parts the lens from the rest; with no other
//    crossing it halves (the opposite point). Splines and curves touching Point only at an end are left alone.
struct SketcherPointCut
{
    int                                                   Curves  = 0;                       // [-]   - Curves that break
    std::vector<std::string>                              Sketches = {};                     // [-]   - Their sketch step labels
    std::vector<std::vector<GeometrySpace::Parametric::Vector3>> Lit     = {};                      // [len] - Those curves (world samples)
    std::vector<GeometrySpace::Parametric::Vector3>              Breaks  = {};                      // [len] - Every break point (world)
};

// Hover preview of the cut at Point (the document untouched).
[[nodiscard]] SketcherPointCut PreviewSketcherPointCut(const SketcherDocument& Document, const GeometrySpace::Parametric::Vector3& Point);

// The cut at Point: every sketch changes in place, one undo entry; refused (untouched) when no curve passes through it,
// or when a later step that reads a sketch refuses the cut curves.
[[nodiscard]] SketcherCommandOutcome CutSketcherSketchesAt(SketcherDocument& Document, const GeometrySpace::Parametric::Vector3& Point);

// 💡 Fillet / chamfer of 2D sketch corners (GeometrySpace::Parametric::CutSketchCorners): two lines meeting at a corner get a
//    tangent arc of radius Size, or a cut line Size back along both. The sketch changes in place, so an extrude or loft
//    made from it follows (parametric); one undo entry for all the sketches. Refused (document untouched) when a corner
//    cannot take the size, or when a step that reads the sketch refuses the rounded profile.
[[nodiscard]] SketcherCommandOutcome CutSketcherSketchCorners(SketcherDocument& Document, const SketcherCornerRequest& Request);

// Plasticity X on bodies: one Remove step per key, one undo entry; refused (document untouched) for an unknown key.
[[nodiscard]] SketcherCommandOutcome RemoveSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys);

// 💡 X / Delete on the picked faces. Fillet / chamfer faces (Blend#n.Strip(edge)) un-blend: their blend step lets go of the
//    source edges (an emptied blend step goes), so the sharp edge comes back exactly. Any other face is deleted and its
//    neighbours grow over the gap (FaceOperationCategory::Delete, heal). One undo entry; refused (document untouched) when the
//    solid cannot close or a later step refuses.
[[nodiscard]] SketcherCommandOutcome DeleteSketcherFaces(SketcherDocument& Document);

// 💡 X / Delete on the picked edges. A fillet / chamfer edge (one of its faces a Blend strip) un-blends that strip's source
//    edge; an edge between two faces on one carrier (an imprint line, a split seam) dissolves: a Heal step merges the
//    faces, accepted only when every picked edge is gone afterwards. Any other edge — two different surfaces meeting — is
//    refused: removing it cannot leave a closed solid (delete a face or the body instead).
[[nodiscard]] SketcherCommandOutcome DeleteSketcherEdges(SketcherDocument& Document);

// 💡 X / Delete on the picked body vertices: a vertex that only splits one straight or circular edge dissolves (Heal merges
//    the two edges), accepted only when every picked vertex is gone afterwards; a corner vertex is refused.
[[nodiscard]] SketcherCommandOutcome DeleteSketcherVertices(SketcherDocument& Document);

// 💡 X / Delete on sketches and space curves (Outliner rows or viewport picks). A curve no later step reads is deleted with
//    its parameters; one a later step reads (an extruded sketch, a loft section) leaves the scene — hidden, off the Outliner
//    (Appearance.RemovedCurves) — while that step keeps it, so the body stays. One undo entry.
[[nodiscard]] SketcherCommandOutcome DeleteSketcherCurves(SketcherDocument& Document, const std::vector<std::string>& Labels);

// 💡 X / Delete on mode 1 sketch points: spline controls are removed locally; other incident curves are erased, in place so a
//    step reading the sketch follows; when that step would refuse, the original stays for it (removed from the scene) and
//    the remaining curves become a free sketch. A sketch left without curves is deleted (or removed when read).
[[nodiscard]] SketcherCommandOutcome DeleteSketcherSketchPoints(SketcherDocument& Document, const std::string& Sketch, const std::vector<int>& Points);

// Plasticity G / R / S: one Transform step per body applying Map (x′ = X·x + Y·y + Z·z + Shift); Verb captions the steps
// ("Move", "Rotate", "Scale"). Names, selection and every later step that references the bodies follow them.
[[nodiscard]] SketcherCommandOutcome TransformSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys,
                                                             const GeometrySpace::Parametric::AffineMapping& Map, const std::string& Verb);

// 📝 G / R / S and the gizmo on sketches: a rigid Map carries each listed Sketch step's plane (the elevation rides
//    along its normal); a scale moves the solved points into the image plane, circle radii and dimensions scale with it
//    (S then X / Y stretches lines and splines, refusing arcs and circles: they would be ellipses 🚧). A pure mirror is
//    refused (🚧), as are labels that are not sketches. Later steps regenerate from the new sketch.
[[nodiscard]] SketcherCommandOutcome PlaceSketcherSketches(SketcherDocument& Document, const std::vector<std::string>& Labels,
                                                           const GeometrySpace::Parametric::AffineMapping& Map, const std::string& Verb);

// Shift+D / Ctrl+D on 2D objects: each listed sketch is copied as a new sketch step (next caption of its kind, e.g.
// Circle2) carried by Map as PlaceSketcherSketches does (scales too); the copies are selected.
[[nodiscard]] SketcherCommandOutcome DuplicateSketcherSketches(SketcherDocument& Document, const std::vector<std::string>& Labels,
                                                               const GeometrySpace::Parametric::AffineMapping& Map);

// Shift+D / Ctrl+V: one Duplicate step per body, placed by Map; each copy is a new body (key = its step label), the last
// one active, the copies selected. Source (paste) copies each body as it stood right after that step — the clipboard.
[[nodiscard]] SketcherCommandOutcome DuplicateSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys,
                                                             const GeometrySpace::Parametric::AffineMapping& Map, const std::string& Source = {});

// 💡 Knife (KitOps-style cutters, Shift+K): a closed sketch is filled and extruded from Top (≥ 0, a lift above its plane
//    so the cutter clears a flush face) down to Bottom (< Top, below the plane) and combined into Target (Difference cuts,
//    Union adds, Intersection keeps the overlap, Separate leaves the cutter as a body of its own). One undo entry with the
//    fill; refused (document untouched) when the profile is open or the Boolean refuses.
[[nodiscard]] SketcherCommandOutcome KnifeSketcherSketch(SketcherDocument& Document, const std::string& SketchLabel, double Top, double Bottom,
                                                         GeometrySpace::Parametric::FeatureCombination Combination, const std::string& Target);

// 📝 An insert (boolean library cutter): its canonical STEP text is imported as a body, placed by Map and combined into
//    Target (Separate: it stays a body of its own). Import + Transform + Combine in one undo entry.
struct SketcherInsertPlacement
{
    std::string                            Name        = {};                                 // [-] - Library caption ("Round hole")
    std::string                            Exchange    = {};                                 // [-] - STEP text (insert frame)
    GeometrySpace::Parametric::AffineMapping          Map         = {};                                 // [-] - Insert frame → world
    GeometrySpace::Parametric::FeatureCombination Combination = GeometrySpace::Parametric::FeatureCombination::Difference; // [-] - Into Target
    std::string                            Target      = {};                                 // [-] - Body cut / joined
};

// 2D cutter (Plasticity / Fusion split face): the sketch's outline split into the faces of Target it lies on — no cut,
// the volume unchanged; push / pull the new faces afterwards (face E). Open chains split edge to edge.
[[nodiscard]] SketcherCommandOutcome ImprintSketcherSketch(SketcherDocument& Document, const std::string& SketchLabel, const std::string& Target = {});

[[nodiscard]] SketcherCommandOutcome KnifeSketcherInsert(SketcherDocument& Document, const SketcherInsertPlacement& Placement);

// Knife with existing bodies: each tool is combined into Target; KeepCondition first duplicates the tools in place so
// the cutters survive (KitOps keeps its cutter objects). One undo entry.
[[nodiscard]] SketcherCommandOutcome KnifeSketcherBodies(SketcherDocument& Document, const std::string& Target, const std::vector<std::string>& Tools,
                                                         GeometrySpace::Parametric::FeatureCombination Combination, bool KeepCondition);

// 📝 Knife with one picked body placed by the pointer (KitOps cutter object): the Tool body is carried by Map (Transform
//    step; KeepCondition places a Duplicate instead so the cutter stays where it was) and combined into Target. One undo
//    entry; refused (document untouched) for a degenerate or mirrored Map, Tool == Target, or Separate.
[[nodiscard]] SketcherCommandOutcome KnifeSketcherPlacedBody(SketcherDocument& Document, const std::string& Target, const std::string& Tool,
                                                             const GeometrySpace::Parametric::AffineMapping& Map, GeometrySpace::Parametric::FeatureCombination Combination,
                                                             bool KeepCondition);

// 📝 Knife with a Construct primitive drawn as the cutter (box, cylinder, cone, prism, sphere, torus, wedge, tube): the
//    tile Request describes (its own Combination / Target ignored) is authored and combined into Target — directly
//    (the sweep's own Boolean) or, with KeepCondition, as a new body whose Duplicate cuts so the cutter stays. Separate
//    only adds the cutter as a new body (selected). One undo entry; refused (document untouched) as Construct refuses.
[[nodiscard]] SketcherCommandOutcome KnifeSketcherPrimitive(SketcherDocument& Document, const SketcherConstructRequest& Request, const std::string& Target,
                                                           GeometrySpace::Parametric::FeatureCombination Combination, bool KeepCondition);

// 📝 Alt+J (Fusion Joint; Plasticity has none): place the body owning MovingName onto the body owning FixedName by a
//    joint between the two joint origins (an edge or a face each; Assembly/JointSolver.h reads the origin off it).
//    Angles in degrees; each value becomes a step parameter (caption.Angle, .Offset, .Slide, .SecondSlide, .Pitch, .Roll)
//    so the step card and the parameter fields drag it, replaying from the joint only.
struct SketcherJointRequest
{
    SketcherSelectionCategory MovingKind  = SketcherSelectionCategory::Edge;                         // [-]   - Edge or face
    std::string           MovingName  = {};                                                  // [-]   - Joint origin on the moving body
    SketcherSelectionCategory FixedKind   = SketcherSelectionCategory::Edge;                         // [-]   - Edge or face
    std::string           FixedName   = {};                                                  // [-]   - Joint origin on the fixed body
    GeometrySpace::Parametric::JointCategory   Kind        = GeometrySpace::Parametric::JointCategory::Revolute;                       // [-]   - Fusion joint type
    double                Angle       = 0.0;                                                 // [deg] - Turn about the joint 𝑧 (ball: yaw)
    double                Offset      = 0.0;                                                 // [len] - Along the joint 𝑧
    double                Slide       = 0.0;                                                 // [len] - Slider / cylindrical 𝑧 · pin-slot / planar 𝑥
    double                SecondSlide = 0.0;                                                 // [len] - Planar 𝑦
    double                Pitch       = 0.0;                                                 // [deg] - Ball: about 𝑦
    double                Roll        = 0.0;                                                 // [deg] - Ball: about 𝑥
    bool                  Flipped     = false;                                               // [-]   - Origins aligned instead of facing
};

// 📝 The joint origin an edge or a face offers (hover glyph of the Joint card), with the body that owns it.
struct SketcherJointOrigin
{
    std::string                Body        = {};                                             // [-] - Owning body key
    GeometrySpace::Parametric::OrthonormalBasis Frame       = {};                                             // [-] - Origin and axes (world)
    std::string                Description = {};                                             // [-] - "circle centre", "plane centroid", …
    std::string                Refusal     = {};                                             // [-] - Empty when the entity offers one
};

[[nodiscard]] SketcherJointOrigin ResolveSketcherJointOrigin(const SketcherDocument& Document, SketcherSelectionCategory Kind, const std::string& Name);

// 📝 Exact foot of a point on a named face's surface: the closest surface point and the outward unit normal there (the
//    face sense applied). The tangent plane of a curved face for sketching on it (Orient to faces).
struct SketcherFaceFoot
{
    bool                        FoundCondition = false;                                      // [-]   - The face exists and the inversion converged
    GeometrySpace::Parametric::Vector3 Position       = {};                                         // [len] - Surface point (world)
    GeometrySpace::Parametric::Vector3 Normal         = {};                                         // [-]   - Outward unit normal
};

[[nodiscard]] SketcherFaceFoot ProjectOntoSketcherFace(const SketcherDocument& Document, const std::string& Name, GeometrySpace::Parametric::Vector3 Position);

// One Joint step: the moving body (and every body jointed to it) moves; refused (document untouched) when both origins
// are on one body, the moving body is already jointed, or the joint would close a loop.
[[nodiscard]] SketcherCommandOutcome JointSketcherBodies(SketcherDocument& Document, const SketcherJointRequest& Request);

// Plasticity H / Shift+H / Alt+H / Ctrl+H (appearance: undoable, saved, no regeneration).
[[nodiscard]] SketcherCommandOutcome ChangeSketcherVisibility(SketcherDocument& Document, SketcherVisibilityVerb Verb);

// Outliner eye: hides one body, or shows it again (appearance: undoable, saved, no regeneration).
[[nodiscard]] SketcherCommandOutcome ToggleSketcherBodyVisibility(SketcherDocument& Document, const std::string& Key);

// Outliner eye on a sketch / space curve: out of view, picking and snapping (lofts and extrudes still read it).
[[nodiscard]] SketcherCommandOutcome ToggleSketcherCurveVisibility(SketcherDocument& Document, const std::string& Label);

// 💡 Fill on / off for a sketch (Inspector toggle, F): filled closed loops extrude into solids, unfilled ones into walls
//    of zero thickness (an open sheet). Filling an open sketch is refused. One undo entry, regenerates.
[[nodiscard]] SketcherCommandOutcome FillSketcherSketch(SketcherDocument& Document, const std::string& Sketch, bool FillCondition);

// 💡 Fill tool: the region under the pointer. A region that is exactly one sketch's own loops fills that sketch; any other
//    (a lens of two circles, a crescent, a cell of crossing lines) becomes a new filled sketch "Fill<n>" holding its exact
//    boundary pieces. One undo entry, regenerates.
[[nodiscard]] SketcherRegionPreview  PreviewSketcherRegion(const SketcherDocument& Document, const SketcherRegionRequest& Request);
[[nodiscard]] SketcherCommandOutcome FillSketcherRegion(SketcherDocument& Document, const SketcherRegionRequest& Request);

// 💡 Flip normals: each sheet body (loft panels, walls) turned over by a Flip step; flipping a body whose last step is
//    its own Flip removes that step instead (a toggle). Solids are refused (their faces point out by definition).
[[nodiscard]] SketcherCommandOutcome FlipSketcherBodies(SketcherDocument& Document, const std::vector<std::string>& Keys);

// 💡 Flip the picked faces' normals (Inspector Flip normals / Alt+N with faces picked): per sheet body a Flip step naming
//    the faces; the pieces of the sheet holding them (faces joined through shared edges) turn over, the rest stays (a
//    piece turns whole so neighbours agree). The same faces flipped again by the body's last step remove it (a toggle).
//    Faces of solids refuse. One undo entry.
[[nodiscard]] SketcherCommandOutcome FlipSketcherFaces(SketcherDocument& Document, const std::vector<std::string>& Faces);

// Outliner lock on a body, sketch or space curve: stays in view but cannot be picked, dragged or moved (appearance).
[[nodiscard]] SketcherCommandOutcome ToggleSketcherLock(SketcherDocument& Document, const std::string& Key);

[[nodiscard]] bool SketcherCurveHiddenCondition(const SketcherDocument& Document, const std::string& Label) noexcept;
[[nodiscard]] bool SketcherLockedCondition(const SketcherDocument& Document, const std::string& Key) noexcept;

// Plasticity J: the coplanar sketches merge into the first (ends welded, frame constraints carried); the others are
// removed. A sketch some later step reads (an extrude profile, a loft section …) is not removed but hidden, so its body
// keeps its input; when the first one is read, the joined sketch becomes a new step at the end instead.
[[nodiscard]] SketcherCommandOutcome JoinSketcherSketches(SketcherDocument& Document, const std::vector<std::string>& Sketches);

// J from one sketch: Sketch first, then every coplanar sketch no later step uses that touches the group (end to end or
// crossing), transitively, in tree order — what Plasticity's join-curves would weld.
[[nodiscard]] std::vector<std::string> SketcherJoinCandidates(const SketcherDocument& Document, const std::string& Sketch);

// Plasticity O: a new sketch step holding the exact offset of every chain of Sketch (Distance > 0: outward / left).
[[nodiscard]] SketcherCommandOutcome OffsetSketcherSketch(SketcherDocument& Document, const std::string& Sketch, double Distance);
[[nodiscard]] GeometrySpace::Parametric::SketchOffsetOutcome PreviewSketcherOffset(const SketcherDocument& Document, const std::string& Sketch, double Distance);

// Plasticity P: a tube of Radius along a one-curve sketch or a SpaceCurve, combined with Target (Separate: a new body).
[[nodiscard]] SketcherCommandOutcome PipeSketcherPath(SketcherDocument& Document, const std::string& Path, double Radius,
                                                      GeometrySpace::Parametric::FeatureCombination Combination = GeometrySpace::Parametric::FeatureCombination::Separate,
                                                      const std::string& Target = {});

// 📝 Revolve axis (Alt+R): a line of the sketch by identifier (construction or profile edge), else a point + direction
//    in sketch coordinates (the sketch x- / y-axis).
struct SketcherRevolveAxis
{
    int               Line      = -1;                                                        // [-] - Sketch line identifier (−1: Origin + t·Direction)
    GeometrySpace::Parametric::Vector2 Origin    = {};                                                        // [len] - A point of the axis
    GeometrySpace::Parametric::Vector2 Direction = { 0.0, 1.0 };                                              // [-]   - Axis direction
};

// The axis Alt+R starts with: the sketch's first construction line, else the sketch y-axis when the profile lies on one
// side of it (touching allowed), else the x-axis when that one is clear, else the y-axis (the revolve then refuses).
[[nodiscard]] SketcherRevolveAxis ProposeSketcherRevolveAxis(const SketcherDocument& Document, const std::string& Sketch);
// "Line3", "sketch y-axis", "sketch x-axis" or "axis (x, y) + t·(dx, dy)".
[[nodiscard]] std::string DescribeSketcherRevolveAxis(const SketcherDocument& Document, const std::string& Sketch, const SketcherRevolveAxis& Axis);

// Alt+R (Plasticity revolution, no default key): the profile of Sketch turned by Angle (0, 2π] about Axis, combined like
// an extrude (the first body is New; later ones Union / Difference / Intersection with Target, or New body).
[[nodiscard]] SketcherCommandOutcome RevolveSketcherSketch(SketcherDocument& Document, const std::string& Sketch, const SketcherRevolveAxis& Axis, double Angle,
                                                           GeometrySpace::Parametric::FeatureCombination Combination = GeometrySpace::Parametric::FeatureCombination::Separate,
                                                           const std::string& Target = {});

// Plasticity Evolution (Shift+P / Alt+E): the profile of sketch Profile swept along Path (a one-curve sketch or a
// SpaceCurve), combined like an extrude.
[[nodiscard]] SketcherCommandOutcome EvolveSketcherProfile(SketcherDocument& Document, const std::string& Profile, const std::string& Path,
                                                           GeometrySpace::Parametric::FeatureCombination Combination = GeometrySpace::Parametric::FeatureCombination::Separate,
                                                           const std::string& Target = {});
// The path Evolution starts with: the first selected curve, else the newest other sketch holding exactly one drawn open
// curve (empty: none).
[[nodiscard]] std::string ProposeSketcherEvolutionPath(const SketcherDocument& Document, const std::string& Profile);

// 📝 Constraint commands on a sketch step (the K session). Each one solves the edited sketch first, with its parameters
//    bound as regeneration binds them: a redundant or conflicting constraint is refused naming the constraints it
//    duplicates or contradicts, and on success the solved positions are written back into the step, so the sketch stays
//    where it is shown and every later step (extrudes of it) regenerates. One undo entry each.
struct SketcherSketchDiagnosis
{
    bool                         PresentCondition = false;                                   // [-] - The label is a sketch step
    GeometrySpace::Parametric::SketchModel        Sketch           = {};                                      // [-] - As solved (parameters bound)
    GeometrySpace::Parametric::SketchSolveOutcome Solve            = {};                                      // [-] - DOF, statuses, fixed points
};

[[nodiscard]] SketcherSketchDiagnosis DiagnoseSketcherSketch(const SketcherDocument& Document, const std::string& Sketch);

// 📝 Geometry picked in an earlier sketch step while constraining this one. Linking appends it as linked points (held
//    at their projection into this plane) and construction curves, reusing whatever is linked already, so the kernel
//    proposals run on one model; the constraint command links the same picks again inside its own undo entry.
struct SketcherSketchPick
{
    std::string          Sketch = {};                                                        // [-] - Sketch step label (empty: the edited sketch)
    GeometrySpace::Parametric::SketchPick Pick   = {};                                                        // [-] - Point / curve in that sketch
};

struct SketcherLinkedPicks
{
    GeometrySpace::Parametric::SketchModel             Sketch   = {};                                         // [-] - As solved, the linked geometry appended
    std::vector<GeometrySpace::Parametric::SketchPick> Picks    = {};                                         // [-] - The picks in that model
    int                               Appended = 0;                                          // [-] - Points + curves it gained
    std::string                       Refusal  = {};                                         // [-] - Why a pick cannot be linked
};

[[nodiscard]] SketcherLinkedPicks LinkSketcherPicks(const SketcherDocument& Document, const std::string& Sketch, const std::vector<SketcherSketchPick>& Picks);

// Sketch steps Sketch may link to (the ones before it), in tree order.
[[nodiscard]] std::vector<std::string> SketcherLinkSources(const SketcherDocument& Document, const std::string& Sketch);

// Appends Constraint (an unlabelled one gets the next "H3" / "D2" … label of its kind). With Links, its indices refer to
// LinkSketcherPicks(Links)'s model and the linked geometry is added with it. G2 replaces the G1 on the same joint.
[[nodiscard]] SketcherCommandOutcome ConstrainSketcherSketch(SketcherDocument& Document, const std::string& Sketch, const GeometrySpace::Parametric::SketchConstraint& Constraint,
                                                             const std::vector<SketcherSketchPick>& Links = {});

// SolveSpace E: a dimension toggled between driving and reference (driven, display-only). Becoming driving keeps the
// measured value and is refused when it would be redundant or conflicting; a parameter-bound one stays driving.
[[nodiscard]] SketcherCommandOutcome ToggleSketcherReference(SketcherDocument& Document, const std::string& Sketch, int Constraint);

// Delete on linked geometry: the link dropped (a circle's radius too), the geometry stays where it is, now free.
[[nodiscard]] SketcherCommandOutcome UnlinkSketcherGeometry(SketcherDocument& Document, const std::string& Sketch, GeometrySpace::Parametric::SketchPick Pick);

// True when the point / curve is held by a link to another sketch.
[[nodiscard]] bool SketcherLinkedCondition(const GeometrySpace::Parametric::SketchModel& Sketch, GeometrySpace::Parametric::SketchPick Pick) noexcept;

[[nodiscard]] SketcherCommandOutcome RemoveSketcherConstraint(SketcherDocument& Document, const std::string& Sketch, int Constraint);

// A dimension's new value ([len], angles [rad]); one bound to a document parameter changes the parameter.
[[nodiscard]] SketcherCommandOutcome ChangeSketcherDimension(SketcherDocument& Document, const std::string& Sketch, int Constraint, double Value);

// SolveSpace drag: the point pinned at Target (sketch-plane coordinates) with the rest following, else as near as its
// constraints let it; refused when it cannot move at all.
[[nodiscard]] SketcherCommandOutcome DragSketcherSketchPoint(SketcherDocument& Document, const std::string& Sketch, int Point, GeometrySpace::Parametric::Vector2 Target);
[[nodiscard]] GeometrySpace::Parametric::SketchDragOutcome PreviewSketcherDrag(const SketcherDocument& Document, const std::string& Sketch, int Point, GeometrySpace::Parametric::Vector2 Target);

// 💡 Several points of one sketch dragged together (gizmo on mode-1 sketch points): each point pinned at its target in
//    turn, the rest following their constraints, swept until every free point sits on its target (or 6 sweeps); one
//    command. Points and Targets pair up (sketch-plane coordinates); refused when nothing can move.
[[nodiscard]] SketcherCommandOutcome DragSketcherSketchPoints(SketcherDocument& Document, const std::string& Sketch, const std::vector<int>& Points,
                                                              const std::vector<GeometrySpace::Parametric::Vector2>& Targets);

// "H3", "D2": the label the next constraint of that kind gets in Sketch.
[[nodiscard]] std::string NextSketcherConstraintLabel(const GeometrySpace::Parametric::SketchModel& Sketch, GeometrySpace::Parametric::SketchConstraintCategory Kind);

// Key of the body carrying a face / edge name, or the key itself for Body; empty when no body has it.
[[nodiscard]] std::string SketcherBodyOfName(const SketcherDocument& Document, SketcherSelectionCategory Kind, const std::string& Name);

// Shown name of a body: the caption of its creating step ("Block1"), else the key.
[[nodiscard]] std::string SketcherBodyCaption(const SketcherDocument& Document, const std::string& Key);

// Construction plane frame: 0 XY (normal +z), 1 XZ (x → x, sketch y → z, normal −y), 2 YZ (y → x, z → y, normal +x).
[[nodiscard]] GeometrySpace::Parametric::OrthonormalBasis SketcherSketchPlane(int GridPlane) noexcept;

// A sketch step some extrude / revolve already uses as its profile.
[[nodiscard]] bool SketcherSketchConsumedCondition(const SketcherDocument& Document, const std::string& SketchLabel);

// A sketch step any later step reads: a profile (extrude / revolve / sweep), a loft or pipe section, a cutter or a path.
[[nodiscard]] bool SketcherSketchReferencedCondition(const SketcherDocument& Document, const std::string& SketchLabel);

// Undo / redo restore the tree snapshot and regenerate; false when the stack is empty.
// 💡 Vehicle plan V1: every curve of the file becomes a SpaceCurve step ("Curve#n", captioned with the loop name) fitted
//    at Tolerance — one undo entry, and all or nothing (a curve that cannot be fitted refuses the import).
[[nodiscard]] SketcherCommandOutcome ImportSketcherCurves(SketcherDocument& Document, const VehicleCurveFile& File, double Tolerance = 2.0e-4);

// 💡 Import STEP (priority 4 X4): one Import step per solid of a ReadSketcherStepFile reading (one undo entry), each a
//    body of its own captioned by the file name ("Boss", "Boss.2" …), its canonical STEP text embedded in the document.
struct SketcherStepReading;
[[nodiscard]] SketcherCommandOutcome ImportSketcherStep(SketcherDocument& Document, const SketcherStepReading& Reading);

// 💡 V4–V6: a Loft step through the SpaceCurve steps and / or one-chain sketches (line, polyline, arc, slot, rectangle,
//    circle, spline: Sketch/SketchSection.h) in the given order (Plasticity L), G0 / G1 / G2 (Continuity 0–2) to a
//    panel already ending on its first or last section; when the picked curves cross each other (DetectCurveNetwork)
//    a Network step (Gordon / Coons, V5) is made instead — order does not matter there, and G1 / G2 refuses (🚧).
//    SolidCondition (G0 sections only): the skin and its two plane caps close into a solid of its own; open sections refuse.
[[nodiscard]] SketcherCommandOutcome LoftSketcherCurves(SketcherDocument& Document, const std::vector<std::string>& Labels, int Continuity = 0,
                                                       bool SolidCondition = false);

// L on mixed curve + laminar/body edge picks: resolves any selected B-rep edges in Document.Selection.Edges to matching
// curve steps (or promotes them as SpaceCurve steps inside the same undo entry) alongside Document.Selection.Curves,
// then runs the section loft or crossing-curve network.
[[nodiscard]] SketcherCommandOutcome LoftSketcherSelection(SketcherDocument& Document, int Continuity = 0, bool SolidCondition = false);

// 💡 V7 (Plasticity Alt+X, gizmo x / y / z): Mirror step through the world plane normal to Axis (0 x · 1 y · 2 z) at the
//    origin; SpaceCurves ending on that plane are re-fitted to leave it along its normal, so lofted halves meet G1.
[[nodiscard]] SketcherCommandOutcome MirrorSketcherPanels(SketcherDocument& Document, int Axis = 0);

// 💡 V7: Close step — plane caps on the planar rim loops of the sewn panels; the result is a solid (or a named refusal).
[[nodiscard]] SketcherCommandOutcome CloseSketcherPanels(SketcherDocument& Document);

// 💡 A drawn 3D curve (Shift+S with Alt snaps onto curves off the drawing plane — the request "snap a curve along a
//    curve"): a SpaceCurve step ("Curve#n", captioned CurveN) through Points (≥ 2, in order; ClosedCondition: back to the
//    first) in the drawing's Form — the sketch form's own weights (WeighSketchSpline), so on a plane it is exactly the
//    sketch curve — 48 samples a span, fitted to 10⁻⁶ of its size. Picked; one undo entry.
[[nodiscard]] std::vector<GeometrySpace::Parametric::Vector3> SampleThroughCurve(const std::vector<GeometrySpace::Parametric::Vector3>& Points, bool ClosedCondition,
                                                                          GeometrySpace::Parametric::SketchSplineForm Form = GeometrySpace::Parametric::SketchSplineForm::Hermite, int PerSpan = 48,
                                                                          const std::vector<double>& Knots = {});
[[nodiscard]] SketcherCommandOutcome DrawSketcherSpaceCurve(SketcherDocument& Document, const std::vector<GeometrySpace::Parametric::Vector3>& Points, bool ClosedCondition,
                                                            GeometrySpace::Parametric::SketchSplineForm Form = GeometrySpace::Parametric::SketchSplineForm::Hermite);

// Control editing is local and atomic: never falls back to whole-object deletion.
// First curve is the guide; the existing target keeps its label and selects side/extent.
[[nodiscard]] SketcherCommandOutcome LinkSketcherSurfaceOffset(SketcherDocument&, const std::string& Guide,
    const std::string& Target, const std::string& Body, double Distance, double Tolerance);
[[nodiscard]] SketcherCommandOutcome DetachSketcherSurfaceOffset(SketcherDocument&, const std::string& Target);
[[nodiscard]] SketcherCommandOutcome SplitSketcherSpaceCurveAt(SketcherDocument& Document, const std::string& Label, double Parameter);
[[nodiscard]] SketcherCommandOutcome SplitSketcherCurveCrossings(SketcherDocument&, double Tolerance = 0.001, bool AutoSurface = false);
[[nodiscard]] SketcherCommandOutcome BuildSketcherClosedLoopSurfaces(SketcherDocument& Document, double Tolerance = 0.001);

struct SketcherSpaceLoopPreview
{
    std::vector<std::string>                        Labels  = {};
    std::vector<GeometrySpace::Parametric::Vector3> Outline = {};
    std::string                                     Refusal = {};
};
[[nodiscard]] SketcherSpaceLoopPreview PreviewSketcherSpaceLoop(const SketcherDocument& Document, GeometrySpace::Parametric::Vector3 RayOrigin,
                                                                GeometrySpace::Parametric::Vector3 RayDirection, double Tolerance = 0.001);
[[nodiscard]] SketcherCommandOutcome   FillSketcherSpaceLoop(SketcherDocument& Document, const std::vector<std::string>& Labels, double Tolerance = 0.001);

[[nodiscard]] std::vector<double> SketcherCurveControlParameters(const GeometrySpace::Parametric::NurbsCurveDefinition& Definition);
[[nodiscard]] SketcherCommandOutcome DeleteSketcherCurvePoles(SketcherDocument&, const std::string&, const std::vector<int>&);
[[nodiscard]] SketcherCommandOutcome SubdivideSketcherCurve(SketcherDocument&, const std::string&, const std::vector<int>& Spans = {});
[[nodiscard]] SketcherCommandOutcome DeleteSketcherCurveSpans(SketcherDocument&, const std::string&, const std::vector<int>&);

// An exact non-rational B-spline: persist supplied poles / knots without fitting, one undo entry.
[[nodiscard]] SketcherCommandOutcome DrawSketcherProjectedCurve(SketcherDocument& Document, const GeometrySpace::Parametric::NurbsCurve& Curve, bool Closed, bool AutoSplit = false, double IntersectionTolerance = 0.001, bool AutoSurface = false);

// 💡 Sketch points moved off their plane (the 3D gizmo on mode-1 points in a 3D view): every curve through one becomes a
//    3D curve of the same form through its points (Points of Sketch → Targets, in the world; Plane: the sketch's placed
//    plane) and leaves the sketch (an emptied sketch goes). Refused when an arc / circle passes through a moved
//    point, or when a later step uses the sketch. The new curves are picked; one undo entry.
[[nodiscard]] SketcherCommandOutcome LiftSketcherSketchPoints(SketcherDocument& Document, const std::string& Sketch, const GeometrySpace::Parametric::OrthonormalBasis& Plane,
                                                              const std::vector<int>& Points, const std::vector<GeometrySpace::Parametric::Vector3>& Targets);

// 💡 V3 (Plasticity mode 1): pole Pole of a SpaceCurve moves to Position; kept as a pole edit re-applied after every re-fit.
[[nodiscard]] SketcherCommandOutcome MoveSketcherPole(SketcherDocument& Document, const std::string& Label, int Pole, GeometrySpace::Parametric::Vector3 Position);
[[nodiscard]] SketcherCommandOutcome MoveSketcherPoles(SketcherDocument& Document, const std::string& Label, const std::vector<int>& Poles,
                                                       const std::vector<GeometrySpace::Parametric::Vector3>& Positions);

// Toggles a SpaceCurve in the curve selection (AdditiveCondition false: it becomes the only selected curve).
void SelectSketcherCurve(SketcherDocument& Document, const std::string& Label, bool AdditiveCondition);

// 💡 Matcaps (Plasticity M / Alt+M). Assign: the selected faces take Matcap, or the body when no face is selected. Remove:
//    the selected faces fall back to the body matcap, or the body returns to Studio. Neither regenerates the body (the
//    Revision stays); both are undo entries. Unknown matcap names refuse.
[[nodiscard]] SketcherCommandOutcome AssignSketcherMatcap(SketcherDocument& Document, const std::string& Matcap);
[[nodiscard]] SketcherCommandOutcome RemoveSketcherMatcap(SketcherDocument& Document);
// A user picture under a new name (not Studio, a built-in or an existing custom name); refused when it does not decode.
[[nodiscard]] SketcherCommandOutcome AddSketcherMatcapPicture(SketcherDocument& Document, const std::string& Name, const std::string& Path);
bool UndoSketcherCommand(SketcherDocument& Document);
bool RedoSketcherCommand(SketcherDocument& Document);

// Replays the tree into a fresh partition, refreshes the summary and drops selected names that no longer resolve.
void RegenerateSketcherDocument(SketcherDocument& Document, bool CaptureCondition = false);
// The two halves of RegenerateSketcherDocument: the replay (bodies, names, selection pruned) and the figures (audit,
// volume) of Summary. Commands measure only once they stand (a refused command rolls back to figures it already had).
void RegenerateSketcherDocumentDeferred(SketcherDocument& Document, bool CaptureCondition = false);
void SummarizeSketcherDocument(SketcherDocument& Document);
// 💡 Apply = adopt the staged preview: a preview session (bevel, knife, gizmo) already ran the very command on a clone
//    of Document; when nothing changed since (the caller checks its staging key and Document.Revision), the clone's
//    tree, bodies, figures and selection become Document's in one undo entry instead of running the command again.
[[nodiscard]] SketcherCommandOutcome AdoptSketcherPreview(SketcherDocument& Document, SketcherDocument&& Preview, const SketcherCommandOutcome& Staged);

// Selection by persistent name; Additive toggles membership, otherwise the selection becomes exactly this name.
// Body: the key of the body a face / edge was picked on (kept as Selection.PickedBody; a cut's two pieces carry the same
// names, so the body decides which piece the blend, inset or loop works on).
void SelectSketcherName(SketcherDocument& Document, SketcherSelectionCategory Kind, const std::string& Name, bool AdditiveCondition, const std::string& Body = {});
void ClearSketcherSelection(SketcherDocument& Document);

// Names of the edge's tangent chain on the current body (the edge itself first); just the name when it does not resolve.
[[nodiscard]] std::vector<std::string> SketcherTangentChain(const SketcherDocument& Document, const std::string& Name);

// 💡 Blender's edge ring (Ctrl+Alt+click, BMW_EDGERING): from the edge across every adjacent face with an even number of
//    sides (one loop of 2k fins, k ≥ 2) to the edge k fins on, and on from there in both directions — the uprights of a
//    prism, the parallel edges of a quad strip, the far rim across a cylinder side. Odd faces (triangles, pentagons) and
//    non-manifold edges end the walk. The edge itself first; just the name when it does not resolve. Body: the key of
//    the body the edge was picked on (empty: the first body holding that name — pieces of one cut share names).
[[nodiscard]] std::vector<std::string> SketcherEdgeRing(const SketcherDocument& Document, const std::string& Name, const std::string& Body = {});

// 💡 Blender's edge loop (Alt+click, BMW_EDGELOOP): from the edge through every vertex with four edges to the opposite
//    one of its fan (two edges: straight on), both directions, stopping at poles (3, 5+ edges: a box corner). An edge at
//    a three-edge vertex beside a face of more than four sides walks round that face instead (a hexagon rim); an open
//    sheet's boundary edge walks the boundary. The edge itself first; just the name when it does not resolve.
[[nodiscard]] std::vector<std::string> SketcherEdgeLoop(const SketcherDocument& Document, const std::string& Name, const std::string& Body = {});

// 📝 Edge path (Ctrl+click an unpicked edge with edges picked — Blender's pick shortest path). AlongLoop stays on a loop
//    holding both edges — an edge loop (Alt walk) through either, a rim of a face beside From, or From's tangent chain —
//    and takes its shorter side (OtherSide: the longer one of a closed loop), so it never cuts across the body the way a
//    shortest path does; WholeLoop takes that whole loop; Shortest any edges, the least total length.
enum class SketcherEdgePathMode : uint8_t
{
    AlongLoop = 0,                                                                           // From … To on a loop holding both
    WholeLoop = 1,                                                                           // The whole loop holding both
    Shortest  = 2,                                                                           // Least total length over any edges
};

struct SketcherEdgePath
{
    std::vector<std::string> Edges   = {};                                                   // [-]   - From first, To last (WholeLoop: then the rest)
    std::string              Carrier = {};                                                   // [-]   - "edge loop", "rim of <face>", "tangent chain", "any edges"
    double                   Length  = 0.0;                                                  // [len] - Total length of Edges
    bool                     ClosedCondition = false;                                        // [-]   - The carrier loop closes (OtherSide applies)
    std::string              Refusal = {};                                                   // [-]   - Why no path (empty: found)
};

[[nodiscard]] SketcherEdgePath SketcherEdgePathBetween(const SketcherDocument& Document, const std::string& From, const std::string& To, SketcherEdgePathMode Mode,
                                                       bool OtherSideCondition = false, const std::string& Body = {});
[[nodiscard]] const char* SketcherEdgePathModeLabel(SketcherEdgePathMode Mode) noexcept;

// Blender's face loop (Alt+click in face mode, BMW_FACELOOP): the four-sided faces the edge's ring crosses (quads only).
[[nodiscard]] std::vector<std::string> SketcherFaceLoop(const SketcherDocument& Document, const std::string& Edge, const std::string& Body = {});

// Step caption for the outliner ("Block1 · Sketch", "Fillet3"), falling back to the label.
[[nodiscard]] std::string DescribeSketcherStep(const SketcherDocument& Document, const GeometrySpace::Parametric::FeatureStep& Step);

[[nodiscard]] const char* SketcherConstructLabel(SketcherConstructCategory Kind) noexcept;
[[nodiscard]] const char* SketcherFaceOperationLabel(GeometrySpace::Parametric::FaceOperationCategory Operation) noexcept;
[[nodiscard]] const char* SketcherDrawingLabel(SketcherDrawingCategory Kind) noexcept;

// 💡 Modular Parametric Variables: create, rename, delete (unbinding references cleanly to their current evaluated literals),
//    bind any step field ("Size", "EndSize", "Extent", "Start", "Elevation", "Angle", "Amount", "Offset", "Joint.Offset",
//    "Joint.Angle", "Joint.Slide") to a variable, or promote an existing operation value into a named variable in one step.
[[nodiscard]] bool                   SketcherUserVariableCondition(const GeometrySpace::Parametric::FeatureParameter& Parameter) noexcept;
[[nodiscard]] SketcherCommandOutcome CreateSketcherVariable(SketcherDocument& Document, const std::string& Name, double Value,
                                                            double Low = -100.0, double High = 100.0, const std::string& Description = {});
[[nodiscard]] SketcherCommandOutcome DeleteSketcherVariable(SketcherDocument& Document, const std::string& Name);
[[nodiscard]] SketcherCommandOutcome RenameSketcherVariable(SketcherDocument& Document, const std::string& OldName, const std::string& NewName);
[[nodiscard]] SketcherCommandOutcome BindSketcherStepValue(SketcherDocument& Document, const std::string& StepLabel, const std::string& Field,
                                                           const std::string& ParameterName, double Factor = 1.0, double Offset = 0.0);
[[nodiscard]] SketcherCommandOutcome SaveStepValueAsVariable(SketcherDocument& Document, const std::string& StepLabel, const std::string& Field,
                                                             const std::string& VariableName);
[[nodiscard]] SketcherCommandOutcome SetSketcherStepOffsetAxis(SketcherDocument& Document, const std::string& StepLabel, GeometrySpace::Parametric::Vector3 Axis);

// 💡 Live Booleans: non-destructive Boolean modifiers whose cutter bodies stay in the scene as semitransparent selectable &
//    editable cutters (Appearance.LiveCutters) so G / R / S, face push/pull, and edge bevels update the Boolean in real time.
[[nodiscard]] bool                   SketcherBodyReferencedByModifiersCondition(const SketcherDocument& Document, const std::string& BodyKey) noexcept;
[[nodiscard]] SketcherCommandOutcome AddSketcherLiveBoolean(SketcherDocument& Document, const std::string& Target, const std::string& Tool,
                                                            GeometrySpace::Parametric::FeatureCombination Combination = GeometrySpace::Parametric::FeatureCombination::Difference,
                                                            GeometrySpace::Parametric::Vector3 OffsetAxis = { 0.0, 0.0, 1.0 }, double Offset = 0.0);
[[nodiscard]] SketcherCommandOutcome KnifeSketcherLiveInsert(SketcherDocument& Document, const SketcherInsertPlacement& Placement);
[[nodiscard]] SketcherCommandOutcome ToggleSketcherLiveCutter(SketcherDocument& Document, const std::string& BodyKey);

// 💡 Multi-Stage Compound Cutters (Boolean Library): non-stretching, zero-duplicate multi-stage cutters drawn interactively
//    stage-by-stage (Stage 1 profile → Stage 1 extrude → Stage 2 profile → Stage 2 extrude), preserving exact analytic
//    PlaneSurface / CylinderSurface / ConeSurface carriers for both destructive cuts and semitransparent Live Booleans.
enum class SketcherCompoundCutterCategory : uint8_t
{
    Countersink   = 0,                                                                       // Conical chamfer Ø₁ × D₁ + shaft Ø₂ × D₂
    Counterbore   = 1,                                                                       // Cylindrical head bore Ø₁ × D₁ + shaft Ø₂ × D₂
    SteppedHole   = 2,                                                                       // Cylindrical bore Ø₁ × D₁ + pilot bore & drill tip Ø₂ × D₂
    HexSocket     = 3,                                                                       // Hex socket head Ø₁ × D₁ + bolt shaft Ø₂ × D₂
    SteppedSlot   = 4,                                                                       // Counterbored rounded slot R₁ × D₁ + clearance slot R₂ × D₂
    SteppedPocket = 5,                                                                       // Concentric square step W₁ × D₁ + inner pocket W₂ × D₂
};

inline constexpr int SketcherCompoundCutterCount = 6;

[[nodiscard]] const char* SketcherCompoundCutterLabel(SketcherCompoundCutterCategory Kind) noexcept;

struct SketcherCompoundCutterSpec
{
    SketcherCompoundCutterCategory             Kind          = SketcherCompoundCutterCategory::Countersink; // [-]   - Compound profile family
    GeometrySpace::Parametric::OrthonormalBasis   Spot          = {};                                      // [len] - Face contact origin + outward normal ZAxis
    double                                 Turn          = 0.0;                                     // [rad] - In-plane rotation about Spot.ZAxis
    double                                 OuterRadius   = 0.12;                                    // [len] - Stage 1 radius / half-width
    double                                 OuterDepth    = 0.05;                                    // [len] - Stage 1 extrude depth
    double                                 InnerRadius   = 0.06;                                    // [len] - Stage 2 radius / half-width (< OuterRadius)
    double                                 InnerDepth    = 0.12;                                    // [len] - Stage 2 extrude depth below Stage 1 floor
    GeometrySpace::Parametric::FeatureCombination Combination   = GeometrySpace::Parametric::FeatureCombination::Difference; // [-] - Q / W / E / R
    std::string                            Target        = {};                                      // [-]   - Target body key
    bool                                   LiveCondition = false;                                   // [-]   - True: semitransparent Live Boolean cutter
};

[[nodiscard]] SketcherCommandOutcome KnifeSketcherCompoundCutter(SketcherDocument& Document, const SketcherCompoundCutterSpec& Spec);

// 💡 Edge Weight Groups ("vertex paint for edges"): assign literal or variable-bound weights to selected edges so Bevel
//    modifiers reference them and interpolate smooth variable-radius rolling-ball fillets / chamfers across corners.
[[nodiscard]] SketcherCommandOutcome AssignSketcherEdgeWeights(SketcherDocument& Document, const std::string& GroupName,
                                                               const std::vector<std::string>& EdgeNames, GeometrySpace::Parametric::FeatureScalar Weight);
[[nodiscard]] SketcherCommandOutcome RemoveSketcherEdgeGroup(SketcherDocument& Document, const std::string& GroupName);
void                                 SelectSketcherEdgeGroup(SketcherDocument& Document, const std::string& GroupName);

// 💡 Blender-style CAD Modifier Stack: non-destructive Boolean, Bevel, Linear / Circular / Curve / Surface Arrays, and Curve Bend.
[[nodiscard]] SketcherCommandOutcome AddSketcherModifier(SketcherDocument& Document, GeometrySpace::Parametric::FeatureModifier Modifier);
[[nodiscard]] SketcherCommandOutcome UpdateSketcherModifier(SketcherDocument& Document, const std::string& Label, const GeometrySpace::Parametric::FeatureModifier& Updated);
[[nodiscard]] SketcherCommandOutcome MoveSketcherModifier(SketcherDocument& Document, const std::string& Label, int Delta);
[[nodiscard]] SketcherCommandOutcome ToggleSketcherModifier(SketcherDocument& Document, const std::string& Label);
[[nodiscard]] SketcherCommandOutcome RemoveSketcherModifier(SketcherDocument& Document, const std::string& Label);
[[nodiscard]] SketcherCommandOutcome ApplySketcherModifier(SketcherDocument& Document, const std::string& Label);

// 💡 Isolate restore helper (Ctrl+I): restores a pre-isolate HiddenBodies / HiddenCurves snapshot as one undoable appearance step.
[[nodiscard]] SketcherCommandOutcome RestoreSketcherVisibility(SketcherDocument& Document, const std::set<std::string>& HiddenBodies,
                                                               const std::set<std::string>& HiddenCurves, const std::string& Label = "Exit isolate");

// 💡 Persistent CAD Dimension Lines (D): associative 3D linear, radial, diametral, angular, and bounding-box dimensions.
void                                                RefreshSketcherDimensions(SketcherDocument& Document);
[[nodiscard]] std::vector<SketcherDimension>        EvaluateSketcherDimensions(const SketcherDocument& Document);
[[nodiscard]] const char*                           SketcherDimensionKindLabel(SketcherDimensionCategory Kind) noexcept;
[[nodiscard]] std::string                           FormatSketcherDimension(const SketcherDimension& Dimension);
[[nodiscard]] SketcherCommandOutcome                AddSketcherDimensionFromSelection(SketcherDocument& Document,
                                                                                      std::optional<SketcherDimensionCategory> PreferredKind = std::nullopt,
                                                                                      double Offset = 0.35);
[[nodiscard]] SketcherCommandOutcome                AddSketcherInteractiveDimension(SketcherDocument& Document, const SketcherDimension& Spec);
[[nodiscard]] SketcherCommandOutcome                AddSketcherPointDimension(SketcherDocument& Document, GeometrySpace::Parametric::Vector3 First,
                                                                              GeometrySpace::Parametric::Vector3 Second,
                                                                              GeometrySpace::Parametric::Vector3 PlaneNormal = { 0.0, 0.0, 1.0 },
                                                                              double Offset = 0.35, int AxisFilter = -1,
                                                                              const std::string& FirstEntity = {},
                                                                              SketcherSelectionCategory FirstKind = SketcherSelectionCategory::Vertex,
                                                                              const std::string& SecondEntity = {},
                                                                              SketcherSelectionCategory SecondKind = SketcherSelectionCategory::Vertex);
[[nodiscard]] SketcherCommandOutcome                UpdateSketcherDimensionOffset(SketcherDocument& Document, const std::string& Id, double Offset);
[[nodiscard]] SketcherCommandOutcome                UpdateSketcherDimensionPlacement(SketcherDocument& Document, const std::string& Id, double Offset,
                                                                                     GeometrySpace::Parametric::Vector3 Direction,
                                                                                     std::optional<int> SlideAxis = std::nullopt);
[[nodiscard]] SketcherCommandOutcome                UpdateSketcherDimensionSlideAxis(SketcherDocument& Document, const std::string& Id, int SlideAxis);
[[nodiscard]] SketcherCommandOutcome                UpdateSketcherDimensionAxisFilter(SketcherDocument& Document, const std::string& Id, int AxisFilter);
[[nodiscard]] SketcherCommandOutcome                UpdateSketcherDimensionNote(SketcherDocument& Document, const std::string& Id, const std::string& Note);
[[nodiscard]] SketcherCommandOutcome                ToggleSketcherDimensionVisibility(SketcherDocument& Document, const std::string& Id);
[[nodiscard]] SketcherCommandOutcome                ToggleSketcherDimensionRadialKind(SketcherDocument& Document, const std::string& Id);
[[nodiscard]] SketcherCommandOutcome                RemoveSketcherDimension(SketcherDocument& Document, const std::string& Id);
[[nodiscard]] SketcherCommandOutcome                ClearSketcherDimensions(SketcherDocument& Document);

} // namespace Frontier::Workspaces::Sketcher
