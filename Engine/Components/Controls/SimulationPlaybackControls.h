/*============================================================================================================================================
                                                         SIMULATIONPLAYBACKCONTROLS.H
============================================================================================================================================*/
// 🧩 Reusable 52px simulation playback transport strip (Reset, Play/Pause, SingleStep, Timecode, TimeScale, Live indicator & Burst trigger)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <cstdint>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SimulationPlaybackReading
{
    bool   Playing            = true;  // [-]   - True while simulation clock advances continuously
    double ElapsedSeconds     = 0.0;   // [s]   - Active simulation clock time
    float  TimeScale          = 1.0f;  // [x]   - Playback speed multiplier (0.25, 0.5, 1.0, 2.0)
    bool   LiveUncached       = true;  // [-]   - True when running real-time solver instead of baked disk playback
    bool   ShowBurstTrigger   = true;  // [-]   - True to display the right-aligned Burst [B] action pill
    const char* BurstCaption  = "Burst"; // [-] - Caption for right-aligned trigger pill
    const char* BurstHotkey   = "B";     // [-] - Shortcut badge inside burst pill
};

struct SimulationPlaybackResult
{
    bool TogglePlayPressed = false; // [-] - User toggled Play/Pause
    bool ResetPressed      = false; // [-] - User triggered Reset & re-seed
    bool SingleStepPressed = false; // [-] - User requested single deterministic step
    bool BurstPressed      = false; // [-] - User fired burst trigger
    bool TimeScaleChanged  = false; // [-] - User cycled playback speed multiplier
    float NewTimeScale     = 1.0f;  // [x] - Selected speed multiplier when TimeScaleChanged is true
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class SimulationPlaybackControls
{
public:
    static constexpr float RecommendedHeightPx = 52.0f; // [px] - Canonical Fluid/Sun playback bar height

    [[nodiscard]] static SimulationPlaybackResult Construct(
        PixelSpace&                      Surface,
        Rectangle2D                      Bounds,
        const SimulationPlaybackReading& Reading,
        const ControlPointer&            Pointer,
        float                            Opacity = 1.0f) noexcept;
};

} // namespace Frontier
