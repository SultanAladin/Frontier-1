/*============================================================================================================================================
                                                             WINDINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Wind Inspector (Anemometer Overview + Air Atmospheric Modifiers)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class WindInspector
{
public:
    WindInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Wind / Air \xC2\xB7 shared advection field"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, WindInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
    bool     TallTrace    = false;
};

} // namespace Frontier
