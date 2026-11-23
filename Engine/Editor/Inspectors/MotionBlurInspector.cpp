/*============================================================================================================================================
                                                         MOTIONBLURINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance MotionBlurInspector

#include "Engine/Editor/Inspectors/MotionBlurInspector.h"

#include <array>
#include <cstdio>

namespace Frontier
{

float MotionBlurInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, MotionBlurInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Shutter" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 128.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Shutter exposure");
        char Deg[16]{}; std::snprintf(Deg, sizeof(Deg), "%.0f", Instance.ShutterAngleDegrees);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, Deg, "deg", "180 deg = standard cinema half-frame");
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Cycle, Entry.VisibilityEnabled && Instance.Enabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "motion-blur", "shutter" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Shutter dynamics");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7061u, "Shutter angle", "deg", "%.0f", 15.0f, 360.0f, Instance.ShutterAngleDegrees, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7062u, "Velocity scale", "x", "%.2f", 0.0f, 3.0f, Instance.VelocityScale, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7063u, "Max radius", "px", "%.0f", 2.0f, 64.0f, Instance.MaxSampleRadiusPx, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

} // namespace Frontier
