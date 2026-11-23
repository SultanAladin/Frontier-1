/*============================================================================================================================================
                                                          AREALIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Area / Softbox Light Inspector (Heitz LTC quad/disk aperture preview; Overview, Output, Aperture, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class AreaLightInspector
{
public:
    AreaLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Area / softbox · LTC emitter"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, AreaLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return AperturePreview.QueryUploadCount(); }

private:
    void UpdateApertureSurface(const AreaLightData& Instance);

    uint32_t                ActivePage      = 0u;
    uint32_t                ActiveDragId    = 0u;
    InspectorPreviewSurface AperturePreview {};
};

} // namespace Frontier
