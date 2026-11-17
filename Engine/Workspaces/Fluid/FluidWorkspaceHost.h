/*============================================================================================================================================
                                                           FLUIDWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level dockable host for Fluid Studio: Outliner + Viewport + Presets + Inspector dock panes, Flipbook & Preset modals

#pragma once

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidAtlasMinimap.h"
#include "Engine/Workspaces/Fluid/FluidInspectorPanel.h"
#include "Engine/Workspaces/Fluid/FluidOutlinerPanel.h"
#include "Engine/Workspaces/Fluid/FluidPresetBrowser.h"
#include "Engine/Workspaces/Fluid/FluidViewportHost.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

namespace Frontier::Workspaces
{

class FluidWorkspaceHost
{
public:
    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept;

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    void SynchronizeHostScale(float ScaleFactor) noexcept;

    void RenderStudio(
        PixelSpace&                       Surface,
        Rectangle2D                       WindowBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] uint32_t                     QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }
    [[nodiscard]] FluidWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const FluidWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] FluidOutlinerPanel&          AccessOutliner() noexcept      { return Outliner; }
    [[nodiscard]] FluidViewportHost&           AccessViewport() noexcept      { return Viewport; }
    [[nodiscard]] FluidInspectorPanel&         AccessInspector() noexcept     { return Inspector; }

private:
    void PresentOutlinerPane();
    void PresentPresetsPane();
    void PresentViewportPane();
    void PresentInspectorPane();

    void RenderFlipbookModal(PixelSpace& Surface, Rectangle2D WindowBounds, const ControlPointer& Pointer) noexcept;
    void RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept;

    FluidWorkspaceSession Session           {};
    FluidOutlinerPanel    Outliner          {};
    FluidViewportHost     Viewport          {};
    FluidInspectorPanel   Inspector         {};
    FluidAtlasMinimap     Minimap           {};
    FluidPresetBrowser    PresetModal       {};
    ContentBrowserPanel*  BoundBrowser      = nullptr;
    float                 HostScaleFactor   = 1.0f;
    uint32_t              RegisteredTabCount = 0u;
};

} // namespace Frontier::Workspaces
