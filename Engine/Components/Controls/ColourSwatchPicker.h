/*============================================================================================================================================
                                                             COLOURSWATCHPICKER.H
============================================================================================================================================*/
// 🧩 Reusable Linear/sRGB/HSL/Kelvin colour swatch picker with WCAG contrast readout & ContentBrowser .ColourSwatch drop-slot support

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"

#include <functional>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourSwatchPickerSession
{
    float       CachedHueDegrees  = 215.0f; // [deg] - Preserved hue angle even at zero saturation/lightness
    bool        DraggingField     = false;  // [-]   - True while dragging inside 2D saturation/lightness pad
    bool        DraggingHue       = false;  // [-]   - True while dragging horizontal hue strip
    bool        DraggingAlpha     = false;  // [-]   - True while dragging horizontal opacity strip
    Rectangle2D LastPreviewBounds {};       // [px]  - Last rendered preview tile rectangle (ContentBrowser drop slot)
    bool        Modified          = false;  // [-]   - True during the frame when user altered the swatch colour
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace ColourSwatchPicker
{

[[nodiscard]] bool Construct(
    PixelSpace&                 Surface,
    Rectangle2D                 Bounds,
    std::string_view            HeadingTitle,
    ColourSpace::LinearRgba&    TargetLinearTint,
    ColourSwatchPickerSession&  Session,
    const ControlPointer&       Pointer,
    bool                        ShowKelvinStrip = true,
    float                       Opacity         = 1.0f);

void RegisterDropSlot(
    ContentBrowserPanel&                            Browser,
    std::string_view                                SlotIdentifier,
    std::string_view                                DisplayTitle,
    const ColourSwatchPickerSession&                Session,
    ColourSpace::LinearRgba*                        TargetLinearTint,
    std::function<void(ColourSpace::LinearRgba)>    NotifyAssigned = {});

} // namespace ColourSwatchPicker

} // namespace Frontier
