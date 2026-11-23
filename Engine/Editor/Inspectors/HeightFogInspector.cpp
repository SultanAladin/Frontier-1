/*============================================================================================================================================
                                                         HEIGHTFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance HeightFogInspector

#include "Engine/Editor/Inspectors/HeightFogInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float HeightFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, HeightFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Density", "Light" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Fog = Instance.HeightFog;
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 224.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Height extinction profile");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Buf[20]{}; std::snprintf(Buf, sizeof(Buf), "%.0f", Fog.FalloffHeightMeters);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Buf, "m", "Exponential altitude falloff");
        std::array<float, 28u> Curve{};
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float Alt = (static_cast<float>(I) / 27.0f) * 2000.0f;
            Curve[I] = std::exp(-Alt / std::max(10.0f, Fog.FalloffHeightMeters));
        }
        InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve, ColorRgba{ 168u, 202u, 232u, 255u }, ColorRgba{ 138u, 174u, 212u, 42u }, "Datum", "2 km");
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",     InspectorSwitchSymbol::Fog,  Entry.VisibilityEnabled, false },
            { "Follow wind", InspectorSwitchSymbol::Wind, Fog.FollowWind,          false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Fog.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Fog.FollowWind = !Fog.FollowWind; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "fog", "height" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Height fog density");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6101u, "Density", "/m", "%.3f", 0.0f, 0.2f, Fog.Density, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6102u, "Falloff height", "m", "%.0f", 10.0f, 3000.0f, Fog.FalloffHeightMeters, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6103u, "Datum altitude", "m", "%.0f", -200.0f, 2000.0f, Fog.BaseAltitudeMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 214.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Height fog light");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6104u, "Sun scatter", "x", "%.2f", 0.0f, 2.0f, Fog.SunScatterIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 6110u, "Fog tint", Fog.HeightColour, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
