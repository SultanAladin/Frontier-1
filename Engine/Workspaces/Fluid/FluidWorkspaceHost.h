/*============================================================================================================================================
                                                           FLUIDWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level host for Fluid Studio: 52px header + 34px document bar + 3-column workspace + Flipbook modal + Preset modal + Status bar

#pragma once

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
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

    void RenderStudio(
        PixelSpace&                       Surface,
        Rectangle2D                       WindowBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] FluidWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const FluidWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] FluidOutlinerPanel&          AccessOutliner() noexcept      { return Outliner; }
    [[nodiscard]] FluidViewportHost&           AccessViewport() noexcept      { return Viewport; }
    [[nodiscard]] FluidInspectorPanel&         AccessInspector() noexcept     { return Inspector; }

private:
    void RenderStudioHeader(PixelSpace& Surface, Rectangle2D HeaderRect, const ControlPointer& Pointer) noexcept;
    void RenderDocumentBar(PixelSpace& Surface, Rectangle2D DocRect, const ControlPointer& Pointer) noexcept;
    void RenderFlipbookModal(PixelSpace& Surface, Rectangle2D WindowBounds, const ControlPointer& Pointer) noexcept;
    void RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept;

    FluidWorkspaceSession Session      {};
    FluidOutlinerPanel    Outliner     {};
    FluidViewportHost     Viewport     {};
    FluidInspectorPanel   Inspector    {};
    FluidAtlasMinimap     Minimap      {};
    FluidPresetBrowser    PresetModal  {};
    ContentBrowserPanel*  BoundBrowser = nullptr;
};

} // namespace Frontier::Workspaces
