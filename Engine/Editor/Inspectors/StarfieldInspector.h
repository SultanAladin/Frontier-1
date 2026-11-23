/*============================================================================================================================================
                                                           STARFIELDINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Starfield Inspector (Overview 300-star chart, Motion twinkle + pole, Renderer, Baking equirectangular star sheet)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class StarfieldInspector
{
public:
    StarfieldInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Starfield  ·  scintillation  ·  celestial pole"; }

    [[nodiscard]] float Construct(
        PixelSpace&             Surface,
        float                   LeftX,
        float                   TopY,
        float                   InnerWidth,
        OutlinerRecordEntry&    Entry,
        StarfieldInstanceData&  Instance,
        const PaneInputReading& Input,
        bool&                   OutMutated,
        bool&                   OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QueryStarSheetUploads() const noexcept { return StarSheetBake.QueryUploadCount(); }

private:
    void UpdateStarSheetSurface(const GeometricRaster::StarfieldConfiguration& Stars, bool EntryVisible);

    uint32_t                ActivePage         = 0u;
    uint32_t                ActiveDragId       = 0u;
    bool                    BakeSettingsOpen   = false;
    uint32_t                BakeResolutionTier = 1u;
    InspectorPreviewSurface StarSheetBake      {};
};

} // namespace Frontier
