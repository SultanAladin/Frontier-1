/*============================================================================================================================================
                                                          POSTPROCESSINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Post Process Inspector (Overview switches, Tone & exposure curve, Colour grading)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class PostProcessInspector
{
public:
    PostProcessInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Tonemap  ·  anti-aliasing  ·  grading"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, PostProcessInstanceData& Instance,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
