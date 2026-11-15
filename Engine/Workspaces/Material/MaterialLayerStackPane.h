/*==========================================================================================================================================
                                                   MATERIALLAYERSTACKPANE.H
==========================================================================================================================================*/
// 🧩 Project Zero floating left pane: Modular Material Repository & Bottom-to-Top MaterialLayerStack editor (Zero node-graph clutter)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"

#include <vector>

namespace Frontier::Workspaces
{

class MaterialLayerStackPane
{
public:
    static constexpr float RecommendedWidthPx = 308.0f;

    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        MaterialWorkspaceSession& Session,
        const ControlPointer&     Pointer) noexcept;

    [[nodiscard]] Rectangle2D QueryPresetRowBounds(std::size_t Index) const noexcept;
    [[nodiscard]] Rectangle2D QueryLayerRowBounds(std::size_t Index) const noexcept;

private:
    std::vector<Rectangle2D> LastPresetRowRects {};
    std::vector<Rectangle2D> LastLayerRowRects  {};
};

} // namespace Frontier::Workspaces
