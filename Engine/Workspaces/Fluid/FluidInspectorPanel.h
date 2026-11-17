/*============================================================================================================================================
                                                          FLUIDINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Right panel for Fluid Studio: Project Zero Inspector (Header card, Source / Simulation / Rendering tabs, property cards & pill sliders)

#pragma once

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

#include <array>

namespace Frontier::Workspaces
{

class FluidInspectorPanel
{
public:
    static constexpr float RecommendedWidthPx = 356.0f;

    void Render(
        PixelSpace&            Surface,
        Rectangle2D            Bounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer,
        float                  Opacity = 1.0f) noexcept;

    [[nodiscard]] Rectangle2D QueryTabBounds(FluidInspectorTab Tab) const noexcept;
    [[nodiscard]] Rectangle2D QueryBlackbodyStripBounds() const noexcept { return LastBlackbodyStripRect; }

private:
    bool DrawSliderRow(
        PixelSpace&           Surface,
        Rectangle2D           RowRect,
        const char*           Label,
        const char*           UnitLabel,
        float&                InOutValue,
        float                 MinVal,
        float                 MaxVal,
        const ControlPointer& Pointer,
        uint32_t              SliderSlot,
        const char*           Format = "%.2f") noexcept;

    bool DrawToggleRow(
        PixelSpace&           Surface,
        Rectangle2D           RowRect,
        const char*           Label,
        bool&                 InOutChecked,
        const ControlPointer& Pointer) noexcept;

    int32_t                    ActiveDragSlider       = -1;
    std::array<Rectangle2D, 3> LastTabRects           {};
    Rectangle2D                LastBlackbodyStripRect {};
};

} // namespace Frontier::Workspaces
