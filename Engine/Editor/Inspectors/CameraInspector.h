/*============================================================================================================================================
                                                            CAMERAINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Camera Inspector (Overview, Lens, Body, Mount [F78 On tripod / Floating], Motion blur, Lens flare)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class CameraInspector
{
public:
    CameraInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Optical lens  ·  sensor gate  ·  mount"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, CameraInstanceData& Instance,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return LensPreview.QueryUploadCount(); }
    [[nodiscard]] uint32_t QueryMountUploads()   const noexcept { return MountPreview.QueryUploadCount(); }

private:
    void UpdateLensSurface(const CameraInstanceData& Instance);
    void UpdateMountSurface(const CameraInstanceData& Instance);

    uint32_t                ActivePage   = 0u;
    uint32_t                ActiveDragId = 0u;
    InspectorPreviewSurface LensPreview  {};
    InspectorPreviewSurface MountPreview {};
};

} // namespace Frontier
