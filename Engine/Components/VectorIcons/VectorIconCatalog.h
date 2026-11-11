/*============================================================================================================================================
                                                        VECTORICONCATALOG.H
============================================================================================================================================*/
// 🧩 SVG 1.1 path decoder, round-cap stroker, and reusable vector icon catalog for engine UI controls and panels

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class NavigationIcon : uint8_t
{
    ArrowUp                = 0,  // [-] - Upward directional arrow
    ArrowDown              = 1,  // [-] - Downward directional arrow
    ArrowLeft              = 2,  // [-] - Leftward directional arrow
    ArrowRight             = 3,  // [-] - Rightward directional arrow
    ChevronUp              = 4,  // [-] - Upward chevron
    ChevronDown            = 5,  // [-] - Downward chevron
    ChevronLeft            = 6,  // [-] - Leftward chevron
    ChevronRight           = 7,  // [-] - Rightward chevron
    ChevronsUp             = 8,  // [-] - Double upward chevrons
    ChevronsDown           = 9,  // [-] - Double downward chevrons
    ChevronsLeft           = 10, // [-] - Double leftward chevrons
    ChevronsRight          = 11, // [-] - Double rightward chevrons
    CornerDownRight        = 12, // [-] - Corner-down-right elbow arrow
    CornerDownLeft         = 13, // [-] - Corner-down-left elbow arrow
    RotateClockwise        = 14, // [-] - Clockwise rotation arrow
    RotateCounterClockwise = 15, // [-] - Counter-clockwise rotation arrow
    ExpandDiagonal         = 16, // [-] - Diagonal expand arrows
    CollapseDiagonal       = 17, // [-] - Diagonal collapse arrows
    Count                  = 18, // [count] - Total navigation icons
};

enum class ControlIcon : uint8_t
{
    SettingsGear         = 0,  // [-] - Settings gear emblem
    AppearanceSwatches   = 1,  // [-] - Appearance color-board emblem
    DisplayMonitor       = 2,  // [-] - Display monitor emblem
    InputDevices         = 3,  // [-] - Input devices emblem
    NotificationsBell    = 4,  // [-] - Notification bell emblem
    WirelessSignal       = 5,  // [-] - Wireless wave emblem
    BluetoothSymbol      = 6,  // [-] - Bluetooth rune emblem
    MoonDisturbance      = 7,  // [-] - Crescent moon emblem
    VolumeSpeaker        = 8,  // [-] - Speaker emblem
    SunIllumination      = 9,  // [-] - Sun rays emblem
    SparklesAntiAliasing = 10, // [-] - Sparkle emblem
    GaugeRate            = 11, // [-] - Speedometer gauge emblem
    SlidersQuality       = 12, // [-] - Tuning sliders emblem
    VideoRenderScale     = 13, // [-] - Video camera emblem
    CloseCross           = 14, // [-] - Dismiss cross emblem
    ChevronBack          = 15, // [-] - Back navigation chevron
    ChevronForward       = 16, // [-] - Forward navigation chevron
    ShieldInput          = 17, // [-] - Input shield emblem
    ChevronDown          = 18, // [-] - Collapse chevron
    ChevronUp            = 19, // [-] - Expand chevron
    TriangleAlert        = 20, // [-] - Warning triangle emblem
    CircleCheck          = 21, // [-] - Success check circle emblem
    CircleNotice         = 22, // [-] - Notice circle emblem
    OctagonAlert         = 23, // [-] - Caution octagon emblem
    EyeVisible           = 24, // [-] - Visible eye emblem
    EyeHidden            = 25, // [-] - Hidden eye emblem
    LockClosed           = 26, // [-] - Locked padlock emblem
    LockOpen             = 27, // [-] - Unlocked padlock emblem
    MotionActivity       = 28, // [-] - Pulse waveform emblem
    FolderClosed         = 29, // [-] - Closed directory emblem
    FolderOpen           = 30, // [-] - Open directory emblem
    CubeShape            = 31, // [-] - 3D cube emblem
    SearchGlass          = 32, // [-] - Magnifying glass emblem
    PlusAdd              = 33, // [-] - Plus cross emblem
    TrashDelete          = 34, // [-] - Trash bin emblem
    LayoutSplit          = 35, // [-] - Split workspace emblem
    LayoutPanelLeft      = 36, // [-] - Left sidebar emblem
    LayoutPanelRight     = 37, // [-] - Right sidebar emblem
    CameraBody           = 38, // [-] - Camera body emblem
    LayersSlabs          = 39, // [-] - Stacked slabs emblem
    SaveDisk             = 40, // [-] - Save disk emblem
    Count                = 41, // [count] - Total control icons
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VectorIconPlacement
{
    float     X           = 0.0f;                    // [px] - Top-left horizontal logical coordinate
    float     Y           = 0.0f;                    // [px] - Top-left vertical logical coordinate
    float     Size        = 24.0f;                   // [px] - Logical square icon extent
    ColorRgba Ink         = { 255, 255, 255, 255 };  // [-]  - Stroke or fill color
    float     StrokeWidth = 2.0f;                    // [px] - ViewBox-space stroke width
};

struct VectorIconEntry
{
    std::string_view Name        {};     // [-]  - Canonical icon identifier
    std::string_view SvgPath     {};     // [-]  - SVG 1.1 path command string
    float            ViewBox     = 24.0f;// [px] - Authoring coordinate square extent
    float            StrokeWidth = 2.0f; // [px] - Default viewBox stroke thickness
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class VectorIconCatalog
{
public:
    struct Contour
    {
        std::vector<Point2D> Points {};     // [px] - Flattened polyline vertices in viewBox coordinates
        bool                 Closed = false;// [-]  - Closed subpath indicator
    };

    static constexpr int   CurveSegments  = 12;          // [count] - Cubic/quadratic subdivision steps
    static constexpr float ArcStepRadians = 0.2617993878f;// [rad]   - Elliptical arc tessellation step (~15 deg)

    [[nodiscard]] static std::vector<Contour> FlattenSvgPath(std::string_view SvgPath) noexcept;
    static void                               StrokeSvgPath(
        PixelSpace&                Surface,
        std::string_view           SvgPath,
        const VectorIconPlacement& Placement,
        float                      ViewBox = 24.0f) noexcept;
    static void                               FillSvgPath(
        PixelSpace&                Surface,
        std::string_view           SvgPath,
        const VectorIconPlacement& Placement,
        float                      ViewBox = 24.0f) noexcept;

    [[nodiscard]] static const VectorIconEntry& QueryNavigation(NavigationIcon Icon) noexcept;
    [[nodiscard]] static const VectorIconEntry& QueryControl(ControlIcon Icon) noexcept;

    static void DrawNavigation(PixelSpace& Surface, NavigationIcon Icon, const VectorIconPlacement& Placement) noexcept;
    static void DrawControl(PixelSpace& Surface, ControlIcon Icon, const VectorIconPlacement& Placement) noexcept;
};

} // namespace Frontier
