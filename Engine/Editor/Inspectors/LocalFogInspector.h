/*============================================================================================================================================
                                                           LOCALFOGINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Local Fog Inspector (FogGraph Overview + FogBeamChamber Medium + FogShapePanel Shape)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

class LocalFogInspector
{
public:
    LocalFogInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Local volumetric fog \xC2\xB7 bounded medium"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, LocalFogInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    void UpdateShapeGlowSurface(const LocalFogInstanceData& Instance);

    uint32_t                ActivePage          = 0u;
    uint32_t                ActiveDragId        = 0u;
    float                   ProbeDistanceMeters = 200.0f;
    InspectorPreviewSurface ShapeGlowPreview    {};
};

} // namespace Frontier
