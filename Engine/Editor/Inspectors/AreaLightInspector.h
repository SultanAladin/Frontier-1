/*============================================================================================================================================
                                                          AREALIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Area Light Inspector — verbatim 1:1 pure-vector port of ProjectZeroEditor LightPanel (FollowUp55_Light-Overview.png)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Controls/InspectorVectorGraphics.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class AreaLightInspector
{
public:
    AreaLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Rectangular or disk soft emitter (LTC)"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, AreaLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return 0u; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
