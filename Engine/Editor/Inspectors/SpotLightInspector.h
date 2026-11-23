/*============================================================================================================================================
                                                          SPOTLIGHTINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Spot Light Inspector (Beam envelope cone & penumbra preview; Overview, Output, Beam, Placement, Shadows)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class SpotLightInspector
{
public:
    SpotLightInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Spot light · conical beam"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, SpotLightData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return EnvelopePreview.QueryUploadCount(); }

private:
    void UpdateEnvelopeSurface(const SpotLightData& Instance);

    uint32_t                ActivePage      = 0u;
    uint32_t                ActiveDragId    = 0u;
    InspectorPreviewSurface EnvelopePreview {};
};

} // namespace Frontier
