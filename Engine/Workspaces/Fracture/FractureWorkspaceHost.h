/*============================================================================================================================================
                                                          FRACTUREWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level Frontier Fracture Studio host mounting 56px Studio Header, floating 22px Outliner & Inspector panels, OpenPBR 3D Viewport & Status Bar

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/Fracture/FractureInspectorPanel.h"
#include "Engine/Workspaces/Fracture/FractureOutlinerPanel.h"
#include "Engine/Workspaces/Fracture/FractureViewportHost.h"
#include "Engine/Workspaces/Fracture/FractureViewportRenderer.h"
#include "Engine/Workspaces/Fracture/FractureWorkspaceSession.h"

#include <filesystem>

namespace Frontier::Workspaces::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FractureWorkspaceHost
{
public:
    FractureWorkspaceHost();

    void AttachVulkanDevice(
        GeometricRaster::VulkanInterchange& TargetDevice,
        const std::filesystem::path&        CompiledShaderDirectory) noexcept;

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept;

    void RenderStudio(
        PixelSpace&                       Surface,
        Rectangle2D                       WindowBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] FractureWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const FractureWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] FractureOutlinerPanel&          AccessOutliner() noexcept      { return Outliner; }
    [[nodiscard]] FractureViewportHost&           AccessViewport() noexcept      { return Viewport; }
    [[nodiscard]] FractureInspectorPanel&         AccessInspector() noexcept     { return Inspector; }
    [[nodiscard]] FractureViewportRenderer&       AccessRenderer() noexcept      { return Renderer; }

private:
    void RenderStudioHeader(PixelSpace& Surface, Rectangle2D HeaderRect, const ControlPointer& Pointer) noexcept;
    void RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept;

    FractureWorkspaceSession Session      {};
    FractureViewportRenderer Renderer     {};
    FractureOutlinerPanel    Outliner     {};
    FractureViewportHost     Viewport     {};
    FractureInspectorPanel   Inspector    {};
    ContentBrowserPanel*     BoundBrowser = nullptr;
};

} // namespace Frontier::Workspaces::Fracture
