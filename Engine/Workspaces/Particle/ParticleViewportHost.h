/*============================================================================================================================================
                                                          PARTICLEVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Floating 22px Center Viewport panel with Viewport Bar, live 3D particle/field preview, 5-tool rail, SimulationPlaybackControls, SimulationTimeRuler & LogStreamDrawer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

namespace Frontier::Workspaces
{

class ParticleViewportHost
{
public:
    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        ParticleWorkspaceSession& Session,
        const ControlPointer&     Pointer,
        float                     Opacity = 1.0f) noexcept;

    [[nodiscard]] Rectangle2D QueryLastViewportSurface() const noexcept { return LastSurfaceBounds; }
    [[nodiscard]] Rectangle2D QueryLastPlaybackBounds() const noexcept  { return LastPlaybackBounds; }
    [[nodiscard]] Rectangle2D QueryLastRulerBounds() const noexcept     { return LastRulerBounds; }

private:
    void RenderViewportPreview3D(
        PixelSpace&                     Surface,
        Rectangle2D                     ViewportRect,
        const ParticleWorkspaceSession& Session) const noexcept;

    Rectangle2D LastSurfaceBounds  {};
    Rectangle2D LastPlaybackBounds {};
    Rectangle2D LastRulerBounds    {};
    bool        DraggingOrbit      = false;
    Point2D     LastPointerPos     {};
};

} // namespace Frontier::Workspaces
