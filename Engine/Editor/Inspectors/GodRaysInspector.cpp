/*============================================================================================================================================
                                                          GODRAYSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of God Rays Inspector (2 tabs: Overview, Settings)

#include "Engine/Editor/Inspectors/GodRaysInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

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
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Rays = Instance.GodRays;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Quick controls (3 tiles: Enabled, Cloud interaction, Fog interaction), Tags, Notes
        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",           InspectorSwitchSymbol::Sun,   Entry.VisibilityEnabled, false },
            { "Cloud interaction", InspectorSwitchSymbol::Cloud, Rays.InteractWithClouds, false },
            { "Fog interaction",   InspectorSwitchSymbol::Fog,   Rays.InteractWithFog,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            Rays.Enabled            = Entry.VisibilityEnabled;
            OutMutated              = true;
        }
        else if (Clicked == 1)
        {
            Rays.InteractWithClouds = !Rays.InteractWithClouds;
            OutMutated              = true;
        }
        else if (Clicked == 2)
        {
            Rays.InteractWithFog = !Rays.InteractWithFog;
            OutMutated           = true;
        }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "light" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        // TAB 2: SETTINGS — Card 1 Shafts (h=379), Card 2 March (h=235), Card 3 Interaction (Shaft tint)
        const Rectangle2D ShaftsCard { LeftX, CursorY, InnerWidth, 379.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ShaftsCard, "Shafts");
        float FY = ShaftsCard.Y + 70.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6701u, "Intensity",  "\xC3\x97", "%.2g", 0.0f, 4.0f,  Rays.Intensity,   ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6702u, "Anisotropy", "g",        "%.2g", 0.0f, 0.95f, Rays.AnisotropyG, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6703u, "Decay",      "",         "%.2g", 0.0f, 1.0f,  Rays.Decay,       ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6704u, "Density",   "\xC3\x97", "%.2g", 0.0f, 3.0f,  Rays.Density,     ActiveDragId, Input, OutMutated);
        CursorY = ShaftsCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D MarchCard { LeftX, CursorY, InnerWidth, 235.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, MarchCard, "March");
        float MY = MarchCard.Y + 70.0f;
        MY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 6705u, "Maximum distance", "m", "%.0f", 10.0f, 2000.0f, Rays.MaxDistanceMeters, ActiveDragId, Input, OutMutated);
        float StepsF = static_cast<float>(Rays.SampleSteps);
        bool StepsChanged = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, MY, BodyW, 6706u, "Steps", "", "%.0f", 8.0f, 48.0f, StepsF, ActiveDragId, Input, StepsChanged);
        if (StepsChanged)
        {
            Rays.SampleSteps = static_cast<uint32_t>(std::clamp(std::lround(StepsF), 8l, 48l));
            OutMutated       = true;
        }
        CursorY = MarchCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D TintCard { LeftX, CursorY, InnerWidth, 148.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, TintCard, "Interaction");
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, BodyX, TintCard.Y + 70.0f, BodyW, 6707u, "Shaft tint", Rays.ShaftTint, ActiveDragId, Input, OutMutated);
        CursorY = TintCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
