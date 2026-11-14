/*============================================================================================================================================
                                                            UVWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Dual-deployment UV Workspace host mounting StandaloneWorkspace (FrontierUV) or FeatureSet docked panes + ContentBrowser integration

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Workspaces/UV/UVInspectorPanel.h"
#include "Engine/Workspaces/UV/UVOutlinerPanel.h"
#include "Engine/Workspaces/UV/UVViewportHost.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#endif

#include <filesystem>
#include <string>

namespace Frontier::Workspaces
{

class UVWorkspaceHost
{
public:
    UVWorkspaceHost();

    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;
    [[nodiscard]] bool AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer);

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel);
#if FRONTIER_EDITOR_ENABLED
    void AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser);
#endif

    [[nodiscard]] bool IntakeExternalFileDrop(
        const std::filesystem::path& DroppedPath,
        std::string&                 OutDiagnostic);

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    [[nodiscard]] UVWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const UVWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] UVOutlinerPanel&          AccessOutliner() noexcept      { return Outliner; }
    [[nodiscard]] UVViewportHost&           AccessViewportHost() noexcept  { return ViewportHost2D; }
    [[nodiscard]] UVViewportHost&           Access2DViewportHost() noexcept { return ViewportHost2D; }
    [[nodiscard]] UVViewportHost&           Access3DViewportHost() noexcept { return ViewportHost3D; }
    [[nodiscard]] UVInspectorPanel&         AccessInspector() noexcept     { return Inspector; }
    [[nodiscard]] uint32_t                  QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }

private:
    Configuration::EngineConfiguration ActiveEngineConfig {};
    UVWorkspaceSession                 Session            {};
    UVOutlinerPanel                    Outliner           {};
    UVViewportHost                     ViewportHost2D     {};
    UVViewportHost                     ViewportHost3D     {};
    UVInspectorPanel                   Inspector          {};
    uint32_t                           RegisteredTabCount = 0u;
    ContentBrowserPanel*               BoundBrowserPanel  = nullptr;
#if FRONTIER_EDITOR_ENABLED
    Editor::Explorer::ContentBrowser*  BoundBrowser       = nullptr;
#endif
};

} // namespace Frontier::Workspaces
