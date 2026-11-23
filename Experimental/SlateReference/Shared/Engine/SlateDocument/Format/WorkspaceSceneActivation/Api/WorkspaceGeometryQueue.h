#pragma once
#include "SlateDocument/Format/WorkspaceSceneActivation/Api/WorkspaceSceneActivation.h"
#include <cstdint>
#include <vector>
namespace Slate
{
struct WorkspaceGeometryQueueEntry { ActivatedGeometryEntry Geometry = {}; std::uint32_t Sequence = 0u; };
class WorkspaceGeometryQueue
{
public:
 Deliver<std::vector<WorkspaceGeometryQueueEntry>> Prepare(const ActivatedWorkspaceScene& Scene) const;
};
}
