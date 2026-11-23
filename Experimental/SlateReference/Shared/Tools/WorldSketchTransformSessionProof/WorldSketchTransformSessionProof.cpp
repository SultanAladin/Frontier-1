// 🧩 Phase-5 proof for interactive world-space transforms.

#include "SlateWorkspace/Discipline/WorldSketchTransformSession/Api/WorldSketchTransformSession.h"
#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"

#include <cmath>
#include <cstdio>
#include <string>

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

bool SameDirection(const SpatialDirection& Left,
                   const SpatialDirection& Right,
                   double Tolerance = 1.0e-6)
{
    return Near(Left.Left, Right.Left, Tolerance)
        && Near(Left.Up, Right.Up, Tolerance)
        && Near(Left.Forward, Right.Forward, Tolerance);
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
    ResolvedCamera FrontPerspective = ResolveFreeCamera({ 0.0, 50.0, -300.0 }, 0.0, 0.0, 60.0, true, 1.0);
    ResolvedCamera OrthoView = ResolveFreeCamera({ -250.0, 160.0, -250.0 }, 45.0, -20.0, 60.0, false, 3.0);

    WorldPlacementFrame Front = {{ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }};
    WorldCurveName AB = {};
    WorldCurveName BC = {};
    WorldCurveName CD = {};
    WorldCurveName DA = {};
    WorldLoopName Loop = {};

    Bench()
    {
        AB = Sketch.DeclareLine({ 0.0, 0.0, 40.0 }, { 100.0, 0.0, 40.0 }, Front);
        BC = Sketch.DeclareLine({ 100.0, 0.0, 40.0 }, { 100.0, 100.0, 40.0 }, Front);
        CD = Sketch.DeclareLine({ 100.0, 100.0, 40.0 }, { 0.0, 100.0, 40.0 }, Front);
        DA = Sketch.DeclareLine({ 0.0, 100.0, 40.0 }, { 0.0, 0.0, 40.0 }, Front);
        Loop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });
    }

    WorldPick CurvePick() const
    {
        WorldPick Pick = {};
        Pick.Subject = WorldPickSubject::Curve;
        Pick.Curve = BC;
        ResolveWorldCurvePivot(Sketch, BC, Pick.Position);
        return Pick;
    }

    WorldPick PointPick() const
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

    WorldPick LoopPick() const
    {
        WorldPick Pick = {};
        Pick.Subject = WorldPickSubject::Loop;
        Pick.Loop = Loop;
        ResolveWorldLoopPivot(Sketch, Loop, Pick.Position);
        return Pick;
    }
};

void ProvePlacementResolution()
{
    std::printf("\n1. A world pick resolves the placements a transform must move\n");

    Bench Stage;

    SpatialPoint Pivot = {};
    std::vector<WorldPlacementSubject> Placements;
    Claim(ResolveWorldTransformPlacements(Stage.Sketch, Stage.CurvePick(), Pivot, Placements),
          "a world edge selection offers placements");
    Claim(Placements.size() >= 4u,
          "and dragging one edge gathers its own placements plus the coincident shared corners around it");
    Claim(SamePoint(Pivot, { 100.0, 50.0, 40.0 }),
          "the edge pivot is the curve midpoint in world space");

    Placements.clear();
    Claim(ResolveWorldTransformPlacements(Stage.Sketch, Stage.LoopPick(), Pivot, Placements),
          "a whole-loop pick also offers placements");
    Claim(Placements.size() >= 8u,
          "and it gathers the placements of every edge in the loop");
}

void ProvePerspectiveScreenMove()
{
    std::printf("\n2. Free dragging in perspective follows the camera plane through the pivot\n");

    Bench Stage;
    const WorldPick Pick = Stage.CurvePick();

    float StartX = 0.0f;
    float StartY = 0.0f;
    float EndX = 0.0f;
    float EndY = 0.0f;
    Claim(ProjectFromCamera(Stage.FrontPerspective, Stage.Extent, Pick.Position, StartX, StartY),
          "the selected edge pivot projects for the drag start");
    Claim(ProjectFromCamera(Stage.FrontPerspective, Stage.Extent, { 130.0, 70.0, 40.0 }, EndX, EndY),
          "and the aimed free-drag target on the same camera plane projects too");

    WorldSketchTransformSession Session;
    Claim(StartWorldSketchTransformSession(Stage.Sketch, Stage.FrontPerspective, Stage.Extent,
                                          StartX, StartY, Pick,
                                          TransformRestriction::Free, false, Session),
          "a perspective world transform starts");

    UpdateWorldSketchTransformSession(Stage.FrontPerspective, Stage.Extent, EndX, EndY,
                                     Stage.Sketch, Session);

    const DeclaredWorldCurve* HeldBC = Stage.Sketch.Resolve(Stage.BC);
    const DeclaredWorldCurve* HeldAB = Stage.Sketch.Resolve(Stage.AB);
    const DeclaredWorldCurve* HeldCD = Stage.Sketch.Resolve(Stage.CD);
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 130.0, 20.0, 40.0 })
                         && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 130.0, 120.0, 40.0 }),
          "the selected edge moved by the screen-plane offset in true world coordinates");
    Claim(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { 130.0, 20.0, 40.0 })
                         && HeldCD != nullptr && SamePoint(HeldCD->Geometry.HeldLine().Origin, { 130.0, 120.0, 40.0 }),
          "and the two neighbouring corners followed it instead of splitting apart");
    Claim(Session.Changed,
          "the live preview marks the session changed");

    CancelWorldSketchTransformSession(Stage.Sketch, Session);
    HeldBC = Stage.Sketch.Resolve(Stage.BC);
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 100.0, 0.0, 40.0 })
                         && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 100.0, 100.0, 40.0 }),
          "cancelling restores the original edge exactly");
}

void ProveAxisLockedZMove()
{
    std::printf("\n3. Axis locks let a whole picked loop move on world Z\n");

    Bench Stage;
    const WorldPick Pick = Stage.LoopPick();

    float StartX = 0.0f;
    float StartY = 0.0f;
    float EndX = 0.0f;
    float EndY = 0.0f;
    Claim(ProjectFromCamera(Stage.OrthoView, Stage.Extent, Pick.Position, StartX, StartY),
          "the loop pivot projects for the start of a Z move");
    Claim(ProjectFromCamera(Stage.OrthoView, Stage.Extent,
                            { Pick.Position.Left, Pick.Position.Up, Pick.Position.Forward + 60.0 }, EndX, EndY),
          "and a point sixty units higher on world Z projects for the locked target");

    WorldSketchTransformSession Session;
    Claim(StartWorldSketchTransformSession(Stage.Sketch, Stage.OrthoView, Stage.Extent,
                                          StartX, StartY, Pick,
                                          TransformRestriction::AxisZ, false, Session),
          "an axis-locked world transform starts");

    UpdateWorldSketchTransformSession(Stage.OrthoView, Stage.Extent, EndX, EndY,
                                     Stage.Sketch, Session);

    const DeclaredWorldCurve* HeldAB = Stage.Sketch.Resolve(Stage.AB);
    const DeclaredWorldCurve* HeldBC = Stage.Sketch.Resolve(Stage.BC);
    Claim(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Origin, { 0.0, 0.0, 100.0 })
                         && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { 100.0, 0.0, 100.0 }),
          "the first loop edge moved only on world Z");
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 100.0, 0.0, 100.0 })
                         && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 100.0, 100.0, 100.0 }),
          "and the rest of the loop followed as one rigid object move");
    Claim(Near(Session.PreviewValue, 60.0, 1.0e-4),
          "the session preview reports the locked Z distance");

    CommitWorldSketchTransformSession(Session);
    Claim(!Session.Engaged(),
          "committing closes the session while keeping the moved geometry in place");
}

void ProveCurveSlideAndNumeric()
{
    std::printf("\n4. Curve slide and numeric override move along one true 3D direction\n");

    WorldSketchStructure Sketch;
    const WorldPlacementFrame Support = {{}, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};
    const WorldCurveName Diagonal = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }, Support);

    WorldPick Pick = {};
    Pick.Subject = WorldPickSubject::Curve;
    Pick.Curve = Diagonal;
    ResolveWorldCurvePivot(Sketch, Diagonal, Pick.Position);

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ResolvedCamera Camera = ResolveFreeCamera({ -220.0, 120.0, -220.0 }, 45.0, -20.0, 60.0, false, 3.0);

    const SpatialDirection Slide = ResolveWorldCurveSlideDirection(Sketch, Diagonal, Pick.Position);
    float StartX = 0.0f;
    float StartY = 0.0f;
    float EndX = 0.0f;
    float EndY = 0.0f;
    Claim(ProjectFromCamera(Camera, Extent, Pick.Position, StartX, StartY),
          "the diagonal curve pivot projects for sliding");
    Claim(ProjectFromCamera(Camera, Extent, Added(Pick.Position, Scaled(Slide, 25.0)), EndX, EndY),
          "and a point twenty-five units along the curve direction projects too");

    WorldSketchTransformSession Session;
    Claim(StartWorldSketchTransformSession(Sketch, Camera, Extent, StartX, StartY, Pick,
                                          TransformRestriction::Curve, true, Session),
          "a curve-slide world transform starts");
    UpdateWorldSketchTransformSession(Camera, Extent, EndX, EndY, Sketch, Session);

    const DeclaredWorldCurve* Held = Sketch.Resolve(Diagonal);
    Claim(Held != nullptr
       && SamePoint(Held->Geometry.HeldLine().Origin,
                    Added(SpatialPoint{ 0.0, 0.0, 0.0 }, Scaled(Slide, 25.0)))
       && SamePoint(Held->Geometry.HeldLine().Terminus,
                    Added(SpatialPoint{ 100.0, 0.0, 100.0 }, Scaled(Slide, 25.0))),
          "sliding a curve moves it only along its own tangent direction");

    CancelWorldSketchTransformSession(Sketch, Session);
    Claim(StartWorldSketchTransformSession(Sketch, Camera, Extent, StartX, StartY, Pick,
                                          TransformRestriction::AxisY, false, Session),
          "a vertical numeric move can start too");
    AppendTransformNumericRun(Session.Standing.Numeric, TransformNumericLimit, "15");
    UpdateWorldSketchTransformSession(Camera, Extent, StartX, StartY, Sketch, Session);
    Held = Sketch.Resolve(Diagonal);
    Claim(Held != nullptr && SamePoint(Held->Geometry.HeldLine().Origin, { 0.0, 15.0, 0.0 })
                         && SamePoint(Held->Geometry.HeldLine().Terminus, { 100.0, 15.0, 100.0 }),
          "numeric override on AxisY performs a true 3D vertical move without needing pointer travel");

    // 🔴 A TYPED DISTANCE WITHOUT AN AXIS USED TO GO TO WORLD X. `Offset` was written as
    //    `{ Numeric, 0, 0 }` for an unrestricted move, so `G 30` slid the selection thirty along an axis
    //    the artist never named — the behaviour that read as "a plain grab travels parallel to the
    //    camera", X being simply what the default view faces. Blender's reading is the correct one: a
    //    distance needs a direction, so `G 30` waits and `G Z 30` moves.
    CancelWorldSketchTransformSession(Sketch, Session);
    Claim(StartWorldSketchTransformSession(Sketch, Camera, Extent, StartX, StartY, Pick,
                                          TransformRestriction::Free, false, Session),
          "an unrestricted move starts");
    AppendTransformNumericRun(Session.Standing.Numeric, TransformNumericLimit, "30");
    UpdateWorldSketchTransformSession(Camera, Extent, StartX, StartY, Sketch, Session);
    Held = Sketch.Resolve(Diagonal);
    Claim(Held != nullptr && SamePoint(Held->Geometry.HeldLine().Origin, { 0.0, 0.0, 0.0 })
                         && SamePoint(Held->Geometry.HeldLine().Terminus, { 100.0, 0.0, 100.0 }),
          "G 30 with no axis named moves NOTHING -- it does not run away down world X");
    Claim(!Session.Changed,
          "and the session reports no change, so a confirm would seal nothing");

    // Naming the axis afterwards applies the distance already typed.
    CancelWorldSketchTransformSession(Sketch, Session);
    Claim(StartWorldSketchTransformSession(Sketch, Camera, Extent, StartX, StartY, Pick,
                                          TransformRestriction::AxisZ, false, Session),
          "and G Z 30 starts on an axis");
    AppendTransformNumericRun(Session.Standing.Numeric, TransformNumericLimit, "30");
    UpdateWorldSketchTransformSession(Camera, Extent, StartX, StartY, Sketch, Session);
    Held = Sketch.Resolve(Diagonal);
    Claim(Held != nullptr && SamePoint(Held->Geometry.HeldLine().Origin, { 0.0, 0.0, 30.0 })
                         && SamePoint(Held->Geometry.HeldLine().Terminus, { 100.0, 0.0, 130.0 }),
          "G Z 30 moves thirty along Z -- the distance applies once a direction exists");
}

void ProveRepeatedStaleVertexMove()
{
    std::printf("\n5. Reusing the same selected vertex still deforms the current closed shape\n");

    Bench Stage;
    const WorldPick Vertex = Stage.PointPick();
    Claim(Vertex.Subject == WorldPickSubject::Point,
          "the bench offers a vertex pick to reuse");

    float StartX = 0.0f;
    float StartY = 0.0f;
    float FirstEndX = 0.0f;
    float FirstEndY = 0.0f;
    Claim(ProjectFromCamera(Stage.FrontPerspective, Stage.Extent, Vertex.Position, StartX, StartY),
          "the selected vertex projects for the first drag");
    Claim(ProjectFromCamera(Stage.FrontPerspective, Stage.Extent, { 120.0, 120.0, 40.0 }, FirstEndX, FirstEndY),
          "and the first target position projects too");

    WorldSketchTransformSession First;
    Claim(StartWorldSketchTransformSession(Stage.Sketch, Stage.FrontPerspective, Stage.Extent,
                                          StartX, StartY, Vertex,
                                          TransformRestriction::Free, false, First),
          "the first vertex drag starts");
    UpdateWorldSketchTransformSession(Stage.FrontPerspective, Stage.Extent, FirstEndX, FirstEndY,
                                     Stage.Sketch, First);
    CommitWorldSketchTransformSession(First);

    float SecondEndX = 0.0f;
    float SecondEndY = 0.0f;
    Claim(ProjectFromCamera(Stage.FrontPerspective, Stage.Extent, { 135.0, 110.0, 40.0 }, SecondEndX, SecondEndY),
          "the second target for the same stale selection also projects");

    WorldSketchTransformSession Second;
    Claim(StartWorldSketchTransformSession(Stage.Sketch, Stage.FrontPerspective, Stage.Extent,
                                          FirstEndX, FirstEndY, Vertex,
                                          TransformRestriction::Free, false, Second),
          "the second drag can start from the same original point pick after the first commit");
    Claim(Second.Placements.size() >= 2u,
          "and it still resolves both incident endpoints at the moved corner");
    UpdateWorldSketchTransformSession(Stage.FrontPerspective, Stage.Extent, SecondEndX, SecondEndY,
                                     Stage.Sketch, Second);

    const DeclaredWorldCurve* HeldBC = Stage.Sketch.Resolve(Stage.BC);
    const DeclaredWorldCurve* HeldCD = Stage.Sketch.Resolve(Stage.CD);
    Claim(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 135.0, 110.0, 40.0 }),
          "after the second drag the selected corner lands at the new target");
    Claim(HeldCD != nullptr && SamePoint(HeldCD->Geometry.HeldLine().Origin, { 135.0, 110.0, 40.0 }),
          "and the neighbouring edge still shares that same moved corner instead of splitting away");
}

//----------------------------------------------------------------------------------------------------
// 🔴 `G Y 30` MUST LIFT A FILLED LOOP OFF ITS PLANE, AND THE FILL MUST WITHDRAW ITSELF.
//----------------------------------------------------------------------------------------------------
// The whole chain is walked here rather than any one link of it: the artist's keystrokes through
// `ResolveTransformCommand`, the restriction they name into `StartWorldSketchTransformSession`, the
// geometry the session writes, and the face eligibility `AnalyzeWorldSketch` reports afterwards.
//
// 🔴 The keyboard gate refused `y` for `Move`, so the G was read, the Y was dropped, the 30 was spent on
//    nothing and the shape did not move at all. Every piece behind the gate was already in place --
//    `ResolveAxisDirection` answers `Basis.Normal` for `AxisY` and `ResolveWorldOffset` projects onto it.
//
// 📝 And nothing has to switch the fill off by hand. A loop lifted clean off its plane stays coplanar and
//    stays fillable; it is a loop lifted UNEVENLY that stops being a face, and `AnalyzeWorldSketch`
//    withdraws the eligibility itself by measuring the deviation. Both halves are claimed below.
void ProveTypedVerticalMoveLiftsAndDropsTheFill()
{
    std::printf("\n6. G Y 30 lifts the loop off its plane, and the fill withdraws itself\n");

    // 📝 A square laid flat on the ground, because `AxisY` is the ACTIVE WORKPLANE's normal rather than a
    //    world letter -- `ResolveCameraAxisDirection` answers `Camera.Basis.Normal`, which is world Y for
    //    the ground. A square standing on the Front frame would have slid within its own plane instead,
    //    which is why `Bench`'s square is not reused here.
    Bench Stage;
    const WorldPlacementFrame Ground = {{ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }};

    WorldSketchStructure Sketch;
    const WorldCurveName AB = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Ground);
    const WorldCurveName BC = Sketch.DeclareLine({ 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }, Ground);
    const WorldCurveName CD = Sketch.DeclareLine({ 100.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 }, Ground);
    const WorldCurveName DA = Sketch.DeclareLine({ 0.0, 0.0, 100.0 }, { 0.0, 0.0, 0.0 }, Ground);
    const WorldLoopName Loop = Sketch.DeclareLoop({ { { AB, true }, { BC, true },
                                                      { CD, true }, { DA, true } } });

    const auto FillStandsFor = [&Sketch, &Loop](bool& Closed, bool& Filled)
    {
        Closed = false;
        Filled = false;
        const WorldSketchAnalysis Report = AnalyzeWorldSketch(Sketch);
        for (const WorldLoopAnalysisRecord& Held : Report.Loops)
            if (Held.Loop.IssuedIndex == Loop.IssuedIndex)
            {
                Closed = Held.Closed;
                Filled = Held.FillEligible;
            }
    };

    bool Closed = false;
    bool Filled = false;
    FillStandsFor(Closed, Filled);
    Claim(Closed && Filled, "the square starts closed, planar and fillable");

    // ① The artist types G, then Y, then 30. The grammar must carry all three.
    //    🔴 It used to carry only the G. `y` was accepted for `Rotate` alone, so the axis letter was
    //       dropped and the 30 was spent on a move with nothing to move along.
    const TransformCommandIntake Started =
        ResolveTransformCommand("g", 1u, false, TransformManner::Move);
    Claim(Started.StartRequested && Started.StartManner == TransformManner::Move, "G starts a move");

    const TransformCommandIntake Axis = ResolveTransformCommand("y", 1u, true, TransformManner::Move);
    Claim(Axis.RestrictionRequested && Axis.Restriction == TransformRestriction::AxisY,
          "Y names the plane normal for a MOVE, rather than being dropped as it was");

    const TransformCommandIntake Amount = ResolveTransformCommand("30", 2u, true, TransformManner::Move);
    Claim(std::string(Amount.NumericAppend) == "30", "and the 30 survives the axis letter");

    // ② That restriction drives a real session, with no pointer travel at all.
    WorldPick Pick = {};
    Pick.Subject = WorldPickSubject::Loop;
    Pick.Loop = Loop;
    Claim(ResolveWorldLoopPivot(Sketch, Loop, Pick.Position), "the lifted loop has a pivot to move about");

    float StartX = 0.0f;
    float StartY = 0.0f;
    Claim(ProjectFromCamera(Stage.OrthoView, Stage.Extent, Pick.Position, StartX, StartY),
          "the loop pivot projects for the start of the typed move");

    WorldSketchTransformSession Session;
    Claim(StartWorldSketchTransformSession(Sketch, Stage.OrthoView, Stage.Extent,
                                           StartX, StartY, Pick,
                                           Axis.Restriction, false, Session),
          "the typed axis starts a session along the plane normal");
    Claim(SameDirection(Session.AxisDirection, { 0.0, 1.0, 0.0 }),
          "and that session travels along the workplane normal, not a screen direction");

    AppendTransformNumericRun(Session.Standing.Numeric, TransformNumericLimit, Amount.NumericAppend);
    UpdateWorldSketchTransformSession(Stage.OrthoView, Stage.Extent, StartX, StartY, Sketch, Session);

    const DeclaredWorldCurve* HeldAB = Sketch.Resolve(AB);
    const DeclaredWorldCurve* HeldCD = Sketch.Resolve(CD);
    Claim(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Origin, { 0.0, 30.0, 0.0 })
       && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { 100.0, 30.0, 0.0 }),
          "G Y 30 lifts the loop thirty off its plane instead of doing nothing at all");
    Claim(HeldCD != nullptr && SamePoint(HeldCD->Geometry.HeldLine().Origin, { 100.0, 30.0, 100.0 })
       && SamePoint(HeldCD->Geometry.HeldLine().Terminus, { 0.0, 30.0, 100.0 }),
          "and the far edge travels with it, so the loop stays rigid");

    // ③ Lifted flat, it is still a face. This is the half that must KEEP its fill: the gate was never
    //    about the axis, so refusing the axis to protect the fill would have been the wrong guard.
    FillStandsFor(Closed, Filled);
    Claim(Closed && Filled, "a rigid lift keeps the loop planar, so the fill correctly stays");

    // ④ Now lift ONE corner. The loop stays closed, and the fill withdraws itself with no code asking it
    //    to -- `AnalyzeWorldSketch` re-measures coplanarity on every pass.
    std::vector<WorldPointPlacement> Points;
    Claim(ResolveWorldSketchPoints(Sketch, BC, Points) && Points.size() == 2u,
          "the corner to skew is an endpoint of one edge");
    Claim(!!MoveWorldSketchPoint(Sketch, Points[1u].Name, { 0.0, 45.0, 0.0 }),
          "lifting a single corner clear of the lifted plane succeeds");

    FillStandsFor(Closed, Filled);
    Claim(Closed, "the skewed loop is still one closed loop");
    Claim(!Filled, "and the fill withdraws itself, because a non-planar loop cannot be a flat face");

    // ⑤ And it comes back. The withdrawal is a measurement, not a latch.
    // 📝 The point name is re-resolved rather than reused: a move re-declares the placement, so the old
    //    name is stale by now. Section 5 above exists because of exactly that.
    std::vector<WorldPointPlacement> Again;
    Claim(ResolveWorldSketchPoints(Sketch, BC, Again) && Again.size() == 2u,
          "the skewed corner can be found again");
    Claim(!!MoveWorldSketchPoint(Sketch, Again[1u].Name, { 0.0, -45.0, 0.0 }),
          "the skewed corner returns to the lifted plane");
    FillStandsFor(Closed, Filled);
    Claim(Closed && Filled, "and the fill returns with it, rather than staying lost");
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("WORLD SKETCH TRANSFORM SESSION PROOF\n");
    std::printf("=========================================================================\n");

    ProvePlacementResolution();
    ProvePerspectiveScreenMove();
    ProveAxisLockedZMove();
    ProveCurveSlideAndNumeric();
    ProveRepeatedStaleVertexMove();
    ProveTypedVerticalMoveLiftsAndDropsTheFill();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
