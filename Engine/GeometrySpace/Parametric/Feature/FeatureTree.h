/*====================================================================================================================================
                                                         FEATURETREE.H
====================================================================================================================================*/
// 🧩 Parametric feature tree: parameters, sketches, sweeps, Booleans, blends, face operations; regeneration by replay

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Assembly/JointSolver.h"
#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"
#include "Engine/GeometrySpace/Parametric/Feature/TopologicalNaming.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/CurveFitting.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class FeatureStepKind : uint8_t
{
    Sketch        = 0,                                                                       // Constrained 2D sketch on a plane
    Extrude       = 1,                                                                       // Sketch profile swept linearly
    Revolve       = 2,                                                                       // Sketch profile revolved (360° default)
    Blend         = 3,                                                                       // Fillet / chamfer on referenced edges
    FaceOperation = 4,                                                                       // Push, offset, draft, … on referenced faces
    SpaceCurve    = 5,                                                                       // 3D curve fitted to imported points (vehicle plan V3)
    Loft          = 6,                                                                       // Panel skinned through SpaceCurve sections (V4, G1/G2 V6)
    Network       = 7,                                                                       // Panel through a SpaceCurve network, Gordon / Coons (V5)
    Mirror        = 8,                                                                       // Mirror image of every panel so far through a plane (V7)
    Close         = 9,                                                                       // Plane caps on the planar rim loops → solid (V7)
    Combine       = 10,                                                                      // Boolean between two bodies; the tool body is consumed
    Cut           = 11,                                                                      // Split a body by a sketch profile / curve into two bodies (Plasticity C)
    Remove        = 12,                                                                      // Delete a body (Plasticity X on a body)
    Transform     = 13,                                                                      // Move / rotate / scale a body in place (Plasticity G / R / S)
    Duplicate     = 14,                                                                      // Copy of a body as a new body (Plasticity Shift+D, Ctrl+C / Ctrl+V)
    Pipe          = 15,                                                                      // Circle swept along a SpaceCurve or a one-curve sketch (Plasticity P)
    Evolution     = 16,                                                                      // Sketch profile swept along a path (Plasticity Shift+P / Alt+E)
    Joint         = 17,                                                                      // Place a body by a joint to another body (Fusion joints, priority 11)
    Import        = 18,                                                                      // One solid read from embedded STEP text (priority 4 X4)
    Flip          = 19,                                                                      // A sheet turned over: every face normal reversed (loft inside out)
    Imprint       = 20,                                                                      // Sketch outline split into the faces it lies on (2D cutter)
};

enum class FeatureCombination : uint8_t
{
    New          = 0,                                                                        // First body (later: union with the target)
    Union        = 1,                                                                        // Target ∪ tool
    Difference   = 2,                                                                        // Target − tool
    Intersection = 3,                                                                        // Target ∩ tool
    Separate     = 4,                                                                        // A new body of its own (Plasticity "new body")
};

enum class FeatureStepStatus : uint8_t
{
    Built   = 0,                                                                             // Step applied
    Refused = 1,                                                                             // Step marked; previous body kept
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A number in the tree: Literal, or Factor·Parameter + SecondFactor·SecondParameter + Offset when Parameter is named.
struct FeatureValue
{
    double      Literal   = 0.0;                                                             // [len|rad|-] - Value without a parameter
    std::string Parameter = {};                                                              // [-]         - Named parameter (empty: literal)
    double      Factor    = 1.0;                                                             // [-]         - Scale on the parameter
    double      Offset    = 0.0;                                                             // [len|rad|-] - Added after scaling
    std::string SecondParameter = {};                                                        // [-]         - Optional second term
    double      SecondFactor    = 0.0;                                                       // [-]         - Scale on the second term
};

struct FeatureParameter
{
    std::string Name        = {};                                                            // [-]         - Key ("Width")
    double      Value       = 0.0;                                                           // [len|rad|-] - Current value
    double      Low         = 0.0;                                                           // [len|rad|-] - Slider range
    double      High        = 1.0;                                                           // [len|rad|-] - Slider range
    std::string Description = {};                                                            // [-]         - One line
};

// 📝 One step; only the fields of its kind are read. Label is the stable naming tag ("Extrude#2").
// 📝 A dragged pole of a SpaceCurve: replaces pole Index of the fitted curve after every re-fit (mode 1 edit).
struct FeaturePoleEdit
{
    int     Index    = 0;                                                                    // [-]   - Pole index in the fitted curve
    Vector3 Position = {};                                                                   // [len] - New pole position
};

// 📝 A joint step's values (Assembly/JointSolver.h); each kind reads only its own, every value may bind a parameter.
struct FeatureJoint
{
    JointKind                 Kind             = JointKind::Rigid;                           // [-]   - Fusion joint type
    FeatureValue              Angle            = {};                                         // [rad] - Turn about 𝑧 (ball: yaw)
    FeatureValue              Slide            = {};                                         // [len] - Slider / cylindrical: along 𝑧 · pin-slot / planar: along 𝑥
    FeatureValue              SecondSlide      = {};                                         // [len] - Planar: along 𝑦
    FeatureValue              Pitch            = {};                                         // [rad] - Ball: about 𝑦
    FeatureValue              Roll             = {};                                         // [rad] - Ball: about 𝑥
    FeatureValue              Offset           = {};                                         // [len] - Along 𝑧, every kind
    bool                      FlippedCondition = false;                                      // [-]   - Origins aligned instead of facing each other
    std::optional<JointRange> AngleRange       = {};                                         // [rad] - Limits on Angle (refused outside)
    std::optional<JointRange> SlideRange       = {};                                         // [len] - Limits on Slide (refused outside)
};

struct FeatureStep
{
    FeatureStepKind                Kind           = FeatureStepKind::Sketch;                 // [-]   - Step type
    std::string                    Label          = {};                                      // [-]   - Unique tag, prefix of the names it creates
    SketchModel                    Sketch         = {};                                      // [-]   - Sketch: unsolved model (constraints may bind parameters)
    OrthonormalFrame               Plane          = {};                                      // [-]   - Sketch: plane frame · mirror: plane · cut by a curve: ZAxis = cut direction
    FeatureValue                   Elevation      = {};                                      // [len] - Sketch: plane shift along its normal
    bool                           FillCondition  = true;                                    // [-]   - Sketch: closed loops are filled faces (extrude → solid);
                                                                                             //         unfilled (Plasticity's fresh curves) → extrude makes walls
    std::string                    Profile        = {};                                      // [-]   - Extrude / revolve / cut: sketch step label
    FeatureValue                   Extent         = {};                                      // [len] - Extrude distance (negative: below the plane) · inset depth
    FeatureValue                   Start          = {};                                      // [len] - Extrude: where the sweep starts along the normal (negative: sunk
                                                                                             //         into the curved face the sketch is tangent to, so a Union closes)
    FeatureCombination             Combination    = FeatureCombination::New;                 // [-]   - Extrude / revolve: Boolean with the current body
    int                            AxisIdentifier = -1;                                      // [-]   - Revolve: identifier of the axis line in the sketch (−1: AxisOrigin / AxisDirection)
    Vector2                        AxisOrigin     = {};                                      // [len] - Revolve without an axis line: a point of the axis (sketch coordinates)
    Vector2                        AxisDirection  = {};                                      // [-]   - Revolve without an axis line: axis direction (sketch coordinates)
    std::string                    Path           = {};                                      // [-]   - Evolution: path — a one-curve sketch or a SpaceCurve label
    FeatureValue                   Angle          = { 2.0 * std::numbers::pi };              // [rad] - Revolve: turn angle in (0, 2π]
    BlendProfile                   Shape          = BlendProfile::Fillet;                    // [-]   - Blend cross-section
    FeatureValue                   Size           = {};                                      // [len] - Blend radius / setback (at the anchors)
    std::optional<FeatureValue>    EndSize        = {};                                      // [len] - Blend: size at the far ends (empty = constant)
    std::vector<TopologyReference> SizeAnchors    = {};                                      // [-]   - Blend (variable): vertices that take Size, one per edge
    std::vector<FeatureValue>      AnchorSizes    = {};                                      // [len] - Blend (per vertex): size at SizeAnchors[i] (empty: Size / EndSize)
    FaceOperationKind              Operation      = FaceOperationKind::Push;                 // [-]   - Face-operation verb
    FeatureValue                   Amount         = {};                                      // [len|rad|-] - Distance / angle / factor (by verb)
    std::vector<TopologyReference> References     = {};                                      // [-]   - Blend: edges · face operation: faces
    std::vector<Vector3>           Points         = {};                                      // [len] - SpaceCurve: imported points in order · FaceOperation
                                                                                             //         Tweak / Rotate: [0] = direction / axis (gizmo)
    bool                           ClosedCondition = false;                                  // [-]   - SpaceCurve: the loop closes (last joins first) · Loft: capped into a solid of its own
    bool                           ReverseNormals = false;                                   // Loft/network: persistent, explicit sheet orientation
    bool                           PlanarCondition = false;                                  // [-]   - SpaceCurve: fit in the points' best plane (cap sections)
    FeatureValue                   Tolerance      = { 2.0e-4 };                              // [len] - SpaceCurve: fit tolerance (0.2 mm)
    std::vector<double>            CurveKnots     = {};                                      // [-]   - SpaceCurve: exact clamped knots — Points are then the
                                                                                             //         poles (degree = knots − poles − 1), no fit (drawn 3D curves)
    std::string                    CurveOffsetGuide = {};                                  // Live intrinsic surface-distance relationship (empty: independent)
    std::string                    CurveOffsetBody = {};                                   // Explicit reference, never another coincident sheet
    Vector3                        CurveOffsetOrigin = {};                                 // Guide midpoint at linking; carries target extent through translations
    double                         CurveOffsetDistance = 0.05;                             // metres along the reference surface
    double                         CurveOffsetTolerance = 0.002;                           // contour convergence tolerance in metres
    std::vector<FeaturePoleEdit>   PoleEdits      = {};                                      // [-]   - SpaceCurve: dragged poles, applied after the fit
    std::vector<std::string>       Sections       = {};                                      // [-]   - Loft: SpaceCurve labels in order · Network: its curves · Cut: the cutting curve
    int                            Continuity     = 0;                                       // [-]   - Loft: 0 G0 · 1 G1 · 2 G2 to panels sharing an end section
    std::string                    Target         = {};                                      // [-]   - Extrude / revolve Boolean, blend, face operation, combine, cut, remove: body key (empty = active body)
    std::string                    Tool           = {};                                      // [-]   - Combine: body key consumed by the Boolean
    AffineMap                      Placement      = {};                                      // [-]   - Transform / duplicate: x ↦ M·x + Shift (identity: none)
    std::string                    Source         = {};                                      // [-]   - Duplicate: step after which Target is copied (empty: as it is now)
    FeatureJoint                   Joint          = {};                                      // [-]   - Joint: kind and values (Target moves onto Tool; References = moving, fixed origin)
    std::string                    Exchange       = {};                                      // [-]   - Import: STEP (ISO 10303-21) text of one body, embedded so the
                                                                                             //         document stands alone
    double                         ExchangeUnit   = 1.0;                                     // [mm]  - Import: millimetres per model unit (1000 in a metre document)
    FeatureValue                   Offset         = {};                                      // [len] - Combine / Transform: parametric shift along OffsetAxis
    Vector3                        OffsetAxis     = { 0.0, 0.0, 1.0 };                       // [-]   - Combine / Transform: unit direction for Offset
};

// 📝 Weighted edge selection ("vertex paint for edges"): each edge carries a scalar or parameter-bound weight
//    that Bevel modifiers multiply against their base Size (interpolated per-vertex for smooth variable blends).
struct EdgeWeightEntry
{
    TopologyReference Edge   = {};                                                           // [-] - Persistent edge reference + signature
    FeatureValue      Weight = { 1.0 };                                                      // [-] - Weight factor (can bind to a FeatureParameter)
};

struct FeatureEdgeGroup
{
    std::string                  Name    = {};                                               // [-] - Unique group name
    std::string                  Body    = {};                                               // [-] - Owning body key (empty = active body)
    std::vector<EdgeWeightEntry> Entries = {};                                               // [-] - Painted / weighted edges
};

enum class ModifierKind
{
    Boolean,                                                                                 // [-] - Live non-destructive Boolean with optional Offset
    Bevel,                                                                                   // [-] - Edge-group / picked-edge / crease-angle fillet or chamfer
    LinearArray,                                                                             // [-] - 1D / 2D linear grid array
    CircularArray,                                                                           // [-] - Radial / helical circular array
    CurveArray,                                                                              // [-] - Array along a 2D Sketch chain or 3D SpaceCurve
    SurfaceArray,                                                                            // [-] - UV pattern across a target shape face
    CurveBend                                                                                // [-] - Certified 3D spatial B-rep bend along a curve
};

enum class BevelLimitMode
{
    EdgeGroup,                                                                               // [-] - Reference a named FeatureEdgeGroup with per-edge weights
    Picked,                                                                                  // [-] - Explicit TopologyReference list on the modifier
    Angle                                                                                    // [-] - All manifold creases with dihedral angle ≥ Angle
};

struct FeatureModifier
{
    std::string                    Label              = {};                                  // [-]     - Unique modifier tag ("Bevel#1", "LiveCut#1")
    ModifierKind                   Kind               = ModifierKind::Bevel;                 // [-]     - Modifier operation
    bool                           EnabledCondition   = true;                                // [-]     - False: muted / bypassed in stack
    std::string                    Target             = {};                                  // [-]     - Target body key (empty = active body)
    std::string                    Tool               = {};                                  // [-]     - Boolean / SurfaceArray: cutter or host shape body key
    FeatureCombination             Combination        = FeatureCombination::Difference;      // [-]     - Boolean operation
    FeatureValue                   Offset             = {};                                  // [len]   - Boolean tool offset / CircularArray rise / SurfaceArray normal offset
    Vector3                        OffsetAxis         = { 0.0, 0.0, 1.0 };                   // [-]     - Boolean offset direction
    bool                           KeepToolCondition  = true;                                // [-]     - Boolean: keep cutter body in scene for Live Boolean gizmo moves
    BlendProfile                   Shape              = BlendProfile::Fillet;                // [-]     - Bevel cross-section (Fillet / Chamfer)
    BevelLimitMode                 LimitMode          = BevelLimitMode::EdgeGroup;           // [-]     - Bevel edge selection mode
    std::string                    EdgeGroup          = {};                                  // [-]     - Bevel: referenced FeatureEdgeGroup name
    std::vector<TopologyReference> References         = {};                                  // [-]     - Bevel (Picked): edges · SurfaceArray: host face (empty = largest face)
    FeatureValue                   Size               = { 1.0 };                             // [len]   - Bevel base radius / setback
    FeatureValue                   Angle              = { std::numbers::pi / 6.0 };          // [rad]   - Bevel crease threshold · CircularArray sweep angle (default 2π when created)
    FeatureValue                   Count              = { 3.0 };                             // [-]     - Primary instance count (bindable to parameter)
    FeatureValue                   SecondaryCount     = { 1.0 };                             // [-]     - Secondary grid count (LinearArray Y / SurfaceArray V)
    Vector3                        Direction          = { 1.0, 0.0, 0.0 };                   // [-]     - LinearArray step 1 direction
    FeatureValue                   Distance           = { 20.0 };                            // [len]   - LinearArray step 1 distance · CurveArray fixed spacing (≤0 = auto)
    Vector3                        SecondaryDirection = { 0.0, 1.0, 0.0 };                   // [-]     - LinearArray step 2 direction
    FeatureValue                   SecondaryDistance  = { 20.0 };                            // [len]   - LinearArray step 2 distance
    Vector3                        AxisOrigin         = {};                                  // [len]   - CircularArray axis point
    Vector3                        AxisDirection      = { 0.0, 0.0, 1.0 };                   // [-]     - CircularArray axis direction
    std::string                    Curve              = {};                                  // [-]     - CurveArray / CurveBend: Sketch or SpaceCurve step label
    int                            DeformAxis         = -1;                                  // [-]     - CurveBend: −1 longest axis, 0 X, 1 Y, 2 Z
    bool                           AlignCondition     = true;                                // [-]     - CurveArray RMF alignment · SurfaceArray normal alignment
    bool                           StretchCondition   = true;                                // [-]     - CurveBend: stretch body extent to curve arc length
    bool                           MergeCondition     = false;                               // [-]     - Arrays: Boolean-union touching copies
    FeatureValue                   Margin             = { 0.1 };                             // [-]     - SurfaceArray UV border inset [0..0.45]
};

struct FeatureModifierReport
{
    std::string       Label        = {};                                                     // [-]  - Modifier tag
    ModifierKind      Kind         = ModifierKind::Bevel;                                    // [-]  - Modifier type
    FeatureStepStatus Status       = FeatureStepStatus::Built;                               // [-]  - Built / refused
    std::string       Refusal      = {};                                                     // [-]  - Reason when refused
    std::string       Target       = {};                                                     // [-]  - Modified body key
    int               Instances    = 1;                                                      // [-]  - Copies generated (arrays)
    double            Milliseconds = 0.0;                                                    // [ms] - Evaluation time
};

struct FeatureTree
{
    std::string                    Name        = {};                                         // [-] - Catalogue key
    std::string                    Description = {};                                         // [-] - One line
    std::vector<FeatureParameter>  Parameters  = {};                                         // [-] - Named driving values
    std::vector<FeatureStep>       Steps       = {};                                         // [-] - Replay order
    std::vector<FeatureEdgeGroup>  EdgeGroups  = {};                                         // [-] - Weighted edge groups ("vertex paint for edges")
    std::vector<FeatureModifier>   Modifiers   = {};                                         // [-] - Non-destructive Blender-style modifier stack
};

struct FeatureStepReport
{
    std::string                    Label        = {};                                        // [-]  - Step tag
    FeatureStepKind                Kind         = FeatureStepKind::Sketch;                   // [-]  - Step type
    FeatureStepStatus              Status       = FeatureStepStatus::Built;                  // [-]  - Built / refused
    std::string                    Refusal      = {};                                        // [-]  - Reason when refused
    std::vector<ResolvedReference> Resolutions  = {};                                        // [-]  - One per reference
    SketchSolveOutcome             Solve        = {};                                        // [-]  - Sketch steps
    BodyToken                      Body         = {};                                        // [-]  - Body after the step (kept one when refused)
    BodyNaming                     Naming       = {};                                        // [-]  - Names of that body
    std::string                    Target       = {};                                        // [-]  - Key of that body (empty before any body)
    double                         Milliseconds = 0.0;                                       // [ms] - Step time
    std::vector<std::string>       Notes        = {};                                        // [-]  - Built with remarks (sketch: dangling links)
};

// 📝 One body of the regenerated part. Key = label of the step that created it ("Extrude#2", "Loft#12"): stable across
//    regenerations, so appearance and selections can name bodies the way they name faces.
struct RegeneratedBody
{
    std::string Key    = {};                                                                 // [-] - Creating step label
    BodyToken   Body   = {};                                                                 // [-] - Body token
    BodyNaming  Naming = {};                                                                 // [-] - Its persistent names
};

// 📝 One joint of the regenerated assembly, with both joint origins where they ended up (world).
struct RegeneratedJoint
{
    std::string      Label        = {};                                                      // [-] - Joint step label
    std::string      Moving       = {};                                                      // [-] - Key of the placed body
    std::string      Fixed        = {};                                                      // [-] - Key of the body it is jointed to
    JointKind        Kind         = JointKind::Rigid;                                        // [-] - Joint type
    OrthonormalFrame MovingOrigin = {};                                                      // [-] - Moving body's joint origin
    OrthonormalFrame FixedOrigin  = {};                                                      // [-] - Fixed body's joint origin
};

struct RegenerationOutcome
{
    BodyToken                          Body         = {};                                    // [-]  - Active body (the one the last body step built or changed)
    BodyNaming                         Naming       = {};                                    // [-]  - Its persistent names
    std::vector<RegeneratedBody>       Bodies       = {};                                    // [-]  - Every body, in creation order
    std::string                        Active       = {};                                    // [-]  - Key of the active body
    std::vector<FeatureStepReport>     Steps        = {};                                    // [-]  - One per step
    std::vector<FeatureModifierReport> Modifiers    = {};                                    // [-]  - One per modifier in stack order
    std::map<std::string, SketchModel> Sketches     = {};                                    // [-]  - Solved sketch per label
    std::map<std::string, CurveFitOutcome> Curves   = {};                                    // [-]  - Fitted (and edited) curve per SpaceCurve label
    std::map<std::string, NurbsSurface> Panels      = {};                                    // [-]  - Carrier per panel face name (zebra / deviation)
    std::vector<RegeneratedJoint>      Joints       = {};                                    // [-]  - Joints whose bodies still exist, in tree order
    int                                Refused      = 0;                                     // [-]  - Steps refused
    int                                Resumed      = 0;                                     // [-]  - First step replayed (earlier ones restored from a checkpoint)
    std::vector<std::pair<BodyToken, double>>      Volumes = {};                             // [len³] - Volumes a step already integrated (a moved body's image), by body token
    std::vector<std::pair<BodyToken, AuditReport>> Audits  = {};                             // [-]    - Full audits already verified by a step (Blend / FaceOperation), by body token
    double                             Milliseconds = 0.0;                                   // [ms] - Whole replay
};

// 📝 Opaque replay state after one step: a copy of the partition plus everything the loop carries forward.
struct ReplayCheckpoint;

// 📝 Incremental replay memory, owned by the caller and handed to every regeneration of one tree. Step k is restored
//    from Checkpoints[k] when the digests of steps 0..k all match (and every Duplicate snapshot a restored step must
//    take is in the restored state); replay resumes at the first mismatch. Checkpoints are immutable and shared, so
//    copying a cache (a preview clone) costs no partition copies.
struct RegenerationCache
{
    std::vector<std::string>                             Digests            = {};            // [-] - DigestFeatureStep per step, after its replay
    std::vector<std::shared_ptr<const ReplayCheckpoint>> Checkpoints        = {};            // [-] - State after each step
    std::vector<bool>                                    Captured           = {};            // [-] - Signatures captured (a capturing run re-captures restored steps in place)
    std::string                                          ModifierDigest     = {};            // [-] - DigestModifiers after post-step modifier stack evaluation
    std::shared_ptr<const ReplayCheckpoint>              ModifierCheckpoint = {};            // [-] - Post-modifier state when !Tree.Modifiers.empty()
    bool                                                 EnabledCondition   = true;          // [-] - False: always replay from scratch (still records)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Replays every step from scratch (plan § 6, step 5): sketches are solved with their parameters bound, sweeps are
//    named by role, Booleans / blends / face operations are named from their histories, and references are resolved
//    exact → source → geometric. A refused step (no solution, unresolvable reference, modeller refusal) is marked and the
//    previous body is kept; later steps still run. CaptureCondition (authoring): store each resolved reference's
//    signature in the tree so later regenerations can rebind geometrically.
//    Bodies: the first sweep (or a Separate one) starts a body keyed by its step label; Booleans, blends and face
//    operations act on Step.Target (empty: the active body, the last one built or changed); panels form one body of their
//    own; Combine merges Step.Tool into Step.Target and consumes the tool. Transform replaces Step.Target by its image
//    under Step.Placement (names kept); Duplicate copies Step.Target — as it stood after step Step.Source, so a copy
//    survives the later removal of its original — into a body keyed by its own label, every face renamed
//    "Label.Copy(name)"; Pipe sweeps a circle of radius Step.Size along a SpaceCurve (Step.Sections) or the single curve
//    of a sketch (Step.Profile) — lines and arcs exactly, free-form paths as a NURBS tube — and combines like an extrude.
//    Evolution sweeps the profile of sketch Step.Profile along Step.Path (a one-curve sketch or a SpaceCurve), from the
//    path end nearer the profile centroid (a curve's stored direction is arbitrary), and combines like an extrude. Revolve turns about the sketch line Step.AxisIdentifier, else about AxisOrigin +
//    t·AxisDirection in sketch coordinates.
//    Joint (priority 11) places Step.Target by a Fusion joint to Step.Tool: References[0] names the joint origin on the
//    target, References[1] the one on the tool (Assembly/JointSolver.h); the target takes the rigid placement (names kept)
//    and every body jointed to it earlier follows, as do bodies jointed to a body that a rigid Transform moves. One joint
//    per placed body; a joint that would close a loop is refused. Removing or consuming a body drops its joints.
//    Import (priority 4 X4) reads Step.Exchange, the STEP text of exactly one body, at Step.ExchangeUnit millimetres per
//    model unit into a body keyed by its label; faces are named Label.Face(k) in file order, edges and vertices from them.
//    Cache (optional): restore the unchanged leading steps from their checkpoints and replay from the first changed one;
//    the result is bit-identical to a full replay (same tokens, names, volumes). The cache is refreshed for the next run.
[[nodiscard]] RegenerationOutcome RegenerateFeatureTree(ModelPartition& Partition, FeatureTree& Tree, bool CaptureCondition = false,
                                                        RegenerationCache* Cache = nullptr);

// A sketch step's solve frame: its plane raised along the normal by the elevation.
[[nodiscard]] OrthonormalFrame SketchStepFrame(const FeatureTree& Tree, const FeatureStep& Step);

// A solved earlier sketch a link reads from (null Model: no such sketch before this one).
struct LinkedSketchSource
{
    const SketchModel* Model = nullptr;                                                      // [-] - Solved positions
    OrthonormalFrame   Plane = {};                                                           // [-] - Its solve frame
};

// 📝 Linked points of Model (solved in Plane) re-projected orthogonally from their source sketches, linked circle radii
//    copied when the planes are parallel. A missing source or bad index keeps the last position (held); the returned
//    notes say so ("Point0: link to Sketch#9 is dangling (held where it was)").
[[nodiscard]] std::vector<std::string> ProjectSketchLinks(SketchModel& Model, const OrthonormalFrame& Plane,
                                                          const std::function<LinkedSketchSource(const std::string&)>& Source);

[[nodiscard]] double EvaluateFeatureValue(const FeatureTree& Tree, const FeatureValue& Value);

// Sets a parameter; false when no parameter has that name.
bool AssignFeatureParameter(FeatureTree& Tree, const std::string& Name, double Value);

// Shorthands for catalogue authors.
[[nodiscard]] FeatureValue LiteralValue(double Value);
[[nodiscard]] FeatureValue ParameterValue(const std::string& Name, double Factor = 1.0, double Offset = 0.0);
[[nodiscard]] FeatureValue SumValue(const std::string& First, double FirstFactor, const std::string& Second, double SecondFactor, double Offset = 0.0);

[[nodiscard]] const char* FeatureStepKindLabel(FeatureStepKind Kind) noexcept;
[[nodiscard]] const char* FeatureCombinationLabel(FeatureCombination Combination) noexcept;
[[nodiscard]] const char* ModifierKindLabel(ModifierKind Kind) noexcept;
[[nodiscard]] const char* BevelLimitModeLabel(BevelLimitMode Mode) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
