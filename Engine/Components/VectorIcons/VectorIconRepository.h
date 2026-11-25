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
    MaterialSphere       = 50, // [-] - PBR material shader preview sphere icon (HTML Material)
    PatternTile          = 51, // [-] - Seamless tileable pattern grid icon (HTML Pattern)
    StampTool            = 52, // [-] - Decal stamp silhouette icon (HTML Stamp)
    BrushTool            = 53, // [-] - Surface paint brush icon (HTML Brush)
    ModifierStar         = 54, // [-] - Non-destructive modifier icon (HTML Modifier)
    NoiseWave            = 55, // [-] - Noise modulation wave icon (HTML Noise)
    FolderPlus           = 56, // [-] - Construct nested folder icon (HTML FolderPlus)
    MaskCutout           = 57, // [-] - Layer mask cutout icon (HTML Mask)
    CopyDuplicate        = 58, // [-] - Duplicate layer icon (HTML Copy)
    DragGrip             = 59, // [-] - Six-dot drag reorder grip icon (HTML Grip)
    VectorPointer        = 60, // [-] - Vector cursor selection arrow icon (HTML Pointer)
    VectorCurve          = 61, // [-] - Smooth cubic spline curve icon (HTML Curve)
    VectorPen            = 62, // [-] - Bezier pen nib vector tool icon (HTML Pen)
    EditFocus            = 63, // [-] - Vertex edit focus reticle icon (HTML Focus)
    PointGradient        = 64, // [-] - Multi-point gradient field icon (HTML Gradient)
    ShapeSquare          = 65, // [-] - Square/rectangle motif icon (HTML Square)
    ShapeCircle          = 66, // [-] - Circle motif icon (HTML Circle)
    ShapeDiamond         = 67, // [-] - Diamond motif icon (HTML Diamond)
    ShapeTriangle        = 68, // [-] - Triangle motif icon (HTML Triangle)
    ShapeStar            = 69, // [-] - Five-point star motif icon (HTML Star)
    ShapeHexagon         = 70, // [-] - Hexagon motif icon (HTML Hexagon)
    ShapeStripe          = 71, // [-] - Horizontal stripe motif icon (HTML Stripe)
    TextType             = 72, // [-] - Typography T motif icon (HTML Type)
    HandPan              = 73, // [-] - Viewport canvas pan hand icon (HTML Hand)
    ImageTile            = 74, // [-] - Image tile upload/preview icon (HTML Image)
    UndoArrow            = 75, // [-] - Step backward undo icon (HTML Undo)
    RedoArrow            = 76, // [-] - Step forward redo icon (HTML Redo)
    DownloadExport       = 77, // [-] - Export / download tray arrow icon (HTML Download)
    EraserTool           = 78, // [-] - Paint eraser tool icon (HTML Eraser)
    FillBucket           = 79, // [-] - Flood fill bucket tool icon (HTML Fill)
    PipetteDropper       = 80, // [-] - Colour sample pipette tool icon (HTML Pipette)
    MoveArrows           = 81, // [-] - 4-way transform/move tool icon (HTML Move)
    UploadImport         = 82, // [-] - Upload / import asset arrow icon (HTML Upload)
    BakeBox              = 83, // [-] - High/low baker isometric cube icon (HTML Bake)
    VolumeBoxes          = 84, // [-] - Stacked 3D volume grid icon (HTML Volume)
    PinAnchor            = 85, // [-] - Surface anchor pin icon (HTML Pin)
    Count                = 86, // [count] - Total control icons
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
