// 🧩 Phase-3 proof for picking on the world-space sketch.

#include "SlateWorkspace/Discipline/WorldSketchPicking/Api/WorldSketchScreenPicking.h"

#include <cmath>
#include <cstdio>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Require(bool Held, const char* Claim)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Claim);
    }
}

} // namespace

int main()
{
    std::printf("[WorldSketchPickingProof] world-space picking resolves vertices, edges and planar faces in 3D\n");

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ResolvedCamera Camera = ResolveFreeCamera({ 0.0, 50.0, -300.0 }, 0.0, 0.0, 60.0, true, 1.0);
    const WorldPlacementFrame Front = { {}, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
    const WorldPlacementFrame Side  = { { 200.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } };

    WorldSketchStructure Sketch;

    const WorldCurveName FrontLine = Sketch.DeclareLine({ -40.0,  0.0, 0.0 }, { -40.0, 100.0, 0.0 }, Front);
    const WorldCurveName BackLine  = Sketch.DeclareLine({ -40.0,  0.0, 80.0 }, { -40.0, 100.0, 80.0 }, Front);

    const WorldCurveName Bezier = Sketch.DeclareBezier(
        { { 40.0, 0.0, 0.0 }, { 70.0, 30.0, 0.0 }, { 80.0, 70.0, 0.0 }, { 40.0, 100.0, 0.0 } }, Front);

    const WorldCurveName AB = Sketch.DeclareLine({   0.0,   0.0, 0.0 }, { 100.0,   0.0, 0.0 }, Front);
    const WorldCurveName BC = Sketch.DeclareLine({ 100.0,   0.0, 0.0 }, { 100.0, 100.0, 0.0 }, Front);
    const WorldCurveName CD = Sketch.DeclareLine({ 100.0, 100.0, 0.0 }, {   0.0, 100.0, 0.0 }, Front);
    const WorldCurveName DA = Sketch.DeclareLine({   0.0, 100.0, 0.0 }, {   0.0,   0.0, 0.0 }, Front);
    const WorldLoopName FrontLoop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });

    const WorldCurveName EF = Sketch.DeclareLine({ 200.0,   0.0,   0.0 }, { 200.0, 100.0,   0.0 }, Side);
    const WorldCurveName FG = Sketch.DeclareLine({ 200.0, 100.0,   0.0 }, { 200.0, 100.0, 100.0 }, Side);
    const WorldCurveName GH = Sketch.DeclareLine({ 200.0, 100.0, 100.0 }, { 200.0,   0.0, 100.0 }, Side);
    const WorldCurveName HE = Sketch.DeclareLine({ 200.0,   0.0, 100.0 }, { 200.0,   0.0,   0.0 }, Side);
    const WorldLoopName SideLoop = Sketch.DeclareLoop({ { { EF, true }, { FG, true }, { GH, true }, { HE, true } } });

    //--------------------------------------------------------------------------------------------
    // 🔴 A POINT WINS OVER THE CURVE PASSING THROUGH IT, just as before, but now without needing one
    //    global sketch plane to probe against.
    //--------------------------------------------------------------------------------------------
    {
        float X = 0.0f;
        float Y = 0.0f;
        Require(ProjectFromCamera(Camera, Extent, { -40.0, 0.0, 0.0 }, X, Y),
                "the front line endpoint must project for picking");

        WorldPick Pick = {};
        Require(ResolveWorldSketchPick(Sketch, Camera, Extent, X, Y, 8.0, Pick),
                "pointing at a world-space endpoint must pick something");
        Require(Pick.Subject == WorldPickSubject::Point,
                "and it must pick the endpoint before the curve that passes through it");
        Require(Pick.Curve.IssuedIndex == FrontLine.IssuedIndex,
                "the picked point must know which world curve it belongs to");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 WHEN TWO CURVES OVERLAP ON SCREEN, THE NEARER WORLD CURVE MUST WIN.
    //--------------------------------------------------------------------------------------------
    {
        const ResolvedCamera OrthoCamera = ResolveFreeCamera({ 0.0, 50.0, -300.0 }, 0.0, 0.0, 60.0, false, 2.0);
        float X = 0.0f;
        float Y = 0.0f;
        float BackX = 0.0f;
        float BackY = 0.0f;
        Require(ProjectFromCamera(OrthoCamera, Extent, { -40.0, 50.0, 0.0 }, X, Y)
             && ProjectFromCamera(OrthoCamera, Extent, { -40.0, 50.0, 80.0 }, BackX, BackY),
                "the overlapping front and back lines must project for comparison");
        Require(std::fabs(X - BackX) < 1.0e-4f && std::fabs(Y - BackY) < 1.0e-4f,
                "and they must share one screen point while standing at different depths");

        WorldPick Pick = {};
        Require(ResolveWorldSketchPickForElement(Sketch, OrthoCamera, Extent, X, Y, 8.0,
                                                SelectionElement::Edge, Pick),
                "pointing at the overlapping curves in edge mode must pick a curve");
        Require(Pick.Subject == WorldPickSubject::Curve,
                "edge mode must return a curve pick");
        Require(Pick.Curve.IssuedIndex == FrontLine.IssuedIndex,
                "and it must be the nearer front curve, not the one behind it");
        Require(Pick.Curve.IssuedIndex != BackLine.IssuedIndex,
                "the farther overlapping back curve must lose to the nearer one");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 CONTROL HANDLES ARE VERTICES TOO. A Bezier control point must be reachable in vertex mode.
    //--------------------------------------------------------------------------------------------
    {
        std::vector<WorldControlPlacement> Controls;
        Require(ResolveWorldSketchControls(Sketch, Bezier, Controls) && Controls.size() == 4u,
                "the Bezier must expose its four control handles");

        float X = 0.0f;
        float Y = 0.0f;
        Require(!Controls.empty() && ProjectFromCamera(Camera, Extent, Controls[1u].Position, X, Y),
                "a Bezier control handle must project for picking");

        WorldPick Pick = {};
        Require(ResolveWorldSketchPickForElement(Sketch, Camera, Extent, X, Y, 8.0,
                                                SelectionElement::Vertex, Pick),
                "pointing at a Bezier control handle in vertex mode must pick something");
        Require(Pick.Subject == WorldPickSubject::Control,
                "and it must return the control handle, not the curve behind it");
        Require(Pick.Curve.IssuedIndex == Bezier.IssuedIndex,
                "the picked control must belong to the Bezier it shapes");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 FACE MODE PICKS THE PLANAR REGION THROUGH ITS AREA, not only by grazing an edge.
    //--------------------------------------------------------------------------------------------
    {
        float X = 0.0f;
        float Y = 0.0f;
        Require(ProjectFromCamera(Camera, Extent, { 50.0, 50.0, 0.0 }, X, Y),
                "the centre of the front loop must project");

        WorldPick Pick = {};
        Require(ResolveWorldSketchPickForElement(Sketch, Camera, Extent, X, Y, 8.0,
                                                SelectionElement::Face, Pick),
                "pointing inside a planar world loop in face mode must pick something");
        Require(Pick.Subject == WorldPickSubject::Loop && Pick.Loop.IssuedIndex == FrontLoop.IssuedIndex,
                "and it must pick the loop the pointer is inside");

        Require(ProjectFromCamera(Camera, Extent, { 200.0, 50.0, 50.0 }, X, Y),
                "the side loop centre must project too");
        Require(ResolveWorldSketchPickForElement(Sketch, Camera, Extent, X, Y, 8.0,
                                                SelectionElement::Face, Pick),
                "a loop on another plane must also be face-pickable through its area");
        Require(Pick.Loop.IssuedIndex == SideLoop.IssuedIndex,
                "and it must resolve to the correct other-plane loop");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 OBJECT MODE RETURNS THE WHOLE LOOP EVEN WHEN THE POINTER IS ON ITS EDGE.
    //--------------------------------------------------------------------------------------------
    {
        float X = 0.0f;
        float Y = 0.0f;
        Require(ProjectFromCamera(Camera, Extent, { 50.0, 0.0, 0.0 }, X, Y),
                "the front loop boundary point must project");

        WorldPick Pick = {};
        Require(ResolveWorldSketchPickForElement(Sketch, Camera, Extent, X, Y, 8.0,
                                                SelectionElement::Object, Pick),
                "object mode on a loop edge must still pick something");
        Require(Pick.Subject == WorldPickSubject::Loop && Pick.Loop.IssuedIndex == FrontLoop.IssuedIndex,
                "and it must return the whole loop rather than only the edge under the pointer");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 ONCE A CLOSED LOOP LEAVES ITS PLANE, FACE PICKING MUST REFUSE IT while edge picking keeps the
    //    same geometry reachable.
    //--------------------------------------------------------------------------------------------
    {
        DeclaredWorldCurve* HeldBC = Sketch.Resolve(BC);
        DeclaredWorldCurve* HeldCD = Sketch.Resolve(CD);
        Require(HeldBC != nullptr && HeldCD != nullptr,
                "the loop's front edges must survive into editing");
        if (HeldBC != nullptr)
            HeldBC->Geometry.HeldLine().Terminus.Forward = 25.0;
        if (HeldCD != nullptr)
            HeldCD->Geometry.HeldLine().Origin.Forward = 25.0;

        float X = 0.0f;
        float Y = 0.0f;
        Require(ProjectFromCamera(Camera, Extent, { 50.0, 50.0, 12.5 }, X, Y),
                "the edited loop centre must still project");

        WorldPick Pick = {};
        Require(!ResolveWorldSketchPickForElement(Sketch, Camera, Extent, X, Y, 8.0,
                                                 SelectionElement::Face, Pick),
                "a non-planar closed loop must stop answering as a face");

        Require(ProjectFromCamera(Camera, Extent, { 100.0, 50.0, 12.5 }, X, Y),
                "one of its edited edges must still project");
        Require(ResolveWorldSketchPickForElement(Sketch, Camera, Extent, X, Y, 8.0,
                                                SelectionElement::Edge, Pick),
                "but the edited non-planar geometry must remain edge-pickable");
        Require(Pick.Subject == WorldPickSubject::Curve
             && (Pick.Curve.IssuedIndex == BC.IssuedIndex || Pick.Curve.IssuedIndex == CD.IssuedIndex),
                "the same edited front edges must still be returned as curves");
    }

    std::printf("[WorldSketchPickingProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
