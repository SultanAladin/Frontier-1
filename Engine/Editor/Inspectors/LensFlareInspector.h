/*============================================================================================================================================
                                                          LENSFLAREINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Lens Flare Inspector (4 tabs: Overview, Optics, Halo, Baking)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LensFlareInspector
{
public:
    LensFlareInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Camera optical effect \xC2\xB7 sun-driven"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LensFlareInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    void UpdateFlareComposite(const GeometricRaster::LensFlareConfiguration& Flare);
    void UpdateFlareAtlas(const GeometricRaster::LensFlareConfiguration& Flare);

    uint32_t                ActivePage        = 0u;
    uint32_t                ActiveDragId      = 0u;
    bool                    AnamorphicEnabled = true;
    bool                    StreaksEnabled    = true;
    bool                    HalosEnabled      = true;
    float                   RayPairs          = 6.0f;
    float                   StreakGain        = 1.0f;
    float                   HaloRadius        = 0.28f;
    float                   HaloWidth         = 0.035f;
    float                   ChromaticDisp     = 0.6f;
    InspectorPreviewSurface FlareComposite    {};
    InspectorPreviewSurface FlareAtlas        {};
};

} // namespace Frontier
