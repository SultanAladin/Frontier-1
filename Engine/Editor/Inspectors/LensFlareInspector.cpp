/*============================================================================================================================================
                                                          LENSFLAREINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LensFlareInspector

#include "Engine/Editor/Inspectors/LensFlareInspector.h"

#include <array>

namespace Frontier
{

float LensFlareInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LensFlareInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Optics" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;
    auto& Flare = Instance.Flare;

    if (ActivePage == 0u)
    {
        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::PostFx, Entry.VisibilityEnabled && Flare.Enabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Flare.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lens-flare", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 326.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Optical lens flare");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        static constexpr std::array<const char*, 3u> Styles = {{ "Anamorphic", "Spherical", "Clean" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Optical style", Styles, static_cast<uint32_t>(Flare.Style), 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 3) { Flare.Style = static_cast<GeometricRaster::LensFlareStyle>(Chosen); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7051u, "Intensity", "x", "%.2f", 0.0f, 3.0f, Flare.FlareIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7052u, "Halo glow", "x", "%.2f", 0.0f, 2.0f, Flare.HaloIntensity, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7053u, "Ghost elements", "x", "%.2f", 0.0f, 2.0f, Flare.GhostIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7054u, "Streak angle", "deg", "%.0f", -90.0f, 90.0f, Flare.StreakRotationDegrees, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

} // namespace Frontier
