/*============================================================================================================================================
                                                               CONTROLTOOLTIP.H
============================================================================================================================================*/
// 🧩 Directional, light/dark, and split-header diagnostic tooltips with icons, metric tokens, and suggestion buttons

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>
#include <span>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class TooltipSurfaceMode : uint8_t
{
    ThemeAutomatic = 0, // [-] - Follow active ThemeRepository dark/light luminance
    DarkSurface    = 1, // [-] - Force deep obsidian dark tooltip surface
    LightSurface   = 2, // [-] - Force crisp white light tooltip surface
};

enum class TooltipTailProfile : uint8_t
{
    FilletedCurve  = 0, // [-] - Smooth C1 concave-shoulder curved speech pointer
    RoundedPointer = 1, // [-] - Sleek tapered pointer with rounded apex
};

enum class TooltipTailAlignment : uint8_t
{
    TargetAligned = 0, // [-] - Aim pointer tail directly toward AnchorBounds center
    Leading       = 1, // [-] - Offset pointer tail near leading corner (22% along edge)
    Centered      = 2, // [-] - Center pointer tail at midpoint of active edge
    Trailing      = 3, // [-] - Offset pointer tail near trailing corner (78% along edge)
};

enum class TooltipLayoutVariant : uint8_t
{
    Standard    = 0, // [-] - Single unified surface with optional title, body, and buttons
    SplitHeader = 1, // [-] - Two-band header + divider + diagnostic body, metrics, and buttons
};

enum class TooltipSuggestionTone : uint8_t
{
    Neutral         = 0, // [-] - Neutral raised action button (e.g. "Learn More", "Reimport")
    SemanticPrimary = 1, // [-] - Highlighted semantic/primary repair button (e.g. "Fix")
    SubtleDismiss   = 2, // [-] - Recessed muted dismissal button (e.g. "Ignore")
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct TooltipMetricToken
{
    const char* Caption       = "";                     // [-] - Compact metric or swatch label (e.g. "26px", "14 facets")
    ControlIcon Icon          = ControlIcon::Count;     // [-] - Optional vector icon inside circular badge
    ColorRgba   SwatchColor   = { 0u, 0u, 0u, 0u };     // [-] - Optional solid color circle token when SwatchEnabled
    bool        SwatchEnabled = false;                  // [-] - Render solid SwatchColor disc instead of vector icon
};

struct TooltipSuggestion
{
    const char*           Label        = "";                             // [-] - Button caption ("Fix", "Reimport", "Ignore", "Learn More")
    ControlIcon           LeadingIcon  = ControlIcon::Count;             // [-] - Optional leading vector icon
    ControlIcon           TrailingIcon = ControlIcon::Count;             // [-] - Optional trailing vector icon (e.g. ExternalOpen)
    TooltipSuggestionTone Tone         = TooltipSuggestionTone::Neutral; // [-] - Visual button prominence
};

struct TooltipDescriptor
{
    const char*                          Title         = "";                                  // [-]  - Optional heading text
    const char*                          HeaderTag     = "";                                  // [-]  - Optional secondary header tag after bullet
    const char*                          BodyText      = "";                                  // [-]  - Wrapped body copy
    TooltipDirection                     Direction     = TooltipDirection::Automatic;         // [-]  - Placement side relative to AnchorBounds
    TooltipTailProfile                   TailProfile   = TooltipTailProfile::FilletedCurve;   // [-]  - Pointer tail silhouette
    TooltipTailAlignment                 TailAlignment = TooltipTailAlignment::TargetAligned; // [-]  - Pointer tail alignment along active edge
    TooltipSurfaceMode                   SurfaceMode   = TooltipSurfaceMode::ThemeAutomatic;  // [-]  - Dark/light surface override
    TooltipLayoutVariant                 Layout        = TooltipLayoutVariant::Standard;      // [-]  - Single-band vs SplitHeader diagnostic layout
    TooltipSeverity                      Severity      = TooltipSeverity::Neutral;            // [-]  - Semantic severity tone & icon default
    ControlIcon                          LeadingIcon   = ControlIcon::Count;                  // [-]  - Custom header/leading icon override
    float                                PreferredWidth = 288.0f;                             // [px] - Target tooltip body width before clamping
    float                                CornerRadius   = 20.0f;                              // [px] - Rounded rectangle corner radius
    float                                AnchorGap      = 12.0f;                              // [px] - Clearance from AnchorBounds to pointer tip
    std::span<const TooltipMetricToken>  MetricTokens   {};                                   // [-]  - Optional compact icon/swatch metric row
    std::span<const TooltipSuggestion>   Suggestions    {};                                   // [-]  - Optional interactive suggestion buttons
};

struct TooltipInteractionResult
{
    Rectangle2D      BodyBounds             {};                          // [px]  - Tooltip rounded body bounds (excluding tail)
    Rectangle2D      EnvelopeBounds         {};                          // [px]  - Bounding box enclosing body + pointer tail
    TooltipDirection ResolvedDirection      = TooltipDirection::Above;   // [-]   - Concrete side selected after viewport clamp
    int32_t          HoveredSuggestionIndex = -1;                        // [idx] - Hovered suggestion button (-1 = none)
    int32_t          PressedSuggestionIndex = -1;                        // [idx] - Pressed suggestion button this step (-1 = none)
    int32_t          ClickedSuggestionIndex = -1;                        // [idx] - Released/activated suggestion button (-1 = none)
    bool             BodyHovered            = false;                     // [-]   - True when pointer lies inside tooltip body
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace ControlTooltip
{

[[nodiscard]] ControlIcon ResolveSeverityIcon(TooltipSeverity Severity, ControlIcon ExplicitIcon = ControlIcon::Count) noexcept;
[[nodiscard]] ColorQuad   ResolveSeverityAccent(TooltipSeverity Severity) noexcept;

[[nodiscard]] Point2D MeasureBodyExtent(
    PixelSpace&              Surface,
    const TooltipDescriptor& Descriptor) noexcept;

[[nodiscard]] TooltipDirection ResolvePlacementDirection(
    Rectangle2D      AnchorBounds,
    Rectangle2D      ViewportBounds,
    Point2D          BodyExtent,
    TooltipDirection Requested,
    float            TailLength = 10.5f,
    float            AnchorGap  = 12.0f) noexcept;

[[nodiscard]] TooltipInteractionResult ConstructAnchored(
    PixelSpace&              Surface,
    Rectangle2D              AnchorBounds,
    Rectangle2D              ViewportBounds,
    const TooltipDescriptor& Descriptor,
    const ControlPointer&    Pointer,
    float                    Opacity = 1.0f) noexcept;

[[nodiscard]] TooltipInteractionResult ConstructAtBodyBounds(
    PixelSpace&              Surface,
    Rectangle2D              BodyBounds,
    Point2D                  TargetPoint,
    TooltipDirection         Direction,
    const TooltipDescriptor& Descriptor,
    const ControlPointer&    Pointer,
    float                    Opacity = 1.0f) noexcept;

bool PresentHoveredControlTooltip(
    PixelSpace&           Surface,
    Rectangle2D           ViewportBounds,
    const ControlPointer& Pointer,
    float                 Opacity = 1.0f) noexcept;

} // namespace ControlTooltip

} // namespace Frontier
