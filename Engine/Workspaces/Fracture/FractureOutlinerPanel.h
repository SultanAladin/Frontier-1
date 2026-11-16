/*============================================================================================================================================
                                                          FRACTUREOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Floating 22px left Fracture Outliner panel: solid selector, 6 presets, search filter, and fragment shard visibility/selection tree

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fracture/FractureWorkspaceSession.h"

namespace Frontier::Workspaces::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FractureOutlinerPanel
{
public:
    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        FractureWorkspaceSession& Session,
        const ControlPointer&     Pointer,
        float                     WheelDeltaY = 0.0f,
        float                     Opacity     = 1.0f) noexcept;

private:
    float ScrollOffsetPx = 0.0f;
};

} // namespace Frontier::Workspaces::Fracture
