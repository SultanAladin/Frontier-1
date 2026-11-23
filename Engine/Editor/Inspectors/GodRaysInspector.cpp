/*============================================================================================================================================
                                                           GODRAYSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance GodRaysInspector

#include "Engine/Editor/Inspectors/GodRaysInspector.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

float GodRaysInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, GodRaysInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Optics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Rays = Instance.GodRays;
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",           InspectorSwitchSymbol::Sun,   Entry.VisibilityEnabled, false },
            { "Cloud interaction", InspectorSwitchSymbol::Cloud, Rays.InteractWithClouds, false },
            { "Fog interaction",   InspectorSwitchSymbol::Fog,   Rays.InteractWithFog,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Rays.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Rays.InteractWithClouds = !Rays.InteractWithClouds; OutMutated = true; }
        else if (Clicked == 2) { Rays.InteractWithFog = !Rays.InteractWithFog; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "god-rays", "shafts" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Crepuscular shafts");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        std::array<float, 28u> Curve{};
        const float G = std::clamp(Rays.AnisotropyG, 0.0f, 0.95f);
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float CosT = 1.0f - 2.0f * (static_cast<float>(I) / 27.0f);
            Curve[I] = std::clamp(std::log2(1.0f + (1.0f - G * G) / std::pow(1.0f + G * G - 2.0f * G * CosT, 1.5f)) / 5.0f, 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 64.0f }, Curve, ColorRgba{ 246u, 214u, 146u, 255u }, ColorRgba{ 246u, 198u, 118u, 38u }, "Sunward", "Anti-solar");
        Y += 76.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6401u, "Intensity", "x", "%.2f", 0.0f, 4.0f, Rays.Intensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6402u, "Anisotropy", "g", "%.2f", 0.0f, 0.95f, Rays.AnisotropyG, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6403u, "Density", "x", "%.2f", 0.0f, 3.0f, Rays.Density, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 6410u, "Shaft tint", Rays.ShaftTint, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
