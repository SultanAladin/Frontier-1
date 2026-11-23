/*============================================================================================================================================
                                                          MOTIONBLURINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Motion Blur Inspector (Shutter angle, velocity scale & max sample radius)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class MotionBlurInspector
{
public:
    MotionBlurInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Per-pixel shutter velocity blur"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, MotionBlurInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
