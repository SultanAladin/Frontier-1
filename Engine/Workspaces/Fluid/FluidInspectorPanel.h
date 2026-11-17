/*============================================================================================================================================
                                                          FLUIDINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Floating 22px right Tabbed Inspector (Emitter/Blast, Solver/WorldGrid, Shading/Palette, Forces/GI)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

namespace Frontier::Workspaces
{

class FluidInspectorPanel
{
public:
    static constexpr float RecommendedWidthPx = 348.0f;

    void Render(
        PixelSpace&            Surface,
        Rectangle2D            Bounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer,
        float                  Opacity = 1.0f) noexcept;

private:
    bool DrawSliderRow(
        PixelSpace&           Surface,
        Rectangle2D           RowRect,
        const char*           Label,
        float&                InOutValue,
        float                 MinVal,
        float                 MaxVal,
        const ControlPointer& Pointer,
        uint32_t              SliderSlot) noexcept;

    bool DrawToggleRow(
        PixelSpace&           Surface,
        Rectangle2D           RowRect,
        const char*           Label,
        bool&                 InOutChecked,
        const ControlPointer& Pointer) noexcept;

    int32_t ActiveDragSlider = -1;
};

} // namespace Frontier::Workspaces
