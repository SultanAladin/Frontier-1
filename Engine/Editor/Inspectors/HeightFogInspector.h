/*============================================================================================================================================
                                                          HEIGHTFOGINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Height Fog Inspector (Overview, Density with altitude extinction curve, Light)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class HeightFogInspector
{
public:
    HeightFogInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Exponential height fog"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, HeightFogInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
