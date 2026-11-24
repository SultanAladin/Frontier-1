/*============================================================================================================================================
                                                           IESLIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance IES Photometric Inspector (Polar C0/C90 candela study + 8 LM-63 presets; Overview, Output, Distribution, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class IesLightInspector
{
public:
    IesLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "IES photometric \xC2\xB7 HTML authoring"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, IesLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return PolarPreview.QueryUploadCount(); }

private:
    void UpdatePolarSurface(const IesLightData& Instance);

    uint32_t                ActivePage   = 0u;
    uint32_t                ActiveDragId = 0u;
    InspectorPreviewSurface PolarPreview {};
};

} // namespace Frontier
