/*============================================================================================================================================
                                                         MOTIONBLURINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Motion Blur Inspector (2 tabs: Overview, Settings)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class MotionBlurInspector
{
public:
    MotionBlurInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Shutter-based blur \xC2\xB7 camera and object motion"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, MotionBlurInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage      = 0u;
    uint32_t ActiveDragId    = 0u;
    bool     ObjectMotion    = true;
    bool     CameraMotion    = true;
    uint32_t FrameRateIdx    = 0u; // 0=24fps, 1=30fps, 2=60fps
    float    MaxBlurPercent  = 6.0f;
    float    SampleCount     = 12.0f;
    float    PreviewSpeed    = 1.0f;
};

} // namespace Frontier
