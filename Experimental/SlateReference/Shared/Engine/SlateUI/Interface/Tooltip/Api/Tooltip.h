//============================================================================================================================================
//                                                             TOOLTIP.H
//============================================================================================================================================
// 🧩 Shared deferred-tooltip declaration, independent of the panel that supplies its anchor.

#pragma once

#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include <cstdint>

namespace Slate
{

enum class TooltipAppearance : std::uint32_t
{
    Light           = 0u,
    Dark            = 1u,
    AppearanceCount = 2u
};

struct TooltipDeclaration
{
    const char*        Title      = "";                            // [-] - borrowed heading
    const char*        Body       = "";                            // [-] - borrowed, wrapped at record time
    SymbolSubject      Figure     = SymbolSubject::BulbFilament;   // [-] - trigger figure
    TooltipAppearance  Appearance = TooltipAppearance::Light;
};

}   // namespace Slate
