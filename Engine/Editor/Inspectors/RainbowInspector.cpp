/*============================================================================================================================================
                                                           RAINBOWINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance RainbowInspector

#include "Engine/Editor/Inspectors/RainbowInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float RainbowInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, RainbowInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Optics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 224.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Spectral dispersion");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Buf[16]{}; std::snprintf(Buf, sizeof(Buf), "%.0f", Instance.DropletRadiusUm);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Buf, "um", "Mean droplet radius (42 deg primary)");
        std::array<float, 28u> Curve{};
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float T = static_cast<float>(I) / 27.0f;
            const float Pri = std::exp(-std::pow((T - 0.35f) * 8.0f, 2.0f)) * Instance.PrimaryIntensity;
            const float Sec = std::exp(-std::pow((T - 0.72f) * 7.0f, 2.0f)) * Instance.SecondaryIntensity;
            Curve[I] = std::clamp((Pri + Sec) * 0.55f, 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve, ColorRgba{ 196u, 224u, 255u, 255u }, ColorRgba{ 168u, 198u, 245u, 38u }, "40 deg", "54 deg");
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Sun, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "rainbow", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 290.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Optical bow parameters");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6601u, "Primary bow", "x", "%.2f", 0.0f, 2.0f, Instance.PrimaryIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6602u, "Secondary bow", "x", "%.2f", 0.0f, 1.0f, Instance.SecondaryIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6603u, "Supernumerary", "", "%.2f", 0.0f, 1.0f, Instance.SupernumeraryWidth, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6604u, "Droplet radius", "um", "%.0f", 50.0f, 1500.0f, Instance.DropletRadiusUm, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
