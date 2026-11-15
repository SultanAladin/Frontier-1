/*==========================================================================================================================================
                                                    MATERIALINSPECTORPANE.H
==========================================================================================================================================*/
// 🧩 Project Zero floating right inspector: Live 256-byte DynamicParameterBlock sliders, ParameterDriverMode & 10-Lobe OpenPBR readouts

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"

namespace Frontier::Workspaces
{

class MaterialInspectorPane
{
public:
    static constexpr float RecommendedWidthPx = 326.0f;

    void Render(
        PixelSpace&               Surface,
        Rectangle2D               Bounds,
        MaterialWorkspaceSession& Session,
        const ControlPointer&     Pointer) noexcept;

    [[nodiscard]] Rectangle2D QueryBakeButtonBounds() const noexcept { return LastBakeButtonRect; }

private:
    Rectangle2D LastBakeButtonRect {};
};

} // namespace Frontier::Workspaces
