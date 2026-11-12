/*============================================================================================================================================
                                                            VECTORICONREPOSITORY.H
============================================================================================================================================*/
// 🧩 SVG 1.1 path decoder, round-cap stroker, and reusable vector icon repository for engine UI controls and panels

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
    SettingsGear         = 0,  // [-] - Settings gear icon
    AppearanceSwatches   = 1,  // [-] - Appearance color-board icon
    DisplayMonitor       = 2,  // [-] - Display monitor icon
    InputDevices         = 3,  // [-] - Input devices icon
    NotificationsBell    = 4,  // [-] - Notification bell icon
    WirelessSignal       = 5,  // [-] - Wireless wave icon
    BluetoothSymbol      = 6,  // [-] - Bluetooth rune icon
    MoonDisturbance      = 7,  // [-] - Crescent moon icon
    VolumeSpeaker        = 8,  // [-] - Speaker icon
    SunIllumination      = 9,  // [-] - Sun rays icon
    SparklesAntiAliasing = 10, // [-] - Sparkle icon
    GaugeRate            = 11, // [-] - Speedometer gauge icon
    SlidersQuality       = 12, // [-] - Tuning sliders icon
    VideoRenderScale     = 13, // [-] - Video camera icon
    CloseCross           = 14, // [-] - Dismiss cross icon
    ChevronBack          = 15, // [-] - Back navigation chevron
    ChevronForward       = 16, // [-] - Forward navigation chevron
    ShieldInput          = 17, // [-] - Input shield icon
    ChevronDown          = 18, // [-] - Collapse chevron
    ChevronUp            = 19, // [-] - Expand chevron
    TriangleAlert        = 20, // [-] - Warning triangle icon
    CircleCheck          = 21, // [-] - Success check circle icon
    CircleNotice         = 22, // [-] - Notice circle icon
    OctagonAlert         = 23, // [-] - Caution octagon icon
    EyeVisible           = 24, // [-] - Visible eye icon
    EyeHidden            = 25, // [-] - Hidden eye icon
    LockClosed           = 26, // [-] - Locked padlock icon
    LockOpen             = 27, // [-] - Unlocked padlock icon
    MotionActivity       = 28, // [-] - Pulse waveform icon
    FolderClosed         = 29, // [-] - Closed directory icon
    FolderOpen           = 30, // [-] - Open directory icon
    CubeShape            = 31, // [-] - 3D cube icon
    SearchGlass          = 32, // [-] - Magnifying glass icon
    PlusAdd              = 33, // [-] - Plus cross icon
    TrashDelete          = 34, // [-] - Trash bin icon
    LayoutSplit          = 35, // [-] - Split workspace icon
    LayoutPanelLeft      = 36, // [-] - Left sidebar icon
    LayoutPanelRight     = 37, // [-] - Right sidebar icon
    CameraBody           = 38, // [-] - Camera body icon
    LayersSlabs          = 39, // [-] - Stacked slabs icon
    SaveDisk             = 40, // [-] - Save disk icon
    ExternalOpen         = 41, // [-] - External launch box-and-arrow icon
    LightbulbHint        = 42, // [-] - Hint lightbulb icon
    QuestionCircle       = 43, // [-] - Contextual help question circle icon
    WrenchRepair         = 44, // [-] - Diagnostic repair wrench icon
    RefreshCycle         = 45, // [-] - Reimport / reload cycle arrows icon
    LayoutGrid           = 46, // [-] - 2x2 rounded tile grid view icon
    LayoutList           = 47, // [-] - Bulleted horizontal row list view icon
    LayoutColumns        = 48, // [-] - Vertical colour-strip columns view icon
    LayoutRows           = 49, // [-] - Horizontal colour-strip rows view icon
    Count                = 50, // [count] - Total control icons
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

class VectorIconRepository
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

    static void ConstructNavigation(PixelSpace& Surface, NavigationIcon Icon, const VectorIconPlacement& Placement) noexcept;
    static void ConstructControl(PixelSpace& Surface, ControlIcon Icon, const VectorIconPlacement& Placement) noexcept;
};

} // namespace Frontier
