/*============================================================================================================================================
                                                              DROPDOWNSELECTOR.H
============================================================================================================================================*/
// 🧩 Reusable Studio Controls rounded dropdown selector button, option sheet, and directive sheet

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <span>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DropdownDirectiveEntry
{
    const char* Label          = "";                    // [-] - Primary row caption
    const char* ShortcutHint   = "";                    // [-] - Optional trailing shortcut hint
    ColorRgba   LeadingTint    = { 0u, 0u, 0u, 0u };    // [-] - Optional leading circular color token (Alpha > 0)
    ControlIcon LeadingIcon    = ControlIcon::Count;    // [-] - Optional leading vector icon
    bool        Selected       = false;                 // [-] - Highlighted / checked selection indicator
    bool        Destructive    = false;                 // [-] - Red-tinted destructive directive styling
    bool        DividerAbove   = false;                 // [-] - Construct horizontal rule above this row
    bool        SectionCaption = false;                 // [-] - Non-interactive uppercase group header row
};

namespace DropdownSelector
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ControlContact ConstructButton(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    const char*            CurrentLabel,
    bool                   Open,
    const ControlPointer&  Pointer,
    float                  Opacity     = 1.0f,
    ControlIcon            LeadingIcon = ControlIcon::Count,
    const OptionalTooltip& Tooltip     = {}) noexcept;

[[nodiscard]] Rectangle2D QuerySheetBounds(Rectangle2D ButtonBounds, uint32_t OptionCount) noexcept;

[[nodiscard]] ControlContact ConstructOptionSheet(
    PixelSpace&           Surface,
    Rectangle2D           ButtonBounds,
    const char* const*    Options,
    uint32_t              OptionCount,
    uint32_t              SelectedIndex,
    const ControlPointer& Pointer,
    uint32_t&             ChosenIndex,
    float                 Opacity = 1.0f) noexcept;

[[nodiscard]] Rectangle2D QueryDirectiveSheetBounds(
    Point2D                                  AnchorTopLeft,
    float                                    SheetWidth,
    std::span<const DropdownDirectiveEntry>  Entries) noexcept;

[[nodiscard]] ControlContact ConstructDirectiveSheet(
    PixelSpace&                              Surface,
    Rectangle2D                              SheetBounds,
    std::span<const DropdownDirectiveEntry>  Entries,
    const ControlPointer&                    Pointer,
    int32_t&                                 ChosenIndex,
    float                                    Opacity = 1.0f) noexcept;

} // namespace DropdownSelector

} // namespace Frontier
