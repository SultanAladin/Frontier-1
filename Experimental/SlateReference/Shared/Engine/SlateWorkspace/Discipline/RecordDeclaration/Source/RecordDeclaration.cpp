//============================================================================================================================================
//                                                       RECORDDECLARATION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/RecordDeclaration/Api/RecordDeclaration.h"

namespace Slate
{


const char* FamilyFolderName(WorkspaceShapeFamily Family)
{
    switch (Family)
    {
        case WorkspaceShapeFamily::Point:         return "Points";
        case WorkspaceShapeFamily::Line:          return "Lines";
        case WorkspaceShapeFamily::CircularArc:   return "Circular Arcs";
        case WorkspaceShapeFamily::Bezier:        return "Bezier";
        case WorkspaceShapeFamily::Hermite:       return "Hermite";
        case WorkspaceShapeFamily::BasisSpline:   return "Basis Splines";
        case WorkspaceShapeFamily::Nurbs:         return "NURBS";
        case WorkspaceShapeFamily::Polygon:       return "Polygons";
        case WorkspaceShapeFamily::Rectangle:     return "Rectangles";
        case WorkspaceShapeFamily::Slot:          return "Slots";
        case WorkspaceShapeFamily::Circle:        return "Circles";
        case WorkspaceShapeFamily::Ellipse:       return "Ellipses";
        case WorkspaceShapeFamily::Profile:       return "Profiles";
        case WorkspaceShapeFamily::Construction:  return "Construction";
        default:                                  return "Unclassified";
    }
}

WorkspaceRecordName EnsureNamedFolder(WorkspaceNameIndex& Naming,
                                      WorkspaceRecordStructure& Records,
                                      WorkspaceCategory Category,
                                      const char* FolderName,
                                      WorkspaceRecordName ParentFolder = {})
{
    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Existing = Records.Resolve({ Index });
        if (Existing != nullptr && Existing->Subject == WorkspaceRecordSubject::Folder &&
            Existing->FolderCategory == Category && Existing->ParentFolder.IssuedIndex == ParentFolder.IssuedIndex &&
            Existing->Naming == FolderName)
            return { Index };
    }

    WorkspaceRecord Folder = {};
    Folder.Subject = WorkspaceRecordSubject::Folder;
    Folder.FolderCategory = Category;
    Folder.ParentFolder = ParentFolder;
    Folder.Naming = FolderName;
    Folder.AutoNamed = false;
    static_cast<void>(Naming);
    return Records.Declare(Folder);
}

WorkspaceRecordName ResolveCategoryFolder(const WorkspaceRecordStructure& Records,
                                          WorkspaceCategory Category)
{
    for (std::uint32_t Index = 1u; Index <= Records.DeclaredCount(); ++Index)
    {
        const WorkspaceRecord* Record = Records.Resolve({ Index });

        // ⚠️ `!ParentFolder.Assigned()` is what confines this to the TOP-LEVEL folder. A nested folder
        //    carrying the same category would otherwise capture records meant for the one at the root.
        if (Record != nullptr && Record->Subject == WorkspaceRecordSubject::Folder &&
            Record->FolderCategory == Category && !Record->ParentFolder.Assigned())
            return { Index };
    }
    return {};
}

WorkspaceRecordName DeclareWorkspaceCurve(WorkspaceNameIndex& Naming,
                                          WorkspaceRecordStructure& Records,
                                          SketchCurveName Curve,
                                          bool Construction,
                                          WorkspaceShapeFamily Family)
{
    WorkspaceRecord Record = {};
    Record.Subject      = WorkspaceRecordSubject::OpenCurve;
    Record.Family       = Construction ? WorkspaceShapeFamily::Construction : Family;
    WorkspaceRecordName RootSketchFolder = ResolveCategoryFolder(Records, WorkspaceCategory::Sketch);
    WorkspaceRecordName ParentFolder = EnsureNamedFolder(Naming, Records, WorkspaceCategory::Sketch, "Curves", RootSketchFolder);
    // Curves are grouped directly under the single Curves folder. The family is metadata
    // on the row, not another folder level; this keeps one Bezier/Hermite/Spline as one
    // curve entry instead of presenting a misleading nested family tree.
    Record.ParentFolder = ParentFolder;
    Record.Naming       = Construction
                        ? std::string("Construction ") + Naming.Issue(WorkspaceRecordSubject::OpenCurve, Family)
                        : Naming.Issue(WorkspaceRecordSubject::OpenCurve, Family);
    Record.SketchCurve  = Curve;
    Record.ConstructionSemantic = Construction;
    return Records.Declare(Record);
}

WorkspaceRecordName DeclareWorkspaceProfile(WorkspaceNameIndex& Naming,
                                            WorkspaceRecordStructure& Records,
                                            ProfileNameInFeature Profile,
                                            WorkspaceShapeFamily Family)
{
    WorkspaceRecord Record = {};
    Record.Subject      = WorkspaceRecordSubject::ClosedProfile;
    Record.Family       = Family;
    WorkspaceRecordName RootSketchFolder = ResolveCategoryFolder(Records, WorkspaceCategory::Sketch);
    Record.ParentFolder = EnsureNamedFolder(Naming, Records, WorkspaceCategory::Sketch,
                                            FamilyFolderName(Family), RootSketchFolder);
    Record.Naming       = Naming.Issue(WorkspaceRecordSubject::ClosedProfile, Family);
    Record.Profile      = Profile;
    Record.ClosedSemantic          = true;
    Record.CappedExtrusionSemantic = true;
    return Records.Declare(Record);
}

WorkspaceRecordName DeclareWorkspaceDimension(WorkspaceNameIndex& Naming,
                                              WorkspaceRecordStructure& Records,
                                              DimensionName Dimension)
{
    WorkspaceRecord Record = {};
    Record.Subject      = WorkspaceRecordSubject::Dimension;
    Record.ParentFolder = ResolveCategoryFolder(Records, WorkspaceCategory::Annotation);
    Record.Naming       = Naming.Issue(WorkspaceRecordSubject::Dimension);
    Record.Dimension    = Dimension;
    return Records.Declare(Record);
}

WorkspaceRecordName DeclareWorkspaceConstraint(WorkspaceNameIndex& Naming,
                                               WorkspaceRecordStructure& Records,
                                               ConstraintName Constraint)
{
    WorkspaceRecord Record = {};
    Record.Subject      = WorkspaceRecordSubject::Constraint;
    Record.ParentFolder = ResolveCategoryFolder(Records, WorkspaceCategory::Annotation);
    Record.Naming       = Naming.Issue(WorkspaceRecordSubject::Constraint);
    Record.Constraint   = Constraint;
    return Records.Declare(Record);
}

WorkspaceRecordName DeclareWorkspacePoint(WorkspaceNameIndex& Naming,
                                          WorkspaceRecordStructure& Records,
                                          SketchPointName Point)
{
    WorkspaceRecord Record = {};
    Record.Subject      = WorkspaceRecordSubject::Point;
    Record.Family       = WorkspaceShapeFamily::Point;
    WorkspaceRecordName RootSketchFolder = ResolveCategoryFolder(Records, WorkspaceCategory::Sketch);
    Record.ParentFolder = EnsureNamedFolder(Naming, Records, WorkspaceCategory::Sketch,
                                             FamilyFolderName(WorkspaceShapeFamily::Point), RootSketchFolder);
    Record.Naming       = Naming.Issue(WorkspaceRecordSubject::Point);
    Record.SketchPoint  = Point;
    return Records.Declare(Record);
}

WorkspaceRecordName AutoDeclareWorkspaceProfilesFromChains(WorkspaceNameIndex& Naming,
                                                           SketchStructure& Sketch,
                                                           WorkspaceRecordStructure& Records,
                                                           WorkspaceRevisionSequence& Revisions)
{
    const Deliver<std::vector<ProfileNameInFeature>> Profiles = AutoDeclareClosedAreaProfiles(Sketch, 0.05);
    if (!Profiles.Resolved || Profiles.Resolve().empty())
        return {};

    std::vector<WorkspaceRecordName> Written;
    for (ProfileNameInFeature Profile : Profiles.Resolve())
        Written.push_back(DeclareWorkspaceProfile(Naming, Records, Profile));

    // 🔴 ONE revision for all of them. Closing a rectangle declares a single area and the artist expects a
    //    single undo; sealing one revision per profile would take four presses to walk back one action.
    Revisions.Seal("Declared closed sketch areas", "Auto Create Profiles", Written,
                   Revisions.DeclaredCount() + 1u);

    return Written.empty() ? WorkspaceRecordName{} : Written.front();
}

}   // namespace Slate
