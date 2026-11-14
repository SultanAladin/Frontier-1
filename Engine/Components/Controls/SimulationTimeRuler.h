/*============================================================================================================================================
                                                           SIMULATIONTIMERULER.H
============================================================================================================================================*/
// 🧩 Reusable scrubbable simulation & animation time ruler with step readout, graduated ticks, active track strip & playhead needle

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SimulationTimeRulerReading
{
    double      ElapsedSeconds     = 0.0;    // [s]    - Current playback timestamp
    double      LoopDurationSec    = 30.0;   // [s]    - Full ruler span shown across ticks (e.g. 0s..30s)
    uint32_t    StepCounter        = 0u;     // [step] - Monotonic discrete simulation step count
    const char* HeaderLabel        = "SIMULATION";                  // [-] - Upper-left caption
    const char* StepPrefix         = "STEP";                        // [-] - Lower-left step prefix
    const char* ActiveTrackCaption = "Active release & force field";// [-] - Label inside active track strip
    float       ActiveTrackEndFrac = 0.38f;  // [0-1]  - Highlighted emission/activity span fraction
};

struct SimulationTimeRulerSession
{
    bool        ScrubbingActive  = false; // [-]  - True while pointer drags playhead along ruler
    Rectangle2D LastTrackBounds  {};      // [px] - Cached ruler track hitbox for test inspection
};

struct SimulationTimeRulerResult
{
    bool   Scrubbed       = false; // [-] - True when user clicked or dragged the playhead
    double ScrubbedTimeSec= 0.0;   // [s] - Resulting timestamp inside [0, LoopDurationSec]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class SimulationTimeRuler
{
public:
    static constexpr float RecommendedHeightPx = 44.0f; // [px] - Canonical Fluid/Particle timeline height

    [[nodiscard]] static SimulationTimeRulerResult Construct(
        PixelSpace&                       Surface,
        Rectangle2D                       Bounds,
        const SimulationTimeRulerReading& Reading,
        SimulationTimeRulerSession&       Session,
        const ControlPointer&             Pointer,
        float                             Opacity = 1.0f) noexcept;
};

} // namespace Frontier
