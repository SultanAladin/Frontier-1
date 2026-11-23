/*============================================================================================================================================
                                                        CAMERASETTINGSINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Camera Settings Inspector — verbatim 1:1 pure-vector port of ProjectZeroEditor F77/F78 Render Settings

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Components/Controls/InspectorVectorGraphics.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class CameraSettingsInspector
{
public:
    CameraSettingsInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Camera settings · render pipeline"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, CameraSettingsData& Instance,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return 0u; } // Pure-vector: zero CPU rasterization / texture uploads

private:
    uint32_t ActivePage   = 0u;
    uint32_t ActiveDragId = 0u;
};

} // namespace Frontier
