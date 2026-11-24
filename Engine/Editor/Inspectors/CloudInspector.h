/*============================================================================================================================================
                                                            CLOUDINSPECTOR.H
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Global Clouds Inspector (6 tabs: Overview, Coverage, Layer, Body, Wind, Quality)

#pragma once

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"
#include "Engine/Editor/Inspectors/InspectorInstanceRegister.h"

namespace Frontier
{

namespace CloudVisualPrimitives
{

void UpdateSwathPreview(
    InspectorPreviewSurface& Preview,
    float Coverage01, float Extinction, bool IsLocal);

void UpdateDeckSectionPreview(
    InspectorPreviewSurface& Preview,
    float BaseMeters, float ThicknessMeters, float CeilingMeters, float Coverage01);

void ConstructOverviewInstruments(
    PixelSpace& Surface, float LeftX, float& InOutCursorY, float InnerWidth,
    InspectorPreviewSurface& SwathPreview,
    float Coverage01, float Extinction, float BaseMeters, float DriftScale,
    bool IsLocal) noexcept;

void ConstructCoverageHistogramCard(
    PixelSpace& Surface, float LeftX, float& InOutCursorY, float InnerWidth,
    float Coverage01) noexcept;

void ConstructDeckSectionOverlay(
    PixelSpace& Surface, Rectangle2D Box,
    InspectorPreviewSurface& DeckPreview,
    float BaseMeters, float MaxAltitudeMeters) noexcept;

} // namespace CloudVisualPrimitives

class CloudInspector
{
public:
    CloudInspector() noexcept = default;
    [[nodiscard]] static const char* QuerySubtitle() noexcept { return "Global cloud layer"; }

    [[nodiscard]] float Construct(
        PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
        OutlinerRecordEntry& Entry, CloudInstanceData& Instance,
        const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched);

    void SetActivePage(uint32_t PageIndex) noexcept { ActivePage = PageIndex; }

private:
    uint32_t                ActivePage            = 0u;
    uint32_t                ActiveDragId          = 0u;
    float                   DensityProbeAltitudeM = 1950.0f;
    bool                    ShadowsExpanded       = false;
    InspectorPreviewSurface SwathPreview          {};
    InspectorPreviewSurface DeckPreview           {};
};

} // namespace Frontier
