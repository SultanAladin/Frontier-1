/*============================================================================================================================================
                                                        POSTPROCESSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Post Process Inspector (2 tabs: Overview, Settings)

#include "Engine/Editor/Inspectors/PostProcessInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

float PostProcessInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PostProcessInstanceData& Instance,
    ViewportDisplaySettings* /*DisplaySettings*/,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Quick controls (1 tile: Enabled), Tags (post process + Add tag inline), Notes
        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Eye, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, WideX, CursorY, WideW, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            Instance.Enabled        = Entry.VisibilityEnabled;
            OutMutated              = true;
        }

        // Single-row Tags card (h = 106 matching post__0.json: tag + Add tag on same row)
        const Rectangle2D TagCard { WideX, CursorY, WideW, 106.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, TagCard, "Tags", "1", InspectorMetrics::MutedInk);
        const Rectangle2D Chip { BodyX, TagCard.Y + 52.0f, 96.0f, 26.0f };
        Surface.FillRectangle(Chip, InspectorMetrics::TagFill, 13.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Chip.X + 11.0f, Chip.Y + 6.5f }, 11.0f, InspectorMetrics::TagInk, "post process");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Chip.Right() - 14.0f, Chip.Y + 6.0f }, 11.0f, InspectorMetrics::TagInk.ScaledAlpha(0.65f), "x");
        const Rectangle2D AddPill { Chip.Right() + 8.0f, Chip.Y, 92.0f, 26.0f };
        Surface.StrokeRectangle(AddPill, ColorRgba{ 255u, 255u, 255u, 28u }, 13.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { AddPill.X + 12.0f, AddPill.Y + 6.5f }, 10.6f, InspectorMetrics::DimInk, "Add tag");
        CursorY = TagCard.Bottom() + InspectorMetrics::SectionGap;

        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, WideX, CursorY, WideW, Entry.AuthorNotes);
    }
    else
    {
        // TAB 2: SETTINGS — Card 1 Exposure (h=163), Card 2 Tone Mapping (h=235), Card 3 Lens Effects (h=235)
        const Rectangle2D ExpCard { WideX, CursorY, WideW, 163.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ExpCard, "Exposure");
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ExpCard.Y + 68.0f, BodyW, 8101u, "EV Compensation", "EV", "%.2g", -4.0f, 4.0f, Instance.ExposureEv, ActiveDragId, Input, OutMutated);
        CursorY = ExpCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D ToneCard { WideX, CursorY, WideW, 235.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ToneCard, "Tone Mapping");
        float TY = ToneCard.Y + 68.0f;
        TY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8102u, "Saturation", "", "%.2g", 0.0f, 2.0f, Instance.SaturationGain, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, TY, BodyW, 8103u, "Contrast", "", "%.2g", 0.5f, 2.0f, Instance.ContrastGain, ActiveDragId, Input, OutMutated);
        CursorY = ToneCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D LensCard { WideX, CursorY, WideW, 235.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, LensCard, "Lens Effects");
        float LY = LensCard.Y + 68.0f;
        LY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, LY, BodyW, 8104u, "Bloom Intensity", "", "%.2g", 0.0f, 1.0f, BloomIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, LY, BodyW, 8105u, "Vignette", "", "%.2g", 0.0f, 1.0f, Instance.VignetteStrength, ActiveDragId, Input, OutMutated);
        CursorY = LensCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
