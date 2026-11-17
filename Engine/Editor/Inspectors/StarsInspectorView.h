/*============================================================================================================================================
                                                           STARSINSPECTORVIEW.H
============================================================================================================================================*/
// 🧩 Native C++20 Inspector view for deterministic Starfield, scintillation monitor waveform, and interactive celestial pole dial

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"

#include <cstdint>

namespace Frontier::Editor::Inspectors
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct StarsInspectorTargets
{
    GeometricRaster::StarfieldConfiguration* Stars             = nullptr; // [-] - Mutable starfield settings
    float                                    SimulationSeconds = 0.0f;    // [s] - Elapsed simulation clock for scintillation wave
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   STARSINSPECTORVIEW
//------------------------------------------------------------------------------------------------------------------------

class StarsInspectorView
{
public:
    StarsInspectorView() noexcept = default;

    void ConfigureScrollOffset(float OffsetY) noexcept;
    [[nodiscard]] float QueryScrollOffset() const noexcept;

    [[nodiscard]] bool Render(
        PixelSpace&                  Surface,
        Rectangle2D                  Bounds,
        const StarsInspectorTargets& Targets,
        const PaneInputReading&      Input,
        double                       DeltaSeconds);

private:
    float   ScrollOffsetY   = 0.0f;  // [px] - Vertical scroll offset
    float   LastContentH    = 0.0f;  // [px] - Measured layout height
    int32_t ActiveSliderId  = -1;    // [id] - Active dragging scalar slider tag
    bool    DraggingPole    = false; // [-]  - Celestial pole dial drag indicator
};

inline void StarsInspectorView::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = OffsetY;
}

inline float StarsInspectorView::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

} // namespace Frontier::Editor::Inspectors
