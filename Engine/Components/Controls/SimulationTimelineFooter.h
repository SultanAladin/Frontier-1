/*============================================================================================================================================
                                                        SIMULATIONTIMELINEFOOTER.H
============================================================================================================================================*/
// 🧩 Reusable playback footer strip (To-Start, Play/Pause, Step, Clock, Scrubbable Kinetic Curve & Event Ticks, Speed Strip, Loop)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstdint>
#include <span>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SimulationTimelineMarker
{
    float       NormalizedFraction = 0.0f; // [0-1] - Normalized marker placement along timeline track
    ColorRgba   Tint               {};     // [-]   - Marker stroke/dot tint
    const char* Caption            = "";   // [-]   - Short event tag rendered above/on marker
};

struct SimulationTimelineFooterReading
{
    double                                    ElapsedSeconds       = 0.0;                 // [s]     - Active playback time
    double                                    DurationSeconds      = 4.0;                 // [s]     - Total timeline duration
    bool                                      Playing              = false;               // [-]     - True while playhead advances
    bool                                      LoopEnabled          = true;                // [-]     - True when playback loops at end
    bool                                      HasBakedTimeline     = false;               // [-]     - True when pose/energy curve exists
    float                                     TimeScale            = 1.0f;                // [x]     - Speed multiplier (0.25, 0.5, 1.0, 2.0)
    float                                     CurrentKineticJoules = 0.0f;                // [J]     - Instantaneous system kinetic energy
    uint32_t                                  MovingBodyCount      = 0u;                  // [count] - Awake moving bodies at current time
    const char*                               PhaseCaption         = "EDIT · NOT BAKED";  // [-]     - Clock subtitle
    const char*                               EmptyPromptText      = "Timeline bakes when you simulate · Shift+2 or Space to play";
    std::span<const float>                    EnergyCurveSamples   {};                    // [J]     - Sampled kinetic curve across timeline
    std::span<const SimulationTimelineMarker> EventMarkers         {};                    // [-]     - Optional discrete event markers
};

struct SimulationTimelineFooterSession
{
    bool        Scrubbing       = false; // [-]  - True while pointer drags playhead along timeline track
    Rectangle2D LastTrackBounds {};      // [px] - Cached timeline track rectangle
};

struct SimulationTimelineFooterResult
{
    bool   ToStartPressed      = false; // [-] - Rewind to t=0 pressed
    bool   TogglePlayPressed   = false; // [-] - Play / Pause toggled
    bool   SingleStepPressed   = false; // [-] - Single frame step pressed
    bool   Scrubbed            = false; // [-] - Playhead dragged or clicked to new timestamp
    double ScrubbedTimeSeconds = 0.0;   // [s] - Resulting timestamp inside [0, DurationSeconds]
    bool   TimeScaleChanged    = false; // [-] - User clicked 0.25x / 0.5x / 1x / 2x speed button
    float  NewTimeScale        = 1.0f;  // [x] - Selected playback multiplier
    bool   LoopToggled         = false; // [-] - User toggled loop button
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class SimulationTimelineFooter
{
public:
    static constexpr float RecommendedHeightPx = 56.0f; // [px] - Canonical playback footer strip height

    [[nodiscard]] static SimulationTimelineFooterResult Construct(
        PixelSpace&                            Surface,
        Rectangle2D                            Bounds,
        const SimulationTimelineFooterReading& Reading,
        SimulationTimelineFooterSession&       Session,
        const ControlPointer&                  Pointer,
        float                                  Opacity = 1.0f) noexcept;
};

} // namespace Frontier
