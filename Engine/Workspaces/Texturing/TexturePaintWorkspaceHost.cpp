/*==========================================================================================================================================
                                                   TEXTUREPAINTWORKSPACEHOST.CPP
==========================================================================================================================================*/
// 🏛️ Implementation of TexturePaintWorkspaceHost: 3-column dock registration + floating Project Zero BrushStudio modal.

#include "Engine/Workspaces/Texturing/TexturePaintWorkspaceHost.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Frontier::Workspaces
{

TexturePaintWorkspaceHost::TexturePaintWorkspaceHost()
{
    LayerStackPane.AttachSession(&Session);
    ViewportPane.AttachSession(&Session);
    InspectorPane.AttachSession(&Session);

    InspectorPane.SetOpenBrushStudioCallback([this]()
    {
        ViewportPane.ConfigureBrushInspectorOpen(true);
    });
}

void TexturePaintWorkspaceHost::Initialize(const Configuration::EngineConfiguration* Config) noexcept
{
    if (Config != nullptr)
    {
        ActiveEngineConfig = *Config;
    }
    DockSpaceConfigured = false;
}

void TexturePaintWorkspaceHost::Shutdown() noexcept
{
    DockSpaceConfigured = false;
}

void TexturePaintWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

void TexturePaintWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel)
{
    BoundBrowserPanel = TargetPanel;
}

#if FRONTIER_EDITOR_ENABLED
void TexturePaintWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    AttachContentBrowserPanel(TargetBrowser != nullptr ? &TargetBrowser->AccessPanel() : nullptr);
}
#endif

void TexturePaintWorkspaceHost::OpenBrushStudioModal(float /*AnchorX*/, float /*AnchorY*/) noexcept
{
    ViewportPane.ConfigureBrushInspectorOpen(true);
}

void TexturePaintWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Layers",
        DockPlacementDirection::SplitLeft,
        0.22f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { LayerStackPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Texture Paint",
        DockPlacementDirection::CenterTabGroup,
        0.54f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]() { ViewportPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Layer Inspector",
        DockPlacementDirection::SplitRight,
        0.24f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { InspectorPane.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;
}

void TexturePaintWorkspaceHost::PresentDockedLayout(double DeltaSeconds)
{
    ImGuiViewport* MainViewport = ImGui::GetMainViewport();
    if (MainViewport == nullptr)
    {
        return;
    }

    const ImGuiID DockspaceId = ImGui::GetID("FrontierTexturePaintDockSpace");
    if (!DockSpaceConfigured)
    {
        DockSpaceConfigured = true;
        ImGui::DockBuilderRemoveNode(DockspaceId);
        ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(DockspaceId, MainViewport->WorkSize);

        ImGuiID CenterNode = DockspaceId;
        ImGuiID LeftNode   = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Left, 0.22f, nullptr, &CenterNode);
        ImGuiID RightNode  = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Right, 0.31f, nullptr, &CenterNode);

        ImGui::DockBuilderDockWindow("Layers##PaintLayers_0", LeftNode);
        ImGui::DockBuilderDockWindow("Texture Paint##PaintViewport_0", CenterNode);
        ImGui::DockBuilderDockWindow("Layer Inspector##PaintInspector_0", RightNode);
        ImGui::DockBuilderFinish(DockspaceId);
    }

    ImGui::SetNextWindowPos(MainViewport->WorkPos);
    ImGui::SetNextWindowSize(MainViewport->WorkSize);
    ImGui::SetNextWindowViewport(MainViewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags HostFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::Begin("##FrontierTexturePaintHostWindow", nullptr, HostFlags);
    ImGui::PopStyleVar();
    ImGui::DockSpace(DockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Layers##PaintLayers_0"))
    {
        LayerStackPane.PresentInImGuiPane(DeltaSeconds, 1.0f);
    }
    ImGui::End();

    if (ImGui::Begin("Texture Paint##PaintViewport_0"))
    {
        ViewportPane.PresentInImGuiPane(DeltaSeconds, 1.0f);
    }
    ImGui::End();

    if (ImGui::Begin("Layer Inspector##PaintInspector_0"))
    {
        InspectorPane.PresentInImGuiPane(DeltaSeconds, 1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Frontier::Workspaces
