/*============================================================================================================================================
                                                          PARTICLEWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level Frontier Particle Studio host mounting 56px Studio Header, 3 floating 22px panels, modal 93-Preset Assets Browser & ContentBrowser ramp drag-drop

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Particle/ParticleInspectorPanel.h"
#include "Engine/Workspaces/Particle/ParticleOutlinerPanel.h"
#include "Engine/Workspaces/Particle/ParticleViewportHost.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

namespace Frontier::Workspaces
{

class ParticleWorkspaceHost
{
public:
    ParticleWorkspaceHost();

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept;

    void RenderStudio(
        PixelSpace&                       Surface,
        Rectangle2D                       WindowBounds,
        const ContentBrowserPointerInput& PointerInput,
        double                            DeltaSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] ParticleWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const ParticleWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] ParticleOutlinerPanel&          AccessOutliner() noexcept      { return Outliner; }
    [[nodiscard]] ParticleViewportHost&           AccessViewport() noexcept      { return Viewport; }
    [[nodiscard]] ParticleInspectorPanel&         AccessInspector() noexcept     { return Inspector; }

private:
    void RenderStudioHeader(PixelSpace& Surface, Rectangle2D HeaderRect, const ControlPointer& Pointer) noexcept;
    void RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept;
    void RenderAssetsBrowserOverlay(PixelSpace& Surface, Rectangle2D WindowBounds, const ControlPointer& Pointer) noexcept;

    ParticleWorkspaceSession Session      {};
    ParticleOutlinerPanel    Outliner     {};
    ParticleViewportHost     Viewport     {};
    ParticleInspectorPanel   Inspector    {};
    ContentBrowserPanel*     BoundBrowser = nullptr;
};

} // namespace Frontier::Workspaces
