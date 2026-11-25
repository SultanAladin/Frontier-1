/*============================================================================================================================================
                                                          STARFIELDINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Stars (Starfield) Inspector (4 tabs: Overview, Motion, Renderer, Baking)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class StarfieldInspector
{
public:
    StarfieldInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Catalogue field \xC2\xB7 scintillation \xC2\xB7 celestial pole"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, StarfieldInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QueryStarSheetUploads() const noexcept { return StarSheetBake.QueryUploadCount(); }

private:
    void UpdateStarChartSurface(const GeometricRaster::StarfieldConfiguration& Stars);
    void UpdateStarSheetSurface(const GeometricRaster::StarfieldConfiguration& Stars, bool EntryVisible);

    uint32_t                ActivePage    = 0u;
    uint32_t                ActiveDragId  = 0u;
    InspectorPreviewSurface StarChart     {};
    InspectorPreviewSurface StarSheetBake {};
};

} // namespace Frontier
