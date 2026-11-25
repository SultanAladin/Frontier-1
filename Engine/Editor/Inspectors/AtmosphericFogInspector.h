/*============================================================================================================================================
                                                        ATMOSPHERICFOGINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Atmospheric Fog Inspector (AerialRidges Overview + FogSpectrum Spectrum)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class AtmosphericFogInspector
{
public:
    AtmosphericFogInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Aerial perspective \xC2\xB7 surface-distance fog"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, AtmosphericFogInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    void UpdateRidgesSurface(const AtmosphericFogInstanceData& Instance);

    uint32_t                ActivePage           = 0u;
    uint32_t                ActiveDragId         = 0u;
    float                   ProbeDistanceMeters  = 200.0f;
    float                   SampleWavelengthNm   = 550.0f;
    InspectorPreviewSurface RidgePreview         {};
};

} // namespace Frontier
