/*============================================================================================================================================
                                                           UVINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 5-tab UV Inspector pane matching Experimental/UVEditor pixel-for-pixel (Pack, Unwrap, UDIM, Shading, Check) with ThemeRepository styling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

#include <array>

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
    float RenderPackingTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, Rectangle2D PopupClip, const PaneInputReading& Input, double Δt);
    float RenderUnwrapTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);
    float RenderUDIMTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);
    float RenderShadingTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);
    float RenderCheckTab(PixelSpace& Surface, float LeftX, float StartY, float InnerW, const PaneInputReading& Input);

    UVWorkspaceSession* Session          = nullptr;
    float               ScrollOffsetY    = 0.0f;
    uint64_t            CachedNetworkRevision = UINT64_MAX;
    uint64_t            CachedUVRevision = UINT64_MAX;
    uint32_t            CachedChartCount = 0u;
    std::array<float, 5u> CachedTabContentHeights {};
    float               SurfaceRoughness = 0.24f;
    float               SurfaceMetallic  = 0.60f;
    float               SurfaceSpecular  = 0.50f;
    float               SurfaceMatBlend  = 0.62f;
    uint8_t             SelectedMatCap   = 5u;
    TextEditBuffer      LayerNameEditor  {};
    uint16_t            EditingLayerOrdinal = 0u;
    bool                ActiveLayerDropdownOpen = false;
    bool                TargetUdimDropdownOpen = false;
    bool                PackingScopeDropdownOpen = false;
    bool                ShadingModeDropdownOpen = false;
    bool                LayerColorPickerOpen = false;
    bool                LiveUnwrapEnabled = false;
};

} // namespace Frontier::Workspaces
