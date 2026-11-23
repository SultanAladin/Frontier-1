// 🧩 Phase-4 proof for true 3D deformation on the world-space sketch.

#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateShape/World/WorldSketchEditing/Api/WorldSketchEditing.h"

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

bool SamePoint(const SpatialPoint& Left,
               const SpatialPoint& Right,
               double Tolerance = 1.0e-9)
{
    return LengthSquared(Difference(Left, Right)) <= Tolerance * Tolerance;
}

const WorldLoopAnalysisRecord* ResolveLoop(const WorldSketchAnalysis& Analysis,
                                           WorldLoopName Name)
{
    for (const WorldLoopAnalysisRecord& Loop : Analysis.Loops)
        if (Loop.Loop.IssuedIndex == Name.IssuedIndex)
            return &Loop;
    return nullptr;
}

} // namespace

int main()
{
    std::printf("[WorldSketchEditingProof] world-space transforms deform the same closed shape in true 3D\n");

    const WorldPlacementFrame Front = {{ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }};

    //----------------------------------------------------------------------------------------------------
    // 🔴 MOVING ONE EDGE MUST DRAG THE CONNECTED CORNERS, NOT SPLIT THE PROFILE OPEN.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldCurveName AB = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Front);
        const WorldCurveName BC = Sketch.DeclareLine({ 100.0, 0.0, 0.0 }, { 100.0, 100.0, 0.0 }, Front);
        const WorldCurveName CD = Sketch.DeclareLine({ 100.0, 100.0, 0.0 }, { 0.0, 100.0, 0.0 }, Front);
        const WorldCurveName DA = Sketch.DeclareLine({ 0.0, 100.0, 0.0 }, { 0.0, 0.0, 0.0 }, Front);
        const WorldLoopName Loop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });

        Require(!!MoveWorldSketchCurve(Sketch, BC, { 20.0, 0.0, 0.0 }),
                "moving one selected edge must succeed");
        Require(Sketch.CurveCount() == 4u,
                "and it must not create duplicate curves while moving that edge");

        const DeclaredWorldCurve* HeldAB = Sketch.Resolve(AB);
        const DeclaredWorldCurve* HeldBC = Sketch.Resolve(BC);
        const DeclaredWorldCurve* HeldCD = Sketch.Resolve(CD);
        const DeclaredWorldCurve* HeldDA = Sketch.Resolve(DA);
        Require(HeldAB != nullptr && HeldBC != nullptr && HeldCD != nullptr && HeldDA != nullptr,
                "the rectangle curves must still exist after the deformation");
        Require(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { 120.0, 0.0, 0.0 }),
                "the lower neighbour must follow the moved edge at the shared corner");
        Require(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 120.0, 0.0, 0.0 })
             && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 120.0, 100.0, 0.0 }),
                "the selected edge itself must slide as one existing curve");
        Require(HeldCD != nullptr && SamePoint(HeldCD->Geometry.HeldLine().Origin, { 120.0, 100.0, 0.0 }),
                "the upper neighbour must follow the moved edge at the other shared corner");
        Require(HeldDA != nullptr && SamePoint(HeldDA->Geometry.HeldLine().Origin, { 0.0, 100.0, 0.0 })
             && SamePoint(HeldDA->Geometry.HeldLine().Terminus, { 0.0, 0.0, 0.0 }),
                "the opposite edge must stay where it was");

        const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch);
        const WorldLoopAnalysisRecord* Record = ResolveLoop(Analysis, Loop);
        Require(Record != nullptr && Record->Closed,
                "the deformed four-edge shape must stay a closed loop");
        Require(Record != nullptr && Record->FillEligible,
                "and staying planar means it must remain face-eligible too");
    }

    //----------------------------------------------------------------------------------------------------
    // 🔴 LIFTING ONE CORNER INTO Z MUST KEEP THE LOOP CLOSED BUT DROP ONLY ITS FILL ELIGIBILITY.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldCurveName AB = Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, Front);
        const WorldCurveName BC = Sketch.DeclareLine({ 100.0, 0.0, 0.0 }, { 100.0, 100.0, 0.0 }, Front);
        const WorldCurveName CD = Sketch.DeclareLine({ 100.0, 100.0, 0.0 }, { 0.0, 100.0, 0.0 }, Front);
        const WorldCurveName DA = Sketch.DeclareLine({ 0.0, 100.0, 0.0 }, { 0.0, 0.0, 0.0 }, Front);
        const WorldLoopName Loop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });

        std::vector<WorldPointPlacement> Points;
        Require(ResolveWorldSketchPoints(Sketch, BC, Points) && Points.size() == 2u,
                "the lifted corner must be one of the selected edge endpoints");
        Require(!!MoveWorldSketchPoint(Sketch, Points[1u].Name, { 0.0, 0.0, 25.0 }),
                "lifting one shared vertex into world Z must succeed");

        const DeclaredWorldCurve* HeldBC = Sketch.Resolve(BC);
        const DeclaredWorldCurve* HeldCD = Sketch.Resolve(CD);
        Require(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 100.0, 100.0, 25.0 }),
                "the selected edge endpoint must move in Z");
        Require(HeldCD != nullptr && SamePoint(HeldCD->Geometry.HeldLine().Origin, { 100.0, 100.0, 25.0 }),
                "the neighbouring edge must share the same lifted corner rather than tearing apart");

        const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch);
        const WorldLoopAnalysisRecord* Record = ResolveLoop(Analysis, Loop);
        Require(Record != nullptr && Record->Closed,
                "the lifted shape must still be recognised as one closed world loop");
        Require(Record != nullptr && !Record->FillEligible,
                "but once one corner leaves plane it must stop pretending to be a planar face");
    }

    //----------------------------------------------------------------------------------------------------
    // 🔴 MOVING A WHOLE LOOP IN Z MUST BE A TRUE 3D OBJECT MOVE, NOT A PLANAR SPECIAL CASE.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldCurveName AB = Sketch.DeclareLine({ -50.0, -50.0, 10.0 }, {  50.0, -50.0, 10.0 }, Front);
        const WorldCurveName BC = Sketch.DeclareLine({  50.0, -50.0, 10.0 }, {  50.0,  50.0, 10.0 }, Front);
        const WorldCurveName CD = Sketch.DeclareLine({  50.0,  50.0, 10.0 }, { -50.0,  50.0, 10.0 }, Front);
        const WorldCurveName DA = Sketch.DeclareLine({ -50.0,  50.0, 10.0 }, { -50.0, -50.0, 10.0 }, Front);
        const WorldLoopName Loop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });

        WorldPick Pick = {};
        Pick.Subject = WorldPickSubject::Loop;
        Pick.Loop = Loop;
        Require(!!MoveWorldSketchPick(Sketch, Pick, { 0.0, 0.0, 60.0 }),
                "moving a picked whole loop along Z must succeed");

        const DeclaredWorldCurve* HeldAB = Sketch.Resolve(AB);
        const DeclaredWorldCurve* HeldBC = Sketch.Resolve(BC);
        Require(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Origin, { -50.0, -50.0, 70.0 })
             && SamePoint(HeldAB->Geometry.HeldLine().Terminus, { 50.0, -50.0, 70.0 }),
                "the first edge of the whole-loop move must land at the new Z height");
        Require(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, { 50.0, -50.0, 70.0 })
             && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 50.0, 50.0, 70.0 }),
                "and the rest of the loop must move rigidly with it");

        const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch);
        const WorldLoopAnalysisRecord* Record = ResolveLoop(Analysis, Loop);
        Require(Record != nullptr && Record->Closed && Record->FillEligible,
                "a rigid object move in Z must keep the loop closed and still planar/fillable");
    }

    //----------------------------------------------------------------------------------------------------
    // 🔴 A PICKED CONTROL HANDLE MUST MOVE THE EXISTING CURVE, NOT ANY SHARED ENDPOINTS AROUND IT.
    //----------------------------------------------------------------------------------------------------
    {
        WorldSketchStructure Sketch;
        const WorldCurveName Bezier = Sketch.DeclareBezier({
            { 0.0, 0.0, 0.0 },
            { 40.0, 20.0, 0.0 },
            { 60.0, 80.0, 0.0 },
            { 100.0, 100.0, 0.0 }
        }, Front);

        std::vector<WorldControlPlacement> Controls;
        Require(ResolveWorldSketchControls(Sketch, Bezier, Controls) && Controls.size() == 4u,
                "the Bezier must expose its control handles for deformation");

        WorldPick Pick = {};
        Pick.Subject = WorldPickSubject::Control;
        Pick.Control = Controls[1u].Name;
        Pick.Curve = Bezier;
        Require(!!MoveWorldSketchPick(Sketch, Pick, { 0.0, 0.0, 30.0 }),
                "moving a picked Bezier control in Z must succeed");

        const DeclaredWorldCurve* Held = Sketch.Resolve(Bezier);
        Require(Held != nullptr && Held->Geometry.HeldBezier().ControlPoints.size() == 4u,
                "the Bezier must still be the same curve after the control edit");
        Require(Held != nullptr && SamePoint(Held->Geometry.HeldBezier().ControlPoints[1u], { 40.0, 20.0, 30.0 }),
                "the chosen control handle must move to the requested 3D position");
        Require(Held != nullptr && SamePoint(Held->Geometry.HeldBezier().ControlPoints.front(), { 0.0, 0.0, 0.0 })
             && SamePoint(Held->Geometry.HeldBezier().ControlPoints.back(), { 100.0, 100.0, 0.0 }),
                "while the untouched Bezier endpoints must stay where they started");
    }

    std::printf("[WorldSketchEditingProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
