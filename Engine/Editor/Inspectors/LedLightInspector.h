/*============================================================================================================================================
                                                         LEDLIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance LED Emitter Inspector (COB star-board 4x4 die array & optic study; Overview, Driver, Optic, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LedLightInspector
{
public:
    LedLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "LED emitter \xC2\xB7 HTML authoring"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LedLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return PackagePreview.QueryUploadCount(); }

private:
    void UpdatePackageSurface(const LedLightData& Instance);

    uint32_t                ActivePage     = 0u;
    uint32_t                ActiveDragId   = 0u;
    InspectorPreviewSurface PackagePreview {};
};

} // namespace Frontier
