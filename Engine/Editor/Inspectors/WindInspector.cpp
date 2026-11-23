/*============================================================================================================================================
                                                             WINDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance WindInspector (vector-summed atmospheric wind fields)

#include "Engine/Editor/Inspectors/WindInspector.h"

#include "Engine/Components/Controls/PolarDirectionDial.h"

#include <array>
#include <cstdio>

namespace Frontier
{

float WindInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    WindInstanceData&       Instance,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Air" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Wind = Instance.Wind;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 240.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Prevailing wind");
        char Spd[16]{}; std::snprintf(Spd, sizeof(Spd), "%.1f", Wind.SpeedMetersPerSecond);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, Spd, "m/s", PolarDirectionDial::ResolveCompassLabel(Wind.BearingDegrees));
        float DummyEl = 45.0f; bool DialChanged = false;
        PolarDirectionDial::Construct(Surface, { S.Right() - PadX - 50.0f, CursorY + 132.0f }, 46.0f, Wind.BearingDegrees, 45.0f, false, Input, Wind.BearingDegrees, DummyEl, DialChanged, true);
        if (DialChanged) OutMutated = true;
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",   InspectorSwitchSymbol::Wind, Entry.VisibilityEnabled, false },
            { "Air shear", InspectorSwitchSymbol::Wind, Wind.AirShearAdvection,  false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Wind.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Wind.AirShearAdvection = !Wind.AirShearAdvection; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "wind", "advection" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 408.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Wind vector descriptors");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6001u, "Wind speed", "m/s", "%.1f", 0.0f, 40.0f, Wind.SpeedMetersPerSecond, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6002u, "Bearing", "deg", "%.0f", 0.0f, 360.0f, Wind.BearingDegrees, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6003u, "Vertical shear", "/km", "%.2f", 0.0f, 2.0f, Wind.ShearPerKilometer, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6004u, "Ekman veer", "deg", "%.1f", -60.0f, 60.0f, Wind.VeerDegreesPerKm, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6005u, "Gust strength", "", "%.2f", 0.0f, 1.0f, Wind.GustStrength, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 6006u, "Turbulence", "", "%.2f", 0.0f, 1.0f, Wind.Turbulence, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
