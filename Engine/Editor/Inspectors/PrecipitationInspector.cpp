/*============================================================================================================================================
                                                       PRECIPITATIONINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance PrecipitationInspector

#include "Engine/Editor/Inspectors/PrecipitationInspector.h"

#include <array>
#include <cstdio>

namespace Frontier
{

float PrecipitationInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PrecipitationInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Weather" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    static constexpr std::array<const char*, 3u> Modes = {{ "Rain", "Snow", "Hail" }};

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 168.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Precipitation rate");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Rate[16]{}; std::snprintf(Rate, sizeof(Rate), "%.0f", Instance.RateMultiplier * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Rate, "%", Modes[Instance.ModeIndex % 3u]);
        int32_t Chosen = -1;
        (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, "Hydrometeor", Modes, Instance.ModeIndex, 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Instance.ModeIndex = static_cast<uint32_t>(Chosen); OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Cloud, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "precipitation", "weather" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Hydrometeor dynamics");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6501u, "Fall rate", "x", "%.2f", 0.0f, 2.0f, Instance.RateMultiplier, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6502u, "Drop diameter", "mm", "%.1f", 0.3f, 8.0f, Instance.DropSizeMm, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6503u, "Wind slant", "x", "%.2f", 0.0f, 2.0f, Instance.WindSlantScale, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
