//============================================================================================================================================
//                                                WORKSPACEPROPERTYPROJECTION.CPP
//============================================================================================================================================

#include "SlateShape/Record/WorkspacePropertyProjection/Api/WorkspacePropertyProjection.h"

namespace Slate
{

Deliver<WorkspacePropertyProjection> ProjectWorkspaceProperty(const WorkspaceRecordStructure& Records,
                                                              const WorkspaceRevisionSequence& Revisions,
                                                              WorkspaceRecordName Subject)
{
    const WorkspaceRecord* Record = Records.Resolve(Subject);
    if (Record == nullptr)
        return Deliver<WorkspacePropertyProjection>::Refuse({ RefusalReason::ContentUnsupported, "no such workspace record is declared" });

    WorkspacePropertyProjection Projection;
    Projection.Subject = Record;
    Revisions.ResolveForRecord(Subject, Projection.RevisionSet);
    return Deliver<WorkspacePropertyProjection>::Result(Projection);
}

} // namespace Slate
