#include "SlateDocument/Format/WorkspaceSceneActivation/Api/WorkspaceGeometryIntake.h"
namespace Slate
{
Deliver<std::vector<GeometryIdentity>> WorkspaceGeometryIntake::Import(const std::vector<WorkspaceGeometryQueueEntry>& Queue,
 GeometryFileInterchange& Files, GeometryInterchange& Geometry, IntakeIndex& Intake) const
{
 std::vector<GeometryIdentity> Imported; Imported.reserve(Queue.size());
 for(const WorkspaceGeometryQueueEntry& Current: Queue)
 {
  const Deliver<GeometryAssetView> Added=Files.Import(Current.Geometry.SourcePath,Current.Geometry.Entry.Naming,Geometry,Intake);
  if(!Added.Resolved) return Deliver<std::vector<GeometryIdentity>>::Refuse(Added.Error);
  Imported.push_back(Added.Resolve().Identity);
 }
 return Deliver<std::vector<GeometryIdentity>>::Result(Imported);
}
}
