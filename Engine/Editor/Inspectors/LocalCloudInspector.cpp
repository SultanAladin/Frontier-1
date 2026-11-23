/*============================================================================================================================================
                                                         LOCALCLOUDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LocalCloudInspector (bounded cloud volume extents, density & edge softness)

#include "Engine/Editor/Inspectors/LocalCloudInspector.h"

#include <array>
#include <cstdio>

namespace Frontier
{

float LocalCloudInspector::Construct(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    LocalCloudInstanceData& Instance,
    const PaneInputReading& Input,
    bool&                   OutMutated,
    bool&                   OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Coverage", "Layer", "Body" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);

    auto& Vol = Instance.Volume;
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        char Cov[16]{}, Opt[16]{}, Alt[16]{}, Soft[16]{};
        std::snprintf(Cov,  sizeof(Cov),  "%.0f", Vol.CloudFraction * 100.0f);
        std::snprintf(Opt,  sizeof(Opt),  "%.1f", Vol.ExtinctionMultiplier);
        std::snprintf(Alt,  sizeof(Alt),  "%.0f", Vol.VolumeCenterMeters.Z);
        std::snprintf(Soft, sizeof(Soft), "%.2f", Vol.EdgeSoftness);
        const std::array<InspectorKpiMetricSpec, 4u> Kpis = {{
            { Cov,  "%", "FILL" },
            { Opt,  "x", "OPTICAL" },
            { Alt,  "m", "ALTITUDE" },
            { Soft, "",  "SOFTNESS" },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Kpis);

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Cloud, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "local-cloud", "volume" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 210.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume fill");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Pct[16]{}; std::snprintf(Pct, sizeof(Pct), "%.0f", Vol.CloudFraction * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Pct, "%", "Bounded cloud fill fraction");
        float PctVal = Vol.CloudFraction * 100.0f; bool Changed = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y + 4.0f, BodyW, 3601u, "Fill", "%", "%.0f", 0.0f, 100.0f, PctVal, ActiveDragId, Input, Changed);
        if (Changed) { Vol.CloudFraction = PctVal * 0.01f; OutMutated = true; }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume half-extents");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3610u, "Extent X", "m", "%.0f", 10.0f, 4000.0f, Vol.HalfExtentMeters.X, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3611u, "Extent Y", "m", "%.0f", 10.0f, 4000.0f, Vol.HalfExtentMeters.Y, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3612u, "Extent Z", "m", "%.0f", 10.0f, 2000.0f, Vol.HalfExtentMeters.Z, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Volume density");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3620u, "Extinction", "x", "%.2f", 0.0f, 4.0f, Vol.ExtinctionMultiplier, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 3621u, "Edge softness", "", "%.2f", 0.0f, 1.0f, Vol.EdgeSoftness, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
