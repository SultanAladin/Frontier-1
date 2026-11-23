/*============================================================================================================================================
                                                          POINTLIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Point Light Inspector (Radial falloff preview; Overview, Output, Attenuation, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class PointLightInspector
{
public:
    PointLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Point light · omnidirectional"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, PointLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return FalloffPreview.QueryUploadCount(); }

private:
    void UpdateFalloffSurface(const PointLightData& Instance);

    uint32_t                ActivePage     = 0u;
    uint32_t                ActiveDragId   = 0u;
    InspectorPreviewSurface FalloffPreview {};
};

} // namespace Frontier
