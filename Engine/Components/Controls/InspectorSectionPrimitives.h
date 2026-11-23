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
    inline constexpr float     PaneInsetX          = 34.0f; // [px] - Horizontal pane inset (340 - 68 = 272px content)
    inline constexpr float     SectionRadius       = 25.0f; // [px] - Rounded section outer radius
    inline constexpr float     SectionPadTop       = 23.0f; // [px] - Section top inner padding
    inline constexpr float     SectionPadX         = 24.0f; // [px] - Section horizontal inner padding
    inline constexpr float     SectionPadBottom    = 22.0f; // [px] - Section bottom inner padding
    inline constexpr float     SectionGap          = 16.0f; // [px] - Vertical gap between sections
    inline constexpr float     PageStripHeight     = 34.0f; // [px] - Numbered page strip height
    inline constexpr float     QuickTileHeight     = 100.0f;// [px] - Quick switch tile height
    inline constexpr float     QuickTileRadius     = 17.0f; // [px] - Quick switch tile corner radius
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

// Shared on-demand retained RGBA32 texture surface for LUTs, sky/moon/cloud visuals, and specimen previews.
// Only re-rasterizes when RevisionSignature changes; otherwise presents the resident texture with zero per-tick work.
class InspectorPreviewSurface
{
public:
    InspectorPreviewSurface() noexcept = default;
    ~InspectorPreviewSurface() noexcept;

    InspectorPreviewSurface(const InspectorPreviewSurface&)            = delete;
    InspectorPreviewSurface& operator=(const InspectorPreviewSurface&) = delete;

    // Ensures the RGBA32 buffer matches (WidthPx, HeightPx) and RevisionSignature.
    // RasterCallback receives a mutable RGBA8 byte buffer of size WidthPx * HeightPx * 4.
    void EnsureUpdated(
        uint64_t                                          RevisionSignature,
        uint32_t                                          WidthPx,
        uint32_t                                          HeightPx,
        const std::function<void(std::vector<uint8_t>&)>& RasterCallback);

    void Present(
        PixelSpace& Surface,
        Rectangle2D Bounds,
        float       CornerRadius = 12.0f,
        ColorRgba   Tint         = { 255u, 255u, 255u, 255u }) noexcept;

    [[nodiscard]] bool     QueryResident() const noexcept { return ResidentTexture.Pixels != nullptr && WidthPx > 0u && HeightPx > 0u; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return RetainedSignature; }
    [[nodiscard]] uint32_t QueryWidthPx()  const noexcept { return WidthPx; }
    [[nodiscard]] uint32_t QueryHeightPx() const noexcept { return HeightPx; }
    [[nodiscard]] uint32_t QueryUploadCount() const noexcept { return UploadCounter; }

private:
    ImTextureData        ResidentTexture   {};
    std::vector<uint8_t> StagingPixels     {};
    uint64_t             RetainedSignature = 0ull;
    uint32_t             WidthPx           = 0u;
    uint32_t             HeightPx          = 0u;
    uint32_t             UploadCounter     = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace InspectorPrimitives
{

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

// Exact 1:1 Baked LUT / Equirectangular texture preview section (Baking pages)
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
    float                    PreviewHeightPx) noexcept;

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

// Compact labelled key-value metadata row inside a section
[[nodiscard]] float ConstructMetadataRow(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* Label,
    const char* ReadingText) noexcept;

// Full-width interactive pill action button inside a section
[[nodiscard]] bool ConstructSectionActionButton(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Label,
    const PaneInputReading& Input,
    bool                    Accent = false) noexcept;

// Bottom 28px inspector footer bar
void ConstructInspectorFooter(
    PixelSpace& Surface,
    Rectangle2D FooterBounds,
    const char* LeftCaption,
    const char* RightCaption) noexcept;

} // namespace InspectorPrimitives

using InspectorPrimitives::GeometrySurfaceBinding;

} // namespace Frontier
