// 🧩 Phase-2 proof for rendering the world-space sketch through the existing CAD packet.

#include "SlateWorkspace/Discipline/WorldSketchRenderingProjection/Api/WorldSketchRenderingProjection.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

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

bool SamePoint(const SpatialPoint& Left,
               const SpatialPoint& Right,
               double Tolerance = 1.0e-9)
{
    return LengthSquared(Difference(Left, Right)) <= Tolerance * Tolerance;
}

} // namespace

int main()
{
    std::printf("[WorldSketchRenderProof] world-space curves from multiple planes project into one CAD packet\n");

    //--------------------------------------------------------------------------------------------
    // 🔴 SCREEN-SPACE PACKETS STILL GO THROUGH THE EXISTING CAD PASS. The projection therefore has to
    //    be the identity-like rows that keep screen pixels as screen pixels and fix w at one.
    //--------------------------------------------------------------------------------------------
    {
        const WorkspaceCadProjection Projection = ResolveWorldSketchScreenProjection(1920u, 1080u);
        Require(Projection.DisplayWidth == 1920.0f && Projection.DisplayHeight == 1080.0f,
                "the screen projection must keep the display size the CAD pass clips against");
        Require(Projection.Projection0[3] == 1.0f
             && Projection.Projection1[0] == 1.0f
             && Projection.Projection2[1] == 1.0f,
                "and it must carry screen x and y straight through with w fixed at one");
    }

    WorldSketchStructure Sketch;
    const WorldPlacementFrame Front = { {}, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
    const WorldPlacementFrame Side  = { { 200.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 } };

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
    static_cast<void>(Sketch.DeclareLoop({ { { AB, true }, { BC, true }, { CD, true }, { DA, true } } }));

    const WorldCurveName EF = Sketch.DeclareLine(E, F, Side);
    const WorldCurveName FG = Sketch.DeclareLine(F, G, Side);
    const WorldCurveName GH = Sketch.DeclareLine(G, H, Side);
    const WorldCurveName HE = Sketch.DeclareLine(H, E, Side);
    static_cast<void>(Sketch.DeclareLoop({ { { EF, true }, { FG, true }, { GH, true }, { HE, true } } }));

    const PlaneExtent Extent = { 0.0f, 0.0f, 800.0f, 600.0f };
    const ResolvedCamera Camera = ResolveFreeCamera({ 100.0, 60.0, -320.0 }, 35.0, 12.0, 60.0, false, 2.0);

    //--------------------------------------------------------------------------------------------
    // 🔴 TWO PLANES, ONE VIEWPORT PACKET. The front and side rectangles each contribute four wire
    //    edges and two fill triangles when both are planar and visible.
    //--------------------------------------------------------------------------------------------
    {
        WorkspaceCadPacket Packet;
        const Deliver<bool> Rendered = ProjectWorldSketchRendering(Sketch, Camera, Extent, Packet);
        Require(Rendered.Resolved, "the world sketch must project into a CAD packet");
        Require(Packet.SegmentCount == 8u,
                "two rectangular loops on different planes must contribute all eight wire edges");
        Require(Packet.FillCount == 4u,
                "and both planar rectangles must contribute their four fill triangles in the same packet");
        Require(Packet.ExtentStanding,
                "the projected packet must declare a screen extent for upload");

        float FrontX = 0.0f, FrontY = 0.0f;
        float SideX = 0.0f, SideY = 0.0f;
        Require(ProjectFromCamera(Camera, Extent, { 50.0, 50.0, 0.0 }, FrontX, FrontY)
             && ProjectFromCamera(Camera, Extent, { 200.0, 50.0, 50.0 }, SideX, SideY),
                "both loop centres must project through the same camera");
        Require(FrontX >= Packet.MinimumAlong && FrontX <= Packet.MaximumAlong
             && FrontY >= Packet.MinimumAcross && FrontY <= Packet.MaximumAcross,
                "the packet extent must cover the front-plane loop's projected centre");
        Require(SideX >= Packet.MinimumAlong && SideX <= Packet.MaximumAlong
             && SideY >= Packet.MinimumAcross && SideY <= Packet.MaximumAcross,
                "and it must cover the side-plane loop's projected centre as well");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A CLOSED LOOP THAT LEAVES ITS PLANE KEEPS ITS WIRES AND LOSES ONLY ITS FILL.
    //--------------------------------------------------------------------------------------------
    {
        DeclaredWorldCurve* HeldBC = Sketch.Resolve(BC);
        DeclaredWorldCurve* HeldCD = Sketch.Resolve(CD);
        Require(HeldBC != nullptr && HeldCD != nullptr,
                "the front loop's curve identities must survive into the render edit");
        if (HeldBC != nullptr)
            HeldBC->Geometry.HeldLine().Terminus.Forward = 20.0;
        if (HeldCD != nullptr)
            HeldCD->Geometry.HeldLine().Origin.Forward = 20.0;

        WorkspaceCadPacket Packet;
        const Deliver<bool> Rendered = ProjectWorldSketchRendering(Sketch, Camera, Extent, Packet);
        Require(Rendered.Resolved, "the edited world sketch must still project");
        Require(Packet.SegmentCount == 8u,
                "moving one corner out of plane must keep the same closed loop wires drawable");
        Require(Packet.FillCount == 2u,
                "but only the still-planar side rectangle may keep its fill triangles");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE EDIT IS GEOMETRIC, NOT DOCUMENTAL. The front edge we moved is still the same edge name,
    //    only with a new world-space endpoint, which is what later 3D transforms need.
    //--------------------------------------------------------------------------------------------
    {
        const DeclaredWorldCurve* HeldBC = Sketch.Resolve(BC);
        Require(HeldBC != nullptr && SamePoint(HeldBC->Geometry.HeldLine().Origin, B)
             && SamePoint(HeldBC->Geometry.HeldLine().Terminus, { 100.0, 100.0, 20.0 }),
                "the same world curve identity must now carry the edited 3D endpoint");
    }

    std::printf("[WorldSketchRenderProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
