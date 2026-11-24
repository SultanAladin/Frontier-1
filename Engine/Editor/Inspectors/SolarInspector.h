/*============================================================================================================================================
                                                             SOLARINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Sun (Solar) Inspector (5 tabs: Overview, Temperature, Sun disc, Dynamic, Baking)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class SolarInspector
{
public:
    SolarInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Directional light"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, SolarInstanceData& Instance,
        const GeometricRaster::AtmosphericMediumConfiguration& ActiveMedium,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QuerySkyViewUploads() const noexcept { return SolarBakeLut.QueryUploadCount(); }

private:
    void UpdateSolarSkySurface(const GeometricRaster::SolarCelestialConfiguration& Solar, uint32_t DesignIndex);
    void UpdateSolarBakeSurface(const GeometricRaster::SolarCelestialConfiguration& Solar);

    uint32_t                ActivePage         = 0u;
    uint32_t                ActiveDragId       = 0u;
    float                   PlanckProbeNm      = 550.0f;
    InspectorPreviewSurface SunNowPreview      {};
    InspectorPreviewSurface SolarBakeLut       {};
};

} // namespace Frontier
