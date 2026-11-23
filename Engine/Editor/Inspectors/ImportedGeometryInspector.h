/*============================================================================================================================================
                                                       IMPORTEDGEOMETRYINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance ImportedGeometryInspector (Category 3 Geometry: Imported glTF/FBX/OBJ/USD/STEP/STL exchange pipeline)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class ImportedGeometryInspector
{
public:
    ImportedGeometryInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept
    {
        return "Imported geometry  ·  exchange pipeline";
    }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, ImportedGeometryData& Instance,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return ExchangePreview.QueryUploadCount(); }

private:
    void UpdateExchangeSurface(const ImportedGeometryData& Instance);

    uint32_t                ActivePage      = 0u;
    uint32_t                ActiveDragId    = 0u;
    InspectorPreviewSurface ExchangePreview {};
};

} // namespace Frontier
