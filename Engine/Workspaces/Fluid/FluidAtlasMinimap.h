/*============================================================================================================================================
                                                            FLUIDATLASMINIMAP.H
============================================================================================================================================*/
// 🧩 Floating 2D Z-Slice / XZ-Slice voxel atlas minimap inspecting live Hero + World Drift Smoke/Temp/Velocity fields

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

namespace Frontier::Workspaces
{

class FluidAtlasMinimap
{
public:
    void RenderOverlay(
        PixelSpace&            Surface,
        Rectangle2D            AnchorBounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer) noexcept;

private:
    float SliceOffset01 = 0.5f;
};

} // namespace Frontier::Workspaces
