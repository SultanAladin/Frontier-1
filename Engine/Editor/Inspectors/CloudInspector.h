/*============================================================================================================================================
                                                             CLOUDINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone Cloud Inspector (multi-instance cloud layers; 6 pages: Overview, Coverage, Layer, Body, Wind, Quality)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class CloudInspector
{
public:
    CloudInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Global cloud layer"; }

    [[nodiscard]] float Construct(
        PixelSpace&             Surface,
        float                   LeftX,
        float                   TopY,
        float                   InnerWidth,
        OutlinerRecordEntry&    Entry,
        CloudInstanceData&      Instance,
        const PaneInputReading& Input,
        bool&                   OutMutated,
        bool&                   OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }

private:
    void UpdateCloudDeckSurface(const GeometricRaster::CloudDeckConfiguration& Clouds);

    uint32_t                ActivePage       = 0u;
    uint32_t                ActiveDragId     = 0u;
    InspectorPreviewSurface CloudDeckPreview {};
};

} // namespace Frontier
