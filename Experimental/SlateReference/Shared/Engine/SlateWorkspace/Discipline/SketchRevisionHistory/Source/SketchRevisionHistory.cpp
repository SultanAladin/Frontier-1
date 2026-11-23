//============================================================================================================================================
//                                                  SKETCHREVISIONHISTORY.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/SketchRevisionHistory/Api/SketchRevisionHistory.h"

namespace Slate
{

SketchRevisionSnapshot ResolveSketchRevisionSnapshot(const WorkspaceNameIndex& Naming,
                                                     const SketchStructure& Sketch,
                                                     const WorldSketchStructure& World,
                                                     const WorldSketchMapping& WorldMapping,
                                                     const WorkspaceRecordStructure& Records,
                                                     const WorkspaceRevisionSequence& Revisions,
                                                     const WorkplaneCatalogue& Workplanes,
                                                     WorkspaceRecordName PendingSelection,
                                                     const SketchPick& SemanticSelection)
{
    SketchRevisionSnapshot Snapshot = {};
    Snapshot.Naming = Naming;
    Snapshot.Sketch = Sketch;
    Snapshot.World = World;
    Snapshot.WorldMapping = WorldMapping;
    Snapshot.Records = Records;
    Snapshot.Revisions = Revisions;
    Snapshot.Workplanes = Workplanes;
    Snapshot.PendingSelection = PendingSelection;
    Snapshot.SemanticSelection = SemanticSelection;
    return Snapshot;
}

void ApplySketchRevisionSnapshot(const SketchRevisionSnapshot& Snapshot,
                                 WorkspaceNameIndex& Naming,
                                 SketchStructure& Sketch,
                                 WorldSketchStructure& World,
                                 WorldSketchMapping& WorldMapping,
                                 WorkspaceRecordStructure& Records,
                                 WorkspaceRevisionSequence& Revisions,
                                 WorkplaneCatalogue& Workplanes,
                                 WorkspaceRecordName& PendingSelection,
                                 SketchPick& SemanticSelection)
{
    Naming = Snapshot.Naming;
    Sketch = Snapshot.Sketch;
    World = Snapshot.World;
    WorldMapping = Snapshot.WorldMapping;
    Records = Snapshot.Records;
    Revisions = Snapshot.Revisions;
    Workplanes = Snapshot.Workplanes;
    PendingSelection = Snapshot.PendingSelection;
    SemanticSelection = Snapshot.SemanticSelection;
}

void RecordSketchRevisionSnapshot(const SketchRevisionSnapshot& Snapshot,
                                  std::vector<SketchRevisionSnapshot>& Retreated,
                                  std::vector<SketchRevisionSnapshot>& Reinstated)
{
    if (!Retreated.empty() &&
        Retreated.back().Revisions.DeclaredCount() == Snapshot.Revisions.DeclaredCount())
        return;

    Retreated.push_back(Snapshot);
    if (Retreated.size() > 128u)
        Retreated.erase(Retreated.begin() + 1u);
    Reinstated.clear();
}

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
                           SketchPick& SemanticSelection)
{
    if (Retreated.size() <= 1u)
        return false;

    Reinstated.push_back(Retreated.back());
    Retreated.pop_back();
    ApplySketchRevisionSnapshot(Retreated.back(), Naming, Sketch, World, WorldMapping,
                                Records, Revisions, Workplanes, PendingSelection, SemanticSelection);
    return true;
}

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
                             SketchPick& SemanticSelection)
{
    if (Reinstated.empty())
        return false;

    Retreated.push_back(Reinstated.back());
    ApplySketchRevisionSnapshot(Retreated.back(), Naming, Sketch, World, WorldMapping,
                                Records, Revisions, Workplanes, PendingSelection, SemanticSelection);
    Reinstated.pop_back();
    return true;
}

} // namespace Slate
