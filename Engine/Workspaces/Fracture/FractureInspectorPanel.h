/*============================================================================================================================================
                                                         FRACTUREINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Floating 22px right Fracture Inspector panel: Material SI properties, 6-Solver pill sliders, SDF budget & Conservation Receipt

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

class FractureInspectorPanel
{
public:
    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        FractureWorkspaceSession& Session,
        const ControlPointer&     Pointer,
        float                     Opacity = 1.0f) noexcept;

private:
    bool DraggingEnergy    = false;
    bool DraggingCeiling   = false;
    bool DraggingMinSize   = false;
    bool DraggingBondScale = false;
    bool DraggingExplode   = false;
    bool DraggingGrain     = false;
};

} // namespace Frontier::Workspaces::Fracture
