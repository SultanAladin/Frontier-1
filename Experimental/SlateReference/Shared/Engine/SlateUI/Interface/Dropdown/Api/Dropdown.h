//============================================================================================================================================
//                                                            DROPDOWN.H
//============================================================================================================================================
// 🧩 Shared dropdown declaration: one field appearance and interaction path, with marked and plain menus.

#pragma once

#include <cstdint>

namespace Slate
{

/// 🧩 Whether an option menu communicates selected state with trailing dots or stays visually plain.
enum class SelectionIndicator : std::uint32_t
{
    Marked = 0u,   // [-] - selection/shading menus: selected and unselected state dots
    Plain  = 1u    // [-] - filter menus: names only
};

/// 🧩 What one dropdown presents. Option captions are borrowed and never copied into the component.
struct SelectionDeclaration
{
    const char*         Caption       = "";        // [-] - the leading label
    const char* const*  Options       = nullptr;   // [-] - borrowed; outlives the tick
    std::uint32_t       OptionCount   = 0u;        // [-] - zero records the field and no menu
    bool                CaptionInside = false;     // [-] - no leading label strip; caption fills the field
    SelectionIndicator  Indicator     = SelectionIndicator::Marked;
};

}   // namespace Slate
