//============================================================================================================================================
//                                                     TOOLAVAILABILITYPROOF.CPP
//============================================================================================================================================
// 🧩 Proves that what the tool panel is told about the selection is decided by WHAT IS SELECTED, and not
//    by what happened to be left in the context from the call before.
//
// 🔴 THE SHAPE OF THE DEFECT THIS GUARDS AGAINST. The host reset twelve flags to false and then ran a
//    nine-arm switch in which each arm assigned only the flags it cared about. That works exactly as long
//    as the reset and the arms agree — and nothing checked that they did. An arm that forgot a flag
//    inherited the reset's value; a reset that forgot a flag let the previous selection's value survive
//    into the next. §1 below writes a saturated context first and demands the answer be identical to the
//    one produced from a blank context, which is the claim the shipped shape could not make.

#include "SlateWorkspace/Discipline/ToolAvailability/Api/ToolAvailability.h"
#include "SlateShape/Sketch/SketchEditing/Api/SketchEditing.h"

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

const char* SubjectText(WorkspaceRecordSubject Subject)
{
    switch (Subject)
    {
        case WorkspaceRecordSubject::Point:         return "a point";
        case WorkspaceRecordSubject::OpenCurve:     return "an open curve";
        case WorkspaceRecordSubject::ClosedProfile: return "a closed profile";
        case WorkspaceRecordSubject::ThinSurface:   return "a thin surface";
        case WorkspaceRecordSubject::Solid:         return "a solid";
        case WorkspaceRecordSubject::Dimension:     return "a dimension";
        case WorkspaceRecordSubject::Constraint:    return "a constraint";
        case WorkspaceRecordSubject::Pattern:       return "a pattern";
        case WorkspaceRecordSubject::Mirror:        return "a mirror";
        case WorkspaceRecordSubject::Folder:        return "a folder";
        case WorkspaceRecordSubject::SubjectCount:  break;
    }
    return "an unnamed subject";
}

/// 🧩 A context with every field set to the OPPOSITE of its resting value.
/// note 🔴 This is the instrument. Anything the unit fails to write stays visibly wrong.
ParametricToolsContext Saturated()
{
    ParametricToolsContext Dirty = {};
    Dirty.ActiveDimension          = ParametricToolDimension::Solid;
    Dirty.ProfileCount             = 99u;
    Dirty.PerimeterEdgeCount       = 99u;
    Dirty.ExistingCircleCount      = 99u;
    Dirty.SolidCount               = 99u;
    Dirty.SelectedCount            = 99u;
    Dirty.AxisAvailability         = true;
    Dirty.PathAvailability         = true;
    Dirty.SupportMaterialCondition = true;
    Dirty.TangentEndpointCondition = true;
    Dirty.OpeningCondition         = true;
    Dirty.MeasurableCondition      = true;
    Dirty.ClosedProfileCondition   = true;
    Dirty.ReferencePlaneCondition  = true;
    Dirty.WorkplaneActivation      = true;
    Dirty.PendingGeometryCondition = true;
    Dirty.SourceImageryCondition   = true;
    return Dirty;
}

bool Same(const ParametricToolsContext& Left, const ParametricToolsContext& Right)
{
    return Left.ActiveDimension          == Right.ActiveDimension
        && Left.ProfileCount             == Right.ProfileCount
        && Left.PerimeterEdgeCount       == Right.PerimeterEdgeCount
        && Left.ExistingCircleCount      == Right.ExistingCircleCount
        && Left.SolidCount               == Right.SolidCount
        && Left.SelectedCount            == Right.SelectedCount
        && Left.AxisAvailability         == Right.AxisAvailability
        && Left.PathAvailability         == Right.PathAvailability
        && Left.SupportMaterialCondition == Right.SupportMaterialCondition
        && Left.TangentEndpointCondition == Right.TangentEndpointCondition
        && Left.OpeningCondition         == Right.OpeningCondition
        && Left.MeasurableCondition      == Right.MeasurableCondition
        && Left.ClosedProfileCondition   == Right.ClosedProfileCondition
        && Left.ReferencePlaneCondition  == Right.ReferencePlaneCondition
        && Left.WorkplaneActivation      == Right.WorkplaneActivation
        && Left.PendingGeometryCondition == Right.PendingGeometryCondition
        && Left.SourceImageryCondition   == Right.SourceImageryCondition;
}

/// 🧩 A directory holding one record of the given subject, and the context that selects its row.
struct Fixture
{
    WorkspaceRecordStructure     Records   = {};
    WorkspaceDirectoryProjection Directory = {};
    ParametricWorkspaceContext   Applied   = {};
};

Fixture WithOneRecord(WorkspaceRecordSubject Subject)
{
    Fixture Built;

    WorkspaceRecord Record = {};
    Record.Subject = Subject;
    Record.Naming  = "the record under test";
    const WorkspaceRecordName Name = Built.Records.Declare(Record);

    WorkspaceDirectoryRow Row = {};
    Row.Role    = WorkspaceDirectoryRowRole::Record;
    Row.Record  = Name;
    Row.Subject = Subject;
    Row.Naming  = "the record under test";
    Built.Directory.Rows.push_back(Row);

    Built.Applied.RowTaken = 0u;
    return Built;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    1. THE ANSWER DOES NOT DEPEND ON WHAT CAME BEFORE
//------------------------------------------------------------------------------------------------------------------------

void ProveThePreviousCallCannotLeak()
{
    std::printf("\n1. What the panel is told depends on the selection, not on the call before it\n");

    SketchStructure Sketch;

    for (std::uint32_t Index = 0u; Index < static_cast<std::uint32_t>(WorkspaceRecordSubject::SubjectCount); ++Index)
    {
        const WorkspaceRecordSubject Subject = static_cast<WorkspaceRecordSubject>(Index);
        Fixture Built = WithOneRecord(Subject);

        ParametricToolsContext FromBlank = {};
        ResolveToolContext(Built.Directory, Built.Records, Sketch, Built.Applied, FromBlank);

        ParametricToolsContext FromDirty = Saturated();
        ResolveToolContext(Built.Directory, Built.Records, Sketch, Built.Applied, FromDirty);

        // 🔴 The load-bearing claim. Every field the unit owns must be written on every call.
        ClaimNamed(Same(FromBlank, FromDirty),
                   std::string("selecting ") + SubjectText(Subject)
                   + " answers the same whatever the context held before");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                       2. EACH SUBJECT OFFERS WHAT IT MEANS
//------------------------------------------------------------------------------------------------------------------------

void ProveEachSubjectOffersWhatItMeans()
{
    std::printf("\n2. What each thing offers follows from what it IS\n");

    // 🔴 ⚠️ STATED INDEPENDENTLY OF THE TABLE. Reading the expectation out of `AvailabilityFor` and then
    //    checking `AvailabilityFor` against it proves only that the unit agrees with itself — a mistake
    //    already made once in this codebase, in the constraint proof, where a sabotage survived because of
    //    exactly that. These follow from the geometry: you can revolve about a curve, sweep along one,
    //    shell a surface, and take a material on something with a face.
    struct Expectation
    {
        WorkspaceRecordSubject Subject;
        bool                   Axis;
        bool                   Path;
        bool                   SupportsMaterial;
        bool                   ClosedProfile;
        bool                   Measurable;
    };

    const Expectation Expected[] =
    {
        { WorkspaceRecordSubject::Point,         false, false, false, false, true  },
        { WorkspaceRecordSubject::OpenCurve,     true,  true,  false, false, true  },
        { WorkspaceRecordSubject::ClosedProfile, true,  true,  false, true,  true  },
        { WorkspaceRecordSubject::ThinSurface,   false, false, true,  false, true  },
        { WorkspaceRecordSubject::Solid,         true,  false, true,  true,  true  },
        { WorkspaceRecordSubject::Folder,        false, false, false, false, false },
    };

    for (const Expectation& Row : Expected)
    {
        const ToolAvailability Answered = AvailabilityFor(Row.Subject);
        ClaimNamed(Answered.Axis == Row.Axis,
                   std::string(Row.Axis ? "you can revolve about " : "you cannot revolve about ")
                   + SubjectText(Row.Subject));
        ClaimNamed(Answered.Path == Row.Path,
                   std::string(Row.Path ? "you can sweep along " : "you cannot sweep along ")
                   + SubjectText(Row.Subject));
        ClaimNamed(Answered.SupportsMaterial == Row.SupportsMaterial,
                   std::string(Row.SupportsMaterial ? "you can put a material on " : "you cannot put a material on ")
                   + SubjectText(Row.Subject));
        ClaimNamed(Answered.ClosedProfile == Row.ClosedProfile,
                   std::string(Row.ClosedProfile ? "it is closed: " : "it is not closed: ")
                   + SubjectText(Row.Subject));
        ClaimNamed(Answered.Measurable == Row.Measurable,
                   std::string(Row.Measurable ? "you can dimension " : "you cannot dimension ")
                   + SubjectText(Row.Subject));
    }

    // 📝 A folder is a place, not a thing: it must offer nothing at all.
    const ToolAvailability Folder = AvailabilityFor(WorkspaceRecordSubject::Folder);
    Claim(Folder.Dimension == ParametricToolDimension::Nothing,
          "a folder has no dimensionality, because it is not geometry");
    Claim(Folder.ProfileCount == 0u && Folder.SolidCount == 0u && Folder.PerimeterEdgeCount == 0u,
          "...and counts nothing");
}

//------------------------------------------------------------------------------------------------------------------------
//                                       3. NOTHING SELECTED OFFERS NOTHING
//------------------------------------------------------------------------------------------------------------------------

void ProveAnEmptySelectionOffersNothing()
{
    std::printf("\n3. With nothing selected, nothing that needs a selection is offered\n");

    SketchStructure Sketch;
    WorkspaceRecordStructure Records;
    WorkspaceDirectoryProjection Empty;
    ParametricWorkspaceContext Applied = {};

    ParametricToolsContext Answered = Saturated();
    ResolveToolContext(Empty, Records, Sketch, Applied, Answered);

    Claim(Answered.ActiveDimension == ParametricToolDimension::Nothing,
          "an empty directory leaves no active dimensionality");
    Claim(!Answered.AxisAvailability && !Answered.PathAvailability,
          "...nothing to revolve about and nothing to sweep along");
    Claim(!Answered.SupportMaterialCondition && !Answered.OpeningCondition,
          "...nothing to put a material on and nothing to open");
    Claim(Answered.ProfileCount == 0u && Answered.SolidCount == 0u,
          "...and no profiles and no solids");

    // 🔴 A row that is a FOLDER is not a selection of geometry, even though it is a row.
    Fixture Built = WithOneRecord(WorkspaceRecordSubject::Folder);
    ParametricToolsContext OnFolder = Saturated();
    ResolveToolContext(Built.Directory, Built.Records, Sketch, Built.Applied, OnFolder);
    Claim(OnFolder.ActiveDimension == ParametricToolDimension::Nothing,
          "selecting a folder offers no more than selecting nothing");

    // ⚠️ A row index past the end must not read past the end.
    Fixture Beyond = WithOneRecord(WorkspaceRecordSubject::Solid);
    Beyond.Applied.RowTaken = 4000u;
    ParametricToolsContext OutOfRange = Saturated();
    ResolveToolContext(Beyond.Directory, Beyond.Records, Sketch, Beyond.Applied, OutOfRange);
    Claim(OutOfRange.ActiveDimension == ParametricToolDimension::Nothing,
          "a row index past the end of the directory selects nothing, rather than reading past it");
}

//------------------------------------------------------------------------------------------------------------------------
//                                  4. THE SKETCH'S OWN PLANE, AND THE SELECTION'S
//------------------------------------------------------------------------------------------------------------------------

void ProveTheTwoSourcesOfAPlaneCombine()
{
    std::printf("\n4. A reference plane may come from the sketch OR from the selection\n");

    SketchStructure Bare;
    SketchStructure Planted;
    Planted.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });

    // 📝 A point offers no plane of its own, so this isolates the SKETCH as the source.
    Fixture Point = WithOneRecord(WorkspaceRecordSubject::Point);
    ParametricToolsContext FromSketch = {};
    ResolveToolContext(Point.Directory, Point.Records, Planted, Point.Applied, FromSketch);
    Claim(FromSketch.ReferencePlaneCondition,
          "a sketch with a plane offers one even when the selection does not");

    ParametricToolsContext FromNeither = {};
    ResolveToolContext(Point.Directory, Point.Records, Bare, Point.Applied, FromNeither);
    Claim(!FromNeither.ReferencePlaneCondition,
          "...and with neither a plane nor a plane-bearing selection, none is offered");

    // 🔴 A solid offers a plane of its own, so this isolates the SELECTION as the source. The host
    //    assigned true in three switch arms and never assigned false, which was correct only because the
    //    reset had already run — the two sources are now combined explicitly.
    Fixture Solid = WithOneRecord(WorkspaceRecordSubject::Solid);
    ParametricToolsContext FromSelection = {};
    ResolveToolContext(Solid.Directory, Solid.Records, Bare, Solid.Applied, FromSelection);
    Claim(FromSelection.ReferencePlaneCondition,
          "a solid offers a reference plane even when the sketch has none");

    Claim(!Bare.Declared() || !Bare.HeldPlane().Declared(),
          "the fixture's bare sketch really has no plane, so the claim above is not vacuous");

    // ⚠️ Workplane activation follows the sketch alone: a solid is not a workplane.
    Claim(!FromSelection.WorkplaneActivation,
          "selecting a solid does not by itself activate a workplane");
    Claim(FromSketch.WorkplaneActivation,
          "...but a sketch that has a plane does");
}

//------------------------------------------------------------------------------------------------------------------------
//                                       5. WHICH ROW A DIRECTORY OPENS ON
//------------------------------------------------------------------------------------------------------------------------

void ProveWhichRowOpens()
{
    std::printf("\n5. A freshly seated directory opens on something the artist can act upon\n");

    WorkspaceDirectoryProjection Directory;

    WorkspaceDirectoryRow FolderRow = {};
    FolderRow.Role    = WorkspaceDirectoryRowRole::Record;
    FolderRow.Subject = WorkspaceRecordSubject::Folder;
    FolderRow.Naming  = "Curves";
    Directory.Rows.push_back(FolderRow);

    WorkspaceDirectoryRow CurveRow = {};
    CurveRow.Role    = WorkspaceDirectoryRowRole::Record;
    CurveRow.Subject = WorkspaceRecordSubject::OpenCurve;
    CurveRow.Naming  = "Line 1";
    Directory.Rows.push_back(CurveRow);

    Claim(InitialRowIn(Directory) == 1u,
          "a directory whose first row is a folder opens on the first real record instead");

    WorkspaceDirectoryProjection OnlyFolders;
    OnlyFolders.Rows.push_back(FolderRow);
    Claim(InitialRowIn(OnlyFolders) == 0u,
          "a directory of nothing but folders falls back to the first row rather than refusing");

    WorkspaceDirectoryProjection Nothing;
    Claim(InitialRowIn(Nothing) == 0u,
          "an empty directory answers zero rather than reading from an empty list");

    // 🔴 ⚠️ ROW ZERO IS TICKED BY DEFAULT. `ParametricWorkspaceContext` declares
    //    `RowSelected[RowLimit] = { true }`, which initialises the FIRST element to true and the rest to
    //    false — so a default-constructed context already has row zero selected. My first version of the
    //    claim below assumed a blank context selected nothing and read the unit as wrong; it was the
    //    fixture that was wrong. The rows are cleared explicitly here so each claim says what it means.
    ParametricWorkspaceContext Applied = {};
    Claim(Applied.RowSelected[0],
          "a default context really does arrive with row zero already ticked");

    for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
        Applied.RowSelected[Index] = false;

    Claim(!AnyRowSelected(Applied, 0u), "with no rows, nothing is selected");
    Claim(!AnyRowSelected(Applied, 8u), "with rows but none ticked, nothing is selected");

    Applied.RowSelected[3] = true;
    Claim(AnyRowSelected(Applied, 8u), "a ticked row within reach is found");
    Claim(!AnyRowSelected(Applied, 3u), "...and one beyond the count asked for is not");
    // 🔴 THE CLAMP CANNOT BE PROVEN BY ITS ANSWER — it has to be proven by where it STOPS. An unclamped
    //    loop reads past the array and usually returns the same value, so a claim on the result alone
    //    passes against both versions; the sabotage that removed the clamp scored zero failures. The
    //    context is therefore placed inside a larger block whose tail is deliberately non-zero, and the
    //    claim is that a search over an absurd count still finds nothing once the real rows are clear.
    struct Bounded
    {
        ParametricWorkspaceContext Context = {};
        bool                       Beyond[256] = {};
    };

    Bounded Guarded;
    for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
        Guarded.Context.RowSelected[Index] = false;
    for (bool& Tail : Guarded.Beyond)
        Tail = true;

    Claim(!AnyRowSelected(Guarded.Context, 4000u),
          "a count larger than the storage stops at the storage, rather than reading into what follows it");
}

//------------------------------------------------------------------------------------------------------------------------
//                               6. A MOVED SUB-ELEMENT STAYS THE SELECTED SUB-ELEMENT
//------------------------------------------------------------------------------------------------------------------------

void ProveEditableSelectionTracksMovedPoint()
{
    std::printf("\n6. A selected vertex refreshes to where the geometry now is\n");

    SketchStructure Sketch;
    Sketch.DeclarePlane({ { 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
    const Deliver<ProfileNameInFeature> Triangle =
        Sketch.DeclareRegularPolygon({ 0.0, 0.0, 0.0 }, 20.0, 3u, SpatialDirection{ 1.0, 0.0, 0.0 });
    Claim(Triangle.Resolved, "a profile-only triangle can be declared");
    if (!Triangle.Resolved)
        return;

    WorkspaceRecordStructure Records;
    WorkspaceRecord ProfileRecord = {};
    ProfileRecord.Subject = WorkspaceRecordSubject::ClosedProfile;
    ProfileRecord.Profile = Triangle.Resolve();
    ProfileRecord.Naming = "Triangle";
    const WorkspaceRecordName Record = Records.Declare(ProfileRecord);

    const ProfileSpecification& Held = Sketch.Profiles()[Triangle.Resolve().IssuedIndex - 1u];
    const SketchCurveName FirstEdge = { Held.HeldLoops()[0].Traversal[0].TraversedCurve.IssuedIndex };

    std::vector<SketchPointPlacement> EdgePoints;
    Claim(ResolveSketchPoints(Sketch, FirstEdge, EdgePoints) && EdgePoints.size() == 2u,
          "the triangle exposes a corner to select");
    if (EdgePoints.size() != 2u)
        return;

    const SpatialPoint Original = EdgePoints[0].Position;
    const SpatialPoint Moved = { Original.Left + 11.0, Original.Up, Original.Forward + 7.0 };
    const SketchPick Vertex = ResolveSketchPickForElement(Sketch, Records, Original, 5.0, SelectionElement::Vertex);
    Claim(Vertex.Subject == SketchPickSubject::Point,
          "the chosen corner is selected as a point");
    Claim(Vertex.Record.IssuedIndex == Record.IssuedIndex,
          "and it belongs to the profile record the directory names");

    std::uint32_t MovedEndpoints = 0u;
    for (std::uint32_t CurveIndex = 1u; CurveIndex <= Sketch.Curves().size(); ++CurveIndex)
    {
        std::vector<SketchPointPlacement> Points;
        if (!ResolveSketchPoints(Sketch, { CurveIndex }, Points))
            continue;
        for (const SketchPointPlacement& Point : Points)
            if (std::fabs(Point.Position.Left - Original.Left) < 1.0e-9
             && std::fabs(Point.Position.Forward - Original.Forward) < 1.0e-9)
            {
                Claim(EnforceSketchPoint(Sketch, Point.Name, Moved).Resolved,
                      "moving one selected corner can update each coincident endpoint that shares it");
                ++MovedEndpoints;
            }
    }
    Claim(MovedEndpoints == 2u,
          "exactly two coincident endpoints shared the selected triangle corner");

    const SketchPick Refreshed = EditableSelection(Sketch, Records, Record, {}, Vertex);
    Claim(Refreshed.Subject == SketchPickSubject::Point,
          "the editable selection remains the vertex, not the whole profile");
    Claim(std::fabs(Refreshed.Position.Left - Moved.Left) < 1.0e-9
       && std::fabs(Refreshed.Position.Forward - Moved.Forward) < 1.0e-9,
          "and its position refreshes to the vertex's NEW location rather than the stale one it was picked at");
}

}   // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("TOOL AVAILABILITY — what is offered follows from what is selected\n");
    std::printf("=========================================================================\n");

    ProveThePreviousCallCannotLeak();
    ProveEachSubjectOffersWhatItMeans();
    ProveAnEmptySelectionOffersNothing();
    ProveTheTwoSourcesOfAPlaneCombine();
    ProveWhichRowOpens();
    ProveEditableSelectionTracksMovedPoint();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures, Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n\n");
    return Failures == 0u ? 0 : 1;
}
