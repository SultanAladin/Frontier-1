/*==========================================================================================================================================
                                                          TEXTUREPAINTSESSION.H
==========================================================================================================================================*/
// 🎨 Authoring session for Frontier Texture Paint Studio: 22 PBR channels, 8 layer kinds, 3D/2D PBR rasterizer, gradient points & Brush Studio.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PaintSurfaceChannel : uint8_t
{
    Albedo         = 0,
    Roughness      = 1,
    Metalness      = 2,
    Height         = 3,
    NormalStrength = 4,
    Occlusion      = 5,
    Opacity        = 6,
    Emissive       = 7,
    GlowPower      = 8,
    Clearcoat      = 9,
    CoatRough      = 10,
    Sheen          = 11,
    SheenRough     = 12,
    Specular       = 13,
    Anisotropy     = 14,
    Iridescence    = 15,
    IridFilm       = 16,
    Transmission   = 17,
    Subsurface     = 18,
    SpecColour     = 19,
    SheenColour    = 20,
    SubsurfTint    = 21,
    Count          = 22,
};

enum class PaintChannelGroup : uint8_t
{
    Primary  = 0,
    Surface  = 1,
    Coat     = 2,
    Optics   = 3,
    Radiance = 4,
    Count    = 5,
};

enum class PaintStudyAsset : uint8_t
{
    UtahTeapot = 0,
    ShaderBall = 1,
    ChamferBox = 2,
    UvSphere   = 3,
    TorusKnot  = 4,
    Count      = 5,
};

enum class PaintViewportLayout : uint8_t
{
    ThreeDOnly = 0,
    SplitView  = 1,
    TwoDOnly   = 2,
    Count      = 3,
};

enum class PaintToolCategory : uint8_t
{
    Brush      = 0,
    Eraser     = 1,
    Fill       = 2,
    Gradient3D = 3,
    Curve3D    = 4,
    Stamp      = 5,
    Text       = 6,
    Sample     = 7,
    Move       = 8,
    Count      = 9,
};

enum class PaintLayerCategory : uint8_t
{
    Paint    = 0,
    Fill     = 1,
    Material = 2,
    Stamp    = 3,
    Text     = 4,
    WearGen  = 5,
    Gradient = 6,
    Folder   = 7,
    Count    = 8,
};

enum class PaintBlendMethod : uint8_t
{
    Normal      = 0,
    Multiply    = 1,
    Screen      = 2,
    Overlay     = 3,
    Add         = 4,
    SoftLight   = 5,
    Darken      = 6,
    Lighten     = 7,
    ColorDodge  = 8,
    HeightBlend = 9,
    Count       = 10,
};

enum class PaintFilterTab : uint8_t
{
    AllLayers   = 0,
    VisibleOnly = 1,
    HiddenOnly  = 2,
    MaskedOnly  = 3,
    LockedOnly  = 4,
    Count       = 5,
};

enum class BrushInspectorTab : uint8_t
{
    Brush    = 0,
    Erase    = 1,
    Fill     = 2,
    Gradient = 3,
    Curve    = 4,
    Stamp    = 5,
    Text     = 6,
    Pick     = 7,
    Move     = 8,
    Count    = 9,
};

enum class BrushDetailSubTab : uint8_t
{
    LibraryGrid    = 0,
    StrokePath     = 1,
    Stabilization  = 2,
    Taper          = 3,
    Shape          = 4,
    Grain          = 5,
    Rendering      = 6,
    WetMix         = 7,
    ColourDynamics = 8,
    Curves         = 9,
    Stylus         = 10,
    Properties     = 11,
    Channels       = 12,
    About          = 13,
    Count          = 14,
};

enum class BrushTipShape : uint8_t
{
    Round       = 0,
    Chisel      = 1,
    Chalk       = 2,
    Splatter    = 3,
    Sponge      = 4,
    CustomImage = 5,
    Count       = 6,
};

enum class BrushResponseCurve : uint8_t
{
    TipFalloff         = 0,
    SizeOverStroke     = 1,
    OpacityOverStroke  = 2,
    HueShiftOverStroke = 3,
    Count              = 4,
};

enum class PaintMaskGenerator : uint8_t
{
    None            = 0,
    EdgeWear        = 1,
    CreviceDirt     = 2,
    TopDust         = 3,
    TriplanarGrunge = 4,
    Count           = 5,
};

enum class PaintFillScope : uint8_t
{
    AssetPart  = 0,
    UvIsland   = 1,
    WholeStudy = 2,
    Count      = 3,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PaintChannelDescriptor
{
    PaintSurfaceChannel Key           = PaintSurfaceChannel::Albedo;
    PaintChannelGroup   Group         = PaintChannelGroup::Primary;
    const char*         Label         = "Albedo";
    const char*         SubHint       = "Surface Tint";
    const char*         ShortTag      = "Al";
    ColorRgba           SwatchTint    { 232u, 194u, 122u, 255u };
    float               DefaultMin    = 0.0f;
    float               DefaultMax    = 1.0f;
    float               DefaultScalar = 0.5f;
    bool                ColorChannel  = false;
};

struct PaintStudyDescriptor
{
    PaintStudyAsset Asset     = PaintStudyAsset::UtahTeapot;
    const char*     Label     = "Utah Teapot";
    uint32_t        PartCount = 4u;
    uint32_t        UvIslands = 32u;
};

struct BrushLibraryPresetDescriptor
{
    uint32_t      PresetIndex = 0u;
    const char*   Label       = "Hog Bristle Flat #12";
    const char*   CategoryTag = "Brush";
    BrushTipShape TipShape    = BrushTipShape::Chisel;
    int32_t       PixelRadius = 26;
    float         Hardness    = 0.76f;
    float         Spacing     = 0.14f;
    float         Delivery    = 0.88f;
    float         Stabilizer  = 0.24f;
    ColorRgba     PreviewTint { 232u, 194u, 122u, 255u };
};

struct PaintStrokeDabSpec
{
    float     U          = 0.50f;
    float     V          = 0.50f;
    float     RadiusUv   = 0.07f;
    float     Hardness   = 0.72f;
    float     Opacity    = 1.0f;
    ColorRgba Tint       { 232u, 194u, 122u, 255u };
    float     Roughness  = 0.32f;
    float     Metalness  = 0.0f;
    float     HeightGain = 0.15f;
    bool      EraserMode = false;
};

struct PaintStampPlacement
{
    uint32_t    Id          = 1u;
    std::string Name        = "Warning Seal";
    bool        Visible     = true;
    bool        ThreeDProj  = false;
    float       U           = 0.50f;
    float       V           = 0.50f;
    float       Scale       = 0.28f;
    float       RotationDeg = 0.0f;
    float       Opacity     = 1.0f;
    float       HeightGain  = 0.35f;
    ColorRgba   TintColor   { 54u, 211u, 196u, 255u };
};

struct PaintGradientPointSpec
{
    uint32_t    Id        = 1u;
    std::string Label     = "Point 1";
    float       PositionX = -0.38f;
    float       PositionY = -0.18f;
    float       PositionZ = 0.56f;
    ColorRgba   TintColor { 232u, 194u, 122u, 255u };
    float       Roughness = 0.22f;
    float       Metalness = 0.85f;
    float       Height    = 0.20f;
    float       Radius    = 1.10f;
    float       Strength  = 1.00f;
};

struct PaintMaskConfig
{
    bool               Attached        = false;
    bool               EditingMask     = false;
    bool               Enabled         = true;
    bool               Invert          = false;
    float              Opacity         = 1.0f;
    PaintMaskGenerator Generator       = PaintMaskGenerator::None;
    float              Threshold       = 0.48f;
    float              Softness        = 0.16f;
    int32_t            FeatherRadiusPx = 0;
    uint8_t            FillBrightness  = 255u;
};

struct PaintLayerEntry
{
    static constexpr std::size_t ChannelCount = static_cast<std::size_t>(PaintSurfaceChannel::Count);

    uint32_t           Id                 = 1u;
    uint32_t           FolderOwnerId      = 0u;
    bool               FolderExpanded     = true;
    bool               ExpandedInOutliner = true;
    PaintLayerCategory Category           = PaintLayerCategory::Paint;
    std::string        Name               = "Hand Painted Layer";
    bool               Visible            = true;
    bool               Locked             = false;
    float              Opacity            = 1.0f;
    PaintBlendMethod   BlendMethod        = PaintBlendMethod::Normal;
    int32_t            PartFilter         = -1; // -1 = All Parts

    std::array<bool, ChannelCount>      ActiveChannels      {};
    std::array<float, ChannelCount>     ChannelScalars      {};
    std::array<float, ChannelCount>     ChannelOpacities    {};
    std::array<uint8_t, ChannelCount>   ChannelBlendMethods {}; // 0 = Inherit, 1..10 = PaintBlendMethod + 1
    std::array<ColorRgba, ChannelCount> ChannelTints        {};
    std::array<bool, ChannelCount>      ExpandedChannels    {};
    ColorRgba                           PrimerTint          { 232u, 194u, 122u, 255u };
    PaintSurfaceChannel                 InspectedChannel    = PaintSurfaceChannel::Albedo;

    // Fill & Pattern parameters
    int32_t            PatternRepeats      = 4;
    float              PatternRotationDeg  = 0.0f;
    float              PatternHeightGain   = 0.35f;

    // Material preset parameters
    uint32_t           PresetIndex         = 0u;
    float              GrainScale          = 8.0f;
    float              GrainContrast       = 0.18f;

    // Procedural Generator parameters
    PaintMaskGenerator WearPreset          = PaintMaskGenerator::EdgeWear;
    float              GeneratorThreshold  = 0.46f;
    float              GeneratorSoftness   = 0.22f;
    float              NoiseBreakup        = 0.35f;
    float              NoiseScale          = 6.0f;
    bool               GeneratorInvert     = false;

    // Text & Stamp parameters
    std::string        TextContent         = "FRONTIER";
    uint32_t           TextFontIndex       = 0u;
    float              TextFontSizePx      = 64.0f;
    ColorRgba          TextTint            { 61u, 111u, 217u, 255u };
    std::vector<PaintStampPlacement> Stamps          {};
    uint32_t                         SelectedStampId = 0u;

    // 3D Point Gradient parameters
    std::vector<PaintGradientPointSpec> GradientPoints              {};
    int32_t                             SelectedGradientPointIndex  = 0; // -1 = Background, 0..N-1 = Point
    ColorRgba                           GradientBackgroundTint      { 28u, 32u, 40u, 255u };
    float                               GradientBackgroundRoughness = 0.50f;
    float                               GradientBackgroundMetalness = 0.00f;
    float                               GradientBackgroundHeight    = 0.00f;
    float                               GradientFalloff             = 1.00f;

    // Hand-painted stroke dabs
    std::vector<PaintStrokeDabSpec> StrokeDabs {};

    PaintMaskConfig Mask {};

    [[nodiscard]] std::size_t CountActiveChannels() const noexcept;
    [[nodiscard]] std::string FormatSubtitle() const;
};

struct PaintCurvePoint
{
    float X = 0.0f;
    float Y = 1.0f;
};

struct PaintBrushProfile
{
    static constexpr std::size_t ChannelCount = static_cast<std::size_t>(PaintSurfaceChannel::Count);

    uint32_t                       ActivePresetIndex     = 6u; // Hog Bristle Flat #12
    BrushTipShape                  TipShape              = BrushTipShape::Chisel;
    ColorRgba                      InkColor              { 232u, 194u, 122u, 255u };
    ColorRgba                      SecondaryColor        { 47u, 109u, 117u, 255u };
    float                          SizeMeters            = 0.22f;
    int32_t                        PixelRadius           = 26;
    float                          Opacity               = 1.0f;
    float                          DeliveryRate          = 0.88f;
    float                          Hardness              = 0.76f;
    float                          TipAngleDeg           = 0.0f;
    float                          TipRoundness          = 1.0f;
    float                          Roughness             = 0.35f;
    float                          Metalness             = 0.0f;
    float                          HeightRelief          = 0.15f;

    float                          Spacing               = 0.14f;
    float                          Stabilizer            = 0.24f;
    float                          TaperSize             = 0.70f;
    float                          TaperOpacity          = 0.0f;
    float                          GrainDepth            = 0.85f;
    float                          GrainScale            = 0.45f;
    float                          WetDilution           = 0.0f;
    float                          WetPull               = 0.0f;
    bool                           FollowStrokeDirection = false;
    float                          SizeJitter            = 0.0f;
    float                          AngleJitterDeg        = 0.0f;
    float                          PositionScatter       = 0.0f;
    float                          DeliveryJitter        = 0.0f;
    uint32_t                       ColourDynamicRule     = 0u;
    uint32_t                       RampPresetIndex       = 0u;
    int32_t                        GradientSpanDabs      = 48;
    uint32_t                       GradientWrapStyle     = 0u;
    uint32_t                       OpacityDynamicRule    = 0u;
    float                          OpacityJitter         = 0.0f;
    float                          MinOpacityFloor       = 0.10f;
    float                          HueJitter             = 0.0f;
    float                          SatJitter             = 0.0f;
    float                          LightnessJitter       = 0.0f;
    bool                           HueShiftOverStroke    = false;

    // Eraser / Fill / Stamp / Text tool parameters
    int32_t                        EraserRadius          = 30;
    float                          EraserStrength        = 1.0f;
    float                          EraserHardness        = 0.70f;
    bool                           EraseWithBrush        = false;
    PaintFillScope                 FillScope             = PaintFillScope::AssetPart;
    int32_t                        FillTolerance         = 18;
    int32_t                        FillFeatherPx         = 1;
    std::string                    ActiveStampName       = "Warning Seal";
    ColorRgba                      StampTint             { 70u, 210u, 103u, 255u };
    int32_t                        StampSizePx           = 84;
    float                          StampRotationDeg      = 0.0f;
    std::string                    TextContent           = "Frontier";
    uint32_t                       TextFontIndex         = 0u;
    int32_t                        TextSizePx            = 64;
    int32_t                        TextLetterSpacingPx   = 0;
    float                          TextArcDeg            = 0.0f;
    int32_t                        MoveOffsetU           = 0;
    int32_t                        MoveOffsetV           = 0;
    int32_t                        MoveScalePct          = 100;
    float                          MoveRotationDeg       = 0.0f;

    BrushResponseCurve             ActiveCurve           = BrushResponseCurve::TipFalloff;
    std::array<std::vector<PaintCurvePoint>, 4u> Curves  {};

    std::array<bool, ChannelCount> PaintChannels         {};
    std::array<bool, ChannelCount> EraseChannels         {};
    std::array<bool, ChannelCount> FillChannels          {};
};

struct PaintDocumentSnapshot
{
    std::vector<PaintLayerEntry> Layers          {};
    uint32_t                     SelectedLayerId = 1u;
    uint32_t                     NextLayerId     = 3u;
    uint32_t                     ResolutionPx    = 512u;
};

struct PaintGeometryCacheStorage;

//------------------------------------------------------------------------------------------------------------------------
//                                                  SESSION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

class TexturePaintSession
{
public:
    static constexpr std::size_t ChannelCount  = static_cast<std::size_t>(PaintSurfaceChannel::Count);
    static constexpr std::size_t MaxLayerCount = 32u;

    std::string                  DocumentName      = "Teapot Paint";
    uint32_t                     ResolutionPx      = 512u;
    PaintStudyAsset              Study             = PaintStudyAsset::UtahTeapot;
    PaintViewportLayout          ViewportLayout    = PaintViewportLayout::ThreeDOnly;
    uint32_t                     ViewChannelIndex  = 0u; // 0 = Lit PBR, 1..22 = individual channel
    PaintToolCategory            ActiveTool        = PaintToolCategory::Brush;
    BrushInspectorTab            InspectorTab      = BrushInspectorTab::Brush;
    BrushDetailSubTab            InspectorSubTab   = BrushDetailSubTab::LibraryGrid;
    bool                         WireframeOverlay  = false;
    bool                         MeshPartsExpanded = false;
    PaintFilterTab               LayerFilter       = PaintFilterTab::AllLayers;
    std::string                  SearchQuery       {};
    std::string                  ChannelQuery      {};
    std::string                  BrushLibraryQuery {};
    std::string                  StatusBanner      = "Vulkan GPU";
    std::string                  InspectorStatus   = "Saved locally";

    // Collapsible group folds in Layer Inspector (Core, Surface, Coat, Optics, Emission)
    std::array<bool, 5u>         ChannelGroupOpen  { true, false, false, false, false };

    // 3D Orbit Camera
    float                        CameraYawRadians     = 0.68f;
    float                        CameraPitchRadians   = 0.34f;
    float                        CameraDistanceMeters = 2.10f;
    float                        CameraPivotX         = 0.0f;
    float                        CameraPivotY         = 0.0f;
    float                        CameraPivotZ         = 0.44f;

    std::vector<PaintLayerEntry> Layers            {};
    uint32_t                     SelectedLayerId   = 2u;
    uint32_t                     NextLayerId       = 3u;
    uint32_t                     NextStampId       = 1u;
    uint32_t                     NextGradientPtId  = 3u;
    uint64_t                     ContentRevision   = 1u;
    PaintBrushProfile            Brush             {};

    std::vector<PaintDocumentSnapshot> UndoHistory {};
    std::vector<PaintDocumentSnapshot> RedoHistory {};

    TexturePaintSession() noexcept;
    ~TexturePaintSession() noexcept;

    TexturePaintSession(const TexturePaintSession&)            = delete;
    TexturePaintSession& operator=(const TexturePaintSession&) = delete;

    void ResetDefaults() noexcept;
    void SelectTool(PaintToolCategory Tool) noexcept;
    void SelectStudy(PaintStudyAsset NextStudy) noexcept;
    void AssignResolution(uint32_t NextResPx) noexcept;
    void SelectBrushPreset(uint32_t PresetIndex) noexcept;
    void SelectTextFont(uint32_t FontIndex) noexcept;

    void AddLayer(PaintLayerCategory Category) noexcept;
    void DuplicateSelectedLayer() noexcept;
    void DeleteSelectedLayer() noexcept;
    bool MoveSelectedLayerUp() noexcept;
    bool MoveSelectedLayerDown() noexcept;
    void ToggleOrCreateMaskOnSelectedLayer() noexcept;

    void AddChannelToSelectedLayer(PaintSurfaceChannel Channel) noexcept;
    void RemoveChannelFromSelectedLayer(PaintSurfaceChannel Channel) noexcept;
    void ToggleChannelOnSelectedLayer(PaintSurfaceChannel Channel) noexcept;
    void ToggleChannelGroupOnSelectedLayer(PaintChannelGroup Group) noexcept;
    void ApplyChannelBulkPreset(uint32_t BulkMode) noexcept; // 0 = All, 1 = Core, 2 = One

    void AddStampToSelectedLayer(bool TextStampMode) noexcept;
    void RemoveStampFromSelectedLayer(uint32_t StampId) noexcept;

    void AddGradientPointToSelectedLayer() noexcept;
    void RemoveGradientPointFromSelectedLayer() noexcept;

    void OrbitCameraByDelta(float DeltaYawRad, float DeltaPitchRad) noexcept;
    void PanCameraByDelta(float RightMeters, float UpMeters) noexcept;
    void ZoomCameraByFactor(float ScaleMultiplier) noexcept;
    void ResetOrbitCamera() noexcept;

    void MarkContentModified() noexcept { ++ContentRevision; }

    [[nodiscard]] bool RenderSpatialViewportRgba8(
        uint32_t              WidthPx,
        uint32_t              HeightPx,
        std::vector<uint8_t>& OutRgba8);

    [[nodiscard]] bool RenderUvCanvasRgba8(
        uint32_t              WidthPx,
        uint32_t              HeightPx,
        std::vector<uint8_t>& OutRgba8);

    void RenderLayerThumbnailRgba8(
        const PaintLayerEntry& Layer,
        uint32_t               ThumbSizePx,
        std::vector<uint8_t>&  OutRgba8) const noexcept;

    [[nodiscard]] bool ProjectWorldPointToViewport(
        float   WorldX,
        float   WorldY,
        float   WorldZ,
        float   ViewportWidth,
        float   ViewportHeight,
        Point2D& OutViewportPoint) const noexcept;

    bool ApplyViewportPointerStroke(
        float NormX,
        float NormY,
        bool  InitialPress,
        bool  TwoDUvSpace) noexcept;

    void PushUndoSnapshot() noexcept;
    bool ApplyUndo() noexcept;
    bool ApplyRedo() noexcept;

    [[nodiscard]] PaintLayerEntry*       AccessSelectedLayer() noexcept;
    [[nodiscard]] const PaintLayerEntry* InspectSelectedLayer() const noexcept;
    [[nodiscard]] int32_t                FindLayerVectorIndex(uint32_t LayerId) const noexcept;
    [[nodiscard]] std::size_t            CountVisibleLayers() const noexcept;
    [[nodiscard]] std::size_t            CountHiddenLayers() const noexcept;
    [[nodiscard]] std::size_t            CountNonFolderLayers() const noexcept;
    [[nodiscard]] std::size_t            CountAttachedMasks() const noexcept;
    [[nodiscard]] std::size_t            CountPaintedChannels() const noexcept;
    [[nodiscard]] bool                   QueryMovableUp() const noexcept;
    [[nodiscard]] bool                   QueryMovableDown() const noexcept;
    [[nodiscard]] bool                   QueryUndoAvailable() const noexcept { return !UndoHistory.empty(); }
    [[nodiscard]] bool                   QueryRedoAvailable() const noexcept { return !RedoHistory.empty(); }
    [[nodiscard]] const PaintStudyDescriptor& QueryActiveStudyDescriptor() const noexcept;

    [[nodiscard]] static const std::array<PaintChannelDescriptor, ChannelCount>& QueryChannelRegistry() noexcept;
    [[nodiscard]] static const PaintChannelDescriptor& QueryChannelDescriptor(PaintSurfaceChannel Channel) noexcept;
    [[nodiscard]] static const std::array<const char*, 5u>& QueryChannelGroupLabels() noexcept;
    [[nodiscard]] static const std::array<PaintStudyDescriptor, 5u>& QueryStudyRegistry() noexcept;
    [[nodiscard]] static const std::array<const char*, 4u>& QueryTeapotPartLabels() noexcept;
    [[nodiscard]] static const std::array<BrushLibraryPresetDescriptor, 12u>& QueryBrushLibraryRegistry() noexcept;
    [[nodiscard]] static const std::array<const char*, 6u>& QueryFontLabels() noexcept;
    [[nodiscard]] static const std::array<const char*, 12u>& QueryMaterialPresetLabels() noexcept;
    [[nodiscard]] static const std::array<const char*, 23u>& QueryViewChannelLabels() noexcept;
    [[nodiscard]] static const std::array<const char*, 10u>& QueryBlendMethodLabels() noexcept;
    [[nodiscard]] static const std::array<ColorRgba, 8u>& QueryBrushQuickSwatches() noexcept;

private:
    void EnsureStudyGeometryPrepared();

    std::unique_ptr<PaintGeometryCacheStorage> GeometryCache {};
};

} // namespace Frontier::Workspaces
