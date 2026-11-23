//============================================================================================================================================
//                                                  WORKSPACEPROPERTYPROJECTION.H
//============================================================================================================================================
// 🧩 The backend projection the future properties panel reads: selected semantic record plus the filtered
//    revision subset that belongs in the Properties | Revision pages.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"
#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"

#include <vector>

namespace Slate
{

struct WorkspacePropertyProjection
{
    const WorkspaceRecord* Subject = nullptr;
    std::vector<WorkspaceRevisionName> RevisionSet = {};
};

Deliver<WorkspacePropertyProjection> ProjectWorkspaceProperty(const WorkspaceRecordStructure& Records,
                                                              const WorkspaceRevisionSequence& Revisions,
                                                              WorkspaceRecordName Subject);

} // namespace Slate
