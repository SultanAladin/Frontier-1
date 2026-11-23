//============================================================================================================================================
//                                                   SKETCHREVISIONHISTORY.H
//============================================================================================================================================
// 🧩 Snapshot-backed undo/redo for the parametric sketch workspace. The editor host owns lifetime and tick
//    order; this unit owns the reversible state transition and keeps it out of the executable boundary.
//
// 🔴 A revision is more than a compatibility SketchStructure. World geometry, the world/compatibility
//    mapping, workplanes, records, names and semantic selection must travel together or undo restores only
//    half of what the artist sees. The snapshot deliberately spans those authorities.

#pragma once

#include "SlateShape/Record/WorkspaceNameIndex/Api/WorkspaceNameIndex.h"
#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"
#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"
#include "SlateWorkspace/Discipline/WorldSketchBridge/Api/WorldSketchBridge.h"

#include <vector>

namespace Slate
{

struct SketchRevisionSnapshot
{
    WorkspaceNameIndex         Naming = {};
    SketchStructure            Sketch = {};
    WorldSketchStructure       World = {};
    WorldSketchMapping         WorldMapping = {};
    WorkspaceRecordStructure   Records = {};
    WorkspaceRevisionSequence  Revisions = {};
    WorkplaneCatalogue         Workplanes = {};
    WorkspaceRecordName        PendingSelection = {};
    SketchPick                 SemanticSelection = {};
};

SketchRevisionSnapshot ResolveSketchRevisionSnapshot(const WorkspaceNameIndex& Naming,
                                                     const SketchStructure& Sketch,
                                                     const WorldSketchStructure& World,
                                                     const WorldSketchMapping& WorldMapping,
                                                     const WorkspaceRecordStructure& Records,
                                                     const WorkspaceRevisionSequence& Revisions,
                                                     const WorkplaneCatalogue& Workplanes,
                                                     WorkspaceRecordName PendingSelection,
                                                     const SketchPick& SemanticSelection);

void ApplySketchRevisionSnapshot(const SketchRevisionSnapshot& Snapshot,
                                 WorkspaceNameIndex& Naming,
                                 SketchStructure& Sketch,
                                 WorldSketchStructure& World,
                                 WorldSketchMapping& WorldMapping,
                                 WorkspaceRecordStructure& Records,
                                 WorkspaceRevisionSequence& Revisions,
                                 WorkplaneCatalogue& Workplanes,
                                 WorkspaceRecordName& PendingSelection,
                                 SketchPick& SemanticSelection);

void RecordSketchRevisionSnapshot(const SketchRevisionSnapshot& Snapshot,
                                  std::vector<SketchRevisionSnapshot>& Retreated,
                                  std::vector<SketchRevisionSnapshot>& Reinstated);

bool RetreatSketchRevision(std::vector<SketchRevisionSnapshot>& Retreated,
                           std::vector<SketchRevisionSnapshot>& Reinstated,
                           WorkspaceNameIndex& Naming,
                           SketchStructure& Sketch,
                           WorldSketchStructure& World,
                           WorldSketchMapping& WorldMapping,
                           WorkspaceRecordStructure& Records,
                           WorkspaceRevisionSequence& Revisions,
                           WorkplaneCatalogue& Workplanes,
                           WorkspaceRecordName& PendingSelection,
                           SketchPick& SemanticSelection);

bool ReinstateSketchRevision(std::vector<SketchRevisionSnapshot>& Retreated,
                             std::vector<SketchRevisionSnapshot>& Reinstated,
                             WorkspaceNameIndex& Naming,
                             SketchStructure& Sketch,
                             WorldSketchStructure& World,
                             WorldSketchMapping& WorldMapping,
                             WorkspaceRecordStructure& Records,
                             WorkspaceRevisionSequence& Revisions,
                             WorkplaneCatalogue& Workplanes,
                             WorkspaceRecordName& PendingSelection,
                             SketchPick& SemanticSelection);

} // namespace Slate
