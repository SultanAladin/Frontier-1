//============================================================================================================================================
//                                                       SKETCHPICKINGPROOF.CPP
//============================================================================================================================================
// 🧩 Proves what the artist gets when they point at the viewport: which of the overlapping candidates
//    wins, which record owns it, what it turns about, and what moves when it is dragged.

#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"

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

/// 🧩 A sketch on the ground plane with a directory over it.
struct Bench
{
    SketchStructure          Sketch;
    WorkspaceRecordStructure Records;

    Bench()
    {
        SketchPlane Ground;
        Ground.Origin         = { 0.0, 0.0, 0.0 };
        Ground.Normal         = { 0.0, 1.0, 0.0 };
        Ground.AlongDirection = { 1.0, 0.0, 0.0 };
        Sketch.DeclarePlane(Ground);
    }

    WorkspaceRecordName DeclareCurveRecord(SketchCurveName Curve)
    {
        WorkspaceRecord Record = {};
        Record.Subject     = WorkspaceRecordSubject::OpenCurve;
        Record.SketchCurve = Curve;
        Record.Naming      = "curve";
        return Records.Declare(Record);
    }

    WorkspaceRecordName DeclareProfileRecord(ProfileNameInFeature Profile)
    {
        WorkspaceRecord Record = {};
        Record.Subject = WorkspaceRecordSubject::ClosedProfile;
        Record.Profile = Profile;
        Record.Naming  = "profile";
        return Records.Declare(Record);
    }
};

//========================================================================================================
// 1. THE PICK PRIORITY
//========================================================================================================

void ProvePriority()
{
    std::printf("\n1. Which candidate wins when several are within reach\n");

    Bench Stage;
    const SketchCurveName Curve = Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 });
    Stage.DeclareCurveRecord(Curve);

    // 🔴 THE CLAIM THE WHOLE UNIT EXISTS FOR. A line passes through its own endpoint, so at the endpoint
    //    both the point and the curve are at distance zero. If they competed on distance the curve could
    //    win and the artist could never grab the end they were aiming at.
    {
        const SketchPick Pick = ResolveSketchPick(Stage.Sketch, Stage.Records, { 0.0, 0.0, 0.0 }, 5.0);
        Claim(Pick.Standing(), "pointing at a line's endpoint picks something");
        Claim(Pick.Subject == SketchPickSubject::Point,
              "pointing at an endpoint picks the POINT, not the curve that passes through it");
        Claim(Pick.Curve.IssuedIndex == Curve.IssuedIndex, "and it knows which curve the point came from");
    }

    // Away from the ends, the curve is the only candidate.
    {
        const SketchPick Pick = ResolveSketchPick(Stage.Sketch, Stage.Records, { 50.0, 0.0, 0.0 }, 5.0);
        Claim(Pick.Standing(), "pointing at the middle of a line picks something");
        Claim(Pick.Subject == SketchPickSubject::Curve, "and it is the curve");
        Claim(Pick.Curve.IssuedIndex == Curve.IssuedIndex, "the right curve");
    }

    // ⚠️ Nothing within reach means nothing picked, rather than the nearest thing at any distance.
    {
        const SketchPick Pick = ResolveSketchPick(Stage.Sketch, Stage.Records, { 500.0, 0.0, 500.0 }, 5.0);
        Claim(!Pick.Standing(), "pointing at empty space picks nothing");
        Claim(Pick.Subject == SketchPickSubject::None, "and reports None rather than a stale subject");
    }

    // 📝 The tolerance is what decides reach; a generous one finds the line from further away.
    {
        const SketchPick Narrow = ResolveSketchPick(Stage.Sketch, Stage.Records, { 50.0, 0.0, 8.0 }, 2.0);
        const SketchPick Wide   = ResolveSketchPick(Stage.Sketch, Stage.Records, { 50.0, 0.0, 8.0 }, 20.0);
        Claim(!Narrow.Standing(), "a point 8 away is outside a tolerance of 2");
        Claim(Wide.Standing(), "and inside a tolerance of 20");
    }

    // 🔴 A pick with no record is not returned. An empty directory can name nothing, so pointing straight
    //    at a curve must still pick nothing rather than a selection nothing can act on.
    {
        Bench Orphan;
        Orphan.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 0.0 });
        const SketchPick Pick = ResolveSketchPick(Orphan.Sketch, Orphan.Records, { 0.0, 0.0, 0.0 }, 5.0);
        Claim(!Pick.Record.Assigned(), "with no directory record, the pick names no record");
    }
}

//========================================================================================================
// 2. WHICH RECORD OWNS WHAT
//========================================================================================================

void ProveOwnership()
{
    std::printf("\n2. The record a picked thing belongs to\n");

    Bench Stage;
    const SketchCurveName First  = Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });
    const SketchCurveName Second = Stage.Sketch.DeclareLine({ 10.0, 0.0, 0.0 }, { 10.0, 0.0, 10.0 });

    const WorkspaceRecordName FirstRecord  = Stage.DeclareCurveRecord(First);
    const WorkspaceRecordName SecondRecord = Stage.DeclareCurveRecord(Second);

    Claim(ResolveRecordForCurve(Stage.Sketch, Stage.Records, First).IssuedIndex == FirstRecord.IssuedIndex,
          "a curve resolves to its own record");
    Claim(ResolveRecordForCurve(Stage.Sketch, Stage.Records, Second).IssuedIndex == SecondRecord.IssuedIndex,
          "and a second curve to its own, not the first");

    // 🔴 THE GUARD. An unassigned name must not match every record that also carries none.
    Claim(!ResolveRecordForCurve(Stage.Sketch, Stage.Records, {}).Assigned(),
          "an unassigned curve resolves to NO record - it must not match the first record carrying none");
    Claim(!ResolveRecordForPoint(Stage.Sketch, Stage.Records, {}).Assigned(),
          "an unassigned point resolves to NO record either");

    // ⚠️ Concretely: a directory whose FIRST record carries no curve at all.
    {
        Bench Bare;
        WorkspaceRecord Folder = {};
        Folder.Subject = WorkspaceRecordSubject::Folder;
        Folder.Naming  = "a folder, carrying no curve and no point";
        Bare.Records.Declare(Folder);

        Claim(!ResolveRecordForCurve(Bare.Sketch, Bare.Records, {}).Assigned(),
              "an unassigned curve does not resolve to a folder that happens to carry no curve");
        Claim(!ResolveRecordForPoint(Bare.Sketch, Bare.Records, {}).Assigned(),
              "and an unassigned point does not resolve to it either");
    }

    // A curve nothing names resolves to nothing.
    {
        const SketchCurveName Unnamed = Stage.Sketch.DeclareLine({ 50.0, 0.0, 0.0 }, { 60.0, 0.0, 0.0 });
        Claim(!ResolveRecordForCurve(Stage.Sketch, Stage.Records, Unnamed).Assigned(),
              "a curve with no record resolves to nothing");
    }

    // 🔴 PROFILE-ONLY SHAPES STILL OWN THEIR CORNERS. A triangle usually has one profile record and no
    //    child edge rows; its vertices must therefore resolve to the PROFILE or Vertex mode cannot pick
    //    them at all.
    {
        Bench ProfileOnly;
        const Deliver<ProfileNameInFeature> Triangle =
            ProfileOnly.Sketch.DeclareRegularPolygon({ 0.0, 0.0, 0.0 }, 20.0, 3u, SpatialDirection{ 1.0, 0.0, 0.0 });
        Claim(Triangle.Resolved, "a profile-only triangle can be declared");
        if (Triangle.Resolved)
        {
            const WorkspaceRecordName ProfileRecord = ProfileOnly.DeclareProfileRecord(Triangle.Resolve());
            const ProfileSpecification& Held =
                ProfileOnly.Sketch.Profiles()[Triangle.Resolve().IssuedIndex - 1u];
            const SketchCurveName FirstCurve = { Held.HeldLoops()[0].Traversal[0].TraversedCurve.IssuedIndex };

            std::vector<SketchPointPlacement> Points;
            Claim(ResolveSketchPoints(ProfileOnly.Sketch, FirstCurve, Points) && !Points.empty(),
                  "the triangle exposes points on its first edge");
            if (!Points.empty())
                Claim(ResolveRecordForPoint(ProfileOnly.Sketch, ProfileOnly.Records, Points[0].Name).IssuedIndex == ProfileRecord.IssuedIndex,
                      "and one of those points resolves to the PROFILE record that owns the triangle");
        }
    }
}

//========================================================================================================
// 3. WHAT A THING TURNS ABOUT
//========================================================================================================

void ProvePivots()
{
    std::printf("\n3. The point a selection rotates and scales about\n");

    Bench Stage;

    // A line turns about its midpoint.
    {
        const SketchCurveName Line = Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 40.0 });
        SpatialPoint Pivot = {};
        Claim(ResolveCurvePivot(Stage.Sketch, Line, Pivot), "a line has a pivot");
        Claim(SamePoint(Pivot, { 50.0, 0.0, 20.0 }), "and it is the midpoint of its two ends");
    }

    // A circle turns about its centre, not the midpoint of anything.
    {
        CircleCurve Declared;
        Declared.Centre = { 25.0, 0.0, -13.0 };
        Declared.Radius = 8.0;
        Declared.Normal = { 0.0, 1.0, 0.0 };
        const SketchCurveName Circle = Stage.Sketch.DeclareCircle(Declared);

        SpatialPoint Pivot = {};
        Claim(ResolveCurvePivot(Stage.Sketch, Circle, Pivot), "a circle has a pivot");
        Claim(SamePoint(Pivot, Declared.Centre), "and it is the centre");
    }

    // ⚠️ An unassigned or out-of-range curve has no pivot, and must not read past the end of the store.
    {
        SpatialPoint Pivot = {};
        Claim(!ResolveCurvePivot(Stage.Sketch, {}, Pivot), "an unassigned curve has no pivot");
        Claim(!ResolveCurvePivot(Stage.Sketch, { 9999u }, Pivot),
              "a curve index past the end of the store has no pivot rather than reading past it");
    }

    // 🔴 A PROFILE AVERAGES ITS CURVES' PIVOTS, NOT ITS POINTS. A square built from four equal edges has
    //    its pivot at the centre either way; the claim below is what distinguishes the two rules.
    {
        Bench Square;
        const SketchCurveName Edges[4] = {
            Square.Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 10.0, 0.0, 0.0 }),
            Square.Sketch.DeclareLine({ 10.0, 0.0, 0.0 },  { 10.0, 0.0, 10.0 }),
            Square.Sketch.DeclareLine({ 10.0, 0.0, 10.0 }, { 0.0, 0.0, 10.0 }),
            Square.Sketch.DeclareLine({ 0.0, 0.0, 10.0 },  { 0.0, 0.0, 0.0 }),
        };

        ProfileSpecification Profile;
        Profile.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
        ProfileLoop Loop;
        Loop.Orientation = ProfileLoopOrientation::Outer;
        for (SketchCurveName Edge : Edges)
            Loop.Traversal.push_back({ { Edge.IssuedIndex }, true });
        Profile.DeclareLoop(Loop);

        const ProfileNameInFeature Named = Square.Sketch.DeclareProfile(Profile);

        SpatialPoint Pivot = {};
        Claim(ResolveProfilePivot(Square.Sketch, Named, Pivot), "a square profile has a pivot");
        Claim(SamePoint(Pivot, { 5.0, 0.0, 5.0 }), "and it is the centre of the square");

        SpatialPoint Missing = {};
        Claim(!ResolveProfilePivot(Square.Sketch, {}, Missing), "an unassigned profile has no pivot");
        Claim(!ResolveProfilePivot(Square.Sketch, { 9999u }, Missing),
              "and a profile index past the end has none either");
    }
}

//========================================================================================================
// 4. WHAT MOVES WHEN A SELECTION IS DRAGGED
//========================================================================================================

void ProvePlacements()
{
    std::printf("\n4. Collecting what a transform will move\n");

    Bench Stage;
    const SketchCurveName First  = Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });
    const SketchCurveName Second = Stage.Sketch.DeclareLine({ 10.0, 0.0, 0.0 }, { 10.0, 0.0, 10.0 });

    {
        std::vector<SketchPlacementSubject> Placements;
        CollectCurvePlacements(Stage.Sketch, First, Placements);
        Claim(!Placements.empty(), "a line contributes placements");
        Claim(Placements.size() >= 2u, "at least its two ends");
    }

    // 🔴 DEDUPLICATION IS BY NAME, NOT BY POSITION — and that is the correct rule, though it is not the
    //    one I first assumed. Two lines meeting at (10, 0, 0) do NOT share a point: each curve names its
    //    own endpoints, so the corner is point 258 of the first line and point 513 of the second. They
    //    are coincident but genuinely distinct subjects, and both must move or dragging one line would
    //    silently drag its neighbour's end with it.
    //
    // 📝 My first claim here asserted the opposite and failed. The guard exists for a subject reachable
    //    by two ROUTES — a curve collected directly and again as part of a profile that contains it —
    //    which is the case proved below, not for two curves that happen to touch.
    {
        std::vector<SketchPlacementSubject> Placements;
        CollectCurvePlacements(Stage.Sketch, First, Placements);
        const std::size_t AfterFirst = Placements.size();
        CollectCurvePlacements(Stage.Sketch, Second, Placements);

        Claim(Placements.size() > AfterFirst, "the second line contributes placements of its own");
        Claim(Placements.size() == AfterFirst * 2u,
              "and ALL of them - two lines meeting at a corner name that corner separately, so both "
              "ends move and neither line drags the other");

        // No two placements may name the same subject.
        bool Duplicated = false;
        for (std::size_t Left = 0u; Left < Placements.size(); ++Left)
            for (std::size_t Right = Left + 1u; Right < Placements.size(); ++Right)
            {
                const SketchPlacementSubject& A = Placements[Left];
                const SketchPlacementSubject& B = Placements[Right];
                if (A.ControlPlacement != B.ControlPlacement)
                    continue;
                if (!A.ControlPlacement && A.Point.IssuedIndex == B.Point.IssuedIndex)
                    Duplicated = true;
                if (A.ControlPlacement && A.Control.IssuedIndex == B.Control.IssuedIndex)
                    Duplicated = true;
            }
        Claim(!Duplicated, "no subject appears twice in the collected placements");
    }

    // 🔴 THE CASE THE GUARD IS ACTUALLY FOR: one subject reached by two routes. Collecting a curve
    //    directly and then collecting the profile that contains it must not list its points twice.
    {
        Bench Square;
        const SketchCurveName Edges[4] = {
            Square.Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 10.0, 0.0, 0.0 }),
            Square.Sketch.DeclareLine({ 10.0, 0.0, 0.0 },  { 10.0, 0.0, 10.0 }),
            Square.Sketch.DeclareLine({ 10.0, 0.0, 10.0 }, { 0.0, 0.0, 10.0 }),
            Square.Sketch.DeclareLine({ 0.0, 0.0, 10.0 },  { 0.0, 0.0, 0.0 }),
        };

        ProfileSpecification Profile;
        Profile.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
        ProfileLoop Loop;
        Loop.Orientation = ProfileLoopOrientation::Outer;
        for (SketchCurveName Edge : Edges)
            Loop.Traversal.push_back({ { Edge.IssuedIndex }, true });
        Profile.DeclareLoop(Loop);
        const ProfileNameInFeature Named = Square.Sketch.DeclareProfile(Profile);

        std::vector<SketchPlacementSubject> ProfileOnly;
        CollectProfilePlacements(Square.Sketch, Named, ProfileOnly);
        Claim(!ProfileOnly.empty(), "a profile contributes placements");

        // Collect one of its edges FIRST, then the whole profile. The edge's points must not double up.
        std::vector<SketchPlacementSubject> Both;
        CollectCurvePlacements(Square.Sketch, Edges[0], Both);
        const std::size_t AfterEdge = Both.size();
        CollectProfilePlacements(Square.Sketch, Named, Both);

        Claim(AfterEdge > 0u, "the edge alone contributes placements");
        Claim(Both.size() == ProfileOnly.size(),
              "collecting an edge and then the profile that contains it yields exactly the profile's "
              "count - the edge's points are not listed twice");

        bool Duplicated = false;
        for (std::size_t Left = 0u; Left < Both.size(); ++Left)
            for (std::size_t Right = Left + 1u; Right < Both.size(); ++Right)
            {
                const SketchPlacementSubject& A = Both[Left];
                const SketchPlacementSubject& B = Both[Right];
                if (A.ControlPlacement != B.ControlPlacement)
                    continue;
                if (!A.ControlPlacement && A.Point.IssuedIndex == B.Point.IssuedIndex)
                    Duplicated = true;
                if (A.ControlPlacement && A.Control.IssuedIndex == B.Control.IssuedIndex)
                    Duplicated = true;
            }
        Claim(!Duplicated, "and no subject appears twice by either route");
    }

    // 📝 A point and a control with the same index are different subjects and must both survive.
    {
        std::vector<SketchPlacementSubject> Placements;
        AppendPlacementUnique(Placements, { false, { 7u }, {}, { 1.0, 0.0, 0.0 } });
        AppendPlacementUnique(Placements, { true, {}, { 7u }, { 2.0, 0.0, 0.0 } });
        Claim(Placements.size() == 2u,
              "a point and a control sharing an index are different subjects and both survive");

        AppendPlacementUnique(Placements, { false, { 7u }, {}, { 9.0, 0.0, 0.0 } });
        Claim(Placements.size() == 2u, "and adding the same point again changes nothing");
    }
}

//========================================================================================================
// 5. SELECTING FROM THE OUTLINER
//========================================================================================================

void ProveRecordSelection()
{
    std::printf("\n5. The pick a directory record corresponds to\n");

    Bench Stage;
    const SketchCurveName Curve  = Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 100.0, 0.0, 40.0 });
    const WorkspaceRecordName Named = Stage.DeclareCurveRecord(Curve);

    {
        SketchPick Pick = {};
        Claim(ResolvePickForRecord(Stage.Sketch, Stage.Records, Named, Pick),
              "selecting a curve record in the outliner yields a pick");
        Claim(Pick.Subject == SketchPickSubject::Curve, "it is a curve pick");
        Claim(Pick.Record.IssuedIndex == Named.IssuedIndex, "it carries the record it came from");
        Claim(SamePoint(Pick.Position, { 50.0, 0.0, 20.0 }),
              "and its position is the curve's pivot, so a rotate turns about the right place");
    }

    // ⚠️ A record with no geometry must not arm a transform.
    {
        WorkspaceRecord Folder = {};
        Folder.Subject = WorkspaceRecordSubject::Folder;
        Folder.Naming  = "folder";
        const WorkspaceRecordName Filed = Stage.Records.Declare(Folder);

        SketchPick Pick = {};
        Claim(!ResolvePickForRecord(Stage.Sketch, Stage.Records, Filed, Pick),
              "selecting a folder yields no pick - there is nothing to grab");
        Claim(!Pick.Standing(), "and the pick is left standing at nothing");
    }

    {
        WorkspaceRecord Measure = {};
        Measure.Subject = WorkspaceRecordSubject::Dimension;
        Measure.Naming  = "dimension";
        const WorkspaceRecordName Filed = Stage.Records.Declare(Measure);

        SketchPick Pick = {};
        Claim(!ResolvePickForRecord(Stage.Sketch, Stage.Records, Filed, Pick),
              "selecting a dimension yields no pick either");
    }

    // A record that does not exist.
    {
        SketchPick Pick = {};
        Claim(!ResolvePickForRecord(Stage.Sketch, Stage.Records, { 9999u }, Pick),
              "a record index that names nothing yields no pick");
        Claim(!ResolvePickForRecord(Stage.Sketch, Stage.Records, {}, Pick),
              "and neither does an unassigned record name");
    }
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("SKETCH PICKING PROOF\n");
    std::printf("=========================================================================\n");

    ProvePriority();
    ProveOwnership();
    ProvePivots();
    ProvePlacements();
    ProveRecordSelection();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
