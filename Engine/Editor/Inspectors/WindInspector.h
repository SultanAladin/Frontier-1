/*============================================================================================================================================
                                                             WINDINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Wind Inspector (multiple Winds vector-sum cleanly; Overview compass + Quick controls, Air velocity & shear)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class WindInspector
{
public:
    WindInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Atmospheric wind vector field"; }

    [[nodiscard]] float Construct(
        PixelSpace&             Surface,
        float                   LeftX,
        float                   TopY,
        float                   InnerWidth,
        OutlinerRecordEntry&    Entry,
        WindInstanceData&       Instance,
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
