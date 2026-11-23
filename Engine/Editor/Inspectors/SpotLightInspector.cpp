/*============================================================================================================================================
                                                         SPOTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance SpotLightInspector with own retained beam-envelope diagram

#include "Engine/Editor/Inspectors/SpotLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

} // namespace



float SpotLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, SpotLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Beam", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float BeamDiam5m = 2.0f * 5.0f * std::tan(Instance.FullConeDegrees * PiF / 360.0f);

    if (ActivePage == 0u)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const Mathematics::LinearRGB EffTint { Instance.RadiantTint.R * K.R, Instance.RadiantTint.G * K.G, Instance.RadiantTint.B * K.B };
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::SpotLight, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffTint),
            "Beam envelope", "Spot");
        float HY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WBuf[20]{}, FBuf[8]{}, CapBuf[64]{};
        std::snprintf(WBuf, sizeof(WBuf), "%.0f", std::floor(std::max(0.0f, Instance.IntensityCandela)));
        std::snprintf(FBuf, sizeof(FBuf), ".%d", static_cast<int>(std::lround((Instance.IntensityCandela - std::floor(Instance.IntensityCandela)) * 10.0f)) % 10);
        std::snprintf(CapBuf, sizeof(CapBuf), "%.0f° cone · %.0f%% soft edge", Instance.FullConeDegrees, Instance.Penumbra * 100.0f);
        HY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, HY, WBuf, FBuf, "cd", CapBuf, 30.0f);
        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind = InspectorVector::LightBlueprintKind::Spot;
        Bp.TintRgb = EffTint;
        Bp.SpreadDegrees = Instance.FullConeDegrees;
        Bp.InnerDegrees  = Instance.FullConeDegrees * (1.0f - std::clamp(Instance.Penumbra, 0.0f, 0.95f));
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, HY + 2.0f, BodyW, 158.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char CdBuf[20]{}, ConeBuf[20]{}, PenBuf[20]{}, DiamBuf[20]{};
        std::snprintf(CdBuf,   sizeof(CdBuf),   "%.1f", Instance.IntensityCandela);
        std::snprintf(ConeBuf, sizeof(ConeBuf), "%.1f", Instance.FullConeDegrees);
        std::snprintf(PenBuf,  sizeof(PenBuf),  "%.0f", Instance.Penumbra * 100.0f);
        std::snprintf(DiamBuf, sizeof(DiamBuf), "%.2f", BeamDiam5m);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { CdBuf,   "cd",  "INTENSITY" },
            { ConeBuf, "deg", "FULL CONE" },
            { PenBuf,  "%",   "SOFT EDGE" },
            { DiamBuf, "m",   "AT 5 M"    },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Cast shadows", InspectorSwitchSymbol::Shadow,    Entry.CastShadows,       false },
            { "Draw cone",    InspectorSwitchSymbol::Sun,       Instance.ShowCone,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowCone = !Instance.ShowCone; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "spot" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 294.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Source & colour");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char CdBuf[20]{}; std::snprintf(CdBuf, sizeof(CdBuf), "%.1f", Instance.IntensityCandela);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, CdBuf, "cd", "Authored luminous intensity");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8210u, "Intensity", "cd", "%.1f", 0.0f, 200.0f, Instance.IntensityCandela, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8211u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8212u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 314.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Beam shaping");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char DiamBuf[20]{}; std::snprintf(DiamBuf, sizeof(DiamBuf), "%.2f", BeamDiam5m);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, DiamBuf, "m", "Beam diameter at 5 m");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8220u, "Full cone angle", "deg", "%.1f", 2.0f, 80.0f, Instance.FullConeDegrees, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8221u, "Penumbra", "", "%.2f", 0.0f, 1.0f, Instance.Penumbra, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8222u, "Reach", "m", "%.0f", 1.0f, 150.0f, Instance.ReachMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8223u, "Lens radius", "m", "%.2f", 0.02f, 0.8f, Instance.SourceRadiusMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8240u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, true, "Draw cone", Instance.ShowCone, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
