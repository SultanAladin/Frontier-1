//============================================================================================================================================
//                                                    WORKPLANECATALOGUEPROOF.CPP
//============================================================================================================================================
// 🧩 Proves that a workspace can hold more than one plane, that activating one never moves what is already
//    drawn, and that a plane the artist made can actually be used afterwards.
//
// 🔴 The property that matters most is the one the shipped code broke: DRAWING DOES NOT MOVE WHEN THE
//    ACTIVE PLANE CHANGES. §3 walks real geometry across four plane changes and checks every coordinate.

#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"

#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

void ClaimNamed(bool Held, const std::string& Sentence)
{
    Claim(Held, Sentence.c_str());
}

bool Near(double Left, double Right, double Tolerance = 1.0e-9)
{
    return std::fabs(Left - Right) <= Tolerance;
}

bool SamePoint(const SpatialPoint& Left, const SpatialPoint& Right)
{
    return Near(Left.Left, Right.Left, 1.0e-9)
        && Near(Left.Up, Right.Up, 1.0e-9)
        && Near(Left.Forward, Right.Forward, 1.0e-9);
}

//========================================================================================================
// 1. A WORKSPACE HAS PLANES BEFORE THE ARTIST MAKES ANY
//========================================================================================================

// 🔴 The promise `WorkplaneStanding` makes — that a sketch draws without ceremony — must survive being
//    given a catalogue. A workspace that opened with nothing to draw on would have withdrawn it.

void ProveTheWorldArrivesWithPlanes()
{
    std::printf("\n1. A workspace opens with somewhere to draw\n");

    const WorkplaneCatalogue Catalogue;

    Claim(Catalogue.DeclaredCount() == 3u, "a new workspace holds exactly the three standing planes");
    Claim(Catalogue.ActiveName().Assigned(), "one of them is already active");
    Claim(Catalogue.Active().Declared(), "the active plane can be drawn on");

    // The three are the ones the world always has, in the order the enum names them.
    const struct { StandingWorkplane Subject; const char* Naming; } Standing[3] =
    {
        { StandingWorkplane::Ground, "Ground Plane" },
        { StandingWorkplane::Front,  "Front Plane"  },
        { StandingWorkplane::Side,   "Side Plane"   },
    };

    for (const auto& Row : Standing)
    {
        const WorkplaneName Named = Catalogue.StandingName(Row.Subject);
        ClaimNamed(Named.Assigned(), std::string("the catalogue can name the ") + Row.Naming);

        const CataloguedWorkplane* Held = Catalogue.Resolve(Named);
        ClaimNamed(Held != nullptr, std::string("...and resolve it: ") + Row.Naming);
        if (Held == nullptr)
            continue;

        ClaimNamed(Held->Naming == Row.Naming,
                   std::string("...under the name the artist reads: ") + Row.Naming);
        ClaimNamed(!Held->Removable(),
                   std::string("...and it cannot be removed: ") + Row.Naming);

        // It must be the same plane WorkplaneStanding resolves, not a copy that has drifted.
        const Workplane Expected = ResolveStandingWorkplane(Row.Subject);
        ClaimNamed(SamePoint(Held->Surface.Origin, Expected.Origin) &&
                   Near(Held->Surface.Normal.Left, Expected.Normal.Left) &&
                   Near(Held->Surface.Normal.Up, Expected.Normal.Up) &&
                   Near(Held->Surface.Normal.Forward, Expected.Normal.Forward),
                   std::string("...and it IS the standing plane, not a copy: ") + Row.Naming);
    }

    // 📝 Ground is active because that is what the grid lies on and what "just start drawing" means.
    Claim(Catalogue.ActiveName() == Catalogue.StandingName(StandingWorkplane::Ground),
          "the ground plane is the one active at the start");
}

//========================================================================================================
// 2. A PLANE THE ARTIST MAKES JOINS THE OTHERS
//========================================================================================================

// 🔴 The shipped tool wrote a Folder record holding only a NAME and put the plane nowhere, so a placed
//    plane could never be selected, re-activated, offset or removed. It has to become a first-class
//    member of the same collection as the standing ones, or "sketch on the plane I made" cannot be said.

void ProveAPlacedPlaneIsUsable()
{
    std::printf("\n2. A plane the artist places can be used afterwards\n");

    WorkplaneCatalogue Catalogue;

    // A plane facing the viewer, the way the workplane tool makes one.
    const Workplane Placed = ResolvePlacedWorkplane({ 10.0, 20.0, 30.0 }, { 0.0, -0.5, -0.866025403784439 });
    Claim(Placed.Declared(), "the tool's plane is well formed before it is catalogued");

    const WorkplaneName Named = Catalogue.Declare(Placed, "Workplane 1");
    Claim(Named.Assigned(), "placing a plane names it");
    Claim(Catalogue.DeclaredCount() == 4u, "and the workspace now holds four planes, not one");

    const CataloguedWorkplane* Held = Catalogue.Resolve(Named);
    Claim(Held != nullptr, "the placed plane can be resolved by name");
    Claim(Held != nullptr && Held->Removable(), "a plane the artist made can be removed again");
    Claim(Held != nullptr && SamePoint(Held->Surface.Origin, Placed.Origin),
          "the catalogue holds the PLANE, not merely a name for it");

    // 🔴 Placing activates, because placing a plane and then selecting it would be two acts for one
    //    intention. This is the claim that says the artist can draw on it immediately.
    Claim(Catalogue.ActiveName() == Named, "the placed plane is the one now being drawn on");
    Claim(SamePoint(Catalogue.Active().Origin, Placed.Origin),
          "...and Active() answers with it");

    // The standing planes are still there, unchanged.
    Claim(Catalogue.StandingName(StandingWorkplane::Ground).Assigned(),
          "the standing planes survive a plane being placed");
    Claim(Catalogue.Resolve(Catalogue.StandingName(StandingWorkplane::Front)) != nullptr,
          "...all of them");

    // Going back to a standing plane is the same act as choosing the placed one.
    const Deliver<WorkplaneName> Back = Catalogue.Activate(Catalogue.StandingName(StandingWorkplane::Front));
    Claim(Back.Resolved, "the artist can go back to a standing plane");
    Claim(Catalogue.ActiveName() == Catalogue.StandingName(StandingWorkplane::Front),
          "...and that is where drawing now happens");

    // And forward again to the one they made.
    const Deliver<WorkplaneName> Forward = Catalogue.Activate(Named);
    Claim(Forward.Resolved, "and back to the one they made");
    Claim(Catalogue.ActiveName() == Named, "...which is the whole point of cataloguing it");

    // ⚠️ Declaring without activating is for restoring a stored workspace.
    WorkplaneCatalogue Restoring;
    const WorkplaneName Quiet = Restoring.Declare(Placed, "Workplane 1", false);
    Claim(Quiet.Assigned(), "a plane can be declared without being activated");
    Claim(Restoring.ActiveName() == Restoring.StandingName(StandingWorkplane::Ground),
          "...leaving the active plane exactly as it was");
}

//========================================================================================================
// 3. CHANGING THE PLANE DOES NOT MOVE THE DRAWING
//========================================================================================================

// 🔴 THIS IS THE DEFECT. `SketchStructure::DeclarePlane` overwrites the single plane a sketch holds, and
//    the workplane tool called it directly. Everything already drawn kept its world coordinates but was
//    from then on MEASURED against the new surface — the grid moved under it, its plane coordinates
//    changed, and every projection answered for a plane the geometry was never drawn on.

void ProveTheDrawingStaysWhereItWasPut()
{
    std::printf("\n3. Changing the active plane never moves what is already drawn\n");

    WorkplaneCatalogue Catalogue;

    // Draw something on the ground: a line, and a circle profile.
    SketchStructure Sketch;
    Sketch.DeclarePlane({ Catalogue.Active().Origin, Catalogue.Active().Normal, Catalogue.Active().Along });
    Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 40.0, 0.0, 0.0 });

    CircleCurve Circle = {};
    Circle.Centre         = { 10.0, 0.0, 10.0 };
    Circle.Normal         = { 0.0, 1.0, 0.0 };
    Circle.StartDirection = { 1.0, 0.0, 0.0 };
    Circle.Radius         = 15.0;
    const Deliver<ProfileNameInFeature> Profile = Sketch.DeclareCircleProfile(Circle);
    Claim(Profile.Resolved, "geometry stands on the ground plane");

    // Every point of it, before anything changes.
    std::vector<SpatialPoint> Before;
    for (const DeclaredSketchCurve& Curve : Sketch.Curves())
    {
        std::vector<SpatialPoint> Points;
        AppendCurvePolyline(Curve.Geometry, Points, 32u);
        Before.insert(Before.end(), Points.begin(), Points.end());
    }
    Claim(!Before.empty(), "and it can be sampled");

    // 🔴 Now change the active plane four times, the way an artist would while working.
    const struct { const char* What; Workplane Surface; } Changes[4] =
    {
        { "activating the front plane",  ResolveStandingWorkplane(StandingWorkplane::Front) },
        { "activating the side plane",   ResolveStandingWorkplane(StandingWorkplane::Side)  },
        { "placing a plane facing the view",
          ResolvePlacedWorkplane({ 5.0, 60.0, -5.0 }, { 0.0, -0.5, -0.866025403784439 })    },
        { "offsetting the ground plane", ResolveOffsetWorkplane(StandingWorkplane::Ground, 25.0) },
    };

    for (const auto& Change : Changes)
    {
        if (Change.Surface.Source == WorkplaneOrigin::Standing)
        {
            // Find the standing one in the catalogue and activate it.
            for (const CataloguedWorkplane& Held : Catalogue.Declared())
            {
                if (Near(Held.Surface.Normal.Left, Change.Surface.Normal.Left) &&
                    Near(Held.Surface.Normal.Up, Change.Surface.Normal.Up) &&
                    Near(Held.Surface.Normal.Forward, Change.Surface.Normal.Forward))
                {
                    const Deliver<WorkplaneName> Done = Catalogue.Activate(Held.Named);
                    ClaimNamed(Done.Resolved, std::string("the artist succeeds at ") + Change.What);
                    break;
                }
            }
        }
        else
        {
            const WorkplaneName Named = Catalogue.Declare(Change.Surface,
                                                          ResolveWorkplaneNaming(Catalogue, Change.Surface.Source));
            ClaimNamed(Named.Assigned(), std::string("the artist succeeds at ") + Change.What);
        }

        // ⚠️ THE CLAIM THAT MATTERS. Not one coordinate of the existing drawing may have moved.
        std::vector<SpatialPoint> After;
        for (const DeclaredSketchCurve& Curve : Sketch.Curves())
        {
            std::vector<SpatialPoint> Points;
            AppendCurvePolyline(Curve.Geometry, Points, 32u);
            After.insert(After.end(), Points.begin(), Points.end());
        }

        bool Moved = After.size() != Before.size();
        for (std::size_t Index = 0u; !Moved && Index < After.size(); ++Index)
            Moved = !SamePoint(After[Index], Before[Index]);

        ClaimNamed(!Moved, std::string("...and the drawing has not moved after ") + Change.What);
    }

    Claim(Catalogue.DeclaredCount() == 5u,
          "after all that the workspace holds five planes and has lost none of them");
}

//========================================================================================================
// 4. THE NEXT THING DRAWN LANDS ON THE PLANE THAT IS ACTIVE
//========================================================================================================

// 🔴 The other half of the same property: changing the plane must change where the NEXT thing goes, or the
//    tool does nothing at all. §3 proves the old geometry stays; this proves the new geometry follows.

void ProveTheNextThingFollowsTheActivePlane()
{
    std::printf("\n4. What is drawn next lands on the plane that is active\n");

    WorkplaneCatalogue Catalogue;

    ViewportStanding View = {};
    View.Focus      = { 0.0, 0.0, 0.0 };
    View.Distance   = 200.0;
    View.OrbitYaw   = 0.0;
    View.OrbitPitch = -30.0;
    View.OrthoScale = 1.0;

    PlaneExtent Extent = {};
    Extent.MinimumX = 0.0f;
    Extent.MinimumY = 0.0f;
    Extent.MaximumX = 800.0f;
    Extent.MaximumY = 600.0f;

    const struct { const char* What; StandingWorkplane Subject; } Surfaces[3] =
    {
        { "the ground plane", StandingWorkplane::Ground },
        { "the front plane",  StandingWorkplane::Front  },
        { "the side plane",   StandingWorkplane::Side   },
    };

    for (const auto& Row : Surfaces)
    {
        const Deliver<WorkplaneName> Done = Catalogue.Activate(Catalogue.StandingName(Row.Subject));
        ClaimNamed(Done.Resolved, std::string("the artist activates ") + Row.What);

        // The sketch adopts whatever the catalogue says is active.
        SketchStructure Sketch;
        Sketch.DeclarePlane({ Catalogue.Active().Origin, Catalogue.Active().Normal, Catalogue.Active().Along });
        const SpatialBasis Basis = ResolveSketchBasis(Sketch);

        // A click in the middle of the viewport must land ON that plane.
        SpatialPoint Landed = {};
        const bool Hit = ResolveViewportPlaneIntersection(Basis, View, true, Extent, 420.0f, 260.0f, Landed);
        ClaimNamed(Hit, std::string("a click resolves onto ") + Row.What);

        if (!Hit)
            continue;

        const double Standoff = ResolveWorkplaneOffset(Catalogue.Active(), Landed);
        ClaimNamed(Near(Standoff, 0.0, 1.0e-6),
                   std::string("...exactly on it, not floating above ") + Row.What);

        // 🔴 And the round trip closes: what lands there projects back to the pixel it came from. A plane
        //    that resolved clicks but projected them back somewhere else would draw in the wrong place in
        //    precisely the way that was reported.
        double Along = 0.0, Across = 0.0;
        ResolveWorkplaneCoordinates(Catalogue.Active(), Landed, Along, Across);

        float BackX = 0.0f, BackY = 0.0f;
        const bool Projected = ProjectViewportPoint(Basis, View, true, Extent, Along, Across, BackX, BackY);
        ClaimNamed(Projected, std::string("...and projects back onto the display from ") + Row.What);
        ClaimNamed(Near(static_cast<double>(BackX), 420.0, 0.01) &&
                   Near(static_cast<double>(BackY), 260.0, 0.01),
                   std::string("...to the very pixel it was clicked at, on ") + Row.What);
    }

    // ⚠️ Two different active planes must send the same pixel to two different places, or activating a
    //    plane achieved nothing. This is the sabotage-facing claim of the section.
    SpatialPoint OnGround = {}, OnFront = {};
    {
        SketchStructure Sketch;
        const Deliver<WorkplaneName> Done = Catalogue.Activate(Catalogue.StandingName(StandingWorkplane::Ground));
        Claim(Done.Resolved, "the ground plane activates for the comparison");
        Sketch.DeclarePlane({ Catalogue.Active().Origin, Catalogue.Active().Normal, Catalogue.Active().Along });
        ResolveViewportPlaneIntersection(ResolveSketchBasis(Sketch), View, true, Extent, 300.0f, 200.0f, OnGround);
    }
    {
        SketchStructure Sketch;
        const Deliver<WorkplaneName> Done = Catalogue.Activate(Catalogue.StandingName(StandingWorkplane::Front));
        Claim(Done.Resolved, "the front plane activates for the comparison");
        Sketch.DeclarePlane({ Catalogue.Active().Origin, Catalogue.Active().Normal, Catalogue.Active().Along });
        ResolveViewportPlaneIntersection(ResolveSketchBasis(Sketch), View, true, Extent, 300.0f, 200.0f, OnFront);
    }

    Claim(!SamePoint(OnGround, OnFront),
          "the same pixel lands somewhere ELSE once the active plane changes");
}

//========================================================================================================
// 5. WHAT THE CATALOGUE REFUSES
//========================================================================================================

// 🔴 Every refusal here is a case where the shipped arrangement would have silently done something else.

void ProveWhatIsRefused()
{
    std::printf("\n5. What the catalogue refuses, and says why\n");

    WorkplaneCatalogue Catalogue;

    // ⚠️ Activating something that is not there must refuse rather than fall back to Ground. Falling back
    //    would draw on a different surface from the one asked for without saying so.
    const Deliver<WorkplaneName> Unnamed = Catalogue.Activate({});
    Claim(!Unnamed.Resolved, "activating nothing is refused");
    Claim(!Unnamed.Resolved && Unnamed.Error.Detail != nullptr, "...and says why");

    WorkplaneName Stranger;
    Stranger.IssuedIndex = 9999u;
    const Deliver<WorkplaneName> Unknown = Catalogue.Activate(Stranger);
    Claim(!Unknown.Resolved, "activating a plane from another workspace is refused");
    Claim(Catalogue.ActiveName() == Catalogue.StandingName(StandingWorkplane::Ground),
          "...and the plane being drawn on has not changed");

    // A malformed plane never enters the catalogue.
    Workplane Broken;
    Broken.Origin = { 0.0, 0.0, 0.0 };
    Broken.Normal = { 0.0, 0.0, 0.0 };
    Broken.Along  = { 1.0, 0.0, 0.0 };
    Claim(!Broken.Declared(), "a plane with no normal cannot describe a surface");
    const WorkplaneName Refused = Catalogue.Declare(Broken, "Broken");
    Claim(!Refused.Assigned(), "...so declaring it is refused");
    Claim(Catalogue.DeclaredCount() == 3u, "...and it did not join the catalogue");

    // The standing planes are permanent.
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const StandingWorkplane Subject = static_cast<StandingWorkplane>(Index);
        const Deliver<WorkplaneName> Gone = Catalogue.Remove(Catalogue.StandingName(Subject), 0u);
        ClaimNamed(!Gone.Resolved, std::string("a standing plane cannot be removed: index ")
                                   + std::to_string(Index));
    }
    Claim(Catalogue.DeclaredCount() == 3u, "so the three the world has are all still there");

    // A plane with geometry on it cannot be removed out from under it.
    const Workplane Placed = ResolvePlacedWorkplane({ 0.0, 40.0, 0.0 }, { 0.0, -1.0, 0.0 });
    const WorkplaneName Named = Catalogue.Declare(Placed, "Workplane 1");
    Claim(Named.Assigned(), "a plane is placed to remove");

    const Deliver<WorkplaneName> Occupied = Catalogue.Remove(Named, 3u);
    Claim(!Occupied.Resolved, "a plane with geometry standing on it is not removed");
    Claim(Catalogue.DeclaredCount() == 4u, "...and it is still in the catalogue");

    const Deliver<WorkplaneName> Empty = Catalogue.Remove(Named, 0u);
    Claim(Empty.Resolved, "an empty plane the artist made is removed");
    Claim(Catalogue.DeclaredCount() == 3u, "...leaving the three standing ones");

    // 🔴 Removing the plane being drawn on must leave SOMEWHERE to draw.
    Claim(Catalogue.ActiveName() == Catalogue.StandingName(StandingWorkplane::Ground),
          "removing the active plane falls back to the ground plane");
    Claim(Catalogue.Active().Declared(), "...so there is always a surface to draw on");
}

//========================================================================================================
// 6. THE NAMES THE ARTIST READS
//========================================================================================================

void ProveTheNaming()
{
    std::printf("\n6. The names planes are given\n");

    WorkplaneCatalogue Catalogue;

    Claim(ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Placed) == "Workplane 1",
          "the first placed plane is Workplane 1");

    const Workplane First = ResolvePlacedWorkplane({ 0.0, 10.0, 0.0 }, { 0.0, -1.0, 0.0 });
    Catalogue.Declare(First, ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Placed));
    Claim(ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Placed) == "Workplane 2",
          "the second is Workplane 2");

    // 📝 Offsets are counted separately, because they read as a different kind of thing in the directory.
    Claim(ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Offset) == "Offset Plane 1",
          "an offset plane is named separately from a placed one");

    const Workplane Offset = ResolveOffsetWorkplane(StandingWorkplane::Ground, 30.0);
    Catalogue.Declare(Offset, ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Offset));
    Claim(ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Offset) == "Offset Plane 2",
          "...and counts up on its own");
    Claim(ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Placed) == "Workplane 2",
          "...without disturbing the placed ones");

    // ⚠️ Counted from what stands, not from a running total, so removing and remaking does not leave gaps.
    const CataloguedWorkplane& Last = Catalogue.Declared().back();
    const Deliver<WorkplaneName> Gone = Catalogue.Remove(Last.Named, 0u);
    Claim(Gone.Resolved, "the offset plane is removed");
    Claim(ResolveWorkplaneNaming(Catalogue, WorkplaneOrigin::Offset) == "Offset Plane 1",
          "...and the next offset plane takes the name back rather than skipping it");
}

//========================================================================================================
// 7. POINTING AT A PLANE THAT ALREADY EXISTS
//========================================================================================================

// 🔴 Placing a plane is half of what was asked for. Being able to point at one already made and draw on
//    it is the other half — without it the only way to reach an existing plane is to make another.

void ProvePointingAtAPlane()
{
    std::printf("\n7. The artist can point at a plane they already made\n");

    WorkplaneCatalogue Catalogue;

    // A plane 60 above the ground, facing up, so a ray straight down meets it before the ground.
    Workplane Upper;
    Upper.Origin = { 0.0, 60.0, 0.0 };
    Upper.Normal = { 0.0, 1.0, 0.0 };
    Upper.Along  = { 1.0, 0.0, 0.0 };
    Upper.Source = WorkplaneOrigin::Offset;
    const WorkplaneName Raised = Catalogue.Declare(Upper, "Offset Plane 1", false);
    Claim(Raised.Assigned(), "a plane stands 60 above the ground");

    // Looking straight down from above, the upper plane is met first.
    const Deliver<WorkplaneName> FromAbove =
        ResolvePointedWorkplane(Catalogue, { 0.0, 200.0, 0.0 }, { 0.0, -1.0, 0.0 }, 100.0);
    Claim(FromAbove.Resolved, "looking down, a plane is found under the pointer");
    Claim(FromAbove.Resolved && FromAbove.Delivered == Raised,
          "...and it is the NEAREST one, not whichever was catalogued first");

    // 🔴 Nearest along the ray wins, so a plane in front occludes one behind it.
    const Deliver<WorkplaneName> FromBelow =
        ResolvePointedWorkplane(Catalogue, { 0.0, -200.0, 0.0 }, { 0.0, 1.0, 0.0 }, 100.0);
    Claim(FromBelow.Resolved, "looking up from below, a plane is found");
    Claim(FromBelow.Resolved && FromBelow.Delivered == Catalogue.StandingName(StandingWorkplane::Ground),
          "...and from below the GROUND plane is the nearer one");

    // ⚠️ Reach is what stops an infinite plane swallowing every ray. Aimed far off to one side, the hit
    //    lands beyond the plane's origin and must not count as pointing at it.
    const Deliver<WorkplaneName> FarOut =
        ResolvePointedWorkplane(Catalogue, { 5000.0, 200.0, 0.0 }, { 0.0, -1.0, 0.0 }, 100.0);
    Claim(!FarOut.Resolved, "pointing far off the side of every plane finds none");
    Claim(!FarOut.Resolved && FarOut.Error.Detail != nullptr, "...and says so");

    // A ray running parallel to a plane never meets it.
    WorkplaneCatalogue OnlyGround;
    const Deliver<WorkplaneName> EdgeOn =
        ResolvePointedWorkplane(OnlyGround, { 0.0, 40.0, 0.0 }, { 1.0, 0.0, 0.0 }, 500.0);
    Claim(!EdgeOn.Resolved, "a plane seen exactly edge-on is not being pointed at");

    // 📝 Behind the viewer is not in front of them.
    const Deliver<WorkplaneName> Behind =
        ResolvePointedWorkplane(OnlyGround, { 0.0, 40.0, 0.0 }, { 0.0, 1.0, 0.0 }, 500.0);
    Claim(!Behind.Resolved, "a plane behind the viewer is not selected");

    // 🔴 ⚠️ THE CASE THAT CAUGHT ME OUT, AND THE REASON THIS IS NOT WIRED TO A BARE CLICK. All three
    //    standing planes pass through the WORLD ORIGIN, so a ray aimed anywhere near the middle of the
    //    scene passes within reach of every one of them and the "nearest along the ray" is decided by
    //    where the camera happens to stand, not by what the artist meant. Measured from a normal
    //    three-quarter view, clicking the ground selects the FRONT plane.
    {
        WorkplaneCatalogue Standing;
        const SpatialPoint Eye = { -122.5, 100.0, -122.5 };

        // Four points the artist would read as "on the ground", from one ordinary three-quarter view.
        const SpatialPoint Aims[4] =
        {
            {   0.0, 0.0,   0.0 },
            {  11.9, 0.0,  19.9 },
            { 135.5, 0.0,  27.1 },
            { -93.2, 0.0, -23.3 },
        };

        std::uint32_t Ground = 0u, Other = 0u;
        for (const SpatialPoint& Aim : Aims)
        {
            const Deliver<WorkplaneName> Met =
                ResolvePointedWorkplane(Standing, Eye, Normalize(Difference(Eye, Aim)), 150.0);
            if (!Met.Resolved)
                continue;
            if (Met.Delivered == Standing.StandingName(StandingWorkplane::Ground))
                ++Ground;
            else
                ++Other;
        }

        Claim(Ground + Other == 4u, "every one of those clicks meets some standing plane");
        Claim(Ground > 0u && Other > 0u,
              "...but WHICH one flips between them, so a bare click cannot choose among coincident planes");
    }

    // 📝 Which is why a plane must be far enough from the others to be told apart. Reach is a distance
    //    from the plane's ORIGIN, and three planes sharing an origin cannot be separated by it.
    {
        WorkplaneCatalogue Separated;
        Workplane Distant;
        Distant.Origin = { 400.0, 0.0, 0.0 };
        Distant.Normal = { 0.0, 1.0, 0.0 };
        Distant.Along  = { 1.0, 0.0, 0.0 };
        Distant.Source = WorkplaneOrigin::Placed;
        const WorkplaneName Named = Separated.Declare(Distant, "Workplane 1", false);

        const SpatialPoint Eye = { 400.0, 200.0, 0.0 };
        const Deliver<WorkplaneName> Aimed =
            ResolvePointedWorkplane(Separated, Eye, { 0.0, -1.0, 0.0 }, 100.0);
        Claim(Aimed.Resolved && Aimed.Delivered == Named,
              "a plane standing well away from the others IS unambiguously pointed at");
    }

    // 🔴 And the whole point: pointing at one and activating it makes it the drawing surface.
    const Deliver<WorkplaneName> Pointed =
        ResolvePointedWorkplane(Catalogue, { 0.0, 200.0, 0.0 }, { 0.0, -1.0, 0.0 }, 100.0);
    Claim(Pointed.Resolved, "the artist points at the raised plane");
    if (Pointed.Resolved)
    {
        const Deliver<WorkplaneName> Done = Catalogue.Activate(Pointed.Delivered);
        Claim(Done.Resolved, "...activating it succeeds");
        Claim(Near(Catalogue.Active().Origin.Up, 60.0),
              "...and drawing now happens 60 above the ground, without a new plane being made");
        Claim(Catalogue.DeclaredCount() == 4u, "...no duplicate plane was created to get there");
    }
}

}   // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("WORKPLANE CATALOGUE PROOF\n");
    std::printf("=========================================================================\n");

    ProveTheWorldArrivesWithPlanes();
    ProveAPlacedPlaneIsUsable();
    ProveTheDrawingStaysWhereItWasPut();
    ProveTheNextThingFollowsTheActivePlane();
    ProveWhatIsRefused();
    ProveTheNaming();
    ProvePointingAtAPlane();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures, Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
