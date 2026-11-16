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
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

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
    Timing = 0, // [-] - Frame time sparkline & percentile statistics
    Scene  = 1, // [-] - Constructed/visible slot breakdown
    Render = 2, // [-] - Quality, raytracing & render scale telemetry
};

enum class ConstructCategoryRail : uint8_t
{
    All          = 0, // [-] - All constructable templates and folders
    ScenePrimitives = 1, // [-] - Generic constructable entries (generic construct slots)
    Organization = 2, // [-] - Organizational folders
};

enum class ForceConstructCategory : uint8_t
{
    All             = 0, // [-] - All 19 ForceModel laws, presets, & scene entries
    VectorFieldModels      = 1, // [-] - VectorField analytic spatial laws (Linear, Buoyancy, Radial, Attractor, Repulsor, Axis, Containment)
    StreamTransportModels        = 2, // [-] - StreamTransport & transport laws (Rotation, Torque, Drag, Vorticity, Lorentz, Dipole, Stream, Spline, Adhesion)
    TurbulenceModels  = 3, // [-] - CurlNoise, AtmosphericGust, BrownianNoise
    Presets         = 4, // [-] - Curated .ForceModel & Formula presets
    SceneFolders = 5, // [-] - Static geometry, lights & folders
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportConstructPresetEntry
{
    std::string Title           {};          // [-] - Preset display title
    std::string Subtitle        {};          // [-] - Short parameter caption
    uint32_t    ForceModelOrdinal = 0u;          // [id]- ForceModel ordinal [0..18]
    bool        FormulaOverride = false;       // [-] - Symbolic formula preset indicator
    uint32_t    DomainOrdinal   = 0u;          // [id]- 0=VectorField, 1=StreamTransport, 2=TurbulenceField
};

struct ViewportDisplaySettings
{
    uint32_t QualityTier           = 2u;     // [idx] - 0=Low, 1=Medium, 2=Standard, 3=High, 4=Ultra
    uint32_t DebugChannelIndex     = 0u;     // [idx] - 0=Off, 1=Depth, 2=Albedo, 3=Normal, 4=Roughness
    uint32_t RenderEngineIndex     = 3u;     // [idx] - 0=MatCap, 1=OpenPbrDirect, 2=DdgiRaytracing, 3=SdfGlobalIllumination, 4=DeterministicPathTrace
    uint32_t ShadowAlgorithmIndex  = 0u;     // [idx] - 0=ContactPcss, 1=PcssPlaneAware, 2=ShadowRayMarch, 3=PcfBilinear, 4=WideTreeRayQuery
    uint32_t ReflectionSourceIndex = 0u;     // [idx] - 0=WideTreeRaytrace, 1=SkyEnvironment
    float    RenderScalePercent    = 100.0f; // [%]   - Internal resolution percentage [50..200]
    bool     GlobalIllumination    = true;   // [-]   - Indirect lighting toggle
    bool     ReflectionsEnabled    = true;   // [-]   - Specular reflections toggle
    bool     AntiAliasingEnabled   = true;   // [-]   - Temporal anti-aliasing toggle
    bool     RaytracingEnabled     = true;   // [-]   - Hardware raytracing toggle
};

struct ViewportPaneSurfaceSpecification
{
    uint32_t                                        PaneIndex     = 0u;
    uint32_t                                        WidthPx       = 0u;
    uint32_t                                        HeightPx      = 0u;
    ViewportProjectionPreset                        Projection    = ViewportProjectionPreset::Perspective;
    ViewportExecutionMode                           ExecutionMode = ViewportExecutionMode::Edit;
    bool                                            PlayPaused    = false;
    ViewportDisplaySettings                         Display       {};
    SimulationSpace::ForceFields::ForceOverlaySettings ForceOverlay{};
    double                                          TimeSeconds   = 0.0;
};

struct ViewportRasterSurface
{
    const void* SoftwareTextureData = nullptr; // [-] - Optional ImTextureData* for headless / CPU readback display
    uint64_t    NativeTextureHandle = 0ull;    // [-] - Optional GPU ImTextureID handle
    uint32_t    ActiveForceCount    = 0u;      // [count] - Contributing active forces in scene
};

using ViewportPaneRasterCallback      = std::function<ViewportRasterSurface(const ViewportPaneSurfaceSpecification&)>;
using ViewportPaneInteractionCallback = std::function<void(uint32_t PaneIndex, Rectangle2D PaneBounds, const PaneInputReading& Input)>;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ViewportPanel
{
public:
    using NavigationCallback = std::function<void(ViewportNavigationDirective)>;

    ViewportPanel() noexcept;
    ~ViewportPanel() noexcept;

    ViewportPanel(const ViewportPanel&)            = delete;
    ViewportPanel& operator=(const ViewportPanel&) = delete;

    void AttachOutliner(OutlinerPanel* SourceOutliner) noexcept;
    void AssignNavigationCallback(NavigationCallback Callback);
    void AssignCanvasRasterImage(const uint8_t* RgbaPixels, uint32_t WidthPx, uint32_t HeightPx);
    void ClearCanvasRasterImage() noexcept;
    void ConfigureConstructPresets(std::vector<ViewportConstructPresetEntry> Presets);
    void ConfigureConstructPresetCallback(std::function<void(uint32_t PresetIndex)> Callback);
    void ConfigureOpenContentBrowserCallback(std::function<void()> Callback);
    void ConfigureRasterCallback(ViewportPaneRasterCallback Callback);
    void ConfigureInteractionCallback(ViewportPaneInteractionCallback Callback);
    void ConfigureForceOverlayToolsVisible(bool Visible) noexcept;
    void ConfigureForceOverlaySheetOpen(bool Open) noexcept;
    void ConfigureForceOverlaySettings(const SimulationSpace::ForceFields::ForceOverlaySettings& Settings) noexcept;
    void ConfigureSimulationTimeSeconds(double TimeSeconds) noexcept;
    void ConfigureProbeReadoutText(std::string_view ReadoutText);
    void ConfigureSceneTitle(std::string_view Title);
    void ConfigureExecutionMode(ViewportExecutionMode Mode, bool Paused = false) noexcept;
    void TogglePlayPause() noexcept;
    void ConfigureSplitViewport(bool SplitEnabled) noexcept;
    void ConfigureDiagnosticsVisible(bool Visible) noexcept;
    void ConfigureSettingsSheetOpen(bool Open) noexcept;
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
    [[nodiscard]] bool                           QuerySettingsSheetOpen() const noexcept;
    [[nodiscard]] bool                           QueryConstructSheetOpen() const noexcept;
    [[nodiscard]] bool                           QueryProjectionDropdownOpen() const noexcept;
    [[nodiscard]] ViewportProjectionPreset       QueryPrimaryProjection() const noexcept;
    [[nodiscard]] ViewportProjectionPreset       QuerySecondaryProjection() const noexcept;
    [[nodiscard]] uint32_t                       QueryActivePaneIndex() const noexcept;
    [[nodiscard]] float                          QueryViewportZoomScale() const noexcept;
    [[nodiscard]] Rectangle2D                    QueryLastCanvasPaneBounds() const noexcept;
    [[nodiscard]] const ViewportDisplaySettings&                           QueryDisplaySettings() const noexcept;
    [[nodiscard]] ViewportDisplaySettings&                                 AccessDisplaySettings() noexcept;
    [[nodiscard]] bool                                                     QueryForceOverlayToolsVisible() const noexcept;
    [[nodiscard]] bool                                                     QueryForceOverlaySheetOpen() const noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceOverlaySettings& QueryForceOverlaySettings() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceOverlaySettings&       AccessForceOverlaySettings() noexcept;
    [[nodiscard]] double                                                   QuerySimulationTimeSeconds() const noexcept;
    [[nodiscard]] bool                                                     ConsumeRewindRequested() noexcept;

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
    void RenderForceOverlaySheet(
        PixelSpace&             Overlay,
        Rectangle2D             Bounds,
        Point2D                 ButtonBottomLeft,
        const PaneInputReading& Input);
    void RenderDiagnosticsPanel(
        PixelSpace&             Surface,
        Rectangle2D             CanvasBounds,
        uint32_t                ConstructedCount,
        uint32_t                VisibleCount,
        const PaneInputReading& Input);
    [[nodiscard]] bool        QueryForceConstructAvailable() const noexcept;
    [[nodiscard]] Rectangle2D QueryConstructDialogBounds(Rectangle2D Bounds) const noexcept;
    void RenderConstructDialog(
        PixelSpace&             Overlay,
        Rectangle2D             Bounds,
        const PaneInputReading& Input);
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
    bool                       SettingsSheetOpen       = false;                                   // [-]   - Viewport settings sheet open
    bool                       ConstructSheetOpen      = false;                                   // [-]   - Construct dialog open
    bool                       ProjectionDropdownOpen  = false;                                   // [-]   - Projection selector open
    bool                       DebugDropdownOpen       = false;                                   // [-]   - Debug View DropdownSelector open
    bool                       EngineDropdownOpen      = false;                                   // [-]   - Render Engine DropdownSelector open
    bool                       ShadowDropdownOpen      = false;                                   // [-]   - Shadow Algorithm DropdownSelector open
    Rectangle2D                DebugButtonBounds       {};                                        // [px]  - Last Debug View button bounds
    Rectangle2D                EngineButtonBounds      {};                                        // [px]  - Last Engine selector button bounds
    Rectangle2D                ShadowButtonBounds      {};                                        // [px]  - Last Shadow selector button bounds
    ViewportProjectionPreset   PrimaryProjection       = ViewportProjectionPreset::Perspective;   // [-]   - Primary pane projection
    ViewportProjectionPreset   SecondaryProjection     = ViewportProjectionPreset::Top;           // [-]   - Split secondary pane projection
    uint32_t                   ActivePaneIndex         = 0u;                                      // [idx] - Focused pane in split mode (0/1)
    ViewportDisplaySettings    DisplaySettings         {};                                        // [-]   - Quality, AA, GI, RT & scale
    DiagnosticsTabCategory     DiagnosticsCategory     = DiagnosticsTabCategory::Timing;          // [-]   - Active diagnostics view tab
    bool                       DiagnosticsPaused       = false;                                   // [-]   - Freeze sparkline sampling
    DiagnosticsOverlaySession  DiagnosticsSession      {};                                        // [-]   - Reusable DiagnosticsOverlay session
    std::array<float, 120>     IntervalHistoryMs      {};                                        // [ms]  - Rolling 120-step timing samples
    uint32_t                   HistoryWriteCursor      = 0u;                                      // [idx] - Ring buffer write cursor
    ConstructCategoryRail      ConstructRail           = ConstructCategoryRail::All;              // [-]   - Active Construct category rail
    ForceConstructCategory     ForceConstructTab       = ForceConstructCategory::All;             // [-]   - Active Force Construct rail tab
    float                      ConstructGridScrollY    = 0.0f;                                    // [px]  - Construct tile grid scroll offset
    TextEditBuffer             ConstructSearchEditor   {};                                        // [-]   - Construct search filter buffer
    float                      ViewportZoomScale       = 1.0f;                                    // [x]   - Interactive canvas zoom multiplier
    Rectangle2D                LastCanvasPaneBounds    {};                                        // [px]  - Last rendered active sub-viewport rect
    NavigationCallback         OnNavigationDirective   {};                                        // [-]   - Optional callback when + / - / Fit triggers
    std::vector<ViewportConstructPresetEntry> ConstructPresets {};                                // [-]   - Optional Force presets shown in Construct dialog
    std::function<void(uint32_t)>             ConstructPresetCallback {};                         // [-]   - Callback invoked when preset tile chosen
    std::function<void()>                     OpenContentBrowserCallback {};                      // [-]   - Callback invoked by Content Browser button

    ViewportPaneRasterCallback      RasterCallback          {};                                        // [-]   - Optional GPU / software surface feeder
    ViewportPaneInteractionCallback InteractionCallback     {};                                        // [-]   - Optional canvas pointer router
    bool                            ForceOverlayToolsVisible = false;                                  // [-]   - Show Force Viz pill & Rewind controls
    bool                            ForceOverlaySheetOpen    = false;                                  // [-]   - Force Viz overlay sheet open
    bool                            RewindRequested          = false;                                  // [-]   - Pulse triggered by Rewind button
    double                          SimulationTimeSeconds    = 0.0;                                    // [s]   - Active simulation clock readout
    uint32_t                        LastReportedForceCount   = 0u;                                     // [count]- Last force count from raster feed
    std::string                     ProbeReadoutText         {};                                       // [-]   - Optional bottom anemometer strip
    SimulationSpace::ForceFields::ForceOverlaySettings ForceOverlay{};                                 // [-]   - Editor Force Viz overlay parameters
    ImTextureData                   CanvasRasterTexture      {};                                       // [-]   - Optional live GTX/RTX viewport texture
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

inline bool ViewportPanel::QuerySettingsSheetOpen() const noexcept
{
    return SettingsSheetOpen;
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
