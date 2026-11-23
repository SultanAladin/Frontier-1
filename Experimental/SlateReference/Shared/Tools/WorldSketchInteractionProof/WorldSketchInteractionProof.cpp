// 🧩 Phase-6 proof for host-style selection and transform flow on the world sketch.

#include "SlateWorkspace/Discipline/WorldSketchInteraction/Api/WorldSketchInteraction.h"

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
    WorldSketchStructure Sketch;
    PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    ResolvedCamera Perspective = ResolveFreeCamera({ 0.0, 50.0, -300.0 }, 0.0, 0.0, 60.0, true, 1.0);
    ResolvedCamera Ortho = ResolveFreeCamera({ -250.0, 160.0, -250.0 }, 45.0, -20.0, 60.0, false, 3.0);
    SelectionOptions Selection = {};
    ModifierCondition Modifiers = {};
    GizmoOptions Gizmo = {};
    WorldSelectionSet SelectionSet = {};
    WorldPick SemanticSelection = {};
    WorldPick HoveredSelection = {};
    WorldSketchTransformSession Transform = {};
    double LastGPressedMilliseconds = 0.0;

    WorldPlacementFrame Front = {{ 0.0, 0.0, 40.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }};
    WorldCurveName AB = {};
    WorldCurveName BC = {};
    WorldCurveName CD = {};
    WorldCurveName DA = {};
    WorldLoopName Loop = {};

    Bench()
    {
        Selection.Element = SelectionElement::Edge;
        Selection.Tolerance = 8.0f;
        Gizmo.Shown = true;

        AB = Sketch.DeclareLine({ 0.0, 0.0, 40.0 }, { 100.0, 0.0, 40.0 }, Front);
        BC = Sketch.DeclareLine({ 100.0, 0.0, 40.0 }, { 100.0, 100.0, 40.0 }, Front);
        CD = Sketch.DeclareLine({ 100.0, 100.0, 40.0 }, { 0.0, 100.0, 40.0 }, Front);
        DA = Sketch.DeclareLine({ 0.0, 100.0, 40.0 }, { 0.0, 0.0, 40.0 }, Front);
        Loop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });
    }

    WorldPick EdgePick() const
    {
        WorldPick Pick = {};
        Pick.Subject = WorldPickSubject::Curve;
        Pick.Curve = BC;
        ResolveWorldCurvePivot(Sketch, BC, Pick.Position);
        return Pick;
    }

    WorldPick VertexPick() const
    {
        std::vector<WorldPointPlacement> Points;
        ResolveWorldSketchPoints(Sketch, BC, Points);
        WorldPick Pick = {};
        if (Points.size() >= 2u)
        {
            Pick.Subject = WorldPickSubject::Point;
            Pick.Point = Points[1u].Name;
            Pick.Curve = BC;
            Pick.Position = Points[1u].Position;
        }
        return Pick;
    }

    void Drive(const PointerCondition& Pointer,
               const TextInputCondition& Text,
               const ResolvedCamera& Camera,
               double Milliseconds,
               bool& PointerTaken,
               GizmoHandle* HoveredHandle = nullptr)
    {
        DriveWorldSketchSelectionAndTransform(Extent, Pointer, Text, Modifiers,
                                             Selection, Gizmo, Camera,
                                             Sketch, SelectionSet, SemanticSelection, HoveredSelection,
                                             Transform, PointerTaken,
                                             Milliseconds, LastGPressedMilliseconds,
                                             HoveredHandle);
    }
};

void ProveHoverAndClickSelection()
{
    std::printf("\n1. Hover and click selection are routed through the world picker\n");

    Bench Stage;
    const WorldPick Edge = Stage.EdgePick();
    float X = 0.0f;
    float Y = 0.0f;
    Claim(ProjectFromCamera(Stage.Perspective, Stage.Extent, Edge.Position, X, Y),
          "the edge pivot projects for hovering");

    PointerCondition Hover = {};
    Hover.PositionX = X;
    Hover.PositionY = Y;
    bool PointerTaken = false;
    GizmoHandle HoveredHandle = GizmoHandle::None;
    Stage.Drive(Hover, {}, Stage.Perspective, 1000.0, PointerTaken, &HoveredHandle);

    Claim(Stage.HoveredSelection.Subject == WorldPickSubject::Curve,
          "hovering in edge mode resolves a world curve selection");
    Claim(Stage.HoveredSelection.Curve.IssuedIndex == Stage.BC.IssuedIndex,
          "and it is the expected rectangle edge");
    Claim(HoveredHandle == GizmoHandle::None,
          "with nothing selected yet the gizmo offers no hovered handle");

    PointerCondition Click = Hover;
    Click.ContactPressed = true;
    PointerTaken = false;
    Stage.Drive(Click, {}, Stage.Perspective, 1016.0, PointerTaken, &HoveredHandle);
    Claim(PointerTaken,
          "clicking the hovered world edge consumes the press");
    Claim(Stage.SemanticSelection.Subject == WorldPickSubject::Curve,
          "and the hovered edge becomes the standing semantic selection");
    Claim(Stage.SemanticSelection.Curve.IssuedIndex == Stage.BC.IssuedIndex,
          "with the same curve identity carried into the standing selection");
}

void ProveMouseDragAndReleaseCommit()
{
    std::printf("\n2. Mouse dragging the selected edge starts a move session and release commits it\n");

    Bench Stage;
    Stage.Gizmo.Shown = false;
    const WorldPick Edge = Stage.EdgePick();
    float StartX = 0.0f;
    float StartY = 0.0f;
    float EndX = 0.0f;
    float EndY = 0.0f;
    Claim(ProjectFromCamera(Stage.Perspective, Stage.Extent, Edge.Position, StartX, StartY),
          "the selected edge pivot projects for dragging");
    Claim(ProjectFromCamera(Stage.Perspective, Stage.Extent, { 130.0, 70.0, 40.0 }, EndX, EndY),
          "and the aimed drag target projects too");

    Stage.SemanticSelection = Edge;
    RefreshWorldSketchPick(Stage.Sketch, Stage.SemanticSelection);

    PointerCondition Arm = {};
    Arm.PositionX = StartX;
    Arm.PositionY = StartY;
    Arm.ContactHeld = true;
    Arm.TravelX = 1.0f;
    bool PointerTaken = false;
    Stage.Drive(Arm, {}, Stage.Perspective, 2000.0, PointerTaken);
    Claim(Stage.Transform.Engaged(),
          "a held drag over the selected edge starts a world transform session");
    Claim(Stage.Transform.AwaitingRelease,
          "and a pointer-driven session waits for release before committing");

    PointerCondition Drag = Arm;
    Drag.PositionX = EndX;
    Drag.PositionY = EndY;
    Drag.TravelX = EndX - StartX;
    Drag.TravelY = EndY - StartY;
    PointerTaken = false;
    Stage.Drive(Drag, {}, Stage.Perspective, 2016.0, PointerTaken);

    const DeclaredWorldCurve* HeldBC = Stage.Sketch.Resolve(Stage.BC);
    const DeclaredWorldCurve* HeldAB = Stage.Sketch.Resolve(Stage.AB);
    const DeclaredWorldCurve* HeldCD = Stage.Sketch.Resolve(Stage.CD);
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 130.0, 20.0, 40.0 })
                         && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 130.0, 120.0, 40.0 }),
          "the selected world edge moves under the drag");
    Claim(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { 130.0, 20.0, 40.0 })
                         && HeldCD != nullptr && SamePoint(HeldCD->Geometry.HeldLine().Origin, { 130.0, 120.0, 40.0 }),
          "and the shared corners stay welded to the neighbouring edges");

    PointerCondition Release = Drag;
    Release.ContactHeld = false;
    Release.ContactReleased = true;
    Release.TravelX = Release.TravelY = 0.0f;
    PointerTaken = false;
    Stage.Drive(Release, {}, Stage.Perspective, 2032.0, PointerTaken);
    Claim(!Stage.Transform.Engaged(),
          "releasing the pointer commits and closes the world transform session");
    Claim(Stage.SemanticSelection.Subject == WorldPickSubject::Curve
       && Stage.SemanticSelection.Curve.IssuedIndex == Stage.BC.IssuedIndex,
          "the same edge remains selected after the commit");
    Claim(SamePoint(Stage.SemanticSelection.Position, { 130.0, 70.0, 40.0 }),
          "and the standing selection refreshes to the edge's new pivot position");
}

void ProveGizmoAxisStartAndCancel()
{
    std::printf("\n3. A gizmo handle starts an axis-locked move and escape restores it\n");

    Bench Stage;
    Stage.SemanticSelection = Stage.EdgePick();
    RefreshWorldSketchPick(Stage.Sketch, Stage.SemanticSelection);

    GizmoScreenBasis Screen = {};
    Claim(ResolveGizmoScreenBasis(Stage.Perspective, Stage.Extent, Stage.SemanticSelection.Position, Screen),
          "the selected edge can resolve a gizmo screen basis from the active camera");

    PointerCondition Hover = {};
    Hover.PositionX = Screen.PivotX + Screen.AlongX * static_cast<float>(GizmoMeasure::AxisEnd - GizmoMeasure::ConeLength * 0.5);
    Hover.PositionY = Screen.PivotY + Screen.AlongY * static_cast<float>(GizmoMeasure::AxisEnd - GizmoMeasure::ConeLength * 0.5);
    bool PointerTaken = false;
    GizmoHandle HoveredHandle = GizmoHandle::None;
    Stage.Drive(Hover, {}, Stage.Perspective, 3000.0, PointerTaken, &HoveredHandle);
    Claim(HoveredHandle == GizmoHandle::MoveX,
          "hovering the X arrow resolves the X move gizmo handle");

    PointerCondition Press = Hover;
    Press.ContactPressed = true;
    PointerTaken = false;
    Stage.Drive(Press, {}, Stage.Perspective, 3016.0, PointerTaken, &HoveredHandle);
    Claim(Stage.Transform.Engaged(),
          "pressing the hovered X handle starts a transform session");
    Claim(Stage.Transform.Restriction() == TransformRestriction::AxisX,
          "and that session is axis-locked to X from the handle it came from");

    PointerCondition Drag = Hover;
    Drag.ContactHeld = true;
    float EndX = 0.0f;
    float EndY = 0.0f;
    Claim(ProjectFromCamera(Stage.Perspective, Stage.Extent, { 125.0, 50.0, 40.0 }, EndX, EndY),
          "an X-only target projects for the gizmo drag");
    Drag.PositionX = EndX;
    Drag.PositionY = EndY;
    Drag.TravelX = EndX - Hover.PositionX;
    Drag.TravelY = EndY - Hover.PositionY;
    PointerTaken = false;
    Stage.Drive(Drag, {}, Stage.Perspective, 3032.0, PointerTaken, &HoveredHandle);

    const DeclaredWorldCurve* HeldBC = Stage.Sketch.Resolve(Stage.BC);
    const double ExpectedX = 100.0 + Stage.Transform.PreviewValue;
    Claim(HeldBC != nullptr
       && Near(HeldBC->Geometry.HeldLine().Origin.Left, ExpectedX)
       && Near(HeldBC->Geometry.HeldLine().Terminus.Left, ExpectedX)
       && Near(HeldBC->Geometry.HeldLine().Origin.Up, 0.0)
       && Near(HeldBC->Geometry.HeldLine().Terminus.Up, 100.0)
       && Near(HeldBC->Geometry.HeldLine().Origin.Forward, 40.0)
       && Near(HeldBC->Geometry.HeldLine().Terminus.Forward, 40.0),
          "the X-handle drag moves the edge only along world X");

    TextInputCondition Cancel = {};
    Cancel.CancelPressed = true;
    PointerCondition Still = Drag;
    PointerTaken = false;
    Stage.Drive(Still, Cancel, Stage.Perspective, 3048.0, PointerTaken, &HoveredHandle);
    HeldBC = Stage.Sketch.Resolve(Stage.BC);
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 100.0, 0.0, 40.0 })
                         && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 100.0, 100.0, 40.0 }),
          "escape cancels the gizmo drag and restores the original edge");
}

void ProveKeyboardRestrictionAndNumeric()
{
    std::printf("\n4. Keyboard move grammar drives world-axis restriction and numeric distance\n");

    Bench Stage;
    Stage.Selection.Element = SelectionElement::Object;
    WorldPick Whole = {};
    Whole.Subject = WorldPickSubject::Loop;
    Whole.Loop = Stage.Loop;
    ResolveWorldLoopPivot(Stage.Sketch, Stage.Loop, Whole.Position);
    Stage.SemanticSelection = Whole;

    PointerCondition Pointer = {};
    float PivotX = 0.0f;
    float PivotY = 0.0f;
    Claim(ProjectFromCamera(Stage.Ortho, Stage.Extent, Whole.Position, PivotX, PivotY),
          "the loop pivot projects for a keyboard-started transform");
    Pointer.PositionX = PivotX;
    Pointer.PositionY = PivotY;

    TextInputCondition Start = {};
    Start.Intake[0] = 'g';
    Start.Intake[1] = '\0';
    Start.IntakeCount = 1u;
    bool PointerTaken = false;
    Stage.Drive(Pointer, Start, Stage.Ortho, 4000.0, PointerTaken);
    Claim(Stage.Transform.Engaged(),
          "typing G with a standing selection starts a world move session");
    Claim(!Stage.Transform.AwaitingRelease,
          "and a keyboard-started world move does not wait for a pointer release");

    TextInputCondition RestrictAndAmount = {};
    RestrictAndAmount.Intake[0] = 'z';
    RestrictAndAmount.Intake[1] = '6';
    RestrictAndAmount.Intake[2] = '0';
    RestrictAndAmount.Intake[3] = '\0';
    RestrictAndAmount.IntakeCount = 3u;
    PointerTaken = false;
    Stage.Drive(Pointer, RestrictAndAmount, Stage.Ortho, 4016.0, PointerTaken);

    const DeclaredWorldCurve* HeldAB = Stage.Sketch.Resolve(Stage.AB);
    Claim(Stage.Transform.Restriction() == TransformRestriction::AxisZ,
          "typing Z during the session locks it to world Z");
    Claim(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Origin, { 0.0, 0.0, 100.0 })
                         && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { 100.0, 0.0, 100.0 }),
          "and the numeric distance moves the selected loop sixty units on world Z");

    TextInputCondition Accept = {};
    Accept.AcceptPressed = true;
    PointerTaken = false;
    Stage.Drive(Pointer, Accept, Stage.Ortho, 4032.0, PointerTaken);
    Claim(!Stage.Transform.Engaged(),
          "accept commits the keyboard-driven world move session");
}

void ProveCurveSlideGesture()
{
    std::printf("\n5. A fast second G starts slide-along-curve instead of free drag\n");

    WorldSketchStructure Sketch;
    const WorldPlacementFrame Support = {{}, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};
    const WorldCurveName Diagonal = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }, Support);

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ResolvedCamera Camera = ResolveFreeCamera({ -220.0, 120.0, -220.0 }, 45.0, -20.0, 60.0, false, 3.0);
    SelectionOptions Selection = {};
    Selection.Element = SelectionElement::Edge;
    GizmoOptions Gizmo = {};
    WorldPick Semantic = {};
    Semantic.Subject = WorldPickSubject::Curve;
    Semantic.Curve = Diagonal;
    ResolveWorldCurvePivot(Sketch, Diagonal, Semantic.Position);
    WorldPick Hovered = {};
    WorldSketchTransformSession Transform = {};
    double LastG = 1000.0;
    bool PointerTaken = false;

    float PivotX = 0.0f;
    float PivotY = 0.0f;
    Claim(ProjectFromCamera(Camera, Extent, Semantic.Position, PivotX, PivotY),
          "the diagonal curve pivot projects for the slide gesture");

    PointerCondition Pointer = {};
    Pointer.PositionX = PivotX;
    Pointer.PositionY = PivotY;
    TextInputCondition Start = {};
    Start.Intake[0] = 'g';
    Start.Intake[1] = '\0';
    Start.IntakeCount = 1u;
    WorldSelectionSet SelectionSet = {};
    SetWorldPick(SelectionSet, Semantic, false);
    DriveWorldSketchSelectionAndTransform(Extent, Pointer, Start, {},
                                         Selection, Gizmo, Camera,
                                         Sketch, SelectionSet, Semantic, Hovered, Transform,
                                         PointerTaken, 1200.0, LastG, nullptr);
    Claim(Transform.Engaged(),
          "a second G within the tap window starts a session");
    Claim(Transform.Restriction() == TransformRestriction::Curve && Transform.SlideAlongCurve(),
          "and that session begins in slide-along-curve mode");

    const SpatialDirection Slide = ResolveWorldCurveSlideDirection(Sketch, Diagonal, Semantic.Position);
    float EndX = 0.0f;
    float EndY = 0.0f;
    Claim(ProjectFromCamera(Camera, Extent, Added(Semantic.Position, Scaled(Slide, 25.0)), EndX, EndY),
          "a point along the curve tangent projects for the slide update");

    Pointer.ContactHeld = true;
    Pointer.PositionX = EndX;
    Pointer.PositionY = EndY;
    Pointer.TravelX = EndX - PivotX;
    Pointer.TravelY = EndY - PivotY;
    PointerTaken = false;
    DriveWorldSketchSelectionAndTransform(Extent, Pointer, {}, {},
                                         Selection, Gizmo, Camera,
                                         Sketch, SelectionSet, Semantic, Hovered, Transform,
                                         PointerTaken, 1216.0, LastG, nullptr);

    const DeclaredWorldCurve* Held = Sketch.Resolve(Diagonal);
    Claim(Held != nullptr
       && SamePoint(Held->Geometry.HeldLine().Origin,
                    Added(SpatialPoint{ 0.0, 0.0, 0.0 }, Scaled(Slide, 25.0)))
       && SamePoint(Held->Geometry.HeldLine().Terminus,
                    Added(SpatialPoint{ 100.0, 0.0, 100.0 }, Scaled(Slide, 25.0))),
          "updating the slide session moves the curve only along its own direction");
}

//------------------------------------------------------------------------------------------------------------------------
//                                        6. ONE G MOVES IN SCREEN SPACE
//------------------------------------------------------------------------------------------------------------------------

/// 🔴 A SINGLE G USED TO BECOME A CURVE SLIDE. The start arm stamped `LastGPressedMilliseconds` and the
///    engaged arm then re-read the SAME tap in the same frame — one tap, zero elapsed, inside the 350 ms
///    window — so every plain G satisfied the double-tap gesture and locked itself to a tangent. What the
///    artist saw was geometry running along its own edge instead of following the mouse.
void ProveSingleTapMovesWithTheMouse()
{
    std::printf("\n6. One G moves in screen space, following the mouse rather than an axis\n");

    Bench Stage;
    Stage.Selection.Element = SelectionElement::Object;
    Stage.Gizmo.Shown = false;

    WorldPick Whole = {};
    Whole.Subject = WorldPickSubject::Loop;
    Whole.Loop = Stage.Loop;
    ResolveWorldLoopPivot(Stage.Sketch, Stage.Loop, Whole.Position);
    Stage.SemanticSelection = Whole;
    SetWorldPick(Stage.SelectionSet, Whole, false);

    float PivotX = 0.0f;
    float PivotY = 0.0f;
    Claim(ProjectFromCamera(Stage.Perspective, Stage.Extent, Whole.Position, PivotX, PivotY),
          "the loop pivot projects for a keyboard-started move");

    PointerCondition Pointer = {};
    Pointer.PositionX = PivotX;
    Pointer.PositionY = PivotY;

    TextInputCondition Start = {};
    Start.Intake[0] = 'g';
    Start.Intake[1] = '\0';
    Start.IntakeCount = 1u;
    bool PointerTaken = false;
    Stage.Drive(Pointer, Start, Stage.Perspective, 7000.0, PointerTaken);

    Claim(Stage.Transform.Engaged(),
          "a single G with a standing selection starts a move");
    Claim(!Stage.Transform.SlideAlongCurve(),
          "and that move is NOT a curve slide — one tap is not the double-tap gesture");
    Claim(Stage.Transform.Restriction() == TransformRestriction::Free,
          "a single G leaves the move unrestricted, which is what screen-space means");

    // 🔴 The whole claim of screen-space movement in one line: wherever the mouse goes, the geometry
    //    draws under it. Both screen axes are exercised, because a move that tracked only one of them
    //    would still read as "parallel to an axis" to the artist.
    const float TravelX[4] = {  160.0f, -160.0f,    0.0f,  110.0f };
    const float TravelY[4] = {    0.0f,    0.0f, -140.0f, -90.0f };
    const char* Sentence[4] = { "dragging right draws the shape under the pointer",
                                "dragging left draws the shape under the pointer",
                                "dragging up draws the shape under the pointer",
                                "dragging diagonally draws the shape under the pointer" };

    for (std::size_t Index = 0u; Index < 4u; ++Index)
    {
        Bench Local;
        Local.Selection.Element = SelectionElement::Object;
        Local.Gizmo.Shown = false;
        WorldPick LocalWhole = {};
        LocalWhole.Subject = WorldPickSubject::Loop;
        LocalWhole.Loop = Local.Loop;
        ResolveWorldLoopPivot(Local.Sketch, Local.Loop, LocalWhole.Position);
        Local.SemanticSelection = LocalWhole;
        SetWorldPick(Local.SelectionSet, LocalWhole, false);

        PointerCondition Begin = {};
        Begin.PositionX = PivotX;
        Begin.PositionY = PivotY;
        bool Taken = false;
        Local.Drive(Begin, Start, Local.Perspective, 7100.0, Taken);

        PointerCondition Moved = Begin;
        Moved.PositionX = PivotX + TravelX[Index];
        Moved.PositionY = PivotY + TravelY[Index];
        Taken = false;
        Local.Drive(Moved, {}, Local.Perspective, 7116.0, Taken);

        SpatialPoint Landed = {};
        ResolveWorldLoopPivot(Local.Sketch, Local.Loop, Landed);
        float DrawnX = 0.0f;
        float DrawnY = 0.0f;
        Claim(ProjectFromCamera(Local.Perspective, Local.Extent, Landed, DrawnX, DrawnY)
           && Near(DrawnX, Moved.PositionX, 0.5)
           && Near(DrawnY, Moved.PositionY, 0.5),
              Sentence[Index]);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       7. G G SLIDES BOTH WAYS ALONG THE CURVE
//------------------------------------------------------------------------------------------------------------------------

/// 🔴 A SLIDE ONLY EVER RAN ONE WAY. The tangent was chosen once, from a motion hint that defaulted to
///    `SpatialDirection{}` — which is `(0, 0, 1)`, not zero — so the branch nearest world +Z always won and
///    the drag was projected onto it for the whole session. Dragging back down the line drove the
///    projection negative against a frozen direction instead of following the other way.
void ProveSlideRunsBothWays()
{
    std::printf("\n7. G G slides along the curve in both directions\n");

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ResolvedCamera Camera = ResolveFreeCamera({ -200.0, 150.0, -200.0 }, 45.0, -25.0, 60.0, true, 1.0);

    // 📝 Declared from +Z toward -Z on purpose: the stored tangent runs OPPOSITE world +Z, which is the
    //    exact geometry the defaulted hint used to override.
    const double Aimed[2] = { 30.0, -30.0 };
    const char* Sentence[2] = { "sliding forward along the line follows the pointer",
                                "sliding backward along the same line follows the pointer too" };

    for (std::size_t Index = 0u; Index < 2u; ++Index)
    {
        WorldSketchStructure Sketch;
        const WorldPlacementFrame Support = {{}, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};
        const WorldCurveName Line = Sketch.DeclareLine({ 0.0, 0.0, 80.0 }, { 0.0, 0.0, -80.0 }, Support);

        SelectionOptions Selection = {};
        Selection.Element = SelectionElement::Edge;
        GizmoOptions Gizmo = {};
        WorldPick Semantic = {};
        Semantic.Subject = WorldPickSubject::Curve;
        Semantic.Curve = Line;
        ResolveWorldCurvePivot(Sketch, Line, Semantic.Position);
        const SpatialPoint Anchor = Semantic.Position;
        WorldPick Hovered = {};
        WorldSelectionSet SelectionSet = {};
        SetWorldPick(SelectionSet, Semantic, false);
        WorldSketchTransformSession Transform = {};
        double LastG = 0.0;
        bool PointerTaken = false;

        float PivotX = 0.0f;
        float PivotY = 0.0f;
        Claim(ProjectFromCamera(Camera, Extent, Anchor, PivotX, PivotY),
              "the curve pivot projects for the slide gesture");

        PointerCondition Pointer = {};
        Pointer.PositionX = PivotX;
        Pointer.PositionY = PivotY;

        TextInputCondition DoubleTap = {};
        DoubleTap.Intake[0] = 'g';
        DoubleTap.Intake[1] = 'g';
        DoubleTap.Intake[2] = '\0';
        DoubleTap.IntakeCount = 2u;
        DriveWorldSketchSelectionAndTransform(Extent, Pointer, DoubleTap, {}, Selection, Gizmo, Camera,
                                              Sketch, SelectionSet, Semantic, Hovered, Transform,
                                              PointerTaken, 8000.0, LastG, nullptr);
        if (Index == 0u)
            Claim(Transform.SlideAlongCurve() && Transform.Restriction() == TransformRestriction::Curve,
                  "two G in one frame begin a slide along the curve");

        const SpatialDirection Aim = { 0.0, 0.0, Aimed[Index] };
        float TargetX = 0.0f;
        float TargetY = 0.0f;
        Claim(ProjectFromCamera(Camera, Extent, Added(Anchor, Aim), TargetX, TargetY),
              "the aimed point along the line projects");
        Pointer.PositionX = TargetX;
        Pointer.PositionY = TargetY;
        PointerTaken = false;
        DriveWorldSketchSelectionAndTransform(Extent, Pointer, {}, {}, Selection, Gizmo, Camera,
                                              Sketch, SelectionSet, Semantic, Hovered, Transform,
                                              PointerTaken, 8016.0, LastG, nullptr);

        const DeclaredWorldCurve* Held = Sketch.Resolve(Line);
        Claim(Held != nullptr
           && SamePoint(Held->Geometry.HeldLine().Origin, Added(SpatialPoint{ 0.0, 0.0, 80.0 }, Aim))
           && SamePoint(Held->Geometry.HeldLine().Terminus, Added(SpatialPoint{ 0.0, 0.0, -80.0 }, Aim)),
              Sentence[Index]);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                    8. ONE SLIDE REVERSES WITHIN A SINGLE SESSION
//------------------------------------------------------------------------------------------------------------------------

/// 🔴 The two directions above are each proven from a fresh session, which a frozen tangent could still
///    satisfy if it were re-chosen only at the tap. This drives ONE session forward, back through the
///    start, and out the far side, which only a per-frame tangent choice can follow.
void ProveSlideReversesMidDrag()
{
    std::printf("\n8. A single slide session reverses when the mouse comes back\n");

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ResolvedCamera Camera = ResolveFreeCamera({ -200.0, 150.0, -200.0 }, 45.0, -25.0, 60.0, true, 1.0);

    WorldSketchStructure Sketch;
    const WorldPlacementFrame Support = {{}, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};
    const WorldCurveName Line = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 140.0, 0.0, 0.0 }, Support);

    SelectionOptions Selection = {};
    Selection.Element = SelectionElement::Edge;
    GizmoOptions Gizmo = {};
    WorldPick Semantic = {};
    Semantic.Subject = WorldPickSubject::Curve;
    Semantic.Curve = Line;
    ResolveWorldCurvePivot(Sketch, Line, Semantic.Position);
    const SpatialPoint Anchor = Semantic.Position;
    WorldPick Hovered = {};
    WorldSelectionSet SelectionSet = {};
    SetWorldPick(SelectionSet, Semantic, false);
    WorldSketchTransformSession Transform = {};
    double LastG = 0.0;
    bool PointerTaken = false;

    float PivotX = 0.0f;
    float PivotY = 0.0f;
    Claim(ProjectFromCamera(Camera, Extent, Anchor, PivotX, PivotY),
          "the line pivot projects for a sweeping slide");

    PointerCondition Pointer = {};
    Pointer.PositionX = PivotX;
    Pointer.PositionY = PivotY;
    TextInputCondition DoubleTap = {};
    DoubleTap.Intake[0] = 'g';
    DoubleTap.Intake[1] = 'g';
    DoubleTap.Intake[2] = '\0';
    DoubleTap.IntakeCount = 2u;
    DriveWorldSketchSelectionAndTransform(Extent, Pointer, DoubleTap, {}, Selection, Gizmo, Camera,
                                          Sketch, SelectionSet, Semantic, Hovered, Transform,
                                          PointerTaken, 8500.0, LastG, nullptr);

    const double Sweep[5] = { 35.0, 60.0, 10.0, -25.0, -55.0 };
    double Time = 8516.0;
    bool Tracked = true;
    for (std::size_t Index = 0u; Index < 5u; ++Index)
    {
        const SpatialDirection Aim = { Sweep[Index], 0.0, 0.0 };
        float TargetX = 0.0f;
        float TargetY = 0.0f;
        ProjectFromCamera(Camera, Extent, Added(Anchor, Aim), TargetX, TargetY);
        Pointer.PositionX = TargetX;
        Pointer.PositionY = TargetY;
        PointerTaken = false;
        DriveWorldSketchSelectionAndTransform(Extent, Pointer, {}, {}, Selection, Gizmo, Camera,
                                              Sketch, SelectionSet, Semantic, Hovered, Transform,
                                              PointerTaken, Time, LastG, nullptr);
        Time += 16.0;

        const DeclaredWorldCurve* Held = Sketch.Resolve(Line);
        if (Held == nullptr || !Near(Held->Geometry.HeldLine().Origin.Left, Sweep[Index]))
            Tracked = false;
    }
    Claim(Tracked,
          "the geometry follows the pointer through the reversal instead of sticking to one sense");
}

//------------------------------------------------------------------------------------------------------------------------
//                                    9. A CORNER SLIDES ALONG EITHER EDGE IT JOINS
//------------------------------------------------------------------------------------------------------------------------

/// 🔴 Only the curve the pick happened to record was searched for tangents, so a vertex where two curves
///    meet could leave along one of them and was projected sideways onto it when the artist aimed down the
///    other. Blender lets a corner slide along whichever edge the mouse indicates.
void ProveCornerSlidesAlongEitherEdge()
{
    std::printf("\n9. A corner joining two edges slides along whichever the mouse indicates\n");

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ResolvedCamera Camera = ResolveFreeCamera({ -200.0, 150.0, -200.0 }, 45.0, -25.0, 60.0, true, 1.0);

    const SpatialDirection Aims[4] = { {  30.0, 0.0,   0.0 }, { -30.0, 0.0,   0.0 },
                                       {   0.0, 0.0,  30.0 }, {   0.0, 0.0, -30.0 } };
    const char* Sentence[4] = { "the corner slides out along its first edge",
                                "and back along that same edge",
                                "and out along the second edge that meets there",
                                "and back along that second edge" };

    for (std::size_t Index = 0u; Index < 4u; ++Index)
    {
        WorldSketchStructure Sketch;
        const WorldPlacementFrame Support = {{}, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};
        const WorldCurveName AlongX = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 120.0, 0.0, 0.0 }, Support);
        static_cast<void>(Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 120.0 }, Support));

        std::vector<WorldPointPlacement> Points;
        ResolveWorldSketchPoints(Sketch, AlongX, Points);

        SelectionOptions Selection = {};
        Selection.Element = SelectionElement::Vertex;
        GizmoOptions Gizmo = {};
        WorldPick Semantic = {};
        Semantic.Subject = WorldPickSubject::Point;
        Semantic.Point = Points[0u].Name;
        Semantic.Curve = AlongX;
        Semantic.Position = Points[0u].Position;
        WorldPick Hovered = {};
        WorldSelectionSet SelectionSet = {};
        SetWorldPick(SelectionSet, Semantic, false);
        WorldSketchTransformSession Transform = {};
        double LastG = 0.0;
        bool PointerTaken = false;

        float PivotX = 0.0f;
        float PivotY = 0.0f;
        ProjectFromCamera(Camera, Extent, Semantic.Position, PivotX, PivotY);
        PointerCondition Pointer = {};
        Pointer.PositionX = PivotX;
        Pointer.PositionY = PivotY;

        TextInputCondition DoubleTap = {};
        DoubleTap.Intake[0] = 'g';
        DoubleTap.Intake[1] = 'g';
        DoubleTap.Intake[2] = '\0';
        DoubleTap.IntakeCount = 2u;
        DriveWorldSketchSelectionAndTransform(Extent, Pointer, DoubleTap, {}, Selection, Gizmo, Camera,
                                              Sketch, SelectionSet, Semantic, Hovered, Transform,
                                              PointerTaken, 8800.0, LastG, nullptr);

        float TargetX = 0.0f;
        float TargetY = 0.0f;
        ProjectFromCamera(Camera, Extent, Added(SpatialPoint{ 0.0, 0.0, 0.0 }, Aims[Index]),
                          TargetX, TargetY);
        Pointer.PositionX = TargetX;
        Pointer.PositionY = TargetY;
        PointerTaken = false;
        DriveWorldSketchSelectionAndTransform(Extent, Pointer, {}, {}, Selection, Gizmo, Camera,
                                              Sketch, SelectionSet, Semantic, Hovered, Transform,
                                              PointerTaken, 8816.0, LastG, nullptr);

        const DeclaredWorldCurve* Held = Sketch.Resolve(AlongX);
        Claim(Held != nullptr
           && SamePoint(Held->Geometry.HeldLine().Origin, Added(SpatialPoint{ 0.0, 0.0, 0.0 }, Aims[Index])),
              Sentence[Index]);
    }
}

} // namespace

void ProveSelectionModeGovernsTheGizmo()
{
    std::printf("\n10. The element mode governs what may be SELECTED, not merely what may be picked\n");

    // 🔴 SELECTING AND TRANSFORMING ANSWERED TO DIFFERENT AUTHORITIES. The picker consulted the element
    //    mode, so it never returned a vertex in Edge mode — but a selection also arrives by other roads,
    //    seeded from the outliner row through the compatibility bridge, and that road asked nothing. A
    //    vertex therefore became the standing selection in Edge mode, the gizmo drew for it, and the very
    //    next frame the drag refused it: the gizmo appeared and vanished, and nothing could be moved.
    //    An inadmissible pick must be a no-op with no gizmo at all.
    {
        Bench Stage;
        Stage.Selection.Element = SelectionElement::Edge;
        Stage.SemanticSelection = Stage.VertexPick();
        Claim(Stage.SemanticSelection.Standing(), "a vertex pick is available to seed with");

        PointerCondition Idle = {};
        Idle.PositionX = 4.0f;
        Idle.PositionY = 4.0f;
        bool PointerTaken = false;
        GizmoHandle HoveredHandle = GizmoHandle::None;
        Stage.Drive(Idle, {}, Stage.Perspective, 1000.0, PointerTaken, &HoveredHandle);

        Claim(!Stage.SemanticSelection.Standing(),
              "a VERTEX seeded from the outliner is refused while the mode reads Edge");
        Claim(Stage.SelectionSet.Empty(),
              "and it never enters the selection set, so no gizmo is drawn for it");
    }

    // The same seed under the mode that owns it is kept, so the filter is a filter and not a ban.
    {
        Bench Stage;
        Stage.Selection.Element = SelectionElement::Vertex;
        Stage.SemanticSelection = Stage.VertexPick();

        PointerCondition Idle = {};
        Idle.PositionX = 4.0f;
        Idle.PositionY = 4.0f;
        bool PointerTaken = false;
        Stage.Drive(Idle, {}, Stage.Perspective, 1000.0, PointerTaken);

        Claim(Stage.SemanticSelection.Subject == WorldPickSubject::Point,
              "the same vertex IS kept when the mode reads Vertex");
        Claim(Stage.SelectionSet.Items.size() == 1u,
              "and it stands as the one selected item");
    }

    // Changing mode with something already selected drops it, rather than leaving a gizmo
    // for a kind the drag would then refuse.
    {
        Bench Stage;
        Stage.Selection.Element = SelectionElement::Edge;
        const WorldPick Edge = Stage.EdgePick();
        float X = 0.0f;
        float Y = 0.0f;
        Claim(ProjectFromCamera(Stage.Perspective, Stage.Extent, Edge.Position, X, Y),
              "the edge pivot projects");

        PointerCondition Click = {};
        Click.PositionX = X;
        Click.PositionY = Y;
        Click.ContactPressed = true;
        bool PointerTaken = false;
        Stage.Drive(Click, {}, Stage.Perspective, 1000.0, PointerTaken);
        Claim(Stage.SemanticSelection.Subject == WorldPickSubject::Curve,
              "an edge is selected while the mode reads Edge");

        Stage.Selection.Element = SelectionElement::Vertex;
        PointerCondition Idle = {};
        Idle.PositionX = X;
        Idle.PositionY = Y;
        bool SecondTaken = false;
        GizmoHandle HoveredHandle = GizmoHandle::None;
        Stage.Drive(Idle, {}, Stage.Perspective, 1016.0, SecondTaken, &HoveredHandle);

        Claim(!Stage.SemanticSelection.Standing(),
              "switching to Vertex mode drops the standing edge instead of keeping a dead gizmo");
        Claim(HoveredHandle == GizmoHandle::None,
              "and no gizmo handle is offered for it");
    }
}

int main()
{
    std::printf("=========================================================================\n");
    std::printf("WORLD SKETCH INTERACTION PROOF\n");
    std::printf("=========================================================================\n");

    ProveHoverAndClickSelection();
    ProveMouseDragAndReleaseCommit();
    ProveGizmoAxisStartAndCancel();
    ProveKeyboardRestrictionAndNumeric();
    ProveCurveSlideGesture();
    ProveSingleTapMovesWithTheMouse();
    ProveSlideRunsBothWays();
    ProveSlideReversesMidDrag();
    ProveCornerSlidesAlongEitherEdge();
    ProveSelectionModeGovernsTheGizmo();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
