// 🧩 Every curve subject must draw, show what shapes it, keep its shape as it grows, land on the
//    workplane, and close on its own endpoint.
//
// 🔴 FIVE INDEPENDENT DEFECTS SHARED ONE SYMPTOM -- "the drawing tools don't work":
//    1. The preview named ONE SUBJECT PER BRANCH, so Hermite, basis spline and NURBS had no branch and
//       drew no feedback at all. All three committed correctly; nothing ever showed it.
//    2. Nothing emitted a marker for an anchor, so a Bezier drew a bare curve with no control points.
//    3. Tessellation was a flat 48 steps whatever the curve's size, and every chord of an approximated
//       arc lies INSIDE the true curve -- so the larger a shape was drawn, the further inside its own
//       outline it was drawn. "The longer the curve, the more it shrinks."
//    4. The polygon commit hardcoded six sides, so the tool drew a hexagon and nothing else.
//    5. `ResolveSketchBasis` asked `Declared()`, which is all-or-nothing over the sketch's CURVES too,
//       so the FIRST shape on a fresh sketch was mapped through the world origin rather than the
//       active workplane -- it drew in mid-air.
//    6. Snapping raced every candidate on raw distance, and the perpendicular foot on a segment is
//       always at least as near as that segment's endpoint, so a polyline could never close on itself.

#include "SketchToolset/SketchTool/SketchPlacement/Api/SketchPlacement.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/Sketch/SketchRenderingProjection/Api/SketchRenderingProjection.h"
#include "SlateShape/Sketch/SketchSnap/Api/SketchSnap.h"
#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationStanding.h"
#include "SlateWorkspace/Discipline/PlacementCommit/Api/PlacementCommit.h"
#include "SlateWorkspace/Discipline/SketchInteraction/Api/SketchInteraction.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/CadProjection.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"
#include "Shared/WorkspaceCadNearClip.slang.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

/// 📝 Drives one subject through `Clicks` contacts and reports what its preview produced.
void DrivePreview(SketchSubject Subject,
                  PlacementMethod Method,
                  std::uint32_t Clicks,
                  WorkspaceCadPacket& Delivered,
                  bool& CurveDeclared)
{
    SketchStructure   Sketch;
    WorkplaneCatalogue Workplanes;
    SketchPlacement   Tool;

    Sketch.DeclarePlane({ Workplanes.Active().Origin,
                          Workplanes.Active().Normal,
                          Workplanes.Active().Along });

    Tool.Declare(Subject, Method, false);
    for (std::uint32_t Index = 0u; Index < Clicks; ++Index)
    {
        Tool.Hover({ 30.0 * Index, 0.0, (Index % 2u) ? 45.0 : 0.0 }, {});
        static_cast<void>(Tool.Anchor(false));
    }
    Tool.Hover({ 30.0 * Clicks, 0.0, 20.0 }, {});

    std::vector<CurveSpecification> Geometry;
    ResolvePlacementCurves(Tool.Subject(), Tool.Anchors(), Tool.HoverPosition(), Geometry);
    CurveDeclared = !Geometry.empty() && Geometry.front().Declared();

    static_cast<void>(ProjectPlacementPreview(Sketch, Geometry, Tool.Anchors(),
                                              Tool.HoverPosition(), Delivered));
}

/// 📝 The worst distance by which a tessellated circle falls inside the circle it approximates.
double ChordErrorForRadius(double Radius)
{
    CircleCurve Round;
    Round.Centre         = { 0.0, 0.0, 0.0 };
    Round.Normal         = { 0.0, 1.0, 0.0 };
    Round.StartDirection = { 1.0, 0.0, 0.0 };
    Round.Radius         = Radius;

    const CurveSpecification Geometry = CurveSpecification::DeclareCircle(Round);
    const std::uint32_t Steps = ResolveCurveStepCount(Geometry, 48u);
    return Radius * (1.0 - std::cos(3.14159265358979 / static_cast<double>(Steps)));
}

}   // namespace

int main()
{
    std::printf("[SketchDrawingProof] every curve subject draws, shows its controls and holds its shape\n");

    //--------------------------------------------------------------------------------------------
    // 🔴 EVERY CURVE SUBJECT PREVIEWS. The four spline subjects are the ones that drew nothing.
    //--------------------------------------------------------------------------------------------
    {
        struct Case
        {
            SketchSubject   Subject;
            PlacementMethod Method;
            std::uint32_t   Clicks;
            const char*     Naming;
        };

        const Case Cases[] = {
            { SketchSubject::Bezier,         PlacementMethod::Extent,  3u, "Bezier" },
            { SketchSubject::Hermite,        PlacementMethod::Extent,  4u, "Hermite" },
            { SketchSubject::BasisSpline,    PlacementMethod::Extent,  4u, "basis spline" },
            { SketchSubject::RationalSpline, PlacementMethod::Extent,  4u, "NURBS curve" },
            { SketchSubject::Polygon,        PlacementMethod::Centred, 1u, "polygon" },
            { SketchSubject::Line,           PlacementMethod::Extent,  1u, "line" },
        };

        for (const Case& Subject : Cases)
        {
            WorkspaceCadPacket Delivered;
            bool CurveDeclared = false;
            DrivePreview(Subject.Subject, Subject.Method, Subject.Clicks, Delivered, CurveDeclared);

            Require(CurveDeclared, "the placement in progress must describe a curve");
            Require(Delivered.SegmentCount >= 1u, "every curve subject must preview at least one segment");

            // 🔴 THE CONTROL POINTS. One marker per anchor, plus one for the hover.
            Require(Delivered.MarkerCount == Subject.Clicks + 1u,
                    "every anchor and the hover must draw a control marker");

            std::printf("  %-14s segments=%3u  controls=%u\n",
                        Subject.Naming, Delivered.SegmentCount, Delivered.MarkerCount);
        }
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE PREVIEW GOES IN THE PACKET THE GPU PASS READS, appended to the committed shapes rather
    //    than drawn separately -- which is what takes it off the CPU.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure           Sketch;
        WorkspaceRecordStructure  Records;
        WorkspaceRevisionSequence Revisions;
        WorkspaceNameIndex        Naming;
        WorkplaneCatalogue        Workplanes;
        SketchPlacement           Tool;
        WorkspaceRecordName       Pending;

        Sketch.DeclarePlane({ Workplanes.Active().Origin,
                              Workplanes.Active().Normal,
                              Workplanes.Active().Along });

        Tool.Declare(SketchSubject::Line, PlacementMethod::Extent, false);
        Tool.Hover({ 0.0, 0.0, 0.0 }, {});
        static_cast<void>(Tool.Anchor(false));
        Tool.Hover({ 100.0, 0.0, 0.0 }, {});
        static_cast<void>(Tool.Anchor(true));

        const SealedPlacement Sealed = Tool.Seal();
        const Deliver<WorkspaceRecordName> Committed =
            CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
        AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

        WorkspaceCadPacket Delivered;
        static_cast<void>(ProjectSketchRendering(Sketch, Records, Delivered));
        const std::uint32_t Committed_Segments = Delivered.SegmentCount;
        Require(Committed_Segments >= 1u, "the committed line must project into the packet");

        Tool.Declare(SketchSubject::Bezier, PlacementMethod::Extent, false);
        Tool.Hover({ 0.0, 0.0, 50.0 }, {});
        static_cast<void>(Tool.Anchor(false));
        Tool.Hover({ 80.0, 0.0, 90.0 }, {});

        std::vector<CurveSpecification> PreviewSpans;
        ResolvePlacementCurves(Tool.Subject(), Tool.Anchors(), Tool.HoverPosition(), PreviewSpans);
        static_cast<void>(ProjectPlacementPreview(
            Sketch, PreviewSpans, Tool.Anchors(), Tool.HoverPosition(), Delivered));

        // 🔴 APPENDED, NOT REPLACED. The committed geometry must survive the preview being added, or
        //    the shape under the pointer would blank everything already drawn.
        Require(Delivered.SegmentCount > Committed_Segments,
                "the preview must be appended to the same packet as the committed shapes");
        Require(Delivered.MarkerCount >= 1u,
                "the preview's control points must reach the packet the GPU pass reads");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A CURVE MUST NOT SHRINK AS IT GROWS. Chord error is held near-constant instead of rising in
    //    proportion to the radius, which is what a fixed step count does.
    //--------------------------------------------------------------------------------------------
    {
        const double Small = ChordErrorForRadius(40.0);
        const double Large = ChordErrorForRadius(1000.0);
        const double Huge  = ChordErrorForRadius(4000.0);

        Require(Large <= Small * 2.0,
                "a 25x larger curve must not be drawn 25x further inside its own outline");
        Require(Huge <= 0.5,
                "chord error must stay well under half a unit at every drawable size");

        // 🔴 THE NEGATIVE CLAIM. Had the step count stayed fixed, error would rise LINEARLY with the
        //    radius -- this is the number the defect produced, and it must not come back.
        const double FixedStepError = 1000.0 * (1.0 - std::cos(3.14159265358979 / 48.0));
        Require(Large < FixedStepError * 0.25,
                "adaptive tessellation must beat the fixed 48-step count it replaced");

        std::printf("  chord error r=40 %.4f  r=1000 %.4f  (fixed-48 would be %.4f)\n",
                    Small, Large, FixedStepError);

        // 🔴 AND THE RENDER PATH MUST ACTUALLY USE IT. Asserting the helper alone leaves the call
        //    sites free to keep passing a flat 48 -- which is precisely the defect. A large circle is
        //    committed and projected end-to-end, and the packet must carry more than the fixed count.
        {
            SketchStructure           Round;
            WorkspaceRecordStructure  RoundRecords;
            WorkspaceRevisionSequence RoundRevisions;
            WorkspaceNameIndex        RoundNaming;
            WorkplaneCatalogue        RoundPlanes;
            SketchPlacement           RoundTool;
            WorkspaceRecordName       RoundPending;

            Round.DeclarePlane({ RoundPlanes.Active().Origin,
                                 RoundPlanes.Active().Normal,
                                 RoundPlanes.Active().Along });

            RoundTool.Declare(SketchSubject::Circle, PlacementMethod::Centred, false);
            RoundTool.Hover({ 0.0, 0.0, 0.0 }, {});
            static_cast<void>(RoundTool.Anchor(false));
            RoundTool.Hover({ 1000.0, 0.0, 0.0 }, {});
            static_cast<void>(RoundTool.Anchor(true));

            const SealedPlacement RoundSealed = RoundTool.Seal();
            const Deliver<WorkspaceRecordName> RoundCommitted =
                CommitPlacement(RoundNaming, Round, RoundRecords, RoundRevisions, RoundSealed);
            AdoptCommittedShape(RoundSealed.Subject, RoundNaming, Round, RoundRecords,
                                RoundRevisions, RoundCommitted, RoundPending);

            WorkspaceCadPacket RoundPacket;
            static_cast<void>(ProjectSketchRendering(Round, RoundRecords, RoundPacket));

            // 🔴 THE SHRINKING, MEASURED ON THE DRAWN GEOMETRY ITSELF. Every chord's midpoint lies
            //    inside the true circle by the sagitta, so the smallest radius across the packet IS
            //    how far the drawn shape has pulled in from the shape the artist asked for. Counting
            //    segments is too weak a claim -- a circle commits as four arcs, so a flat 48 steps
            //    still yields 192 segments and a count-based claim passes while the shape shrinks.
            double Tightest = 1.0e30;
            for (std::uint32_t Index = 0u; Index < RoundPacket.SegmentCount; ++Index)
            {
                const WorkspaceCadSegment& Drawn = RoundPacket.Segments[Index];
                const double MidAlong  = (static_cast<double>(Drawn.Along0)  + Drawn.Along1)  * 0.5;
                const double MidAcross = (static_cast<double>(Drawn.Across0) + Drawn.Across1) * 0.5;
                Tightest = std::min(Tightest, std::sqrt(MidAlong * MidAlong + MidAcross * MidAcross));
            }

            // ⚠️ THE THRESHOLD IS THE TOLERANCE, NOT A ROUND NUMBER. A loose bound is a gate that
            //    cannot fail: the flat-48 defect shrinks this circle by 0.134 units, so any threshold
            //    above that passes while the bug is present. Twice `CurveChordTolerance` is the
            //    tightest bound the adaptive path is guaranteed to meet and the defect cannot.
            const double Shrinkage = 1000.0 - Tightest;
            Require(Shrinkage < CurveChordTolerance * 2.0,
                    "a radius-1000 circle must not be drawn measurably inside its own outline");

            std::printf("  radius-1000 circle: %u segments, drawn %.4f units inside "
                        "(a flat 48 steps shrinks it by %.4f)\n",
                        RoundPacket.SegmentCount, Shrinkage,
                        1000.0 * (1.0 - std::cos(3.14159265358979 / (48.0 * 4.0))));
        }

        // 📝 A line is exact at two points however long it is; it must not be given 512 of them.
        const CurveSpecification Straight =
            CurveSpecification::DeclareLine({ 0.0, 0.0, 0.0 }, { 9000.0, 0.0, 0.0 });
        Require(ResolveCurveStepCount(Straight, 48u) == 2u,
                "a line needs exactly two points however long it is drawn");

        // 📝 And the ceiling must hold, or a curve drawn kilometres wide would ask for millions.
        Require(ResolveCurveStepCount(
                    CurveSpecification::DeclareLine({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }), 48u)
                <= CurveStepLimit,
                "the step count must never exceed its limit");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A POLYGON IS A CIRCLE WITH A SIDE COUNT: drag for the radius, WHEEL for the resolution.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure           Sketch;
        WorkspaceRecordStructure  Records;
        WorkspaceRevisionSequence Revisions;
        WorkspaceNameIndex        Naming;
        WorkplaneCatalogue        Workplanes;
        SketchPlacement           Tool;
        WorkspaceRecordName       Pending;

        Sketch.DeclarePlane({ Workplanes.Active().Origin,
                              Workplanes.Active().Normal,
                              Workplanes.Active().Along });

        Tool.Declare(SketchSubject::Polygon, PlacementMethod::Centred, false);
        Tool.Hover({ 0.0, 0.0, 0.0 }, {});
        static_cast<void>(Tool.Anchor(false));
        Tool.Hover({ 80.0, 0.0, 0.0 }, {});

        Require(Tool.Resolution() == PolygonSideDefault, "a polygon starts at its default resolution");

        Require(Tool.Resolve(2.0f), "the wheel must be consumed while a polygon is being placed");
        Require(Tool.Resolution() == PolygonSideDefault + 2u, "two notches must add two sides");

        static_cast<void>(Tool.Resolve(-50.0f));
        Require(Tool.Resolution() == PolygonSideMinimum, "the side count must clamp at its minimum");

        static_cast<void>(Tool.Resolve(500.0f));
        Require(Tool.Resolution() == PolygonSideMaximum, "the side count must clamp at its maximum");

        static_cast<void>(Tool.Resolve(-56.0f));
        const std::uint32_t Wanted = Tool.Resolution();
        Require(Wanted == 8u, "the wheel must land on the side count it was driven to");

        Require(Tool.Anchor(true) == PlacementArrival::Complete, "Enter must complete the polygon");
        const SealedPlacement Sealed = Tool.Seal();
        Require(Sealed.Resolution == Wanted, "the sealed placement must carry the chosen resolution");

        const Deliver<WorkspaceRecordName> Committed =
            CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
        Require(Committed.Resolved, "the polygon must commit");

        // 🔴 THE HARDCODED SIX. An eight-sided polygon must have eight edges, not six.
        Require(Sketch.Curves().size() == Wanted,
                "the committed polygon must have the number of sides the wheel chose");

        std::printf("  polygon wheeled to %u sides -> %zu curves committed\n",
                    Wanted, Sketch.Curves().size());
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE WHEEL BELONGS TO THE CAMERA FOR EVERY OTHER TOOL. Consuming it always would break zoom.
    //--------------------------------------------------------------------------------------------
    {
        SketchPlacement Tool;
        Tool.Declare(SketchSubject::Circle, PlacementMethod::Centred, false);
        Require(!Tool.Resolve(3.0f), "the wheel must still zoom while a circle is being drawn");

        Tool.Declare(SketchSubject::Polygon, PlacementMethod::Centred, false);
        Require(!Tool.Resolve(0.0f), "an idle wheel must not be consumed");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 SHAPES LAND ON THE ACTIVE WORKPLANE, NOT IN MID-AIR -- including the FIRST shape drawn, when
    //    the sketch holds a plane but not yet a single curve.
    //--------------------------------------------------------------------------------------------
    {
        WorkplaneCatalogue Workplanes;
        for (std::uint32_t Index = 1u; Index <= 3u; ++Index)
        {
            Discard(Workplanes.Activate({ Index }));

            SketchStructure Sketch;
            Sketch.DeclarePlane({ Workplanes.Active().Origin,
                                  Workplanes.Active().Normal,
                                  Workplanes.Active().Along });

            // 🔴 A plane and no curves is the state the FIRST shape of every sketch is drawn in.
            Require(Sketch.PlaneDeclared(), "a sketch given a plane must report that plane as declared");

            const SpatialBasis Basis = ResolveSketchBasis(Sketch);
            const SpatialDirection Wanted = Workplanes.Active().Normal;

            Require(std::fabs(Basis.Normal.Left    - Wanted.Left)    < 1.0e-9 &&
                    std::fabs(Basis.Normal.Up      - Wanted.Up)      < 1.0e-9 &&
                    std::fabs(Basis.Normal.Forward - Wanted.Forward) < 1.0e-9,
                    "the drawing basis must be the ACTIVE workplane, not the world origin");
        }
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 WORLD-NATIVE DRAWING MUST SNAP ON THE ACTIVE WORKPLANE, not on whatever legacy plane the
    //    compatibility sketch last remembered.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure Sketch;
        Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });

        const Workplane Raised = ResolveOffsetWorkplane(StandingWorkplane::Ground, 40.0);
        SketchSnapMask GridOnly = {};
        GridOnly.EndpointAccepted = false;
        GridOnly.MidpointAccepted = false;
        GridOnly.CentreAccepted = false;
        GridOnly.ControlAccepted = false;
        GridOnly.AlongCurveAccepted = false;
        GridOnly.IntersectionAccepted = false;
        GridOnly.GridAccepted = true;
        GridOnly.PerpendicularAccepted = false;
        GridOnly.TangentAccepted = false;

        const SketchSnapPlacement Snapped = ResolveNearestSnap(
            Sketch, { Raised.Origin, Raised.Normal, Raised.Along },
            { 23.0, 40.0, 17.0 }, 100.0, GridOnly, 10.0);
        Require(Snapped.Subject == SketchSnapSubject::Grid,
                "an explicit active workplane must still produce grid snaps");
        Require(std::fabs(Snapped.Position.Up - 40.0) < 1.0e-9,
                "and the snapped point must land on that active workplane, not the sketch's stale plane");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 ONE MALFORMED CURVE MUST NOT MOVE EVERY OTHER SHAPE INTO MID-AIR. `Declared()` is
    //    all-or-nothing across every curve, so a single degenerate one flipped the whole sketch to
    //    "not declared" -- and the basis silently fell back to the world origin, relocating every
    //    shape already drawn on a Front or Right workplane.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure Sketch;
        Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } });
        static_cast<void>(Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 50.0, 0.0, 0.0 }));

        const SpatialBasis Healthy = ResolveSketchBasis(Sketch);

        // 📝 A default-constructed specification is undeclared: the state a refused placement leaves.
        static_cast<void>(Sketch.DeclareCurve(CurveSpecification{}));
        Require(!Sketch.Declared(), "one malformed curve makes the whole sketch undeclared");
        Require(Sketch.PlaneDeclared(), "but the plane it was drawn on is still perfectly good");

        const SpatialBasis Wounded = ResolveSketchBasis(Sketch);
        Require(std::fabs(Healthy.Normal.Left    - Wounded.Normal.Left)    < 1.0e-12 &&
                std::fabs(Healthy.Normal.Up      - Wounded.Normal.Up)      < 1.0e-12 &&
                std::fabs(Healthy.Normal.Forward - Wounded.Normal.Forward) < 1.0e-12,
                "a malformed curve must not relocate every other shape onto a different plane");
        Require(std::fabs(Wounded.Normal.Forward - 1.0) < 1.0e-12,
                "the basis must stay on the Front workplane it was given");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A POLYLINE MUST CLOSE ON ITS OWN ENDPOINT. The perpendicular foot on a segment is always at
    //    least as near as that segment's end, so distance alone can never pick the corner.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure Sketch;
        Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });

        static_cast<void>(Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 100.0, 0.0, 0.0 }));
        static_cast<void>(Sketch.DeclareLine({ 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }));
        static_cast<void>(Sketch.DeclareLine({ 100.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 }));

        // 📝 Probing diagonally inward from the start corner: the foot of the perpendicular onto the
        //    first segment sits at (2,0,0), strictly nearer than the corner at the origin.
        const SketchSnapPlacement Placed = ResolveNearestSnap(Sketch, { 2.0, 0.0, 2.0 }, 10.0);
        Require(Placed.Resolved(), "a probe within tolerance of a corner must snap to something");
        Require(Placed.Subject == SketchSnapSubject::Endpoint,
                "an endpoint must beat a nearer point along the same curve");
        Require(std::fabs(Placed.Position.Left) < 1.0e-9 &&
                std::fabs(Placed.Position.Forward) < 1.0e-9,
                "closing a loop must land exactly on the start point");

        // 🔴 THE NEGATIVE CLAIM. Precedence must not swallow distance: two endpoints still resolve to
        //    the nearer one, or snapping would jump to whichever corner happened to be declared first.
        const SketchSnapPlacement Nearer = ResolveNearestSnap(Sketch, { 98.0, 0.0, 2.0 }, 10.0);
        Require(Nearer.Subject == SketchSnapSubject::Endpoint, "the far corner must also snap by kind");
        Require(std::fabs(Nearer.Position.Left - 100.0) < 1.0e-9,
                "between two endpoints the nearer one must still win");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A POLYLINE MUST CLOSE ON ITS OWN IN-PROGRESS ANCHORS. Nothing is declared into the sketch
    //    until the placement seals, so the FIRST loop an artist draws has no declared curve to aim at.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure Sketch;
        Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });

        Require(Sketch.Curves().empty(), "the sketch holds no declared curve yet");

        const std::vector<SpatialPoint> Pending = {
            { 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }
        };

        const SketchSnapPlacement Blind = ResolveNearestSnap(Sketch, { 2.0, 0.0, 2.0 }, 10.0, {}, 10.0, {});
        Require(Blind.Subject != SketchSnapSubject::Endpoint,
                "with nothing declared and nothing pending there is no endpoint to find");

        const SketchSnapPlacement Seeing =
            ResolveNearestSnap(Sketch, { 2.0, 0.0, 2.0 }, 10.0, {}, 10.0, Pending);
        Require(Seeing.Subject == SketchSnapSubject::Endpoint,
                "the placement in progress must offer its own anchors as endpoints");
        Require(std::fabs(Seeing.Position.Left) < 1.0e-9 &&
                std::fabs(Seeing.Position.Forward) < 1.0e-9,
                "the first loop must close exactly on the point it started from");
    }


    //--------------------------------------------------------------------------------------------
    // 🔴 EVERY DRAWABLE SUBJECT PREVIEWS WHILE IT IS BEING DRAWN. `ResolvePlacementCurve` answered
    //    for seven subjects and let the rest fall to `default`, so Ellipse, EllipticalArc and
    //    Polyline previewed as NOTHING -- the artist saw only the committed shape appear on release,
    //    which is exactly "it only shows at the end".
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure    Sketch;
        WorkplaneCatalogue Workplanes;
        Sketch.DeclarePlane({ Workplanes.Active().Origin,
                              Workplanes.Active().Normal,
                              Workplanes.Active().Along });

        struct Drawable { SketchSubject Subject; const char* Naming; };
        const Drawable Every[] = {
            { SketchSubject::Line,           "line" },
            { SketchSubject::Polyline,       "polyline" },
            { SketchSubject::Rectangle,      "rectangle" },
            { SketchSubject::Circle,         "circle" },
            { SketchSubject::Arc,            "arc" },
            { SketchSubject::Ellipse,        "ellipse" },
            { SketchSubject::EllipticalArc,  "elliptical arc" },
            { SketchSubject::Polygon,        "polygon" },
            { SketchSubject::Slot,           "slot" },
            { SketchSubject::Bezier,         "Bezier" },
            { SketchSubject::BasisSpline,    "basis spline" },
            { SketchSubject::RationalSpline, "NURBS" },
            { SketchSubject::Hermite,        "Hermite" },
            { SketchSubject::Dimension,      "dimension" },
        };

        for (const Drawable& Subject : Every)
        {
            const std::vector<SpatialPoint> Anchors = { { 0.0, 0.0, 0.0 } };
            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(Subject.Subject, Anchors, { 100.0, 0.0, 60.0 }, Spans);

            WorkspaceCadPacket Delivered;
            static_cast<void>(ProjectPlacementPreview(Sketch, Spans, Anchors,
                                                      { 100.0, 0.0, 60.0 }, Delivered));

            Require(Delivered.SegmentCount >= 1u,
                    "every drawable subject must preview from its first anchor onwards");
        }
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 AN ELLIPSE PREVIEWS AS ONE CLOSED CURVE. It had no arm at all and fell to `default`.
    //--------------------------------------------------------------------------------------------
    {
        const std::vector<SpatialPoint> Anchors = { { 0.0, 0.0, 0.0 } };
        std::vector<CurveSpecification> Spans;
        ResolvePlacementCurves(SketchSubject::Ellipse, Anchors, { 100.0, 0.0, 60.0 }, Spans);

        Require(Spans.size() == 1u, "an ellipse previews as exactly one curve");
        Require(!Spans.empty() && Spans.front().Subject() == CurveSubject::Ellipse,
                "and that curve is an ellipse, not a circle or a line");

        if (!Spans.empty())
        {
            std::vector<SpatialPoint> Outline;
            AppendCurvePolyline(Spans.front(), Outline,
                                ResolveCurveStepCount(Spans.front(), 48u));

            Require(Outline.size() >= 3u, "the previewed ellipse must tessellate");

            // 🔴 THE OPEN TIP. First and last point must coincide, or the outline shows a gap.
            const double Gap = std::sqrt(LengthSquared(
                Difference(Outline.front(), Outline.back())));
            Require(Gap < 1.0e-9, "the previewed ellipse must close on itself, with no open tip");
        }
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A HERMITE IS A CONTINUOUS CHAIN THAT GROWS WITH EVERY CLICK. Read as
    //    {start, end, tangent, tangent} it spent four clicks on ONE span and ignored the rest --
    //    "it renders the first 2 points as a curve, other places are just points".
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure    Sketch;
        WorkplaneCatalogue Workplanes;
        Sketch.DeclarePlane({ Workplanes.Active().Origin,
                              Workplanes.Active().Normal,
                              Workplanes.Active().Along });

        for (std::uint32_t Clicks = 2u; Clicks <= 6u; ++Clicks)
        {
            std::vector<SpatialPoint> Anchors;
            for (std::uint32_t Index = 0u; Index < Clicks; ++Index)
                Anchors.push_back({ 40.0 * Index, 0.0, (Index % 2u) ? 50.0 : 0.0 });

            const SpatialPoint Hover = { 40.0 * Clicks, 0.0, 25.0 };

            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(SketchSubject::Hermite, Anchors, Hover, Spans);

            // 🔴 One span per gap between points: N anchors plus the hover give N spans.
            Require(Spans.size() == Clicks,
                    "each further Hermite click must add a further span");

            // 🔴 CONTINUITY. Every span must begin exactly where the last one ended, or the curve
            //    is the row of disconnected pieces that was reported.
            double Worst = 0.0;
            for (std::size_t Index = 0u; Index + 1u < Spans.size(); ++Index)
            {
                std::vector<SpatialPoint> Before, After;
                AppendCurvePolyline(Spans[Index], Before, 16u);
                AppendCurvePolyline(Spans[Index + 1u], After, 16u);
                if (Before.empty() || After.empty())
                    continue;
                Worst = std::max(Worst, std::sqrt(LengthSquared(
                    Difference(Before.back(), After.front()))));
            }
            Require(Worst < 1.0e-9, "the Hermite chain must be continuous across every span");

            // 🔴 AND IT MUST PASS THROUGH THE ANCHORS THE ARTIST CLICKED, not merely near them.
            std::vector<SpatialPoint> First;
            AppendCurvePolyline(Spans.front(), First, 16u);
            Require(!First.empty() && std::sqrt(LengthSquared(
                        Difference(First.front(), Anchors.front()))) < 1.0e-9,
                    "the chain must start at the first anchor");
        }

        std::printf("  Hermite: 2..6 clicks give 2..6 continuous spans\n");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE HERMITE CHAIN SURVIVES ENTER. The commit read four anchors and discarded the rest.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure           Sketch;
        WorkspaceRecordStructure  Records;
        WorkspaceRevisionSequence Revisions;
        WorkspaceNameIndex        Naming;
        WorkplaneCatalogue        Workplanes;
        SketchPlacement           Tool;
        WorkspaceRecordName       Pending;

        Sketch.DeclarePlane({ Workplanes.Active().Origin,
                              Workplanes.Active().Normal,
                              Workplanes.Active().Along });

        // 📝 Six clicks, then Enter to end the chain. Enter no longer places a seventh point.
        for (std::uint32_t Index = 0u; Index < 6u; ++Index)
        {
            Tool.Declare(SketchSubject::Hermite, PlacementMethod::Extent, false);
            Tool.Hover({ 40.0 * Index, 0.0, (Index % 2u) ? 50.0 : 0.0 }, {});
            static_cast<void>(Tool.Anchor(false));
        }

        Require(Tool.Anchor(true) == PlacementArrival::Complete,
                "Enter must complete a Hermite of any length");

        const SealedPlacement Sealed = Tool.Seal();
        Require(Sealed.Anchors.size() == 6u, "every anchor taken must reach the commit");

        const Deliver<WorkspaceRecordName> Committed =
            CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
        Require(Committed.Resolved, "the Hermite chain must commit");
        AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

        // 🔴 FIVE SPANS FROM SIX ANCHORS. One curve was the defect.
        Require(Sketch.Curves().size() == 5u,
                "six anchors must commit as five spans, not one");

        WorkspaceCadPacket Delivered;
        static_cast<void>(ProjectSketchRendering(Sketch, Records, Delivered));
        Require(Delivered.SegmentCount > 100u, "the committed chain must project as a curve");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 ONE LENS FOR THE SKETCH AND THE GROUND. The standing carries the field of view now; a
    //    constant there is what drew shapes through a 42° lens onto a 60° grid.
    //--------------------------------------------------------------------------------------------
    {
        const SpatialBasis Basis = { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
        const PlaneExtent  Leaf  = { 0.0f, 0.0f, 800.0f, 600.0f };

        ViewportStanding Narrow;
        Narrow.Orientation = ViewportOrientation::Isometric;
        Narrow.Focus       = { 0.0, 0.0, 0.0 };
        Narrow.Distance    = 240.0;
        Narrow.FieldOfViewDegrees = 42.0;

        ViewportStanding Wide = Narrow;
        Wide.FieldOfViewDegrees = 60.0;

        float NarrowX = 0.0f, NarrowY = 0.0f, WideX = 0.0f, WideY = 0.0f;
        const bool GotNarrow = ProjectViewportPoint(Basis, Narrow, true, Leaf, 50.0, 50.0, NarrowX, NarrowY);
        const bool GotWide   = ProjectViewportPoint(Basis, Wide,   true, Leaf, 50.0, 50.0, WideX,   WideY);

        Require(GotNarrow && GotWide, "both lenses must project the same point");

        // 🔴 THE LENSES MUST DISAGREE, or the field of view is being ignored and the fix is inert.
        Require(std::fabs(NarrowX - WideX) > 1.0,
                "the field of view must actually change where a point lands");

        // 📝 tan(21°)/tan(30°) = 0.665: the wider lens puts the same point nearer the centre.
        const double Centre  = static_cast<double>(Leaf.MinimumX) + Leaf.Width() * 0.5;
        const double Ratio   = (static_cast<double>(WideX) - Centre)
                             / (static_cast<double>(NarrowX) - Centre);
        Require(std::fabs(Ratio - 0.6653) < 0.01,
                "the offset from centre must scale by the ratio of the two lenses");

        std::printf("  lens 42 deg vs 60 deg: same point at x=%.1f and x=%.1f (ratio %.4f)\n",
                    NarrowX, WideX, Ratio);
    }


    //--------------------------------------------------------------------------------------------
    // 🔴 AN ORTHOGRAPHIC VIEW DRAWS ON THE PLANE IT IS LOOKING AT. Only Top agreed with the plane
    //    being drawn on; Front and Side kept Ground active, so the artist drew on a surface seen
    //    EDGE ON and the shape landed behind the pointer instead of under it.
    //--------------------------------------------------------------------------------------------
    {
        struct Facing
        {
            ViewportOrientation Orientation;
            StandingWorkplane   Plane;
            const char*         ZeroedAxis;
        };

        // 📝 By axis, not by sign: a plane has no far side, so Front and Back share one.
        const Facing Every[] = {
            { ViewportOrientation::Top,    StandingWorkplane::Ground, "Y" },
            { ViewportOrientation::Bottom, StandingWorkplane::Ground, "Y" },
            { ViewportOrientation::Front,  StandingWorkplane::Front,  "Z" },
            { ViewportOrientation::Back,   StandingWorkplane::Front,  "Z" },
            { ViewportOrientation::Left,   StandingWorkplane::Side,   "X" },
            { ViewportOrientation::Right,  StandingWorkplane::Side,   "X" },
        };

        for (const Facing& Looking : Every)
        {
            StandingWorkplane Viewed = StandingWorkplane::Ground;
            Require(ResolveViewedWorkplane(Looking.Orientation, Viewed),
                    "every axis-aligned view must name the plane it looks at");
            Require(Viewed == Looking.Plane,
                    "and it must be the plane square to the display, not the ground");
            Require(std::strcmp(ResolveWorkplaneZeroedAxis(Viewed), Looking.ZeroedAxis) == 0,
                    "the view must hold the expected axis at zero");

            WorkplaneCatalogue Catalogue;
            static_cast<void>(ActivateViewedWorkplane(Catalogue, Looking.Orientation, false));

            const Workplane Expected = ResolveStandingWorkplane(Looking.Plane);
            Require(std::fabs(Dot(Catalogue.Active().Normal, Expected.Normal) - 1.0) < 1.0e-9,
                    "an orthographic view must activate the plane it faces");
        }

        // 🔴 Isometric is square to nothing and must leave the plane alone, or the artist's choice
        //    would be dragged back to a standing plane every time they orbited off an axis.
        StandingWorkplane Unused = StandingWorkplane::Ground;
        Require(!ResolveViewedWorkplane(ViewportOrientation::Isometric, Unused),
                "an isometric view is square to no plane and must name none");

        // 🔴 Nor may a perspective view, which is a free camera by definition.
        for (const Facing& Looking : Every)
        {
            WorkplaneCatalogue Catalogue;
            Require(!ActivateViewedWorkplane(Catalogue, Looking.Orientation, true),
                    "a perspective view must never change the plane being drawn on");
        }

        // 🔴 A plane the artist placed themselves outranks the view; they asked for that surface.
        {
            WorkplaneCatalogue Catalogue;
            Workplane Mine;
            Mine.Origin = { 5.0, 5.0, 5.0 };
            Mine.Normal = { 0.0, 1.0, 0.0 };
            Mine.Along  = { 1.0, 0.0, 0.0 };
            Mine.Source = WorkplaneOrigin::Placed;

            const WorkplaneName Named = Catalogue.Declare(Mine, "My Plane", true);
            Require(!ActivateViewedWorkplane(Catalogue, ViewportOrientation::Front, false),
                    "a placed plane must survive a change of view");
            Require(Catalogue.ActiveName() == Named, "and must remain the active one");
        }

        std::printf("  workplanes: Top/Bottom->XZ  Front/Back->XY  Left/Right->YZ\n");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE ORIENTATION IS DERIVED FROM THE CAMERA, so reaching a Front view by dragging and by
    //    clicking the cube's Front face land on the same plane. The inverse must stay an inverse.
    //--------------------------------------------------------------------------------------------
    {
        const ViewportOrientation Every[] = {
            ViewportOrientation::Top,   ViewportOrientation::Bottom,
            ViewportOrientation::Front, ViewportOrientation::Back,
            ViewportOrientation::Left,  ViewportOrientation::Right,
        };

        for (const ViewportOrientation Facing : Every)
        {
            double Yaw   = 0.0;
            double Pitch = 0.0;
            OrientationYawPitch(Facing, Yaw, Pitch);
            Require(ResolveCameraOrientation(Yaw, Pitch) == Facing,
                    "the angles a view flies to must resolve back to that same view");
        }

        // 🔴 OFF AXIS IS NOT AN AXIS VIEW. Answering one here would yank the active plane about as
        //    the artist merely orbited.
        Require(ResolveCameraOrientation(30.0, 20.0) == ViewportOrientation::Isometric,
                "an off-axis camera must resolve to Isometric");
        Require(ResolveCameraOrientation(45.0, -5.0) == ViewportOrientation::Isometric,
                "and so must a camera part-way between two views");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE PROJECTION EASES BETWEEN ITS TWO ENDS RATHER THAN CUTTING. Pressing Ortho swapped the
    //    projection between ticks and the whole scene jumped.
    //--------------------------------------------------------------------------------------------
    {
        const PlaneExtent Leaf = { 0.0f, 0.0f, 800.0f, 600.0f };
        ViewFrame Frame;
        Frame.Eye     = { 0.0, 0.0, -10.0 };
        Frame.Right   = { 1.0, 0.0, 0.0 };
        Frame.Up      = { 0.0, 1.0, 0.0 };
        Frame.Forward = { 0.0, 0.0, 1.0 };

        const SpatialPoint Subject = { 30.0, 20.0, 5.0 };
        const double Fov = 60.0, Scale = 8.0, Focus = 10.0;

        // 🔴 THE ENDS MUST BE THE REAL PROJECTIONS, EXACTLY. A blend that merely approaches them
        //    would jump on the frame it snapped to the true one -- the very defect being fixed.
        float WantX = 0.0f, WantY = 0.0f, GotX = 0.0f, GotY = 0.0f;
        Require(ProjectThroughFrame(Frame, Leaf, Fov, Subject, WantX, WantY),
                "the perspective end must project");

        ProjectionTransit Transit;
        Transit.Travelled = 0.0;
        Require(ProjectThroughTransit(Frame, Leaf, Transit, Fov, Scale, Focus, Subject, GotX, GotY),
                "a transit at rest must project");
        Require(std::fabs(WantX - GotX) < 1.0e-4 && std::fabs(WantY - GotY) < 1.0e-4,
                "an unstarted transit must BE the perspective projection, not merely resemble it");

        const SpatialDirection ToSubject = Difference(Frame.Eye, Subject);
        const double OrthoX = Leaf.MinimumX + Leaf.Width() * 0.5 + Dot(ToSubject, Frame.Right) * Scale;
        const double OrthoY = Leaf.MinimumY + Leaf.Height() * 0.5 - Dot(ToSubject, Frame.Up) * Scale;

        Transit.Travelled = 1.0;
        Require(ProjectThroughTransit(Frame, Leaf, Transit, Fov, Scale, Focus, Subject, GotX, GotY),
                "a completed transit must project");
        Require(std::fabs(OrthoX - GotX) < 1.0e-4 && std::fabs(OrthoY - GotY) < 1.0e-4,
                "a completed transit must BE the orthographic projection");

        // 🔴 AND THE PATH BETWEEN THEM MUST BE A PATH, not a jump. Every step moves the point
        //    towards the far end and never past either end -- which a cross-fade of two finished
        //    pictures would not guarantee.
        double Previous = static_cast<double>(WantX);
        const double Lowest  = std::min(static_cast<double>(WantX), OrthoX) - 1.0e-3;
        const double Highest = std::max(static_cast<double>(WantX), OrthoX) + 1.0e-3;
        double Largest = 0.0;

        for (std::uint32_t Step = 1u; Step <= 100u; ++Step)
        {
            Transit.Travelled = static_cast<double>(Step) / 100.0;
            Require(ProjectThroughTransit(Frame, Leaf, Transit, Fov, Scale, Focus, Subject, GotX, GotY),
                    "every step of the transit must project");
            Require(GotX <= Previous + 1.0e-3, "the transit must not reverse part-way");
            Require(GotX >= Lowest && GotX <= Highest, "and must never overshoot either end");
            Largest = std::max(Largest, std::fabs(static_cast<double>(GotX) - Previous));
            Previous = static_cast<double>(GotX);
        }

        // 🔴 NO SINGLE FRAME MAY CARRY MOST OF THE MOVE. That is what a snap IS: one step doing all
        //    the travel. A hundredth of the way along must move a small fraction of the distance.
        const double Whole = std::fabs(OrthoX - static_cast<double>(WantX));
        Require(Largest < Whole * 0.1,
                "no single step of the transit may jump a tenth of the whole distance");

        std::printf("  projection transit: %.1f px in 100 steps, largest step %.1f px\n",
                    Whole, Largest);
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE TRANSIT TAKES TIME AND REVERSES FROM WHEREVER IT STANDS.
    //--------------------------------------------------------------------------------------------
    {
        ProjectionTransit Transit;
        Require(!Transit.Parallel(), "a viewport opens in perspective");

        std::uint32_t Ticks = 0u;
        while (!Transit.Parallel() && Ticks < 1000u)
        {
            AdvanceProjectionTransit(Transit, 1.0 / 60.0, true);
            ++Ticks;
        }
        Require(Transit.Parallel(), "the transit must complete");
        Require(Ticks > 4u, "and must take enough frames to be seen as movement, not a cut");

        // 🔴 Pressing the button again mid-flight eases BACK from where it stands rather than
        //    snapping to the far end first.
        Transit.Travelled = 0.5;
        AdvanceProjectionTransit(Transit, 1.0 / 60.0, false);
        Require(Transit.Travelled < 0.5 && Transit.Travelled > 0.0,
                "a reversed transit must ease back from the middle, not jump");

        // ⚠️ A stalled frame must not teleport the projection across.
        ProjectionTransit Stalled;
        AdvanceProjectionTransit(Stalled, 30.0, true);
        Require(Stalled.Travelled < 1.0, "one very long frame must not complete the whole transit");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A SHAPE THAT CLOSES IS A REGION AND SHADES. A closed polyline committed as separate lines
    //    that happened to meet: nothing knew it enclosed anything, so it could not be shaded and
    //    could not be extruded or lofted.
    //--------------------------------------------------------------------------------------------
    {
        struct Shape
        {
            const char*               Naming;
            std::vector<SpatialPoint> Anchors;
            bool                      Closed;
            bool                      Construction;
            bool                      Shades;
        };

        const std::vector<SpatialPoint> Square = {
            { 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 },
            { 0.0, 0.0, 100.0 }, { 0.0, 0.0, 0.0 } };

        // 📝 An L and a star: both deeply concave, both of which a triangle fan fills WRONGLY and
        //    the old code therefore refused to fill at all.
        const std::vector<SpatialPoint> Bent = {
            { 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, { 100.0, 0.0, 40.0 }, { 40.0, 0.0, 40.0 },
            { 40.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 }, { 0.0, 0.0, 0.0 } };

        const std::vector<SpatialPoint> Star = {
            { 0.0, 0.0, 100.0 },   { 22.0, 0.0, 31.0 },   { 95.0, 0.0, 31.0 },
            { 36.0, 0.0, -12.0 },  { 59.0, 0.0, -81.0 },  { 0.0, 0.0, -38.0 },
            { -59.0, 0.0, -81.0 }, { -36.0, 0.0, -12.0 }, { -95.0, 0.0, 31.0 },
            { -22.0, 0.0, 31.0 },  { 0.0, 0.0, 100.0 } };

        const std::vector<SpatialPoint> Open = {
            { 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, { 100.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 } };

        const Shape Every[] = {
            { "closed square",        Square, true,  false, true  },
            { "toggle off",           Square, false, false, false },
            { "construction",         Square, true,  true,  false },
            { "an open run",          Open,   true,  false, false },
            { "a concave L",          Bent,   true,  false, true  },
            { "a five-pointed star",  Star,   true,  false, true  },
        };

        for (const Shape& Subject : Every)
        {
            SketchStructure           Sketch;
            WorkspaceRecordStructure  Records;
            WorkspaceRevisionSequence Revisions;
            WorkspaceNameIndex        Naming;
            WorkplaneCatalogue        Workplanes;
            SketchPlacement           Tool;
            WorkspaceRecordName       Pending;

            Sketch.DeclarePlane({ Workplanes.Active().Origin,
                                  Workplanes.Active().Normal,
                                  Workplanes.Active().Along });

            Tool.Declare(SketchSubject::Polyline, PlacementMethod::Extent, Subject.Construction);
            Tool.DeclareClosedProfile(Subject.Closed);

            // 📝 Every corner is a plain press; Enter afterwards ends the run WITHOUT placing a
            //    further point, which is what it now does.
            for (const SpatialPoint& Anchor : Subject.Anchors)
            {
                Tool.Hover(Anchor, {});
                static_cast<void>(Tool.Anchor(false));
            }
            static_cast<void>(Tool.Anchor(true));

            const SealedPlacement Sealed = Tool.Seal();
            Require(Sealed.ClosedProfile == Subject.Closed,
                    "the closed-profile choice must reach the commit");

            const Deliver<WorkspaceRecordName> Committed =
                CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
            Require(Committed.Resolved, "the shape must commit");
            AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

            WorkspaceCadPacket Delivered;
            static_cast<void>(ProjectSketchRendering(Sketch, Records, Delivered));

            Require(Delivered.SegmentCount >= 3u, "every one of them draws its outline");

            if (Subject.Shades)
            {
                Require(Sketch.Profiles().size() == 1u,
                        "a closed shape must seal as a region, so it can be extruded");
                Require(Delivered.FillCount > 0u,
                        "and must shade, concave or convex");
            }
            else
            {
                Require(Sketch.Profiles().empty(),
                        "an open, unwanted or construction shape must NOT become a region");
                Require(Delivered.FillCount == 0u, "and must not shade");
            }
        }

        std::printf("  closed profiles: square, concave L and star all shade; open stays open\n");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE NEAR-PLANE CLIPPER MUST HOLD FOR EVERY KIND OF CLOSED SHAPE, not only a rectangle. By the
    //    time the CAD pass sees a profile, polygonal and curved loops have all become fill triangles in
    //    one packet; clipping per fill triangle is therefore the generic fix, provided representative
    //    shapes from both families survive it.
    //--------------------------------------------------------------------------------------------
    {
        struct ClosedShape
        {
            SketchSubject             Subject;
            PlacementMethod           Method;
            std::vector<SpatialPoint> Anchors;
            std::uint32_t             Resolution;
            const char*               Naming;
        };

        const ClosedShape Every[] = {
            { SketchSubject::Polygon, PlacementMethod::Centred,
              { { 0.0, 0.0, 0.0 }, { 120.0, 0.0, 45.0 } }, 8u, "polygon" },
            { SketchSubject::Circle, PlacementMethod::Centred,
              { { 0.0, 0.0, 0.0 }, { 90.0, 0.0, 40.0 } }, PolygonSideDefault, "circle" },
            { SketchSubject::Ellipse, PlacementMethod::Centred,
              { { 0.0, 0.0, 0.0 }, { 150.0, 0.0, 60.0 } }, PolygonSideDefault, "ellipse" },
            { SketchSubject::Slot, PlacementMethod::Extent,
              { { -100.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, { 100.0, 0.0, 50.0 } },
              PolygonSideDefault, "slot" },
        };

        const PlaneExtent Leaf = { 0.0f, 0.0f, 800.0f, 600.0f };
        const DrawableScale Unscaled = {};

        for (const ClosedShape& Subject : Every)
        {
            SketchStructure           Sketch;
            WorkspaceRecordStructure  Records;
            WorkspaceRevisionSequence Revisions;
            WorkspaceNameIndex        Naming;
            WorkplaneCatalogue        Workplanes;
            SketchPlacement           Tool;
            WorkspaceRecordName       Pending;

            Sketch.DeclarePlane({ Workplanes.Active().Origin,
                                  Workplanes.Active().Normal,
                                  Workplanes.Active().Along });

            Tool.Declare(Subject.Subject, Subject.Method, false);
            while (Tool.Resolution() < Subject.Resolution)
                static_cast<void>(Tool.Resolve(1.0f));
            for (const SpatialPoint& Anchor : Subject.Anchors)
            {
                Tool.Hover(Anchor, {});
                static_cast<void>(Tool.Anchor(false));
            }

            const auto Demand = [&](bool Held, const char* Suffix)
            {
                const std::string Claim = std::string(Subject.Naming) + Suffix;
                Require(Held, Claim.c_str());
            };

            const SealedPlacement Sealed = Tool.Seal();
            const Deliver<WorkspaceRecordName> Committed =
                CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
            Demand(Committed.Resolved, " must commit as a closed shape");
            AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

            WorkspaceCadPacket Delivered;
            static_cast<void>(ProjectSketchRendering(Sketch, Records, Delivered));
            Demand(Delivered.FillCount > 0u,
                   " must reach the CAD packet as shaded fill triangles");

            const SpatialBasis Basis = ResolveSketchBasis(Sketch);
            const ViewportStanding Skimming = ResolveOrbitStandingFromFree(
                { 0.0, 0.08, 0.0 }, 0.0, -2.0, Basis);
            const WorkspaceCadProjection Rows = ResolveCadProjection(
                Basis, Skimming, true, Leaf, Unscaled,
                static_cast<std::uint32_t>(Leaf.Width()),
                static_cast<std::uint32_t>(Leaf.Height()));

            const auto Projected = [&](float Along, float Across)
            {
                WorkspaceCadProjectedPoint Point;
                Point.X = Rows.Projection0[0] + Along * Rows.Projection1[0] + Across * Rows.Projection2[0];
                Point.Y = Rows.Projection0[1] + Along * Rows.Projection1[1] + Across * Rows.Projection2[1];
                Point.W = Rows.Projection0[3] + Along * Rows.Projection1[3] + Across * Rows.Projection2[3];
                return Point;
            };

            bool AnyVisible = false;
            bool AnyClipped = false;
            std::uint32_t Quads = 0u;
            for (Unsigned32 Index = 0u; Index < Delivered.FillCount; ++Index)
            {
                const WorkspaceCadFillTriangle& Triangle = Delivered.Fills[Index];
                const WorkspaceCadProjectedPoint First  = Projected(Triangle.Along0, Triangle.Across0);
                const WorkspaceCadProjectedPoint Second = Projected(Triangle.Along1, Triangle.Across1);
                const WorkspaceCadProjectedPoint Third  = Projected(Triangle.Along2, Triangle.Across2);
                const bool Mixed = WorkspaceCadProjectedFront(First) != WorkspaceCadProjectedFront(Second)
                                || WorkspaceCadProjectedFront(Second) != WorkspaceCadProjectedFront(Third);
                WorkspaceCadProjectedPoint Clipped[4] = {};
                const Unsigned32 Count = ClipWorkspaceCadFillTriangleNear(First, Second, Third, Clipped);
                Demand(Count <= 4u,
                       " fill clipping must produce at most a quad");
                if (Count != 0u)
                    AnyVisible = true;
                if (Mixed)
                    AnyClipped = true;
                if (Count == 4u)
                    ++Quads;
                for (Unsigned32 Corner = 0u; Corner < Count; ++Corner)
                    Demand(Clipped[Corner].W >= WorkspaceCadNearDepth,
                           " clipped fill corners must land on or ahead of the near plane");
            }

            Demand(AnyVisible,
                   " must still show some filled surface after near clipping");
            Demand(AnyClipped,
                   " must exercise the near-plane clipper in a skimming view");
            Demand(Quads > 0u,
                   " must produce at least one clipped quad for the two-triangle path");
        }

        std::printf("  near-plane clipping holds for polygon, circle, ellipse and slot fills\n");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 CURVE OUTLINES CROSS THE SAME NEAR PLANE. Bezier, basis spline, NURBS and Hermite do not
    //    reach the CAD pass as special cases; they are tessellated into the packet's ordinary segment
    //    list. So the segment clipper must preserve their visible prefixes too, or a camera skimming
    //    the plane drops the very curve families that motivated the viewport rewrite.
    //--------------------------------------------------------------------------------------------
    {
        struct CurveSubject
        {
            SketchSubject             Subject;
            PlacementMethod           Method;
            std::vector<SpatialPoint> Anchors;
            const char*               Naming;
        };

        const CurveSubject Every[] = {
            { SketchSubject::Bezier, PlacementMethod::Extent,
              { { -140.0, 0.0, -30.0 }, { -40.0, 0.0, 120.0 }, { 60.0, 0.0, -120.0 }, { 160.0, 0.0, 40.0 } },
              "Bezier" },
            { SketchSubject::BasisSpline, PlacementMethod::Extent,
              { { -150.0, 0.0, -50.0 }, { -90.0, 0.0, 130.0 }, { 0.0, 0.0, -140.0 }, { 110.0, 0.0, 120.0 }, { 180.0, 0.0, -20.0 } },
              "basis spline" },
            { SketchSubject::RationalSpline, PlacementMethod::Extent,
              { { -150.0, 0.0, -40.0 }, { -80.0, 0.0, 140.0 }, { 10.0, 0.0, -150.0 }, { 120.0, 0.0, 110.0 }, { 190.0, 0.0, -10.0 } },
              "NURBS curve" },
            { SketchSubject::Hermite, PlacementMethod::Extent,
              { { -170.0, 0.0, -30.0 }, { -70.0, 0.0, 110.0 }, { 20.0, 0.0, -130.0 }, { 110.0, 0.0, 110.0 }, { 190.0, 0.0, -20.0 } },
              "Hermite" },
        };

        const PlaneExtent Leaf = { 0.0f, 0.0f, 800.0f, 600.0f };
        const DrawableScale Unscaled = {};

        for (const CurveSubject& Subject : Every)
        {
            SketchStructure           Sketch;
            WorkspaceRecordStructure  Records;
            WorkspaceRevisionSequence Revisions;
            WorkspaceNameIndex        Naming;
            WorkplaneCatalogue        Workplanes;
            SketchPlacement           Tool;
            WorkspaceRecordName       Pending;

            Sketch.DeclarePlane({ Workplanes.Active().Origin,
                                  Workplanes.Active().Normal,
                                  Workplanes.Active().Along });

            const auto Demand = [&](bool Held, const char* Suffix)
            {
                const std::string Claim = std::string(Subject.Naming) + Suffix;
                Require(Held, Claim.c_str());
            };

            Tool.Declare(Subject.Subject, Subject.Method, false);
            for (const SpatialPoint& Anchor : Subject.Anchors)
            {
                Tool.Hover(Anchor, {});
                static_cast<void>(Tool.Anchor(false));
            }

            const SealedPlacement Sealed = Tool.Seal();
            const Deliver<WorkspaceRecordName> Committed =
                CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
            Demand(Committed.Resolved, " must commit as a curve");
            AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

            WorkspaceCadPacket Delivered;
            static_cast<void>(ProjectSketchRendering(Sketch, Records, Delivered));
            Demand(Delivered.SegmentCount > 0u,
                   " must reach the CAD packet as outline segments");

            const SpatialBasis Basis = ResolveSketchBasis(Sketch);
            const ViewportStanding Skimming = ResolveOrbitStandingFromFree(
                { 0.0, 0.08, 0.0 }, 0.0, -2.0, Basis);
            const WorkspaceCadProjection Rows = ResolveCadProjection(
                Basis, Skimming, true, Leaf, Unscaled,
                static_cast<std::uint32_t>(Leaf.Width()),
                static_cast<std::uint32_t>(Leaf.Height()));

            const auto Projected = [&](float Along, float Across)
            {
                WorkspaceCadProjectedPoint Point;
                Point.X = Rows.Projection0[0] + Along * Rows.Projection1[0] + Across * Rows.Projection2[0];
                Point.Y = Rows.Projection0[1] + Along * Rows.Projection1[1] + Across * Rows.Projection2[1];
                Point.W = Rows.Projection0[3] + Along * Rows.Projection1[3] + Across * Rows.Projection2[3];
                return Point;
            };

            bool AnyVisible = false;
            bool AnyClipped = false;
            for (Unsigned32 Index = 0u; Index < Delivered.SegmentCount; ++Index)
            {
                const WorkspaceCadSegment& Segment = Delivered.Segments[Index];
                WorkspaceCadProjectedPoint Start = Projected(Segment.Along0, Segment.Across0);
                WorkspaceCadProjectedPoint End   = Projected(Segment.Along1, Segment.Across1);
                const bool Mixed = WorkspaceCadProjectedFront(Start) != WorkspaceCadProjectedFront(End);
                const bool Survived = ClipWorkspaceCadSegmentNear(Start, End);
                if (Survived)
                    AnyVisible = true;
                if (Mixed)
                {
                    AnyClipped = true;
                    Demand(Start.W >= WorkspaceCadNearDepth && End.W >= WorkspaceCadNearDepth,
                           " clipped segment endpoints must land on or ahead of the near plane");
                }
            }

            Demand(AnyVisible,
                   " must still show some outline after near clipping");
            Demand(AnyClipped,
                   " must exercise the segment near-plane clipper in a skimming view");
        }

        std::printf("  near-plane clipping holds for Bezier, basis spline, NURBS and Hermite outlines\n");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 THE FILL COVERS THE SHAPE AND NOTHING ELSE. A fan across a concave outline lays triangles
    //    over the notches -- it would still report a fill count, so counting is not enough.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure           Sketch;
        WorkspaceRecordStructure  Records;
        WorkspaceRevisionSequence Revisions;
        WorkspaceNameIndex        Naming;
        WorkplaneCatalogue        Workplanes;
        SketchPlacement           Tool;
        WorkspaceRecordName       Pending;

        Sketch.DeclarePlane({ Workplanes.Active().Origin,
                              Workplanes.Active().Normal,
                              Workplanes.Active().Along });

        // 📝 The L again: 100x100 with a 60x60 bite out of the far corner, so the true area is
        //    10000 - 3600 = 6400. A fan from vertex 0 would cover the bite as well.
        const std::vector<SpatialPoint> Bent = {
            { 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 }, { 100.0, 0.0, 40.0 }, { 40.0, 0.0, 40.0 },
            { 40.0, 0.0, 100.0 }, { 0.0, 0.0, 100.0 }, { 0.0, 0.0, 0.0 } };

        Tool.Declare(SketchSubject::Polyline, PlacementMethod::Extent, false);
        Tool.DeclareClosedProfile(true);
        for (const SpatialPoint& Corner : Bent)
        {
            Tool.Hover(Corner, {});
            static_cast<void>(Tool.Anchor(false));
        }
        static_cast<void>(Tool.Anchor(true));

        const SealedPlacement Sealed = Tool.Seal();
        const Deliver<WorkspaceRecordName> Committed =
            CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
        AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

        WorkspaceCadPacket Delivered;
        static_cast<void>(ProjectSketchRendering(Sketch, Records, Delivered));

        // 🔴 The triangles must sum to the shape's own area. Too much means the fill spilled across
        //    the notch; too little means part of the shape is hollow.
        double Covered = 0.0;
        for (Unsigned32 Index = 0u; Index < Delivered.FillCount; ++Index)
        {
            const WorkspaceCadFillTriangle& Triangle = Delivered.Fills[Index];
            Covered += std::fabs(
                (static_cast<double>(Triangle.Along1) - Triangle.Along0)
                    * (static_cast<double>(Triangle.Across2) - Triangle.Across0)
              - (static_cast<double>(Triangle.Along2) - Triangle.Along0)
                    * (static_cast<double>(Triangle.Across1) - Triangle.Across0)) * 0.5;
        }

        Require(Delivered.FillCount == 4u, "an L of six corners clips into four triangles");
        Require(std::fabs(Covered - 6400.0) < 1.0,
                "the fill must cover the shape's own area, not the notch as well");

        std::printf("  concave fill: 4 triangles covering %.0f units (the L's true area is 6400)\n",
                    Covered);
    }


    //--------------------------------------------------------------------------------------------
    // 🔴 WHAT IS PREVIEWED IS WHAT IS COMMITTED. Every defect in this group was the same defect:
    //    the preview computed one shape and the commit computed another, so the figure the artist
    //    dragged out jumped into a different one the instant they released. An ellipse dragged to
    //    (150,60) previewed 161.6 long tilted 22 degrees and committed 150 by 60 lying flat; a
    //    polygon previewed a circle and committed a hexagon rotated off the drag. The two are
    //    compared as SHAPES -- the tessellated outlines must lie on top of one another -- because
    //    comparing parameters would pass for two shapes that merely happen to share a radius.
    //--------------------------------------------------------------------------------------------
    {
        struct Agreement
        {
            SketchSubject             Subject;
            PlacementMethod           Method;
            std::vector<SpatialPoint> Anchors;
            const char*               Naming;
        };

        const Agreement Every[] = {
            { SketchSubject::Ellipse,   PlacementMethod::Centred,
              { { 0.0, 0.0, 0.0 }, { 150.0, 0.0, 60.0 } }, "ellipse" },
            { SketchSubject::Circle,    PlacementMethod::Centred,
              { { 0.0, 0.0, 0.0 }, { 90.0, 0.0, 40.0 } },  "circle" },
            { SketchSubject::Polygon,   PlacementMethod::Centred,
              { { 0.0, 0.0, 0.0 }, { 120.0, 0.0, 45.0 } }, "polygon" },
            { SketchSubject::Rectangle, PlacementMethod::Extent,
              { { -20.0, 0.0, -10.0 }, { 130.0, 0.0, 95.0 } }, "rectangle" },
            { SketchSubject::Slot,      PlacementMethod::Extent,
              { { 0.0, 0.0, 0.0 }, { 200.0, 0.0, 0.0 }, { 200.0, 0.0, 50.0 } }, "slot" },
        };

        for (const Agreement& Subject : Every)
        {
            SketchStructure           Sketch;
            WorkspaceRecordStructure  Records;
            WorkspaceRevisionSequence Revisions;
            WorkspaceNameIndex        Naming;
            WorkplaneCatalogue        Workplanes;
            SketchPlacement           Tool;
            WorkspaceRecordName       Pending;

            Sketch.DeclarePlane({ Workplanes.Active().Origin,
                                  Workplanes.Active().Normal,
                                  Workplanes.Active().Along });

            // ① The preview, as the host draws it on the frame before the last click.
            const std::vector<SpatialPoint> Held(Subject.Anchors.begin(), Subject.Anchors.end() - 1u);
            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(Subject.Subject, Held, Subject.Anchors.back(), Spans,
                                   PolygonSideDefault);
            Require(!Spans.empty(), "every one of these subjects must preview");

            // ⚠️ `AppendCurvePolyline` CLEARS its output despite the name, so several spans must be
            //    gathered through a scratch vector or only the last one survives.
            std::vector<SpatialPoint> Previewed;
            for (const CurveSpecification& Span : Spans)
            {
                std::vector<SpatialPoint> Scratch;
                AppendCurvePolyline(Span, Scratch, 64u);
                Previewed.insert(Previewed.end(), Scratch.begin(), Scratch.end());
            }
            Require(Previewed.size() >= 4u, "and must preview as a real outline");

            // ② The commit, from those same anchors.
            // 📝 Every anchor is a plain press: these shapes complete on a count, exactly as the
            //    host drives them.
            Tool.Declare(Subject.Subject, Subject.Method, false);
            for (const SpatialPoint& Anchor : Subject.Anchors)
            {
                Tool.Hover(Anchor, {});
                static_cast<void>(Tool.Anchor(false));
            }

            const SealedPlacement Sealed = Tool.Seal();
            const Deliver<WorkspaceRecordName> Committed =
                CommitPlacement(Naming, Sketch, Records, Revisions, Sealed);
            Require(Committed.Resolved, "and must commit");
            AdoptCommittedShape(Sealed.Subject, Naming, Sketch, Records, Revisions, Committed, Pending);

            std::vector<SpatialPoint> Delivered;
            for (const DeclaredSketchCurve& Curve : Sketch.Curves())
            {
                std::vector<SpatialPoint> Scratch;
                AppendCurvePolyline(Curve.Geometry, Scratch, 64u);
                Delivered.insert(Delivered.end(), Scratch.begin(), Scratch.end());
            }
            Require(Delivered.size() >= 4u, "the committed shape must have an outline");

            // ③ Every previewed point must lie ON the committed outline, and the reverse. One
            //    direction alone would pass for a shape drawn twice over half the figure.
            //
            // ⚠️ MEASURED TO THE NEAREST SEGMENT, NOT THE NEAREST VERTEX. The two outlines are
            //    tessellated independently -- an ellipse previews as one curve and commits as four
            //    quarter arcs -- so their sample points land in different places along the SAME
            //    figure. A vertex-to-vertex distance therefore reports the sampling interval rather
            //    than any disagreement, and reported 7.9 units for two shapes that were identical.
            const auto FromOutline = [](const SpatialPoint& Point,
                                        const std::vector<SpatialPoint>& Outline)
            {
                double Nearest = 1.0e30;
                for (std::size_t Index = 0u; Index + 1u < Outline.size(); ++Index)
                {
                    const SpatialDirection Span  = Difference(Outline[Index], Outline[Index + 1u]);
                    const SpatialDirection Reach = Difference(Outline[Index], Point);
                    const double Length = LengthSquared(Span);

                    double Along = Length > 0.0 ? Dot(Reach, Span) / Length : 0.0;
                    Along = Along < 0.0 ? 0.0 : (Along > 1.0 ? 1.0 : Along);

                    Nearest = std::min(Nearest, LengthSquared(Difference(
                        Added(Outline[Index], Scaled(Span, Along)), Point)));
                }
                return std::sqrt(Nearest);
            };

            double Worst = 0.0;
            for (const SpatialPoint& Point : Previewed)
                Worst = std::max(Worst, FromOutline(Point, Delivered));
            for (const SpatialPoint& Point : Delivered)
                Worst = std::max(Worst, FromOutline(Point, Previewed));

            // 📝 A quarter unit on shapes 150 to 200 units across -- far below anything visible, and
            //    now that the measure is to the outline rather than to its samples, far above what
            //    tessellation noise can produce.
            Require(Worst < 0.25,
                    "the previewed shape and the committed shape must be the same shape");
        }

        std::printf("  preview and commit agree for ellipse, circle, polygon, rectangle and slot\n");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A SLOT'S END CAPS BULGE OUTWARD. Both swept the short way round, so the semicircles were
    //    carved OUT of the slot's body instead of capping its ends -- two crescents biting into it.
    //    The outline still closed, so nothing downstream complained; it simply looked wrong.
    //--------------------------------------------------------------------------------------------
    {
        SketchStructure    Sketch;
        WorkplaneCatalogue Workplanes;
        Sketch.DeclarePlane({ Workplanes.Active().Origin,
                              Workplanes.Active().Normal,
                              Workplanes.Active().Along });

        const double Radius = 50.0;
        static_cast<void>(Sketch.DeclareSlot({ 0.0, 0.0, 0.0 }, { 200.0, 0.0, 0.0 }, Radius));

        // 🔴 The slot runs 0 to 200 along X with radius 50, so its true extent is -50 to 250. Caps
        //    that bulge inward leave it 0 to 200 -- the rectangle alone.
        double Lowest = 1.0e30, Highest = -1.0e30;
        for (const DeclaredSketchCurve& Curve : Sketch.Curves())
        {
            std::vector<SpatialPoint> Outline;
            AppendCurvePolyline(Curve.Geometry, Outline, 48u);
            for (const SpatialPoint& Point : Outline)
            {
                Lowest  = std::min(Lowest, Point.Left);
                Highest = std::max(Highest, Point.Left);
            }
        }

        Require(std::fabs(Lowest + Radius) < 0.5,
                "the start cap must bulge out beyond the axis, not bite into the slot");
        Require(std::fabs(Highest - (200.0 + Radius)) < 0.5,
                "and so must the end cap");

        std::printf("  slot: extent %.0f to %.0f for an axis of 0 to 200, radius 50\n",
                    Lowest, Highest);
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 A SPLINE ENDS AT ITS LAST POINT, NOT BACK AT ITS FIRST. A clamped basis is a half-open
    //    interval, so at exactly the last knot every basis function was zero, the weight sum with
    //    it, and the evaluator's divide-by-zero guard returned the FIRST control point. The curve
    //    therefore leapt home on its final step and drew a long straight line from end to start
    //    that was on screen the whole time the artist was drawing.
    //--------------------------------------------------------------------------------------------
    {
        const std::vector<SpatialPoint> Held = {
            { 0.0, 0.0, 0.0 }, { 40.0, 0.0, 90.0 }, { 120.0, 0.0, 120.0 }, { 200.0, 0.0, 40.0 } };
        const SpatialPoint Hover = { 260.0, 0.0, 100.0 };

        const SketchSubject Splines[] = { SketchSubject::Bezier, SketchSubject::BasisSpline,
                                          SketchSubject::RationalSpline };

        for (const SketchSubject Subject : Splines)
        {
            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(Subject, Held, Hover, Spans);
            Require(!Spans.empty(), "every spline must preview");

            std::vector<SpatialPoint> Outline;
            AppendCurvePolyline(Spans.front(), Outline, 64u);
            Require(Outline.size() >= 4u, "and must tessellate");

            // 🔴 The last point must be the pointer, which for a clamped spline is where the curve
            //    ends. Landing on the FIRST control point instead is the defect.
            Require(std::sqrt(LengthSquared(Difference(Outline.back(), Hover))) < 1.0,
                    "a spline must end under the pointer, not leap back to its start");
            Require(std::sqrt(LengthSquared(Difference(Outline.back(), Held.front()))) > 1.0,
                    "and must not end where it began");

            // 🔴 NOR MAY IT PASS THROUGH THE START ON ITS WAY. A single stray sample at the origin
            //    draws the same phantom line as an endpoint there.
            std::uint32_t Strays = 0u;
            for (std::size_t Index = 1u; Index < Outline.size(); ++Index)
                if (std::sqrt(LengthSquared(Difference(Outline[Index], Held.front()))) < 1.0)
                    ++Strays;
            Require(Strays == 0u, "no interior sample may sit on the curve's own start");
        }

        std::printf("  splines end under the pointer, with no closing chord\n");
    }

    // ⭐ A BEZIER MUST PASS THROUGH EVERY POINT THE ARTIST CLICKED, AT ANY LENGTH.
    //
    // 🔴 It did not. `EvaluateBezier` ran de Casteljau across ALL control points, so N anchors built
    //    ONE Bezier of degree N-1 rather than a chain. Such a curve interpolates its first and last
    //    control point and no other, and the pull of the interior ones weakens as the degree climbs:
    //    the curve missed its own interior anchors by 40.00 at three anchors, rising monotonically to
    //    42.86 at eight. Every added point made it smoother and less like the drawing.
    //
    // 📝 Measured at SIX lengths, because the defect was not that the curve was wrong at some length
    //    but that it grew worse with every point. A single length could not have told the two apart.
    {
        for (std::uint32_t Count = 3u; Count <= 8u; ++Count)
        {
            std::vector<SpatialPoint> Clicked;
            for (std::uint32_t Index = 0u; Index < Count; ++Index)
                Clicked.push_back({ 40.0 * Index, 0.0, (Index % 2u) ? 80.0 : 0.0 });

            std::vector<SpatialPoint> Held(Clicked.begin(), Clicked.end() - 1u);
            std::vector<CurveSpecification> Spans;
            ResolvePlacementCurves(SketchSubject::Bezier, Held, Clicked.back(), Spans);
            Require(!Spans.empty(), "a Bezier of any length must preview");

            std::vector<SpatialPoint> Outline;
            for (const CurveSpecification& Span : Spans)
            {
                std::vector<SpatialPoint> Scratch;
                AppendCurvePolyline(Span, Scratch, 256u);
                Outline.insert(Outline.end(), Scratch.begin(), Scratch.end());
            }

            double Worst = 0.0;
            for (std::size_t Index = 1u; Index + 1u < Clicked.size(); ++Index)
            {
                double Nearest = 1.0e30;
                for (const SpatialPoint& Sample : Outline)
                    Nearest = std::min(Nearest, LengthSquared(Difference(Clicked[Index], Sample)));
                Worst = std::max(Worst, std::sqrt(Nearest));
            }

            // 📝 One unit against anchors 40 apart. The defect measured 40 to 43 -- two orders of
            //    magnitude clear -- while honest tessellation error at 256 steps is under half a unit.
            Require(Worst < 1.0,
                    "a Bezier must pass through every anchor, however many are placed");
        }

        std::printf("  a Bezier passes through its anchors at 3 to 8 points\n");
    }

    //--------------------------------------------------------------------------------------------
    // 🔴 ENTER ENDS THE SHAPE, IT DOES NOT PLACE A POINT. A terminating press anchored the hover
    //    first and asked about termination afterwards, so finishing a polyline left a stray anchor
    //    wherever the pointer happened to rest -- an extra leg the artist never clicked.
    //--------------------------------------------------------------------------------------------
    {
        SketchPlacement Tool;
        Tool.Declare(SketchSubject::Polyline, PlacementMethod::Extent, false);

        for (std::uint32_t Click = 0u; Click < 4u; ++Click)
        {
            Tool.Hover({ 40.0 * Click, 0.0, 0.0 }, {});
            static_cast<void>(Tool.Anchor(false));
        }
        Require(Tool.Anchors().size() == 4u, "four clicks take four anchors");

        // 📝 The pointer has moved on since the last click, as it always has in practice.
        Tool.Hover({ 500.0, 0.0, 300.0 }, {});
        Require(Tool.Anchor(true) == PlacementArrival::Complete, "Enter must finish the polyline");
        Require(Tool.Anchors().size() == 4u,
                "and must NOT leave a fifth anchor under the resting pointer");

        // 🔴 But Enter on a shape that has not got enough anchors yet must still take the point that
        //    completes it, or the artist would press Enter and see nothing happen.
        SketchPlacement Short;
        Short.Declare(SketchSubject::Polyline, PlacementMethod::Extent, false);
        Short.Hover({ 0.0, 0.0, 0.0 }, {});
        static_cast<void>(Short.Anchor(false));
        Short.Hover({ 90.0, 0.0, 0.0 }, {});
        Require(Short.Anchor(true) == PlacementArrival::Complete,
                "Enter must complete a polyline that needs this point");
        Require(Short.Anchors().size() == 2u, "taking the point that completes it");
    }

    std::printf("[SketchDrawingProof] %u claims, %u failures\n", Claims, Failures);
    return Failures == 0u ? 0 : 1;
}
