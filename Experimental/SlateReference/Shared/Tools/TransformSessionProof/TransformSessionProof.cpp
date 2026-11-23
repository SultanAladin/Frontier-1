//============================================================================================================================================
//                                                     TRANSFORMSESSIONPROOF.CPP
//============================================================================================================================================
// 🧩 Proves dragging a selection: what moves, about what, by how much, and that cancelling puts every
//    point back exactly where it was.

#include "SlateWorkspace/Discipline/TransformSession/Api/TransformSession.h"

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

bool Near(double Left, double Right, double Tolerance = 1.0e-9)
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right, double Tolerance = 1.0e-9)
{
    return Near(Left.Left, Right.Left, Tolerance)
        && Near(Left.Up, Right.Up, Tolerance)
        && Near(Left.Forward, Right.Forward, Tolerance);
}

/// 🧩 A sketch on the ground plane, viewed from straight above, with a line to drag.
struct Bench
{
    SketchStructure           Sketch;
    WorkspaceRecordStructure  Records;
    WorkspaceRevisionSequence Revisions;

    SpatialBasis      Basis = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    ViewportStanding  View;
    PlaneExtent       Extent = { 0.0f, 0.0f, 800.0f, 600.0f };

    SketchCurveName     Line   = {};
    WorkspaceRecordName Record = {};

    Bench(double StartAlong = -20.0, double StartAcross = 0.0,
          double EndAlong = 20.0, double EndAcross = 0.0)
    {
        SketchPlane Ground;
        Ground.Origin         = { 0.0, 0.0, 0.0 };
        Ground.Normal         = { 0.0, 1.0, 0.0 };
        Ground.AlongDirection = { 1.0, 0.0, 0.0 };
        Sketch.DeclarePlane(Ground);

        View.Focus      = { 0.0, 0.0, 0.0 };
        View.OrthoScale = 4.0;
        View.Distance   = 240.0;

        Line = Sketch.DeclareLine({ StartAlong, 0.0, StartAcross }, { EndAlong, 0.0, EndAcross });

        WorkspaceRecord Held = {};
        Held.Subject     = WorkspaceRecordSubject::OpenCurve;
        Held.SketchCurve = Line;
        Held.Naming      = "Line 1";
        Record = Records.Declare(Held);
    }

    SketchPick CurvePick() const
    {
        SketchPick Pick = {};
        Pick.Subject = SketchPickSubject::Curve;
        Pick.Curve   = Line;
        Pick.Record  = Record;
        ResolveCurvePivot(Sketch, Line, Pick.Position);
        return Pick;
    }

    /// 🧩 The screen position a plane point projects to, so a drag can be aimed at a known place.
    void ScreenOf(double Along, double Across, float& X, float& Y) const
    {
        ProjectViewportPoint(Basis, View, false, Extent, Along, Across, X, Y);
    }

    std::vector<SpatialPoint> Positions() const
    {
        std::vector<SketchPointPlacement> Points;
        ResolveSketchPoints(Sketch, Line, Points);
        std::vector<SpatialPoint> Held;
        for (const SketchPointPlacement& Point : Points)
            Held.push_back(Point.Position);
        return Held;
    }
};

//========================================================================================================
// 1. WHAT A SELECTION OFFERS UP
//========================================================================================================

void ProveResolution()
{
    std::printf("\n1. What a transform will move\n");

    Bench Stage;

    {
        WorkspaceRecordName Record = {};
        SpatialPoint Pivot = {};
        std::vector<SketchPlacementSubject> Placements;
        Claim(ResolveTransformPlacements(Stage.Sketch, Stage.Records, Stage.CurvePick(),
                                         Record, Pivot, Placements),
              "a curve selection offers placements");
        Claim(Placements.size() >= 2u, "at least the line's two ends");
        Claim(SamePoint(Pivot, { 0.0, 0.0, 0.0 }), "and the pivot is the line's midpoint");
        Claim(Record.IssuedIndex == Stage.Record.IssuedIndex, "carrying the record it came from");
    }

    // 📝 A point selection moves exactly itself, about itself.
    {
        SketchPick Pick = {};
        Pick.Subject  = SketchPickSubject::Point;
        Pick.Point    = { 258u };
        Pick.Position = { 20.0, 0.0, 0.0 };
        Pick.Record   = Stage.Record;

        WorkspaceRecordName Record = {};
        SpatialPoint Pivot = {};
        std::vector<SketchPlacementSubject> Placements;
        Claim(ResolveTransformPlacements(Stage.Sketch, Stage.Records, Pick, Record, Pivot, Placements),
              "a point selection offers a placement");
        Claim(Placements.size() == 1u, "exactly one - itself");
        Claim(SamePoint(Pivot, Pick.Position), "and it turns about itself");
    }

    // ⚠️ A selection standing at nothing offers nothing.
    {
        WorkspaceRecordName Record = {};
        SpatialPoint Pivot = {};
        std::vector<SketchPlacementSubject> Placements;
        Claim(!ResolveTransformPlacements(Stage.Sketch, Stage.Records, {}, Record, Pivot, Placements),
              "an empty selection offers nothing to move");
        Claim(Placements.empty(), "and the list is left empty rather than stale");
    }
}

/// 🧩 Three connected lines: left, bottom and right of a square, so dragging the bottom edge must bring
///    the shared corners of the neighbouring edges with it.
struct ConnectedBench
{
    SketchStructure           Sketch;
    WorkspaceRecordStructure  Records;
    WorkspaceRevisionSequence Revisions;

    SpatialBasis      Basis = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    ViewportStanding  View;
    PlaneExtent       Extent = { 0.0f, 0.0f, 800.0f, 600.0f };

    SketchCurveName Left = {};
    SketchCurveName Bottom = {};
    SketchCurveName Right = {};
    WorkspaceRecordName BottomRecord = {};

    ConnectedBench()
    {
        SketchPlane Ground;
        Ground.Origin = { 0.0, 0.0, 0.0 };
        Ground.Normal = { 0.0, 1.0, 0.0 };
        Ground.AlongDirection = { 1.0, 0.0, 0.0 };
        Sketch.DeclarePlane(Ground);

        View.Focus = { 0.0, 0.0, 0.0 };
        View.OrthoScale = 4.0;
        View.Distance = 240.0;

        Left = Sketch.DeclareLine({ -20.0, 0.0, -20.0 }, { -20.0, 0.0, 20.0 });
        Bottom = Sketch.DeclareLine({ -20.0, 0.0, -20.0 }, { 20.0, 0.0, -20.0 });
        Right = Sketch.DeclareLine({ 20.0, 0.0, -20.0 }, { 20.0, 0.0, 20.0 });

        WorkspaceRecord Held = {};
        Held.Subject = WorkspaceRecordSubject::OpenCurve;
        Held.SketchCurve = Bottom;
        Held.Naming = "Bottom";
        BottomRecord = Records.Declare(Held);
    }

    SketchPick BottomPick() const
    {
        SketchPick Pick = {};
        Pick.Subject = SketchPickSubject::Curve;
        Pick.Curve = Bottom;
        Pick.Record = BottomRecord;
        ResolveCurvePivot(Sketch, Bottom, Pick.Position);
        return Pick;
    }

    void ScreenOf(double Along, double Across, float& X, float& Y) const
    {
        ProjectViewportPoint(Basis, View, false, Extent, Along, Across, X, Y);
    }
};

/// 🧩 A triangle carried ONLY by a closed-profile record. Its corners must still be pickable and moving
///    one corner must keep the loop closed by moving both coincident endpoints that meet there.
struct ProfileTriangleBench
{
    SketchStructure           Sketch;
    WorkspaceRecordStructure  Records;
    WorkspaceRevisionSequence Revisions;

    SpatialBasis      Basis = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    ViewportStanding  View;
    PlaneExtent       Extent = { 0.0f, 0.0f, 800.0f, 600.0f };

    ProfileNameInFeature Profile = {};
    WorkspaceRecordName  ProfileRecord = {};
    SketchCurveName      EdgeA = {};
    SketchCurveName      EdgeB = {};
    SketchCurveName      EdgeC = {};
    SpatialPoint         Corner = {};

    ProfileTriangleBench()
    {
        SketchPlane Ground;
        Ground.Origin = { 0.0, 0.0, 0.0 };
        Ground.Normal = { 0.0, 1.0, 0.0 };
        Ground.AlongDirection = { 1.0, 0.0, 0.0 };
        Sketch.DeclarePlane(Ground);

        View.Focus = { 0.0, 0.0, 0.0 };
        View.OrthoScale = 4.0;
        View.Distance = 240.0;

        const Deliver<ProfileNameInFeature> Triangle =
            Sketch.DeclareRegularPolygon({ 0.0, 0.0, 0.0 }, 24.0, 3u, SpatialDirection{ 1.0, 0.0, 0.0 });
        if (!Triangle.Resolved)
            return;

        Profile = Triangle.Resolve();

        WorkspaceRecord Held = {};
        Held.Subject = WorkspaceRecordSubject::ClosedProfile;
        Held.Profile = Profile;
        Held.Naming = "Triangle";
        ProfileRecord = Records.Declare(Held);

        const ProfileSpecification& Declared = Sketch.Profiles()[Profile.IssuedIndex - 1u];
        if (Declared.HeldLoops().empty() || Declared.HeldLoops()[0].Traversal.size() < 3u)
            return;

        EdgeA = { Declared.HeldLoops()[0].Traversal[0].TraversedCurve.IssuedIndex };
        EdgeB = { Declared.HeldLoops()[0].Traversal[1].TraversedCurve.IssuedIndex };
        EdgeC = { Declared.HeldLoops()[0].Traversal[2].TraversedCurve.IssuedIndex };

        std::vector<SketchPointPlacement> Points;
        if (ResolveSketchPoints(Sketch, EdgeA, Points) && !Points.empty())
            Corner = Points[0].Position;
    }

    void ScreenOf(double Along, double Across, float& X, float& Y) const
    {
        ProjectViewportPoint(Basis, View, false, Extent, Along, Across, X, Y);
    }

    SketchPick VertexPick() const
    {
        return ResolveSketchPickForElement(Sketch, Records, Corner, 5.0, SelectionElement::Vertex);
    }

    SketchPick ProfilePick() const
    {
        SketchPick Pick = {};
        ResolvePickForRecord(Sketch, Records, ProfileRecord, Pick);
        return Pick;
    }
};


//========================================================================================================
// 2. A MOVE
//========================================================================================================

void ProveMove()
{
    std::printf("\n2. Dragging a selection across the plane\n");

    Bench Stage;
    const std::vector<SpatialPoint> Before = Stage.Positions();

    float StartX = 0.0f;
    float StartY = 0.0f;
    Stage.ScreenOf(0.0, 0.0, StartX, StartY);

    TransformSession Session;
    Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                Stage.Extent, StartX, StartY, Stage.CurvePick(),
                                TransformManner::Move, TransformRestriction::Free, false, true, Session),
          "a drag starts");
    Claim(Session.Engaged(), "and the session is engaged");
    Claim(Session.Origins.size() == Session.Placements.size(),
          "every subject has a remembered starting position");

    // Drag to a point 30 along and 10 across.
    float EndX = 0.0f;
    float EndY = 0.0f;
    Stage.ScreenOf(30.0, 10.0, EndX, EndY);
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, EndX, EndY, false,
                           Stage.Sketch, Session);

    Claim(Session.Changed, "the session reports that it changed something");

    const std::vector<SpatialPoint> After = Stage.Positions();
    Claim(After.size() == Before.size(), "the same number of points exist");

    bool AllShifted = After.size() == Before.size();
    for (std::size_t Index = 0u; Index < After.size() && AllShifted; ++Index)
        AllShifted = Near(After[Index].Left    - Before[Index].Left,    30.0, 1.0e-6)
                  && Near(After[Index].Forward - Before[Index].Forward, 10.0, 1.0e-6);
    Claim(AllShifted, "and EVERY point moved by the same 30 along and 10 across");

    // 🔴 REPLAY, NOT ACCUMULATE. Updating again to the SAME position must leave the geometry where it is,
    //    not move it a second time. This is what makes a long drag exact.
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, EndX, EndY, false,
                           Stage.Sketch, Session);
    const std::vector<SpatialPoint> Again = Stage.Positions();
    bool Unmoved = Again.size() == After.size();
    for (std::size_t Index = 0u; Index < Again.size() && Unmoved; ++Index)
        Unmoved = SamePoint(Again[Index], After[Index], 1.0e-9);
    Claim(Unmoved, "updating twice to the same place moves nothing further - it replays, not accumulates");

    // And dragging back to the start returns the original positions exactly.
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, StartX, StartY, false,
                           Stage.Sketch, Session);
    const std::vector<SpatialPoint> Returned = Stage.Positions();
    bool Restored = Returned.size() == Before.size();
    for (std::size_t Index = 0u; Index < Returned.size() && Restored; ++Index)
        Restored = SamePoint(Returned[Index], Before[Index], 1.0e-6);
    Claim(Restored, "dragging back to the start returns the original positions with no drift");
}

void ProveConnectedEdgeMove()
{
    std::printf("\n2b. Dragging one edge carries the corners it shares\n");

    ConnectedBench Stage;

    std::vector<SketchPointPlacement> BottomPoints;
    std::vector<SketchPointPlacement> LeftPoints;
    std::vector<SketchPointPlacement> RightPoints;
    ResolveSketchPoints(Stage.Sketch, Stage.Bottom, BottomPoints);
    ResolveSketchPoints(Stage.Sketch, Stage.Left, LeftPoints);
    ResolveSketchPoints(Stage.Sketch, Stage.Right, RightPoints);

    float StartX = 0.0f;
    float StartY = 0.0f;
    Stage.ScreenOf(0.0, -20.0, StartX, StartY);

    TransformSession Session;
    Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                Stage.Extent, StartX, StartY, Stage.BottomPick(),
                                TransformManner::Move, TransformRestriction::Free, false, true, Session),
          "a connected edge drag starts");

    float EndX = 0.0f;
    float EndY = 0.0f;
    Stage.ScreenOf(0.0, -8.0, EndX, EndY);
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, EndX, EndY, false,
                           Stage.Sketch, Session);

    std::vector<SketchPointPlacement> BottomAfter;
    std::vector<SketchPointPlacement> LeftAfter;
    std::vector<SketchPointPlacement> RightAfter;
    ResolveSketchPoints(Stage.Sketch, Stage.Bottom, BottomAfter);
    ResolveSketchPoints(Stage.Sketch, Stage.Left, LeftAfter);
    ResolveSketchPoints(Stage.Sketch, Stage.Right, RightAfter);

    Claim(BottomAfter.size() == 2u && LeftAfter.size() == 2u && RightAfter.size() == 2u,
          "the connected geometry remains readable after the drag");

    if (BottomAfter.size() == 2u && LeftAfter.size() == 2u && RightAfter.size() == 2u)
    {
        Claim(Near(BottomAfter[0].Position.Forward, -8.0, 1.0e-6)
           && Near(BottomAfter[1].Position.Forward, -8.0, 1.0e-6),
              "the selected edge itself moved to the new across value");
        Claim(SamePoint(LeftAfter[0].Position, BottomAfter[0].Position, 1.0e-6)
           || SamePoint(LeftAfter[1].Position, BottomAfter[0].Position, 1.0e-6),
              "the left neighbour keeps sharing the dragged corner");
        Claim(SamePoint(RightAfter[0].Position, BottomAfter[1].Position, 1.0e-6)
           || SamePoint(RightAfter[1].Position, BottomAfter[1].Position, 1.0e-6),
              "and the right neighbour keeps sharing the other dragged corner");
    }
}

void ProveProfileVertexAndProfileMove()
{
    std::printf("\n2c. Profile-only triangles still move as one closed shape\n");

    ProfileTriangleBench Stage;
    Claim(Stage.Profile.Assigned() && Stage.ProfileRecord.Assigned(),
          "the profile-only triangle bench is standing");

    const SketchPick Vertex = Stage.VertexPick();
    Claim(Vertex.Subject == SketchPickSubject::Point,
          "one corner of a profile-only triangle is still a vertex pick");
    Claim(Vertex.Record.IssuedIndex == Stage.ProfileRecord.IssuedIndex,
          "and that vertex carries the profile record that owns the triangle");

    WorkspaceRecordName ResolvedRecord = {};
    SpatialPoint Pivot = {};
    std::vector<SketchPlacementSubject> Placements;
    Claim(ResolveTransformPlacements(Stage.Sketch, Stage.Records, Vertex, ResolvedRecord, Pivot, Placements),
          "that vertex can arm a transform session");
    Claim(Placements.size() >= 2u,
          "moving one triangle corner resolves BOTH coincident endpoints that meet there");
    Claim(ResolvedRecord.IssuedIndex == Stage.ProfileRecord.IssuedIndex,
          "and the session stays attached to the triangle's profile record");

    SketchPick WholeProfile = Stage.ProfilePick();
    std::vector<SketchPlacementSubject> WholePlacements;
    Claim(WholeProfile.Subject == SketchPickSubject::Record,
          "the triangle's directory record resolves to a whole-profile pick");
    Claim(ResolveTransformPlacements(Stage.Sketch, Stage.Records, WholeProfile,
                                     ResolvedRecord, Pivot, WholePlacements),
          "the whole profile can arm a transform session too");

    std::vector<SketchPointPlacement> EdgeAPoints;
    std::vector<SketchPointPlacement> EdgeBPoints;
    std::vector<SketchPointPlacement> EdgeCPoints;
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeA, EdgeAPoints);
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeB, EdgeBPoints);
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeC, EdgeCPoints);
    const auto HasPoint = [&](SketchPointName Name)
    {
        for (const SketchPlacementSubject& Placement : WholePlacements)
            if (!Placement.ControlPlacement && Placement.Point.IssuedIndex == Name.IssuedIndex)
                return true;
        return false;
    };
    bool CoversWholeTriangle = EdgeAPoints.size() == 2u && EdgeBPoints.size() == 2u && EdgeCPoints.size() == 2u;
    for (const SketchPointPlacement& Point : EdgeAPoints) CoversWholeTriangle = CoversWholeTriangle && HasPoint(Point.Name);
    for (const SketchPointPlacement& Point : EdgeBPoints) CoversWholeTriangle = CoversWholeTriangle && HasPoint(Point.Name);
    for (const SketchPointPlacement& Point : EdgeCPoints) CoversWholeTriangle = CoversWholeTriangle && HasPoint(Point.Name);
    Claim(CoversWholeTriangle,
          "and selecting the whole triangle gathers the placements of every edge it is built from");

    float StartX = 0.0f;
    float StartY = 0.0f;
    Stage.ScreenOf(Vertex.Position.Left, Vertex.Position.Forward, StartX, StartY);

    TransformSession Session;
    Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                Stage.Extent, StartX, StartY, Vertex,
                                TransformManner::Move, TransformRestriction::Free, false, true, Session),
          "a triangle-corner drag starts");

    const double TargetAlong = Vertex.Position.Left + 12.0;
    const double TargetAcross = Vertex.Position.Forward + 9.0;
    float EndX = 0.0f;
    float EndY = 0.0f;
    Stage.ScreenOf(TargetAlong, TargetAcross, EndX, EndY);
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, EndX, EndY, false,
                           Stage.Sketch, Session);

    std::vector<SketchPointPlacement> A;
    std::vector<SketchPointPlacement> B;
    std::vector<SketchPointPlacement> C;
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeA, A);
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeB, B);
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeC, C);
    Claim(A.size() == 2u && B.size() == 2u && C.size() == 2u,
          "the triangle still exposes all three edges after the vertex drag");

    if (A.size() == 2u && B.size() == 2u && C.size() == 2u)
    {
        std::uint32_t MovedCount = 0u;
        for (const SketchPointPlacement& Point : { A[0], A[1], B[0], B[1], C[0], C[1] })
            if (SamePoint(Point.Position, { TargetAlong, 0.0, TargetAcross }, 1.0e-6))
                ++MovedCount;
        Claim(MovedCount == 2u,
              "exactly the two endpoints that shared the dragged corner moved to the new vertex position");

        const SpatialPoint VertexPosition = { TargetAlong, 0.0, TargetAcross };
        const bool AtoB = SamePoint(A[1].Position, B[0].Position, 1.0e-6)
                       || SamePoint(A[1].Position, B[1].Position, 1.0e-6)
                       || SamePoint(A[0].Position, B[0].Position, 1.0e-6)
                       || SamePoint(A[0].Position, B[1].Position, 1.0e-6);
        const bool BtoC = SamePoint(B[0].Position, C[0].Position, 1.0e-6)
                       || SamePoint(B[0].Position, C[1].Position, 1.0e-6)
                       || SamePoint(B[1].Position, C[0].Position, 1.0e-6)
                       || SamePoint(B[1].Position, C[1].Position, 1.0e-6);
        const bool CtoA = SamePoint(C[0].Position, A[0].Position, 1.0e-6)
                       || SamePoint(C[0].Position, A[1].Position, 1.0e-6)
                       || SamePoint(C[1].Position, A[0].Position, 1.0e-6)
                       || SamePoint(C[1].Position, A[1].Position, 1.0e-6);
        Claim(AtoB && BtoC && CtoA,
              "and every neighbouring edge pair still shares a corner - the triangle stays closed");
        Claim((SamePoint(A[0].Position, VertexPosition, 1.0e-6) || SamePoint(A[1].Position, VertexPosition, 1.0e-6)) &&
              (SamePoint(C[0].Position, VertexPosition, 1.0e-6) || SamePoint(C[1].Position, VertexPosition, 1.0e-6)),
              "the dragged corner is shared by the two incident edges rather than split into separate points");
    }
}

void ProveRepeatedVertexMoveDoesNotSplit()
{
    std::printf("\n2d. Reusing the same selected vertex does not split the shape\n");

    ProfileTriangleBench Stage;
    const SketchPick Vertex = Stage.VertexPick();
    Claim(Vertex.Subject == SketchPickSubject::Point,
          "the triangle still offers a vertex pick to start from");

    TransformSession First;
    float StartX = 0.0f;
    float StartY = 0.0f;
    Stage.ScreenOf(Vertex.Position.Left, Vertex.Position.Forward, StartX, StartY);
    Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                Stage.Extent, StartX, StartY, Vertex,
                                TransformManner::Move, TransformRestriction::Free, false, true, First),
          "the first vertex drag starts");

    const double FirstAlong = Vertex.Position.Left + 10.0;
    const double FirstAcross = Vertex.Position.Forward + 6.0;
    float FirstEndX = 0.0f;
    float FirstEndY = 0.0f;
    Stage.ScreenOf(FirstAlong, FirstAcross, FirstEndX, FirstEndY);
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, FirstEndX, FirstEndY, false,
                           Stage.Sketch, First);
    CommitTransformSession(Stage.Records, Stage.Revisions, First);

    // 🔴 THE ORIGINAL BUG: the standing semantic point kept its OLD position, so the second drag looked
    //    for coincident neighbours at the abandoned corner and moved only one endpoint. Reusing the same
    //    `Vertex` pick here reproduces that exact state.
    TransformSession Second;
    Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                Stage.Extent, FirstEndX, FirstEndY, Vertex,
                                TransformManner::Move, TransformRestriction::Free, false, true, Second),
          "the second drag can start from the same selected vertex, even though its stored position is stale");
    Claim(Second.Placements.size() >= 2u,
          "and it still resolves both incident endpoints rather than only one stale point");

    const double SecondAlong = FirstAlong + 7.0;
    const double SecondAcross = FirstAcross - 5.0;
    float SecondEndX = 0.0f;
    float SecondEndY = 0.0f;
    Stage.ScreenOf(SecondAlong, SecondAcross, SecondEndX, SecondEndY);
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, SecondEndX, SecondEndY, false,
                           Stage.Sketch, Second);

    std::vector<SketchPointPlacement> A;
    std::vector<SketchPointPlacement> B;
    std::vector<SketchPointPlacement> C;
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeA, A);
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeB, B);
    ResolveSketchPoints(Stage.Sketch, Stage.EdgeC, C);
    Claim(A.size() == 2u && B.size() == 2u && C.size() == 2u,
          "the triangle remains readable after dragging the same selection twice");

    if (A.size() == 2u && B.size() == 2u && C.size() == 2u)
    {
        std::uint32_t MovedCount = 0u;
        for (const SketchPointPlacement& Point : { A[0], A[1], B[0], B[1], C[0], C[1] })
            if (SamePoint(Point.Position, { SecondAlong, 0.0, SecondAcross }, 1.0e-6))
                ++MovedCount;
        Claim(MovedCount == 2u,
              "after the second drag, exactly the two incident endpoints still share the moved corner");

        const bool AtoB = SamePoint(A[1].Position, B[0].Position, 1.0e-6)
                       || SamePoint(A[1].Position, B[1].Position, 1.0e-6)
                       || SamePoint(A[0].Position, B[0].Position, 1.0e-6)
                       || SamePoint(A[0].Position, B[1].Position, 1.0e-6);
        const bool BtoC = SamePoint(B[0].Position, C[0].Position, 1.0e-6)
                       || SamePoint(B[0].Position, C[1].Position, 1.0e-6)
                       || SamePoint(B[1].Position, C[0].Position, 1.0e-6)
                       || SamePoint(B[1].Position, C[1].Position, 1.0e-6);
        const bool CtoA = SamePoint(C[0].Position, A[0].Position, 1.0e-6)
                       || SamePoint(C[0].Position, A[1].Position, 1.0e-6)
                       || SamePoint(C[1].Position, A[0].Position, 1.0e-6)
                       || SamePoint(C[1].Position, A[1].Position, 1.0e-6);
        Claim(AtoB && BtoC && CtoA,
              "and the second drag still leaves a closed triangle rather than a split-open one");
    }
}


//========================================================================================================
// 3. AXIS RESTRICTION
//========================================================================================================

void ProveRestriction()
{
    std::printf("\n3. Restricting a drag to one axis\n");

    for (int Axis = 0; Axis < 2; ++Axis)
    {
        const bool AlongOnly = Axis == 0;

        Bench Stage;
        const std::vector<SpatialPoint> Before = Stage.Positions();

        float StartX = 0.0f;
        float StartY = 0.0f;
        Stage.ScreenOf(0.0, 0.0, StartX, StartY);

        TransformSession Session;
        StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                              Stage.Extent, StartX, StartY, Stage.CurvePick(),
                              TransformManner::Move,
                              AlongOnly ? TransformRestriction::AxisX : TransformRestriction::AxisZ,
                              false, true, Session);

        // Drag diagonally: the restricted axis must be the only one that moves.
        float EndX = 0.0f;
        float EndY = 0.0f;
        Stage.ScreenOf(30.0, 25.0, EndX, EndY);
        UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, EndX, EndY, false,
                               Stage.Sketch, Session);

        const std::vector<SpatialPoint> After = Stage.Positions();
        bool Correct = After.size() == Before.size();
        for (std::size_t Index = 0u; Index < After.size() && Correct; ++Index)
        {
            const double MovedAlong  = After[Index].Left    - Before[Index].Left;
            const double MovedAcross = After[Index].Forward - Before[Index].Forward;
            Correct = AlongOnly ? (Near(MovedAlong, 30.0, 1.0e-6) && Near(MovedAcross, 0.0, 1.0e-6))
                                : (Near(MovedAlong, 0.0, 1.0e-6)  && Near(MovedAcross, 25.0, 1.0e-6));
        }
        Claim(Correct, AlongOnly
              ? "restricted to the along axis, a diagonal drag moves ONLY along"
              : "restricted to the across axis, a diagonal drag moves ONLY across");
    }
}

//========================================================================================================
// 4. ROTATE AND SCALE
//========================================================================================================

void ProveRotateAndScale()
{
    std::printf("\n4. Turning and resizing about the pivot\n");

    // A quarter turn: a line along the plane's Along becomes a line along its Across.
    {
        Bench Stage;
        float StartX = 0.0f;
        float StartY = 0.0f;
        Stage.ScreenOf(20.0, 0.0, StartX, StartY);

        TransformSession Session;
        Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                    Stage.Extent, StartX, StartY, Stage.CurvePick(),
                                    TransformManner::Rotate, TransformRestriction::Free,
                                    false, true, Session),
              "a rotate starts");
        Claim(Near(Session.StartAngle, 0.0, 1.0e-9),
              "beginning on the along axis, the start angle is zero");

        float QuarterX = 0.0f;
        float QuarterY = 0.0f;
        Stage.ScreenOf(0.0, 20.0, QuarterX, QuarterY);
        UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, QuarterX, QuarterY, false,
                               Stage.Sketch, Session);

        Claim(Near(std::fabs(Session.PreviewValue), 90.0, 1.0e-6),
              "dragging a quarter turn reads as 90 degrees");

        const std::vector<SpatialPoint> After = Stage.Positions();
        bool OnAcross = !After.empty();
        for (const SpatialPoint& Point : After)
            OnAcross = OnAcross && Near(Point.Left, 0.0, 1.0e-6) && !Near(Point.Forward, 0.0, 1.0e-6);
        Claim(OnAcross, "and the line now lies along the across axis");

        // ⚠️ The pivot itself does not move, whatever the rotation.
        SpatialPoint Pivot = {};
        Claim(ResolveCurvePivot(Stage.Sketch, Stage.Line, Pivot), "the rotated line still has a pivot");
        Claim(SamePoint(Pivot, Session.Pivot, 1.0e-6), "and it is where it was - a rotation is about it");
    }

    // Doubling: dragging to twice the starting distance doubles the selection.
    {
        Bench Stage;
        float StartX = 0.0f;
        float StartY = 0.0f;
        Stage.ScreenOf(20.0, 0.0, StartX, StartY);

        TransformSession Session;
        StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                              Stage.Extent, StartX, StartY, Stage.CurvePick(),
                              TransformManner::Scale, TransformRestriction::Free, false, true, Session);
        Claim(Near(Session.StartDistance, 20.0, 1.0e-6), "the starting distance is the drag radius");

        float FarX = 0.0f;
        float FarY = 0.0f;
        Stage.ScreenOf(40.0, 0.0, FarX, FarY);
        UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, FarX, FarY, false,
                               Stage.Sketch, Session);

        Claim(Near(Session.PreviewValue, 2.0, 1.0e-6), "dragging to twice the radius reads as 2.0");

        const std::vector<SpatialPoint> After = Stage.Positions();
        bool Doubled = !After.empty();
        for (const SpatialPoint& Point : After)
            Doubled = Doubled && (Near(std::fabs(Point.Left), 40.0, 1.0e-6));
        Claim(Doubled, "and the line is twice as long, about its centre");
    }

    // 🔴 THE FLOOR. Dragging onto the pivot must not collapse the selection to a point it can never be
    //    dragged back out of, because every subsequent scale multiplies zero by something.
    {
        Bench Stage;
        float StartX = 0.0f;
        float StartY = 0.0f;
        Stage.ScreenOf(20.0, 0.0, StartX, StartY);

        TransformSession Session;
        StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                              Stage.Extent, StartX, StartY, Stage.CurvePick(),
                              TransformManner::Scale, TransformRestriction::Free, false, true, Session);

        float PivotX = 0.0f;
        float PivotY = 0.0f;
        Stage.ScreenOf(0.0, 0.0, PivotX, PivotY);
        UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, PivotX, PivotY, false,
                               Stage.Sketch, Session);

        Claim(Session.PreviewValue >= 0.05, "dragging onto the pivot floors the scale at 0.05");
        Claim(Session.PreviewValue > 0.0, "it never reaches zero");

        const std::vector<SpatialPoint> After = Stage.Positions();
        bool Collapsed = true;
        for (const SpatialPoint& Point : After)
            if (!Near(Point.Left, 0.0, 1.0e-9) || !Near(Point.Forward, 0.0, 1.0e-9))
                Collapsed = false;
        Claim(!Collapsed, "so the selection keeps a size and can be dragged back out");
    }
}

//========================================================================================================
// 4b. THE SAME, SOMEWHERE AWKWARD
//========================================================================================================

// ⚠️ Sections 2 to 4 stand a line symmetrically about the world origin, which is exactly the fixture a
//    broken implementation passes by coincidence: rotating about the origin and rotating about the pivot
//    agree when they are the same place. These repeat the claims somewhere that tells them apart.

void ProveOffOrigin()
{
    std::printf("\n4b. The pivot is the pivot, not the origin\n");

    // A line from 10 to 50 along: its pivot is at 30, nowhere near the world origin.
    Bench Stage(10.0, 0.0, 50.0, 0.0);

    float StartX = 0.0f;
    float StartY = 0.0f;
    Stage.ScreenOf(50.0, 0.0, StartX, StartY);

    TransformSession Session;
    Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                Stage.Extent, StartX, StartY, Stage.CurvePick(),
                                TransformManner::Rotate, TransformRestriction::Free,
                                false, true, Session),
          "a rotate starts on an off-origin line");
    Claim(Near(Session.PivotAlong, 30.0, 1.0e-6), "the pivot is the line's own midpoint, at 30");
    Claim(!Near(Session.PivotAlong, 0.0, 1.0e-6), "which is NOT the world origin");

    float QuarterX = 0.0f;
    float QuarterY = 0.0f;
    Stage.ScreenOf(30.0, 20.0, QuarterX, QuarterY);
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, QuarterX, QuarterY, false,
                           Stage.Sketch, Session);

    const std::vector<SpatialPoint> After = Stage.Positions();
    bool AboutPivot = !After.empty();
    for (const SpatialPoint& Point : After)
        AboutPivot = AboutPivot && Near(Point.Left, 30.0, 1.0e-6);
    Claim(AboutPivot, "a quarter turn leaves every point on the line THROUGH THE PIVOT, not the origin");

    bool Spread = false;
    for (const SpatialPoint& Point : After)
        if (Near(std::fabs(Point.Forward), 20.0, 1.0e-6))
            Spread = true;
    Claim(Spread, "and 20 out from it, the half-length it started with");
}

//========================================================================================================
// 4c. A DRAG THAT BEGINS ON THE PIVOT
//========================================================================================================

void ProveDegenerateStart()
{
    std::printf("\n4c. Beginning a scale exactly on the pivot\n");

    // 🔴 The starting radius divides the scale factor. A drag that begins ON the pivot has a radius of
    //    zero, and without a floor the very first frame divides by it — every point becomes NaN, and no
    //    later drag can recover geometry that is no longer a number.
    Bench Stage;

    float PivotX = 0.0f;
    float PivotY = 0.0f;
    Stage.ScreenOf(0.0, 0.0, PivotX, PivotY);

    TransformSession Session;
    Claim(StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                                Stage.Extent, PivotX, PivotY, Stage.CurvePick(),
                                TransformManner::Scale, TransformRestriction::Free,
                                false, true, Session),
          "a scale beginning on the pivot still starts");
    Claim(Session.StartDistance > 0.0, "the starting radius is never zero");
    Claim(std::isfinite(Session.StartDistance), "and is a number");

    float OutX = 0.0f;
    float OutY = 0.0f;
    Stage.ScreenOf(15.0, 0.0, OutX, OutY);
    UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, OutX, OutY, false,
                           Stage.Sketch, Session);

    Claim(std::isfinite(Session.PreviewValue), "dragging out from it yields a finite scale");

    const std::vector<SpatialPoint> After = Stage.Positions();
    bool Sane = !After.empty();
    for (const SpatialPoint& Point : After)
        Sane = Sane && std::isfinite(Point.Left) && std::isfinite(Point.Forward);
    Claim(Sane, "and every point is still a number rather than NaN");
}

//========================================================================================================
// 5. CANCELLING AND COMMITTING
//========================================================================================================

void ProveClosure()
{
    std::printf("\n5. Accepting or abandoning a drag\n");

    // 🔴 CANCELLING RESTORES EXACTLY. Not approximately, not to the nearest snap — the originals.
    // ⚠️ The coordinates are deliberately awkward. A line at round numbers survives an implementation
    //    that restores to three decimal places, and would prove nothing.
    {
        Bench Stage(-17.318309886183790, 4.6692016091029906,
                     23.140692632779267, -8.5397342226735670);
        const std::vector<SpatialPoint> Before = Stage.Positions();

        float StartX = 0.0f;
        float StartY = 0.0f;
        Stage.ScreenOf(0.0, 0.0, StartX, StartY);

        TransformSession Session;
        StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                              Stage.Extent, StartX, StartY, Stage.CurvePick(),
                              TransformManner::Move, TransformRestriction::Free, false, true, Session);

        float EndX = 0.0f;
        float EndY = 0.0f;
        Stage.ScreenOf(137.5, -62.25, EndX, EndY);
        UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, EndX, EndY, false,
                               Stage.Sketch, Session);

        bool Moved = false;
        const std::vector<SpatialPoint> During = Stage.Positions();
        for (std::size_t Index = 0u; Index < During.size(); ++Index)
            if (!SamePoint(During[Index], Before[Index], 1.0e-6))
                Moved = true;
        Claim(Moved, "the drag moved the geometry");

        CancelTransformSession(Stage.Sketch, Session);

        const std::vector<SpatialPoint> After = Stage.Positions();
        bool Exact = After.size() == Before.size();
        for (std::size_t Index = 0u; Index < After.size() && Exact; ++Index)
            Exact = SamePoint(After[Index], Before[Index], 0.0);
        Claim(Exact, "cancelling puts every point back EXACTLY, bit for bit");
        Claim(!Session.Engaged(), "and the session is no longer engaged");
        Claim(Session.Placements.empty(), "with nothing left over");
        Claim(Stage.Revisions.DeclaredCount() == 0u, "a cancelled drag seals no revision");
    }

    // Committing keeps the geometry and seals one revision.
    {
        Bench Stage;
        float StartX = 0.0f;
        float StartY = 0.0f;
        Stage.ScreenOf(0.0, 0.0, StartX, StartY);

        TransformSession Session;
        StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                              Stage.Extent, StartX, StartY, Stage.CurvePick(),
                              TransformManner::Move, TransformRestriction::Free, false, true, Session);

        float EndX = 0.0f;
        float EndY = 0.0f;
        Stage.ScreenOf(30.0, 0.0, EndX, EndY);
        UpdateTransformSession(Stage.Basis, Stage.View, false, Stage.Extent, EndX, EndY, false,
                               Stage.Sketch, Session);
        const std::vector<SpatialPoint> Moved = Stage.Positions();

        CommitTransformSession(Stage.Records, Stage.Revisions, Session);

        const std::vector<SpatialPoint> After = Stage.Positions();
        bool Kept = After.size() == Moved.size();
        for (std::size_t Index = 0u; Index < After.size() && Kept; ++Index)
            Kept = SamePoint(After[Index], Moved[Index], 0.0);
        Claim(Kept, "committing leaves the geometry where the drag put it");
        Claim(Stage.Revisions.DeclaredCount() == 1u, "and seals exactly one revision");
        Claim(!Session.Engaged(), "the session closes");

        const WorkspaceRevision* Sealed = Stage.Revisions.Resolve({ 1u });
        Claim(Sealed != nullptr, "the revision resolves");
        if (Sealed != nullptr)
        {
            Claim(!Sealed->Description.empty(), "it is described for the history panel");
            Claim(Sealed->Affected.size() == 1u, "and names the record it moved");
        }
    }

    // 🔴 A DRAG THAT MOVED NOTHING SEALS NOTHING. Arming a transform and releasing without moving must
    //    not push an empty step the artist has to undo twice.
    {
        Bench Stage;
        float StartX = 0.0f;
        float StartY = 0.0f;
        Stage.ScreenOf(0.0, 0.0, StartX, StartY);

        TransformSession Session;
        StartTransformSession(Stage.Sketch, Stage.Records, Stage.Basis, Stage.View, false,
                              Stage.Extent, StartX, StartY, Stage.CurvePick(),
                              TransformManner::Move, TransformRestriction::Free, false, true, Session);
        CommitTransformSession(Stage.Records, Stage.Revisions, Session);

        Claim(Stage.Revisions.DeclaredCount() == 0u,
              "a drag that never moved anything seals NO revision");
    }
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("TRANSFORM SESSION PROOF\n");
    std::printf("=========================================================================\n");

    ProveResolution();
    ProveMove();
    ProveConnectedEdgeMove();
    ProveProfileVertexAndProfileMove();
    ProveRepeatedVertexMoveDoesNotSplit();
    ProveRestriction();
    ProveRotateAndScale();
    ProveOffOrigin();
    ProveDegenerateStart();
    ProveClosure();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
