//============================================================================================================================================
//                                                   WORKSPACERECORDSTRUCTURE.CPP
//============================================================================================================================================

#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"

namespace Slate
{

WorkspaceCategory CategoryOfRecord(WorkspaceRecordSubject Subject)
{
    switch (Subject)
    {
        case WorkspaceRecordSubject::Point:
        case WorkspaceRecordSubject::OpenCurve:
        case WorkspaceRecordSubject::ClosedProfile:
            return WorkspaceCategory::Sketch;
        case WorkspaceRecordSubject::ThinSurface:
        case WorkspaceRecordSubject::Solid:
            return WorkspaceCategory::Geometry;
        case WorkspaceRecordSubject::Dimension:
        case WorkspaceRecordSubject::Constraint:
            return WorkspaceCategory::Annotation;
        case WorkspaceRecordSubject::Pattern:
        case WorkspaceRecordSubject::Mirror:
            return WorkspaceCategory::Operation;
        case WorkspaceRecordSubject::Folder:
        case WorkspaceRecordSubject::SubjectCount:
            return WorkspaceCategory::Folder;
    }
    return WorkspaceCategory::Folder;
}

WorkspaceCategory PresentedCategoryOfRecord(const WorkspaceRecord& Subject)
{
    return Subject.Subject == WorkspaceRecordSubject::Folder ? Subject.FolderCategory
                                                             : CategoryOfRecord(Subject.Subject);
}

bool WorkspaceRecord::Declared() const
{
    switch (Subject)
    {
        case WorkspaceRecordSubject::Point:         return SketchPoint.Assigned();
        case WorkspaceRecordSubject::OpenCurve:     return SketchCurve.Assigned();
        case WorkspaceRecordSubject::ClosedProfile: return Profile.Assigned();
        case WorkspaceRecordSubject::ThinSurface:   return Profile.Assigned();
        case WorkspaceRecordSubject::Solid:         return Solid.Assigned();
        case WorkspaceRecordSubject::Dimension:     return Dimension.Assigned();
        case WorkspaceRecordSubject::Constraint:    return Constraint.Assigned();
        case WorkspaceRecordSubject::Pattern:       return Feature.Assigned();
        case WorkspaceRecordSubject::Mirror:        return Feature.Assigned();
        case WorkspaceRecordSubject::Folder:        return true;
        case WorkspaceRecordSubject::SubjectCount:  return false;
    }
    return false;
}

WorkspaceRecordName WorkspaceRecordStructure::Declare(WorkspaceRecord Incoming)
{
    HeldRecords.push_back(std::move(Incoming));
    return { static_cast<std::uint32_t>(HeldRecords.size()) };
}

Deliver<bool> WorkspaceRecordStructure::Promote(WorkspaceRecordName Subject, WorkspaceRecordSubject TargetSubject)
{
    WorkspaceRecord* Held = Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace record is declared" });
    Held->Subject = TargetSubject;
    Held->ClosedSemantic = TargetSubject == WorkspaceRecordSubject::ClosedProfile || TargetSubject == WorkspaceRecordSubject::Solid;
    if (Held->ClosedSemantic && !Held->CappedExtrusionSemantic)
        Held->CappedExtrusionSemantic = true;
    return Deliver<bool>::Result(true);
}

Deliver<bool> WorkspaceRecordStructure::Rename(WorkspaceRecordName Subject, const std::string& Naming)
{
    WorkspaceRecord* Held = Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace record is declared" });
    Held->Naming = Naming;
    Held->AutoNamed = false;
    return Deliver<bool>::Result(true);
}

Deliver<bool> WorkspaceRecordStructure::SetFolderCategory(WorkspaceRecordName Subject, WorkspaceCategory Category)
{
    WorkspaceRecord* Held = Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace record is declared" });
    if (Held->Subject != WorkspaceRecordSubject::Folder)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "only workspace folders carry a folder category" });
    if (Category == WorkspaceCategory::Folder || Category == WorkspaceCategory::CategoryCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "folder categories must resolve to a workspace content category" });
    Held->FolderCategory = Category;
    return Deliver<bool>::Result(true);
}

Deliver<bool> WorkspaceRecordStructure::MoveToFolder(WorkspaceRecordName Subject, WorkspaceRecordName Folder)
{
    WorkspaceRecord* Held = Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace record is declared" });
    if (Folder.Assigned())
    {
        const WorkspaceRecord* Parent = Resolve(Folder);
        if (Parent == nullptr || Parent->Subject != WorkspaceRecordSubject::Folder)
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the target folder is not declared" });
    }
    Held->ParentFolder = Folder;
    return Deliver<bool>::Result(true);
}

Deliver<bool> WorkspaceRecordStructure::ToggleVisible(WorkspaceRecordName Subject, bool Visible)
{
    WorkspaceRecord* Held = Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace record is declared" });
    Held->Visible = Visible;
    return Deliver<bool>::Result(true);
}

Deliver<bool> WorkspaceRecordStructure::ToggleLocked(WorkspaceRecordName Subject, bool Locked)
{
    WorkspaceRecord* Held = Resolve(Subject);
    if (Held == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace record is declared" });
    Held->Locked = Locked;
    return Deliver<bool>::Result(true);
}

const WorkspaceRecord* WorkspaceRecordStructure::Resolve(WorkspaceRecordName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldRecords.size())
        return nullptr;
    return &HeldRecords[Subject.IssuedIndex - 1u];
}

WorkspaceRecord* WorkspaceRecordStructure::Resolve(WorkspaceRecordName Subject)
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldRecords.size())
        return nullptr;
    return &HeldRecords[Subject.IssuedIndex - 1u];
}

void WorkspaceRecordStructure::ResolvePresented(std::vector<WorkspacePresentedRow>& Presented) const
{
    Presented.clear();
    Presented.reserve(HeldRecords.size());
    for (std::uint32_t RecordIndex = 1u; RecordIndex <= HeldRecords.size(); ++RecordIndex)
    {
        const WorkspaceRecord& Held = HeldRecords[RecordIndex - 1u];
        Presented.push_back({ { RecordIndex }, Held.Subject, PresentedCategoryOfRecord(Held), Held.ParentFolder,
                              Held.Naming.c_str(), ResolveDepth(Held.ParentFolder), Held.Visible, Held.Locked,
                              Held.ClosedSemantic, Held.CappedExtrusionSemantic, Held.ConstructionSemantic });
    }
}

std::uint32_t WorkspaceRecordStructure::ResolveDepth(WorkspaceRecordName Subject) const
{
    std::uint32_t Depth = 0u;
    WorkspaceRecordName Current = Subject;
    while (Current.Assigned())
    {
        const WorkspaceRecord* Held = Resolve(Current);
        if (Held == nullptr)
            break;
        ++Depth;
        Current = Held->ParentFolder;
    }
    return Depth;
}

void WorkspaceRecordStructure::Reclaim()
{
    HeldRecords.clear();
}

} // namespace Slate
