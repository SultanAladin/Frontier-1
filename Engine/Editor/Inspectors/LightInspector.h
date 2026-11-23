/*============================================================================================================================================
                                                             LIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Light Inspector (Point/Spot/Area/Rect/Tube; owns retained photometric beam preview; Overview, Intensity, Beam, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LightInspector
{
public:
    LightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Photometric scene luminaire"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LightInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return BeamPreview.QueryUploadCount(); }

private:
    void UpdateBeamSurface(const LightInstanceData& Instance);

    uint32_t                ActivePage   = 0u;
    uint32_t                ActiveDragId = 0u;
    InspectorPreviewSurface BeamPreview  {};
};

} // namespace Frontier
