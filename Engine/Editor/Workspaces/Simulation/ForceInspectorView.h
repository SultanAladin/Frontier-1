/*============================================================================================================================================
                                                             FORCEINSPECTORVIEW.H
============================================================================================================================================*/
// 🧩 Full 1:1 ForceFields Inspector renderer: 4 tabs (Field / Formula / Layers / Cache), 19 ForceModel law schemas,
//    Beaufort Anemometer telemetry plot, radial envelope f(r), motion path waypoints, VectorFieldLang compiler & GPU layers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <cstdint>
#include <span>
#include <string>

namespace Frontier::Editor::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ForceInspectorTab : uint8_t
{
    Field   = 0u, // [-] - Anemometer, Sample Cursor, Selected ForceModel & Motion Path
    Formula = 1u, // [-] - VectorFieldLang symbolic editor, presets, diagnostics & kernel view
    Layers  = 2u, // [-] - GPU slice colorimetry & RK4 streamline style controls
    Cache   = 3u, // [-] - 4-Tier Cascade Clipmap budget & SimulationSpace TOML preview
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ForceInspectorViewTargets
{
    OutlinerPanel*                                       Outliner          = nullptr; // [-]   - Attached scene outliner
    OutlinerRecordEntry*                                 OutlinerEntry     = nullptr; // [-]   - Currently selected entry
    SimulationSpace::ForceFields::ForceFieldAccumulator* Accumulator       = nullptr; // [-]   - Live CPU/GPU force accumulator
    SimulationSpace::ForceFields::FieldLatticeCascade*   LatticeCascade    = nullptr; // [-]   - 4-tier spatial clipmap metrics
    SimulationSpace::ForceFields::ForceSourceRecordEntry* ActiveForce      = nullptr; // [-]   - Mutable force source descriptor
    SimulationSpace::ForceFields::ForceOverlaySettings*  OverlaySettings   = nullptr; // [-]   - Mutable viewport layer controls
    std::span<const float>                               AnemometerSamples {};        // [m/s] - Ordered rolling wind trace
    double                                               SimulationTimeSec = 0.0;     // [s]   - Active simulation clock
    int32_t*                                             SelectedWaypoint  = nullptr; // [idx] - Mutable selected path knot index
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   FORCEINSPECTORVIEW
//------------------------------------------------------------------------------------------------------------------------

class ForceInspectorView
{
public:
    ForceInspectorView() noexcept = default;

    void ConfigureActiveTab(ForceInspectorTab Tab) noexcept;
    void ConfigureScrollOffset(float OffsetY) noexcept;

    [[nodiscard]] ForceInspectorTab QueryActiveTab() const noexcept;
    [[nodiscard]] float             QueryScrollOffset() const noexcept;

    [[nodiscard]] bool Render(
        PixelSpace&                       Surface,
        Rectangle2D                       Bounds,
        const ForceInspectorViewTargets& Targets,
        const PaneInputReading&           Input,
        double                            Δt);

private:
    enum class OpenDropdownCategory : uint8_t
    {
        None             = 0u,
        ForceModelSelector = 1u,
        ReachShape       = 2u,
        Attenuation      = 3u,
        DurationMode     = 4u,
        MotionTopology   = 5u,
        MotionCycle      = 6u,
        StreamProfile    = 7u,
        HeatmapField     = 8u,
        ColorRamp        = 9u,
        StreamColorMode  = 10u,
    };

    struct DeferredSheetOverlay
    {
        OpenDropdownCategory Kind          = OpenDropdownCategory::None;
        Rectangle2D      TriggerBounds {};
        float            FixedWidth    = 0.0f;
    };

    float RenderFieldTab(
        PixelSpace&                       Surface,
        Rectangle2D                       ScrollViewport,
        float                             LeftX,
        float                             TopY,
        float                             InnerW,
        const ForceInspectorViewTargets& Targets,
        const PaneInputReading&           Input,
        DeferredSheetOverlay&             OutOverlay);

    float RenderFormulaTab(
        PixelSpace&                       Surface,
        Rectangle2D                       ScrollViewport,
        float                             LeftX,
        float                             TopY,
        float                             InnerW,
        const ForceInspectorViewTargets& Targets,
        const PaneInputReading&           Input);

    float RenderLayersTab(
        PixelSpace&                       Surface,
        Rectangle2D                       ScrollViewport,
        float                             LeftX,
        float                             TopY,
        float                             InnerW,
        const ForceInspectorViewTargets& Targets,
        const PaneInputReading&           Input,
        DeferredSheetOverlay&             OutOverlay);

    float RenderCacheTab(
        PixelSpace&                       Surface,
        Rectangle2D                       ScrollViewport,
        float                             LeftX,
        float                             TopY,
        float                             InnerW,
        const ForceInspectorViewTargets& Targets,
        const PaneInputReading&           Input);

    ForceInspectorTab ActiveTab        = ForceInspectorTab::Field; // [-]  - Currently visible inspector tab
    float             ScrollOffsetY    = 0.0f;                     // [px] - Vertical scroll offset
    float             LastContentH     = 0.0f;                     // [px] - Total measured scroll height
    int32_t           ActiveSliderId   = -1;                       // [id] - Active dragging scalar slider tag
    int32_t           ActiveAxisDragId = -1;                       // [id] - Active dragging XYZ axis box tag
    float             AxisDragStartX   = 0.0f;                     // [px] - Cursor X at axis drag start
    float             AxisDragOrigin   = 0.0f;                     // [m]  - Initial coordinate at axis drag start
    OpenDropdownCategory  OpenDropdown     = OpenDropdownCategory::None;   // [-]  - Currently open selector sheet
    Rectangle2D       OpenTriggerRect  {};                         // [px] - Screen bounds of open trigger button
    bool              FormulaCopiedAck = false;                    // [-]  - Temporary copy confirmation marker
    double            CopyAckTimerSec  = 0.0;                      // [s]  - Remaining feedback seconds
};

} // namespace Frontier::Editor::Simulation
