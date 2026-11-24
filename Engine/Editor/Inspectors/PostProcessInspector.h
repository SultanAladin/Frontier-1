/*============================================================================================================================================
                                                         POSTPROCESSINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Post Process Inspector (2 tabs: Overview, Settings)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class PostProcessInspector
{
public:
    PostProcessInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Post processing"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, PostProcessInstanceData& Instance,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage     = 0u;
    uint32_t ActiveDragId   = 0u;
    float    BloomIntensity = 0.05f;
};

} // namespace Frontier
