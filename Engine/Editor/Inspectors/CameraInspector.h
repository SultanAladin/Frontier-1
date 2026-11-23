/*============================================================================================================================================
                                                            CAMERAINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Camera Inspector — verbatim 1:1 pure-vector port of ProjectZeroEditor F78 (Overview, Mount, Focus, Motion blur, Effects)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Controls/InspectorVectorGraphics.h"
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
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return 0u; } // Pure-vector: zero texture uploads
    [[nodiscard]] uint32_t QueryMountUploads()   const noexcept { return 0u; } // Pure-vector: zero texture uploads

private:
    uint32_t                                ActivePage       = 0u;
    uint32_t                                ActiveDragId     = 0u;
    InspectorVector::TransformTableDragState TransformDrag   {};
};

} // namespace Frontier
