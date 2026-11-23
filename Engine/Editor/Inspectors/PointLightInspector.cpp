/*============================================================================================================================================
                                                         POINTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance PointLightInspector with own retained radial falloff diagram

#include "Engine/Editor/Inspectors/PointLightInspector.h"

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



float PointLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PointLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Attenuation", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float Lux5m = Instance.IntensityCandela / std::pow(5.0f, std::max(0.01f, Instance.DecayExponent));

    if (ActivePage == 0u)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const Mathematics::LinearRGB EffTint { Instance.RadiantTint.R * K.R, Instance.RadiantTint.G * K.G, Instance.RadiantTint.B * K.B };
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::PointLight, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffTint),
            "Radial falloff", "Point");
        float HY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WBuf[20]{}, FBuf[8]{}, CapBuf[64]{};
        std::snprintf(WBuf, sizeof(WBuf), "%.0f", std::floor(std::max(0.0f, Instance.IntensityCandela)));
        std::snprintf(FBuf, sizeof(FBuf), ".%d", static_cast<int>(std::lround((Instance.IntensityCandela - std::floor(Instance.IntensityCandela)) * 10.0f)) % 10);
        std::snprintf(CapBuf, sizeof(CapBuf), "Omnidirectional · decay %.1f · %.0f K", Instance.DecayExponent, Instance.KelvinTemperature);
        HY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, HY, WBuf, FBuf, "cd", CapBuf, 30.0f);
        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind = InspectorVector::LightBlueprintKind::Point;
        Bp.TintRgb = EffTint;
        Bp.WidthMeters = Instance.ReachMeters;
        Bp.FalloffPower = Instance.DecayExponent;
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, HY + 2.0f, BodyW, 158.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char CdBuf[20]{}, LuxBuf[20]{}, ReachBuf[20]{}, RadBuf[20]{};
        std::snprintf(CdBuf,    sizeof(CdBuf),    "%.1f", Instance.IntensityCandela);
        std::snprintf(LuxBuf,   sizeof(LuxBuf),   "%.2f", Lux5m);
        std::snprintf(ReachBuf, sizeof(ReachBuf), "%.0f", Instance.ReachMeters);
        std::snprintf(RadBuf,   sizeof(RadBuf),   "%.0f", Instance.SourceRadiusMeters * 1000.0f);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { CdBuf,    "cd", "INTENSITY" },
            { LuxBuf,   "lx", "AT 5 M"    },
            { ReachBuf, "m",  "REACH"     },
            { RadBuf,   "mm", "BULB RAD"  },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Show glow",    InspectorSwitchSymbol::Sun,       Instance.ShowGlow,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.ShowGlow = !Instance.ShowGlow; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "point" }};
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
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8110u, "Intensity", "cd", "%.1f", 0.0f, 60.0f, Instance.IntensityCandela, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8111u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8112u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Attenuation & distance response");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        std::array<float, 32u> Curve {};
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float Dist = 1.0f + (static_cast<float>(I) / 31.0f) * 9.0f;
            Curve[I] = std::clamp(1.0f / std::pow(Dist, std::max(0.05f, Instance.DecayExponent)), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(
            Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve,
            ColorRgba{ 246u, 206u, 128u, 255u }, ColorRgba{ 246u, 206u, 128u, 32u }, "1 m", "10 m");
        Y += 86.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8120u, "Reach", "m", "%.0f", 1.0f, 120.0f, Instance.ReachMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8121u, "Decay exponent", "", "%.2f", 0.0f, 4.0f, Instance.DecayExponent, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8122u, "Source radius", "m", "%.2f", 0.01f, 1.0f, Instance.SourceRadiusMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8140u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Show glow", Instance.ShowGlow, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
