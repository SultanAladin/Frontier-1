/*============================================================================================================================================
                                                           COLOURRAMPEDITOR.H
============================================================================================================================================*/
// 🎨 Reusable multi-stop ColourRamp / LinearFill / SolidPigment / QuantizedRamp / BlackbodyThermal UI editor with ContentBrowser drop-slot support

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourSourceTypes.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstddef>
#include <functional>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourRampEditorSession
{
    std::size_t SelectedStopIndex = 0u;    // [idx] - Currently active stop index
    bool        DraggingStop      = false; // [-]   - True while horizontally dragging SelectedStopIndex
    Rectangle2D LastStripBounds   {};      // [px]  - Last rendered gradient strip bounds (drop target for ContentBrowser)
    bool        Modified          = false; // [-]   - True during the frame when user altered stops or parameters
};

struct ColourRampEditorLayout
{
    float RecommendedHeight = 166.0f; // [px] - Compact total logical height for full ramp editor widget
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace ColourRampEditor
{

void ConstructGradientBar(
    PixelSpace&                              Surface,
    Rectangle2D                              StripBounds,
    const ColourSpace::ColourSourceProfile&  Profile,
    float                                    CornerRadius = 6.0f,
    bool                                     HighlightDropHalo = false) noexcept;

[[nodiscard]] bool Construct(
    PixelSpace&                       Surface,
    Rectangle2D                       Bounds,
    std::string_view                  HeadingTitle,
    ColourSpace::ColourSourceProfile& Profile,
    ColourRampEditorSession&          Session,
    const ControlPointer&             Pointer,
    bool                              HighlightDropTarget = false,
    float                             Opacity             = 1.0f);

void RegisterDropSlot(
    ContentBrowserPanel&                                                                    Browser,
    std::string_view                                                                        SlotIdentifier,
    std::string_view                                                                        DisplayTitle,
    const ColourRampEditorSession&                                                          Session,
    ColourSpace::ColourSourceProfile*                                                       TargetProfile,
    std::function<void(const ColourSpace::ColourSourceProfile&, std::string_view)>          NotifyAssigned = {});

} // namespace ColourRampEditor

} // namespace Frontier
