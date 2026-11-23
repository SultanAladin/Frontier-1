//============================================================================================================================================
//                                                         CODEXACTIVATION.H
//============================================================================================================================================
// 🧩 Opening a codex the artist picked in the content browser: reading it, centring it, and saying plainly
//    when it could not be opened.
//
// 🔴 LIFTED OUT OF `Application/Api/SharedViewportHostBridge.h` — 675 `inline` lines under `Application/`
//    that three hosts included. "Bridge" named the file, not the behaviour. Nothing here is a bridge: it
//    is what happens when a `.codex` is double-clicked, and it is the same in every product that has a
//    content browser.
//
// 📝 The refusal is carried rather than thrown or logged. A host that cannot open a scene has to tell the
//    artist something, and `Refusal` is what it tells them.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Format/WorkspaceSceneActivation/Api/WorkspaceSceneActivation.h"
#include "SlateUI/Interface/ContentBrowserPanel/Api/ContentBrowserPanel.h"

#include <filesystem>

namespace Slate
{

/// 🧩 What came of a request to open a codex.
/// note 📝 `Requested` false means the artist asked for nothing this frame — not that anything failed.
struct CodexActivation
{
    bool                    Requested = false;
    bool                    Resolved = false;
    ActivatedWorkspaceScene Scene = {};
    Refusal                 Error = { RefusalReason::CapabilityAbsent, "no codex activation was requested" };
    std::filesystem::path   ScenePath = {};
};

/// 🧩 Whether a content record is a codex scene at all.
bool ContentRecordIsCodexScene(const ContentRecord& Record);

/// 🧩 Moves an opened scene so its geometry straddles the world origin.
/// note ⚠️ Entries named "Floor" are deliberately excluded from the measurement AND left unmoved — a floor
///       is a backdrop, and letting it drag the centre would push the actual subject off-screen.
void CenterActivatedSceneAtWorldOrigin(ActivatedWorkspaceScene& Scene);

/// 🧩 Takes the browser's pending activation, if there is one, and opens it.
/// note 🔴 CONSUMES the request: the pending index is cleared before the file is touched, so a codex that
///       fails to open is not retried every frame for the life of the session.
/// cost 🔴
/// tag  api, allocating
CodexActivation ConsumeCodexActivation(ContentBrowserConfiguration& Applied,
                                       const ContentLibrary& Library,
                                       const std::filesystem::path& EngineContentRoot);

}   // namespace Slate
