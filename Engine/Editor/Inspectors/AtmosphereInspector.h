/*============================================================================================================================================
                                                           ATMOSPHEREINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone Atmosphere Inspector (6 pages: Overview, Scattering, Density, Sky colour, View, Baking LUTs) with own retained preview surfaces

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class AtmosphereInspector
{
public:
    AtmosphereInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Procedural sky"; }

    [[nodiscard]] float Construct(
        PixelSpace&                               Surface,
        float                                     LeftX,
        float                                     TopY,
        float                                     InnerWidth,
        OutlinerRecordEntry&                      Entry,
        AtmosphereInstanceData&                   Instance,
        const GeometricRaster::SolarCelestialConfiguration& ActiveSolar,
        const PaneInputReading&                   Input,
        bool&                                     OutMutated,
        bool&                                     OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QueryTransmittanceLutUploads() const noexcept { return TransmittanceLut.QueryUploadCount(); }
    [[nodiscard]] uint32_t QuerySkyViewLutUploads() const noexcept { return SkyViewLut.QueryUploadCount(); }

private:
    void UpdateLimbSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium, float SunAtLimbDeg, bool FullDisc);
    void UpdateTransmittanceSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium);
    void UpdateSkyViewSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium, const GeometricRaster::SolarCelestialConfiguration& Solar);

    uint32_t                ActivePage         = 0u;
    uint32_t                ActiveDragId       = 0u;
    InspectorPreviewSurface LimbPreview        {};
    InspectorPreviewSurface OrbitalPreview     {};
    InspectorPreviewSurface TransmittanceLut   {};
    InspectorPreviewSurface SkyViewLut         {};
};

} // namespace Frontier
