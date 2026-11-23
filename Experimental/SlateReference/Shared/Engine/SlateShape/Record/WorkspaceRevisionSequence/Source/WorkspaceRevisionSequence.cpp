//============================================================================================================================================
//                                                  WORKSPACEREVISIONSEQUENCE.CPP
//============================================================================================================================================

#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"

namespace Slate
{

WorkspaceRevisionName WorkspaceRevisionSequence::Seal(const std::string& Description,
                                                      const std::string& Operation,
                                                      const std::vector<WorkspaceRecordName>& Affected,
                                                      std::uint64_t SealedAt)
{
    HeldRevisions.push_back({ Description, Operation, Affected, SealedAt });
    return { static_cast<std::uint32_t>(HeldRevisions.size()) };
}

const WorkspaceRevision* WorkspaceRevisionSequence::Resolve(WorkspaceRevisionName Subject) const
{
    if (!Subject.Assigned() || Subject.IssuedIndex > HeldRevisions.size())
        return nullptr;
    return &HeldRevisions[Subject.IssuedIndex - 1u];
}

void WorkspaceRevisionSequence::ResolveForRecord(WorkspaceRecordName Subject,
                                                 std::vector<WorkspaceRevisionName>& Presented) const
{
    Presented.clear();
    for (std::uint32_t RevisionIndex = 1u; RevisionIndex <= HeldRevisions.size(); ++RevisionIndex)
    {
        const WorkspaceRevision& Held = HeldRevisions[RevisionIndex - 1u];
        for (WorkspaceRecordName Affected : Held.Affected)
            if (Affected.IssuedIndex == Subject.IssuedIndex)
            {
                Presented.push_back({ RevisionIndex });
                break;
            }
    }
}

void WorkspaceRevisionSequence::Reclaim()
{
    HeldRevisions.clear();
}

} // namespace Slate
