/*============================================================================================================================================
                                                            MOONINSPECTORVIEW.H
============================================================================================================================================*/
// 🧩 Native C++20 Inspector view for 4-slot Moon instances, 6-preset lunar surface gallery, synodic phase, and Az/El crosshair pad

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"

#include <array>
#include <cstdint>

namespace Frontier::Editor::Inspectors
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MoonInspectorTargets
{
    std::array<GeometricRaster::LunarBodyConfiguration, 4u>* Moons      = nullptr; // [-]   - 4-slot lunar body array
    uint32_t*                                                ActiveSlot = nullptr; // [idx] - Mutable active moon slot [0..3]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   MOONINSPECTORVIEW
//------------------------------------------------------------------------------------------------------------------------

class MoonInspectorView
{
public:
    MoonInspectorView() noexcept = default;

    void ConfigureScrollOffset(float OffsetY) noexcept;
    [[nodiscard]] float QueryScrollOffset() const noexcept;

    [[nodiscard]] bool Render(
        PixelSpace&                 Surface,
        Rectangle2D                 Bounds,
        const MoonInspectorTargets& Targets,
        const PaneInputReading&     Input,
        double                      DeltaSeconds);

private:
    float       ScrollOffsetY  = 0.0f;  // [px] - Vertical scroll offset
    float       LastContentH   = 0.0f;  // [px] - Measured scroll document height
    int32_t     ActiveSliderId = -1;    // [id] - Active dragging scalar slider tag
    bool        SlotSheetOpen  = false; // [-]  - Active slot dropdown sheet open indicator
    bool        DraggingSkyPad = false; // [-]  - Azimuth/Elevation pad drag indicator
    Rectangle2D SlotTriggerRect{};      // [px] - Trigger bounds for active slot sheet
};

inline void MoonInspectorView::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = OffsetY;
}

inline float MoonInspectorView::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

} // namespace Frontier::Editor::Inspectors
