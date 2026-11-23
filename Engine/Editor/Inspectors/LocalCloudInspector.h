/*============================================================================================================================================
                                                          LOCALCLOUDINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone Local Cloud Volume Inspector (multi-instance bounded cloud volumes; 4 pages: Overview, Coverage, Layer, Body)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LocalCloudInspector
{
public:
    LocalCloudInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Bounded cloud volume"; }

    [[nodiscard]] float Construct(
        PixelSpace&             Surface,
        float                   LeftX,
        float                   TopY,
        float                   InnerWidth,
        OutlinerRecordEntry&    Entry,
        LocalCloudInstanceData& Instance,
        const PaneInputReading& Input,
        bool&                   OutMutated,
        bool&                   OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
