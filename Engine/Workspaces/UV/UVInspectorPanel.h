/*============================================================================================================================================
                                                           UVINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 5-tab UV Inspector pane matching Experimental/UVEditor pixel-for-pixel (Pack, Unwrap, UDIM, Shading, Check) with ThemeRepository styling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

class UVInspectorPanel
{
public:
    UVInspectorPanel() noexcept = default;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    float RenderPackingTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);
    float RenderUnwrapTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);
    float RenderUDIMTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);
    float RenderShadingTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);
    float RenderCheckTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);

    UVWorkspaceSession* Session          = nullptr;
    float               ScrollOffsetY    = 0.0f;
    float               SurfaceRoughness = 0.22f;
    float               SurfaceMetallic  = 0.68f;
    float               SurfaceMatBlend  = 0.55f;
    uint8_t             SelectedMatCap   = 2u;
};

} // namespace Frontier::Workspaces
