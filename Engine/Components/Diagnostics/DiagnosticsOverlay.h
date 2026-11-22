/*============================================================================================================================================
                                                           DIAGNOSTICSOVERLAY.H
============================================================================================================================================*/
// 🧩 Reusable floating Diagnostics overlay (metric tabs, view modes, sparkline plot, oversized readout, stats rows & move/resize grip)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DiagnosticsViewMode : uint8_t
{
    CombinedPlotStats = 0u, // [-] - Display sparkline plot and key-value stats grid
    PlotOnly          = 1u, // [-] - Display sparkline plot only
    StatsOnly         = 2u, // [-] - Display stats grid only
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DiagnosticsStatRow
{
    std::string Label       {}; // [-] - Left-aligned row caption
    std::string ReadingText {}; // [-] - Right-aligned formatted metric string
};

struct DiagnosticsMetricChannel
{
    std::string                     Title             {}; // [-] - Metric heading displayed beside accent bar
    std::string                     TabCaption        {}; // [-] - Compact tab label (falls back to Title when empty)
    std::string                     PrimaryWhole      {}; // [-] - Oversized integer portion (e.g. "16")
    std::string                     PrimaryFraction   {}; // [-] - Dimmed decimal portion (e.g. ".67")
    std::string                     PrimaryUnit       {}; // [-] - Trailing unit suffix (e.g. "ms")
    std::string                     SourceCaption     {}; // [-] - Descriptive subtitle below primary reading
    std::string                     DebugSummary      {}; // [-] - Optional bottom technical note
    std::vector<float>              Samples           {}; // [-] - Ordered chronological plot samples
    float                           PlotMinBound      = 0.0f;  // [-] - Suggested minimum Y bound
    float                           PlotMaxBound      = 1.0f;  // [-] - Suggested maximum Y bound
    float                           BudgetThreshold   = 0.0f;  // [-] - Optional budget horizontal line threshold (e.g. 16.67 ms)
    float                           SecondaryThreshold = 0.0f; // [-] - Optional secondary horizontal guide (e.g. 33.33 ms)
    int32_t                         SweepCursorIndex  = -1;    // [idx]- >=0 enables stationary oscilloscope sweep bar
    bool                            HasBudgetLine     = false; // [-] - Render amber budget line when true
    bool                            SecondaryGuideEnabled = false; // [-] - Render secondary guide line when true
    bool                            FixedPlotScale    = false; // [-] - Plot [PlotMinBound, PlotMaxBound] without horizontal shift
    ColorRgba                       AccentColor       { 142, 198, 152, 240 }; // [-] - Plot stroke & accent color
    std::vector<DiagnosticsStatRow> Rows              {}; // [-] - Bottom key-value statistics rows
};

struct DiagnosticsOverlaySession
{
    bool                Visible             = false;                         // [-]  - True when overlay is displayed
    bool                Paused              = false;                         // [-]  - Freeze live sampling when true
    uint32_t            ActiveChannelIndex  = 0u;                            // [idx]- Active metric channel index
    DiagnosticsViewMode ViewMode            = DiagnosticsViewMode::CombinedPlotStats; // [-] - Plot/Stats visibility mode
    uint32_t            HistorySeconds      = 30u;                           // [s]  - Displayed history span (15/30/60)
    bool                CustomLayoutPlaced  = false;                         // [-]  - True after user moved or resized panel
    Rectangle2D         PanelBounds         {};                              // [px] - Current overlay bounds in host coordinates
    bool                DraggingMove        = false;                         // [-]  - Header drag active
    bool                DraggingResize      = false;                         // [-]  - Bottom-right resize grip active
    Point2D             DragStartCursor     {};                              // [px] - Pointer position at drag start
    Rectangle2D         DragStartBounds     {};                              // [px] - Panel bounds at drag start
};

struct DiagnosticsOverlaySpec
{
    const char*                               Title                    = "Diagnostics"; // [-] - Header title
    const char*                               ShortcutBadge            = "F3";          // [-] - Header shortcut tag
    const char*                               FooterSampleLabel        = "120 samples"; // [-] - Footer right label
    bool                                      AllowDragResize          = true;          // [-] - Enable header drag & corner resize
    bool                                      ShowViewSpanControls     = false;         // [-] - Show Plot/Stats & 15/30/60s toggles
    float                                     DefaultWidthPx           = 324.0f;        // [px]- Initial panel width
    float                                     DefaultHeightPx          = 432.0f;        // [px]- Initial panel height
    std::span<const DiagnosticsMetricChannel> Channels                 {};              // [-] - Available metric channels
};

struct DiagnosticsOverlayResult
{
    bool     ClosePressed       = false; // [-] - User clicked header close button
    bool     ResetLayoutPressed = false; // [-] - User clicked layout reset button
    bool     PauseToggled       = false; // [-] - User clicked Pause / Resume samples
    bool     ChannelChanged     = false; // [-] - User switched metric tab
    uint32_t SelectedChannel    = 0u;    // [idx]- Active channel index after interaction
    bool     Interacted         = false; // [-] - True when pointer landed inside overlay or dragged
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class DiagnosticsOverlay
{
public:
    [[nodiscard]] static DiagnosticsOverlayResult Construct(
        PixelSpace&                   Surface,
        Rectangle2D                   HostBounds,
        const DiagnosticsOverlaySpec& Spec,
        DiagnosticsOverlaySession&    Session,
        const ControlPointer&         Pointer,
        float                         Opacity = 1.0f) noexcept;
};

} // namespace Frontier
