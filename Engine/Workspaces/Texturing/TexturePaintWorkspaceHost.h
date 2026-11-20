#pragma once
/*==========================================================================================================================================
                                                   TEXTUREPAINTWORKSPACEHOST.H
==========================================================================================================================================*/
// 🏛️ TexturePaintWorkspaceHost — 3-column studio dock (Outliner | 3D & UV Paint Viewport | Layer Inspector) + floating Brush Studio modal.

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Workspaces/Texturing/TexturePaintInspectorPane.h"
#include "Engine/Workspaces/Texturing/TexturePaintLayerStackPane.h"
#include "Engine/Workspaces/Texturing/TexturePaintSession.h"
#include "Engine/Workspaces/Texturing/TexturePaintViewportPane.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#endif

#include <cstdint>
#include <filesystem>

namespace Frontier::Workspaces
{

class TexturePaintWorkspaceHost
{
public:
    TexturePaintWorkspaceHost();
    ~TexturePaintWorkspaceHost() noexcept = default;

    TexturePaintWorkspaceHost(const TexturePaintWorkspaceHost&)            = delete;
    TexturePaintWorkspaceHost& operator=(const TexturePaintWorkspaceHost&) = delete;

    void Initialize(const Configuration::EngineConfiguration* Config = nullptr) noexcept;
    void Shutdown() noexcept;
    void PresentDockedLayout(double DeltaSeconds);

    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;
    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel);

#if FRONTIER_EDITOR_ENABLED
    void AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser);
#endif

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    void OpenBrushStudioModal(float AnchorX = -1.0f, float AnchorY = -1.0f) noexcept;
    void CloseBrushStudioModal() noexcept { ViewportPane.ConfigureBrushInspectorOpen(false); }
    [[nodiscard]] bool IsBrushStudioModalOpen() const noexcept { return ViewportPane.QueryBrushInspectorOpen(); }

    [[nodiscard]] TexturePaintSession&        AccessSession() noexcept { return Session; }
    [[nodiscard]] const TexturePaintSession&  AccessSession() const noexcept { return Session; }
    [[nodiscard]] const TexturePaintSession&  InspectSession() const noexcept { return Session; }

    [[nodiscard]] TexturePaintLayerStackPane& AccessLayerStackPane() noexcept { return LayerStackPane; }
    [[nodiscard]] TexturePaintViewportPane&   AccessViewportPane() noexcept { return ViewportPane; }
    [[nodiscard]] TexturePaintInspectorPane&  AccessInspectorPane() noexcept { return InspectorPane; }

    [[nodiscard]] uint32_t QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }

private:
    TexturePaintSession               Session            {};
    TexturePaintLayerStackPane        LayerStackPane     {};
    TexturePaintViewportPane          ViewportPane       {};
    TexturePaintInspectorPane         InspectorPane      {};

    ContentBrowserPanel*              BoundBrowserPanel  = nullptr;
#if FRONTIER_EDITOR_ENABLED
    Editor::Explorer::ContentBrowser* BoundBrowser       = nullptr;
#endif

    Configuration::EngineConfiguration ActiveEngineConfig {};
    uint32_t                           RegisteredTabCount = 0u;
    bool                               DockSpaceConfigured = false;
};

} // namespace Frontier::Workspaces
