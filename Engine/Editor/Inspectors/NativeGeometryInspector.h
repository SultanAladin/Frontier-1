/*============================================================================================================================================
                                                        NATIVEGEOMETRYINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance NativeGeometryInspector (Category 2 Geometry: Native Engine .GeometrySpace, Cluster LOD, Local SDF)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class NativeGeometryInspector
{
public:
    NativeGeometryInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept
    {
        return "Native engine geometry  ·  cluster LOD";
    }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, NativeGeometryData& Instance,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return SpecimenPreview.QueryUploadCount(); }

private:
    void UpdateSpecimenSurface(const NativeGeometryData& Instance);

    uint32_t                ActivePage      = 0u;
    uint32_t                ActiveDragId    = 0u;
    InspectorPreviewSurface SpecimenPreview {};
};

} // namespace Frontier
