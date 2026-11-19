/*============================================================================================================================================
                                                           FLUIDOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Left panel for Fluid Studio: Project Zero Outliner (census tiles, filters, scene tree) + integrated Preset Library

#pragma once

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

#include <array>

namespace Frontier::Workspaces
{

class FluidOutlinerPanel
{
public:
    static constexpr float RecommendedWidthPx = 304.0f;

    void Render(
        PixelSpace&            Surface,
        Rectangle2D            Bounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer,
        float                  Opacity = 1.0f) noexcept;

    [[nodiscard]] Rectangle2D QueryLastNodeRowBounds(FluidOutlinerRow Node) const noexcept;
    [[nodiscard]] Rectangle2D QueryLastPresetCardBounds(std::size_t Index) const noexcept;

private:
    std::array<Rectangle2D, 7>  LastNodeRowRects   {};
    std::array<Rectangle2D, 12> LastPresetCardRects {};
};

} // namespace Frontier::Workspaces
