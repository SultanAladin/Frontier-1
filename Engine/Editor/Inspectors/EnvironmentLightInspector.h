/*============================================================================================================================================
                                                        ENVIRONMENTLIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Environment Light Inspector (Sky hemisphere radiance, gain & indirect GI)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class EnvironmentLightInspector
{
public:
    EnvironmentLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Sky hemisphere radiance & GI"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, EnvironmentLightInstanceData& Instance,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return HemispherePreview.QueryUploadCount(); }

private:
    void UpdateHemisphereSurface(const EnvironmentLightInstanceData& Instance);

    uint32_t                ActivePage        = 0u;
    uint32_t                ActiveDragId      = 0u;
    InspectorPreviewSurface HemispherePreview {};
};

} // namespace Frontier
