/*============================================================================================================================================
                                                        DROPDOWNSELECTOR.H
============================================================================================================================================*/
// 🧩 Reusable rounded dropdown selector button and overlay option sheet

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlStyle.h"

namespace Frontier
{

namespace DropdownSelector
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ControlContact DrawButton(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    const char*           CurrentLabel,
    bool                  Open,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

[[nodiscard]] Rectangle2D QuerySheetBounds(Rectangle2D ButtonBounds, uint32_t OptionCount) noexcept;

[[nodiscard]] ControlContact DrawOptionSheet(
    PixelSpace&           Surface,
    Rectangle2D           ButtonBounds,
    const char* const*    Options,
    uint32_t              OptionCount,
    uint32_t              SelectedIndex,
    const ControlPointer& Pointer,
    uint32_t&             ChosenIndex,
    float                 Opacity = 1.0f) noexcept;

} // namespace DropdownSelector

} // namespace Frontier
