/*============================================================================================================================================
                                                             CONTROLAPPEARANCE.H
============================================================================================================================================*/
// 🧩 Shared control color set, layout metrics, pointer contact primitives, and section containers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

struct ControlMetrics
{
    static constexpr float ControlHeight        = 40.0f; // [px]  - Standard interactive control row height
    static constexpr float LabelWidth           = 110.0f;// [px]  - Leading row label column width
    static constexpr float RowGap               = 14.0f; // [px]  - Vertical spacing between control rows
    static constexpr float SectionPadding       = 24.0f; // [px]  - Interior padding inside section containers
    static constexpr float SectionGap           = 24.0f; // [px]  - Vertical spacing between section containers
    static constexpr float SliderHeight         = 28.0f; // [px]  - Capsule slider track height
    static constexpr float SliderThumbDiameter  = 26.0f; // [px]  - Circular slider knob diameter
    static constexpr float ReadoutCapsuleWidth  = 118.0f;// [px]  - Split numeric readout capsule width
    static constexpr float ReadoutUnitWidth     = 44.0f; // [px]  - Right-hand unit cell width inside readout capsule
    static constexpr float SwitchWidth          = 46.0f; // [px]  - Standard boolean switch width
    static constexpr float SwitchHeight         = 26.0f; // [px]  - Standard boolean switch height
    static constexpr float ChipHeight           = 28.0f; // [px]  - Compact weight/scope chip height
    static constexpr float RoundIconDiameter    = 32.0f; // [px]  - Circular icon button diameter
    static constexpr float SwatchDiameter       = 32.0f; // [px]  - Theme/semantic color swatch diameter
    static constexpr float DropdownPaddingX     = 6.0f;  // [px]  - Horizontal inset inside dropdown option sheet
    static constexpr float DropdownPaddingY     = 6.0f;  // [px]  - Vertical inset inside dropdown option sheet
    static constexpr float DropdownOptionHeight = 36.0f; // [px]  - Single dropdown option row height
    static constexpr float DropdownRowGap       = 2.0f;  // [px]  - Vertical gap between dropdown rows
    static constexpr float DropdownCornerRadius = 14.0f; // [px]  - Dropdown option sheet corner radius
    static constexpr float DropdownRowRadius    = 10.0f; // [px]  - Highlighted dropdown row corner radius
    static constexpr float DisabledAlpha        = 0.40f; // [0-1] - Opacity multiplier for disabled controls
    static constexpr float CaretBlinkSeconds    = 1.06f; // [s]   - Text caret blink period
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class TooltipDirection : uint8_t
{
    Automatic = 0, // [-] - Automatically select non-clipping side around anchor bounds
    Above     = 1, // [-] - Place tooltip above anchor (pointer tail on bottom edge)
    Below     = 2, // [-] - Place tooltip below anchor (pointer tail on top edge)
    Left      = 3, // [-] - Place tooltip left of anchor (pointer tail on right edge)
    Right     = 4, // [-] - Place tooltip right of anchor (pointer tail on left edge)
};

enum class TooltipSeverity : uint8_t
{
    Neutral       = 0, // [-] - Standard neutral tooltip surface
    Informational = 1, // [-] - Informational blue accent & notice icon
    Caution       = 2, // [-] - Caution yellow-amber accent & octagon alert icon
    Warning       = 3, // [-] - Warning orange-amber accent & triangle alert icon
    Error         = 4, // [-] - Error crimson accent & critical alert icon
    Success       = 5, // [-] - Verified emerald accent & check circle icon
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OptionalTooltip
{
    const char*      Title       = "";                          // [-] - Optional bold tooltip heading
    const char*      Text        = "";                          // [-] - Primary tooltip body description
    const char*      HeaderTag   = "";                          // [-] - Optional secondary header tag (SplitHeader mode)
    TooltipDirection Direction   = TooltipDirection::Automatic; // [-] - Preferred placement side relative to control
    TooltipSeverity  Severity    = TooltipSeverity::Neutral;    // [-] - Semantic severity tone
    ControlIcon      LeadingIcon = ControlIcon::Count;          // [-] - Optional leading vector icon
    bool             Enabled     = false;                       // [-] - Tooltip activation toggle

    [[nodiscard]] static constexpr OptionalTooltip FromText(
        const char*      BodyText,
        TooltipDirection Placement = TooltipDirection::Automatic) noexcept
    {
        OptionalTooltip Result{};
        Result.Text      = BodyText != nullptr ? BodyText : "";
        Result.Direction = Placement;
        Result.Enabled   = (BodyText != nullptr && BodyText[0] != '\0');
        return Result;
    }

    [[nodiscard]] static constexpr OptionalTooltip FromTitled(
        const char*      HeadingText,
        const char*      BodyText,
        TooltipDirection Placement = TooltipDirection::Automatic,
        TooltipSeverity  Tone      = TooltipSeverity::Neutral,
        ControlIcon      Icon      = ControlIcon::Count) noexcept
    {
        OptionalTooltip Result{};
        Result.Title       = HeadingText != nullptr ? HeadingText : "";
        Result.Text        = BodyText != nullptr ? BodyText : "";
        Result.Direction   = Placement;
        Result.Severity    = Tone;
        Result.LeadingIcon = Icon;
        Result.Enabled     = (Result.Title[0] != '\0') || (Result.Text[0] != '\0');
        return Result;
    }
};

struct ControlPointer
{
    float X        = -1.0e9f; // [px] - Logical cursor horizontal coordinate
    float Y        = -1.0e9f; // [px] - Logical cursor vertical coordinate
    bool  Down     = false;   // [-]  - Primary mouse button held
    bool  Pressed  = false;   // [-]  - Primary mouse button pressed this step
    bool  Released = false;   // [-]  - Primary mouse button released this step
    bool  Enabled  = true;    // [-]  - Pointer hit-testing enabled
};

struct ControlContact
{
    bool Hovered  = false; // [-] - Pointer inside control bounds
    bool Pressed  = false; // [-] - Pointer pressed on control this step
    bool Clicked  = false; // [-] - Pointer released on control this step
    bool Dragging = false; // [-] - Continuous drag active on control
};

struct ControlColorSet
{
    ColorQuad TabBackground  = { 0.0f, 0.0f, 0.0f, 1.0f };         // [-] - Top pull-down notch fill
    ColorQuad Panel          = { 0.039f, 0.039f, 0.039f, 1.0f };   // [-] - Sheet surface fill
    ColorQuad Inset          = { 1.0f, 1.0f, 1.0f, 0.05f };        // [-] - Subtle recessed control fill
    ColorQuad Field          = { 0.0f, 0.0f, 0.0f, 1.0f };         // [-] - Input field and dropdown fill
    ColorQuad Raised         = { 1.0f, 1.0f, 1.0f, 0.08f };        // [-] - Hovered/raised control fill
    ColorQuad Selected       = { 0.110f, 0.110f, 0.118f, 1.0f };   // [-] - Selected row fill
    ColorQuad Stroke         = { 1.0f, 1.0f, 1.0f, 0.04f };        // [-] - Subtle hairline border
    ColorQuad StrokeStrong   = { 0.110f, 0.110f, 0.118f, 1.0f };   // [-] - Emphasized perimeter border
    ColorQuad Divider        = { 1.0f, 1.0f, 1.0f, 0.04f };        // [-] - Horizontal separator rule
    ColorQuad SectionSurface = { 0.078f, 0.078f, 0.082f, 1.0f };   // [-] - Section container background
    ColorQuad SectionSubtle  = { 0.110f, 0.110f, 0.118f, 1.0f };   // [-] - Secondary section tint
    ColorQuad Text           = { 1.0f, 1.0f, 1.0f, 0.95f };        // [-] - Primary label ink
    ColorQuad TextDim        = { 1.0f, 1.0f, 1.0f, 0.40f };        // [-] - Secondary muted ink
    ColorQuad TextFaint      = { 1.0f, 1.0f, 1.0f, 0.28f };        // [-] - Tertiary hint ink
    ColorQuad Primary        = { 1.0f, 1.0f, 1.0f, 1.0f };         // [-] - High-contrast primary button fill
    ColorQuad PrimaryInk     = { 0.067f, 0.067f, 0.067f, 1.0f };   // [-] - Primary button foreground ink
    ColorQuad Accent         = { 0.231f, 0.510f, 0.965f, 1.0f };   // [-] - Active accent fill
    ColorQuad AccentInk      = { 1.0f, 1.0f, 1.0f, 1.0f };         // [-] - Active accent foreground ink
    ColorQuad AccentSoft     = { 0.231f, 0.510f, 0.965f, 0.12f };  // [-] - Translucent accent wash
    ColorQuad Highlight      = { 0.231f, 0.510f, 0.965f, 1.0f };   // [-] - Focus ring and selection highlight
    ColorQuad Danger         = ColorQuad::FromHex(0xEF4444u);      // [-] - Destructive/error semantic color
    ColorQuad Success        = ColorQuad::FromHex(0x22C55Eu);      // [-] - Success semantic color
    ColorQuad Notice         = ColorQuad::FromHex(0x3B82F6u);      // [-] - Informational semantic color
    ColorQuad Warning        = ColorQuad::FromHex(0xF59E0Bu);      // [-] - Warning semantic color
    ColorQuad Caution        = ColorQuad::FromHex(0xEAB308u);      // [-] - Caution semantic color
    ColorQuad SliderTrack    = { 1.0f, 1.0f, 1.0f, 0.0665f };      // [-] - Unfilled slider rail tint
    ColorQuad SliderFill     = { 1.0f, 1.0f, 1.0f, 0.5585f };      // [-] - Active slider rail fill
    ColorQuad SliderThumb    = { 1.0f, 1.0f, 1.0f, 0.9675f };      // [-] - Circular slider knob fill
    ColorQuad SwitchKnobOff  = ColorQuad::FromHex(0xBDBDBDu);      // [-] - Inactive switch thumb tint
    bool      LightSurface   = false;                              // [-] - High-luminance theme indicator
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace ControlAppearance
{

void ConfigureTheme(
    const ThemeRepository& Theme,
    ColorQuad           WarningColor = ColorQuad::FromHex(0xF59E0Bu),
    ColorQuad           SuccessColor = ColorQuad::FromHex(0x22C55Eu),
    ColorQuad           NoticeColor  = ColorQuad::FromHex(0x3B82F6u),
    ColorQuad           CautionColor = ColorQuad::FromHex(0xEAB308u)) noexcept;

[[nodiscard]] ControlColorSet ConstructColorSet(
    const ThemeRepository& Theme,
    ColorQuad           WarningColor = ColorQuad::FromHex(0xF59E0Bu),
    ColorQuad           SuccessColor = ColorQuad::FromHex(0x22C55Eu),
    ColorQuad           NoticeColor  = ColorQuad::FromHex(0x3B82F6u),
    ColorQuad           CautionColor = ColorQuad::FromHex(0xEAB308u)) noexcept;

void                                 BlendActiveColors(const ControlColorSet& From, const ControlColorSet& To, float Progress) noexcept;
[[nodiscard]] const ControlColorSet& Colors() noexcept;

[[nodiscard]] inline bool  QueryPointerOver(Rectangle2D Bounds, const ControlPointer& Pointer) noexcept;
[[nodiscard]] inline float CapsuleRadius(Rectangle2D Bounds) noexcept;

void ConstructRoundedOutline(PixelSpace& Surface, Rectangle2D Bounds, ColorQuad Stroke, float Radius, float Thickness = 1.0f) noexcept;
void ConstructCircleOutline(PixelSpace& Surface, float CenterX, float CenterY, float Radius, ColorQuad Stroke, float Thickness = 1.0f) noexcept;
void ConstructCircleFill(PixelSpace& Surface, float CenterX, float CenterY, float Radius, ColorQuad Fill) noexcept;
void ConstructHairline(PixelSpace& Surface, float X, float Y, float Width, ColorQuad Ink) noexcept;

void TextCentered(PixelSpace& Surface, Rectangle2D Bounds, ColorQuad Ink, const char* Utf8, float FontSize) noexcept;
void TextLeading(PixelSpace& Surface, Rectangle2D Bounds, float PadX, ColorQuad Ink, const char* Utf8, float FontSize) noexcept;

void ConstructIcon(PixelSpace& Surface, float X, float Y, float Size, ColorQuad Ink, ControlIcon Icon, float StrokeWidth = 2.0f) noexcept;
void ConstructIconCentered(PixelSpace& Surface, Rectangle2D Bounds, float Size, ColorQuad Ink, ControlIcon Icon, float StrokeWidth = 2.0f) noexcept;

[[nodiscard]] Rectangle2D ConstructSectionContainer(PixelSpace& Surface, Rectangle2D Bounds, float Radius, float Opacity = 1.0f) noexcept;
float                     ConstructSectionHeading(
    PixelSpace& Surface,
    float       X,
    float       Y,
    float       Width,
    const char* Title,
    const char* Description,
    ColorQuad   TitleInk,
    ColorQuad   MutedInk,
    float       Opacity = 1.0f) noexcept;
[[nodiscard]] Rectangle2D ConstructLabeledRow(
    PixelSpace& Surface,
    float       X,
    float       Y,
    float       Width,
    const char* Label,
    ColorQuad   LabelInk,
    float       Opacity = 1.0f) noexcept;

[[nodiscard]] float AdvanceScrollOffset(float Offset, float WheelDelta, float ContentHeight, float ViewHeight) noexcept;
void                ConstructScrollIndicator(PixelSpace& Surface, Rectangle2D ViewBounds, float Offset, float ContentHeight, float Opacity = 1.0f) noexcept;

void                RecordHoveredTooltip(Rectangle2D AnchorBounds, const OptionalTooltip& Tooltip) noexcept;
[[nodiscard]] bool  ConsumeHoveredTooltip(Rectangle2D& OutAnchorBounds, OptionalTooltip& OutTooltip) noexcept;
void                ClearHoveredTooltip() noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline bool QueryPointerOver(Rectangle2D Bounds, const ControlPointer& Pointer) noexcept
{
    return Pointer.Enabled && Bounds.Contains({ Pointer.X, Pointer.Y });
}

inline float CapsuleRadius(Rectangle2D Bounds) noexcept
{
    return (Bounds.Width < Bounds.Height ? Bounds.Width : Bounds.Height) * 0.5f;
}

} // namespace ControlAppearance

} // namespace Frontier
