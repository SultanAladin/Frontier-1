/*============================================================================================================================================
                                                        ATMOSPHERICFOGINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Atmospheric Fog Inspector (Overview, Density with distance curve, Light)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class AtmosphericFogInspector
{
public:
    AtmosphericFogInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Distance-gated aerial perspective"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, AtmosphericFogInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
