/*==========================================================================================================================================
                                                        PATTERNSTAMPSTACKSESSION.H
==========================================================================================================================================*/
// 📊 Unified non-destructive vector layer-stack and modifier-stack document session shared by Pattern Studio and Stamp Studio.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/VectorCodec.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.h"
#include "Engine/ParticleSpace/Sprites/ProceduralSpriteSynthesizer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PatternStampWorkspaceMode : uint8_t
{
    Pattern = 0, // [-] - Seamless tileable pattern design workspace
    Stamp   = 1, // [-] - Transparent alpha cutout decal stamp workspace
};

enum class PatternStampLayerCategory : uint8_t
{
    Motif        = 0, // [-] - Geometric / procedural vector motif
    Typographic  = 1, // [-] - Engraved label / typographic block
    GradientWash = 2, // [-] - Full-tile linear / radial / point field gradient wash
    Folder       = 3, // [-] - Nestable grouping folder
};

enum class PatternStampMotifShape : uint8_t
{
    Circle          = 0,
    Ring            = 1,
    Square          = 2,
    RoundedBox      = 3,
    Diamond         = 4,
    Hexagon         = 5,
    Triangle        = 6,
    Star            = 7,
    Cross           = 8,
    Chevron         = 9,
    StripeBar       = 10,
    Arc             = 11,
    Teardrop        = 12,
    Leaf            = 13,
    FivePetalRosette = 14,
    CogGear         = 15,
    LineStroke      = 16,
    VectorPath      = 17,
    Count           = 18,
};

enum class PatternStampToolCategory : uint8_t
{
    Select        = 0,
    Curve         = 1,
    Pen           = 2,
    EditPoints    = 3,
    PointGradient = 4,
    Square        = 5,
    Circle        = 6,
    Diamond       = 7,
    Triangle      = 8,
    Star          = 9,
    Hexagon       = 10,
    Stripe        = 11,
    TextBlock     = 12,
    HandPan       = 13,
    Count         = 14,
};

enum class PatternStampViewChannel : uint8_t
{
    VectorSVG = 0,
    Seamless2x2,
    StackedPbrTile,
    Albedo,
    Roughness,
    Metalness,
    Height,
    Normal,
    Occlusion,
    Opacity,
    Emissive,
    GlowPower,
    Clearcoat,
    CoatRough,
    Sheen,
    SheenRough,
    Specular,
    Anisotropy,
    Iridescence,
    IridFilm,
    Transmission,
    Subsurface,
    SpecColour,
    SheenColour,
    SubsurfTint,
    Count,
};

enum class PatternStampFillSourceMode : uint8_t
{
    Solid          = 0,
    LinearGradient = 1,
    RadialGradient = 2,
    PointField     = 3,
    Count          = 4,
};

enum class PatternStampFinishPreset : uint8_t
{
    PrintedDye     = 0,
    WovenCotton    = 1,
    CutPileWool    = 2,
    GlazedCeramic  = 3,
    MetalInlay     = 4,
    EmbossedRubber = 5,
    Count          = 6,
};

enum class PatternStampSurfaceChannelKey : uint8_t
{
    Albedo = 0,
    Roughness,
    Metalness,
    Height,
    NormalStr,
    Occlusion,
    Opacity,
    Emissive,
    GlowPower,
    Clearcoat,
    CoatRough,
    Sheen,
    SheenRough,
    Specular,
    Anisotropy,
    Iridescence,
    IridFilm,
    Transmission,
    Subsurface,
    SpecColour,
    SheenColour,
    SubsurfTint,
    Count,
};

enum class PatternStampModifierCategory : uint8_t
{
    NoiseScale     = 0,
    GridArray      = 1,
    RadialArray    = 2,
    MirrorSymmetry = 3,
    InsetEcho      = 4,
    ScatterDropout = 5,
    DomainWarp     = 6,
    BevelEdgeWear  = 7,
    Count          = 8,
};

enum class PatternStampMaskMode : uint8_t
{
    Noise  = 0,
    Radial = 1,
    Stripe = 2,
    Count  = 3,
};

enum class PatternStampLayerFilterMode : uint8_t
{
    AllLayers   = 0,
    VisibleOnly = 1,
    HiddenOnly  = 2,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PatternStampSurfaceChannelDescriptor
{
    PatternStampSurfaceChannelKey Key          = PatternStampSurfaceChannelKey::Albedo;
    const char*              Label        = "Albedo";
    const char*              ShortLabel   = "Albedo";
    const char*              Subtitle     = "Base Colour";
    bool                     ColorChannel = false;
    bool                     Extended     = false;
    float                    MinScalar    = 0.0f;
    float                    MaxScalar    = 1.0f;
    float                    StepScalar   = 0.01f;
    float                    DefaultScalar = 0.5f;
    ColorRgba                DefaultTint  = { 232u, 194u, 122u, 255u };
};

struct PatternStampSurfaceChannelEntry
{
    PatternStampSurfaceChannelKey Key           = PatternStampSurfaceChannelKey::Albedo;
    float                    ScalarReading = 0.5f;
    ColorRgba                Tint          = { 232u, 194u, 122u, 255u };
};

struct PatternStampGradientStop
{
    ColorRgba Tint   = { 239u, 244u, 207u, 255u };
    float     Offset = 0.0f;
};

struct PatternStampLayerMaskSpec
{
    bool            Enabled   = false;
    bool            Invert    = false;
    PatternStampMaskMode Mode      = PatternStampMaskMode::Noise;
    float           Frequency = 4.0f;
    float           Threshold = 0.45f;
    float           Softness  = 0.25f;
    int32_t         Seed      = 211;
};

struct PointFieldPinSpec
{
    float     NormX  = 0.5f;
    float     NormY  = 0.5f;
    float     Weight = 1.0f;
    ColorRgba Tint   { 239u, 244u, 207u, 255u };
    bool      Active = false;
};

struct PatternStampModifierSpec
{
    PatternStampModifierCategory Category = PatternStampModifierCategory::NoiseScale;
    bool                Enabled = true;

    // Noise Scale fields
    uint8_t   NoiseAlgorithm = 0u; // 0: FBM, 1: Perlin, 2: Worley, 3: Ridged, 4: Radial
    uint8_t   ModulateTarget = 0u; // 0: Scale + Color, 1: Scale Only, 2: Scale + Opacity, 3: Scale + Relief
    float     MinScale       = 0.40f;
    float     MaxScale       = 1.60f;
    float     Frequency      = 3.5f;
    ColorRgba TargetTintB    = { 118u, 178u, 216u, 255u };

    // Grid / Hex Array fields
    uint8_t GridLayout = 2u; // 0: Grid, 1: Brick, 2: Hex, 3: Half-drop
    int32_t Columns    = 8;
    int32_t Rows       = 8;

    // Radial Array fields
    int32_t RadialCount = 8;
    float   RadiusPx    = 110.0f;
    int32_t Rings       = 1;

    // Mirror Symmetry fields
    uint8_t MirrorAxis = 2u; // 0: X, 1: Y, 2: Quad, 3: Diagonal

    // Inset / Echo fields
    int32_t   EchoCopies = 3;
    float     InsetStepPx = 24.0f;
    ColorRgba AltTint    = { 24u, 27u, 31u, 255u };

    // Scatter & Dropout fields
    float PosJitterPx = 16.0f;
    float RotJitterDeg = 25.0f;
    float Dropout      = 0.15f;

    // Domain Warp fields
    uint8_t WarpMode     = 0u; // 0: Wave, 1: Swirl, 2: Curl, 3: Pinch
    float   WarpAmplitude = 24.0f;

    // Bevel & Edge Wear fields
    float ChipWearAmount = 0.25f;
};

struct PatternStampLayerSpec
{
    uint32_t         Identity          = 0u;
    uint32_t         EnclosingFolderId = 0u; // 0 = top-level (no Enclosing/Nested wording)
    std::string      Name              {};
    PatternStampLayerCategory Category          = PatternStampLayerCategory::Motif;
    bool             Visible           = true;
    bool             Locked            = false;
    bool             ExpandedInStack   = false;
    bool             CollapsedFolder   = false;

    uint8_t               BlendMode         = 0u; // 0: Normal, 1: Multiply, 2: Screen, 3: Overlay, 4: Add, 5: Darken
    float                 Opacity           = 1.0f;
    PatternStampMotifShape     MotifShape        = PatternStampMotifShape::Circle;
    bool                  ClosedCurveLoop   = true;
    float                 CurveSmoothness   = 0.50f;
    std::string           TextContent       = "FRONTIER";
    float                 FontSizePx        = 54.0f;
    bool                  FillEnabled       = true;
    PatternStampFillSourceMode FillSource        = PatternStampFillSourceMode::Solid;
    ColorRgba             FillTint          = { 239u, 244u, 207u, 255u };
    ColorRgba             FillTintB         = { 90u, 118u, 146u, 255u };
    float                 GradientAngleDeg  = 45.0f;
    std::vector<PatternStampGradientStop> FillStops {};

    float                 StrokeWidthPx     = 0.0f;
    PatternStampFillSourceMode StrokeSource      = PatternStampFillSourceMode::Solid;
    ColorRgba             StrokeTint        = { 22u, 24u, 28u, 255u };
    GeometricRaster::VectorStrokeCapStyle  StrokeCap  = GeometricRaster::VectorStrokeCapStyle::Round;
    GeometricRaster::VectorStrokeJoinStyle StrokeJoin = GeometricRaster::VectorStrokeJoinStyle::Round;
    bool                  EvenOddFillRule   = false;

    std::string           FontFilePath     { "EngineContent/FontsArchives/Archivo/Archivo-Bold.ttf" };
    std::string           TypefaceTitle   { "Archivo Bold" };
    float                 LetterSpacingEm   = 0.0f;
    std::string           SvgPathPayload    {};
    std::vector<GeometricRaster::VectorCubicSubpath> CubicSubpaths {};
    std::array<PointFieldPinSpec, 12u>               PointFieldPins {};

    float                 CenterX           = 256.0f;
    float                 CenterY           = 256.0f;
    float                 WidthPx           = 128.0f;
    float                 HeightPx          = 128.0f;
    float                 RotationDeg       = 0.0f;

    PatternStampFinishPreset                       FinishPreset = PatternStampFinishPreset::PrintedDye;
    std::vector<PatternStampSurfaceChannelEntry> Channels     {};
    PatternStampLayerMaskSpec                      Mask         {};
    std::vector<PatternStampModifierSpec>          Modifiers    {};
};

struct PatternStampDocumentSnapshot
{
    std::string                   DocumentName          = "Noise-Scaled Hex Matrix";
    uint32_t                      SelectedLayerId       = 0u;
    uint32_t                      NextIdentitySeed      = 10u;
    bool                          GroundEnabled         = true;
    PatternStampFillSourceMode         GroundFillSource      = PatternStampFillSourceMode::Solid;
    ColorRgba                     GroundTintA           = { 23u, 26u, 31u, 255u };
    ColorRgba                     GroundTintB           = { 11u, 11u, 11u, 255u };
    float                         GroundAngleDeg        = 45.0f;
    bool                          ShowGhostTileBorders  = true;
    std::vector<PatternStampLayerSpec> Layers                {}; // bottom -> top order; rendered top -> bottom in layer stack
};

struct PatternStampToolDescriptor
{
    PatternStampToolCategory Category   = PatternStampToolCategory::Select;
    const char*     Label      = "Select";
    const char*     KeyCap     = "V";
    bool            PrimaryBar = true;
};

struct PatternStampPresetDescriptor
{
    std::string Id       {};
    std::string Title    {};
    std::string Subtitle {};
    ColorRgba   SwatchA  {};
    ColorRgba   SwatchB  {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLASS DECLARATION
//------------------------------------------------------------------------------------------------------------------------

class PatternStampWorkspaceSession
{
public:
    static constexpr std::size_t MaxLayerLimit = 32u;
    static constexpr std::size_t MaxUndoDepth  = 40u;

    explicit PatternStampWorkspaceSession(PatternStampWorkspaceMode InitialMode = PatternStampWorkspaceMode::Pattern) noexcept;

    void ConfigureMode(PatternStampWorkspaceMode TargetMode) noexcept;
    void LoadPresetByIndex(std::size_t Index) noexcept;

    [[nodiscard]] const std::vector<PatternStampPresetDescriptor>& QueryPresetDirectory() const noexcept;
    [[nodiscard]] std::size_t                                 QueryActivePresetIndex() const noexcept { return ActivePresetIndex; }

    [[nodiscard]] PatternStampLayerSpec*       InspectSelectedLayer() noexcept;
    [[nodiscard]] const PatternStampLayerSpec* InspectSelectedLayer() const noexcept;
    [[nodiscard]] PatternStampLayerSpec*       FindLayerById(uint32_t Identity) noexcept;
    [[nodiscard]] const PatternStampLayerSpec* FindLayerById(uint32_t Identity) const noexcept;
    [[nodiscard]] bool                    QueryEffectiveVisible(uint32_t Identity) const noexcept;
    [[nodiscard]] uint32_t                QueryLayerDepth(uint32_t Identity) const noexcept;
    [[nodiscard]] bool                    QueryUnderCollapsedFolder(uint32_t Identity) const noexcept;
    [[nodiscard]] std::size_t             CountVisibleLayers() const noexcept;
    [[nodiscard]] std::size_t             CountEnabledModifiers() const noexcept;
    [[nodiscard]] std::size_t             CountFolderImmediateMembers(uint32_t FolderIdentity) const noexcept;

    void SelectLayer(uint32_t Identity) noexcept;
    void AddLayer(PatternStampLayerCategory Category) noexcept;
    bool DuplicateSelectedLayer() noexcept;
    bool DeleteSelectedLayer() noexcept;
    bool MoveLayerStep(uint32_t Identity, int32_t DeltaUpInStack) noexcept;
    bool ReorderLayerRelative(uint32_t SourceId, uint32_t TargetId, bool DropAboveVisual) noexcept;
    void ToggleLayerVisible(uint32_t Identity) noexcept;
    void ToggleLayerLocked(uint32_t Identity) noexcept;
    void ToggleLayerExpandedDrawer(uint32_t Identity) noexcept;
    void ToggleFolderCollapsed(uint32_t Identity) noexcept;
    void ToggleSelectedLayerMask() noexcept;

    [[nodiscard]] std::vector<PatternStampSurfaceChannelKey> QueryAvailableChannelsForSelectedLayer() const noexcept;
    bool                                                AddChannelToSelectedLayer(PatternStampSurfaceChannelKey ChannelKey) noexcept;
    bool                                                RemoveChannelFromSelectedLayer(PatternStampSurfaceChannelKey ChannelKey) noexcept;
    [[nodiscard]] PatternStampSurfaceChannelEntry*         FindChannelEntry(PatternStampLayerSpec& Layer, PatternStampSurfaceChannelKey ChannelKey) noexcept;
    void                                                ApplyFinishPresetToSelectedLayer(PatternStampFinishPreset Preset) noexcept;

    bool AddModifierToSelectedLayer(PatternStampModifierCategory Category) noexcept;
    bool RemoveModifierFromSelectedLayer(std::size_t ModifierIndex) noexcept;

    void AssignResolution(uint32_t TargetResolutionPx) noexcept;
    void PushUndoSnapshot() noexcept;
    bool ApplyUndo() noexcept;
    bool ApplyRedo() noexcept;
    [[nodiscard]] bool QueryCanUndo() const noexcept { return !UndoSnapshots.empty(); }
    [[nodiscard]] bool QueryCanRedo() const noexcept { return !RedoSnapshots.empty(); }

    void BumpRevision() noexcept { ++GeometryRevision; }
    [[nodiscard]] static uint64_t AllocateSessionSerial() noexcept;
    static void InitializeDefaultPointFieldPins(PatternStampLayerSpec& Layer) noexcept;
    static void SeedMotifCubicSubpathsIfEmpty(PatternStampLayerSpec& Layer) noexcept;
    [[nodiscard]] bool ImportSvgIntoSession(
        std::string_view            SvgSourceOrPath,
        bool                        SourceIsFilePath,
        float                       DropDocX,
        float                       DropDocY,
        std::string_view            DisplayTitle,
        uint32_t                    TargetFolderOrLayerId = 0u,
        std::string*                OptionalDiagnostic = nullptr);
    [[nodiscard]] bool ApplyFontDrop(
        const std::filesystem::path& FontPath,
        std::string_view             TypefaceTitle,
        float                        DropDocX,
        float                        DropDocY,
        uint32_t                     TargetLayerId = 0u);
    [[nodiscard]] bool ApplyColourDrop(
        ColorRgba                    Tint,
        uint32_t                     TargetLayerId,
        int32_t                      PinIndex = -1,
        bool                         StrokeTarget = false);
    [[nodiscard]] std::string ExportDocumentToSvgString() const;
    [[nodiscard]] Codecs::VectorSpaceDocumentModel BuildVectorSpaceModel() const;
    [[nodiscard]] bool                           LoadFromVectorSpaceModel(const Codecs::VectorSpaceDocumentModel& Model);
    [[nodiscard]] ProceduralSpriteChannelSet     SynthesizeParticleSpriteAtlas(
        ProceduralSpriteArchetype Archetype,
        uint32_t                  TileResolutionPx = 128u,
        uint32_t                  AtlasColumns     = 1u,
        uint32_t                  Seed             = 1337u) const;

    void RecomputeSummaryMetrics() noexcept;

    [[nodiscard]] static const std::vector<PatternStampSurfaceChannelDescriptor>& InspectChannelDescriptors() noexcept;
    [[nodiscard]] static const PatternStampSurfaceChannelDescriptor&              QueryChannelDescriptor(PatternStampSurfaceChannelKey Key) noexcept;
    [[nodiscard]] static const std::vector<PatternStampToolDescriptor>&           InspectToolDescriptors() noexcept;
    [[nodiscard]] static const std::vector<const char*>&                     InspectMotifShapeLabels() noexcept;
    [[nodiscard]] static const std::vector<const char*>&                     InspectModifierLabels() noexcept;
    [[nodiscard]] static const std::vector<const char*>&                     InspectFinishLabels() noexcept;
    [[nodiscard]] static const std::vector<const char*>&                     InspectViewChannelLabels() noexcept;
    [[nodiscard]] static std::string                                         DescribeLayerSubtitle(const PatternStampLayerSpec& Layer) noexcept;

public:
    PatternStampWorkspaceMode Mode            = PatternStampWorkspaceMode::Pattern;
    PatternStampDocumentSnapshot Document     {};

    // Toolbar & Canvas UI Chrome settings (pure UI chrome — zero 2D/PBR shader rendering)
    uint32_t               ResolutionPx  = 1024u;
    PatternStampToolCategory        ActiveTool    = PatternStampToolCategory::Select;
    PatternStampViewChannel     ViewChannel   = PatternStampViewChannel::VectorSVG;
    bool                   ShowGrid      = true;
    bool                   ShowAxes      = true;
    int32_t                SnapStepPx    = 16;
    float                  ZoomFraction  = 1.0f;
    float                  PanOffsetU    = 0.0f;
    float                  PanOffsetV    = 0.0f;

    // Interactive vector editing & GPU picking highlight
    uint32_t               HoveredLayerId      = 0u;
    int32_t                ActiveKnotIndex     = -1;
    int32_t                ActivePointPinIndex = -1;
    bool                   MarqueeActive       = false;
    float                  MarqueeStartX       = 0.0f;
    float                  MarqueeStartY       = 0.0f;
    float                  MarqueeEndX         = 0.0f;
    float                  MarqueeEndY         = 0.0f;
    uint64_t               SessionInstanceSerial = AllocateSessionSerial();
    uint64_t               GeometryRevision      = 1ull;

    // Layer stack filter/search UI settings
    PatternStampLayerFilterMode LayerFilter   = PatternStampLayerFilterMode::AllLayers;
    std::string            LayerSearch   {};

    // Evaluated Inspector KPI metrics
    int32_t InstancesEvaluated = 125;
    float   MinScaleReading    = 0.40f;
    float   MaxScaleReading    = 1.60f;
    int32_t SurfaceFractionPct = 100;

    // Footer status message
    std::string StatusNotice {};

private:
    void BuildPresetDirectory() noexcept;
    void PopulatePatternHexArrayPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulatePatternKenteWeavePreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulatePatternRadialMandalaPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulatePatternPerlinPolkaPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulatePatternRidgedDiamondsPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulatePatternVoronoiPebblesPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulateStampSolidCirclePreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulateStampBotanicalCrestPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulateStampPrecisionReticlePreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulateStampHexFastenerPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulateStampIndustrialStencilPreset(PatternStampDocumentSnapshot& Out) noexcept;
    void PopulateStampRadialCogPreset(PatternStampDocumentSnapshot& Out) noexcept;

    std::size_t                          ActivePresetIndex = 0u;
    std::vector<PatternStampPresetDescriptor> PresetDirectory   {};
    std::vector<PatternStampDocumentSnapshot> UndoSnapshots     {};
    std::vector<PatternStampDocumentSnapshot> RedoSnapshots     {};
};

} // namespace Frontier::Workspaces
