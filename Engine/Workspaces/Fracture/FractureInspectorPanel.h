/*============================================================================================================================================
                                                         FRACTUREINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Right Fracture Studio Inspector panel: Overview (Energy Share + KPI Trio + ScalarSliders), Material, Solver & Receipt

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

class FractureInspectorPanel
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
