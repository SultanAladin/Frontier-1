/*============================================================================================================================================
                                                            EXPOSUREINSPECTOR.H
============================================================================================================================================*/
// 🧩 ProjectZeroEditor-parity Exposure component inspector (2 tabs: Overview, Settings)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class ExposureInspector
{
public:
    ExposureInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Photographic EV exposure & display tone control"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, ExposureInstanceData& Instance,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = (PageIndex < 2u) ? PageIndex : 1u; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
