/*============================================================================================================================================
                                                         WORKSPACEAPPEARANCE.H
============================================================================================================================================*/
// 🧩 Shared surface color tokens, VisualToken, VectorGraphic, Workspace icons, indicators, signs, and symbols

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ConstructSlotCategory : uint8_t
{
    Folder       = 0, // [-] - Organizational Showcase folder container
    CategoryOne  = 1, // [-] - Generic constructible slot category 1 (Cube icon)
    CategoryTwo  = 2, // [-] - Generic constructible slot category 2 (Stacked layers icon)
    CategoryThree= 3, // [-] - Generic constructible slot category 3 (Point light icon)
    CategoryFour = 4, // [-] - Generic constructible slot category 4 (Camera icon)
};

enum class VisualTokenShape : uint8_t
{
    Circle        = 0, // [-] - Circular filled/stroked color token
    Square        = 1, // [-] - Sharp rectangular color token
    RoundedSquare = 2, // [-] - Rounded-corner square color token
};

enum class CapabilityIconId : uint8_t
{
    Visibility    = 0, // [-] - Solid eye capability icon
    ShadowCaster  = 1, // [-] - Solid caster square + projected shadow icon
    DynamicMotion = 2, // [-] - Solid play wedge + motion streaks icon
    TransformLock = 3, // [-] - Solid padlock capability icon
};

enum class ViewportProjectionPreset : uint8_t
{
    Perspective  = 0, // [-] - Perspective camera projection
    Orthographic = 1, // [-] - Parallel orthographic projection
    Top          = 2, // [-] - Top-down axial projection
    Front        = 3, // [-] - Front axial projection
    Right        = 4, // [-] - Right axial projection
    Count        = 5, // [count] - Total projection presets
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VisualToken
{
    VisualTokenShape Shape        = VisualTokenShape::Circle;  // [-]  - Geometric primitive shape
    ColorRgba        FillColor    = { 255u, 255u, 255u, 255u };// [-]  - Interior token fill color
    ColorRgba        StrokeColor  = { 0u, 0u, 0u, 0u };        // [-]  - Optional perimeter ring color
    float            Extent       = 8.0f;                      // [px] - Diameter or side length
    float            CornerRadius = 3.0f;                      // [px] - Corner radius for RoundedSquare
    float            StrokeWidth  = 1.2f;                      // [px] - Perimeter stroke thickness
};

struct VectorGraphic
{
    std::string_view SvgPath     {};     // [-]  - SVG 1.1 path command string
    float            ViewBox     = 24.0f;// [px] - Normalized authoring square extent
    bool             Filled      = false;// [-]  - Solid fill vs round-cap stroke
    float            StrokeWidth = 1.6f; // [px] - ViewBox-space stroke thickness
};

struct WorkspaceSurfaceColors
{
    static constexpr ColorRgba RootGap           = { 38,  38,  38,  255 }; // [-] - #262626 2px workspace divider
    static constexpr ColorRgba TabBarBackground  = { 10,  10,  10,  255 }; // [-] - #0A0A0A 30px tab strip
    static constexpr ColorRgba DockBackground    = { 18,  18,  18,  255 }; // [-] - #121212 primary pane background
    static constexpr ColorRgba FooterBackground  = { 21,  21,  21,  255 }; // [-] - #151515 pane status footer
    static constexpr ColorRgba ControlWell       = { 25,  25,  25,  255 }; // [-] - #191919 search, stats & notes surface
    static constexpr ColorRgba ControlWellBorder = { 44,  44,  44,  255 }; // [-] - #2C2C2C recessed control border
    static constexpr ColorRgba DeepRecess        = { 17,  17,  17,  255 }; // [-] - #111111 text input interior
    static constexpr ColorRgba ModeStripRecess   = { 9,   9,   9,   255 }; // [-] - #090909 Edit/Simulate/Play recess
    static constexpr ColorRgba SectionSurface    = { 32,  32,  32,  255 }; // [-] - #202020 Showcase folder section fill
    static constexpr ColorRgba GenericSurface    = { 26,  26,  26,  255 }; // [-] - #1A1A1A transform & inspector tile fill
    static constexpr ColorRgba OverlaySheet      = { 0,   0,   0,   255 }; // [-] - #000000 Studio Controls overlay surface
    static constexpr ColorRgba OverlayHover      = { 255, 255, 255, 16  }; // [-] - Subtle hover wash inside overlay sheets
    static constexpr ColorRgba RowHover          = { 30,  30,  30,  255 }; // [-] - #1E1E1E hovered outliner row
    static constexpr ColorRgba RowSelected       = { 38,  38,  38,  255 }; // [-] - #262626 selected outliner row
    static constexpr ColorRgba RowSelectedBorder = { 68,  68,  68,  255 }; // [-] - #444444 selected row perimeter
    static constexpr ColorRgba SelectionAmber    = { 255, 180, 84,  255 }; // [-] - #FFB454 3px leading selection bar
    static constexpr ColorRgba HairlineLight     = { 255, 255, 255, 16  }; // [-] - Subtle horizontal separator
    static constexpr ColorRgba PrimaryInk        = { 235, 235, 235, 255 }; // [-] - Primary foreground ink
    static constexpr ColorRgba SecondaryInk      = { 176, 176, 176, 255 }; // [-] - Secondary label ink
    static constexpr ColorRgba MutedInk          = { 138, 138, 138, 255 }; // [-] - Muted caption & breadcrumb ink
    static constexpr ColorRgba FaintInk          = { 92,  92,  92,  255 }; // [-] - Dim count & hint ink
    static constexpr ColorRgba FolderHeadingInk  = { 214, 219, 207, 255 }; // [-] - Showcase folder section title ink
    static constexpr ColorRgba StatusGreen       = { 52,  199, 89,  255 }; // [-] - #34C759 visible status disc
    static constexpr ColorRgba StatusGreenInk    = { 17,  17,  17,  255 }; // [-] - Dark check ink inside visible disc
    static constexpr ColorRgba ConstructCream    = { 222, 222, 219, 255 }; // [-] - #DEDEDB primary Construct button fill
    static constexpr ColorRgba ConstructDarkInk  = { 21,  21,  21,  255 }; // [-] - #151515 Construct button dark ink
    static constexpr ColorRgba PreviewGoldInk    = { 191, 162, 122, 255 }; // [-] - #BFA27A PREVIEW & active tool tint
    static constexpr ColorRgba ShowcaseWarmGold  = { 222, 193, 147, 255 }; // [-] - #DEC193 active tool foreground
    static constexpr ColorRgba SageCollection    = { 162, 184, 124, 255 }; // [-] - #A2B87C Showcase folder accent
    static constexpr ColorRgba ShowcaseSage      = { 162, 184, 124, 255 }; // [-] - #A2B87C Showcase collection accent
    static constexpr ColorRgba CheckerDark       = { 13,  13,  13,  255 }; // [-] - #0D0D0D viewport checker dark cell
    static constexpr ColorRgba CheckerLight      = { 20,  20,  20,  255 }; // [-] - #141414 viewport checker light cell
};

struct PaneInputReading
{
    ControlPointer PrimaryPointer    {};    // [-]  - Left mouse pointer reading
    float          PointerDeltaX     = 0.0f;// [px] - Pointer horizontal delta since previous frame
    float          PointerDeltaY     = 0.0f;// [px] - Pointer vertical delta since previous frame
    bool           SecondaryPressed  = false;// [-] - Right mouse button pressed this step
    bool           SecondaryDown     = false;// [-] - Right mouse button held
    bool           SecondaryReleased = false;// [-] - Right mouse button released this step
    bool           MiddlePressed     = false;// [-] - Middle mouse button pressed this step
    bool           MiddleDown        = false;// [-] - Middle mouse button held
    bool           MiddleReleased    = false;// [-] - Middle mouse button released this step
    bool           DoubleClicked     = false;// [-] - Primary double-click pulse
    float          WheelDeltaY       = 0.0f; // [ln] - Vertical mouse wheel notches (+up / -down)
    bool           ShiftHeld         = false;// [-] - Shift modifier held (range multi-select)
    bool           ControlHeld       = false;// [-] - Ctrl modifier held (toggle multi-select / shortcuts)
    bool           AltHeld           = false;// [-] - Alt modifier held (edge loop / orbit)
    bool           EnterPressed      = false;// [-] - Return/Enter key pressed
    bool           EscapePressed     = false;// [-] - Escape key pressed
    bool           DeletePressed     = false;// [-] - Delete key pressed
    bool           BackspacePressed  = false;// [-] - Backspace key pressed
    bool           TabPressed        = false;// [-] - Tab key pressed
    bool           ArrowLeftPressed  = false;// [-] - Left arrow pressed
    bool           ArrowRightPressed = false;// [-] - Right arrow pressed
    bool           HomePressed       = false;// [-] - Home key pressed
    bool           EndPressed        = false;// [-] - End key pressed
    bool           ShortcutAll       = false;// [-] - Ctrl+A shortcut pulse
    bool           ShortcutFind      = false;// [-] - Ctrl+F shortcut pulse
    bool           ShortcutGroup     = false;// [-] - Ctrl+G shortcut pulse
    bool           ShortcutDuplicate = false;// [-] - Ctrl+D shortcut pulse
    bool           ToggleDiagnostics = false;// [-] - F3 diagnostics shortcut pulse
    std::string    TypedUtf8         {};    // [-]  - UTF-8 characters typed this step
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace WorkspaceAppearance
{

[[nodiscard]] PaneInputReading CaptureImGuiPaneInput(Rectangle2D ScreenBounds, float ScaleFactor) noexcept;

void ForwardInputToTextBuffer(TextEditBuffer& Buffer, const PaneInputReading& Input, double Δt) noexcept;

[[nodiscard]] ImFont* ResolveWeightedFont(FontWeightClass Weight, bool Monospace = false) noexcept;

void ConstructWeightedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text,
    bool             Monospace = false) noexcept;

[[nodiscard]] float MeasureWeightedTextWidth(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    float            PixelSize,
    std::string_view Text,
    bool             Monospace = false) noexcept;

void ConstructTruncatedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            MaxWidth,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text,
    bool             Monospace = false) noexcept;

void ConstructRightAlignedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          RightTopAnchor,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text,
    bool             Monospace = false) noexcept;

// VisualToken & VectorGraphic primitives
[[nodiscard]] ColorRgba   QueryDockedPaneBackground() noexcept;
[[nodiscard]] ColorRgba   QueryCategoryAccent(
    ConstructSlotCategory Category,
    ColorRgba             FolderTint = WorkspaceSurfaceColors::SageCollection) noexcept;
[[nodiscard]] VisualToken QueryCategoryVisualToken(
    ConstructSlotCategory Category,
    float                 Extent     = 7.0f,
    ColorRgba             FolderTint = WorkspaceSurfaceColors::SageCollection) noexcept;
[[nodiscard]] const char* QueryCategoryDefaultLabel(ConstructSlotCategory Category) noexcept;
[[nodiscard]] const char* QueryProjectionPresetLabel(ViewportProjectionPreset Preset) noexcept;

void ConstructVisualToken(
    PixelSpace&        Surface,
    Point2D            Center,
    const VisualToken& Token,
    float              Opacity = 1.0f) noexcept;

void ConstructVectorGraphic(
    PixelSpace&          Surface,
    const VectorGraphic& Graphic,
    Point2D              TopLeft,
    float                Extent,
    ColorRgba            Ink) noexcept;

// Authentic Workspace UI Icons
void ConstructShowcaseFolderIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   AccentTint = WorkspaceSurfaceColors::SageCollection,
    float       Opacity    = 1.0f) noexcept;

void ConstructGenericFolderIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   AccentTint = { 185, 173, 149, 255 },
    float       Opacity    = 1.0f) noexcept;

void ConstructSlotCategoryIcon(
    PixelSpace&           Surface,
    ConstructSlotCategory Category,
    Point2D               TopLeft,
    float                 Extent,
    ColorRgba             FolderTint = WorkspaceSurfaceColors::SageCollection,
    float                 Opacity    = 1.0f) noexcept;

void ConstructCameraIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    float       Opacity = 1.0f) noexcept;

void ConstructFilterSlidersIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   Ink,
    ColorRgba   KnobFill = WorkspaceSurfaceColors::DockBackground) noexcept;

void ConstructEditIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept;
void ConstructSimulateIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink, ColorRgba InteriorCutout) noexcept;
void ConstructPlayIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept;
void ConstructPauseIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept;
void ConstructConstructIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept;
void ConstructFocusIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept;
void ConstructSplitViewportIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink, ColorRgba InteriorCutout) noexcept;
void ConstructDiagnosticsIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept;
void ConstructViewportSettingsIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink, ColorRgba InteriorCutout) noexcept;
void ConstructCapabilityIcon(
    PixelSpace&      Surface,
    Point2D          TopLeft,
    float            Extent,
    CapabilityIconId Capability,
    ColorRgba        Ink,
    ColorRgba        InteriorCutout) noexcept;

// Semantic Indicators, Signs, and Symbols
void ConstructVisibilityIndicator(
    PixelSpace& Surface,
    Point2D     Center,
    bool        Visible,
    float       Opacity = 1.0f) noexcept;

void ConstructVerticalScrollIndicator(
    PixelSpace& Surface,
    Rectangle2D ViewportBounds,
    float       ContentHeight,
    float       ScrollOffset,
    Point2D     Cursor) noexcept;

void ConstructCautionTriangleSign(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    ColorRgba   Ink) noexcept;

void ConstructOrientationAxesSign(
    PixelSpace&              Surface,
    Rectangle2D              PaneBounds,
    ViewportProjectionPreset Projection) noexcept;

void ConstructCheckSymbol(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    ColorRgba   Ink,
    float       Thickness = 1.8f) noexcept;

void ConstructChevronSymbol(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    bool        Expanded,
    ColorRgba   Ink) noexcept;

void ConstructResetSymbol(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    ColorRgba   Ink,
    float       Thickness = 1.35f) noexcept;

} // namespace WorkspaceAppearance

} // namespace Frontier
