/*============================================================================================================================================
                                                           FLUIDVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Center panel for Fluid Studio: Scene View top bar, 3D floor grid + volumetric fire/smoke viewport, HUD, Diagnostics, Transport & Ruler

#pragma once

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

    [[nodiscard]] Rectangle2D QueryLastSurfaceBounds() const noexcept     { return LastSurfaceBounds; }
    [[nodiscard]] Rectangle2D QueryLastBakeButtonBounds() const noexcept  { return LastBakeButtonBounds; }
    [[nodiscard]] Rectangle2D QueryLastDiagButtonBounds() const noexcept  { return LastDiagButtonBounds; }
    [[nodiscard]] Rectangle2D QueryLastBoundsBtnBounds() const noexcept   { return LastBoundsBtnBounds; }

private:
    Rectangle2D LastSurfaceBounds    {};
    Rectangle2D LastBakeButtonBounds {};
    Rectangle2D LastDiagButtonBounds {};
    Rectangle2D LastBoundsBtnBounds  {};
    bool        DraggingOrbit        = false;
    Point2D     LastPointerPos       {};
};

} // namespace Frontier::Workspaces
