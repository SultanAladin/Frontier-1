/*============================================================================================================================================
                                                             SOLARINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone Solar Inspector — verbatim 1:1 port of ProjectZeroEditor Sun inspector (F76 Baking + F81 6392b471 Sun direction)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Controls/InspectorVectorGraphics.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class SolarInspector
{
public:
    SolarInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Directional light"; }

    [[nodiscard]] float Construct(
        PixelSpace&                                           Surface,
        float                                                 LeftX,
        float                                                 TopY,
        float                                                 InnerWidth,
        OutlinerRecordEntry&                                  Entry,
        SolarInstanceData&                                    Instance,
        const GeometricRaster::AtmosphericMediumConfiguration& ActiveMedium,
        const PaneInputReading&                               Input,
        bool&                                                 OutMutated,
        bool&                                                 OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QuerySkyViewUploads() const noexcept { return SolarBakeLut.QueryUploadCount(); }

private:
    void BakeSolarLutSurface(
        const GeometricRaster::AtmosphericMediumConfiguration& Medium,
        const GeometricRaster::SolarCelestialConfiguration&    Solar,
        uint32_t                                               ResolutionTier);

    uint32_t                            ActivePage         = 0u;
    uint32_t                            ActiveDragId       = 0u;
    bool                                BakeSettingsOpen   = false;
    uint32_t                            BakeResolutionTier = 2u; // [idx] - 2 = 1.0x default (192x108)
    InspectorVector::SunOrbitGizmoState OrbitDragState     {};
    InspectorPreviewSurface             SolarBakeLut       {};
};

} // namespace Frontier
