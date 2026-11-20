/*============================================================================================================================================
                                                                SCALARSLIDER.H
============================================================================================================================================*/
// 🧩 Reusable scalar slider and split readout capsule ("[ 100 | % ]" / "[ 16 | px ]")

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

namespace Frontier
{

namespace ScalarSlider
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ControlContact Construct(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    float                  Minimum,
    float                  Maximum,
    float                  CurrentPosition,
    bool                   DraggingActive,
    const ControlPointer&  Pointer,
    float&                 UpdatedPosition,
    float                  Opacity      = 1.0f,
    float                  RailHeight   = ControlMetrics::SliderHeight,
    float                  KnobDiameter = ControlMetrics::SliderThumbDiameter,
    const OptionalTooltip& Tooltip      = {}) noexcept;

void ConstructReadoutCapsule(
    PixelSpace& Surface,
    float       X,
    float       Y,
    const char* NumberText,
    const char* UnitText,
    float       Opacity       = 1.0f,
    float       TotalWidth    = ControlMetrics::ReadoutCapsuleWidth,
    float       UnitCellWidth = ControlMetrics::ReadoutUnitWidth) noexcept;

[[nodiscard]] ControlContact ConstructWithReadout(
    PixelSpace&            Surface,
    Rectangle2D            RowBounds,
    const char*            NumberText,
    const char*            UnitText,
    float                  Minimum,
    float                  Maximum,
    float                  CurrentPosition,
    bool                   DraggingActive,
    const ControlPointer&  Pointer,
    float&                 UpdatedPosition,
    float                  Opacity = 1.0f,
    const OptionalTooltip& Tooltip = {}) noexcept;

} // namespace ScalarSlider

} // namespace Frontier
