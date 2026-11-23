//============================================================================================================================================
//                                                  WORKSPACESCENEACTIVATION.H
//============================================================================================================================================
// 🧩 Validated workspace-Codex activation data, independent of editor, UI, GPU, and scene lifetime.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"

#include <string>
#include <vector>

namespace Slate
{

/// 🧩 One geometry entry after its binary workspace identity has been validated.
struct ActivatedGeometryEntry
{
    CodexSceneEntry Entry = {};
    std::string SourcePath = {}; // [-] - codex fragment identity, not an external OBJ path
};

/// 🧩 Immutable result a host may commit atomically into its Outliner, layer model, and geometry intake queue.
struct ActivatedWorkspaceScene
{
    WorkspaceCodex Workspace = {};
    std::vector<ActivatedGeometryEntry> Geometry = {};
    std::vector<WorkspaceMaterialRecord> Materials = {};
};

/// 🧩 Opens and validates a WorkspaceCodex before a host changes its presented scene.
class WorkspaceSceneActivation
{
public:
    Deliver<ActivatedWorkspaceScene> Open(const std::string& CodexPath,
                                          const std::string& EngineContentPath) const;
};

} // namespace Slate
