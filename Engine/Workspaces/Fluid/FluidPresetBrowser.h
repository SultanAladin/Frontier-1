/*============================================================================================================================================
                                                           FLUIDPRESETBROWSER.H
============================================================================================================================================*/
// 🧩 Modal 10-preset Pyro/Explosion/Plasma gallery with 6 blackbody palette previews & .gasscene.toml export/import triggers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Fluid/FluidWorkspaceSession.h"

namespace Frontier::Workspaces
{

class FluidPresetBrowser
{
public:
    void RenderPane(
        PixelSpace&            Surface,
        Rectangle2D            PaneBounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer) noexcept;

    void RenderModal(
        PixelSpace&            Surface,
        Rectangle2D            WindowBounds,
        FluidWorkspaceSession& Session,
        const ControlPointer&  Pointer) noexcept;
};

} // namespace Frontier::Workspaces
