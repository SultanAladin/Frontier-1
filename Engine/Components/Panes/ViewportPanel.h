/*============================================================================================================================================
                                                               VIEWPORTPANEL.H
============================================================================================================================================*/
// 🧩 Workspace Viewport pane with Edit/Simulate/Play-Pause strip, Construct sheet, Split Viewport, Diagnostics, and Settings

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ExecutionModeStrip.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/Components/Diagnostics/ViewportTimingTelemetry.h"
#include "Engine/Components/Panes/OutlinerPanel.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ViewportExecutionMode : uint8_t
{
    Edit     = 0, // [-] - Authoring & manipulation mode
    Simulate = 1, // [-] - In-place physics/world simulation mode
    Play     = 2, // [-] - Runtime play session mode (supports Play/Pause)
};

enum class DiagnosticsTabCategory : uint8_t
{
    Timing   = 0, // [-] - Frame time sparkline & percentile statistics
    Scene    = 1, // [-] - Constructed/visible slot breakdown
    Render   = 2, // [-] - Quality, raytracing & render scale telemetry
    Profiler = 3, // [-] - Detailed per-stage GPU + CPU millisecond breakdown
};

enum class ConstructCategoryRail : uint8_t
{
    All             = 0, // [-] - All constructable entries
    Environment     = 1, // [-] - Atmosphere, Sun, Moon, Stars
    Weather         = 2, // [-] - Clouds, Wind, Fog, Precipitation, God Rays, Rainbow
    Cameras         = 3, // [-] - Perspective, Orthographic, Cinematic cameras
    Geometry        = 4, // [-] - Primitive meshes and volumes
    Lighting        = 5, // [-] - Point, Spot, Directional, Area luminaires
    Vehicle         = 6, // [-] - Wheeled and airborne vehicles
    ScenePrimitives = 7, // [-] - Legacy generic fallback rail (WorkspacePanesValidation)
    Organization    = 8, // [-] - Legacy organizational folders rail (WorkspacePanesValidation)
};

enum class ConstructModalStage : uint8_t
{
    Entities   = 0, // [-] - Step 01: browse & search entity grid
    Properties = 1, // [-] - Step 02: edit display name & add to scene
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ConstructRegistryEntry
{
    std::string               RegistryKey    {};                                         // [-] - Stable key (e.g. "env:sun")
    std::string               Title          {};                                         // [-] - Tile display title ("Sun")
    std::string               DefaultTitle   {};                                         // [-] - Initial Step 02 editable title ("Sun" / "Moon 2")
    std::string               CategoryLabel  {};                                         // [-] - Subtitle shown in Step 02 ("Environment")
    std::string               Description    {};                                         // [-] - Short summary shown in Step 02
    std::string               FolderCaption  {};                                         // [-] - Target folder label ("Environment")
    ConstructCategoryRail     Rail           = ConstructCategoryRail::Environment;       // [-] - Left rail membership
    EnvironmentSymbolCategory Symbol         = EnvironmentSymbolCategory::None;          // [-] - Dedicated environment vector icon
    ConstructSlotCategory     FallbackIcon   = ConstructSlotCategory::GeometryVolume;    // [-] - Fallback icon when Symbol == None
    bool                      Disabled       = false;                                    // [-] - Greyed out (already added or not yet implemented)
    std::string               BadgeText      {};                                         // [-] - Optional short pill ("Added", "1 / 6", "Soon")
    std::string               DisabledReason {};                                         // [-] - Hover tooltip explanation when Disabled == true
};

struct ViewportDisplaySettings
{
    uint32_t QualityTier           = 3u;     // [idx] - 0=Low, 1=Medium, 2=Standard, 3=High, 4=Ultra
    uint32_t DebugChannelIndex     = 0u;     // [idx] - 0=Off, 1..16=Modular Debug Channels (12..16 = SDF views)
    uint32_t RenderEngineIndex     = 2u;     // [idx] - 0=MatCap, 1=OpenPbrDirect, 2=GtxSdfGi, 3=HighEndBvh4Gi, 4=PtRealtime, 5=PtFinal
    uint32_t ShadowAlgorithmIndex  = 0u;     // [idx] - 0=ContactPcss, 1=PcssPlaneAware, 2=ShadowRayMarch, 3=PcfBilinear, 4=WideTreeRayQuery
    uint32_t SdfShadowPenumbraIndex = 3u;    // [idx] - 0=PointSdf, 1=AnalyticCone, 2=VoxelClipmap, 3=VoxelConeGi, 4=HybridSobolCap, 5=SdfMcRef96Spp
    uint32_t ReflectionSourceIndex = 0u;     // [idx] - 0=WideTreeRaytrace, 1=SkyEnvironment
    uint32_t TraceResolutionIndex  = 0u;     // [idx] - 0=Full Trace, 1=1/2 Trace, 2=1/4 Trace
    float    RenderScalePercent    = 100.0f; // [%]   - Internal resolution percentage [50..200]
    bool     ReflectionsEnabled    = true;   // [-]   - Specular reflections toggle
    bool     AntiAliasingEnabled   = true;   // [-]   - Spatial shadow filter / AA toggle
    bool     RaytracingEnabled     = true;   // [-]   - Hardware BVH4 raytracing toggle
    bool     XeGtaoEnabled         = false;  // [-]   - XeGTAO screen-space AO for direct PBR (no GI)
    float    XeGtaoStrength        = 1.0f;   // [x]   - XeGTAO occlusion strength [0..2]
    uint32_t XeGtaoSliceCount      = 4u;     // [cnt] - XeGTAO directional slices [2..8]
    uint32_t XeGtaoStepsPerSide    = 4u;     // [cnt] - XeGTAO horizon march steps per side [2..8]
    float    XeGtaoSearchRadiusM   = 0.85f;  // [m]   - XeGTAO world-space search radius [0.25..3.0]
    uint32_t GiCandidateIndex      = 1u;     // [idx] - 0=StokesContour, 1=SurfelRadiosity
    uint32_t SdfConeSteps          = 6u;     // [cnt] - GDF voxel cone GI march steps [3..12]
    float    SdfGiIntensity        = 1.0f;   // [x]   - GDF voxel cone GI indirect scale [0..2.5]
    float    SdfAoRadiusM          = 1.55f;  // [m]   - SDF UDF ambient occlusion reach [0.35..3.0]
    float    SdfAoContrast         = 1.50f;  // [x]   - SDF UDF ambient occlusion contrast/darkness [0.40..3.0]
    uint32_t MatcapChannelIndex    = 0u;     // [idx] - 0=Clay, 1=Ceramic, 2=Metallic, 3=Normal
    float    MatcapRimGain         = 0.35f;  // [0-1] - MatCap silhouette rim boost
    float    CameraFocalLengthMm   = 35.0f;  // [mm]  - Active viewport camera focal length [12..200]
    float    CameraSensorWidthMm   = 36.0f;  // [mm]  - Active viewport camera sensor width [12..70]
    float    CameraApertureFStop   = 2.8f;   // [f/]  - Active viewport camera aperture [1.2..22]
    float    CameraFocusDistanceM  = 6.0f;   // [m]   - Active viewport camera focus distance [0.3..100]
    float    CameraRollDegrees     = 0.0f;   // [deg] - Active viewport camera Dutch roll [-45..45]
    uint32_t PathTraceTargetSpp    = 256u;   // [spp] - Progressive PT convergence target (0 = render continuously, no cap)
    uint32_t PathTraceBatchSpp     = 1u;     // [spp] - PT samples accumulated per frame (1/2/4/8, HTML lab parity)
    uint32_t PathTraceMaxBounces   = 3u;     // [cnt] - Path tracer maximum bounce depth [1..8]
    uint32_t SmrtShadowRays        = 4u;     // [cnt] - SMRT quadratic shadow ray count [1..8]
    uint32_t SmrtMarchSteps        = 10u;    // [cnt] - SMRT quadratic march steps per ray [4..16]
    float    SkyFillGain           = 1.0f;   // [x]   - Sky dome fill gain multiplier [0..3.0]
    float    ExposureGain          = 1.0f;   // [x]   - Final ACES display exposure gain [0.1..4.0]
    bool     ClusterLodEnabled       = true;   // [-]   - Nanite-like cluster LOD drives the viewport BVH
    float    ClusterLodPixelError    = 1.25f;  // [px]  - Screen-space LOD error budget (0.5 quality / 1.25 balanced / 2.5 / 5 aggressive)
    int32_t  ClusterForcedLodLevel   = -1;     // [lvl] - -1 = automatic view cut, >= 0 = force exact pyramid level
    bool     ClusterWireframeOverlay = false;  // [-]   - Analytical triangle wireframe overlay across active viewport view
    bool     AtmosphereSkyEnabled  = true;   // [-]   - Physical Bruneton/Hillaire atmospheric sky toggle
    bool     VolumetricCloudsEnabled = false;// [-]   - Raymarched volumetric cloud deck toggle (default off for Clear Sky)
    bool     CloudShadowsEnabled   = false;  // [-]   - Volumetric cloud ground shadow casting toggle
    bool     CelestialStarsEnabled = true;   // [-]   - Sun disc, moons & nocturnal starfield toggle
    bool     DayCyclePlaying       = false;  // [-]   - Advances solar ephemeris time of day
    float    SolarTimeOfDayHours   = 10.5f;  // [h]   - Solar time of day [0..24]
    float    CloudFraction         = 0.34f;  // [0-1] - Cloud sky fraction [0..1]
    float    CloudWindSpeedMps     = 0.0f;   // [m/s] - Static cloud deck by default (0 = no fake movement)
};

struct ViewportPaneSurfaceSpecification
{
    uint32_t                 PaneIndex     = 0u;
    uint32_t                 WidthPx       = 0u;
    uint32_t                 HeightPx      = 0u;
    ViewportProjectionPreset Projection    = ViewportProjectionPreset::Perspective;
    ViewportExecutionMode    ExecutionMode = ViewportExecutionMode::Edit;
    bool                     PlayPaused    = false;
    ViewportDisplaySettings  Display       {};
};

struct ViewportRasterSurface
{
    const void* SoftwareTextureData = nullptr; // [-] - Optional ImTextureData* for headless / CPU readback display
    uint64_t    NativeTextureHandle = 0ull;    // [-] - Optional GPU ImTextureID handle
};

using ViewportPaneRasterCallback      = std::function<ViewportRasterSurface(const ViewportPaneSurfaceSpecification&)>;
using ViewportPaneInteractionCallback = std::function<void(uint32_t PaneIndex, Rectangle2D PaneBounds, const PaneInputReading& Input)>;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ViewportPanel
{
public:
    using NavigationCallback          = std::function<void(ViewportNavigationDirective)>;
    using ConstructCanvasDropCallback = std::function<void(int32_t TemplateIndex, int32_t PresetIndex, Point2D CanvasLogicalPos)>;
    using ConstructApplyCallback      = std::function<void(std::string_view RegistryKey, std::string_view CustomTitle)>;

    ViewportPanel() noexcept;
    ~ViewportPanel() noexcept;

    ViewportPanel(const ViewportPanel&)            = delete;
    ViewportPanel& operator=(const ViewportPanel&) = delete;

    void AttachOutliner(OutlinerPanel* SourceOutliner) noexcept;
    void AssignNavigationCallback(NavigationCallback Callback);
    void AssignCanvasRasterImage(const uint8_t* RgbaPixels, uint32_t WidthPx, uint32_t HeightPx);
    void AssignCanvasGpuTexture(uint64_t NativeTextureHandle, uint32_t WidthPx, uint32_t HeightPx) noexcept;
    void ClearCanvasRasterImage() noexcept;
    void ConfigureConstructCanvasDropCallback(ConstructCanvasDropCallback Callback);
    void ConfigureConstructRegistry(std::vector<ConstructRegistryEntry> Entries);
    void ConfigureConstructApplyCallback(ConstructApplyCallback Callback);
    void ConfigureConstructRail(ConstructCategoryRail Rail) noexcept;
    void SelectConstructPreviewEntry(std::string_view RegistryKey);
    void ConfigureOpenContentBrowserCallback(std::function<void()> Callback);
    void ConfigureRasterCallback(ViewportPaneRasterCallback Callback);
    void ConfigureInteractionCallback(ViewportPaneInteractionCallback Callback);
    void ConfigureViewportTimingBreakdown(const ViewportTimingBreakdown& Breakdown) noexcept;
    void ConfigureProbeReadoutText(std::string_view ReadoutText);
    void ConfigureSceneTitle(std::string_view Title);
    void ConfigureExecutionMode(ViewportExecutionMode Mode, bool Paused = false) noexcept;
    void TogglePlayPause() noexcept;
    void ConfigureSplitViewport(bool SplitEnabled) noexcept;
    void ConfigureDiagnosticsVisible(bool Visible) noexcept;
    void AdvanceDebugChannelCycle() noexcept { DisplaySettings.DebugChannelIndex = (DisplaySettings.DebugChannelIndex + 1u) % 23u; }
    void ConfigureRenderSettingsSheetOpen(bool Open) noexcept;
    void ConfigureSdfPenumbraDropdownOpen(bool Open) noexcept { SdfPenumbraDropdownOpen = Open; }
    void ConfigureConstructSheetOpen(bool Open) noexcept;
    void ConfigureProjectionDropdownOpen(bool Open) noexcept;
    void ConfigurePrimaryProjection(ViewportProjectionPreset Preset) noexcept;
    void ConfigureSecondaryProjection(ViewportProjectionPreset Preset) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt,
        PixelSpace*             OverlaySurface = nullptr);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

    [[nodiscard]] ViewportExecutionMode          QueryExecutionMode() const noexcept;
    [[nodiscard]] bool                           QueryPlayPaused() const noexcept;
    [[nodiscard]] bool                           QuerySplitViewportEnabled() const noexcept;
    [[nodiscard]] bool                           QueryDiagnosticsVisible() const noexcept;
    [[nodiscard]] bool                           QueryDiagnosticsPaused() const noexcept { return DiagnosticsPaused; }
    [[nodiscard]] bool                           QueryRenderSettingsSheetOpen() const noexcept;
    [[nodiscard]] bool                           QueryConstructSheetOpen() const noexcept;
    [[nodiscard]] ConstructCategoryRail          QueryConstructRail() const noexcept { return ConstructRail; }
    [[nodiscard]] ConstructModalStage            QueryConstructStage() const noexcept { return ConstructStage; }
    [[nodiscard]] bool                           QueryProjectionDropdownOpen() const noexcept;
    [[nodiscard]] ViewportProjectionPreset       QueryPrimaryProjection() const noexcept;
    [[nodiscard]] ViewportProjectionPreset       QuerySecondaryProjection() const noexcept;
    [[nodiscard]] uint32_t                       QueryActivePaneIndex() const noexcept;
    [[nodiscard]] float                          QueryViewportZoomScale() const noexcept;
    [[nodiscard]] Rectangle2D                    QueryLastCanvasPaneBounds() const noexcept;
    [[nodiscard]] Rectangle2D                    QueryLastCanvasPhysicalBounds() const noexcept;
    [[nodiscard]] const ViewportDisplaySettings& QueryDisplaySettings() const noexcept;
    [[nodiscard]] ViewportDisplaySettings&       AccessDisplaySettings() noexcept;
    [[nodiscard]] const ImTextureData&           QueryCanvasRasterTexture() const noexcept { return CanvasRasterTexture; }

private:
    void PushDiagnosticsSample(double Δt) noexcept;
    void RenderCanvasPane(
        PixelSpace&              Surface,
        Rectangle2D              PaneBounds,
        ViewportProjectionPreset Projection,
        uint32_t                 PaneIndex,
        uint32_t                 ConstructedCount,
        const PaneInputReading&  Input,
        bool                     OverlayCoversCanvas);
    void RenderDiagnosticsPanel(
        PixelSpace&             Surface,
        Rectangle2D             CanvasBounds,
        uint32_t                ConstructedCount,
        uint32_t                VisibleCount,
        const PaneInputReading& Input);
    [[nodiscard]] Rectangle2D QueryConstructDialogBounds(Rectangle2D Bounds) const noexcept;
    void RenderConstructDialog(
        PixelSpace&             Overlay,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);
    void RenderSettingsSheet(
        PixelSpace&             Overlay,
        Rectangle2D             Bounds,
        Point2D                 GearButtonRightBottom,
        const PaneInputReading& Input);

    OutlinerPanel*             AttachedOutliner        = nullptr;                                 // [-]   - Bound Outliner / LayerStack
    std::string                SceneTitle              = "Scene";                                 // [-]   - Viewport identity heading
    ViewportExecutionMode      ExecutionMode           = ViewportExecutionMode::Edit;             // [-]   - Active Edit/Simulate/Play mode
    bool                       PlayPaused              = false;                                   // [-]   - Play session paused indicator
    bool                       SplitViewportEnabled    = false;                                   // [-]   - Dual side-by-side viewport toggle
    bool                       DiagnosticsVisible      = false;                                   // [-]   - Diagnostics overlay active
    bool                       RenderSettingsSheetOpen       = false;                                   // [-]   - Dedicated Render Settings panel open
    bool                       RenderMenuOpen              = false;                                   // [-]   - Render header menu open
    uint32_t                   RenderSettingsSection       = 0u;                                      // [idx] - Render panel section (0 Renderer, 1 Lighting, 2 Path Tracing, 3 Nanite, 4 Atmosphere)
    Rectangle2D                RenderMenuButtonBounds      {};                                        // [px]  - Header Render menu button bounds
    bool                       ConstructSheetOpen      = false;                                   // [-]   - Construct dialog open
    bool                       ProjectionDropdownOpen  = false;                                   // [-]   - Projection selector open
    bool                       HeaderDebugDropdownOpen = false;                                   // [-]   - Header Debug View DropdownSelector open
    bool                       DebugDropdownOpen       = false;                                   // [-]   - Debug View DropdownSelector open
    bool                       EngineDropdownOpen      = false;                                   // [-]   - Render Engine DropdownSelector open
    bool                       ShadowDropdownOpen      = false;                                   // [-]   - Shadow Algorithm DropdownSelector open
    bool                       SdfPenumbraDropdownOpen = false;                                   // [-]   - 6-Mode SDF Shadow/Penumbra DropdownSelector open
    Rectangle2D                HeaderDebugButtonBounds {};                                        // [px]  - Last Header Debug button bounds
    Rectangle2D                DebugButtonBounds       {};                                        // [px]  - Last Debug View button bounds
    Rectangle2D                EngineButtonBounds      {};                                        // [px]  - Last Engine selector button bounds
    Rectangle2D                ShadowButtonBounds      {};                                        // [px]  - Last Shadow selector button bounds
    Rectangle2D                SdfPenumbraButtonBounds {};                                        // [px]  - Last SDF Penumbra selector button bounds
    ViewportProjectionPreset   PrimaryProjection       = ViewportProjectionPreset::Perspective;   // [-]   - Primary pane projection
    ViewportProjectionPreset   SecondaryProjection     = ViewportProjectionPreset::Top;           // [-]   - Split secondary pane projection
    uint32_t                   ActivePaneIndex         = 0u;                                      // [idx] - Focused pane in split mode (0/1)
    ViewportDisplaySettings    DisplaySettings         {};                                        // [-]   - Quality, AA, GI, RT & scale
    DiagnosticsTabCategory     DiagnosticsCategory     = DiagnosticsTabCategory::Timing;          // [-]   - Active diagnostics view tab
    bool                       DiagnosticsPaused       = false;                                   // [-]   - Freeze sparkline sampling
    DiagnosticsOverlaySession  DiagnosticsSession      {};                                        // [-]   - Reusable DiagnosticsOverlay session
    std::array<float, 600>     IntervalHistoryMs      {};                                        // [ms]  - 10 Hz timing samples (600 = 60 s window)
    uint32_t                   HistoryWriteCursor      = 0u;                                      // [idx] - Ring buffer write cursor
    float                      HistoryPendingMs        = 0.0f;                                    // [ms]  - Frame-time accumulator for the next 10 Hz sample
    float                      HistoryPendingSeconds   = 0.0f;                                    // [s]   - Elapsed time inside the pending sample bucket
    uint32_t                   HistoryPendingFrames    = 0u;                                      // [cnt] - Frames inside the pending sample bucket
    ViewportTimingBreakdown       LastViewportTiming     {};                                        // [ms]  - Latest per-stage GPU/CPU telemetry
    ConstructCategoryRail      ConstructRail           = ConstructCategoryRail::All;              // [-]   - Active Construct category rail
    ConstructModalStage        ConstructStage          = ConstructModalStage::Entities;           // [-]   - Step 01 Entities vs Step 02 Properties
    float                      ConstructGridScrollY    = 0.0f;                                    // [px]  - Construct tile grid scroll offset
    TextEditBuffer             ConstructSearchEditor   {};                                        // [-]   - Construct Step 01 search filter buffer
    TextEditBuffer             ConstructTitleEditor    {};                                        // [-]   - Construct Step 02 editable title buffer
    std::string                ConstructSelectedKey    {};                                        // [-]   - RegistryKey chosen in Step 01
    std::vector<ConstructRegistryEntry> ConstructRegistry {};                                     // [-]   - Configured Construct modal entries (empty = legacy fallback)
    ConstructApplyCallback     OnConstructApply        {};                                        // [-]   - Invoked when Step 02 "Add to scene +" triggers
    float                      ViewportZoomScale       = 1.0f;                                    // [x]   - Interactive canvas zoom multiplier
    Rectangle2D                LastCanvasPaneBounds    {};                                        // [px]  - Last rendered active sub-viewport rect
    Rectangle2D                LastCanvasPhysicalBounds{};                                        // [px]  - Last rendered active sub-viewport physical rect
    NavigationCallback         OnNavigationDirective   {};                                        // [-]   - Optional callback when + / - / Fit triggers
    ConstructCanvasDropCallback OnConstructCanvasDrop  {};                                        // [-]   - Optional drag-to-canvas Construct drop callback
    std::function<void()>      OpenContentBrowserCallback {};                                     // [-]   - Callback invoked by Content Browser button
    bool                       ConstructTilePressed    = false;                                   // [-]   - Tile pressed inside Construct dialog
    bool                       ConstructDragActive     = false;                                   // [-]   - Tile dragged onto 3D canvas
    Point2D                    ConstructDragOrigin     {};                                        // [px]  - Press position for Construct drag deadzone
    int32_t                    ConstructDragTemplateIndex = -1;                                   // [idx] - Template index being dragged/clicked
    ConstructSlotCategory      ConstructDragCategory   = ConstructSlotCategory::GeometryVolume;
    std::string                ConstructDragLabel      {};                                        // [-]   - Label shown on floating Construct ghost

    ViewportPaneRasterCallback      RasterCallback               {};                                   // [-]   - Optional GPU / software surface feeder
    ViewportPaneInteractionCallback InteractionCallback          {};                                   // [-]   - Optional canvas pointer router
    int32_t                         CapturedInteractionPaneIndex = -1;                                 // [idx] - Pane holding pointer drag across borders
    std::string                     ProbeReadoutText        {};                                        // [-]   - Optional bottom strip readout
    ImTextureData                   CanvasRasterTexture     {};                                        // [-]   - Optional live GTX/RTX viewport texture
    uint64_t                        CanvasGpuTextureHandle  = 0ull;                                    // [-]   - GPU-resident ImTextureID (zero-copy viewport)
    uint32_t                        CanvasGpuWidthPx        = 0u;                                      // [px]  - GPU-resident viewport width
    uint32_t                        CanvasGpuHeightPx       = 0u;                                      // [px]  - GPU-resident viewport height
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void ViewportPanel::AttachOutliner(OutlinerPanel* SourceOutliner) noexcept
{
    AttachedOutliner = SourceOutliner;
}

inline void ViewportPanel::AssignNavigationCallback(NavigationCallback Callback)
{
    OnNavigationDirective = std::move(Callback);
}

inline float ViewportPanel::QueryViewportZoomScale() const noexcept
{
    return ViewportZoomScale;
}

inline Rectangle2D ViewportPanel::QueryLastCanvasPaneBounds() const noexcept
{
    return LastCanvasPaneBounds;
}

inline Rectangle2D ViewportPanel::QueryLastCanvasPhysicalBounds() const noexcept
{
    return LastCanvasPhysicalBounds;
}

inline ViewportExecutionMode ViewportPanel::QueryExecutionMode() const noexcept
{
    return ExecutionMode;
}

inline bool ViewportPanel::QueryPlayPaused() const noexcept
{
    return PlayPaused;
}

inline bool ViewportPanel::QuerySplitViewportEnabled() const noexcept
{
    return SplitViewportEnabled;
}

inline bool ViewportPanel::QueryDiagnosticsVisible() const noexcept
{
    return DiagnosticsVisible;
}

inline bool ViewportPanel::QueryRenderSettingsSheetOpen() const noexcept
{
    return RenderSettingsSheetOpen;
}

inline bool ViewportPanel::QueryConstructSheetOpen() const noexcept
{
    return ConstructSheetOpen;
}

inline void ViewportPanel::ConfigureProjectionDropdownOpen(bool Enabled) noexcept
{
    ProjectionDropdownOpen = Enabled;
}

inline bool ViewportPanel::QueryProjectionDropdownOpen() const noexcept
{
    return ProjectionDropdownOpen;
}

inline ViewportProjectionPreset ViewportPanel::QueryPrimaryProjection() const noexcept
{
    return PrimaryProjection;
}

inline ViewportProjectionPreset ViewportPanel::QuerySecondaryProjection() const noexcept
{
    return SecondaryProjection;
}

inline uint32_t ViewportPanel::QueryActivePaneIndex() const noexcept
{
    return ActivePaneIndex;
}

inline const ViewportDisplaySettings& ViewportPanel::QueryDisplaySettings() const noexcept
{
    return DisplaySettings;
}

inline ViewportDisplaySettings& ViewportPanel::AccessDisplaySettings() noexcept
{
    return DisplaySettings;
}

} // namespace Frontier
