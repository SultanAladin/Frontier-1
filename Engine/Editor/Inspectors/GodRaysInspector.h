/*============================================================================================================================================
                                                           GODRAYSINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance God Rays Inspector (Overview, Optics with phase anisotropy curve)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class GodRaysInspector
{
public:
    GodRaysInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Volumetric crepuscular shafts"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, GodRaysInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
