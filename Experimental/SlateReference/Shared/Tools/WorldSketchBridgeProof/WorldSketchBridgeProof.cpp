#include "SlateShape/World/WorldSketchCorner/Api/WorldSketchCorner.h"
#include "SlateWorkspace/Discipline/WorldSketchBridge/Api/WorldSketchBridge.h"
#include "SlateWorkspace/Discipline/SketchInteraction/Api/SketchInteraction.h"
#include "SlateWorkspace/Discipline/SketchViewportOverlay/Api/SketchViewportOverlay.h"

#include <cmath>
#include <cstdio>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Sentence)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Sentence);
    }
}

bool Near(double Left, double Right, double Tolerance = 1.0e-4)
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool SamePoint(const SpatialPoint& Left,
               const SpatialPoint& Right,
               double Tolerance = 1.0e-4)
{
    return Near(Left.Left, Right.Left, Tolerance)
        && Near(Left.Up, Right.Up, Tolerance)
        && Near(Left.Forward, Right.Forward, Tolerance);
}

struct Bench
{
    PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    DrawableScale Drawable = { 1.0 };
    ResolvedCamera Camera = ResolveFreeCamera({ 0.0, 50.0, -300.0 }, 0.0, 0.0, 60.0, true, 1.0);
    SketchStructure Sketch = {};
    WorldSketchStructure World = {};
    WorldSketchMapping Mapping = {};
    WorkspaceNameIndex Naming = {};
    WorkspaceRecordStructure Records = {};
    WorkspaceRevisionSequence Revisions = {};
    WorkspaceDirectoryProjection Directory = {};
    ParametricWorkspaceContext WorkspaceApplied = {};
    WorkspaceRecordName PendingSelection = {};
    SketchPick SemanticSelection = {};
    SketchSelectionSet SelectionSet = {};
    SketchPick HoveredSelection = {};
    WorldSketchTransformSession Transform = {};
    SelectionOptions Selection = {};
    GizmoOptions Gizmo = {};
    double LastGPressedMilliseconds = 0.0;
    OverlayGeometry Overlay = {};
    WorkspaceRecordName ProfileRecord = {};
    SketchCurveName AB = {};
    SketchCurveName BC = {};
    SketchCurveName CD = {};
    SketchCurveName DA = {};

    Bench()
    {
        Sketch.DeclarePlane({ { 0.0, 0.0, 40.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } });
        AB = Sketch.DeclareLine({ 0.0, 0.0, 40.0 }, { 100.0, 0.0, 40.0 });
        BC = Sketch.DeclareLine({ 100.0, 0.0, 40.0 }, { 100.0, 100.0, 40.0 });
        CD = Sketch.DeclareLine({ 100.0, 100.0, 40.0 }, { 0.0, 100.0, 40.0 });
        DA = Sketch.DeclareLine({ 0.0, 100.0, 40.0 }, { 0.0, 0.0, 40.0 });

        ProfileSpecification Profile;
        Profile.DeclarePlane({ Sketch.HeldPlane().Origin, Sketch.HeldPlane().Normal, Sketch.HeldPlane().AlongDirection });
        ProfileLoop Loop;
        Loop.Orientation = ProfileLoopOrientation::Outer;
        Loop.Traversal = {
            { { AB.IssuedIndex }, true },
            { { BC.IssuedIndex }, true },
            { { CD.IssuedIndex }, true },
            { { DA.IssuedIndex }, true }
        };
        Profile.DeclareLoop(Loop);
        const ProfileNameInFeature DeclaredProfile = Sketch.DeclareProfile(Profile);

        WorkspaceRecord Record = {};
        Record.Subject = WorkspaceRecordSubject::ClosedProfile;
        Record.Naming = "Rectangle";
        Record.Profile = DeclaredProfile;
        ProfileRecord = Records.Declare(Record);

        MirrorSketchIntoWorldSketch(Sketch, World, Mapping);
        ProjectWorkspaceDirectory(Records, Directory);
        for (bool& Held : WorkspaceApplied.RowSelected)
            Held = false;
        WorkspaceApplied.RowTaken = 0u;

        Selection.Element = SelectionElement::Edge;
        Selection.Tolerance = 8.0f;
        Gizmo.Shown = true;
    }

    SpatialPoint EdgePivot() const
    {
        SpatialPoint Pivot = {};
        ResolveCurvePivot(Sketch, BC, Pivot);
        return Pivot;
    }

    void Drive(const PointerCondition& Pointer,
               const TextInputCondition& Text,
               double Milliseconds,
               bool& PointerTaken)
    {
        Overlay.Reset();
        // 📝 The arm gained a modifier condition and a multi-selection set after this bench was
        //    written, and the bench was never re-aimed -- so this proof stopped compiling and stopped
        //    being run at all. Both are defaulted here: the flow under test is single-selection.
        const ModifierCondition Plain = {};
        DriveViewportSelectionAndTransformWorldBacked(
            Extent, Pointer, Text, Plain, ParametricToolSubject::Select, Selection, Gizmo, Camera,
            Directory, WorkspaceApplied, Naming,
            Sketch, World, Mapping, Records, Revisions,
            PendingSelection, SemanticSelection, SelectionSet, HoveredSelection,
            Transform, Overlay, PointerTaken,
            Milliseconds, LastGPressedMilliseconds);
    }
};

/// 🔴 AN OPERATION DECLARES INTO THE WORLD MODEL ALONE, AND PICKING READS THE COMPATIBILITY SKETCH.
///    `ApplyWorldSketchToSketch` walks the MAPPING, so a curve with no mapping entry -- every fillet arc,
///    every chamfer chord, every second half of a cut -- was copied nowhere. The artist could see the new
///    curve, because it is drawn from the world model, and could never select it, because selection asks
///    the sketch. That is the "it shows for a frame then cancels" defect.
void ProveOperationCurvesReachTheCompatibilitySketch()
{
    std::printf("\n10. A curve an operation declares becomes selectable, not merely visible\n");

    Bench Stage;

    const std::size_t SketchBefore  = Stage.Sketch.Curves().size();
    const std::size_t MappedBefore  = Stage.Mapping.Curves.size();
    Claim(SketchBefore == 4u && MappedBefore == 4u, "the bench starts with four paired curves");

    // 📐 A fillet, declared straight into the world model exactly as the operation driver does it.
    WorldCurveName Arc = {};
    Claim(ApplyWorldCorner(Stage.World, { 1u }, { 2u }, 20.0, false, Arc) == CornerVerdict::Produced,
          "a corner is filleted in the world model");
    Claim(Stage.World.CurveCount() == 5u, "which leaves the world model holding five curves");

    // ① BEFORE ADOPTION THE MIRROR HAS NOT HEARD OF IT. Stated so the fix is measured, not assumed.
    Claim(Stage.Sketch.Curves().size() == SketchBefore,
          "the compatibility sketch has not heard of the arc yet");

    // ② ADOPTION PAIRS IT.
    Claim(AdoptWorldSketchCurvesIntoSketch(Stage.World, Stage.Mapping, Stage.Sketch),
          "adopting reports that it paired something");
    Claim(Stage.Mapping.Curves.size() == 5u, "the mapping now pairs all five curves");
    Claim(Stage.Sketch.Curves().size() == 5u, "and the compatibility sketch holds five as well");

    bool Paired = false;
    for (const WorldSketchCurveReference& Reference : Stage.Mapping.Curves)
        if (Reference.World.IssuedIndex == Arc.IssuedIndex)
            Paired = Reference.Sketch.Assigned();
    Claim(Paired, "the arc specifically is paired to a sketch curve");

    // ③ AND THE PAIRING IS WHAT MAKES IT SELECTABLE. A world pick on the arc must resolve to a sketch
    //    pick, because that is the direction selection actually travels.
    WorldPick OnArc = {};
    OnArc.Subject = WorldPickSubject::Curve;
    OnArc.Curve   = Arc;
    SketchPick Reached = {};
    Claim(ResolveSketchPickForWorldPick(Stage.Sketch, Stage.Records, Stage.Mapping, OnArc, Reached),
          "and a pick on the arc resolves into the compatibility sketch -- so it can be selected");

    // ④ IDEMPOTENT. Called every frame, it must not keep declaring duplicates.
    Claim(!AdoptWorldSketchCurvesIntoSketch(Stage.World, Stage.Mapping, Stage.Sketch),
          "a second adoption pairs nothing new");
    Claim(Stage.Sketch.Curves().size() == 5u, "and declares no duplicate curve");

    // ⑤ THE WRITEBACK STILL AGREES. Adoption that broke the geometry copy would trade one defect
    //    for another.
    Claim(ApplyWorldSketchToSketch(Stage.World, Stage.Mapping, Stage.Sketch),
          "and the geometry writeback still succeeds across the widened mapping");
}

void ProveMirrorAndPickMapping()
{
    std::printf("\n1. Sketch geometry mirrors into a world sketch and picks round-trip\n");

    Bench Stage;

    Claim(Stage.World.CurveCount() == 4u && Stage.Mapping.Curves.size() == 4u,
          "the sketch's four edges mirror into four persistent world curves with curve mappings");
    Claim(Stage.World.LoopCount() == 1u && Stage.Mapping.Loops.size() == 1u,
          "and the rectangle profile mirrors into one persistent world loop with recorded source mapping");

    SketchPick ProfilePick = {};
    ProfilePick.Subject = SketchPickSubject::Record;
    ProfilePick.Record = Stage.ProfileRecord;
    ResolveProfilePivot(Stage.Sketch, { 1u }, ProfilePick.Position);

    WorldPick Mirrored = {};
    Claim(ResolveWorldPickForSketchPick(Stage.Sketch, Stage.Records, Stage.World, Stage.Mapping, ProfilePick, Mirrored),
          "a selected profile record maps into a world-loop selection");
    Claim(Mirrored.Subject == WorldPickSubject::Loop && Mirrored.Loop.IssuedIndex == 1u,
          "and it chooses the mirrored rectangle loop");

    SketchPick Back = {};
    Claim(ResolveSketchPickForWorldPick(Stage.Sketch, Stage.Records, Stage.Mapping, Mirrored, Back),
          "a mirrored world-loop selection maps back into sketch selection space");
    Claim(Back.Subject == SketchPickSubject::Record && Back.Record.IssuedIndex == Stage.ProfileRecord.IssuedIndex,
          "and it comes back as the same profile record");
}

void ProveWorldNameMappingSurvivesDifferentIssuance()
{
    std::printf("\n2. World snap names adapt at the compatibility boundary when issuance diverges\n");

    WorldSketchMapping Mapping = {};
    Mapping.Curves.push_back({ { 17u }, { 3u } });

    WorldSnapPlacement WorldSnap = {};
    WorldSnap.Subject = WorldSnapSubject::Endpoint;
    WorldSnap.SourceCurve = { 17u };
    WorldSnap.WorldPoint = { (17u << 8u) | 1u };
    WorldSnap.Position = { 25.0, 40.0, 0.0 };
    WorldSnap.Distance = 0.5;

    const SketchSnapPlacement Compatibility = ResolveCompatibilitySnap(WorldSnap, Mapping);
    Claim(Compatibility.SourceCurve.IssuedIndex == 3u
       && Compatibility.SketchPoint.IssuedIndex == ((3u << 8u) | 1u),
          "world snap curve and point names map to their compatibility sketch names explicitly");
}

void ProveWorldBackedViewportFlow()
{
    std::printf("\n3. The world-backed viewport flow selects, drags, and seals a revision\n");

    Bench Stage;
    Stage.Gizmo.Shown = false;
    const SpatialPoint Pivot = Stage.EdgePivot();
    float StartX = 0.0f;
    float StartY = 0.0f;
    float EndX = 0.0f;
    float EndY = 0.0f;
    Claim(ProjectFromCamera(Stage.Camera, Stage.Extent, Pivot, StartX, StartY),
          "the selected edge pivot projects for a live viewport drag");
    Claim(ProjectFromCamera(Stage.Camera, Stage.Extent, { 130.0, 70.0, 40.0 }, EndX, EndY),
          "and the drag target projects too");

    PointerCondition Click = {};
    Click.PositionX = StartX;
    Click.PositionY = StartY;
    Click.ContactPressed = true;
    bool PointerTaken = false;
    Stage.Drive(Click, {}, 1000.0, PointerTaken);

    Claim(PointerTaken,
          "clicking the rectangle edge is consumed by the world-backed selection flow");
    Claim(Stage.SemanticSelection.Subject == SketchPickSubject::Curve,
          "and the standing sketch-space semantic selection becomes that edge");
    Claim(Stage.PendingSelection.IssuedIndex == Stage.ProfileRecord.IssuedIndex,
          "with the profile record kept in sync for the outliner selection");

    PointerCondition Arm = {};
    Arm.PositionX = StartX;
    Arm.PositionY = StartY;
    Arm.ContactHeld = true;
    Arm.TravelX = 1.0f;
    PointerTaken = false;
    Stage.Drive(Arm, {}, 1016.0, PointerTaken);
    Claim(Stage.Transform.Engaged() && Stage.Transform.AwaitingRelease,
          "a held drag on the selected edge starts a pointer-driven world transform session");

    PointerCondition Drag = Arm;
    Drag.PositionX = EndX;
    Drag.PositionY = EndY;
    Drag.TravelX = EndX - StartX;
    Drag.TravelY = EndY - StartY;
    PointerTaken = false;
    Stage.Drive(Drag, {}, 1032.0, PointerTaken);

    const DeclaredWorldCurve* WorldCurve = Stage.World.Resolve(WorldCurveName{ 2u });
    Claim(WorldCurve != nullptr
       && SamePoint(WorldCurve->Geometry.HeldLine().Origin, { 130.0, 20.0, 40.0 })
       && SamePoint(WorldCurve->Geometry.HeldLine().Terminus, { 130.0, 120.0, 40.0 })
       && SamePoint(Stage.Sketch.Curves()[1u].Geometry.HeldLine().Origin, { 130.0, 20.0, 40.0 })
       && SamePoint(Stage.Sketch.Curves()[1u].Geometry.HeldLine().Terminus, { 130.0, 120.0, 40.0 }),
          "the persistent world sketch moves first and the sketch mirror follows it");

    PointerCondition Release = Drag;
    Release.ContactHeld = false;
    Release.ContactReleased = true;
    Release.TravelX = Release.TravelY = 0.0f;
    PointerTaken = false;
    Stage.Drive(Release, {}, 1048.0, PointerTaken);

    Claim(!Stage.Transform.Engaged(),
          "releasing the pointer commits and clears the world-backed session");
    Claim(Stage.Revisions.DeclaredCount() == 1u,
          "and a committed drag seals one sketch revision for undo/redo");
}

void ProveWorldConstraintCompatibilityMirror()
{
    std::printf("\n4. World constraints mirror into compatibility records after world solving\n");

    Bench Stage;
    WorldPick Primary = {};
    Primary.Subject = WorldPickSubject::Curve;
    Primary.Curve = { 1u };
    WorldPick Secondary = {};
    Secondary.Subject = WorldPickSubject::Curve;
    Secondary.Curve = { 2u };

    const bool Applied = ApplyViewportWorldConstraintTool(
        ParametricToolSubject::ParallelConstraint, Stage.Naming,
        Stage.World, Stage.Mapping, Stage.Sketch, Stage.Records, Stage.Revisions,
        Primary, Secondary, Stage.PendingSelection);
    Claim(Applied, "semantic world curve picks author and apply a world constraint through interaction");
    Claim(Stage.World.ConstraintCount() == 1u && Stage.Sketch.Constraints().size() == 1u,
          "the world constraint and its compatibility specification are both stored");
    Claim(Stage.Mapping.Constraints.size() == 1u
       && Stage.Mapping.Constraints[0u].World.IssuedIndex == 1u
       && Stage.Mapping.Constraints[0u].Sketch.IssuedIndex == 1u,
          "world and compatibility constraint identifiers are mapped explicitly");
    Claim(Stage.PendingSelection.Assigned() && Stage.Revisions.DeclaredCount() == 1u,
          "the mirrored constraint writes a workspace record and one undo revision");
    const SpatialPoint CompatibilityEnd = Stage.Sketch.Curves()[1u].Geometry.HeldLine().Terminus;
    Claim(SamePoint(CompatibilityEnd, { 200.0, 0.0, 40.0 }),
          "the compatibility geometry follows the solved world geometry");
}

void ProveWorldConstraintRollback()
{
    std::printf("\n5. Failed world constraint commits roll back every mirrored mutation\n");

    Bench Stage;
    const SpatialPoint WorldBefore = Stage.World.Resolve(WorldCurveName{ 2u })->Geometry.HeldLine().Terminus;
    const SpatialPoint SketchBefore = Stage.Sketch.Curves()[1u].Geometry.HeldLine().Terminus;
    Stage.Mapping.Curves[0u].Sketch = { 99u };

    WorldPick Primary = {};
    Primary.Subject = WorldPickSubject::Curve;
    Primary.Curve = { 1u };
    WorldPick Secondary = {};
    Secondary.Subject = WorldPickSubject::Curve;
    Secondary.Curve = { 2u };
    const WorkspaceRecordName PendingBefore = Stage.PendingSelection;

    Claim(!ApplyViewportWorldConstraintTool(
        ParametricToolSubject::ParallelConstraint, Stage.Naming,
        Stage.World, Stage.Mapping, Stage.Sketch, Stage.Records, Stage.Revisions,
        Primary, Secondary, Stage.PendingSelection),
          "a failed compatibility refresh refuses the world constraint");
    Claim(Stage.World.ConstraintCount() == 0u && Stage.Sketch.Constraints().empty()
       && Stage.Mapping.Constraints.empty(),
          "and removes the unmirrored world constraint and mapping");
    Claim(SamePoint(Stage.World.Resolve(WorldCurveName{ 2u })->Geometry.HeldLine().Terminus, WorldBefore)
       && SamePoint(Stage.Sketch.Curves()[1u].Geometry.HeldLine().Terminus, SketchBefore),
          "the world solve and compatibility geometry return to their previous positions");
    Claim(Stage.Records.DeclaredCount() == 1u,
          "workspace records remain unchanged after the refused world constraint");
    Claim(Stage.Revisions.DeclaredCount() == 0u,
          "revision history remains unchanged after the refused world constraint");
    Claim(Stage.PendingSelection.IssuedIndex == PendingBefore.IssuedIndex,
          "pending selection remains unchanged after the refused world constraint");
}

void ProveWorldDimensionTextEdit()
{
    std::printf("\n6. Dimension text edits drive the world dimension and then refresh the mirror\n");

    Bench Stage;
    const Workplane ActiveWorkplane = { { 0.0, 40.0, 0.0 },
                                        { 0.0, 1.0, 0.0 },
                                        { 1.0, 0.0, 0.0 },
                                        WorkplaneOrigin::Offset };
    SealedPlacement Rectangle = {};
    Rectangle.Subject = SketchSubject::Rectangle;
    Rectangle.Method = PlacementMethod::Extent;
    Rectangle.Anchors = { { 0.0, 40.0, 0.0 }, { 100.0, 40.0, 80.0 } };
    Rectangle.ClosedProfile = true;
    WorkspaceRecordName Profile = {};
    CommitPlacementWorldBacked(ActiveWorkplane, Stage.World, Stage.Mapping,
                               Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                               Rectangle, Profile);

    SealedPlacement Placement = {};
    Placement.Subject = SketchSubject::Dimension;
    Placement.Anchors = { { 0.0, 40.0, 0.0 }, { 100.0, 40.0, 0.0 } };
    Placement.Placements = {
        { SketchSnapSubject::Endpoint, { 1u }, { (1u << 8u) | 1u }, {}, Placement.Anchors[0], 0.0 },
        { SketchSnapSubject::Endpoint, { 1u }, { (1u << 8u) | 2u }, {}, Placement.Anchors[1], 0.0 }
    };
    WorkspaceRecordName DimensionRecord = {};
    CommitPlacementWorldBacked(ActiveWorkplane, Stage.World, Stage.Mapping,
                               Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions,
                               Placement, DimensionRecord);

    TextInputCondition Text = {};
    Text.Intake[0] = '2';
    Text.Intake[1] = '0';
    Text.Intake[2] = '0';
    Text.IntakeCount = 3u;
    Claim(ApplyViewportWorldDimensionTextEdit(Text, Stage.World, Stage.Mapping,
                                              Stage.Sketch, Stage.Records, Stage.Revisions,
                                              DimensionRecord),
          "dimension text edits are applied through the world dimension mapping");
    const SpatialPoint End = Stage.World.Resolve(WorldCurveName{ 1u })->Geometry.HeldLine().Terminus;
    Claim(SamePoint(End, { 200.0, 0.0, 40.0 }),
          "the text edit changes the live world geometry");
    Claim(Stage.Sketch.Dimensions()[0u].Target == 200.0
       && Stage.Revisions.DeclaredCount() == 3u,
          "the compatibility target and one edit revision follow the world result");

    // Force the post-solve compatibility mapping to fail. The world target and geometry must return to
    // the last committed edit rather than exposing a half-applied text edit.
    Stage.Mapping.Dimensions[0u].Sketch = { 99u };
    Text.Intake[0] = '3';
    Claim(!ApplyViewportWorldDimensionTextEdit(Text, Stage.World, Stage.Mapping,
                                               Stage.Sketch, Stage.Records, Stage.Revisions,
                                               DimensionRecord),
          "a failed compatibility refresh refuses the text edit");
    const SpatialPoint RolledBackEnd = Stage.World.Resolve(WorldCurveName{ 1u })->Geometry.HeldLine().Terminus;
    Claim(SamePoint(RolledBackEnd, { 200.0, 0.0, 40.0 })
       && Stage.World.Dimensions()[0u].Target == 200.0
       && Stage.Sketch.Dimensions()[0u].Target == 200.0
       && Stage.Revisions.DeclaredCount() == 3u,
          "and rolls back world geometry, both targets, and revision history as one transaction");
}

void ProveWorldBackedRenderingAndPreview()
{
    std::printf("\n7. The persistent world sketch renders directly and preview appends in screen space\n");

    Bench Stage;
    DeclaredWorldCurve* Raised = Stage.World.Resolve(WorldCurveName{ 2u });
    Claim(Raised != nullptr,
          "the persistent world sketch exposes the rectangle edge for direct rendering edits");
    if (Raised != nullptr)
    {
        Raised->Geometry.HeldLine().Origin.Forward = 100.0;
        Raised->Geometry.HeldLine().Terminus.Forward = 100.0;
    }

    WorkspaceCadPacket Packet = {};
    Discard(ProjectWorldBackedSketchRendering(Stage.World, Stage.Camera,
                                              Stage.Extent, Stage.Drawable, Packet));
    Claim(Packet.SegmentCount > 0u,
          "the mirrored sketch still produces visible linework through the world renderer");
    Claim(Packet.FillCount == 0u,
          "and a no-longer-coplanar closed shape loses only its fill rather than the whole profile");

    std::vector<CurveSpecification> PreviewSpans;
    PreviewSpans.push_back(CurveSpecification::DeclareLine({ 0.0, 0.0, 40.0 }, { 60.0, 0.0, 40.0 }));
    const std::vector<SpatialPoint> Anchors = { { 0.0, 0.0, 40.0 }, { 60.0, 0.0, 40.0 } };
    Claim(ProjectWorldPlacementPreview(Stage.Camera, Stage.Extent, Stage.Drawable,
                                       PreviewSpans, Anchors, { 80.0, 0.0, 40.0 }, Packet),
          "a live placement preview appends through the same world-backed CAD packet path");
    Claim(Packet.MarkerCount >= 3u,
          "and it contributes both anchor markers and the moving hover marker");
}

void ProveOverlayUsesExplicitWorldBasis()
{
    std::printf("\n8. Compatibility overlays can be projected from the active basis\n");

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ViewportStanding View = {};
    const EditorPanelConfiguration Configuration = {};
    const SpatialBasis Active = { { 0.0, 40.0, 0.0 },
                                  { 1.0, 0.0, 0.0 },
                                  { 0.0, 0.0, 1.0 },
                                  { 0.0, 1.0, 0.0 } };
    const SpatialBasis Shifted = { { 120.0, 40.0, 0.0 },
                                   { 1.0, 0.0, 0.0 },
                                   { 0.0, 0.0, 1.0 },
                                   { 0.0, 1.0, 0.0 } };
    OverlayGeometry ActiveOverlay = {};
    OverlayGeometry ShiftedOverlay = {};
    RecordViewportGridOverlay(ActiveOverlay, Extent, Active, View, false, Configuration);
    RecordViewportGridOverlay(ShiftedOverlay, Extent, Shifted, View, false, Configuration);

    Claim(ActiveOverlay.LineCount > 0u && ShiftedOverlay.LineCount > 0u,
          "the explicit active-basis grid entry point records the compatibility lattice");
    Claim(std::fabs(ActiveOverlay.Lines[0u].X0 - ShiftedOverlay.Lines[0u].X0) > 1.0e-4f
       || std::fabs(ActiveOverlay.Lines[0u].X1 - ShiftedOverlay.Lines[0u].X1) > 1.0e-4f,
          "and changing the supplied workplane basis changes the projected overlay rather than reading the sketch basis");
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("WORLD SKETCH SKETCH BRIDGE PROOF\n");
    std::printf("=========================================================================\n");

    ProveMirrorAndPickMapping();
    ProveWorldNameMappingSurvivesDifferentIssuance();
    ProveWorldBackedViewportFlow();
    ProveWorldConstraintCompatibilityMirror();
    ProveWorldConstraintRollback();
    ProveWorldDimensionTextEdit();
    ProveWorldBackedRenderingAndPreview();
    ProveOverlayUsesExplicitWorldBasis();
    ProveOperationCurvesReachTheCompatibilitySketch();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
