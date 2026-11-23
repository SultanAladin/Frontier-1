/*============================================================================================================================================
                                                         COMPONENTDETAILSPANEL.H
============================================================================================================================================*/
// 🧩 Right-dock Details inspector: breadcrumb, inline rename, CLASS DEFAULTS, COMPILER RESULTS, TRANSFORM, CONSTRAINT & typed properties

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Controls/Vector3CapsuleInput.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.h"

#include <imgui.h>

namespace Frontier::Workspaces::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentDetailsPanel
{
public:
    void Render(
        PixelSpace&                Surface,
        Rectangle2D                Bounds,
        ComponentWorkspaceSession& Session,
        const PaneInputReading&    Input,
        double                     DeltaSeconds) noexcept;

private:
    float                  ScrollOffsetPx = 0.0f;
    uint32_t               DraggingSlider = 0u;
    Vector3CapsuleSession  LocSession   {};
    Vector3CapsuleSession  RotSession   {};
    Vector3CapsuleSession  ScaleSession {};
    ImTextureData       ClassThumbTexture {};
};

} // namespace Frontier::Workspaces::ComponentEditor
