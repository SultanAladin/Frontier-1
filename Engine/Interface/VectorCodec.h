//============================================================================================================================================
// 🧩 Frontier/DisplayPresentation/VectorCodec.h — Categorized Scalable Vector Graphic (SVG) Path Decoders and Glyph Geometry
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include <cstdint>
#include <string_view>
#include <array>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  ICON CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class IconCategory : uint32_t
{
    Navigation                          = 0,                    // Directional arrows, chevrons, rotations and expands
    ControlCentre                       = 1,                    // Settings, appearance, display, input, bell, wifi, power
    EditorTools                         = 2,                    // Select, translate, rotate, scale, snap and viewport
    TexturePainting                     = 3,                    // Brush, eraser, eyedropper, bucket, stamp, gradient
    Outliner                            = 4,                    // Folder, hierarchy, light, camera, material, visibility
    Count                               = 5
};

//------------------------------------------------------------------------------------------------------------------------
//                                             NAVIGATION ICON CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class NavigationIconCategory : uint32_t
{
    ArrowUp                             = 0,                    // ↑ straight up
    ArrowDown                           = 1,                    // ↓ straight down
    ArrowLeft                           = 2,                    // ← straight left
    ArrowRight                          = 3,                    // → straight right
    ChevronUp                           = 4,                    // ⌃ single chevron up
    ChevronDown                         = 5,                    // ⌄ single chevron down
    ChevronLeft                         = 6,                    // ‹ single chevron left
    ChevronRight                        = 7,                    // › single chevron right
    ChevronsUp                          = 8,                    // ︽ double chevron up
    ChevronsDown                        = 9,                    // ︾ double chevron down
    ChevronsLeft                        = 10,                   // « double chevron left
    ChevronsRight                       = 11,                   // » double chevron right
    CornerDownRight                     = 12,                   // ↳ tree branch right
    CornerDownLeft                      = 13,                   // ↵ tree branch left
    RotateClockwise                     = 14,                   // ↻ clockwise loop
    RotateCounterClockwise              = 15,                   // ↺ counter-clockwise loop
    ExpandDiagonal                      = 16,                   // ⤢ maximize/expand
    CollapseDiagonal                    = 17,                   // ⤡ minimize/collapse
    Count                               = 18
};

//------------------------------------------------------------------------------------------------------------------------
//                                           CONTROL CENTRE ICON CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class ControlCentreIconCategory : uint32_t
{
    SettingsGear                        = 0,                    // ⚙ gear cog
    AppearancePalette                   = 1,                    // 🎨 artist palette / theme swatches
    DisplayMonitor                      = 2,                    // 🖥 monitor display screen
    InputDevices                        = 3,                    // ⌨ keyboard / input controller
    NotificationsBell                   = 4,                    // 🔔 bell notification alert
    WirelessSignal                      = 5,                    // 🛜 wifi wireless waves
    BluetoothSymbol                     = 6,                    // ᛒ bluetooth node
    MoonDisturbance                     = 7,                    // 🌙 do not disturb moon
    VolumeSpeaker                       = 8,                    // 🔊 audio master speaker
    SunIllumination                     = 9,                    // ☀️ brightness illumination
    SparklesAntiAliasing                = 10,                   // ✦ lucide "sparkles" — anti-aliasing tile
    GaugeFrameRate                      = 11,                   // ◔ lucide "gauge" — FPS overlay tile
    SlidersQuality                      = 12,                   // ☰ lucide "sliders-horizontal" — quality tile
    VideoRenderScale                    = 13,                   // ▭ lucide "video" — render-scale pill
    CloseCross                          = 14,                   // ✕ lucide "x" — page close button
    ChevronBack                         = 15,                   // ‹ lucide "chevron-left" — hub back button
    ChevronForward                      = 16,                   // › lucide "chevron-right" — hub row affordance
    ShieldInput                         = 17,                   // ⛨ lucide "shield" — Input & Keybindings row
    ChevronDown                         = 18,                   // ⌄ lucide "chevron-down" — dropdown caret
    ChevronUp                           = 19,                   // ⌃ lucide "chevron-up" — dropdown caret (open)
    TriangleAlert                       = 20,                   // ⚠ lucide "triangle-alert" — warning tone
    CircleCheck                         = 21,                   // ✓ lucide "circle-check-big" — success tone
    CircleInfo                          = 22,                   // ⓘ lucide "info" — info tone
    OctagonAlert                        = 23,                   // ⛔ lucide "octagon-alert" — caution / danger tone

    // ── Editor row columns and record furniture ────────────────────────────────────────────────────────────────────
    // Added for the editor's record rows. Before these existed the register borrowed DisplayMonitor for
    //    "visible" and ShieldInput for "locked", which is why a row read as a monitor and a shield rather than
    //    an eye and a padlock. A borrowed glyph is worse than a missing one: it looks deliberate.
    EyeVisible                          = 24,                   // 👁 lucide "eye" — row is visible
    EyeHidden                           = 25,                   // 👁̸ lucide "eye-off" — row is hidden
    LockClosed                          = 26,                   // 🔒 lucide "lock" — row is locked
    LockOpen                            = 27,                   // 🔓 lucide "lock-open" — row is editable
    MotionActivity                      = 28,                   // ∿ lucide "activity" — row is dynamic
    FolderClosed                        = 29,                   // 🗀 lucide "folder" — container row
    FolderOpen                          = 30,                   // 🗁 lucide "folder-open" — expanded container
    CubeObject                          = 31,                   // ⬢ lucide "box" — mesh / geometry row
    SearchGlass                         = 32,                   // 🔍 lucide "search" — the search field
    PlusAdd                             = 33,                   // ＋ lucide "plus" — add a row
    TrashDelete                         = 34,                   // 🗑 lucide "trash-2" — remove a row
    LayoutSplit                         = 35,                   // ▥ lucide "columns-2" — split layout mode
    LayoutPanelLeft                     = 36,                   // ▤ lucide "panel-left" — outliner-only mode
    LayoutPanelRight                    = 37,                   // ▤ lucide "panel-right" — properties-only mode
    CameraBody                          = 38,                   // 🎥 lucide "video" — camera row
    LayersSlabs                         = 39,                   // 🗂 lucide "layers" — Materials hub row (stacked slabs)
    Count                               = 40
};

//------------------------------------------------------------------------------------------------------------------------
//                                              OUTLINER ICON CATEGORY
//------------------------------------------------------------------------------------------------------------------------
// The celestial outliner's own glyph set — 24 × 24, stroke 1.6, round caps and joins, no fill. Ported stroke for
//    stroke from the page's icon function; a glyph is several strokes because the page dims some of its
//    children (opacity) and dashes others (stroke-dasharray), and one flat string cannot carry either.

enum class OutlinerIconCategory : uint32_t
{
    Globe = 0, Cloud, VolumeClouds, LocalCloud, Rainbow, Wind, Rain, AerialFog, VolumeFog, Folder, Fog,
    Atmosphere, Sun, Sky, Check, Dot, Warn, Shadow, Bounce, Collide, Stars, Moon, Camera, Effects, Eye,
    EyeOff, Chevron, ChevronUp, Wave, Horizon, Orbit, Bulb, Palette, Ground, Lattice, Galaxy, Aperture,
    Sliders, Flare, Up, Down, Flat, Plus, Plane, Key, Minus, Trash, Search, Compact,
    Count
};

struct OutlinerGlyphStroke
{
    const char* SvgPathString;                              // [svg] one child, circles / rects rewritten as arcs
    float       Opacity;                                    // [0..1] the child's own opacity attribute
    float       DashOn;                                     // [viewBox units] 0 = solid
    float       DashOff;                                    // [viewBox units]
    bool        Filled;                                     // the one solid child (the standing dot)
};

constexpr uint32_t kOutlinerGlyphStrokes = 5u;

struct OutlinerGlyphRecord
{
    const char*         IdentifierName;
    OutlinerGlyphStroke Strokes[kOutlinerGlyphStrokes];
    uint32_t            StrokeCount;
    float               StrokeWidth;                        // [viewBox units] 1.6, the compact mark 1.8
};

// Lucide glyphs are authored as several <path>/<circle>/<rect> children. VectorGlyphRecord stores them
//    concatenated into one path string: circles and rects are rewritten as equivalent arc / line paths so a
//    single SVG path decoder handles every glyph. Sub-paths are separated by their own M commands.

//------------------------------------------------------------------------------------------------------------------------
//                                                VECTOR GLYPH RECORD
//------------------------------------------------------------------------------------------------------------------------

struct VectorGlyphRecord
{
    const char*             IdentifierName;                     // [text] unique glyph slug
    const char*             SvgPathString;                      // [svg] normalized 24x24 SVG path coordinate stream
    uint32_t                ViewBoxWidth;                       // [px] base viewbox width (24px standard)
    uint32_t                ViewBoxHeight;                      // [px] base viewbox height (24px standard)
    float                   DefaultStrokeWidth;                 // [px] standard stroke thickness (2.0px)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR CODEC
//------------------------------------------------------------------------------------------------------------------------

class VectorCodec
{
public:
    VectorCodec() noexcept = default;
    ~VectorCodec() noexcept = default;

    VectorCodec(const VectorCodec&) = delete;
    VectorCodec& operator=(const VectorCodec&) = delete;

    [[nodiscard]] static const VectorGlyphRecord& QueryNavigationIcon(NavigationIconCategory Icon) noexcept;
    [[nodiscard]] static std::string_view QueryNavigationSvgPath(NavigationIconCategory Icon) noexcept;
    [[nodiscard]] static uint32_t         QueryNavigationIconCount() noexcept;

    [[nodiscard]] static const VectorGlyphRecord& QueryControlCentreIcon(ControlCentreIconCategory Icon) noexcept;
    [[nodiscard]] static std::string_view QueryControlCentreSvgPath(ControlCentreIconCategory Icon) noexcept;
    [[nodiscard]] static uint32_t         QueryControlCentreIconCount() noexcept;

    // Single unified conversion operator for icon record
    template<typename TargetType>
    [[nodiscard]] static TargetType Convert(NavigationIconCategory Icon) noexcept;

    template<typename TargetType>
    [[nodiscard]] static TargetType Convert(ControlCentreIconCategory Icon) noexcept;

    [[nodiscard]] static const OutlinerGlyphRecord& QueryOutlinerIcon(OutlinerIconCategory Icon) noexcept;
    [[nodiscard]] static uint32_t                   QueryOutlinerIconCount() noexcept;

private:
    static const std::array<OutlinerGlyphRecord, static_cast<size_t>(OutlinerIconCategory::Count)> OutlinerGlyphTable;
    static const std::array<VectorGlyphRecord, static_cast<size_t>(NavigationIconCategory::Count)> NavigationGlyphTable;
    static const std::array<VectorGlyphRecord, static_cast<size_t>(ControlCentreIconCategory::Count)> ControlCentreGlyphTable;
};

template<>
inline std::string_view VectorCodec::Convert<std::string_view>(NavigationIconCategory Icon) noexcept
{
    return QueryNavigationSvgPath(Icon);
}

template<>
inline const VectorGlyphRecord& VectorCodec::Convert<const VectorGlyphRecord&>(NavigationIconCategory Icon) noexcept
{
    return QueryNavigationIcon(Icon);
}

template<>
inline std::string_view VectorCodec::Convert<std::string_view>(ControlCentreIconCategory Icon) noexcept
{
    return QueryControlCentreSvgPath(Icon);
}

template<>
inline const VectorGlyphRecord& VectorCodec::Convert<const VectorGlyphRecord&>(ControlCentreIconCategory Icon) noexcept
{
    return QueryControlCentreIcon(Icon);
}

} // namespace Frontier
