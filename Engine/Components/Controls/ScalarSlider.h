/*============================================================================================================================================
                                                          SCALARSLIDER.H
============================================================================================================================================*/
// 🧩 Reusable scalar slider and split readout capsule ("[ 100 | % ]" / "[ 16 | px ]")

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlStyle.h"

namespace Frontier
{

namespace ScalarSlider
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ControlContact Draw(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    float                 Minimum,
    float                 Maximum,
    float                 CurrentPosition,
    bool                  DraggingActive,
    const ControlPointer& Pointer,
    float&                UpdatedPosition,
    float                 Opacity      = 1.0f,
    float                 RailHeight   = ControlMetrics::SliderHeight,
    float                 KnobDiameter = ControlMetrics::SliderThumbDiameter) noexcept;

void DrawReadoutCapsule(
    PixelSpace& Surface,
    float       X,
    float       Y,
    const char* NumberText,
    const char* UnitText,
    float       Opacity       = 1.0f,
    float       TotalWidth    = ControlMetrics::ReadoutCapsuleWidth,
    float       UnitCellWidth = ControlMetrics::ReadoutUnitWidth) noexcept;

[[nodiscard]] ControlContact DrawWithReadout(
    PixelSpace&           Surface,
    Rectangle2D           RowBounds,
    const char*           NumberText,
    const char*           UnitText,
    float                 Minimum,
    float                 Maximum,
    float                 CurrentPosition,
    bool                  DraggingActive,
    const ControlPointer& Pointer,
    float&                UpdatedPosition,
    float                 Opacity = 1.0f) noexcept;

} // namespace ScalarSlider

} // namespace Frontier
