/*============================================================================================================================================
                                                           MATERIALWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Native Frontier Material Workspace host: Material Viewport pane + ContentBrowser integration (Inspector & Layer Stack deferred)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/Material/MaterialInspectorPane.h"
#include "Engine/Workspaces/Material/MaterialOutlinerPane.h"
#include "Engine/Workspaces/Material/MaterialPreviewRenderer.h"
#include "Engine/Workspaces/Material/MaterialViewportHost.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#endif

#include <cstdint>
#include <filesystem>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          TYPES
//------------------------------------------------------------------------------------------------------------------------

class MaterialWorkspaceHost
{
public:
    MaterialWorkspaceHost();
    ~MaterialWorkspaceHost() noexcept = default;

    MaterialWorkspaceHost(const MaterialWorkspaceHost&)            = delete;
    MaterialWorkspaceHost& operator=(const MaterialWorkspaceHost&) = delete;

    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;
    bool AttachVulkanDevice(
        GeometricRaster::VulkanInterchange* ExternalDevice,
        const std::filesystem::path&        CompiledShaderPath = {});

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel);
#if FRONTIER_EDITOR_ENABLED
    void AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser);
#endif

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    void SynchronizeContentBrowserOcclusion(float ViewportScaleFactor = 1.0f) noexcept;
    void PopulateDynamicMaterialThumbnails();

    [[nodiscard]] MaterialWorkspaceSession&       AccessSession() noexcept { return Session; }
    [[nodiscard]] const MaterialWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] MaterialPreviewRenderer&        AccessPreviewRenderer() noexcept { return PreviewRenderer; }
    [[nodiscard]] MaterialViewportHost&           AccessViewportHost() noexcept { return ViewportHost; }
    [[nodiscard]] MaterialOutlinerPane&           AccessOutlinerPane() noexcept { return OutlinerPane; }
    [[nodiscard]] MaterialInspectorPane&          AccessInspectorPane() noexcept { return InspectorPane; }
    [[nodiscard]] uint32_t                        QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }
    [[nodiscard]] std::size_t                     QueryPopulatedThumbnailCount() const noexcept { return PopulatedThumbnailCount; }

private:
    bool ApplyContentBrowserEntry(const ContentRepositoryEntry& Entry);

    Configuration::EngineConfiguration ActiveEngineConfig      {};
    MaterialWorkspaceSession           Session                 {};
    MaterialPreviewRenderer            PreviewRenderer         {};
    MaterialViewportHost               ViewportHost            {};
    MaterialOutlinerPane               OutlinerPane            {};
    MaterialInspectorPane              InspectorPane           {};
    ContentBrowserPanel*               BoundBrowserPanel       = nullptr;
#if FRONTIER_EDITOR_ENABLED
    Editor::Explorer::ContentBrowser*  BoundBrowser            = nullptr;
#endif
    uint32_t                           RegisteredTabCount      = 0u;
    std::size_t                        PopulatedThumbnailCount = 0u;
};

} // namespace Frontier::Workspaces
