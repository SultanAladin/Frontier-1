/*============================================================================================================================================
                                                           FLUIDOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Floating 22px left Fluid Outliner panel listing Hero/World grids, Emitter, Obstacle, ForceFields, Embers & GI Lighting

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

namespace Frontier::Workspaces
{

class FluidOutlinerPanel
{
public:
    static constexpr float RecommendedWidthPx = 248.0f;

    void Render(
        PixelSpace&            Surface,
        Rectangle2D            Bounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer,
        float                  Opacity = 1.0f) noexcept;
};

} // namespace Frontier::Workspaces
