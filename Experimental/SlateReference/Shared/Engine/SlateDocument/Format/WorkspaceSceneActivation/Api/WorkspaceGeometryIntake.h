#pragma once
#include "SlateDocument/Document/GeometryInterchange/Api/GeometryFileInterchange.h"
#include "SlateDocument/Format/WorkspaceSceneActivation/Api/WorkspaceGeometryQueue.h"
#include <vector>
namespace Slate
{
class WorkspaceGeometryIntake
{
public:
 Deliver<std::vector<GeometryIdentity>> Import(const std::vector<WorkspaceGeometryQueueEntry>& Queue,
                                                GeometryFileInterchange& Files, GeometryInterchange& Geometry,
                                                IntakeIndex& Intake) const;
};
}
