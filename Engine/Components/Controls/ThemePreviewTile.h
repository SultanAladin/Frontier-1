/*============================================================================================================================================
                                                        THEMEPREVIEWTILE.H
============================================================================================================================================*/
// 🧩 Reusable 4:3 miniature OS window theme preview tile and circular accent/semantic color swatch controls

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlStyle.h"

namespace Frontier
{

namespace ThemePreviewTile
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ThemePreset PresetAtDisplayOrder(uint32_t DisplayOrdinal) noexcept;
[[nodiscard]] const char* LabelAtDisplayOrder(uint32_t DisplayOrdinal) noexcept;

[[nodiscard]] ControlContact Draw(
    PixelSpace&           Surface,
    Rectangle2D           TileBounds,
    ThemePreset           Preset,
    bool                  Active,
    float                 CornerRadius,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

[[nodiscard]] ControlContact DrawColorSwatch(
    PixelSpace&           Surface,
    float                 CenterX,
    float                 CenterY,
    ColorQuad             SwatchColor,
    bool                  Selected,
    bool                  OuterRingStyle,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

} // namespace ThemePreviewTile

} // namespace Frontier
