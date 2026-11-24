/*============================================================================================================================================
                                                          ATMOSPHEREINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Atmosphere Inspector (6 tabs: Overview, Scattering, Density, Sky colour, View, Baking)

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
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, AtmosphereInstanceData& Instance,
        const GeometricRaster::SolarCelestialConfiguration& ActiveSolar,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QueryTransmittanceLutUploads() const noexcept { return TransmittanceLut.QueryUploadCount(); }
    [[nodiscard]] uint32_t QuerySkyViewLutUploads() const noexcept { return SkyViewLut.QueryUploadCount(); }

private:
    void UpdateLimbSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium, float SunAtLimbDeg);
    void UpdateSpaceGlobeSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium);
    void UpdateTransmittanceSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium);
    void UpdateSkyViewSurface(const GeometricRaster::AtmosphericMediumConfiguration& Medium, const GeometricRaster::SolarCelestialConfiguration& Solar);

    uint32_t                ActivePage             = 0u;
    uint32_t                ActiveDragId           = 0u;
    float                   ScatterProbeNm         = 550.0f;
    float                   DensityProbeM          = 8000.0f;
    float                   SpaceAltitudeKm        = 15000.0f;
    float                   SpaceSunAngleDeg       = 35.0f;
    float                   SpaceLimbThickness     = 8.0f;
    bool                    AnimateOrbit           = false;
    InspectorPreviewSurface LimbPreview            {};
    InspectorPreviewSurface SpaceGlobePreview      {};
    InspectorPreviewSurface TransmittanceLut       {};
    InspectorPreviewSurface SkyViewLut             {};
};

} // namespace Frontier
