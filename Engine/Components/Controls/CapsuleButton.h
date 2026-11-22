/*============================================================================================================================================
                                                               CAPSULEBUTTON.H
============================================================================================================================================*/
// 🧩 Reusable capsule buttons, weight/scope chips, bordered circular icon buttons, segmented bars, and quick discs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <span>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ButtonTone : uint8_t
{
    Primary   = 0, // [-] - High-contrast primary confirmation button
    Secondary = 1, // [-] - Neutral secondary button
    Ghost     = 2, // [-] - Borderless transparent button
    Danger    = 3, // [-] - Destructive red-tinted button
    Tinted    = 4, // [-] - Custom color-tinted button
};

enum class ViewportNavigationDirective : uint8_t
{
    None    = 0, // [-] - No navigation button triggered
    ZoomIn  = 1, // [-] - Zoom viewport camera or planar canvas inward
    ZoomOut = 2, // [-] - Zoom viewport camera or planar canvas outward
    FitView = 3, // [-] - Enclose active selection or complete scene bounds
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ButtonDescriptor
{
    const char*     Label    = "";                    // [-]  - Button caption text
    ButtonTone      Tone     = ButtonTone::Secondary; // [-]  - Visual tone style
    float           FontSize = 13.5f;                 // [px] - Caption font size
    float           PaddingX = 20.0f;                 // [px] - Horizontal text padding
    bool            Disabled = false;                 // [-]  - Disabled interaction toggle
    ColorQuad       Tint     = {};                    // [-]  - Custom fill color when Tone == ButtonTone::Tinted
    OptionalTooltip Tooltip  = {};                    // [-]  - Optional hover tooltip descriptor
};

struct ViewportNavigationCapsuleSpec
{
    const char* ZoomInTooltip  = "Zoom in.";                      // [-]  - Tooltip shown over '+' button
    const char* ZoomOutTooltip = "Zoom out.";                     // [-]  - Tooltip shown over '-' button
    const char* FitViewTooltip = "Fit active selection or view."; // [-]  - Tooltip shown over Fit icon button
    float       MarginRight    = 12.0f;                           // [px] - Inset from SubViewport right edge
    float       MarginBottom   = 12.0f;                           // [px] - Inset from SubViewport bottom edge
};

struct CapsuleReadoutSegment
{
    const char* Text    = "";                        // [-]  - Formatted telemetry label text
    ColorRgba   Ink     { 220u, 220u, 220u, 255u };  // [-]  - Segment ink colour
    float       OffsetX = -1.0f;                     // [px] - Explicit left offset inside capsule (<0 = auto layout)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace CapsuleButton
{

[[nodiscard]] float          MeasureWidth(PixelSpace& Surface, const ButtonDescriptor& Button) noexcept;
[[nodiscard]] ControlContact Construct(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const ButtonDescriptor& Button,
    const ControlPointer&   Pointer,
    float                   Opacity = 1.0f) noexcept;

[[nodiscard]] float          MeasureChipWidth(PixelSpace& Surface, const char* Label) noexcept;
[[nodiscard]] ControlContact ConstructChip(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    const char*           Label,
    bool                  Active,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

[[nodiscard]] ControlContact ConstructRoundIcon(
    PixelSpace&            Surface,
    float                  CenterX,
    float                  CenterY,
    ControlIcon            Icon,
    const ControlPointer&  Pointer,
    float                  Opacity = 1.0f,
    const OptionalTooltip& Tooltip = {}) noexcept;

[[nodiscard]] ControlContact ConstructSegmented(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    const char* const*    Labels,
    uint32_t              Count,
    uint32_t              ActiveIndex,
    const ControlPointer& Pointer,
    uint32_t&             SelectedIndex,
    float                 Opacity = 1.0f) noexcept;

[[nodiscard]] ControlContact ConstructQuickCircleTile(
    PixelSpace&           Surface,
    Rectangle2D           DiscBounds,
    ControlIcon           Icon,
    const char*           Caption,
    bool                  Active,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

[[nodiscard]] Rectangle2D QueryViewportNavigationCapsuleBounds(
    Rectangle2D ViewportRect,
    float       MarginRight  = 12.0f,
    float       MarginBottom = 12.0f) noexcept;

[[nodiscard]] ViewportNavigationDirective ConstructViewportNavigationCapsule(
    PixelSpace&                        Surface,
    Rectangle2D                        ViewportRect,
    const ControlPointer&              Pointer,
    const ViewportNavigationCapsuleSpec& Spec    = {},
    float                              Opacity = 1.0f) noexcept;

void ConstructTelemetryCapsule(
    PixelSpace&                            Surface,
    Rectangle2D                            Bounds,
    std::span<const CapsuleReadoutSegment> Segments,
    float                                  FontPx  = 9.0f,
    float                                  Opacity = 1.0f) noexcept;

} // namespace CapsuleButton

} // namespace Frontier
