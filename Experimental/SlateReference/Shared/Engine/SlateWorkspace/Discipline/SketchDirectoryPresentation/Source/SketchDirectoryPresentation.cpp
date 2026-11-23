//============================================================================================================================================
//                                                 SKETCHDIRECTORYPRESENTATION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/SketchDirectoryPresentation/Api/SketchDirectoryPresentation.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"

#include <algorithm>
#include <string>

namespace Slate
{

void ClearInspectorPresentation(SketchDirectoryPresentation& Bridge)
{
    Bridge.Property = ParametricPropertyPresentation{};
    Bridge.PropertyNaming.clear();
    Bridge.PropertySecondary.clear();
    for (std::string& Run : Bridge.PropertyCaptions) Run.clear();
    for (std::string& Run : Bridge.PropertyValues)   Run.clear();
    for (std::string& Run : Bridge.PropertyTrails)   Run.clear();
    Bridge.RevisionRows.clear();
    Bridge.RevisionBacking.clear();
}

void SeedParametricWorkspace(WorkspaceNameIndex& Naming,
                             SketchStructure& Sketch,
                             WorkspaceRecordStructure& Records,
                             WorkspaceRevisionSequence& Revisions)
{
    // Parametric sketch starts empty. The default grid/basis is supplied by
    // ResolveSketchBasis until the artist creates real CAD records.
    static_cast<void>(Naming);
    static_cast<void>(Sketch);
    static_cast<void>(Records);
    static_cast<void>(Revisions);
}

void SeatParametricContext(const WorkspaceDirectoryProjection& Directory,
                           ParametricWorkspaceContext& Applied,
                           bool& Seeded)
{
    if (Directory.Rows.empty())
    {
        for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
            Applied.RowSelected[Index] = false;
        Applied.RowTaken = 0u;
        Applied.RowSelectionAnchor = 0u;
        Seeded = false;
        return;
    }

    const std::uint32_t DeclaredCount = static_cast<std::uint32_t>(Directory.Rows.size());
    for (std::uint32_t Index = DeclaredCount; Index < ParametricWorkspaceContext::RowLimit; ++Index)
        Applied.RowSelected[Index] = false;

    if (!Seeded)
    {
        Seeded = true;
        for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
            Applied.RowSelected[Index] = false;

        for (std::uint32_t Index = 0u; Index < DeclaredCount; ++Index)
            if (Directory.Rows[Index].Subject == WorkspaceRecordSubject::Folder)
                Applied.RowExpanded[Index] = true;

        const std::uint32_t Initial = InitialRowIn(Directory);
        Applied.RowTaken = Initial;
        Applied.RowSelectionAnchor = Initial;
        Applied.RowSelected[Initial] = true;
    }
}

Deliver<bool> SynchroniseParametricPresentation(const WorkspaceRecordStructure& Records,
                                                const WorkspaceRevisionSequence& Revisions,
                                                WorkspaceDirectoryProjection& Directory,
                                                SketchDirectoryPresentation& Bridge,
                                                ParametricWorkspaceContext& Applied,
                                                WorkspaceRecordName& PendingSelection,
                                                bool& Seeded,
                                                const SketchSelectionSet* SelectionSet)
{
    ProjectWorkspaceDirectory(Records, Directory);

    const Deliver<bool> DirectoryBridge = BuildDirectoryPresentation(Directory, Bridge);
    if (!DirectoryBridge.Resolved)
        return DirectoryBridge;

    SeatParametricContext(Directory, Applied, Seeded);

    if (PendingSelection.Assigned())
    {
        const Deliver<std::uint32_t> Row = ResolveWorkspaceDirectoryRow(Directory, PendingSelection);
        if (Row.Resolved && Row.Resolve() < ParametricWorkspaceContext::RowLimit)
        {
            for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
                Applied.RowSelected[Index] = false;
            Applied.RowTaken = Row.Resolve();
            Applied.RowSelectionAnchor = Row.Resolve();
            Applied.RowSelected[Row.Resolve()] = true;
        }
        if (SelectionSet != nullptr && (SelectionSet->Empty() || SelectionSet->Active()->Subject == SketchPickSubject::Record))
        {
            SketchSelectionSet* MutableSet = const_cast<SketchSelectionSet*>(SelectionSet);
            MutableSet->Clear();
            SketchPick Pick = {};
            Pick.Record = PendingSelection;
            SetSketchPick(*MutableSet, Pick, false);
        }
        PendingSelection = {};
    }
    else if (SelectionSet != nullptr && !SelectionSet->Empty())
    {
        for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
            Applied.RowSelected[Index] = false;

        std::uint32_t PrimaryRow = ParametricWorkspaceContext::RowLimit;
        for (const SketchPick& Item : SelectionSet->Items)
        {
            if (Item.Record.Assigned())
            {
                const Deliver<std::uint32_t> Row = ResolveWorkspaceDirectoryRow(Directory, Item.Record);
                if (Row.Resolved && Row.Resolve() < ParametricWorkspaceContext::RowLimit)
                {
                    Applied.RowSelected[Row.Resolve()] = true;
                    if (PrimaryRow == ParametricWorkspaceContext::RowLimit)
                        PrimaryRow = Row.Resolve();
                }
            }
        }
        if (PrimaryRow < ParametricWorkspaceContext::RowLimit)
        {
            Applied.RowTaken = PrimaryRow;
            Applied.RowSelectionAnchor = PrimaryRow;
        }
    }

    const std::uint32_t RowCount = static_cast<std::uint32_t>(Directory.Rows.size());
    if (Applied.RowTaken >= RowCount || Directory.Rows.empty() ||
        Directory.Rows[Applied.RowTaken].Role != WorkspaceDirectoryRowRole::Record)
    {
        ClearInspectorPresentation(Bridge);
        return Deliver<bool>::Result(true);
    }

    const WorkspaceRecordName Selected = Directory.Rows[Applied.RowTaken].Record;
    const Deliver<WorkspacePropertyProjection> Property =
        ProjectWorkspaceProperty(Records, Revisions, Selected);
    if (!Property.Resolved)
        return Deliver<bool>::Refuse(Property.Error);

    return BuildInspectorPresentation(Records, Selected, Property.Resolve(), Revisions, Bridge);
}



ParametricCategory PresentedCategory(WorkspaceCategory Category)
{
    switch (Category)
    {
        case WorkspaceCategory::Sketch:      return ParametricCategory::Sketch;
        case WorkspaceCategory::Geometry:    return ParametricCategory::Geometry;
        case WorkspaceCategory::Annotation:  return ParametricCategory::Annotation;
        case WorkspaceCategory::Operation:   return ParametricCategory::Operation;
        case WorkspaceCategory::Folder:
        case WorkspaceCategory::CategoryCount:
            return ParametricCategory::Sketch;
    }
    return ParametricCategory::Sketch;
}

ParametricRowSubject PresentedRowSubject(const WorkspaceDirectoryRow& Source)
{
    if (Source.Role == WorkspaceDirectoryRowRole::CategoryRoot)
        return ParametricRowSubject::CategoryRoot;

    switch (Source.Subject)
    {
        case WorkspaceRecordSubject::Folder:        return ParametricRowSubject::Folder;
        case WorkspaceRecordSubject::Point:         return ParametricRowSubject::Point;
        case WorkspaceRecordSubject::OpenCurve:     return ParametricRowSubject::OpenCurve;
        case WorkspaceRecordSubject::ClosedProfile: return ParametricRowSubject::ClosedProfile;
        case WorkspaceRecordSubject::ThinSurface:   return ParametricRowSubject::ThinSurface;
        case WorkspaceRecordSubject::Solid:         return ParametricRowSubject::Solid;
        case WorkspaceRecordSubject::Dimension:     return ParametricRowSubject::Dimension;
        case WorkspaceRecordSubject::Constraint:    return ParametricRowSubject::Constraint;
        case WorkspaceRecordSubject::Pattern:       return ParametricRowSubject::Pattern;
        case WorkspaceRecordSubject::Mirror:        return ParametricRowSubject::Mirror;
        case WorkspaceRecordSubject::SubjectCount:
            return ParametricRowSubject::Folder;
    }
    return ParametricRowSubject::Folder;
}

const char* AffirmationText(bool Reading)
{
    return Reading ? "Yes" : "No";
}

std::string IdentityRun(const char* Stem, std::uint32_t Index)
{
    return std::string(Stem) + "_" + std::to_string(Index);
}

std::string RecordDisplayName(const WorkspaceRecordStructure& Records, WorkspaceRecordName Subject)
{
    if (!Subject.Assigned())
        return "Root";

    const WorkspaceRecord* Held = Records.Resolve(Subject);
    if (Held != nullptr && !Held->Naming.empty())
        return Held->Naming;

    return IdentityRun("Record", Subject.IssuedIndex);
}

std::string AffectedRecordsRun(const WorkspaceRecordStructure& Records,
                                     const std::vector<WorkspaceRecordName>& Affected)
{
    if (Affected.empty())
        return "None";

    std::string Joined = {};
    for (std::uint32_t Index = 0u; Index < Affected.size(); ++Index)
    {
        if (Index > 0u)
            Joined += ", ";
        Joined += RecordDisplayName(Records, Affected[Index]);
    }
    return Joined;
}

std::string SubjectReferenceText(const WorkspaceRecord& Subject)
{
    switch (Subject.Subject)
    {
        case WorkspaceRecordSubject::Point:
            return Subject.SketchPoint.Assigned()
                 ? IdentityRun("SketchPoint", Subject.SketchPoint.IssuedIndex) : "";
        case WorkspaceRecordSubject::OpenCurve:
            return Subject.SketchCurve.Assigned()
                 ? IdentityRun("SketchCurve", Subject.SketchCurve.IssuedIndex) : "";
        case WorkspaceRecordSubject::ClosedProfile:
        case WorkspaceRecordSubject::ThinSurface:
            return Subject.Profile.Assigned()
                 ? IdentityRun("Profile", Subject.Profile.IssuedIndex) : "";
        case WorkspaceRecordSubject::Solid:
            return Subject.Solid.Assigned()
                 ? IdentityRun("Solid", Subject.Solid.IssuedIndex) : "";
        case WorkspaceRecordSubject::Dimension:
            return Subject.Dimension.Assigned()
                 ? IdentityRun("Dimension", Subject.Dimension.IssuedIndex) : "";
        case WorkspaceRecordSubject::Constraint:
            return Subject.Constraint.Assigned()
                 ? IdentityRun("Constraint", Subject.Constraint.IssuedIndex) : "";
        case WorkspaceRecordSubject::Pattern:
        case WorkspaceRecordSubject::Mirror:
            return Subject.Feature.Assigned()
                 ? IdentityRun("Feature", Subject.Feature.IssuedIndex) : "";
        case WorkspaceRecordSubject::Folder:
        case WorkspaceRecordSubject::SubjectCount:
            return "";
    }
    return "";
}

Deliver<bool> BuildDirectoryPresentation(const WorkspaceDirectoryProjection& Source,
                                               SketchDirectoryPresentation& Delivered)
{
    Delivered.DirectoryRows.clear();
    Delivered.DirectoryBacking.clear();

    if (Source.Rows.size() > ParametricWorkspaceContext::RowLimit)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "the parametric workspace row limit would be exceeded" });
    }

    Delivered.DirectoryRows.reserve(Source.Rows.size());
    Delivered.DirectoryBacking.reserve(Source.Rows.size());

    for (const WorkspaceDirectoryRow& Row : Source.Rows)
    {
        Delivered.DirectoryBacking.push_back({ Row.Naming != nullptr ? Row.Naming : "",
                                               Row.Tagged != nullptr ? Row.Tagged : "" });
        SketchDirectoryPresentation::DirectoryText& Backing = Delivered.DirectoryBacking.back();
        // Keep family searchable without changing the authored display name. This lets the panel filter
        // for "bezier", "ellipse", or "construction" even when a record was custom-renamed.
        const char* FamilyTag = WorkspaceShapeFamilyText(Row.Family);
        if (FamilyTag != nullptr && FamilyTag[0] != '\0')
        {
            if (!Backing.Tagged.empty())
                Backing.Tagged += " ";
            Backing.Tagged += FamilyTag;
        }
        ParametricDirectoryRow Presented = {};
        Presented.Naming         = Backing.Naming.c_str();
        Presented.Subject        = PresentedRowSubject(Row);
        Presented.Family         = Row.Family;
        Presented.Category       = PresentedCategory(Row.Category);
        Presented.Depth          = Row.Depth;
        Presented.Enclosing      = Row.Enclosing;
        Presented.EnclosedCount  = Row.EnclosedCount;
        Presented.Tagged         = Backing.Tagged.c_str();
        Presented.Identity       = Row.StableIdentity;
        Presented.Visible        = Row.Visible;
        Presented.Locked         = Row.Locked;
        Presented.ClosedSemantic = Row.ClosedSemantic;
        Presented.AutoNamed      = Row.AutoNamed;

        Delivered.DirectoryRows.push_back(Presented);
    }

    return Deliver<bool>::Result(true);
}

Deliver<bool> BuildInspectorPresentation(const WorkspaceRecordStructure& Records,
                                               WorkspaceRecordName SubjectName,
                                               const WorkspacePropertyProjection& Source,
                                               const WorkspaceRevisionSequence& Revisions,
                                               SketchDirectoryPresentation& Delivered)
{
    Delivered.Property = ParametricPropertyPresentation{};
    Delivered.PropertyNaming.clear();
    Delivered.PropertySecondary.clear();
    for (std::string& Run : Delivered.PropertyCaptions) Run.clear();
    for (std::string& Run : Delivered.PropertyValues)   Run.clear();
    for (std::string& Run : Delivered.PropertyTrails)   Run.clear();
    Delivered.RevisionRows.clear();
    Delivered.RevisionBacking.clear();

    if (!SubjectName.Assigned())
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the selected workspace record is not assigned" });
    }

    if (Source.Subject == nullptr)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the selected workspace record is absent from the property projection" });
    }

    if (Source.RevisionSet.size() > ParametricWorkspaceContext::RevisionLimit)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                       "the parametric revision row limit would be exceeded" });
    }

    const WorkspaceRecord& Held = *Source.Subject;
    const ParametricCategory Category = PresentedCategory(PresentedCategoryOfRecord(Held));
    const ParametricRowSubject Subject = PresentedRowSubject(
        WorkspaceDirectoryRow{ WorkspaceDirectoryRowRole::Record, PresentedCategoryOfRecord(Held), SubjectName,
                               Held.Subject, Held.Family, 0u, 0xFFFFFFFFu, 0u, 0u, nullptr, nullptr,
                               Held.Visible, Held.Locked, Held.ClosedSemantic, Held.AutoNamed });

    Delivered.PropertyNaming = Held.Naming;
    Delivered.PropertySecondary = std::string(ParametricCategoryText(Category))
                                + " • " + ParametricRowText(Subject);

    Delivered.Property.Naming         = Delivered.PropertyNaming.c_str();
    Delivered.Property.Secondary      = Delivered.PropertySecondary.c_str();
    Delivered.Property.Subject        = Subject;
    Delivered.Property.Family         = Held.Family;
    Delivered.Property.Category       = Category;
    Delivered.Property.Identity       = static_cast<StableRowIdentity>(SubjectName.IssuedIndex);
    Delivered.Property.Visible        = Held.Visible;
    Delivered.Property.Locked         = Held.Locked;
    Delivered.Property.ClosedSemantic = Held.ClosedSemantic;
    Delivered.Property.CappedExtrusionSemantic = Held.CappedExtrusionSemantic;
    Delivered.Property.AutoNamed      = Held.AutoNamed;

    std::uint32_t Field = 0u;
    auto Append = [&](const char* Caption, const std::string& Value, const std::string& Trail = std::string{})
    {
        if (Field >= ParametricPropertyPresentation::FieldLimit)
            return;
        Delivered.PropertyCaptions[Field] = Caption != nullptr ? Caption : "";
        Delivered.PropertyValues[Field] = Value;
        Delivered.PropertyTrails[Field] = Trail;
        Delivered.Property.Fields[Field].Caption = Delivered.PropertyCaptions[Field].c_str();
        Delivered.Property.Fields[Field].Value = Delivered.PropertyValues[Field].c_str();
        Delivered.Property.Fields[Field].Secondary = Delivered.PropertyTrails[Field].c_str();
        ++Field;
    };

    Append("Record", IdentityRun("Record", SubjectName.IssuedIndex));
    Append("Category", ParametricCategoryText(Category));
    Append("Subject", ParametricRowText(Subject));
    Append("Family", WorkspaceShapeFamilyText(Held.Family));
    Append("Visible", AffirmationText(Held.Visible));
    Append("Locked", AffirmationText(Held.Locked));
    Append("Auto Name", AffirmationText(Held.AutoNamed));
    if (Held.ClosedSemantic)
    {
        Append("Curve Closure", "Closed loop", "outline only");
        Append("Extrude Result", Held.CappedExtrusionSemantic ? "Solid" : "Walls only",
               Held.CappedExtrusionSemantic ? "top/bottom caps" : "no caps");
        Append("Extrude Caps", Held.CappedExtrusionSemantic ? "On" : "Off", "click toggle");
    }
    Append("Parent Folder", RecordDisplayName(Records, Held.ParentFolder));

    const std::string Reference = SubjectReferenceText(Held);
    if (!Reference.empty())
        Append("Reference", Reference);

    if (Held.Feature.Assigned() &&
        Held.Subject != WorkspaceRecordSubject::Pattern &&
        Held.Subject != WorkspaceRecordSubject::Mirror)
    {
        Append("Feature", IdentityRun("Feature", Held.Feature.IssuedIndex));
    }

    Delivered.Property.FieldCount = Field;

    Delivered.RevisionRows.reserve(Source.RevisionSet.size());
    Delivered.RevisionBacking.reserve(Source.RevisionSet.size());

    for (WorkspaceRevisionName RevisionName : Source.RevisionSet)
    {
        const WorkspaceRevision* Revision = Revisions.Resolve(RevisionName);
        if (Revision == nullptr)
        {
            return Deliver<bool>::Refuse({ RefusalReason::IdentityStale,
                                           "a projected workspace revision is no longer declared" });
        }

        Delivered.RevisionBacking.push_back({ Revision->Description,
                                              Revision->Operation,
                                              AffectedRecordsRun(Records, Revision->Affected),
                                              std::string("Step ") + std::to_string(Revision->SealedAt) });
        const SketchDirectoryPresentation::RevisionText& Backing = Delivered.RevisionBacking.back();

        ParametricRevisionRow Presented = {};
        Presented.Description = Backing.Description.c_str();
        Presented.Operation   = Backing.Operation.c_str();
        Presented.Affected    = Backing.Affected.c_str();
        Presented.SealedAt    = Backing.SealedAt.c_str();
        Presented.Identity    = static_cast<StableRowIdentity>(RevisionName.IssuedIndex);

        Delivered.RevisionRows.push_back(Presented);
    }

    return Deliver<bool>::Result(true);
}


}   // namespace Slate
