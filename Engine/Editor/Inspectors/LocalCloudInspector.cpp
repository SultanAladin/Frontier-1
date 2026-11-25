/*============================================================================================================================================
                                                         LOCALCLOUDINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Local Cloud Inspector (4 tabs: Overview, Coverage, Layer, Body)

#include "Engine/Editor/Inspectors/LocalCloudInspector.h"
#include "Engine/Editor/Inspectors/CloudInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float LocalCloudInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LocalCloudInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Coverage", "Layer", "Body" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Vol = Instance.Volume;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    const float BaseMeters      = std::max(0.0f, Vol.VolumeCenterMeters.Z - Vol.HalfExtentMeters.Z);
    const float ThicknessMeters = std::max(1.0f, Vol.HalfExtentMeters.Z * 2.0f);

    if (ActivePage == 0u)
    {
        CloudVisualPrimitives::UpdateSwathPreview(SwathPreview, Vol.CloudFraction, Vol.ExtinctionMultiplier, true);
        CloudVisualPrimitives::ConstructOverviewInstruments(
            Surface, LeftX, CursorY, InnerWidth, SwathPreview,
            Vol.CloudFraction, Vol.ExtinctionMultiplier, BaseMeters, 1.0f, true);

        const std::array<InspectorQuickSwitchSpec, 1u> Quick = {{
            { "Enabled", InspectorSwitchSymbol::Cloud, Entry.VisibilityEnabled, false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            Vol.LocalEnabled        = Entry.VisibilityEnabled;
            OutMutated              = true;
        }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "clouds" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        CloudVisualPrimitives::ConstructCoverageHistogramCard(Surface, LeftX, CursorY, InnerWidth, Vol.CloudFraction);

        const Rectangle2D SliderCard { LeftX, CursorY, InnerWidth, 70.0f };
        Surface.FillRectangle(SliderCard, ColorRgba{ 27u, 27u, 27u, 255u }, InspectorMetrics::SectionRadius);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + 16.0f, SliderCard.Y + 10.0f, InnerWidth - 32.0f, 6601u, "Coverage", "", "%.2g", 0.0f, 1.0f, Vol.CloudFraction, ActiveDragId, Input, OutMutated);
        CursorY = SliderCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const float MaxVisAlt = std::max(500.0f, BaseMeters + ThicknessMeters + 200.0f);
        CloudVisualPrimitives::UpdateDeckSectionPreview(DeckPreview, BaseMeters, ThicknessMeters, MaxVisAlt, Vol.CloudFraction);

        // Card 1: Cloud base (h = 346, no Base slider since derived from Centre Z - Half Height)
        const Rectangle2D BaseCard { LeftX, CursorY, InnerWidth, 346.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, BaseCard, "Cloud base");

        char BaseKm[16]{};
        std::snprintf(BaseKm, sizeof(BaseKm), "%.2f", BaseMeters / 1000.0f);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, BaseCard.Y + 65.0f, BaseKm, "km", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, BaseCard.Y + 118.0f }, 12.0f, InspectorMetrics::MutedInk, "Lower bound \xC2\xB7 centre Z - half height");

        const Rectangle2D DeckBox { BodyX, BaseCard.Y + 148.0f, BodyW, 178.0f };
        CloudVisualPrimitives::ConstructDeckSectionOverlay(Surface, DeckBox, DeckPreview, BaseMeters, MaxVisAlt);

        CursorY = BaseCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Layer thickness (h = 504)
        const Rectangle2D ThickCard { LeftX, CursorY, InnerWidth, 504.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ThickCard, "Layer thickness");

        char ThickKm[16]{};
        std::snprintf(ThickKm, sizeof(ThickKm), "%.2f", ThicknessMeters / 1000.0f);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, ThickCard.Y + 65.0f, ThickKm, "km", nullptr, 36.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, ThickCard.Y + 118.0f }, 12.0f, InspectorMetrics::MutedInk, "Bounded depth \xC2\xB7 twice half height");

        const float Ext = Vol.ExtinctionMultiplier;
        const std::array<InspectorPrimitives::InspectorLivePlotSeries, 1u> Series = {{
            {
                "Density study",
                ColorRgba{ 168u, 196u, 212u, 255u },
                [BaseMeters, ThicknessMeters, Ext](float AltM) noexcept -> float
                {
                    if (AltM < BaseMeters || AltM > BaseMeters + ThicknessMeters) return 0.0f;
                    const float U = (AltM - BaseMeters) / ThicknessMeters;
                    return Ext * std::sin(U * 3.14159265f);
                },
            },
        }};
        (void)InspectorPrimitives::ConstructLivePlot(
            Surface, BodyX, ThickCard.Y + 150.0f, BodyW, 6602u,
            0.0f, MaxVisAlt, 0.0f, 4.0f, Series, DensityProbeAltitudeM,
            "m Z", "\xC3\x97", "", ActiveDragId, Input, OutMutated);

        CursorY = ThickCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 3: Shape
        const Rectangle2D ShapeCard { LeftX, CursorY, InnerWidth, 232.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ShapeCard, "Shape");
        float SY = ShapeCard.Y + 70.0f;
        SY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY, BodyW, 6603u, "Yaw",           "deg", "%.0f", 0.0f, 360.0f, Vol.YawDegrees,   ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, SY, BodyW, 6604u, "Edge softness", "",    "%.2g", 0.0f,   1.0f, Vol.EdgeSoftness, ActiveDragId, Input, OutMutated);
        CursorY = ShapeCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 4: BODY — Cloud body (h = 307)
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 307.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cloud body");

        float FY = S.Y + 70.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6610u, "Density",       "\xC3\x97", "%.2g",  0.0f,   4.0f, Vol.ExtinctionMultiplier,     ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6611u, "Feature Scale", "m",        "%.0f",  5.0f, 300.0f, Vol.FeatureScaleMeters,       ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6612u, "Anisotropy",   "",         "%.2g", -0.8f,   0.9f, Vol.ForwardScatterAnisotropy, ActiveDragId, Input, OutMutated);

        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
