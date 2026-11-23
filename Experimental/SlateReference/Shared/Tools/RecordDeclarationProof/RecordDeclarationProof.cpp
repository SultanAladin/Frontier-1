//============================================================================================================================================
//                                                     RECORDDECLARATIONPROOF.CPP
//============================================================================================================================================
// 🧩 Proves what happens when a sketched thing is written into the workspace directory: which folder it
//    lands in, what it is named, which semantics come with it, and how many undo steps it costs.
//
// 🔴 The claim this file exists to make is that FILING IS NOT ARBITRARY. A curve belongs under Sketch, a
//    dimension under Annotation, and a construction curve is still a curve. Those are decisions a reader
//    can check without knowing anything about the record structure's internals, which is exactly what a
//    proof should assert.

#include "SlateWorkspace/Discipline/RecordDeclaration/Api/RecordDeclaration.h"

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

/// 🧩 A workspace with the two top-level folders the host declares at startup.
struct Bench
{
    WorkspaceNameIndex        Naming;
    WorkspaceRecordStructure  Records;
    WorkspaceRevisionSequence Revisions;
    SketchStructure           Sketch;

    WorkspaceRecordName SketchFolder;
    WorkspaceRecordName AnnotationFolder;

    WorkspaceRecordName DeclareFolder(WorkspaceCategory Category, WorkspaceRecordName Parent = {})
    {
        WorkspaceRecord Folder = {};
        Folder.Subject        = WorkspaceRecordSubject::Folder;
        Folder.FolderCategory = Category;
        Folder.ParentFolder   = Parent;
        Folder.Naming         = Naming.Issue(WorkspaceRecordSubject::Folder);
        return Records.Declare(Folder);
    }

    /// 🧩 Puts the sketch on the ground plane.
    /// note ⚠️ A SketchStructure with no plane is not `Declared()` and every area query refuses outright.
    ///       Area is measured in Left/Forward with Up as height, so the ground plane is where a closed
    ///       shape actually encloses something.
    void StandSketchOnGround()
    {
        SketchPlane Ground;
        Ground.Origin         = { 0.0, 0.0, 0.0 };
        Ground.Normal         = { 0.0, 1.0, 0.0 };
        Ground.AlongDirection = { 1.0, 0.0, 0.0 };
        Sketch.DeclarePlane(Ground);
    }

    /// 🧩 Draws a ten-unit square lying flat on the ground plane.
    void DrawGroundSquare()
    {
        Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 10.0, 0.0, 0.0 });
        Sketch.DeclareLine({ 10.0, 0.0, 0.0 },  { 10.0, 0.0, 10.0 });
        Sketch.DeclareLine({ 10.0, 0.0, 10.0 }, { 0.0, 0.0, 10.0 });
        Sketch.DeclareLine({ 0.0, 0.0, 10.0 },  { 0.0, 0.0, 0.0 });
    }

    Bench()
    {
        SketchFolder     = DeclareFolder(WorkspaceCategory::Sketch);
        AnnotationFolder = DeclareFolder(WorkspaceCategory::Annotation);
        StandSketchOnGround();
    }
};

bool SameName(WorkspaceRecordName Left, WorkspaceRecordName Right)
{
    return Left.IssuedIndex == Right.IssuedIndex;
}

bool IsUnderFolder(const WorkspaceRecordStructure& Records,
                   WorkspaceRecordName Item,
                   WorkspaceRecordName TargetFolder)
{
    if (SameName(Item, TargetFolder))
        return true;
    const WorkspaceRecord* Record = Records.Resolve(Item);
    if (Record == nullptr)
        return false;
    WorkspaceRecordName Current = Record->ParentFolder;
    while (Current.Assigned())
    {
        if (SameName(Current, TargetFolder))
            return true;
        const WorkspaceRecord* Parent = Records.Resolve(Current);
        if (Parent == nullptr)
            break;
        Current = Parent->ParentFolder;
    }
    return false;
}

//========================================================================================================
// 1. THE FOLDER LOOKUP
//========================================================================================================

void ProveFolderLookup()
{
    std::printf("\n1. Finding the folder a category files into\n");

    {
        Bench Stage;
        Claim(SameName(ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Sketch), Stage.SketchFolder),
              "the sketch category resolves to the sketch folder");
        Claim(SameName(ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Annotation),
                       Stage.AnnotationFolder),
              "the annotation category resolves to the annotation folder");
        Claim(!ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Geometry).Assigned(),
              "a category with no folder resolves to nothing rather than to the first folder");
        Claim(!ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Operation).Assigned(),
              "the operation category resolves to nothing in a workspace that has no operation folder");
    }

    {
        WorkspaceRecordStructure Empty;
        Claim(!ResolveCategoryFolder(Empty, WorkspaceCategory::Sketch).Assigned(),
              "an empty workspace resolves no folder at all");
    }

    // ⚠️ THE LOAD-BEARING CASE. A nested folder carrying the same category must not capture records meant
    //    for the top-level one, or a curve declared while a subfolder exists lands inside the subfolder.
    {
        Bench Stage;
        const WorkspaceRecordName Nested = Stage.DeclareFolder(WorkspaceCategory::Sketch, Stage.SketchFolder);
        Claim(Nested.Assigned(), "a nested sketch folder can be declared");
        Claim(SameName(ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Sketch), Stage.SketchFolder),
              "a nested folder of the same category does NOT capture the lookup");
        Claim(!SameName(ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Sketch), Nested),
              "the nested folder is not what a new curve files under");
    }

    // 🔴 THE CASE THAT ACTUALLY DISCRIMINATES. The claims above pass even with the top-level restriction
    //    removed, because the lookup walks forward and the top-level folder happens to have the lower
    //    index. This one cannot: the ONLY folder carrying Geometry is a subfolder, so a lookup that
    //    accepts nested folders returns it and a lookup that does not returns nothing.
    //    Found by sabotaging the unit and watching the proof stay green — the first five claims here
    //    were describing a coincidence of declaration order, not the rule.
    {
        Bench Stage;
        const WorkspaceRecordName Buried = Stage.DeclareFolder(WorkspaceCategory::Geometry, Stage.SketchFolder);
        Claim(Buried.Assigned(), "a subfolder carrying the geometry category can be declared");
        Claim(!ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Geometry).Assigned(),
              "a category whose ONLY folder is nested resolves to nothing - a new record files at the top "
              "level rather than burying itself in somebody's subfolder");
    }

    // 📝 And the same shape one level deeper, so the rule is "top level", not "not the immediate child".
    {
        Bench Stage;
        const WorkspaceRecordName Middle = Stage.DeclareFolder(WorkspaceCategory::Sketch, Stage.SketchFolder);
        const WorkspaceRecordName Deep   = Stage.DeclareFolder(WorkspaceCategory::Operation, Middle);
        Claim(Deep.Assigned(), "a folder two levels down can be declared");
        Claim(!ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Operation).Assigned(),
              "a folder two levels down does not answer the lookup either");
    }

    // 📝 The lookup walks forward from index 1, so the FIRST declared top-level folder of a category wins.
    {
        Bench Stage;
        const WorkspaceRecordName Second = Stage.DeclareFolder(WorkspaceCategory::Sketch);
        Claim(Second.Assigned(), "a second top-level sketch folder can be declared");
        Claim(SameName(ResolveCategoryFolder(Stage.Records, WorkspaceCategory::Sketch), Stage.SketchFolder),
              "when two top-level folders share a category the FIRST one wins");
    }

    // ⚠️ Only a Folder record answers the lookup. A curve is not a folder even though it has a category.
    {
        Bench Stage;
        WorkspaceRecordStructure Bare;
        WorkspaceRecord NotAFolder = {};
        NotAFolder.Subject        = WorkspaceRecordSubject::OpenCurve;
        NotAFolder.FolderCategory = WorkspaceCategory::Sketch;
        Bare.Declare(NotAFolder);
        Claim(!ResolveCategoryFolder(Bare, WorkspaceCategory::Sketch).Assigned(),
              "a non-folder record carrying a category does not answer the folder lookup");
    }
}

//========================================================================================================
// 2. WHERE EACH KIND OF THING IS FILED
//========================================================================================================

void ProveFiling()
{
    std::printf("\n2. Which folder each kind of record lands in\n");

    Bench Stage;
    const SketchCurveName Curve = Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });

    const WorkspaceRecordName Written    = DeclareWorkspaceCurve(Stage.Naming, Stage.Records, Curve);
    const WorkspaceRecordName Point      = DeclareWorkspacePoint(Stage.Naming, Stage.Records, {});
    const WorkspaceRecordName Profile    = DeclareWorkspaceProfile(Stage.Naming, Stage.Records, {});
    const WorkspaceRecordName Dimension  = DeclareWorkspaceDimension(Stage.Naming, Stage.Records, {});
    const WorkspaceRecordName Constraint = DeclareWorkspaceConstraint(Stage.Naming, Stage.Records, {});

    Claim(Written.Assigned() && Point.Assigned() && Profile.Assigned() &&
          Dimension.Assigned() && Constraint.Assigned(),
          "every declaration returns an assigned name");

    struct Filing
    {
        WorkspaceRecordName    Subject;
        WorkspaceRecordSubject Expected;
        bool                   UnderSketch;
        const char*            Sentence;
    };

    const Filing Expected[] = {
        { Written,    WorkspaceRecordSubject::OpenCurve,     true,  "a curve"      },
        { Point,      WorkspaceRecordSubject::Point,         true,  "a point"      },
        { Profile,    WorkspaceRecordSubject::ClosedProfile, true,  "a profile"    },
        { Dimension,  WorkspaceRecordSubject::Dimension,     false, "a dimension"  },
        { Constraint, WorkspaceRecordSubject::Constraint,    false, "a constraint" },
    };

    for (const Filing& Case : Expected)
    {
        const WorkspaceRecord* Record = Stage.Records.Resolve(Case.Subject);
        Claim(Record != nullptr, (std::string(Case.Sentence) + " resolves back out of the directory").c_str());
        if (Record == nullptr)
            continue;

        Claim(Record->Subject == Case.Expected,
              (std::string(Case.Sentence) + " carries the subject it was declared as").c_str());

        const WorkspaceRecordName Wanted = Case.UnderSketch ? Stage.SketchFolder : Stage.AnnotationFolder;
        Claim(IsUnderFolder(Stage.Records, Case.Subject, Wanted),
              (std::string(Case.Sentence) + (Case.UnderSketch ? " files under Sketch"
                                                              : " files under Annotation")).c_str());
        Claim(!Record->Naming.empty(),
              (std::string(Case.Sentence) + " is given a name").c_str());
        Claim(Record->Visible && !Record->Locked,
              (std::string(Case.Sentence) + " starts visible and unlocked").c_str());
    }

    // 🔴 The whole point of the split: geometry and annotation do NOT share a folder.
    const WorkspaceRecord* Geometry   = Stage.Records.Resolve(Written);
    const WorkspaceRecord* Annotation = Stage.Records.Resolve(Dimension);
    Claim(Geometry != nullptr && Annotation != nullptr &&
          !SameName(Geometry->ParentFolder, Annotation->ParentFolder),
          "a curve and a dimension are NOT filed in the same folder");
}

//========================================================================================================
// 3. WHAT A DECLARATION IMPLIES
//========================================================================================================

void ProveImpliedSemantics()
{
    std::printf("\n3. The semantics that come with a declaration\n");

    Bench Stage;

    const WorkspaceRecordName Ordinary =
        DeclareWorkspaceCurve(Stage.Naming, Stage.Records, {}, false);
    const WorkspaceRecordName Construction =
        DeclareWorkspaceCurve(Stage.Naming, Stage.Records, {}, true);
    const WorkspaceRecordName Profile =
        DeclareWorkspaceProfile(Stage.Naming, Stage.Records, {});

    const WorkspaceRecord* Plain  = Stage.Records.Resolve(Ordinary);
    const WorkspaceRecord* Guide  = Stage.Records.Resolve(Construction);
    const WorkspaceRecord* Closed = Stage.Records.Resolve(Profile);

    Claim(Plain != nullptr && Guide != nullptr && Closed != nullptr, "all three resolve");
    if (Plain == nullptr || Guide == nullptr || Closed == nullptr)
        return;

    Claim(!Plain->ConstructionSemantic, "an ordinary curve is not construction");
    Claim(Guide->ConstructionSemantic, "a construction curve is marked as construction");

    // 📝 A construction curve is still a CURVE and still lives in the sketch folder. It is distinguished by
    //    its name and its flag, not by being filed somewhere else.
    Claim(Guide->Subject == WorkspaceRecordSubject::OpenCurve,
          "a construction curve is still an open curve, not a separate subject");
    Claim(SameName(Guide->ParentFolder, Plain->ParentFolder),
          "a construction curve files in the SAME folder as an ordinary curve");
    Claim(Guide->Naming.rfind("Construction ", 0u) == 0u,
          "a construction curve's name is prefixed with the word Construction");
    Claim(Plain->Naming.rfind("Construction ", 0u) != 0u,
          "an ordinary curve's name is NOT prefixed");
    Claim(Guide->Naming.size() > std::string("Construction ").size(),
          "the prefix is added TO the issued name, it does not replace it");

    // 🔴 A profile is closed and cappable by construction. A profile that is not closed is not a profile.
    Claim(Closed->ClosedSemantic, "a profile is closed");
    Claim(Closed->CappedExtrusionSemantic, "a profile caps when extruded");
    Claim(!Plain->ClosedSemantic, "an open curve is not closed");
    Claim(!Plain->CappedExtrusionSemantic, "an open curve does not cap when extruded");
}

//========================================================================================================
// 4. NAMING
//========================================================================================================

void ProveNaming()
{
    std::printf("\n4. Names issued to declared records\n");

    Bench Stage;

    std::vector<std::string> Issued;
    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
    {
        const WorkspaceRecordName Written = DeclareWorkspaceCurve(Stage.Naming, Stage.Records, {});
        const WorkspaceRecord*    Record  = Stage.Records.Resolve(Written);
        Issued.push_back(Record != nullptr ? Record->Naming : std::string());
    }

    bool AllDistinct = true;
    for (std::size_t Left = 0u; Left < Issued.size(); ++Left)
        for (std::size_t Right = Left + 1u; Right < Issued.size(); ++Right)
            if (Issued[Left] == Issued[Right])
                AllDistinct = false;

    Claim(AllDistinct, "four curves declared in a row all get different names");

    // ⚠️ Naming advances PER SUBJECT. A dimension does not consume a curve's number.
    const WorkspaceRecordName Dimension = DeclareWorkspaceDimension(Stage.Naming, Stage.Records, {});
    const WorkspaceRecordName Fifth     = DeclareWorkspaceCurve(Stage.Naming, Stage.Records, {});
    const WorkspaceRecord*    Curve     = Stage.Records.Resolve(Fifth);
    const WorkspaceRecord*    Measure   = Stage.Records.Resolve(Dimension);

    Claim(Curve != nullptr && Measure != nullptr, "both resolve");
    if (Curve == nullptr || Measure == nullptr)
        return;

    Claim(Curve->Naming != Measure->Naming, "a curve and a dimension do not share a name");

    bool ClashesWithEarlier = false;
    for (const std::string& Earlier : Issued)
        if (Earlier == Curve->Naming)
            ClashesWithEarlier = true;
    Claim(!ClashesWithEarlier,
          "declaring a dimension between curves does not make the next curve reuse an earlier name");
}

//========================================================================================================
// 5. AUTOMATIC PROFILES AND THE COST IN UNDO STEPS
//========================================================================================================

void ProveAutomaticProfiles()
{
    std::printf("\n5. Declaring every closed area at once\n");

    // ⚠️ A SKETCH WITH NO PLANE REFUSES OUTRIGHT. `SketchStructure::Declared()` is false until a plane is
    //    standing, and the area query refuses rather than returning an empty list. The difference matters:
    //    a refusal says "I could not look", an empty result says "I looked and found nothing".
    {
        WorkspaceNameIndex        Naming;
        WorkspaceRecordStructure  Records;
        WorkspaceRevisionSequence Revisions;
        SketchStructure           Planeless;
        Planeless.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });

        Claim(!Planeless.Declared(), "a sketch with no plane is not declared");
        const WorkspaceRecordName First =
            AutoDeclareWorkspaceProfilesFromChains(Naming, Planeless, Records, Revisions);
        Claim(!First.Assigned(), "a sketch with no plane declares no profile");
        Claim(Revisions.DeclaredCount() == 0u, "a refusal seals no revision");
    }

    // A sketch that encloses nothing.
    {
        Bench Stage;
        Claim(Stage.Sketch.Declared(), "standing the sketch on a plane makes it declared");
        Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 });

        const std::uint32_t Before = Stage.Records.DeclaredCount();
        const WorkspaceRecordName First = AutoDeclareWorkspaceProfilesFromChains(
            Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions);

        Claim(!First.Assigned(), "a single open line declares no profile");
        Claim(Stage.Records.DeclaredCount() == Before,
              "a sketch that encloses nothing writes no records");
        Claim(Stage.Revisions.DeclaredCount() == 0u,
              "a sketch that encloses nothing seals no revision, so there is nothing to undo");
    }

    // 🔴 CLOSURE IS TOPOLOGICAL, NOT AREAL. Curves chain end to end; the loop is closed when the chain
    //    returns to where it started. A square standing upright in Left/Up has zero signed area when
    //    measured on the ground plane, and it STILL declares a profile. I expected the opposite and was
    //    wrong — pinned here because "closed" and "encloses some area" are not the same test.
    {
        Bench Stage;
        Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 10.0, 0.0, 0.0 });
        Stage.Sketch.DeclareLine({ 10.0, 0.0, 0.0 },  { 10.0, 10.0, 0.0 });
        Stage.Sketch.DeclareLine({ 10.0, 10.0, 0.0 }, { 0.0, 10.0, 0.0 });
        Stage.Sketch.DeclareLine({ 0.0, 10.0, 0.0 },  { 0.0, 0.0, 0.0 });

        const WorkspaceRecordName Upright = AutoDeclareWorkspaceProfilesFromChains(
            Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions);
        Claim(Upright.Assigned(),
              "a square standing upright still declares a profile - closure is chaining end to end, "
              "not measuring an area");
    }

    // ⚠️ THE 0.05 IS A GAP DISTANCE, NOT AN AREA. Two curve ends closer than that are treated as joined.
    {
        Bench Stage;
        Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 10.0, 0.0, 0.0 });
        Stage.Sketch.DeclareLine({ 10.0, 0.0, 0.0 },  { 10.0, 0.0, 10.0 });
        Stage.Sketch.DeclareLine({ 10.0, 0.0, 10.0 }, { 0.0, 0.0, 10.0 });
        Stage.Sketch.DeclareLine({ 0.0, 0.0, 10.0 },  { 0.0, 0.0, 0.03 });   // 0.03 short of the start

        const WorkspaceRecordName Sloppy = AutoDeclareWorkspaceProfilesFromChains(
            Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions);
        Claim(Sloppy.Assigned(),
              "a square left 0.03 units open still closes - the gap is inside the 0.05 tolerance");
    }

    {
        Bench Stage;
        Stage.Sketch.DeclareLine({ 0.0, 0.0, 0.0 },   { 10.0, 0.0, 0.0 });
        Stage.Sketch.DeclareLine({ 10.0, 0.0, 0.0 },  { 10.0, 0.0, 10.0 });
        Stage.Sketch.DeclareLine({ 10.0, 0.0, 10.0 }, { 0.0, 0.0, 10.0 });
        Stage.Sketch.DeclareLine({ 0.0, 0.0, 10.0 },  { 0.0, 0.0, 0.5 });    // 0.5 short of the start

        const WorkspaceRecordName TooOpen = AutoDeclareWorkspaceProfilesFromChains(
            Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions);
        Claim(!TooOpen.Assigned(),
              "a square left 0.5 units open does NOT close - the gap is outside the 0.05 tolerance");
        Claim(Stage.Revisions.DeclaredCount() == 0u,
              "and an unclosed sketch seals no revision");
    }

    // A closed square lying flat, where it does enclose an area.
    {
        Bench Stage;
        Stage.DrawGroundSquare();

        const std::uint32_t Before = Stage.Records.DeclaredCount();
        const WorkspaceRecordName First = AutoDeclareWorkspaceProfilesFromChains(
            Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions);

        Claim(First.Assigned(), "four lines forming a flat square declare a profile");

        const std::uint32_t Written = Stage.Records.DeclaredCount() - Before;
        Claim(Written >= 1u, "at least one profile record is written");

        const WorkspaceRecord* Record = Stage.Records.Resolve(First);
        Claim(Record != nullptr, "the returned name resolves");
        if (Record != nullptr)
        {
            Claim(Record->Subject == WorkspaceRecordSubject::ClosedProfile,
                  "an automatically declared area is a closed profile");
            Claim(IsUnderFolder(Stage.Records, First, Stage.SketchFolder),
                  "an automatically declared profile files under Sketch like any other");
            Claim(Record->ClosedSemantic && Record->CappedExtrusionSemantic,
                  "an automatically declared profile carries the same semantics as a manual one");
            Claim(!Record->Naming.empty(), "it is named like any other profile");
        }

        // 🔴 THE CLAIM THAT MATTERS. Closing a rectangle is ONE action and must cost ONE undo press,
        //    however many areas the closure happened to enclose.
        Claim(Stage.Revisions.DeclaredCount() == 1u,
              "declaring the enclosed areas seals exactly ONE revision, not one per profile");

        const WorkspaceRevision* Sealed = Stage.Revisions.Resolve({ 1u });
        Claim(Sealed != nullptr, "the sealed revision resolves");
        if (Sealed != nullptr)
        {
            Claim(!Sealed->Affected.empty(),
                  "the one revision lists every profile it wrote, so undo reverses all of them");
            Claim(!Sealed->Description.empty() && !Sealed->Operation.empty(),
                  "the revision is described for the history panel");
            Claim(Sealed->SealedAt == 1u, "the first revision is sealed at sequence position one");
        }
    }

    // 📝 An area already declared is not declared twice, so running it again is harmless.
    {
        Bench Stage;
        Stage.DrawGroundSquare();

        AutoDeclareWorkspaceProfilesFromChains(Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions);
        const std::uint32_t AfterFirst = Stage.Records.DeclaredCount();

        const WorkspaceRecordName Again = AutoDeclareWorkspaceProfilesFromChains(
            Stage.Naming, Stage.Sketch, Stage.Records, Stage.Revisions);

        Claim(!Again.Assigned(),
              "running it a second time on an unchanged sketch declares nothing new");
        Claim(Stage.Records.DeclaredCount() == AfterFirst,
              "the same area is not written twice");
        Claim(Stage.Revisions.DeclaredCount() == 1u,
              "and no second revision is sealed, so undo is not padded with an empty step");
    }
}

//========================================================================================================
// 6. A WORKSPACE WITH NO FOLDERS
//========================================================================================================

void ProveFolderlessWorkspace()
{
    std::printf("\n6. Declaring into a workspace that has no folders\n");

    // ⚠️ Unassigned ParentFolder means top level. Declaring must still SUCCEED — losing the artist's curve
    //    because the outliner has no folder would be far worse than filing it at the root.
    WorkspaceNameIndex       Naming;
    WorkspaceRecordStructure Records;

    const WorkspaceRecordName Curve     = DeclareWorkspaceCurve(Naming, Records, {});
    const WorkspaceRecordName Dimension = DeclareWorkspaceDimension(Naming, Records, {});

    Claim(Curve.Assigned(), "a curve still declares when there is no sketch folder");
    Claim(Dimension.Assigned(), "a dimension still declares when there is no annotation folder");

    const WorkspaceRecord* Written = Records.Resolve(Curve);
    Claim(Written != nullptr, "it resolves");
    if (Written != nullptr)
    {
        Claim(!Written->ParentFolder.Assigned(), "it sits at the top level rather than under a wrong folder");
        Claim(Written->Subject == WorkspaceRecordSubject::OpenCurve, "it is still a curve");
        Claim(!Written->Naming.empty(), "it is still named");
    }
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("RECORD DECLARATION PROOF\n");
    std::printf("=========================================================================\n");

    ProveFolderLookup();
    ProveFiling();
    ProveImpliedSemantics();
    ProveNaming();
    ProveAutomaticProfiles();
    ProveFolderlessWorkspace();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
