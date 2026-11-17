/*============================================================================================================================================
                                                            FLUIDWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level Frontier Fluid Studio host mounting 56px Studio Header, 3 floating 22px panels, PresetBrowser, Minimap & Status Bar

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

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
    FluidWorkspaceHost() = default;

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept;

    void RenderStudio(
        PixelSpace&                       Surface,
        Rectangle2D                       WindowBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] FluidWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const FluidWorkspaceSession& InspectSession() const noexcept { return Session; }

private:
    void RenderStudioHeader(PixelSpace& Surface, Rectangle2D HeaderRect, const ControlPointer& Pointer) noexcept;
    void RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept;

    FluidWorkspaceSession Session      {};
    FluidOutlinerPanel    Outliner     {};
    FluidViewportHost     Viewport     {};
    FluidInspectorPanel   Inspector    {};
    FluidPresetBrowser    PresetModal  {};
    FluidAtlasMinimap     Minimap      {};
    ContentBrowserPanel*  BoundBrowser = nullptr;
};

} // namespace Frontier::Workspaces
