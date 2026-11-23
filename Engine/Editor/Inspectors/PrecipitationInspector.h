/*============================================================================================================================================
                                                        PRECIPITATIONINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Precipitation Inspector (Rain / Snow / Hail rate, drop diameter & wind slant)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class PrecipitationInspector
{
public:
    PrecipitationInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Atmospheric precipitation"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, PrecipitationInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
