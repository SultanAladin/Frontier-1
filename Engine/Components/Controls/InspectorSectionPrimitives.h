/*============================================================================================================================================
                                                      INSPECTORSECTIONPRIMITIVES.H
============================================================================================================================================*/
// 🧩 Generic reusable Inspector UI building blocks (1:1 HTML parity): page strip, property sections, quick switches, metrics, fields & retained previews

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/RetainedCanvasCache.h"
#include "Engine/Mathematics/LinearColors.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InspectorSwitchSymbol : uint8_t
{
    Eye          = 0u, // [-] - Visibility eye symbol
    Sun          = 1u, // [-] - Solar rays symbol
    Clock        = 2u, // [-] - Real-time clock symbol
    Cycle        = 3u, // [-] - Cyclic arrow symbol
    Compass      = 4u, // [-] - Directional compass symbol
    Orbit        = 5u, // [-] - Planetary orbit ring symbol
    LimbLine     = 6u, // [-] - Civil twilight limb line symbol
    Cloud        = 7u, // [-] - Cloud silhouette symbol
    Stars        = 8u, // [-] - Four-point star sparkle symbol
    Twinkle      = 9u, // [-] - Scintillation sparkle symbol
    Galaxy       = 10u,// [-] - Spiral Milky Way band symbol
    Zodiac       = 11u,// [-] - Zodiacal light diamond symbol
    Wind         = 12u,// [-] - Streamline wind waves symbol
    Fog          = 13u,// [-] - Layered fog waves symbol
    LightBulb    = 14u,// [-] - Photometric luminaire symbol
    CameraLens   = 15u,// [-] - Optical camera lens symbol
    Cube         = 16u,// [-] - Geometric primitive cube symbol
    Shadow       = 17u,// [-] - Cast-shadow projection symbol
    Physics      = 18u,// [-] - Dynamic physics motion symbol
    PostFx       = 19u,// [-] - Optical post-effect aperture symbol
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

namespace InspectorMetrics
{
    inline constexpr float     PaneInsetX          = 18.0f; // [px] - Responsive baseline horizontal pane inset
    inline constexpr float     SectionRadius       = 12.0f; // [px] - Subtle precision section corner radius
    inline constexpr float     SectionPadTop       = 20.0f; // [px] - Section top inner padding
    inline constexpr float     SectionPadX         = 18.0f; // [px] - Section horizontal inner padding
    inline constexpr float     SectionPadBottom    = 18.0f; // [px] - Section bottom inner padding
    inline constexpr float     SectionGap          = 12.0f; // [px] - Vertical gap between sections
    inline constexpr float     PageStripHeight     = 34.0f; // [px] - Numbered page strip height
    inline constexpr float     QuickTileHeight     = 92.0f; // [px] - Quick switch tile baseline height
    inline constexpr float     QuickTileRadius     = 9.0f;  // [px] - Subtle quick switch tile corner radius
    inline constexpr float     PreviewRadius       = 8.0f;  // [px] - Subtle preview surface corner radius
    inline constexpr float     ControlRadius       = 8.0f;  // [px] - Subtle input / dropdown corner radius
    inline constexpr float     SplitWidth          = 92.0f; // [px] - Split numeric + unit capsule width
    inline constexpr float     SplitHeight         = 28.0f; // [px] - Split numeric + unit capsule height
    inline constexpr float     ToggleWidth         = 44.0f; // [px] - Binary switch track width
    inline constexpr float     ToggleHeight        = 24.0f; // [px] - Binary switch track height

    inline constexpr ColorRgba SectionFill         { 32u,  32u,  32u,  255u }; // [-] - rgb(32, 32, 32)
    inline constexpr ColorRgba SectionBorder       { 255u, 255u, 255u, 8u   }; // [-] - rgba(255, 255, 255, 0.03)
    inline constexpr ColorRgba HeadingInk          { 202u, 202u, 202u, 255u }; // [-] - rgb(202, 202, 202)
    inline constexpr ColorRgba MutedInk            { 145u, 145u, 145u, 255u }; // [-] - rgb(145, 145, 145)
    inline constexpr ColorRgba DimInk              { 108u, 108u, 108u, 255u }; // [-] - rgb(108, 108, 108)
    inline constexpr ColorRgba PageStepFill        { 28u,  28u,  31u,  255u }; // [-] - rgb(28, 28, 31)
    inline constexpr ColorRgba PageRailFill        { 20u,  20u,  22u,  255u }; // [-] - rgb(20, 20, 22)
    inline constexpr ColorRgba PageTabOpenFill     { 52u,  52u,  58u,  255u }; // [-] - rgb(52, 52, 58)
    inline constexpr ColorRgba PageNumOpenFill     { 236u, 236u, 239u, 255u }; // [-] - rgb(236, 236, 239)
    inline constexpr ColorRgba PageNumClosedFill   { 42u,  42u,  46u,  255u }; // [-] - rgb(42, 42, 46)
    inline constexpr ColorRgba QuickActiveGreen    { 91u,  219u, 135u, 255u }; // [-] - rgb(91, 219, 135)
    inline constexpr ColorRgba QuickWellActiveFill { 26u,  58u,  38u,  255u }; // [-] - Emerald active switch icon well
    inline constexpr ColorRgba QuickWellIdleFill   { 44u,  44u,  47u,  255u }; // [-] - Neutral inactive switch icon well
    inline constexpr ColorRgba ToggleOnFill        { 52u,  199u, 89u,  255u }; // [-] - rgb(52, 199, 89)
    inline constexpr ColorRgba ToggleOffFill       { 60u,  60u,  60u,  255u }; // [-] - rgb(60, 60, 60)
    inline constexpr ColorRgba TagFill             { 36u,  48u,  63u,  255u }; // [-] - rgb(36, 48, 63)
    inline constexpr ColorRgba TagInk              { 188u, 216u, 255u, 255u }; // [-] - rgb(188, 216, 255)
    inline constexpr ColorRgba NotesFill           { 22u,  22u,  22u,  255u }; // [-] - rgb(22, 22, 22)
    inline constexpr ColorRgba NotesBorder         { 51u,  51u,  51u,  255u }; // [-] - rgb(51, 51, 51)
    inline constexpr ColorRgba ChamberDark         { 4u,   5u,   8u,   255u }; // [-] - Deep dark optical chamber
} // namespace InspectorMetrics

struct InspectorQuickSwitchSpec
{
    const char*           Label    = "";
    InspectorSwitchSymbol Symbol   = InspectorSwitchSymbol::Eye;
    bool                  Active   = false;
    bool                  Disabled = false;
};

struct InspectorKpiMetricSpec
{
    const char* ReadingText = "0";
    const char* UnitText    = "";
    const char* CaptionText = "";
};

enum class InspectorGpuBakeMode : uint32_t
{
    AtmosphereLimb          = 0u,
    AtmosphereGlobe         = 1u,
    TransmittanceLut        = 2u,
    SkyViewLut              = 3u,
    SolarBake               = 4u,
    CloudSwath              = 5u,
    LunarPhase              = 6u,
    StarfieldSheet          = 7u,
    FogLayer                = 8u,
    RainbowArc              = 9u,
    EnvironmentDome         = 10u,
    GeometrySdfSlice        = 11u,
    GeometryUvAtlas         = 12u,
    LensFlarePreview        = 13u,
    CameraBodyPreview       = 14u,
    RendererHeroPreview     = 15u,
    PhotometricLightPreview = 16u,
    GeometrySpecimenPreview = 17u,
};

struct InspectorGpuBakeSpec
{
    InspectorGpuBakeMode Mode       = InspectorGpuBakeMode::AtmosphereLimb;
    uint32_t             Flags      = 0u;
    float                ParamA[4]  { 1.0f, 1.0f, 1.0f, 30.0f };
    float                ParamB[4]  { 0.0f, 0.0f, 0.0f, 0.0f };
    float                ColourA[4] { 0.8f, 0.85f, 0.92f, 1.0f };
    float                ColourB[4] { 0.1f, 0.14f, 0.22f, 1.0f };
};

enum class InspectorPreviewFitMode : uint8_t
{
    Contain = 0, // [-] - Preserve 1:1 aspect inside Bounds (zero stretch, zero clip)
    Cover   = 1, // [-] - Fill Bounds edge-to-edge with aspect-preserving centered UV crop (zero stretch)
    Stretch = 2, // [-] - Full UV [0,1]x[0,1] mapping (for 2D LUT heatmaps)
};

// Shared on-demand retained RGBA32 texture surface for LUTs, sky/moon/cloud visuals, and specimen previews.
// Only re-bakes when RevisionSignature changes; dispatches on Vulkan GPU compute whenever attached, and throttles drag updates.
class InspectorPreviewSurface
{
public:
    using GpuBakeHookFn          = std::function<bool(const InspectorGpuBakeSpec&, uint32_t, uint32_t, std::vector<uint8_t>&)>;
    using GpuDirectBakeHookFn    = std::function<bool(uint64_t, const InspectorGpuBakeSpec&, uint32_t, uint32_t, uint64_t&)>;
    using GpuDirectReleaseHookFn = std::function<void(uint64_t)>;

    InspectorPreviewSurface() noexcept = default;
    ~InspectorPreviewSurface() noexcept;

    InspectorPreviewSurface(const InspectorPreviewSurface&)            = delete;
    InspectorPreviewSurface& operator=(const InspectorPreviewSurface&) = delete;

    static void AssignGpuBakeHook(GpuBakeHookFn Hook);
    static void ClearGpuBakeHook() noexcept;
    [[nodiscard]] static bool QueryGpuBakeHookAttached() noexcept;

    static void AssignGpuDirectBakeHook(GpuDirectBakeHookFn Hook, GpuDirectReleaseHookFn ReleaseHook = {});
    static void ClearGpuDirectBakeHook() noexcept;
    [[nodiscard]] static bool QueryGpuDirectBakeHookAttached() noexcept;

    // Ensures the RGBA32 buffer matches (WidthPx, HeightPx) and RevisionSignature.
    // RasterCallback receives a mutable RGBA8 byte buffer of size WidthPx * HeightPx * 4.
    void EnsureUpdated(
        uint64_t                                          RevisionSignature,
        uint32_t                                          WidthPx,
        uint32_t                                          HeightPx,
        const std::function<void(std::vector<uint8_t>&)>& RasterCallback);

    // Pure-GPU first path: dispatches InspectorPreviewCompute.comp.spv directly into a zero-copy GPU ImGui VkImage
    // when available (zero CPU readback / re-upload bounce), falling back to staging GPU or headless CPU callback.
    void EnsureUpdatedGpu(
        uint64_t                                          RevisionSignature,
        uint32_t                                          WidthPx,
        uint32_t                                          HeightPx,
        const InspectorGpuBakeSpec&                       GpuSpec,
        const std::function<void(std::vector<uint8_t>&)>& CpuFallbackCallback);

    void Present(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        float                   CornerRadius = InspectorMetrics::PreviewRadius,
        ColorRgba               Tint         = { 255u, 255u, 255u, 255u },
        InspectorPreviewFitMode FitMode      = InspectorPreviewFitMode::Contain,
        ColorRgba               ChamberFill  = InspectorMetrics::ChamberDark) noexcept;

    [[nodiscard]] bool     QueryResident() const noexcept { return (DirectGpuTextureHandle != 0ull || ResidentTexture.Pixels != nullptr) && WidthPx > 0u && HeightPx > 0u; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return RetainedSignature; }
    [[nodiscard]] uint32_t QueryWidthPx()  const noexcept { return WidthPx; }
    [[nodiscard]] uint32_t QueryHeightPx() const noexcept { return HeightPx; }
    [[nodiscard]] uint32_t QueryUploadCount() const noexcept { return UploadCounter; }
    [[nodiscard]] uint32_t QueryGpuUploadCount() const noexcept { return GpuUploadCounter; }
    [[nodiscard]] uint32_t QueryDirectGpuUploadCount() const noexcept { return DirectGpuUploadCounter; }
    [[nodiscard]] uint32_t QueryCpuUploadCount() const noexcept { return CpuUploadCounter; }

private:
    void UploadStagingPixels(uint64_t RevisionSignature, uint32_t TargetWidthPx, uint32_t TargetHeightPx, bool SizeMismatch, bool BakedOnGpu);

    ImTextureData        ResidentTexture        {};
    std::vector<uint8_t> StagingPixels          {};
    uint64_t             DirectGpuTextureHandle = 0ull;
    uint64_t             SurfaceUniqueId        = 0ull;
    uint64_t             RetainedSignature      = 0ull;
    uint32_t             WidthPx                = 0u;
    uint32_t             HeightPx               = 0u;
    uint32_t             UploadCounter          = 0u;
    uint32_t             GpuUploadCounter       = 0u;
    uint32_t             DirectGpuUploadCounter = 0u;
    uint32_t             CpuUploadCounter       = 0u;
    uint64_t             LastBakeTimeNs         = 0ull;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace InspectorPrimitives
{

[[nodiscard]] Rectangle2D ComputeAspectFitRect(Rectangle2D Bounds, uint32_t TextureWidthPx, uint32_t TextureHeightPx) noexcept;

[[nodiscard]] uint64_t MixRevisionU64(uint64_t Seed, uint64_t Bits) noexcept;
[[nodiscard]] uint64_t MixRevisionFloat(uint64_t Seed, float Reading) noexcept;
[[nodiscard]] ColorRgba EncodeLinearRgbToSrgb8(const Mathematics::LinearRGB& Linear) noexcept;

void ConstructSwitchSymbol(
    PixelSpace&           Surface,
    InspectorSwitchSymbol Symbol,
    Point2D               Center,
    float                 Diameter,
    ColorRgba             Ink,
    ColorRgba             WellFill) noexcept;

// Top breadcrumb ("Inspector / Environment"), large Light title, and muted subtitle
[[nodiscard]] float ConstructInspectorHeader(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* Breadcrumb,
    const char* Title,
    const char* Subtitle) noexcept;

// Exact 1:1 port of PageCarousel.jsx + InspectorTabs.jsx: < | (1) Overview (2) ... | > | 1 / N
[[nodiscard]] float ConstructPageStrip(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    std::span<const char* const> PageLabels,
    uint32_t&                    InOutActivePage,
    const PaneInputReading&      Input,
    bool&                        OutPageSwitched) noexcept;

// Exact 1:1 rounded property section surface (25px radius, rgb(32,32,32), 23/24/22 padding)
void ConstructSectionSurface(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* Title,
    const char* RightText   = nullptr,
    ColorRgba   RightInk    = InspectorMetrics::MutedInk) noexcept;

// Large Light metric reading + muted unit + optional caption
[[nodiscard]] float ConstructMetricReadout(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    const char* NumberText,
    const char* UnitText,
    const char* CaptionText = nullptr,
    float       NumberSize  = 30.0f) noexcept;

// 2x2 KPI metric tiles (Clouds Overview: COVERAGE / OPTICAL / BASE / DRIFT)
[[nodiscard]] float ConstructKpiMetricGrid(
    PixelSpace&                             Surface,
    float                                   LeftX,
    float                                   TopY,
    float                                   InnerWidth,
    std::span<const InspectorKpiMetricSpec> Metrics) noexcept;

// Exact 1:1 Quick controls section with green "N / M on" count & 106x100 sheen tiles
[[nodiscard]] float ConstructQuickSwitchSection(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    std::span<const InspectorQuickSwitchSpec> Switches,
    const PaneInputReading&                   Input,
    int32_t&                                  OutClickedIndex) noexcept;

// Exact 1:1 .field + .split-value (92x28) + horizontal slider row
[[nodiscard]] float ConstructScalarFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    uint32_t                ControlId,
    const char*             Label,
    const char*             UnitText,
    const char*             FormatSpec,
    float                   Minimum,
    float                   Maximum,
    float&                  InOutReading,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled = false) noexcept;

// Exact 1:1 .switch-row + 44x24 .toggle switch
[[nodiscard]] float ConstructToggleFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Label,
    bool&                   InOutEnabled,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled = false) noexcept;

// Exact 1:1 .colour-field with live swatch + hex readout + inline R/G/B adjust sliders
[[nodiscard]] float ConstructColourFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    uint32_t                ControlOriginId,
    const char*             Label,
    Mathematics::LinearRGB& InOutColour,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled = false) noexcept;

// Compact multi-choice segmented / grid selector row
[[nodiscard]] float ConstructChoiceRow(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    const char*                  Label,
    std::span<const char* const> Choices,
    uint32_t                     ActiveIndex,
    uint32_t                     ColumnCount,
    const PaneInputReading&      Input,
    int32_t&                     OutChosenIndex) noexcept;

// Interactive optical/signal curve plot box
void ConstructCurvePlot(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    std::span<const float> NormalizedY,
    ColorRgba              StrokeInk,
    ColorRgba              UnderFill,
    const char*            LeftCaption     = nullptr,
    const char*            RightCaption    = nullptr,
    float                  MarkerX01       = -1.0f) noexcept;

// Floating hover tooltip fact row (used by Baked LUT capsules and field help triggers)
struct InspectorTooltipFact
{
    std::string Label   {};
    std::string Reading {};
};

// Clear queued hover tooltip at the start of an Inspector frame
void ResetTooltipQueue() noexcept;

// Queue a simple text tooltip anchored near AnchorRect (rendered on top of the pane by FlushTooltipOverlay)
void QueueTooltip(Rectangle2D AnchorRect, std::string_view Heading, std::string_view BodyText) noexcept;

// Queue a structured multi-row fact tooltip (F76 Baked LUT details tooltip parity)
void QueueFactTooltip(
    Rectangle2D                             AnchorRect,
    std::string_view                        Heading,
    std::span<const InspectorTooltipFact>   Facts) noexcept;

// Render the active queued tooltip floating cleanly inside PaneBounds (never clipped by cards)
void FlushTooltipOverlay(PixelSpace& Surface, Rectangle2D PaneBounds) noexcept;

// Small circular '?' hover trigger that queues a tooltip when hovered
void ConstructTooltipTrigger(
    PixelSpace&             Surface,
    Point2D                 Center,
    std::string_view        Heading,
    std::string_view        BodyText,
    const PaneInputReading& Input) noexcept;

// Exact 1:1 F76 Baked LUT / Equirectangular texture preview section (Baking pages)
// Includes green Baked capsule with hover details tooltip, resolution readout, Rebake & Save icon buttons, and optional Bake Settings face
[[nodiscard]] float ConstructBakedLutSection(
    PixelSpace&              Surface,
    float                    LeftX,
    float                    TopY,
    float                    InnerWidth,
    const char*              Title,
    const char*              ResolutionText,
    const char*              FormatText,
    const char*              AxisCaption,
    InspectorPreviewSurface& Preview,
    float                    PreviewHeightPx,
    const PaneInputReading*  OptionalInput          = nullptr,
    bool*                    OptionalSettingsOpen   = nullptr,
    uint32_t*                OptionalScaleTier      = nullptr,
    bool*                    OptionalRebakeClicked  = nullptr,
    bool*                    OptionalSaveClicked    = nullptr) noexcept;

// Exact 1:1 Tags section (.tags-card with #24303F / #BCD8FF chips)
[[nodiscard]] float ConstructTagsSection(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    std::span<const char* const> TagLabels) noexcept;

// Exact 1:1 Notes section (.notes-card bound to OutlinerRecordEntry::Notes)
[[nodiscard]] float ConstructNotesSection(
    PixelSpace&  Surface,
    float        LeftX,
    float        TopY,
    float        InnerWidth,
    std::string& NotesText) noexcept;

// Shared OpenPBR surface binding edited by all 4 geometry inspector categories
struct GeometrySurfaceBinding
{
    Mathematics::LinearRGB AlbedoTint          { 0.78f, 0.76f, 0.72f }; // [lin] - OpenPBR diffuse reflectance
    float                  Roughness           = 0.36f;                 // [0-1] - Isotropic GGX micro-roughness
    float                  Metallic            = 0.10f;                 // [0-1] - Conductor fraction
    float                  SelfGlow            = 0.0f;                  // [x]   - Self-illuminance gain
    uint16_t               MaterialSlotOrdinal = 0u;                    // [idx] - Bound CompactMaterialRegister slot
};

// Shared Position / Rotation / Scale sections (used by all 4 geometry categories)
[[nodiscard]] float ConstructTransformSections(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    uint32_t                ControlOriginId,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept;

// Shared OpenPBR surface material section (used by all 4 geometry categories)
[[nodiscard]] float ConstructMaterialSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    GeometrySurfaceBinding& Binding,
    uint32_t                ControlOriginId,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept;

// Shared Physics & visibility section (used by all 4 geometry categories)
[[nodiscard]] float ConstructPhysicsSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    const char*             CollisionSummary,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept;

// 1:1 HTML split-precision KPI metric (bright white integer part + dimmed fractional digits + unit + caption)
[[nodiscard]] float ConstructSplitMetricReadout(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    const char* WholePart,
    const char* FractionalPart,
    const char* UnitText,
    const char* CaptionText) noexcept;

// 1:1 HTML dark rounded stat tile (#121316 fill, 12px radius, uppercase caption + bright reading)
void ConstructStatTile(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* TopCaption,
    const char* PrimaryReading,
    const char* UnitSuffix = "",
    bool        CaptionBelow = false) noexcept;

// 1:1 HTML side-by-side 2-column summary boxes (e.g. Checks passed / Warnings or Visible / Hidden)
[[nodiscard]] float ConstructDualStatusBoxes(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* LeftLabel,
    const char* LeftCount,
    ColorRgba   LeftAccent,
    bool        LeftCheckMark,
    const char* RightLabel,
    const char* RightCount,
    ColorRgba   RightAccent,
    bool        RightExclaimMark) noexcept;

// Compact labelled key-value metadata row inside a section
[[nodiscard]] float ConstructMetadataRow(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* Label,
    const char* ReadingText) noexcept;

// Exact 1:1 HTML .rc-segments pill-track selector (#121212 track, #e6e6e6 active white pill with #141414 ink)
[[nodiscard]] float ConstructSegmentTrack(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    const char*                  UppercaseCaption,
    std::span<const char* const> Choices,
    uint32_t                     ActiveIndex,
    const PaneInputReading&      Input,
    int32_t&                     OutChosenIndex) noexcept;

// Exact 1:1 HTML clickable summary navigation row ("Shadows"  "Voxel + Cone GI · 24 steps  >")
[[nodiscard]] float ConstructNavigationRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             LeftLabel,
    const char*             RightSummary,
    bool                    ShowBottomDivider,
    const PaneInputReading& Input,
    bool&                   OutClicked) noexcept;

// Full-width interactive pill action button inside a section
[[nodiscard]] bool ConstructSectionActionButton(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Label,
    const PaneInputReading& Input,
    bool                    Accent = false) noexcept;

// Blackbody colour temperature tint [0..1] (Tanner Helland / ProjectZeroEditor parity)
[[nodiscard]] Mathematics::LinearRGB ComputeKelvinRgb(float Kelvin) noexcept;

// Shared Shadows & Scene Participation section for standalone luminaire inspectors
[[nodiscard]] float ConstructLuminaireShadowsSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    bool                    SupportsShadowCasting,
    const char*             HelperToggleLabel,
    bool&                   InOutHelperVisible,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept;

// Shared Geometry workspace pages matching PrimitiveInspector.jsx (UV, Fracture, SDF, LOD)
[[nodiscard]] float ConstructUvWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    uint32_t    UvChannelCount,
    const char* ProjectionLabel) noexcept;

[[nodiscard]] float ConstructFractureWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    uint32_t    CellCount,
    const char* ModeLabel) noexcept;

[[nodiscard]] float ConstructSdfWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    bool        SdfActive,
    const char* BoundsLabel) noexcept;

[[nodiscard]] float ConstructLodWorkspaceSection(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    uint32_t    TriangleCount,
    uint32_t    ClusterCount) noexcept;

// Exact HTML LiveGraph.jsx <Plot> series descriptor (supports up to 4 curves per interactive plot)
struct InspectorLivePlotSeries
{
    const char*                  Name    = "";
    ColorRgba                    Ink     { 158u, 184u, 208u, 255u };
    std::function<float(float)>  Sample  {};
};

// Word-wrapped body paragraph matching HTML .graph-note / .property-card > p
[[nodiscard]] float ConstructWrappedParagraph(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       MaxWidth,
    float       FontSize,
    float       LineStep,
    ColorRgba   Ink,
    const char* Text) noexcept;

// Dashed line stroke helper
void ConstructDashedSegment(
    PixelSpace& Surface,
    Point2D     Start,
    Point2D     End,
    ColorRgba   Ink,
    float       DashLength,
    float       GapLength,
    float       StrokeWidth = 1.0f) noexcept;

// Exact HTML LiveGraph.jsx <Plot> interactive curve plot (header readout + 215px SVG grid/fill/curve/probe + wrapped note)
[[nodiscard]] float ConstructLivePlot(
    PixelSpace&                                Surface,
    float                                      LeftX,
    float                                      TopY,
    float                                      InnerWidth,
    uint32_t                                   ControlId,
    float                                      DomainMin,
    float                                      DomainMax,
    float                                      RangeMin,
    float                                      RangeMax,
    std::span<const InspectorLivePlotSeries>   Series,
    float&                                     InOutProbeX,
    const char*                                UnitX,
    const char*                                UnitY,
    const char*                                NoteText,
    uint32_t&                                  InOutActiveDragId,
    const PaneInputReading&                    Input,
    bool&                                      OutChanged) noexcept;

// Exact HTML "Bake / image" card (rainbow__2, atmosphere, stars, lens-flare): left status tile + Bake/Load buttons + footer note
[[nodiscard]] float ConstructBakeImageCard(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    bool                    UseBakedImage,
    const char*             StatusCaption,
    const char*             RightCaption,
    const char*             FooterNote,
    const PaneInputReading& Input,
    bool&                   OutBakeClicked,
    bool&                   OutLoadClicked) noexcept;

// Frame capture scope for hover-zone dirty gating, scroll-clip culling, and drag-cadence bake throttling
void BeginInspectorCaptureScope(
    RetainedCanvasCache* ActiveCache,
    Rectangle2D          ScrollClipLogical,
    Point2D              SurfaceOriginPhysical,
    float                ScaleFactor) noexcept;
void EndInspectorCaptureScope() noexcept;
void RegisterInteractiveRect(Rectangle2D LogicalBounds) noexcept;
[[nodiscard]] bool QueryVisibleInScrollClip(Rectangle2D LogicalBounds) noexcept;
void NoteInteractiveDragActive(bool Active) noexcept;
[[nodiscard]] bool QueryInteractiveDragActive() noexcept;

// Bottom 45px inspector footer bar
void ConstructInspectorFooter(
    PixelSpace& Surface,
    Rectangle2D FooterBounds,
    const char* LeftCaption,
    const char* RightCaption) noexcept;

} // namespace InspectorPrimitives

using InspectorPrimitives::GeometrySurfaceBinding;

} // namespace Frontier
