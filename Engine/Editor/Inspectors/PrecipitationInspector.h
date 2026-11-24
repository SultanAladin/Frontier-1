/*============================================================================================================================================
                                                        PRECIPITATIONINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Precipitation Inspector (3 tabs: Overview, Particles, Simulation)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class PrecipitationInspector
{
public:
    PrecipitationInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Clouds / Precipitation \xC2\xB7 world-space simulation"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, PrecipitationInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage         = 0u;
    uint32_t ActiveDragId       = 0u;
    bool     FollowWind         = true;
    bool     SpawnFromClouds    = true;
    bool     GroundCollision    = true;
    float    IntensityMmPerHour = 12.0f;
    float    DensityScale       = 1.5f;
    float    ParticleSizeScale  = 1.0f;
    float    WindDrift          = 0.35f;
    float    Accumulation       = 0.4f;
};

} // namespace Frontier
