/*============================================================================================================================================
                                                             LUNARINSPECTOR.H
============================================================================================================================================*/
// 🧩 Standalone multi-instance Lunar Inspector (supports 2+ Moons; owns ray-cast LunarSurfaceAtlas globe previews; Overview, Appearance, Position)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

#include <array>

namespace Frontier
{

class LunarInspector
{
public:
    LunarInspector() noexcept = default;

    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Lunar body  ·  multi-instance"; }

    [[nodiscard]] float Construct(
        PixelSpace&             Surface,
        float                   LeftX,
        float                   TopY,
        float                   InnerWidth,
        OutlinerRecordEntry&    Entry,
        LunarInstanceData&      Instance,
        uint32_t                DisplayOrdinal,
        const PaneInputReading& Input,
        bool&                   OutMutated,
        bool&                   OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }
    [[nodiscard]] uint32_t QueryActivePage() const noexcept { return ActivePage; }
    [[nodiscard]] uint32_t QueryPhasePreviewUploads() const noexcept { return PhaseGlobe.QueryUploadCount(); }

private:
    static void RasterizeLunarGlobe(
        InspectorPreviewSurface&                       Target,
        const GeometricRaster::LunarBodyConfiguration& Moon,
        uint32_t                                       WidthPx,
        uint32_t                                       HeightPx,
        float                                          DiscRadiusPx,
        bool                                           ApplyPhaseShadow,
        bool                                           IncludeNightBackdrop,
        float                                          NaturalRingRadiusPx);

    uint32_t                                ActivePage   = 0u;
    uint32_t                                ActiveDragId = 0u;
    InspectorPreviewSurface                 PhaseGlobe   {};
    InspectorPreviewSurface                 LookGlobe    {};
    InspectorPreviewSurface                 TurnGlobe    {};
    std::array<InspectorPreviewSurface, 6u> PresetGlobes {};
};

} // namespace Frontier
