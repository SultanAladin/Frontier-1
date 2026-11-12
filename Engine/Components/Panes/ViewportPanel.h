/*============================================================================================================================================
                                                               VIEWPORTPANEL.H
============================================================================================================================================*/
// 🧩 Workspace Viewport pane with Edit/Simulate/Play-Pause strip, Construct sheet, Split Viewport, Diagnostics, and Settings

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"

#include <array>
#include <cstdint>
#include <string_view>

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
    ConstructSet = 1, // [-] - Generic constructable entries (generic construct slots)
    Organization = 2, // [-] - Organizational folders
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportDisplaySettings
{
    uint32_t QualityTier         = 2u;     // [idx] - 0=Low, 1=Medium, 2=Standard, 3=High, 4=Ultra
    uint32_t DebugChannelIndex   = 0u;     // [idx] - 0=Off, 1=Depth, 2=Albedo, 3=Normal, 4=Roughness
    float    RenderScalePercent  = 100.0f; // [%]   - Internal resolution percentage [50..200]
    bool     GlobalIllumination  = true;   // [-]   - Indirect lighting toggle
    bool     ReflectionsEnabled  = true;   // [-]   - Specular reflections toggle
    bool     AntiAliasingEnabled = true;   // [-]   - Temporal anti-aliasing toggle
    bool     RaytracingEnabled   = true;   // [-]   - Hardware raytracing toggle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ViewportPanel
{
public:
    ViewportPanel() noexcept;

    void AttachOutliner(OutlinerPanel* SourceOutliner) noexcept;
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
    [[nodiscard]] const ViewportDisplaySettings& QueryDisplaySettings() const noexcept;
    [[nodiscard]] ViewportDisplaySettings&       AccessDisplaySettings() noexcept;

private:
    void PushDiagnosticsSample(double Δt) noexcept;
    void RenderCanvasPane(
        PixelSpace&              Surface,
        Rectangle2D              PaneBounds,
        ViewportProjectionPreset Projection,
        uint32_t                 PaneIndex,
        uint32_t                 ConstructedCount,
        const PaneInputReading&  Input);
    void RenderDiagnosticsPanel(
        PixelSpace&             Surface,
        Rectangle2D             CanvasBounds,
        uint32_t                ConstructedCount,
        uint32_t                VisibleCount,
        const PaneInputReading& Input);
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
    Rectangle2D                DebugButtonBounds       {};                                        // [px]  - Last Debug View button bounds
    ViewportProjectionPreset   PrimaryProjection       = ViewportProjectionPreset::Perspective;   // [-]   - Primary pane projection
    ViewportProjectionPreset   SecondaryProjection     = ViewportProjectionPreset::Top;           // [-]   - Split secondary pane projection
    uint32_t                   ActivePaneIndex         = 0u;                                      // [idx] - Focused pane in split mode (0/1)
    ViewportDisplaySettings    DisplaySettings         {};                                        // [-]   - Quality, AA, GI, RT & scale
    DiagnosticsTabCategory     DiagnosticsCategory     = DiagnosticsTabCategory::Timing;          // [-]   - Active diagnostics view tab
    bool                       DiagnosticsPaused       = false;                                   // [-]   - Freeze sparkline sampling
    std::array<float, 120>     IntervalHistoryMs      {};                                        // [ms]  - Rolling 120-step timing samples
    uint32_t                   HistoryWriteCursor      = 0u;                                      // [idx] - Ring buffer write cursor
    ConstructCategoryRail      ConstructRail           = ConstructCategoryRail::All;              // [-]   - Active Construct category rail
    TextEditBuffer             ConstructSearchEditor   {};                                        // [-]   - Construct search filter buffer
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void ViewportPanel::AttachOutliner(OutlinerPanel* SourceOutliner) noexcept
{
    AttachedOutliner = SourceOutliner;
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
