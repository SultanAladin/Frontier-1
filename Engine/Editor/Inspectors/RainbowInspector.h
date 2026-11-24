/*============================================================================================================================================
                                                           RAINBOWINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Rainbow Inspector (3 tabs: Overview, Bow, Baking)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class RainbowInspector
{
public:
    RainbowInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Rainbow \xC2\xB7 liquid-water optics"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, RainbowInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    void UpdateRainbowSurface(const RainbowInstanceData& Instance);

    uint32_t                ActivePage           = 0u;
    uint32_t                ActiveDragId         = 0u;
    bool                    AlexanderBandEnabled = true;
    float                   MinimumPathMeters    = 250.0f;
    InspectorPreviewSurface RainbowPreview       {};
};

} // namespace Frontier
