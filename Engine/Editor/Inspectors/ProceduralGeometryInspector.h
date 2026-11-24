/*============================================================================================================================================
                                                      PROCEDURALGEOMETRYINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance ProceduralGeometryInspector (Category 4 Geometry: Parametric Sweep, Terrain, Boolean, Spline, Voronoi)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"
#include "Engine/GeometrySpace/Procedural/ProceduralGeometry.h"

namespace Frontier
{

class ProceduralGeometryInspector
{
public:
    ProceduralGeometryInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept
    {
        return "Procedural geometry  ·  parametric generator";
    }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, ProceduralGeometryData& Instance,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return ProceduralPreview.QueryUploadCount(); }

private:
    void UpdateProceduralSurface(const ProceduralGeometryData& Instance);

    uint32_t                                ActivePage        = 0u;
    uint32_t                                ActiveDragId      = 0u;
    InspectorPreviewSurface                 ProceduralPreview {};
    GeometrySpace::ProceduralGeometryAsset  LiveAsset         {};
};

} // namespace Frontier
