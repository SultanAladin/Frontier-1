/*============================================================================================================================================
                                                            FLUIDVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Floating 22px Center Viewport panel with live volumetric raymarch preview, 4-tool rail, orbit/Pyro-brush input & playback controls

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

namespace Frontier::Workspaces
{

class FluidViewportHost
{
public:
    void Render(
        PixelSpace&            Surface,
        Rectangle2D            Bounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer,
        float                  Opacity = 1.0f) noexcept;

    [[nodiscard]] Rectangle2D QueryLastSurfaceBounds() const noexcept { return LastSurfaceBounds; }

private:
    Rectangle2D LastSurfaceBounds {};
    bool        DraggingOrbit     = false;
    Point2D     LastPointerPos    {};
};

} // namespace Frontier::Workspaces
