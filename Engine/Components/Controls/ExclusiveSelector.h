/*============================================================================================================================================
                                                             EXCLUSIVESELECTOR.H
============================================================================================================================================*/
// 🧩 Reusable rounded toggle switches (standard 46x26 bordered and compact 48x24 borderless)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

namespace Frontier
{

namespace ExclusiveSelector
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ControlContact Construct(
    PixelSpace&            Surface,
    float                  X,
    float                  Y,
    bool                   Active,
    const ControlPointer&  Pointer,
    float                  Opacity = 1.0f,
    const OptionalTooltip& Tooltip = {}) noexcept;

[[nodiscard]] ControlContact ConstructCompact(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    bool                   Active,
    const ControlPointer&  Pointer,
    float                  Opacity = 1.0f,
    const OptionalTooltip& Tooltip = {}) noexcept;

} // namespace ExclusiveSelector

} // namespace Frontier
