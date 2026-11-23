/*============================================================================================================================================
                                                           RAINBOWINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Rainbow Inspector (primary 42 deg & secondary 51 deg optical bow dispersion)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class RainbowInspector
{
public:
    RainbowInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Atmospheric optical bow"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, RainbowInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
