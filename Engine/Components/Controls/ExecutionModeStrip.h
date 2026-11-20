/*============================================================================================================================================
                                                            EXECUTIONMODESTRIP.H
============================================================================================================================================*/
// 🧩 Reusable Edit / Simulate / Play-Pause execution phase strip with optional Rewind trigger for studio viewports

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
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ExecutionStripPhase : uint8_t
{
    Edit     = 0u, // [-] - Authoring & parameter editing phase
    Simulate = 1u, // [-] - In-place physics / solver simulation phase
    Play     = 2u, // [-] - Live interactive runtime playback phase
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ExecutionModeStripReading
{
    ExecutionStripPhase ActivePhase          = ExecutionStripPhase::Edit; // [-] - Currently active phase
    bool                PlayPaused           = false;                       // [-] - True while Play phase is paused
    bool                ShowPauseWhenPlaying = true;                        // [-] - Show "Pause" label & pause icon while playing
    bool                ShowRewindButton     = false;                       // [-] - Show leading circular Rewind trigger
    bool                OverlayBlockingInput = false;                       // [-] - Suppress hover/click when modal sheet covers bar
    const char*         EditCaption          = "Edit";                      // [-] - First capsule label
    const char*         SimulateCaption      = "Simulate";                  // [-] - Second capsule label
    const char*         PlayCaption          = "Play";                      // [-] - Third capsule label when idle/paused
    const char*         PauseCaption         = "Pause";                     // [-] - Third capsule label while actively playing
};

struct ExecutionModeStripResult
{
    bool                PhaseChanged        = false;                       // [-] - True when user clicked Edit, Simulate, or Play
    ExecutionStripPhase SelectedPhase       = ExecutionStripPhase::Edit; // [-] - Target phase after click
    bool                PlayToggleRequested = false;                       // [-] - True when user clicked the Play/Pause capsule
    bool                RewindPressed       = false;                       // [-] - True when user clicked the leading Rewind trigger
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ExecutionModeStrip
{
public:
    static constexpr float RecommendedHeightPx = 31.0f; // [px] - Canonical pill capsule height

    [[nodiscard]] static float PreferredStripWidth(bool ShowRewind = false) noexcept;

    [[nodiscard]] static ExecutionModeStripResult Construct(
        PixelSpace&                      Surface,
        Rectangle2D                      Bounds,
        const ExecutionModeStripReading& Reading,
        const ControlPointer&            Pointer,
        float                            Opacity = 1.0f) noexcept;
};

} // namespace Frontier
