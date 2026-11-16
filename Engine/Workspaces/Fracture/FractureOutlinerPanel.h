/*============================================================================================================================================
                                                          FRACTUREOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Left Fracture Studio Outliner panel: Census tiles (Released/Held), search bar, filter chips & Primitives/Recipe/Fragments tree

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
        float                     WheelDeltaY = 0.0f) noexcept;
};

} // namespace Frontier::Workspaces::Fracture
