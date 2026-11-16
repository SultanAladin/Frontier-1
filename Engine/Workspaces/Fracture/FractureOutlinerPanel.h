/*============================================================================================================================================
                                                          FRACTUREOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Project Zero Fracture Outliner: Released/Held census cards, Search/Filter bar, 9 analytical primitives, Recipe, Fragments & 4-slot footer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/Controls/ControlAppearance.h"
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
