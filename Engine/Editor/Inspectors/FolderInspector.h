/*============================================================================================================================================
                                                            FOLDERINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone generic Folder Inspector for any Outliner container (Overview metrics, bulk visibility, colour accent, contents list, notes)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Panes/OutlinerPanel.h"

namespace Frontier
{

class FolderInspector
{
public:
    FolderInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Generic folder container"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& FolderEntry, OutlinerPanel& Outliner,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage = 0u;
};

} // namespace Frontier
