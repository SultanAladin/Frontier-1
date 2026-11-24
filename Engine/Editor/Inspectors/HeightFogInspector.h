/*============================================================================================================================================
                                                          HEIGHTFOGINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone Height Fog Inspector (1:1 HTML parity port of FogCards.jsx + HeightFogVisual.jsx + Inspectors.jsx)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class HeightFogInspector
{
public:
    HeightFogInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Height fog \xC2\xB7 exponential vertical density"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, HeightFogInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    void UpdateLayerSurface(const HeightFogInstanceData& Instance, bool Enabled);
    void UpdateLightStudySurfaces(const HeightFogInstanceData& Instance);

    uint32_t                ActivePage         = 0u;
    uint32_t                ActiveDragId       = 0u;
    InspectorPreviewSurface LayerPreview       {};
    InspectorPreviewSurface TowardsSunPreview  {};
    InspectorPreviewSurface AwayFromSunPreview {};
};

} // namespace Frontier
