// 🧩 A drawn shape must survive being finished, and one bad shape must not erase the others.
//
// 🔴 Closing a polyline anchors the start point twice, so the final pair was coincident and
//    `DeclarePolyline` declared a ZERO-LENGTH line. `SketchStructure::Declared()` is all-or-nothing
//    across every curve, and `ProjectSketchRendering` refused outright on an undeclared sketch -- so
//    pressing Enter to finish a closed shape made EVERY shape already drawn disappear at once.

#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateShape/Sketch/SketchRenderingProjection/Api/SketchRenderingProjection.h"
#include "SlateWorkspace/Discipline/PlacementCommit/Api/PlacementCommit.h"
#include "SlateWorkspace/Discipline/SketchInteraction/Api/SketchInteraction.h"
#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"

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

}   // namespace

int main()
{
    SketchStructure          Sketch;
    WorkspaceRecordStructure Records;
    WorkspaceRevisionSequence Revisions;
    WorkspaceNameIndex       Naming;
    WorkplaneCatalogue       Planes;
    SketchPlacement          Tool;
    WorkspaceRecordName      Pending;

    // 📝 The grid is the plane, adopted exactly as the first press adopts it.
    Sketch.DeclarePlane({ Planes.Active().Origin, Planes.Active().Normal, Planes.Active().Along });
    Tool.Declare(SketchSubject::Polyline, PlacementMethod::Extent, false);

    // ① A square, closed by returning to the start -- the shape that used to blank the viewport.
    const double Corners[5][2] = { { 0.0, 0.0 }, { 100.0, 0.0 }, { 100.0, 100.0 },
                                   { 0.0, 100.0 }, { 0.0, 0.0 } };
    for (const auto& Corner : Corners)
    {
        Tool.Hover(SpatialPoint{ Corner[0], 0.0, Corner[1] }, SketchSnapPlacement{});
        static_cast<void>(Tool.Anchor(false));
    }

    Require(Tool.Anchor(true) == PlacementArrival::Complete, "Enter completes a closed polyline");

    const SealedPlacement Sealed = Tool.Seal();
    const Deliver<WorkspaceRecordName> Committed =
        CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
    Require(Committed.Resolved, "the closed polyline commits");
    AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

    // ② Four corners produce FOUR segments, not five. The coincident closing pair is not a line.
    Require(Sketch.Curves().size() == 4u, "a closed square declares four segments, not a fifth of zero length");
    for (const DeclaredSketchCurve& Curve : Sketch.Curves())
        Require(Curve.Geometry.Declared(), "every declared segment is well-formed");

    // ③ And the shapes are still there to draw.
    WorkspaceCadPacket Packet;
    const Deliver<bool> Projected = ProjectSketchRendering(Sketch, Records, Packet);
    Require(Projected.Resolved, "the finished sketch still projects");
    Require(Packet.SegmentCount >= 4u, "the four sides are still drawn after Enter");

    // ④ THE RESILIENCE CLAIM. A malformed curve costs its own shape and nothing else. Sketches
    //    accumulate work, and refusing the whole projection over one bad record throws it all away.
    SketchStructure Damaged = Sketch;
    static_cast<void>(Damaged.DeclareLine({ 5.0, 0.0, 5.0 }, { 5.0, 0.0, 5.0 }));   // zero length
    Require(!Damaged.Declared(), "the damaged sketch is indeed not wholly declared");

    WorkspaceCadPacket DamagedPacket;
    const Deliver<bool> Survived = ProjectSketchRendering(Damaged, Records, DamagedPacket);
    Require(Survived.Resolved, "one malformed curve does not refuse the whole projection");
    Require(DamagedPacket.SegmentCount >= 4u,
            "the well-formed shapes still draw alongside a malformed one");

    // ⑤ A sketch with no plane has no coordinate frame and must still refuse.
    SketchStructure Planeless;
    WorkspaceCadPacket EmptyPacket;
    Require(!ProjectSketchRendering(Planeless, Records, EmptyPacket).Resolved,
            "a sketch with no plane is refused, because there is nowhere to project onto");

    std::printf("[SketchDurability] %u claims, %u failures (%u segments after Enter)\n",
                Claims, Failures, Packet.SegmentCount);
    return Failures == 0u ? 0 : 1;
}
