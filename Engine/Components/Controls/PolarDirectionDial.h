/*============================================================================================================================================
                                                          POLARDIRECTIONDIAL.H
============================================================================================================================================*/
// 🧩 Generic reusable interactive azimuth & elevation polar compass dial for celestial, wind & directional controls

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/PixelSpace.h"

namespace Frontier::PolarDirectionDial
{

[[nodiscard]] const char* ResolveCompassLabel(float AzimuthDegrees) noexcept;

void Construct(
    PixelSpace&             Surface,
    Point2D                 Center,
    float                   Radius,
    float                   AzimuthDegrees,
    float                   ElevationDegrees,
    bool                    ShowElevation,
    const PaneInputReading& Input,
    float&                  OutAzimuthDegrees,
    float&                  OutElevationDegrees,
    bool&                   OutChanged,
    bool                    Interactive = true) noexcept;

} // namespace Frontier::PolarDirectionDial
