/*==========================================================================================================================================
                                                    MATERIALWORKSPACEHOST.H
==========================================================================================================================================*/
// 🧩 Top-level Frontier Material Workspace host mounting 56px Studio Header, 3 floating Project Zero panels & GI-free Direct Studio OpenPBR

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Material/MaterialInspectorPane.h"
#include "Engine/Workspaces/Material/MaterialLayerStackPane.h"
#include "Engine/Workspaces/Material/MaterialViewportHost.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"

namespace Frontier::Workspaces
{

class MaterialWorkspaceHost
{
public:
    MaterialWorkspaceHost() = default;

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept;

    void RenderStudio(
        PixelSpace&                       Surface,
        Rectangle2D                       WindowBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] MaterialWorkspaceSession&       AccessSession() noexcept        { return Session; }
    [[nodiscard]] const MaterialWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] MaterialLayerStackPane&         AccessLayerPane() noexcept      { return LayerPane; }
    [[nodiscard]] MaterialViewportHost&           AccessViewport() noexcept       { return Viewport; }
    [[nodiscard]] MaterialInspectorPane&          AccessInspector() noexcept      { return Inspector; }

private:
    void RenderStudioHeader(PixelSpace& Surface, Rectangle2D HeaderRect, const ControlPointer& Pointer) noexcept;
    void RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept;

    MaterialWorkspaceSession Session      {};
    MaterialLayerStackPane   LayerPane    {};
    MaterialViewportHost     Viewport     {};
    MaterialInspectorPane    Inspector    {};
    ContentBrowserPanel*     BoundBrowser = nullptr;
};

} // namespace Frontier::Workspaces
