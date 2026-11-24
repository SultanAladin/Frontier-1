/*============================================================================================================================================
                                                        CAMERASETTINGSINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Camera Settings Inspector (formerly Render Engine - F77/F78: PBR/SDF/Ray traced/MatCap hero + per-renderer pages + Output)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class CameraSettingsInspector
{
public:
    CameraSettingsInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Render type, its shadows, lighting, GI, AO and reflections, quality and output"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, CameraSettingsData& Instance,
        ViewportDisplaySettings* DisplaySettings,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryPreviewUploads() const noexcept { return RendererHeroPreview.QueryUploadCount(); }

private:
    void UpdateRendererHeroSurface(const CameraSettingsData& Instance);

    uint32_t                ActivePage          = 0u;
    uint32_t                ActiveDragId        = 0u;
    InspectorPreviewSurface RendererHeroPreview {};
};

} // namespace Frontier
