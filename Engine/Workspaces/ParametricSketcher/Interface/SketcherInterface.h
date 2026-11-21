/*====================================================================================================================================
                                                         SOLIDARCINTERFACE.H
====================================================================================================================================*/
// 🧩 Sketcher ImGui shell: header, Outliner, viewport, Inspector, drawing and bevel sessions, shortcuts, autosave / recovery

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherExchange.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherKnife.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMeasurement.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherOptions.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherOutliner.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherStyleSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherCamera.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherSnapping.h"

#include <imgui.h>
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherUiKit.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

class SketcherPreviewWorker;

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Plasticity selection modes: 1 control points (curves and their poles), 2 edges, 3 faces, 4 solids, 5 everything.
enum class SketcherPickMode : uint8_t
{
    ControlPoint = 1,                                                                        // Plasticity 1: curves, pole drag
    Edge  = 2,                                                                               // Plasticity 2
    Face  = 3,                                                                               // Plasticity 3
    Solid = 4,                                                                               // Plasticity 4: a click selects the whole body
    Curve = 6,                                                                              // Whole curves only; never spline spans or body edges
    Any   = 5,                                                                               // Plasticity 5 / Tab: edges and faces
};

// What a viewport click does to the selection (Plasticity: click, Shift+click, Ctrl+click; Blender: Alt loops, Ctrl+Alt rings).
enum class SketcherPickIntent : uint8_t
{
    Replace    = 0,                                                                          // The selection becomes the hit
    Add        = 1,                                                                          // The hit joins the selection
    Remove     = 2,                                                                          // The hit leaves the selection
    Loop       = 3,                                                                          // Edges (Alt): the selection becomes the hit's edge loop (Blender)
    LoopToggle = 4,                                                                          // Edges (Shift+Alt): its loop joins (leaves when the hit is picked)
    Ring       = 5,                                                                          // Edges (Ctrl+Alt): the selection becomes the hit's edge ring
    RingToggle = 6,                                                                          // Edges (Shift+Ctrl+Alt): its ring joins (leaves when picked)
    Chain      = 7,                                                                          // Edges (double-click): the hit's tangent chain joins
};

// The command a session (B fillet, E extrude) stages and applies.
enum class SketcherSessionVerb : uint8_t
{
    Blend   = 0,                                                                             // Fillet / chamfer on the selected edges
    Extrude = 1,                                                                             // Extrude the selected faces
    Profile = 2,                                                                             // Extrude the focused sketch's profile (Q / W / E / R Boolean)
    Inset   = 3,                                                                             // Inset the selected planar faces (O / I; D distance · H depth)
    Corner  = 4,                                                                             // Fillet / chamfer 2D sketch corners (B on sketch points or sketches)
};

// Plasticity / Blender extrude modes surfaced via Ctrl+E context menu; E reuses the last chosen mode.
enum class SketcherExtrudeMode : uint8_t
{
    Standard        = 0,                                                                     // Extrude along averaged face normal / profile Z
    AlongNormals    = 1,                                                                     // Extrude each face along its outward normal
    Manifold        = 2,                                                                     // Auto union/cut manifold extrude
    IndividualFaces = 3,                                                                     // Extrude each selected face individually
    Symmetric       = 4,                                                                     // Extrude symmetrically in both + and - directions
};

// Shift+R repeats the last applied command on the current selection.
enum class SketcherRepeatCategory : uint8_t
{
    None      = 0,
    Blend     = 1,
    Face      = 2,
    Construct = 3,
};

struct SketcherRepeat
{
    SketcherRepeatCategory          Kind      = SketcherRepeatCategory::None;                        // [-]   - What to repeat
    GeometrySpace::Parametric::BlendProfile      Shape     = GeometrySpace::Parametric::BlendProfile::Fillet;                  // [-]   - Blend
    double                      Size      = 0.0;                                             // [len] - Blend size
    GeometrySpace::Parametric::FaceOperationCategory Operation = GeometrySpace::Parametric::FaceOperationCategory::Extrude;            // [-]   - Face verb
    double                      Amount    = 0.0;                                             // [-]   - Face amount (model units)
    SketcherConstructRequest    Construct = {};                                              // [-]   - Construct request
    SketcherBlendRequest        Blend     = {};                                              // [-]   - Blend request (variable sizes, flip)
    double                      Depth     = 0.0;                                             // [len] - Inset depth (Amount = inset distance)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 B / Shift+B session: a staged copy of the document carries the preview; the authored document is untouched
//    until Apply (one undo entry). Positive pointer-adjusted size = fillet radius, negative = chamfer setback.
//    V (Plasticity gizmo:fillet-solid:add) makes the size variable: Size at each edge's anchor corner, EndSize at its
//    other end; F flips which corner is the anchor. The pointer adjusts Size; EndSize is typed.
struct SketcherBevelSession
{
    bool                              ActiveCondition = false;                               // [-]   - Session open
    SketcherSessionVerb               Verb            = SketcherSessionVerb::Blend;          // [-]   - B (blend) / E (extrude)
    SketcherSessionVerb               PreviewVerb      = SketcherSessionVerb::Blend;          // [-]   - Verb the preview was built for
    SketcherExtrudeMode               ExtrudeMode     = SketcherExtrudeMode::AlongNormals;   // [-]   - Active extrude mode (Ctrl+E / E)
    SketcherExtrudeMode               StagedExtrudeMode = SketcherExtrudeMode::AlongNormals; // [-]   - ExtrudeMode the preview was built for
    bool                              PinnedCondition = false;                               // [-]   - Numeric entry pins the size
    GeometrySpace::Parametric::BlendProfile            Shape           = GeometrySpace::Parametric::BlendProfile::Fillet;      // [-]   - Cross-section
    double                            Size            = 0.25;                                // [len] - Radius / setback
    std::array<char, 32>              SizeText        = {};                                  // [-]   - Field text ("250mm", "0.25")
    bool                              SizeTypedCondition = false;                            // [-]   - SizeText typed but not yet entered
    std::unique_ptr<SketcherDocument> Preview         = {};                                  // [-]   - Staged document
    SketcherCommandOutcome            PreviewOutcome  = {};                                  // [-]   - The staged command (adopted on apply)
    uint64_t                          PreviewRevision = 0;                                   // [-]   - Document revision it was staged from
    SketcherScene                     PreviewScene    = {};                                  // [-]   - Its display arrays
    std::string                       Refusal         = {};                                  // [-]   - Why Apply is disabled
    double                            StagedSize      = -1.0;                                // [len] - Size the preview was built at
    GeometrySpace::Parametric::BlendProfile            StagedShape     = GeometrySpace::Parametric::BlendProfile::Fillet;      // [-]   - Shape the preview was built at
    bool                              VariableEnabled = false;                               // [-]   - V: size varies along each edge
    double                            EndSize         = 0.0;                                 // [len] - Size at the far corners (0 = not chosen yet)
    std::array<char, 32>              EndSizeText     = {};                                  // [-]   - Field text
    bool                              ReversedCondition = false;                             // [-]   - F: anchor at each edge's end vertex
    bool                              StagedVariable  = false;                               // [-]   - Variable state the preview was built at
    double                            StagedEndSize   = -1.0;                                // [len] - EndSize the preview was built at
    bool                              StagedReversed  = false;                               // [-]   - Flip the preview was built at
    std::string                       ProfileLabel    = {};                                  // [-]   - Profile: sketch step extruded
    double                            ProfileSink     = 0.0;                                 // [len] - Profile on a curved face's tangent plane: Union starts this deep
    GeometrySpace::Parametric::FeatureCombination      Combination     = GeometrySpace::Parametric::FeatureCombination::Union; // [-]   - Profile: Q union · W difference · E intersect · R new
    GeometrySpace::Parametric::FeatureCombination      StagedCombination = GeometrySpace::Parametric::FeatureCombination::Union; // [-] - Combination the preview was built at
    double                            Depth           = 0.0;                                 // [len] - Inset: inner face shift (− pocket)
    std::array<char, 32>              DepthText       = {};                                  // [-]   - Inset: depth field text
    double                            StagedDepth     = 0.0;                                 // [len] - Depth the preview was built at
    double                            StageMs         = 0.0;                                 // [ms]  - Last exact preview build
    std::shared_ptr<SketcherPreviewWorker> PreviewWorker = {};                              // [-]   - One immutable-snapshot kernel job; UI never waits while dragging
    uint64_t                          PreviewGeneration = 0;                                // [-]   - Monotonic async request generation
    int                               StageCount      = 0;                                   // [-]   - Exact previews completed this session
    uint64_t                          LastStagedFrame = UINT64_MAX;                          // [-]   - Frame number of last synchronous StageBevelPreview build
    bool                              DepthEnabled    = false;                               // [-]   - Inset: the pointer drives the depth (H) instead of the distance (D)
    SketcherCornerRequest             Corner          = {};                                  // [-]   - Corner: the sketches / corner points (size and shape from the session)
    // 📝 Handles (Plasticity's fillet gizmo): [0] the uniform handle drives Size; one per corner of the picked edges
    //    gives that corner its own size (a variable blend). A handle drag pins the size (the free pointer stops).
    std::vector<SketcherBlendHandle>  Handles         = {};                                  // [-]   - Blend: [0] uniform, then corners
    std::vector<double>               CornerSizes     = {};                                  // [len] - Per handle (0: follows Size; [0] unused)
    std::vector<std::string>          HandleEdges     = {};                                  // [-]   - Edge picks the handles were measured for
    std::vector<double>               StagedCorners   = {};                                  // [len] - CornerSizes the preview was built at
    int                               HoverHandle     = -1;                                  // [idx] - Handle under the pointer
    int                               DragHandle      = -1;                                  // [idx] - Handle being dragged
    double                            DragOrigin      = 0.0;                                 // [len] - Axis parameter at the press
    double                            DragSize        = 0.0;                                 // [len] - Handle size at the press
    float                             DragLast        = 0.0f;                                // [px]  - Pointer x of the last empty-space drag frame
    bool                              PointerDragCondition = false;                          // [-]   - A left drag on empty space drives the size
    bool                              DragScreenCondition  = false;                          // [-]   - Handle axis faces the camera: drag sizes by pointer x
    SketcherBlendFit                  Fit             = {};                                  // [len] - What the picked edges allow (start size)
    SketcherScene                     GoodScene       = {};                                  // [-]   - Last accepted preview (shown under a refusal)
    double                            GoodSize        = 0.0;                                 // [len] - Its size
    bool                              GoodCondition   = false;                               // [-]   - GoodScene holds a preview
    int                               FocusCorner     = -1;                                  // [idx] - Corner handle last dragged (its card field)
    std::array<char, 32>              CornerText      = {};                                  // [-]   - That corner's field text
    // 📝 Feasible bounds: a refused size above an accepted one is a ceiling; the session bisects between them and settles
    //    on the largest accepted size, which the pointer, the handles and typed values then stop at (never an error).
    //    One parameter at a time: [0] the size, k > 0 corner handle k; reset when anything else changes (LimitKey).
    int                               LimitParameter  = 0;                                   // [idx] - 0 size, k corner k
    std::string                       LimitKey        = {};                                  // [-]   - Everything else the bound was found for
    double                            LimitGood       = 0.0;                                 // [len] - Largest accepted value (0: none yet)
    double                            LimitCeiling    = 0.0;                                 // [len] - Smallest refused value above it (0: none)
    bool                              LimitProbeCondition = false;                           // [-]   - Bisecting toward the bound
    int                               LimitStops      = 0;                                   // [-]   - Values held at the bound (script evidence)
    int                               MergeSnaps      = 0;                                   // [-]   - Drags caught by a merge size (script evidence)
    // 📝 Blender's extrude manifold on a sketch: until Q / W / E / R picks one, the Boolean follows the drag — into the
    //    material (ProfileMaterial: +1 along the sketch +𝑧, −1 along −𝑧) cuts, away from it adds.
    bool                              AutoCombinationCondition = false;                      // [-]   - Profile: Boolean follows the direction
    int                               ProfileMaterial = 0;                                   // [-]   - Profile: material side of the sketch plane
    // 📝 Blender's extrude: the distance follows the pointer along the extrusion axis (outward face normal / sketch +𝑧
    //    through the selection's centre), measured from where a drag began (anchor), so the face tracks the cursor.
    bool                              AxisCondition   = false;                               // [-]   - An axis is known (else pointer x)
    GeometrySpace::Parametric::Vector3       AxisOrigin      = {};                                  // [len] - Point on the axis (selection centre)
    GeometrySpace::Parametric::Vector3       AxisDirection   = {};                                  // [-]   - Unit direction of + distance
    bool                              AxisAnchorCondition = false;                           // [-]   - A pointer drive is under way
    double                            AxisAnchorParameter = 0.0;                             // [len] - Axis parameter when it began
    double                            AxisAnchorValue = 0.0;                                 // [len] - Distance / depth when it began
    bool                              AxisAnchorFine  = false;                               // [-]   - Shift held when it began
};

// 📝 Drawing session (Shift+A line, Shift+Q rectangle, Shift+W circle, Shift+S curve; arc / slot from the Draw card):
//    clicks on the construction plane (snapped to 0.1 unless Ctrl is held) collect points while the shape previews
//    through the pointer. Rectangle and circle finish on their 2nd click, arc and slot on their 3rd; line and curve on
//    Enter / right-click, or closed by a click on their first point. Ctrl+Z drops the last point, Esc aborts.
struct SketcherDrawingSession
{
    bool                           ActiveCondition  = false;                                 // [-]   - Session open
    bool                           SurfaceCondition = false;
    std::vector<GeometrySpace::Parametric::Vector3> TracePreview = {};
    std::vector<GeometrySpace::Parametric::Vector3> TraceControls = {};
    uint64_t                       TraceRevision = 0;
    OrbitCameraBasis               TraceFrame = {};
    GeometrySpace::Parametric::SketchSplineForm TraceForm = GeometrySpace::Parametric::SketchSplineForm::Hermite;
    SketcherDrawingCategory            Kind             = SketcherDrawingCategory::Line;             // [-]   - Tool
    std::vector<GeometrySpace::Parametric::Vector2> Points           = {};                                    // [len] - Committed clicks (plane coordinates)
    GeometrySpace::Parametric::Vector2              Pointer          = {};                                    // [len] - Pointer on the plane (snapped)
    bool                           PointerCondition = false;                                 // [-]   - Pointer hits the plane
    int                            GridPlane        = 0;                                     // [-]   - 0 XY · 1 XZ · 2 YZ (fixed at open)
    bool                           CentreCondition  = false;                                 // [-]   - Rectangle: Alt centre mode · circle: V two-point mode off
    GeometrySpace::Parametric::SketchSplineForm     Form             = GeometrySpace::Parametric::SketchSplineForm::Hermite;   // [-]   - Curve: 1 Hermite · 2 Bezier · 3 NURBS · 4 cubic spline
    bool                           SquareCondition  = false;                                 // [-]   - Rectangle: s keeps the sides equal
    bool                           SegmentCondition = false;                                 // [-]   - Line: one segment, finished on the second click
    std::string                    Refusal          = {};                                    // [-]   - Why the preview cannot finish
    int                            Sides            = 6;                                     // [-]   - Polygon / prism sides (+ / − keys, wheel)
    bool                           PressCondition   = false;                                 // [-]   - Left button went down on the plane (click-drag)
    bool                           PressPlacedCondition = false;                             // [-]   - That press placed the first point (its release is no click)
    std::string                    StartFace        = {};                                    // [-]   - Solid: face under its first click (empty: ground / snap)
    bool                           SolidCondition   = false;                                 // [-]   - Construct solid: plane clicks, then a height
    SketcherConstructCategory          Solid            = SketcherConstructCategory::Block;          // [-]   - Which solid
    double                         Elevation        = 0.0;                                   // [len] - Plane height (solids: z of the first click)
    double                         PointerElevation = 0.0;                                   // [len] - Height the pointer snapped to before the first click
    double                         Lift             = 0.0;                                   // [len] - Height from the pointer (signed, along +z)
    GeometrySpace::Parametric::FeatureCombination Combination = GeometrySpace::Parametric::FeatureCombination::Separate;   // [-] - Solid: R new body (default) · Q / W / E
    bool                           FacePlaneCondition = false;                               // [-]   - Drawn on the body face under the first click (sketch or solid)
    GeometrySpace::Parametric::OrthonormalBasis     FacePlane        = {};                                    // [len] - That face plane (grid-aligned axes, outward n̂)
    std::string                    FaceName         = {};                                    // [-]   - Persistent name of the face (empty: grid plane)
    uint64_t                       FaceRevision     = 0;                                     // [-]   - Document revision FacePlane was resolved at
    // 📝 Curve (Shift+S) with Alt on a curve off the drawing plane (a wrapped 3D curve, a loft rim): the point keeps its
    //    3D place; any such point makes the finished curve a 3D curve through every point (DrawSketcherSpaceCurve).
    std::vector<std::optional<GeometrySpace::Parametric::Vector3>> Lifted = {};                     // [len] - Per committed point: its 3D place (off the plane)
    std::optional<GeometrySpace::Parametric::Vector3>              PointerLifted = {};              // [len] - The pointer's 3D place this frame
};

// 📝 Point picker (Plasticity body[gizmo=point-picker]): object snaps under the pointer while drawing and moving, and
//    the direction lock keys n / b / t / x / y / z through the last placed point. Object snaps act only while Ctrl is
//    held, and only at the important points (ends, midpoints, centres, crossings, tangent / square feet); without Ctrl
//    the pointer keeps the 0.1 grid.
struct SketcherPointPicker
{
    bool                                   Enabled           = true;                         // [-]   - Object snaps on (Inspector / Snap Menu)
    bool                                   ContextAwareSnap  = true;                         // [-]   - Bias curve snaps on curves & solid snaps on bodies
    bool                                   GridEnabled       = false;                        // [-]   - 0.1 grid steps while drawing / dragging (Snaps card; off: free, Ctrl snaps)
    bool                                   FaceOrientEnabled = true;                         // [-]   - Drawing on a body face orients the sketch / solid to it (Inspector)
    SketcherSnapFilter                     Filter          = {};                             // [-]   - Enabled kinds (Inspector)
    SketcherSnap                           Current         = {};                             // [-]   - Snap under the pointer this frame
    SketcherSnap                           Placed          = {};                             // [-]   - Snap of the last placed point (t / n / b)
    SketcherSnapLockCategory                   Lock            = SketcherSnapLockCategory::None;     // [-]   - Direction lock (cleared by the next point)
    GeometrySpace::Parametric::Vector3                      LockOrigin      = {};                             // [len] - Last placed point
    GeometrySpace::Parametric::Vector3                      LockDirection   = {};                             // [-]   - Unit lock direction
    std::unique_ptr<SketcherSnapRepository> Catalogue       = {};                             // [-]   - Exact carriers of the shown scene
    size_t                                 SceneFaces      = 0;                              // [-]   - Scene face count the catalogue indexes
    int                                    Snapped         = 0;                              // [-]   - Points placed on object snaps (script evidence)
    bool                                   AlongUsedCondition = false;                       // [-]   - Alt slid along a curve since it went down
};

// 📝 Mode 1 pole drag: pressing on a shown pole grabs it; the pole follows the pointer in the screen-parallel plane
//    through its start; the release commits one MoveSketcherPole (one undo entry), Esc / right-click drops it.
struct SketcherPoleDrag
{
    bool              ActiveCondition = false;                                               // [-]   - Pole grabbed
    std::string       Label           = {};                                                  // [-]   - SpaceCurve step
    int               Pole            = -1;                                                  // [-]   - Pole index
    GeometrySpace::Parametric::Vector3 Origin          = {};                                                  // [len] - Pole at the press
    GeometrySpace::Parametric::Vector3 Position        = {};                                                  // [len] - Pole under the pointer
    GeometrySpace::Parametric::Vector3 Facing          = {};                                                  // [-]   - Drag plane normal (view ray at the press)
    std::string       HoverLabel      = {};                                                  // [-]   - Pole under the pointer (mode 1)
    int               HoverPole       = -1;                                                  // [-]   - Its index
    std::string       PickedLabel     = {};                                                  // [-]   - SpaceCurve step with picked pole handles
    std::vector<int>  PickedPoles     = {};                                                  // [-]   - Picked pole indices for the gizmo
    int               Commits         = 0;                                                   // [-]   - Accepted drags (script evidence)
};

struct SketcherSplineSpans
{
    std::string Label;
    std::vector<int> Indices;
};

// 📝 Pictures behind the document's image planes as the frame last decoded them (one entry per distinct path).
struct SketcherPictureShelf
{
    std::string              Key      = {};                                                  // [-]  - Joined paths of the last upload
    std::vector<std::string> Paths    = {};                                                  // [-]  - Distinct resolved paths, upload order
    std::vector<uint32_t>    Widths   = {};                                                  // [px] - Per path (0: unreadable)
    std::vector<uint32_t>    Heights  = {};                                                  // [px] - Per path
    std::vector<std::string> Refusals = {};                                                  // [-]  - Per path (empty: read)
    int                      Drawn    = 0;                                                   // [-]  - Planes drawn last frame
};

// 📝 Matcap library as the interface last assembled it (Studio + built-ins + the document's pictures) and the Plasticity M
//    picker. Generation changes with every assembly so the frame re-uploads the texture array and thumbnails only then.
struct SketcherMatcapShelf
{
    SketcherMatcapLibrary    Library    = {};                                                // [-] - Layer order names / captions / pictures
    uint64_t                 Generation = 0;                                                 // [-] - Bumped per assembly
    std::vector<std::unique_ptr<ImTextureData>> Thumbnails = {};                             // [-] - Per-layer baked sphere previews
    bool                     PickerOpen = false;                                             // [-] - M: Set material popup
    std::array<char, 260>    PicturePath = {};                                               // [-] - Add-picture path field
};

// 📝 Plasticity Q (command:boolean) on the bodies picked in mode 4: the first is the target, the rest are tools.
//    Q union · W difference · E intersect · Enter applies · Esc cancels.
struct SketcherBooleanSession
{
    bool                         ActiveCondition = false;                                    // [-] - Card open
    GeometrySpace::Parametric::FeatureCombination Combination     = GeometrySpace::Parametric::FeatureCombination::Union;      // [-] - Chosen operation
    bool                         LiveCondition   = false;                                    // [-] - L: keep tools as Live Boolean modifiers
    int                          Commits         = 0;                                        // [-] - Booleans applied (script evidence)
};

// 📝 Plasticity T (command:trim): while open, the sketch curve under the pointer (≤ 8 px) previews in red the piece that
//    would come off; a click trims it and the session stays open for the next piece; Esc, T or right-click ends it.
struct SketcherTrimSession
{
    bool                           ActiveCondition = false;                                  // [-]   - T pressed, not ended
    bool                           HoverCondition  = false;                                  // [-]   - A curve is under the pointer
    std::string                    Sketch          = {};                                     // [-]   - Hovered sketch step
    int                            Curve           = -1;                                     // [-]   - Hovered curve index
    GeometrySpace::Parametric::Vector2              Pick            = {};                                     // [len] - Pointer on that sketch's plane
    std::vector<GeometrySpace::Parametric::Vector3> Preview         = {};                                     // [len] - Piece that would come off (world)
    std::string                    Refusal         = {};                                     // [-]   - Why the hovered piece cannot come off
    int                            Commits         = 0;                                      // [-]   - Trims applied (script evidence)
    bool                           SplitCondition  = false;                                  // [-]   - Cut tool: break the curve at the pointer
    std::vector<GeometrySpace::Parametric::Vector3> Breaks  = {};                                   // [len] - Cut: where the curve would break (world)
    // 💡 Cut with Ctrl: the pointer snaps (crossing, end, midpoint, quarter) and every curve through that point breaks.
    bool                                              PointCondition = false;                // [-]   - A snapped point cut is staged
    GeometrySpace::Parametric::Vector3                       Point          = {};                   // [len] - The snapped point (world)
    std::string                                       PointSource    = {};                   // [-]   - "Intersection · Circle1 …"
    std::vector<std::vector<GeometrySpace::Parametric::Vector3>> PointCurves = {};                  // [len] - Curves that break there (world)
    // 💡 Point tool (V): a click adds a point on the curve where it is, the shape unchanged; the point is then picked.
    bool                                              InsertCondition = false;               // [-]   - Point tool instead of Cut / Trim
    bool                                              SpaceCurveCondition = false;
    double                                            SpaceParameter = 0.0;
    int                                               InsertedPoint   = -1;                  // [-]   - The point the hovered insert would add
};

// 📝 Fill tool (bottom toolbar, next to Trim): while open, the closed region under the pointer among the shown coplanar
//    sketch curves previews filled (any cell of overlapping curves: a lens, a crescent, a square around a hole); a click
//    fills it — the sketch itself when the region is its own loop, else a new filled sketch. Esc or right-click ends it.
struct SketcherFillSession
{
    bool                                    ActiveCondition = false;                         // [-]   - Fill tool open
    bool                                    HoverCondition  = false;                         // [-]   - A region is under the pointer
    std::string                             Sketch          = {};                            // [-]   - Sketch whose plane carries Pick
    GeometrySpace::Parametric::Vector2             Pick            = {};                            // [len] - Pointer on that plane
    std::vector<std::vector<GeometrySpace::Parametric::Vector3>> Outlines = {};                     // [len] - Region loops (world; [0] outer)
    std::string                             WholeSketch     = {};                            // [-]   - The region is this sketch's own loop
    std::vector<std::string>                LoopLabels      = {};                            // [-]   - Closed 3D SpaceCurve loop under pointer
    std::string                             Refusal         = {};                            // [-]   - Why nothing fills under the pointer
    ImVec2                                  Pointer         = { -1.0f, -1.0f };              // [px]  - Pointer of the cached hover
    uint64_t                                Revision        = 0;                             // [-]   - Document revision of the cached hover
    int                                     Commits         = 0;                             // [-]   - Fills applied (script evidence)
};

// 📝 Closed-sketch tint (Plasticity: fresh closed curves show a faint fill until filled): loops per sketch, cached by a
//    fingerprint of the solved points and the fill flag.
struct SketcherSketchTint
{
    double                                               Fingerprint   = 0.0;                // [-]   - Points / curves / fill digest
    bool                                                 FillCondition = false;              // [-]   - Filled (stronger tint)
    std::vector<std::vector<GeometrySpace::Parametric::Vector2>> Outers       = {};                 // [len] - Outer loops (plane coordinates)
    std::vector<std::vector<std::vector<GeometrySpace::Parametric::Vector2>>> Holes = {};           // [len] - Holes per outer loop
};

// 📝 K (constrain, SolveSpace's verbs on Plasticity's free K key) on one sketch step: click points / curves to pick them
//    (a click on a constraint glyph or dimension picks that constraint, a click on empty space clears), then D distance /
//    length / radius and N angle wait for a typed value (Enter; nothing typed keeps the measured one), H V O Q M Y L [ ]
//    apply at once. A press on a point that travels drags it (snapped 0.1 unless Ctrl; the rest of the sketch follows
//    its constraints), the release commits. A picked constraint: Delete / X / Backspace removes it, digits + Enter edit a
//    dimension. Esc drops a pending verb, then the picks, then ends; K or right-click ends.
//    Splines: O point on the curve, H / V with an end point (end direction), L (G1) and C (G2) with a curve meeting an end.
//    Shift+D adds a reference (driven, display-only) dimension, drawn grey in parentheses; E toggles a picked dimension
//    between driving and reference. Earlier sketches draw dimmed and are pickable: a constraint on their geometry links
//    it in (amber, held at its projection, following them on regeneration); Delete on a picked linked point / curve unlinks.
struct SketcherConstraintSession
{
    bool                                ActiveCondition      = false;                         // [-]   - K pressed, not ended
    std::string                         Sketch               = {};                            // [-]   - Sketch step label
    std::vector<SketcherSketchPick>     Picks                = {};                            // [-]   - Picked points / curves (click order; Sketch set: an earlier sketch's)
    bool                                HoverCondition       = false;                         // [-]   - A point / curve is under the pointer
    SketcherSketchPick                  Hover                = {};                            // [-]   - It
    int                                 HoverConstraint      = -1;                            // [-]   - Glyph under the pointer (−1: none)
    int                                 PickedConstraint     = -1;                            // [-]   - Picked glyph (−1: none)
    bool                                PendingCondition     = false;                         // [-]   - D / N waits for a value
    GeometrySpace::Parametric::SketchConstraint          Proposed             = {};                            // [-]   - The constraint D / N would add
    std::string                         Typed                = {};                            // [-]   - Typed value (pending verb or picked dimension)
    bool                                PressCondition       = false;                         // [-]   - Left button went down on a point
    int                                 DragPoint            = -1;                            // [-]   - That point
    bool                                DragCondition        = false;                         // [-]   - The press travelled: dragging
    GeometrySpace::Parametric::Vector2                   DragTarget           = {};                            // [len] - Pointer on the sketch plane (snapped)
    bool                                DragPreviewCondition = false;                         // [-]   - DragPreview is valid
    GeometrySpace::Parametric::SketchModel               DragPreview          = {};                            // [-]   - The sketch as the drag would leave it
    std::string                         Refusal              = {};                            // [-]   - Why the last verb / drag was refused
    int                                 Commits              = 0;                             // [-]   - Constraints added / removed / edited, drags (script evidence)
};

enum class SketcherTransformVerb : uint8_t
{
    Move   = 0,                                                                              // G
    Rotate = 1,                                                                              // R
    Scale  = 2,                                                                              // S
};

// 📝 Plasticity G / R / S on the picked bodies (Shift+D: duplicate, then move). The pointer drives the map from where
//    the session opened: move along the screen plane, an axis (X / Y / Z) or the plane normal to one (Shift+X / Y / Z);
//    rotate by the pointer's sweep about the pivot (screen axis or X / Y / Z); scale by its distance ratio to the pivot
//    (uniform or one axis). Snaps 0.1 / 5° / ×0.1 unless Ctrl is held; typed digits override the pointer (m, °, ×).
//    A staged document shows the result; click / Enter commits one command (one undo entry), Esc / right-click drops it.
enum class SketcherTransformTarget : uint8_t
{
    Bodies       = 0,                                                                        // Whole solid / sheet bodies
    Sketches     = 1,                                                                        // 2D Sketch or 3D SpaceCurve steps
    SketchPoints = 2,                                                                        // Mode-1 picked control points of a sketch
    CurvePoles   = 3,                                                                        // Mode-1 picked poles of a fitted 3D curve
    Vertices     = 4,                                                                        // Picked B-rep vertices (direct reshape)
    Edges        = 5,                                                                        // Picked B-rep edges (direct reshape)
    Faces        = 6,                                                                        // Picked B-rep faces (reshape, else analytic change)
    ImagePlanes  = 7,                                                                        // Focused/selected reference image plane (.ImageSpace / image)
};

struct SketcherTransformSession
{
    bool                              ActiveCondition    = false;                            // [-]   - Session open
    SketcherTransformVerb             Verb               = SketcherTransformVerb::Move;      // [-]   - G / R / S
    SketcherTransformTarget           TargetKind         = SketcherTransformTarget::Bodies;  // [-]   - Entity class transformed
    bool                              DuplicateCondition = false;                            // [-]   - Shift+D: commit copies
    std::vector<std::string>          Keys               = {};                               // [-]   - Bodies / sketches / faces / edges / vertices moved
    GeometrySpace::Parametric::Vector3       FaceNormal         = {};                               // [-]   - Area-weighted outward normal for face targets
    std::string                       PointOwner         = {};                               // [-]   - Sketch / curve label for point/pole targets
    std::vector<int>                  PointIndices       = {};                               // [-]   - Picked sketch points or curve poles
    int                               Axis               = -1;                               // [-]   - −1 screen · 0 x · 1 y · 2 z
    bool                              PlaneCondition     = false;                            // [-]   - Move / Scale in the plane normal to Axis
    bool                              StartCondition     = false;                            // [-]   - Pointer anchor taken
    bool                              LeftDragCondition  = false;                            // [-]   - LMB held & dragged inside modal session
    ImVec2                            Start              = {};                               // [px]  - Anchor (screen)
    ImVec2                            StartLocal         = {};                               // [px]  - Anchor (viewport)
    GeometrySpace::Parametric::Vector3                 Pivot              = {};                               // [len] - Bounds centre of the target
    GeometrySpace::Parametric::Vector3                 DisplayPivot       = {};                               // [len] - Live shifted pivot for gizmo & axis line
    bool                              AnchorCondition    = false;                            // [-]   - Move started on an object snap
    GeometrySpace::Parametric::Vector3                 Anchor             = {};                               // [len] - That snap point (moves onto the target snap)
    std::string                       AnchorSource       = {};                               // [-]   - "Endpoint · Edge …"
    std::string                       Typed              = {};                               // [-]   - Typed amount
    double                            CurrentValue       = 0.0;                              // [-]   - Live scalar (m, °, ×) shown in Inspector numeric field
    std::array<char, 48>              ValueText          = {};                               // [-]   - Editable numeric text buffer in Inspector session card
    GeometrySpace::Parametric::AffineMapping     Map                = {};                               // [-]   - Current map
    GeometrySpace::Parametric::AffineMapping               StagedMap          = {};                               // [-]   - Map the preview shows
    bool                              StagedCondition    = false;                            // [-]   - A preview was built
    std::unique_ptr<SketcherDocument> Preview            = {};                               // [-]   - Staged document
    SketcherScene                     PreviewScene       = {};                               // [-]   - Its display arrays
    bool                              RigidCondition     = false;                            // [-]   - PreviewScene is the shown scene mapped (no regeneration)
    std::string                       Readout            = {};                               // [-]   - "Move x 0.3"
    std::string                       Refusal            = {};                               // [-]   - Why commit is refused
    int                               Commits            = 0;                                // [-]   - Committed (script evidence)
    bool                              SketchCondition    = false;                            // [-]   - Keys are sketch steps (2D objects), no bodies picked
};

// 📝 Multi-view split layouts inside the docked Viewport panel (never undocked; draggable splitters adjust SplitX / SplitY).
enum class SketcherViewportLayout : uint8_t
{
    Single        = 0,                                                                       // 1 view (full viewport)
    TwoVertical   = 1,                                                                       // 2 views side-by-side (Left | Right)
    TwoHorizontal = 2,                                                                       // 2 views stacked (Top / Bottom)
    Three         = 3,                                                                       // 3 views (Top-Left + Bottom-Left | Right)
    Quad          = 4,                                                                       // 4 views CAD Quad (Top | 3D / Front | Right)
};

struct SketcherSplitViewMode
{
    SketcherViewportLayout              Layout          = SketcherViewportLayout::Single;    // [-]   - Active split layout
    SketcherViewportLayout              LastMultiLayout = SketcherViewportLayout::Quad;      // [-]   - Restored on Ctrl+Alt+Q toggle
    float                               SplitX          = 0.5f;                              // [0-1] - Vertical divider position
    float                               SplitY          = 0.5f;                              // [0-1] - Horizontal divider position
    int                                 ActivePane      = 0;                                 // [idx] - Focused pane (0 .. PaneCount-1)
    bool                                Initialised     = false;                             // [-]   - Per-pane preset cameras seeded
    bool                                DraggingX       = false;                             // [-]   - Dragging vertical splitter
    bool                                DraggingY       = false;                             // [-]   - Dragging horizontal splitter
    int                                 PopupPane       = -1;                                // [idx] - Pane whose view dropdown is open
    std::array<OrbitCamera, 4>          Cameras         = {};                                // [-]   - Independent OrbitCamera per pane
    std::array<std::array<float, 4>, 4> PaneRects       = {};                                // [px]  - Screen x, y, width, height per pane
    std::array<float, 4>                FullRect        = { 0, 0, 1, 1 };                    // [px]  - Outer multi-pane viewport rect
};

// 📝 HTML .inspector-tabs: Geometry (the native cards), Transform (G / R / S), Topology (body audit, picks, history).
enum class SketcherInspectorTab : uint8_t
{
    Geometry  = 0,                                                                           // Dimensions, bounds, operations, tool cards
    Transform = 1,                                                                           // Bounds and move / rotate / scale
    Topology  = 2,                                                                           // Body audit, picked figures, history
    History   = 3,                                                                           // Construction history (feature steps), the step card
};

enum class SketcherSweepVerb : uint8_t
{
    Offset = 0,                                                                              // O: offset-curve
    Pipe      = 1,                                                                           // P: pipe
    Revolve   = 2,                                                                           // Alt+R: revolution (no Plasticity default key)
    Evolution = 3,                                                                           // Shift+P / Alt+E: evolution
};

// 📝 Plasticity O (offset the focused sketch: a new sketch) and P (a tube along the focused sketch or the selected curve):
//    horizontal pointer motion sets the distance / radius (Shift fine, snapped 0.01 unless Ctrl), typed digits override;
//    P takes Q / W / E / R like extrude. The offset previews as an outline, the pipe as a staged document.
//    Alt+R turns the focused sketch about an axis (typed degrees, default 360; X / Y sketch axes; a click on one of its
//    lines makes that the axis). Shift+P / Alt+E sweeps the focused sketch along a path (the selected curve or the newest
//    one-curve sketch; a click on another sketch's curve picks it). Both stage the result and take Q / W / E / R.
struct SketcherSweepSession
{
    bool                                        ActiveCondition   = false;                   // [-]   - Session open
    SketcherSweepVerb                           Verb              = SketcherSweepVerb::Offset; // [-] - O / P
    std::string                                 Path              = {};                      // [-]   - Sketch step / curve label (Revolve: profile; Evolution: path)
    std::string                                 Profile           = {};                      // [-]   - Evolution: profile sketch
    SketcherRevolveAxis                         Axis              = {};                      // [-]   - Revolve: axis
    std::string                                 StagedKey         = {};                      // [-]   - Revolve / Evolution: axis / path the preview shows
    std::string                                 HoverSketch       = {};                      // [-]   - Revolve / Evolution: sketch under the pointer
    int                                         HoverCurve        = -1;                      // [-]   - Its curve index (−1: none)
    double                                      Amount            = 0.1;                     // [len] - Offset distance / pipe radius ([°] revolve angle)
    double                                      Drift             = 0.0;                     // [len] - Unsnapped pointer amount
    std::string                                 Typed             = {};                      // [-]   - Typed amount
    GeometrySpace::Parametric::FeatureCombination                Combination       = GeometrySpace::Parametric::FeatureCombination::Separate; // [-] - Pipe: Q / W / E / R
    double                                      StagedAmount      = -1.0e300;                // [len] - Amount the preview shows
    GeometrySpace::Parametric::FeatureCombination                StagedCombination = GeometrySpace::Parametric::FeatureCombination::Separate; // [-] - Its combination
    std::unique_ptr<SketcherDocument>           Preview           = {};                      // [-]   - Pipe: staged document
    SketcherScene                               PreviewScene      = {};                      // [-]   - Its display arrays
    std::vector<std::vector<GeometrySpace::Parametric::Vector3>> Outline           = {};                      // [len] - Offset: preview curves (world)
    std::string                                 Refusal           = {};                      // [-]   - Why commit is refused
    int                                         Commits           = 0;                       // [-]   - Committed (script evidence)
};

enum class SketcherKnifeMode : uint8_t
{
    Sketch = 0,                                                                              // A closed sketch cut to a depth / through
    Insert = 1,                                                                              // A library cutter placed on a face
    Object = 2,                                                                              // Picked bodies: the first is cut by the rest (one: placed by the pointer)
};

// 📝 A polyline knife's edge treatment: an edge of a piece the slice makes, filleted or chamfered on apply (each its own
//    blend step after the cut, in the order picked; the preview shows them).
struct SketcherKnifeBlend
{
    std::string                      Edge  = {};                                             // [-]   - Edge name on the sliced piece
    std::string                      Body  = {};                                             // [-]   - Piece it was picked on
    GeometrySpace::Parametric::BlendProfile Shape = GeometrySpace::Parametric::BlendProfile::Fillet;      // [-]   - Fillet / chamfer
    double                           Size  = 0.05;                                           // [len] - Radius / setback
};

// 📝 Shift+K Knife (Blender KitOps style). Sketch: the focused closed sketch (or a shape drawn for it on a face) is filled
//    and extruded into the body under it — horizontal pointer motion sets the depth (Shift fine, snapped 0.01 unless
//    Ctrl), digits type it, T cuts straight through. Insert: the library cutter ghost follows the faces under the pointer;
//    the press puts one base corner down, the drag (or, after a click, the pointer) carries the opposite corner — the
//    base stretches in x and y apart, Shift keeps it square — and the release (the next click) hands the pointer to its
//    depth along the normal (as a Construct box's height), [ / ] turn it 15°, T through. Object: the first picked body is cut by the others, K
//    keeps the cutters. Q union · W difference (default) · E intersect · R separate; LMB / Enter applies and RMB / Esc cancels,
//    Esc cancels. The result stages as a document (gated like the bevel when a stage is slow).
struct SketcherKnifeSession
{
    bool                                   ActiveCondition      = false;                     // [-]   - Session open
    SketcherKnifeMode                      Mode                 = SketcherKnifeMode::Sketch; // [-]   - Cutter source
    GeometrySpace::Parametric::FeatureCombination Combination          = GeometrySpace::Parametric::FeatureCombination::Difference; // [-] - Q / W / E / R
    std::string                            Sketch               = {};                        // [-]   - Sketch: profile step label
    std::string                            Face                 = {};                        // [-]   - Sketch: face it was drawn on (target hint)
    bool                                   DrawPendingCondition = false;                     // [-]   - Sketch: a shape is being drawn for the knife
    bool                                   ThroughCondition     = false;                     // [-]   - Sketch: T, straight through the body
    double                                 Depth                = 0.1;                       // [len] - Sketch: cut depth below the plane
    double                                 Drift                = 0.1;                       // [len] - Unsnapped pointer amount (depth / size)
    std::string                            Typed                = {};                        // [-]   - Typed depth / size
    int                                    Insert               = 0;                         // [idx] - Insert: library index
    bool                                   HoverCondition       = false;                     // [-]   - Insert: a face is under the pointer
    bool                                   PlacedCondition      = false;                     // [-]   - Insert: centre put down (drag, then depth)
    bool                                   SizingCondition      = false;                     // [-]   - Insert: press held, the drag sizes and turns it
    bool                                   SizedCondition       = false;                     // [-]   - Insert: size chosen by hand (no auto size)
    bool                                   DepthChosenCondition = false;                     // [-]   - Insert: depth set by hand (else it keeps the shape)
    double                                 InsertDepth          = 0.075;                     // [len] - Insert: below the face (above it for Q)
    bool                                   InsertThroughCondition = false;                   // [-]   - Insert: T, straight through (this session only)
    GeometrySpace::Parametric::OrthonormalBasis   Spot                 = {};                        // [len] - Insert: surface point + face frame
    std::string                            SpotBody             = {};                        // [-]   - Insert: body under the spot
    double                                 Size                 = 0.25;                      // [len] - Insert: width
    double                                 Turn                 = 0.0;                       // [rad] - Insert: about the face normal
    bool                                   KeepCondition        = false;                     // [-]   - Object: K, cutters survive
    bool                                   LiveCondition        = false;                     // [-]   - L: place as Live Boolean cutter + modifier
    // 📝 Object with one picked body: that body is the cutter, carried to the face under the pointer (Ctrl: its z along
    //    the face normal); LMB places it, then the pointer pushes it in / pulls it out along the normal; RMB cancels.
    bool                                   AlignCondition       = false;                     // [-]   - Placement: cutter z along the face normal (Ctrl)
    double                                 Sink                 = 0.0;                       // [len] - Placement: cutter centre below the face (− above)
    bool                                   RigidCondition       = false;                     // [-]   - PreviewScene is the carried cutter (no Boolean yet)
    std::string                            GhostKey             = {};                        // [-]   - Placement the carried cutter shows
    std::string                            Target               = {};                        // [-]   - Body the staged knife cuts
    std::string                            StagedKey            = {};                        // [-]   - Parameters the preview shows
    SketcherCommandOutcome                 PreviewOutcome       = {};                        // [-]   - The staged command (adopted on apply)
    double                                 StageMilliseconds    = 0.0;                       // [ms]  - Last stage time
    double                                 QuietSince           = 0.0;                       // [s]   - Last staged preview completion (steady clock)
    std::unique_ptr<SketcherDocument>      Preview              = {};                        // [-]   - Staged document
    SketcherScene                          PreviewScene         = {};                        // [-]   - Its display arrays
    std::string                            Refusal              = {};                        // [-]   - Why apply is refused
    int                                    Commits              = 0;                         // [-]   - Applied (script evidence)
    double                                 Removed              = 0.0;                       // [len³] - Volume the last apply took away (script evidence)
    SketcherInsertLibrary                  Library              = {};                        // [-]   - Built-ins + library folder (lazy)
    bool                                   LibraryCondition     = false;                     // [-]   - Library assembled
    // 📝 Toolbar tool menu: the cutter Shift+K and the knife icon open with (kept across sessions).
    SketcherKnifeMode                      PreferredMode        = SketcherKnifeMode::Sketch; // [-]   - Sketch shape or library insert
    SketcherDrawingCategory                    PreferredShape       = SketcherDrawingCategory::Circle; // [-] - Sketch: the shape drawn first
    bool                                   PreferredSolidCondition = false;                  // [-]   - The cutter is a Construct primitive (3D)
    SketcherConstructCategory                  PreferredSolid       = SketcherConstructCategory::Block; // [-] - That primitive (box, cylinder, …)
    bool                                   PreferredPolylineCondition = false;               // [-]   - Line cutter: a polyline (Enter / right-click ends)
    // 📝 Insert / Compound Boolean Library: built-in 2-stage Compound Cutters (0..5: Countersink, Counterbore, Stepped hole,
    //    Hex socket, Stepped slot, Stepped pocket) are drawn stage-by-stage on the face without non-uniform stretching:
    //    Stage 0 place centre → Stage 1 outer radius → Stage 2 outer extrude → Stage 3 inner radius → Stage 4 inner extrude.
    //    User-saved custom STEP files (≥ 6) size uniformly (Aspect = 1.0, centre → radius → depth).
    int                                    CompoundStage        = 0;                         // [-]   - 0 place · 1 outer R · 2 outer D · 3 inner R · 4 inner D
    double                                 OuterRadius          = 0.12;                      // [len] - Compound Stage 1 radius / half-width
    double                                 OuterDepth           = 0.05;                      // [len] - Compound Stage 1 extrude depth
    double                                 InnerRadius          = 0.06;                      // [len] - Compound Stage 2 radius / half-width
    double                                 InnerDepth           = 0.12;                      // [len] - Compound Stage 2 extrude depth
    GeometrySpace::Parametric::Vector3            Corner               = {};                        // [len] - Insert: centre anchor (world)
    double                                 Aspect               = 1.0;                       // [-]   - Always 1.0 (uniform, non-stretching)
    bool                                   SizeByMoveCondition  = false;                     // [-]   - Clicked: pointer drives stage until next click
    // 📝 Slice (polyline) knife: click edges on the staged pieces to fillet / chamfer each before the cut applies; D / C
    //    turn the focused one, digits type its size.
    std::vector<SketcherKnifeBlend>        Blends               = {};                        // [-]   - Edge treatments, picked order
    int                                    BlendFocus           = -1;                        // [idx] - Treatment D / C / digits act on
    GeometrySpace::Parametric::BlendProfile       BlendShape           = GeometrySpace::Parametric::BlendProfile::Fillet;   // [-] - Next treatment's profile
    double                                 BlendSize            = 0.05;                      // [len] - Next treatment's size
    // 💡 2D cutter (the menu's own section): the sketch shape drawn is imprinted into the face it lies on — the face
    //    splits along the outline, nothing is cut, no depth stage — ready for face E to push it in or pull it out.
    bool                                   PreferredImprintCondition = false;                // [-]   - Sketch shapes imprint (2D)
};

// 📝 Ctrl+C: the picked bodies and the step they were copied after; Ctrl+V pastes them as they stood then (in place).
struct SketcherClipboard
{
    std::vector<std::string> Keys   = {};                                                    // [-] - Copied body keys
    std::string              Source = {};                                                    // [-] - Last step at copy time
};

// 📝 Box selection (Plasticity viewport-selector drag): a left drag from empty viewport space in modes 1 / 2 / 5 selects
//    every curve whose projection lies wholly inside the box, in feature-tree order (Shift adds, Ctrl removes); in modes
//    2 / 5 also every B-rep edge wholly inside — the visible ones, or hidden ones too in x-ray (EncloseSketcherEdges).
struct SketcherBoxSelection
{
    bool   ActiveCondition = false;                                                          // [-]  - Left button held after a press on empty space
    ImVec2 Start           = {};                                                             // [px] - Press position (screen)
    ImVec2 End             = {};                                                             // [px] - Pointer (screen)
    int    Commits         = 0;                                                              // [-]  - Box selections applied (script evidence)
    int    Edges           = 0;                                                              // [-]  - B-rep edges the last box enclosed
};

// 📝 Measure (I; Fusion Inspect → Measure — Plasticity has none): the Inspector card and viewport overlay (dimension line
//    between the closest points, centroid marker) of MeasureSketcherSelection, recomputed only when the selection or
//    the document revision changes.
struct SketcherMeasureSession
{
    bool                ActiveCondition = false;                                             // [-] - Card + overlay shown
    SketcherMeasurement Measurement     = {};                                                // [-] - Last exact measurement
    int                 Refreshes       = 0;                                                 // [-] - Recomputations (script evidence)
};

// 📝 Section (Alt+S; Fusion Section Analysis): the viewport clips everything on the removed side of the plane and caps
//    the cut solids on the GPU; the card shows the exact kernel section (SectionSketcherDocument), recomputed when the
//    request or the revision changes. The plane keeps its +normal side, as the kernel cut does.
struct SketcherSectionSession
{
    bool                   ActiveCondition = false;                                          // [-] - Clipping + card shown
    SketcherSectionRequest Request         = {};                                             // [-] - Plane choice
    SketcherSection        Section         = {};                                             // [-] - Last exact section
    std::array<char, 32>   OffsetText      = {};                                             // [-] - Offset field ("0.5", "250mm")
    SketcherSectionClip    Clip            = {};                                             // [-] - Plane + sheet faces for the CPU pick
    int                    Refreshes       = 0;                                              // [-] - Recomputations (script evidence)
};

// 📝 Drawing sheet (Alt+D; Fusion Drawing — Plasticity has none): the viewport shows the paper of the shown solid
//    bodies (kernel views, hidden lines, dimensions, title block) instead of the model; wheel zooms about the pointer,
//    middle / right drag pans, double-click fits. The card picks paper, angle, scale, views and the title fields (saved
//    with the document) and exports SVG / DXF R12 beside the document. Esc or Alt+D returns to the model.
struct SketcherDrawingSheetSession
{
    bool                  ActiveCondition = false;                                           // [-]  - Paper shown + card
    SketcherDrawingSheet  Sheet           = {};                                              // [-]  - Last composition
    float                 Zoom            = 1.0f;                                            // [-]  - × the fitted size
    std::array<float, 2>  Pan             = {};                                              // [px] - Paper centre offset
    std::array<char, 96>  TitleText       = {};                                              // [-]  - Title field
    std::array<char, 96>  NumberText      = {};                                              // [-]  - Drawing number field
    std::array<char, 96>  AuthorText      = {};                                              // [-]  - Drawn-by field
    std::array<char, 96>  MaterialText    = {};                                              // [-]  - Material field
    std::string           ExportPath      = {};                                              // [-]  - Last written file
    std::string           ExportRefusal   = {};                                              // [-]  - Last export refusal
    double                ExportBytes     = 0.0;                                             // [B]  - Size of the last file
    int                   Refreshes       = 0;                                               // [-]  - Compositions (script evidence)
    int                   Exports         = 0;                                               // [-]  - Files written
};

struct SketcherIsolateMode
{
    bool                  ActiveCondition = false;                                           // [-] - Ctrl+I isolate toggle active
    std::set<std::string> HiddenBodies    = {};                                              // [-] - Pre-isolate HiddenBodies snapshot
    std::set<std::string> HiddenCurves    = {};                                              // [-] - Pre-isolate HiddenCurves snapshot
};

// 📝 Interactive 3D CAD Dimension Tool session (D / Dim toolbar button): click Point A then Point B (or click a circle,
//    arc, or edge directly), drag/move to position the dimension offset parallel to the edge or radially around the circle,
//    and place an associative dimension that follows the drawing or 3D shape across moves, rotations, scales, and edits.
struct SketcherDimensionSession
{
    bool                                        ActiveCondition     = false;                 // [-]   - Interactive Dimension Tool active
    int                                         Stage               = 0;                     // [-]   - 0 pick start/entity · 1 pick end · 2 position offset
    SketcherDimensionCategory                       PreviewKind         = SketcherDimensionCategory::Linear; // [-] - Linear, Radius, Diameter, Angular
    SketcherSelectionCategory                       FirstKind           = SketcherSelectionCategory::Vertex; // [-] - First associative entity kind
    SketcherSelectionCategory                       SecondKind          = SketcherSelectionCategory::Vertex; // [-] - Second associative entity kind
    GeometrySpace::Parametric::Vector3                 FirstPoint          = {};                    // [len] - Snapped start anchor (or circle/arc centre)
    GeometrySpace::Parametric::Vector3                 SecondPoint         = {};                    // [len] - Snapped end anchor (or circle/arc rim)
    GeometrySpace::Parametric::Vector3                 HoverPoint          = {};                    // [len] - Live snapped hover point
    bool                                        HoverCondition      = false;                 // [-]   - HoverPoint valid
    std::string                                 FirstEntity         = {};                    // [-]   - Persistent sketch/body entity name
    std::string                                 SecondEntity        = {};                    // [-]   - Persistent sketch/body entity name
    GeometrySpace::Parametric::Vector3                 FirstLineDir        = {};                    // [-]   - Direction when Stage 1/2 began on a line/edge
    bool                                        FirstLineCondition  = false;                 // [-]   - Stage 2 can accept a 2nd line for Angular °
    GeometrySpace::Parametric::Vector3                 PlaneNormal         = { 0.0, 0.0, 1.0 };     // [-]   - Witness-line / circle plane normal
    GeometrySpace::Parametric::Vector3                 Direction           = { 1.0, 0.0, 0.0 };     // [-]   - Radial leader unit vector or 2nd angle ray
    double                                      Radius              = 0.0;                   // [len] - Circle / arc radius during Stage 2 preview
    double                                      Offset              = 0.35;                  // [len] - Parallel witness offset or radial leader extension
    int                                         AxisFilter          = -1;                    // [-]   - -1 parallel/aligned, 0 dX, 1 dY, 2 dZ
    int                                         SlideAxis           = -1;                    // [-]   - -1 plane-perpendicular, 0 slide X, 1 slide Y, 2 slide Z
    std::string                                 TypedOffset         = {};                    // [-]   - Optional typed numeric slide offset
    float                                       TypedMouseX         = 0.0f;                  // [px]  - Cursor X when numeric typing locked offset
    float                                       TypedMouseY         = 0.0f;                  // [px]  - Cursor Y when numeric typing locked offset
    std::vector<GeometrySpace::Parametric::Vector3>    HoverHighlight      = {};                    // [len] - Hovered circle / arc / edge preview polyline
    std::string                                 HoverBadge          = {};                    // [-]   - Hover snap badge ("Corner", "Circle Ø", "Arc R", …)
    float                                       Stage2PressX        = 0.0f;                  // [px]  - Screen X where Stage 2 began
    float                                       Stage2PressY        = 0.0f;                  // [px]  - Screen Y where Stage 2 began
    bool                                        Stage2HoldCondition = false;                 // [-]   - LMB held from Stage 2 entry (drag-release places)
    std::string                                 SelectedId          = {};                    // [-]   - Selected dimension ID
    bool                                        ExplicitSelectCondition = false;             // [-]   - SelectedId chosen explicitly in viewport/Inspector
    std::string                                 DraggingId          = {};                    // [-]   - Dimension ID being LMB-dragged in viewport
    std::string                                 EditingId           = {};                    // [-]   - Placed dimension ID in modal Cutter/G/X/Y/Z slide mode
    double                                      DragLiveOffset      = 0.0;                   // [len] - Live dragged offset before release/click commit
    GeometrySpace::Parametric::Vector3                 DragLiveDirection   = { 1.0, 0.0, 0.0 };     // [-]   - Live dragged radial direction before release commit
    int                                         DragLiveSlideAxis   = -1;                    // [-]   - Live slide axis during placed-dimension edit/drag
    bool                                        DragMovedCondition  = false;                 // [-]   - Cursor or axis changed during dimension drag/edit
};

// 📝 STEP / STL exchange (priority 4 X4; Plasticity File → Import / Export, and Open / Save as by file type): Export…
//    writes the selected bodies (else every shown body) beside the document as STEP AP214 or binary STL in millimetres;
//    Import… reads .step / .stp (one Import step per solid, the file embedded) or VehicleCurves .toml.
struct SketcherExchangeSession
{
    SketcherExportOutcome Export             = {};                                           // [-]  - Last export (path, bodies, size, refusal)
    std::string           ImportPath         = {};                                           // [-]  - Last STEP file read
    std::string           ImportRefusal      = {};                                           // [-]  - Why the last STEP import was refused
    int                   ImportSolids       = 0;                                            // [-]  - Solids the last import added
    int                   ImportFaces        = 0;                                            // [-]  - Faces the last import read
    int                   ImportRepairs      = 0;                                            // [-]  - Loops reoriented + tolerant edges
    double                ImportMilliseconds = 0.0;                                          // [ms] - Read + canonical rewrite + regeneration
    int                   Exports            = 0;                                            // [-]  - Files written (script evidence)
    int                   Imports            = 0;                                            // [-]  - STEP files imported (script evidence)
};

// 📝 Joint (Alt+J; Fusion Joint — Plasticity has none): ① pick the joint origin on the moving body, ② on the fixed body
//    (the hovered edge / face shows its origin glyph: z blue, x red), ③ choose the kind (R rigid · V revolute · S slider ·
//    C cylindrical · P pin-slot · L planar · B ball), drag the values, F flips, Enter applies. Two entities picked beforehand
//    (on two bodies; the later pick is the fixed one) skip ① and ②. The preview is a staged document.
struct SketcherJointSession
{
    bool                              ActiveCondition = false;                               // [-] - Alt+J pressed, not ended
    int                               Phase           = 0;                                   // [-] - 0 moving origin · 1 fixed origin · 2 adjust
    SketcherJointRequest              Request         = {};                                  // [-] - Picks, kind and values
    SketcherSelectionCategory             HoverKind       = SketcherSelectionCategory::Edge;         // [-] - Entity under the pointer
    std::string                       HoverName       = {};                                  // [-] - Its name (empty: none)
    SketcherJointOrigin               Hover           = {};                                  // [-] - Its joint origin (glyph)
    SketcherJointOrigin               Moving          = {};                                  // [-] - Picked origin on the moving body
    SketcherJointOrigin               Fixed           = {};                                  // [-] - Picked origin on the fixed body
    std::string                       StagedKey       = {};                                  // [-] - Request the preview shows
    std::unique_ptr<SketcherDocument> Preview         = {};                                  // [-] - Staged document
    SketcherScene                     PreviewScene    = {};                                  // [-] - Its display arrays
    std::string                       Refusal         = {};                                  // [-] - Why the preview / commit is refused
    int                               Commits         = 0;                                   // [-] - Joints applied (script evidence)
};

// 📝 Dragging a joint value in the step card: the staged document shows the motion (replayed from the joint), the
//    document takes the value once on release (one undo entry).
struct SketcherParameterDrag
{
    std::string                       Parameter    = {};                                     // [-] - Parameter being dragged (empty: none)
    double                            Value        = 0.0;                                    // [-] - Value the preview shows
    std::unique_ptr<SketcherDocument> Preview      = {};                                     // [-] - Staged document
    SketcherScene                     PreviewScene = {};                                     // [-] - Its display arrays
    int                               Stagings     = 0;                                      // [-] - Previews built (script evidence)
};

// 📝 What the gizmo acts on, chosen from the selection: faces (mode 3 with faces picked) → face operations; bodies →
//    Transform steps; sketches (loft sections, picked in mode 1 / 2 / 5 or Shift-clicked in the outliner) → plane moves.
enum class SketcherGizmoTarget : uint8_t
{
    None     = 0,
    Faces    = 1,                                                                            // Offset / Tweak / Rotate / Scale face operations
    Bodies   = 2,                                                                            // TransformSketcherBodies
    Sketches = 3,                                                                            // PlaceSketcherSketches (Sketch and SpaceCurve steps)
    Points   = 4,                                                                            // Mode-1 / selected-curve sketch points (Drag / LiftSketcherSketchPoints)
    Shape    = 5,                                                                            // Mode-1 body vertices / mode-2 edges (ReshapeSketcherSelection)
    Poles    = 6,                                                                            // SpaceCurve NURBS control poles (MoveSketcherPoles)
};

// 📝 Mode 1 sketch points: every sketch shows its defining points (line ends, arc and circle centres, spline points;
//    Bezier / NURBS with their control polygon). A click picks a point (Shift adds, Ctrl removes) and the gizmo moves the
//    picked points in the sketch plane (several together also rotate and scale about their centre); a press-drag on a
//    point moves it directly (0.1 grid, Ctrl free). The sketch re-solves its constraints and every step using it
//    (extrude, loft) regenerates live; the release commits one command.
struct SketcherSketchPointSession
{
    std::string                       Sketch        = {};                                    // [-]   - Picked points' sketch step
    std::vector<int>                  Points        = {};                                    // [-]   - Picked point indices
    std::string                       HoverSketch   = {};                                    // [-]   - Point under the pointer: sketch
    int                               HoverPoint    = -1;                                    // [-]   - Its index
    bool                              DragCondition = false;                                 // [-]   - A point is held
    std::string                       DragSketch    = {};                                    // [-]   - Held point's sketch
    int                               DragPoint     = -1;                                    // [-]   - Held point
    GeometrySpace::Parametric::Vector2       Origin        = {};                                    // [len] - Held point at the press (plane)
    GeometrySpace::Parametric::Vector2       Target        = {};                                    // [len] - Where it is dragged (plane, snapped)
    std::string                       StagedKey     = {};                                    // [-]   - Target the preview shows
    std::unique_ptr<SketcherDocument> Preview       = {};                                    // [-]   - Staged document
    SketcherScene                     PreviewScene  = {};                                    // [-]   - Its display arrays
    std::string                       Refusal       = {};                                    // [-]   - Why the staged target refuses
    int                               Commits       = 0;                                     // [-]   - Committed point drags (script evidence)
};

// 💡 Viewport transform gizmo (Slate GizmoPRO): Modes picks any mix of move / rotate / scale (y cycles, toolbar chips,
//    script `gizmo`). The handle triangles live here on the CPU (pick) and in Style.Gizmo (GPU draw). A drag keeps the
//    start frame fixed, stages one preview per distinct value and commits one command on release (Esc cancels).
struct SketcherGizmoSession
{
    uint32_t                          Modes         = GizmoTranslate;                        // [-]   - GizmoTranslate | GizmoRotate | GizmoScale (0 off)
    SketcherGizmoTarget               Target        = SketcherGizmoTarget::None;             // [-]   - What the selection offers
    bool                              FlatCondition = false;                                 // [-]   - A 90° view: only the screen-plane handles (FitGizmoToView)
    std::vector<std::string>          Names         = {};                                    // [-]   - Face names / body keys / sketch labels
    std::vector<int>                  Points        = {};                                    // [-]   - Sketch point indices (Points target)
    GizmoPose                 Pose          = {};                                    // [-]   - Frame shown (start frame while dragging)
    GizmoGeometry             Geometry      = {};                                    // [-]   - CPU copy of the drawn triangles (pick)
    GizmoHandle               Hover         = GizmoHandle::None;             // [-]   - Handle under the pointer
    bool                              DragCondition = false;                                 // [-]   - A handle is held
    GizmoHandle               Grab          = GizmoHandle::None;             // [-]   - Held handle
    GeometrySpace::Parametric::Vector3       GrabPoint     = {};                                    // [len] - Plane / screen hit at the press
    double                            GrabValue     = 0.0;                                   // [-]   - Axis parameter [len] or angle [rad] at the press
    ImVec2                            GrabPixel     = {};                                    // [px]  - Press position (viewport local)
    GeometrySpace::Parametric::Vector3       Shift         = {};                                    // [len] - Current move
    double                            Angle         = 0.0;                                   // [rad] - Current turn (snapped)
    double                            Sweep         = 0.0;                                   // [rad] - Unsnapped turn, unwrapped past ±π
    double                            Factor        = 1.0;                                   // [-]   - Current scale
    std::string                       StagedKey     = {};                                    // [-]   - Value the preview shows
    std::unique_ptr<SketcherDocument> Preview       = {};                                    // [-]   - Staged document
    SketcherCommandOutcome            PreviewOutcome  = {};                                  // [-]   - The staged command (adopted on release)
    uint64_t                          PreviewRevision = 0;                                   // [-]   - Document revision it was staged from
    SketcherScene                     PreviewScene  = {};                                    // [-]   - Its display arrays
    bool                              RigidCondition = false;                                // [-]   - PreviewScene is the shown scene mapped (no regeneration)
    std::string                       Readout       = {};                                    // [-]   - "X  move 0.250"
    std::string                       Refusal       = {};                                    // [-]   - Why the staged value refuses
    double                            StageMilliseconds = 0.0;                               // [ms]  - Last non-rigid stage duration
    double                            QuietSince    = 0.0;                                   // [s]   - Last real pointer motion
    int                               Commits       = 0;                                     // [-]   - Committed drags (script evidence)
    bool                              VertexCondition = false;                               // [-]   - Shape target: Names are vertices (mode 1), else edges (mode 2)
    GeometrySpace::Parametric::Vector3       FaceNormal    = { 0.0, 0.0, 1.0 };                     // [-]   - Mean outward normal for Faces target (Offset vs Tweak)
};

// 📝 Frame timing for the trace (SketcherTrace.log / F12): each frame records named CPU phases; a frame slower than
//    SlowThreshold writes one "Frame" line with the total, the host's GPU wait and upload, and the costliest phases.
struct SketcherRenderPhase
{
    const char* Name         = "";                                                            // [-]  - Static phase name
    double      Milliseconds = 0.0;                                                           // [ms] - Time this frame
    int         Calls        = 0;                                                             // [-]  - Entries this frame
};

struct SketcherRenderProfile
{
    std::vector<SketcherRenderPhase> Phases        = {};                                       // [-]  - This frame's phases (entry order)
    double                          InterfaceMs   = 0.0;                                      // [ms] - DrawSketcherInterface, whole
    double                          UploadMs      = 0.0;                                      // [ms] - Host: PrepareViewport (scene / matcap uploads)
    double                          GpuWaitMs     = 0.0;                                      // [ms] - Host: fence wait for the previous frame
    double                          SlowThreshold = 50.0;                                     // [ms] - Frame time that writes a trace line
    uint64_t                        SlowFrames    = 0;                                        // [-]  - Lines written this session
    double                          WorstMs       = 0.0;                                      // [ms] - Slowest frame this session
    double                          LineGap       = 0.25;                                     // [s]  - Least time between two lines (a slow drag stays legible)
    double                          LineSeconds   = -1.0e9;                                   // [s]  - When the last line was written
    uint64_t                        Suppressed    = 0;                                        // [-]  - Slow frames since the last line (inside LineGap)
};

// Adds the scope's duration to the named phase of Profile (one entry per name per frame).
class SketcherPhaseClock
{
public:
    SketcherPhaseClock(SketcherRenderProfile& Profile, const char* Name);
    ~SketcherPhaseClock();

    SketcherPhaseClock(const SketcherPhaseClock&)            = delete;
    SketcherPhaseClock& operator=(const SketcherPhaseClock&) = delete;

private:
    SketcherRenderProfile& Profile;                                                            // [-]  - Where the time goes
    const char*           Name;                                                               // [-]  - Phase name (static text)
    double                Start;                                                              // [s]  - Entry time (steady clock)
};

struct SketcherWorkspaceStatus
{
    bool SurfaceTraceEnabled = false;
    bool DrawingCurveSnap = true;
    bool DrawingAutoSplit = true;
    bool DrawingAutoSurface = true;
    bool DrawingOptionsVisible = true;
    double DrawingIntersectionTolerance = 0.001;
    double SurfaceOffsetDistance = 0.05;
    double SurfaceOffsetTolerance = 0.002;
    std::string SurfaceOffsetBody;
    std::string SurfaceOffsetPair;
    std::string SurfaceOffsetReadback;
    bool SurfaceOffsetPending = false;
    std::string SurfaceOffsetFeedback;
    bool SurfaceOffsetFailed = false;
    bool SurfaceOffsetAutoApply = true;
    bool CurveGuideVisible = false;
    PanelDockspace WorkspacePanels;
    ImTextureID WorkspaceViewportTexture = {};
    bool WorkspacePanelsRegistered = false;

    bool FullXYZEnabled = false;
    SketcherOptions                       Options        = {};                               // [-]   - Loaded options
    SketcherDocument                      Document       = {};                               // [-]   - Authored document
    SketcherScene                         Scene          = {};                               // [-]   - Display arrays of the document
    std::optional<SketcherScene>          ReadyScene     = {};                               // [-]   - Arrays of an adopted preview (next refresh takes them)
    OrbitCamera                        Camera         = {};                               // [-]   - Viewport camera
    SketcherViewportStyle                 Style          = {};                               // [-]   - Viewport appearance
    SketcherPickMode                      PickMode       = SketcherPickMode::Any;            // [-]   - 2 / 3 / 4 / 5 (Plasticity)
    SketcherBevelSession                  Bevel          = {};                               // [-]   - B (blend) / E (extrude) session
    SketcherDrawingSession                Drawing        = {};                               // [-]   - Line / rectangle / circle / arc / slot / curve
    SketcherSplineSpans                   SplineSpans = {};
    SketcherPoleDrag                      PoleDrag       = {};                               // [-]   - Mode 1 pole drag
    SketcherSketchPointSession            SketchPoints   = {};                               // [-]   - Mode 1 sketch points
    SketcherPointPicker                   Picker         = {};                               // [-]   - Object snaps and direction locks
    SketcherBoxSelection                  Box            = {};                               // [-]   - Left-drag box selection of curves
    SketcherBooleanSession                Boolean        = {};                               // [-]   - Q: Boolean card on picked bodies
    SketcherTrimSession                   Trim           = {};                               // [-]   - T: curve trim session
    SketcherFillSession                   Fill           = {};                               // [-]   - Fill tool: closed regions
    std::map<std::string, SketcherSketchTint> Tints      = {};                               // [-]   - Closed-sketch tints by step label
    SketcherConstraintSession             Constrain      = {};                               // [-]   - K: sketch constraint session
    SketcherTransformSession              Transform      = {};                               // [-]   - G / R / S / Shift+D session
    SketcherGizmoSession                  Gizmo          = {};                               // [-]   - Viewport move / rotate / scale gizmo
    SketcherSweepSession                  Sweep          = {};                               // [-]   - O / P session
    SketcherKnifeSession                  Knife          = {};                               // [-]   - Shift+K knife (KitOps-style cutters)
    SketcherClipboard                     Clipboard      = {};                               // [-]   - Ctrl+C / Ctrl+V
    SketcherMeasureSession                Measure        = {};                               // [-]   - I: Measure card + overlay
    SketcherIsolateMode                  Isolate        = {};                               // [-]   - Ctrl+I: Isolate toggle snapshot
    SketcherDimensionSession              DimensionTool  = {};                               // [-]   - D / Dim: Interactive CAD dimension lines
    SketcherSectionSession                Section        = {};                               // [-]   - Alt+S: section plane, caps, exact figures
    SketcherDrawingSheetSession           DrawingSheet   = {};                               // [-]   - Alt+D: paper, views, dimensions, export
    SketcherJointSession                  Joint          = {};                               // [-]   - Alt+J: joint card, origin glyphs, preview
    SketcherExchangeSession               Exchange       = {};                               // [-]   - Import… / Export…: STEP and STL files
    SketcherParameterDrag                 ParameterDrag  = {};                               // [-]   - Step card: dragged joint value
    bool                                  CombsEnabled   = true;                             // [-]   - Curvature combs on selected curves
    int                                   LoftContinuity = 0;                                // [-]   - Next loft: 0 G0 · 1 G1 · 2 G2 to the panel it meets
    SketcherInspectorTab                  InspectorTab   = SketcherInspectorTab::Geometry; // [-]   - Inspector tab shown
    bool                                  ZebraEnabled   = false;                            // [-]   - Zebra stripes on the faces (fine tessellation, V8)
    bool                                  DeviationEnabled = false;                          // [-]   - Needles from imported points to the panels (V8)
    SketcherDeviationPlot                  Deviation      = {};                               // [-]   - Cached per document revision
    bool                                  ImagePlanesEnabled = true;                         // [-]   - Blueprint image planes drawn (toolbar Images)
    std::string                           FocusedImage       = {};                           // [-]   - Image plane picked in the Outliner (its Inspector rows open)
    int                                   RingPicks          = 0;                            // [-]   - Alt / Alt+Shift edge picks (script evidence)
    SketcherPictureShelf                  Pictures       = {};                               // [-]   - Decoded image-plane pictures (frame-owned)
    SketcherMatcapShelf                   Matcaps        = {};                               // [-]   - Matcap library + thumbnails + M picker
    SketcherRepeat                        LastCommand    = {};                               // [-]   - Shift+R
    SketcherExtrudeMode                   LastExtrudeMode = SketcherExtrudeMode::AlongNormals; // [-] - Last extrude type chosen in Ctrl+E menu
    bool                                  ExtrudeMenuRequest = false;                        // [-]   - Ctrl+E extrude-type context menu pending
    ImVec2                                ExtrudeMenuPos = {};                               // [px]  - Cursor/viewport anchor for Ctrl+E context menu
    bool                                  SnapMenuRequest = false;                           // [-]   - Viewport Snap filter popover pending
    std::string                           PopupRequest   = {};                               // [-]   - Header popup a shortcut asked for
    SketcherConstructRequest              ConstructForm  = {};                               // [-]   - Construct dialog values
    bool                                  ConstructOpen  = false;                            // [-]   - Construct dialog requested
    bool                                  ConstructShown = false;                            // [-]   - Construct dialog drawn this frame
    int                                   ConstructSection = 0;                              // [-]   - Context menu: 0 Solids · 1 Sketch · 2 Reference · 3 Tools
    bool                                  ConstructMenuRequest = false;                      // [-]   - Construct catalogue popover requested
    bool                                  ConstructMenuAtPointer = false;                    // [-]   - Empty-space RMB anchors the catalogue at the pointer
    std::array<float, 2>                  ConstructMenuPointer = {};                         // [px]  - Requested viewport context-menu corner
    bool                                  TraceShown     = false;                            // [-]   - F12 operation trace panel (development builds)
    std::string                           TraceFilter    = {};                               // [-]   - Its substring filter
    SketcherOutlinerMode                 Outliner       = {};                               // [-]   - Scene search, filter chips, compact / folded
    bool                                  ViewportMaximizedEnabled = false;                  // [-]   - Scene panel spans the row (maximize button)
    size_t                                HistoryCount   = 0;                                // [-]   - Steps when the strip last scrolled to the newest
    GeometrySpace::Parametric::FaceOperationCategory           FaceVerb       = GeometrySpace::Parametric::FaceOperationCategory::Push; // [-]  - Face-edit verb
    double                                FaceAmount     = 0.25;                             // [-]   - Face-edit amount (by verb)
    std::string                           FocusedStep    = {};                               // [-]   - Outliner row (step label)
    std::string                           DocumentPath   = {};                               // [-]   - Save / Open path
    std::array<char, 260>                 PathText       = {};                               // [-]   - Path field
    std::array<char, 260>                 PicturePath    = {};                               // [-]   - Construct › Image plane: picture file
    int                                   PicturePlane   = 1;                                // [-]   - 0 top (XY) · 1 front (XZ) · 2 right (YZ)
    float                                 PictureWidth   = 4.0f;                             // [len] - World width of the new plane
    bool                                  DirtyCondition = false;                            // [-]   - Unsaved edits
    double                                AutosaveTimer  = -1.0;                             // [s]   - Countdown (< 0: idle)
    bool                                  WelcomeOpen    = false;                            // [-]   - Recovery prompt pending
    uint32_t                              HoverIdentity  = 0;                                // [-]   - GPU identity under the pointer
    std::array<float, 4>                  ViewportRect   = { 0, 0, 1, 1 };                   // [px]  - Active pane x, y, width, height in the frame
    SketcherSplitViewMode                SplitView      = {};                               // [-]   - 1 / 2 / 3 / 4 docked split-viewport state
    bool                                  ViewportHovered = false;                           // [-]   - Pointer over the viewport
    std::string                           Status         = "Ready";                          // [-]   - Status line
    std::vector<std::string>              Journal        = {};                               // [-]   - Command log (newest last)
    SketcherWidgetBoard                   Widgets        = {};                               // [px]  - Named widget rectangles of the last frame (scripts)
    uint64_t                              FrameNumber    = 0;                                // [-]   - Frames drawn
    double                                FrameMs        = 0.0;                              // [ms]  - Last frame's CPU + GPU time
    SketcherRenderProfile                  Profile        = {};                               // [-]   - Phase timing of the current frame
    std::string                           DeviceLabel    = {};                               // [-]   - Vulkan device description
    // 💡 Flow along curve (Inspector › Curves › Flow): the rail is the first picked sketch (FlowSwapCondition: the last).
    GeometrySpace::Parametric::SketchRailDeformSettings Flow              = {};                           // [-]   - Axis, stretch, reverse
    bool                                  FlowSwapCondition = false;                         // [-]   - Rail is the last pick instead of the first
    // 💡 Edge path (Ctrl+click an unpicked edge with edges picked): the mode and side, and the last pick so a change in
    //    Inspector › Edge path redoes it while the selection is still that pick's.
    SketcherEdgePathMode                  PathMode          = SketcherEdgePathMode::AlongLoop; // [-] - Along loop · Whole loop · Shortest
    bool                                  PathOtherSideCondition = false;                    // [-]   - Along a closed loop: the far side
    std::string                           PathFrom          = {};                            // [-]   - Last pick: the edge it started from
    std::string                           PathTo            = {};                            // [-]   - Last pick: the Ctrl-clicked edge
    std::string                           PathBody          = {};                            // [-]   - Last pick: the body
    std::vector<std::string>              PathBase          = {};                            // [-]   - Edges picked before the path
    std::vector<std::string>              PathSelection     = {};                            // [-]   - Edges picked after it (unchanged: redo allowed)
    SketcherEdgePath                      PathLast          = {};                            // [-]   - Last outcome (carrier, length, refusal)
    int                                   PathPicks         = 0;                             // [-]   - Path picks made (script evidence)
    // 💡 Inspector › Curves › Wrap: 0 wrap round the rail's surface (lengths kept), 1 project onto it, 2 flat flow (2D);
    //    the projection axis −1 auto (a parallel view: the view direction; perspective: the popup asks), 0 / 1 / 2 x / y / z.
    int                                   WrapMode          = 0;                             // [-]   - 0 wrap · 1 project · 2 flat flow
    int                                   WrapAxis          = -1;                            // [-]   - −1 auto · 0 x · 1 y · 2 z
    bool                                  WrapAxisPending   = false;                         // [-]   - The axis popup is open
    int                                   WrapCurves        = 0;                             // [-]   - 3D curves made by the last wrap (script evidence)
    std::string                           WrapNote          = {};                            // [-]   - The axes the last wrap used
    std::string                           WrapAutoNote      = {};                            // [-]   - The axes Auto reads from the picks now (empty: the view / popup decides)
    std::array<float, 2>                  FileMenuAnchor    = { 0, 0 };                      // [px]  - Top-left of the file menu (under the Outliner ☰)
    // 💡 Parametric Variables & Edge Weight Groups Inspector state:
    std::array<char, 64>                  VariableNameText  = { 'B', 'e', 'v', 'e', 'l', 'R', 'a', 'd', 0 }; // [-] - New variable identifier
    double                                VariableValueInit = 0.05;                          // [-]   - Initial literal value for new variable
    std::array<char, 64>                  EdgeGroupNameText = { 'C', 'r', 'e', 'a', 's', 'e', 0 };          // [-] - Active edge group name
    double                                EdgeGroupWeight   = 1.0;                           // [-]   - Weight assigned to selected edges
    std::string                           EdgeGroupVariable = {};                            // [-]   - Optional variable bound to edge weight
    int                                   AddModifierKind   = 0;                             // [-]   - Selected ModifierCategory in '+ Add Modifier'
    bool                                  DimensionsEnabled = true;                          // [-]   - Persistent CAD dimension lines overlay
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Loads the font, applies the Sketcher dark palette and spacing to the current ImGui context.
void ConfigureSketcherInterface(const SketcherOptions& Options);

// Empty document + defaults; offers recovery when Options.RecoveryPath holds an autosave.
void InitialiseSketcherWorkspace(SketcherWorkspaceStatus& State, const SketcherOptions& Options);

// Host hook, once per frame after present: writes the slow-frame trace line for the frame just shown (FrameMs set).
void ConcludeSketcherFrame(SketcherWorkspaceStatus& State);

// Rigid previews (a move / rotate / scale of whole bodies shows the scene carried by the map, no regeneration): the map a
// gizmo drag shows, and the volume the commit will give (each keyed body's volume × det M).
[[nodiscard]] GeometrySpace::Parametric::AffineMapping GizmoPreviewMap(const SketcherGizmoSession& Gizmo);
[[nodiscard]] double                        RigidPreviewVolume(const SketcherDocument& Document, const std::vector<std::string>& Keys, const GeometrySpace::Parametric::AffineMapping& Map);

// 💡 Builds the whole frame (between ImGui::NewFrame and ImGui::Render). ViewportTexture is the colour target of the
//    previous viewport render. Serves every shortcut and viewport gesture; DeltaSeconds drives the autosave timer.
void DrawSketcherInterface(SketcherWorkspaceStatus& State, ImTextureID ViewportTexture, double DeltaSeconds);

// Hosted dock-pane entry points for Frontier's HeadlessWorkspaceDockContainer (used by SketcherWorkspaceHost).
void BeginSketcherHostedFrame(SketcherWorkspaceStatus& State);
void DrawSketcherOutlinerPanel(SketcherWorkspaceStatus& State);
void DrawSketcherViewportPanel(SketcherWorkspaceStatus& State, ImTextureRef ViewportTexture);
void DrawSketcherInspectorPanel(SketcherWorkspaceStatus& State);
void DrawSketcherConstructPanel(SketcherWorkspaceStatus& State);
void DrawSketcherDrawingSheetPanel(SketcherWorkspaceStatus& State);
void EndSketcherHostedFrame(SketcherWorkspaceStatus& State, double DeltaSeconds, bool DrawFloatingOverlays = true, bool HostedOverlayMode = false);
bool IntakeSketcherDroppedFile(SketcherWorkspaceStatus& State, const std::string& FilePath, std::string* OutDiagnostic = nullptr);

// Reassembles the matcap library when the document's custom pictures changed (or on first use).
void RefreshSketcherMatcaps(SketcherWorkspaceStatus& State);

// The scene the viewport must show (the staged preview during a bevel session).
[[nodiscard]] const SketcherScene& DisplayedSketcherScene(const SketcherWorkspaceStatus& State);

// Per-face / per-edge state words for the displayed scene (selection, active operand, hover).
[[nodiscard]] SketcherSceneStates DisplayedSketcherStates(const SketcherWorkspaceStatus& State);

// Split-viewport pane count (1, 2, 3, or 4), layout switcher, and GPU raster pane descriptor list.
[[nodiscard]] int  SketcherViewportPaneCount(SketcherViewportLayout Layout) noexcept;
void               ConfigureSketcherStartupViews(SketcherWorkspaceStatus& State);
void               SetSketcherViewportLayout(SketcherWorkspaceStatus& State, SketcherViewportLayout Layout);
[[nodiscard]] std::vector<SketcherRasterPane> ComposeSketcherRasterPanes(const SketcherWorkspaceStatus& State, uint32_t TargetWidth, uint32_t TargetHeight);

// Rebuilds the scene after a document change and marks the document dirty (autosave pending).
void RefreshSketcherScene(SketcherWorkspaceStatus& State, bool DirtyCondition);

// Where New document keeps the previous recovery copy ("X.solidarc" → "X.previous.solidarc").
[[nodiscard]] std::string SketcherPreviousRecoveryPath(const SketcherOptions& Options);

// "250mm", "-25cm", "0.25m", "1in", "1ft", "2.5e-1" → model units (metres); false when unparsable.
bool ParseSketcherLength(const std::string& Text, double& Value);

// Sketch step the constraint card / script fields describe: the K session's, else the focused sketch step, else "".
[[nodiscard]] std::string SketcherConstraintSketch(const SketcherWorkspaceStatus& State);

// JSON summary of the workspace (document, selection, camera, bevel session) for capture exhibits.
[[nodiscard]] std::string DescribeSketcherWorkspace(const SketcherWorkspaceStatus& State);

// Evaluates the selection's gizmo target, fits its handles to State.Camera's view (2D in 90° views, 3D otherwise),
// and builds State.Gizmo.Geometry on CPU without requiring a GPU / ImGui frame (used by document/gizmo validation).
void EvaluateSketcherGizmo(SketcherWorkspaceStatus& State, const OrbitCameraBasis& Frame);

// Executes a single gizmo command on State.Document for the currently resolved State.Gizmo target and refreshes Scene.
[[nodiscard]] SketcherCommandOutcome ApplySketcherGizmo(SketcherWorkspaceStatus& State, GizmoHandle Handle,
                                                        GeometrySpace::Parametric::Vector3 Shift, double Angle = 0.0, double Factor = 1.0);

} // namespace Frontier::Workspaces::Sketcher
