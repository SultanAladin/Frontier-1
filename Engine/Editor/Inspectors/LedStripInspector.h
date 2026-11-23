/*============================================================================================================================================
                                                          LEDSTRIPINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance LED Strip Inspector (Ribbon channel / SMD emitters / Opal diffuser study; Overview, Output, Layout, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Controls/InspectorVectorGraphics.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LedStripInspector
{
public:
    LedStripInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "LED strip · linear ribbon"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LedStripData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return 0u; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
