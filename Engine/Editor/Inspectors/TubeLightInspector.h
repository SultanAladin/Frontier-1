/*============================================================================================================================================
                                                          TUBELIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Tube Light Inspector (Linear cylindrical radiance study; Overview, Output, Emitter, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class TubeLightInspector
{
public:
    TubeLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Tube light \xC2\xB7 HTML authoring"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, TubeLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return TubePreview.QueryUploadCount(); }

private:
    void UpdateTubeSurface(const TubeLightData& Instance);

    uint32_t                ActivePage   = 0u;
    uint32_t                ActiveDragId = 0u;
    InspectorPreviewSurface TubePreview  {};
};

} // namespace Frontier
