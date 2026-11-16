/*============================================================================================================================================
                                                         FRACTUREINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Port-8091 Project Zero Inspector: Hero chart, 2x2 KPI cards, Baked vs Realtime cards, 6-Algorithm grid, Shape, Material, Quality & Fragment view

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
