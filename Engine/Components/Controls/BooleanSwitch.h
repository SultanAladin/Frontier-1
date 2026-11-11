/*============================================================================================================================================
                                                         BOOLEANSWITCH.H
============================================================================================================================================*/
// 🧩 Reusable rounded toggle switches (standard 46x26 bordered and compact 48x24 borderless)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlStyle.h"

namespace Frontier
{

namespace BooleanSwitch
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ControlContact Draw(
    PixelSpace&           Surface,
    float                 X,
    float                 Y,
    bool                  Active,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

[[nodiscard]] ControlContact DrawCompact(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    bool                  Active,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

} // namespace BooleanSwitch

} // namespace Frontier
