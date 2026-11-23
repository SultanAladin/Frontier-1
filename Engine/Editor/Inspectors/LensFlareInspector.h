/*============================================================================================================================================
                                                           LENSFLAREINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Lens Flare Inspector (Anamorphic / Spherical / Clean optical flare & ghosting)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LensFlareInspector
{
public:
    LensFlareInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Anamorphic & spherical optical flare"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LensFlareInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
