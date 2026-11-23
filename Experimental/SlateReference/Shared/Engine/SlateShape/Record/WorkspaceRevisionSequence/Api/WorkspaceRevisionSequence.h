//============================================================================================================================================
//                                                    WORKSPACEREVISIONSEQUENCE.H
//============================================================================================================================================
// 🧩 Committed workspace revisions, stored once and later filtered by selected semantic record for the
//    Properties | Revision page.

#pragma once

#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

struct WorkspaceRevisionName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct WorkspaceRevision
{
    std::string Description = {};
    std::string Operation = {};
    std::vector<WorkspaceRecordName> Affected = {};
    std::uint64_t SealedAt = 0u;
};

class WorkspaceRevisionSequence
{
public:
    WorkspaceRevisionName Seal(const std::string& Description,
                               const std::string& Operation,
                               const std::vector<WorkspaceRecordName>& Affected,
                               std::uint64_t SealedAt);
    const WorkspaceRevision* Resolve(WorkspaceRevisionName Subject) const;
    void ResolveForRecord(WorkspaceRecordName Subject,
                          std::vector<WorkspaceRevisionName>& Presented) const;
    std::uint32_t DeclaredCount() const { return static_cast<std::uint32_t>(HeldRevisions.size()); }
    void Reclaim();

private:
    std::vector<WorkspaceRevision> HeldRevisions = {};
};

} // namespace Slate
