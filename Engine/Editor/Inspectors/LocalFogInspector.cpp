/*============================================================================================================================================
                                                          LOCALFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LocalFogInspector

#include "Engine/Editor/Inspectors/LocalFogInspector.h"

#include <array>
#include <cstdio>

namespace Frontier
{

float LocalFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LocalFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Density", "Light" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Fog = Instance.LocalFog;
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Fog, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Fog.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "fog", "local-volume" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Local fog density");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6301u, "Density", "x", "%.2f", 0.0f, 4.0f, Fog.Density, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6302u, "Edge softness", "", "%.2f", 0.0f, 1.0f, Fog.EdgeSoftness, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6303u, "Half extent X", "m", "%.1f", 1.0f, 400.0f, Fog.HalfExtentMeters.X, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 162.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Local fog tint");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 6310u, "Volume tint", Fog.FogTint, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
