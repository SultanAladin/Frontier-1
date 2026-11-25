/*============================================================================================================================================
                                                           BAKINGWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of BakingWorkspaceHost 3-column studio dock (Bake Targets | Baking Viewport | Baker Parameters).

#include "Engine/Workspaces/Texturing/BakingWorkspaceHost.h"

namespace Frontier::Workspaces
{

BakingWorkspaceHost::BakingWorkspaceHost()
{
    TargetsPane.AttachSession(&Session);
    ViewportPane.AttachSession(&Session);
    InspectorPane.AttachSession(&Session);
}

void BakingWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

void BakingWorkspaceHost::AttachVulkanDevice(
    GeometricRaster::VulkanInterchange* ExternalDevice,
    const std::filesystem::path&        ShaderDirectory)
{
    Session.AttachExternalVulkanDevice(ExternalDevice, ShaderDirectory);
    ViewportPane.InvalidateCachedFrames();
}

void BakingWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel)
{
    BoundBrowserPanel = TargetPanel;
}

#if FRONTIER_EDITOR_ENABLED
void BakingWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    AttachContentBrowserPanel(TargetBrowser != nullptr ? &TargetBrowser->AccessPanel() : nullptr);
}
#endif

void BakingWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Bake Map Targets",
        DockPlacementDirection::SplitLeft,
        0.22f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { TargetsPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Baking",
        DockPlacementDirection::CenterTabGroup,
        0.53f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]() { ViewportPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Baker Parameters",
        DockPlacementDirection::SplitRight,
        0.25f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { InspectorPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
}

bool BakingWorkspaceHost::IntakeExternalFileDrop(
    const std::string& DropFilePath,
    bool               ImportAsHighPoly,
    std::string&       OutDiagnostic)
{
    const bool Loaded = Session.LoadCustomStudyAsset(std::filesystem::path(DropFilePath), ImportAsHighPoly, &OutDiagnostic);
    if (Loaded)
    {
        ViewportPane.InvalidateCachedFrames();
    }
    return Loaded;
}

} // namespace Frontier::Workspaces
