/*============================================================================================================================================
                                                       ATMOSPHERICFOGINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance AtmosphericFogInspector

#include "Engine/Editor/Inspectors/AtmosphericFogInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float AtmosphericFogInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AtmosphericFogInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Density", "Light" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Fog = Instance.AerialFog;
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 224.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Aerial perspective curve");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Buf[20]{}; std::snprintf(Buf, sizeof(Buf), "%.0f", Fog.StartDistanceMeters);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Buf, "m", "Aerial onset distance");
        std::array<float, 28u> Curve{};
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float Dist = (static_cast<float>(I) / 27.0f) * 12000.0f;
            const float Eff  = std::max(0.0f, Dist - Fog.StartDistanceMeters);
            Curve[I] = 1.0f - std::exp(-Eff * 0.00025f * std::max(0.05f, Fog.DensityMultiplier));
        }
        InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve, ColorRgba{ 146u, 186u, 242u, 255u }, ColorRgba{ 118u, 156u, 220u, 42u }, "0 km", "12 km");
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",     InspectorSwitchSymbol::Fog,  Entry.VisibilityEnabled, false },
            { "Follow wind", InspectorSwitchSymbol::Wind, Fog.FollowWind,          false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Fog.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Fog.FollowWind = !Fog.FollowWind; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "fog", "aerial" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Aerial density");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6201u, "Density multiplier", "x", "%.2f", 0.0f, 4.0f, Fog.DensityMultiplier, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6202u, "Start distance", "m", "%.0f", 0.0f, 2000.0f, Fog.StartDistanceMeters, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6203u, "Scale height", "m", "%.0f", 200.0f, 8000.0f, Fog.ScaleHeightMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 162.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Aerial scatter");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6204u, "Mie blend", "", "%.2f", 0.0f, 1.0f, Fog.MieBlend, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
