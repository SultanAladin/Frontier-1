/*============================================================================================================================================
                                                            BAKINGWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 3-column dock host for Frontier Baking Studio: Targets (left) | Viewport (center) | Inspector (right).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Workspaces/Texturing/BakingInspectorPane.h"
#include "Engine/Workspaces/Texturing/BakingTargetsPane.h"
#include "Engine/Workspaces/Texturing/BakingViewportPane.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceSession.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#endif

#include <filesystem>
#include <string>

namespace Frontier::Workspaces
{

class BakingWorkspaceHost
{
public:
    BakingWorkspaceHost();
    ~BakingWorkspaceHost() noexcept = default;

    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;
    void AttachVulkanDevice(
        GeometricRaster::VulkanInterchange* ExternalDevice,
        const std::filesystem::path&        ShaderDirectory = {});
    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel);
#if FRONTIER_EDITOR_ENABLED
    void AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser);
#endif
    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    [[nodiscard]] bool IntakeExternalFileDrop(
        const std::string& DropFilePath,
        bool               ImportAsHighPoly,
        std::string&       OutDiagnostic);

    [[nodiscard]] BakingWorkspaceSession&       AccessSession() noexcept { return Session; }
    [[nodiscard]] const BakingWorkspaceSession& InspectSession() const noexcept { return Session; }

    [[nodiscard]] BakingTargetsPane&            AccessTargetsPane() noexcept { return TargetsPane; }
    [[nodiscard]] BakingViewportPane&           AccessViewportPane() noexcept { return ViewportPane; }
    [[nodiscard]] BakingInspectorPane&          AccessInspectorPane() noexcept { return InspectorPane; }

    [[nodiscard]] std::size_t QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }

private:
    BakingWorkspaceSession             Session            {};
    BakingTargetsPane                  TargetsPane        {};
    BakingViewportPane                 ViewportPane       {};
    BakingInspectorPane                InspectorPane      {};
    Configuration::EngineConfiguration ActiveEngineConfig {};
    ContentBrowserPanel*               BoundBrowserPanel  = nullptr;
#if FRONTIER_EDITOR_ENABLED
    Editor::Explorer::ContentBrowser*  BoundBrowser       = nullptr;
#endif
    std::size_t                        RegisteredTabCount = 0u;
};

} // namespace Frontier::Workspaces
