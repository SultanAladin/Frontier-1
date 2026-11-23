//============================================================================================================================================
//                                                        CONTENTIMPORTCOMMIT.H
//============================================================================================================================================
// 🧩 Takes the file the artist confirmed in the content browser and puts it into the open scene:
//    a material image binds to a material slot, anything else imports as a mesh.
//
// 🔴 THIS WAS 82 LINES INSIDE `ParametricSketchHost`, AND DELETING THAT HOST WOULD HAVE TAKEN IT.
//    The hosts had been reduced to zero definitions, so nothing failed to compile and no gate went
//    red -- three gates asserted this behaviour BY ITS ADDRESS in a file that no longer existed, and
//    they reported a missing FILE, not missing behaviour. Importing a mesh would simply have stopped
//    working in a build that looked entirely healthy.
//
// 📝 The same lesson as the workplane tool, from the other direction: zero definitions is not zero
//    responsibility. Wiring is invisible to a gate that counts definitions, so it has to be moved
//    deliberately, not assumed to have moved with everything else.

#pragma once

#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"
#include "SlateUI/Interface/ContentBrowserPanel/Api/ContentBrowserPanel.h"
#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectoryPanel.h"
#include "SlateWorkspace/Discipline/CodexSceneProxy/Api/CodexSceneProxy.h"

namespace Slate
{

/// 🧩 Commits the confirmed import into the scene, clearing the request either way.
/// in    Named     [-]  the host name, used only in the refusal messages
/// out   Scene     [-]  gains the mesh, or the material image binding
/// out   Standing  [-]  set when an import opens a scene that was not standing
void CommitConfirmedImport(const char* Named,
                           ContentBrowserConfiguration& ContentBrowserApplied,
                           const SceneDirectoryRows& SceneDirectoryStorage,
                           SceneDirectoryContext& SceneApplied,
                           WorkspaceCodex& OpenedScene,
                           bool& OpenedSceneStanding);

}   // namespace Slate
