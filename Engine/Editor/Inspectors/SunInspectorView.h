/*============================================================================================================================================
                                                             SUNINSPECTORVIEW.H
============================================================================================================================================*/
// 🧩 Native C++20 Inspector view for Sun ephemeris, interactive orbital arc dial, solar disc radiance, and atmospheric scattering

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"

#include <cstdint>

namespace Frontier::Editor::Inspectors
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SunInspectorTargets
{
    GeometricRaster::SolarCelestialConfiguration*    Solar  = nullptr; // [-] - Mutable solar ephemeris & disc settings
    GeometricRaster::AtmosphericMediumConfiguration* Medium = nullptr; // [-] - Mutable Rayleigh/Mie/Ozone medium settings
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SUNINSPECTORVIEW
//------------------------------------------------------------------------------------------------------------------------

class SunInspectorView
{
public:
    SunInspectorView() noexcept = default;

    void ConfigureScrollOffset(float OffsetY) noexcept;
    [[nodiscard]] float QueryScrollOffset() const noexcept;

    [[nodiscard]] bool Render(
        PixelSpace&                Surface,
        Rectangle2D                Bounds,
        const SunInspectorTargets& Targets,
        const PaneInputReading&    Input,
        double                     DeltaSeconds);

private:
    enum class OpenDropdownKind : uint8_t
    {
        None             = 0u,
        TintSourceSelect = 1u,
        CycleSpeedSelect = 2u,
    };

    float            ScrollOffsetY    = 0.0f;                    // [px] - Vertical scroll offset
    float            LastContentH     = 0.0f;                    // [px] - Last measured layout height
    int32_t          ActiveSliderId   = -1;                      // [id] - Active dragging scalar slider tag
    bool             DraggingOrbit    = false;                   // [-]  - Interactive orbit arc drag indicator
    float            OrbitDragStartX  = 0.0f;                    // [px] - Pointer X at orbit drag start
    float            OrbitDragStartY  = 0.0f;                    // [px] - Pointer Y at orbit drag start
    float            OrbitOriginAz    = 0.0f;                    // [deg]- Azimuth snapshot at orbit drag start
    float            OrbitOriginEl    = 0.0f;                    // [deg]- Elevation snapshot at orbit drag start
    OpenDropdownKind OpenDropdown     = OpenDropdownKind::None;  // [-]  - Currently open selector sheet
    Rectangle2D      OpenTriggerRect  {};                        // [px] - Trigger bounds for deferred sheet
};

inline void SunInspectorView::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = OffsetY;
}

inline float SunInspectorView::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

} // namespace Frontier::Editor::Inspectors
