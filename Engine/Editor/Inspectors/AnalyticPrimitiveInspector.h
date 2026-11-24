/*============================================================================================================================================
                                                       ANALYTICPRIMITIVEINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance AnalyticPrimitiveInspector (Category 1 Geometry: Sphere, Box, Cylinder, Cone, Torus, Capsule, Plane)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class AnalyticPrimitiveInspector
{
public:
    AnalyticPrimitiveInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept
    {
        return "Geometry";
    }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, AnalyticPrimitiveData& Instance,
        GeometricRaster::WorldSceneSpecification& Scene,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return PrimitivePreview.QueryUploadCount(); }

private:
    void UpdatePrimitiveSurface(const AnalyticPrimitiveData& Instance);

    uint32_t                ActivePage       = 0u;
    uint32_t                ActiveDragId     = 0u;
    InspectorPreviewSurface PrimitivePreview {};
};

} // namespace Frontier
