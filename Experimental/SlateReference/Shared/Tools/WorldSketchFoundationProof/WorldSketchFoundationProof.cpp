// 🧩 Phase-1 proof for the world-space sketch replacement foundation.

#include "SlateShape/Operation/ExtrusionSpecification/Api/ExtrusionSpecification.h"
#include "SlateShape/World/WorldSketchAnalysis/Api/WorldSketchAnalysis.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cmath>
#include <cstdio>
#include <vector>

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

const WorldLoopAnalysisRecord* ResolveLoop(const WorldSketchAnalysis& Analysis,
                                           WorldLoopName Subject)
{
    for (const WorldLoopAnalysisRecord& Loop : Analysis.Loops)
        if (Loop.Loop.IssuedIndex == Subject.IssuedIndex)
            return &Loop;
    return nullptr;
}

bool SamePoint(const SpatialPoint& Left,
               const SpatialPoint& Right,
               double Tolerance = 1.0e-9)
{
    return LengthSquared(Difference(Left, Right)) <= Tolerance * Tolerance;
}

} // namespace

int main()
{
    std::printf("[WorldSketchFoundationProof] world-space authoring replaces the one-plane sketch basis\n");

    //--------------------------------------------------------------------------------------------
    // 🔴 A WORKPLANE IS NOW A PLACEMENT FRAME, NOT THE OWNER OF ALL GEOMETRY.
    //--------------------------------------------------------------------------------------------
    {
        const WorldPlacementFrame Front = { { 10.0, 20.0, 30.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
        Require(Front.Declared(), "a front-facing placement frame must declare");

        const SpatialPoint OnPlane = ResolveWorldPlacementPosition(Front, 25.0, -10.0);
        Require(SamePoint(OnPlane, { 35.0, 10.0, 30.0 }),
                "placement coordinates must resolve back into world space");

        double Along = 0.0;
        double Across = 0.0;
        ResolveWorldPlacementCoordinates(Front, OnPlane, Along, Across);
        Require(std::fabs(Along - 25.0) < 1.0e-9 && std::fabs(Across + 10.0) < 1.0e-9,
                "and the same world point must round-trip back to its placement coordinates");

        const SpatialPoint OffPlane = { 35.0, 10.0, 70.0 };
        Require(std::fabs(ResolveWorldPlacementOffset(Front, OffPlane) - 40.0) < 1.0e-9,
                "the placement frame must measure signed distance off its plane");
        Require(SamePoint(ResolveWorldPlacementProjection(Front, OffPlane), OnPlane),
                "projection must drop a world point back onto the placement plane");

        SpatialPoint Hit = {};
        Require(ResolveWorldPlacementIntersection(Front,
                                                  { 35.0, 10.0, -100.0 },
                                                  { 0.0, 0.0, 1.0 }, Hit),
                "a view ray pointing through the frame must intersect it");
        Require(SamePoint(Hit, OnPlane),
                "and it must intersect at the world point those coordinates name");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 TWO LOOPS ON TWO DIFFERENT PLANES MUST COEXIST IN ONE DOCUMENT.
    //--------------------------------------------------------------------------------------------
    WorldSketchStructure Sketch;
    const WorldPlacementFrame Front = { {}, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };   // XY
    const WorldPlacementFrame Side  = { { 200.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } }; // YZ

    const SpatialPoint A = {   0.0,   0.0, 0.0 };
    const SpatialPoint B = { 100.0,   0.0, 0.0 };
    const SpatialPoint C = { 100.0, 100.0, 0.0 };
    const SpatialPoint D = {   0.0, 100.0, 0.0 };

    const SpatialPoint E = { 200.0,   0.0,   0.0 };
    const SpatialPoint F = { 200.0, 100.0,   0.0 };
    const SpatialPoint G = { 200.0, 100.0, 100.0 };
    const SpatialPoint H = { 200.0,   0.0, 100.0 };

    const WorldCurveName AB = Sketch.DeclareLine(A, B, Front);
    const WorldCurveName BC = Sketch.DeclareLine(B, C, Front);
    const WorldCurveName CD = Sketch.DeclareLine(C, D, Front);
    const WorldCurveName DA = Sketch.DeclareLine(D, A, Front);
    const WorldLoopName FrontLoop = Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } });

    const WorldCurveName EF = Sketch.DeclareLine(E, F, Side);
    const WorldCurveName FG = Sketch.DeclareLine(F, G, Side);
    const WorldCurveName GH = Sketch.DeclareLine(G, H, Side);
    const WorldCurveName HE = Sketch.DeclareLine(H, E, Side);
    const WorldLoopName SideLoop = Sketch.DeclareLoop({ { { EF, true }, { FG, true }, { GH, true }, { HE, true } } });

    {
        Require(Sketch.CurveCount() == 8u && Sketch.LoopCount() == 2u,
                "one world sketch must hold world curves and loops from more than one plane at once");
        Require(Sketch.Declared(),
                "a world sketch with valid curve references must declare without any one global sketch plane");

        const DeclaredWorldCurve* HeldAB = Sketch.Resolve(AB);
        const DeclaredWorldCurve* HeldEF = Sketch.Resolve(EF);
        Require(HeldAB != nullptr && HeldEF != nullptr,
                "declared world curves must resolve by stable identity");
        Require(HeldAB != nullptr && SamePoint(HeldAB->Geometry.HeldLine().Origin, A)
             && SamePoint(HeldAB->Geometry.HeldLine().Terminus, B),
                "declaring a side-plane loop later must not reinterpret the front-plane curve already stored");
        Require(HeldEF != nullptr && HeldEF->SupportFrameStanding
             && std::fabs(Dot(Normalize(HeldEF->SupportFrame.Normal), Normalize(Side.Normal)) - 1.0) < 1.0e-9,
                "each world curve keeps the placement frame it was authored on as metadata only");

        const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch);
        const WorldLoopAnalysisRecord* FrontRecord = ResolveLoop(Analysis, FrontLoop);
        const WorldLoopAnalysisRecord* SideRecord = ResolveLoop(Analysis, SideLoop);
        Require(FrontRecord != nullptr && SideRecord != nullptr,
                "derived analysis must answer for every declared world loop");
        Require(FrontRecord != nullptr && FrontRecord->Closed && FrontRecord->Coplanar && FrontRecord->FillEligible,
                "the front loop must be recognised as a closed planar region");
        Require(SideRecord != nullptr && SideRecord->Closed && SideRecord->Coplanar && SideRecord->FillEligible,
                "the side loop must do the same on a different plane");
        Require(FrontRecord != nullptr && SideRecord != nullptr
             && std::fabs(Dot(Normalize(FrontRecord->SupportFrame.Normal),
                               Normalize(SideRecord->SupportFrame.Normal))) < 1.0e-6,
                "the two loops must derive genuinely different support planes, not one shared sketch basis");

        const Deliver<ProfileSpecification> PlanarFront = ResolvePlanarWorldLoopProfile(Sketch, FrontLoop);
        Require(PlanarFront.Resolved,
                "a closed planar world loop must hand off as a modelling profile");

        std::vector<CurveSpecification> SourceCurves;
        Sketch.ResolveCurves(SourceCurves);
        ExtrusionSpecification Extruded;
        Extruded.SourceProfile = { 1u };
        Extruded.Direction = FrontRecord->SupportFrame.Normal;
        Extruded.Distance = 50.0;
        const Deliver<SolidStructure> Solid = ConstructExtrusion(PlanarFront.Resolve(), SourceCurves, Extruded);
        Require(Solid.Resolved && Solid.Resolve().Declared(),
                "that derived profile must already feed the existing solid extrusion seam");
        Require(Solid.Resolved && Solid.Resolve().FaceCount() == 6u,
                "extruding a rectangular world loop must produce four walls and two caps");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 MOVING ONE CORNER OUT OF PLANE KEEPS THE LOOP CLOSED BUT REMOVES ITS FILL ELIGIBILITY.
    //--------------------------------------------------------------------------------------------
    {
        DeclaredWorldCurve* HeldBC = Sketch.Resolve(BC);
        DeclaredWorldCurve* HeldCD = Sketch.Resolve(CD);
        Require(HeldBC != nullptr && HeldCD != nullptr,
                "the loop's original curve identities must survive into editing");
        if (HeldBC != nullptr)
            HeldBC->Geometry.HeldLine().Terminus.Forward = 20.0;
        if (HeldCD != nullptr)
            HeldCD->Geometry.HeldLine().Origin.Forward = 20.0;

        const WorldSketchAnalysis Analysis = AnalyzeWorldSketch(Sketch);
        const WorldLoopAnalysisRecord* FrontRecord = ResolveLoop(Analysis, FrontLoop);
        const WorldLoopAnalysisRecord* SideRecord = ResolveLoop(Analysis, SideLoop);
        Require(FrontRecord != nullptr && FrontRecord->Closed,
                "editing one corner in Z must not break the loop's topological closure");
        Require(FrontRecord != nullptr && !FrontRecord->Coplanar,
                "but the same closed loop must stop claiming to be planar once one corner leaves the plane");
        Require(FrontRecord != nullptr && !FrontRecord->FillEligible,
                "and a non-planar closed loop must therefore lose fill/profile eligibility while staying valid geometry");
        Require(SideRecord != nullptr && SideRecord->Closed && SideRecord->Coplanar && SideRecord->FillEligible,
                "making one loop non-planar must not disturb another loop authored on a different plane");

        bool NonCoplanarIssue = false;
        for (const WorldLoopIssue& Issue : Analysis.Issues)
            if (Issue.Loop.IssuedIndex == FrontLoop.IssuedIndex &&
                Issue.Subject == WorldLoopIssueSubject::NonCoplanar)
            {
                NonCoplanarIssue = true;
                break;
            }
        Require(NonCoplanarIssue,
                "analysis must report that the loop lost coplanarity rather than silently filling it anyway");

        const Deliver<ProfileSpecification> Refused = ResolvePlanarWorldLoopProfile(Sketch, FrontLoop);
        Require(!Refused.Resolved,
                "a closed loop that has been edited out of plane must be refused as a profile until it is planar again");
    }

    std::printf("[WorldSketchFoundationProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
