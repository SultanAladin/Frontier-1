/*============================================================================================================================================
                                                           FLUIDVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Center panel for Fluid Studio: Scene View top bar, 3D floor grid + volumetric fire/smoke viewport, HUD, Diagnostics, Transport & Ruler

#pragma once

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

#include <imgui.h>

namespace Frontier::Workspaces
{

class FluidViewportHost
{
public:
    FluidViewportHost() = default;
    ~FluidViewportHost() noexcept;

    FluidViewportHost(const FluidViewportHost&)            = delete;
    FluidViewportHost& operator=(const FluidViewportHost&) = delete;

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
    void UploadPreviewTexture(const std::vector<uint8_t>& RgbaPixels, uint32_t WidthPx, uint32_t HeightPx);

    ImTextureData PreviewTexture     {};
    Rectangle2D   LastSurfaceBounds  {};
    Rectangle2D LastBakeButtonBounds {};
    Rectangle2D LastDiagButtonBounds {};
    Rectangle2D LastBoundsBtnBounds  {};
    bool        DraggingOrbit        = false;
    Point2D     LastPointerPos       {};
};

} // namespace Frontier::Workspaces
