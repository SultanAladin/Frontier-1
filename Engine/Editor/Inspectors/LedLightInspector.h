/*============================================================================================================================================
                                                         LEDLIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance LED Emitter Inspector (COB star-board 4x4 die array & optic study; Overview, Driver, Optic, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Controls/InspectorVectorGraphics.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LedLightInspector
{
public:
    LedLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "LED emitter · solid-state COB"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LedLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return 0u; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
