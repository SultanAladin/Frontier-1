/*============================================================================================================================================
                                                            LUNARINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Moon (Lunar) Inspector (3 tabs: Overview, Appearance, Position)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LunarInspector
{
public:
    LunarInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Lunar bodies \xC2\xB7 six slots"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LunarInstanceData& Instance, uint32_t DisplayOrdinal,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QueryPhasePreviewUploads() const noexcept { return PhaseGlobe.QueryUploadCount(); }

private:
    static void RasterizeLunarGlobe(
        InspectorPreviewSurface& Target, const GeometricRaster::LunarBodyConfiguration& Moon,
        uint32_t WidthPx, uint32_t HeightPx, float DiscRadiusPx,
        bool ApplyPhaseShadow, bool IncludeNightBackdrop, uint8_t OverridePresetIndex);

    uint32_t                ActivePage   = 0u;
    uint32_t                ActiveDragId = 0u;
    InspectorPreviewSurface PhaseGlobe   {};
    InspectorPreviewSurface LunaPreset   {};
    InspectorPreviewSurface EmberPreset  {};
    InspectorPreviewSurface NightPreview {};
    InspectorPreviewSurface TurnGlobe    {};
};

} // namespace Frontier
