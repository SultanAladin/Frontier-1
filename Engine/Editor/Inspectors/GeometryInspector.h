/*============================================================================================================================================
                                                           GEOMETRYINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Geometry/Primitive/Vehicle Inspector (Overview with retained PBR preview, Transform, Material, Physics — no Workspaces)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class GeometryInspector
{
public:
    GeometryInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle(const OutlinerRecordEntry& Entry) noexcept
    {
        return Entry.Subtitle.empty() ? "Geometry  ·  transform  ·  OpenPBR" : Entry.Subtitle.c_str();
    }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, GeometrySpecimenData& Instance,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return SpecimenPreview.QueryUploadCount(); }

private:
    void UpdateSpecimenSurface(const OutlinerRecordEntry& Entry, const GeometrySpecimenData& Instance);

    uint32_t                ActivePage      = 0u;
    uint32_t                ActiveDragId    = 0u;
    InspectorPreviewSurface SpecimenPreview {};
};

} // namespace Frontier
